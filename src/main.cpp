#include <Arduino.h>
#include <ESP8266WiFi.h>          //ESP8266 Core WiFi Library (you most likely already have this in your sketch)

#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include <IPAddress.h>
#include <WiFiUdp.h>
#include <ESP_EEPROM.h>
#include <time.h>
#include <sys/time.h>
#include <coredecls.h>
#include <SPI.h>
#include <ArduinoOTA.h>
#include <RotaryEncoder.h>
#include <JC_Button.h>

// Disables the writing to "EEPROM" (flash) for rapid testing/debugging.
#define DISABLE_EEPROM_WRITES 1

/*
 * Pin allocations:
 * Tx     / D1  - H -N/A-
 * Rx     / D3  - Encoder A
 * GPIO5  / D5  - Encoder B
 * GPIO4  / D4  - Encoder SW
 * GPIO0  / D0  - H Radio SDA
 * GPIO2  / D2  - H Radio SCL
 * GPIO15 / D15 - L LED ~CS
 * GPIO13 / D13 - LED MOSI
 * GPIO12 / D12 - LED MISO - Unused
 * GPIO14 / D14 - LED SCLK
 * GPIO16 / D16 - Buzzer
 */

// The object used to send UDP debug messages.
WiFiUDP udp;

// The server to which debug UDP messages are sent.
const IPAddress DEBUG_SERVER = IPAddress(10, 0, 1, 253);

// The port on the server to which debug UDP messages are sent.
const uint16_t DEBUG_PORT = 65432;

// The pin used for the rotary encoder "A" input.
const uint8_t ENCODER_A_PIN = 3;

// The pin used for the rotary encoder "B" input.
const uint8_t ENCODER_B_PIN = 5;

// The pin used for the rotary encoder press switch input.
const uint8_t ENCODER_SW_PIN = 4;

// The pin used for the LED controller's CS input.
const uint8_t LED_CS_PIN = 15;

// The pin used for the alarm buzzer.
const uint8_t BUZZER_PIN = 16;

// The amount of time (in seconds) to wait for a network connections before 
// starting the configuration server,
const unsigned long CONFIG_TIMEOUT = 5 * 60;

// The amount of time (in milliseconds) to wait after a click for a 
// double-click to be triggered.
const uint32_t DOUBLE_CLICK_INTERVAL = 300;

// THe number of milliseconds to wait between loop executions.
const uint32_t LOOP_DELAY = 10;

// The number of loops to go through before the show alarm state ends.
const uint32_t SHOW_ALARM_COUNTDOWN = 3 * (1000 / LOOP_DELAY);

// The number of loops before the flash value inverts.
const uint32_t FLASH_COUNT_MAX = 0.5 * (1000 / LOOP_DELAY);

// The minimum tunable radio frequency (Hz).
const uint16_t MIN_RADIO_FREQUENCY = 88;

// The maximum tunable radio frequency (Hz).
const uint16_t MAX_RADIO_FREQUENCY = 107;

// The maximum brightness for the LEDs.
const uint8_t MAX_BRIGHTNESS = 0x0F;

// The value to use for a dash (negative) digit.
const uint8_t DIGIT_DASH = 16;

// The value to use for a "r" digit.
const uint8_t DIGIT_R = 17;

// The value to use for a "H" digit.
const uint8_t DIGIT_H = 18;

// The value to use for a "i" digit.
const uint8_t DIGIT_I = 19;

// The value to use for a "L" digit.
const uint8_t DIGIT_L = 20;

// The value to use for a blank (empty) digit.
const uint8_t DIGIT_BLANK = 21;

// The number of hours in a day.
const uint8_t HOURS_PER_DAY = 24;

// The number of minutes in an hour.
const uint8_t MINUTES_PER_HOUR = 60;

const uint8_t FONT[] = {0x7E, // 0
                        0x30, // 1
                        0x6D, // 2
                        0x79, // 3
                        0x33, // 4
                        0x5B, // 5
                        0x5F, // 6
                        0x70, // 7
                        0x7F, // 8
                        0x7B, // 9
                        0x77, // A
                        0x1F, // b
                        0x0D, // c
                        0x3D, // d
                        0x4F, // E
                        0x47, // F
                        0x01, // -
                        0x05, // r
                        0x37, // H
                        0x10, // i
                        0x0E, // L
                        0x00  // (blank)
                        };

// The states that the clock may be in.
typedef enum {
    INITIALISING,
    RUNNING,
    SHOW_ALARM,
    MENU_ALARM_HOURS,
    MENU_ALARM_MINUTES,
    MENU_ALARM_DAYS,
    MENU_RADIO_WHOLE,
    MENU_RADIO_FRACTION,
    MENU_12_24_HOURS,
    MENU_BRIGHTNESS,
    MENU_TYPE
} state_t;

// The setting values for the alarm.
typedef enum {
    DISABLED,
    WEEKDAYS,
    ALL_DAYS,
    TOMORROW
} alarm_t;

// The configuration for the clock.
typedef struct {
    uint32_t alarmTime;
    alarm_t alarmStatus;
    uint16_t radioFrequency;
    uint8_t brightness;
    bool isLargeLED : 1;
    bool is24Hour : 1;
    bool isUseRadio : 1;
} flash_config_t;

// The current operating state of the clock.
state_t myState = state_t::INITIALISING;

// The configuration for this clock.
flash_config_t myConfiguration;

// The new configuration values while the user is editing it in the menu.
flash_config_t myNewConfiguration;

// Timer used for counting down to an event.
uint32_t myCountdownTimer = 0;

// Counter used to control digit flashing.
int32_t myFlashCounter = -1;

// The current state of the flash, true = on.
boolean myFlashValue = true;

// The last timestamp that we processed.
time_t myLastTimestamp = 0;

// Flag used to force the updating of the display.
boolean myForceRedisplay = true;

// Reads the position of the encoder.
RotaryEncoder myEncoder(ENCODER_A_PIN, ENCODER_B_PIN);

// The last encoder position.
uint32_t myLastPosition = -999;

// The button on the encoder.
Button myButton(ENCODER_SW_PIN);

// Flag to indicate that the button has been pressed.
// This allows us to differentiate single and double clicks.
boolean myWasPressed = false;

// Flag to indicate that the button has been held long enough to register as a
// long press.
boolean myIsLongPress = false;

/*
 * Sends debugging information to the server via UDP.
 * 
 * @param format The "printf" formatting string.
 * @param ... The values to be used when referenced from the format string.
 */
void debug_printf(const char *format, ...) {
    char buf[64];
    char *buffer = buf;

    // Format the supplied string as per the formatting rules.
    va_list arg;
    va_start(arg, format);
    size_t strlen = vsnprintf(buf, sizeof(buf), format, arg);
    va_end(arg);

    if (strlen > sizeof(buf) - 1) {
        // The formatted string is too big for the buffer, create a bigger one.
        buffer = new char[strlen + 1];
        if (buffer == nullptr) {
            // We couldn't allocate enough memory for it, do nothing.
            return;
        }
        
        va_start(arg, format);
        vsnprintf(buffer, strlen + 1, format, arg);
        va_end(arg);
    }

    udp.beginPacket(DEBUG_SERVER, DEBUG_PORT);
    udp.write(buffer, strlen);
    udp.endPacket();

    if (buf != buffer) {
        // We had allocated a new buffer earlier, clear it now.
        delete[] buffer;
    }
}

/* 
 * Callback used when the NTP time has been updated.
 */
void ntp_time_received_cb() {
    time_t now = time(nullptr);
    debug_printf("Received NTP update: %ld.\n", now);

    if (myState == INITIALISING) {
        // This is the first time we've had a time to display.
        myLastTimestamp = now;
        myState = RUNNING;
        myForceRedisplay = true;
    } else {
        // TODO: Time changes.
    }
}

/*
 * Sends a command to the MAX7219/7221 LED driver chip.
 * 
 * @param command The 16-bit command to send to the LED driver.
 */
void led_command(uint16_t command) {
    //debug_printf("LED command: %04x.\n", command);

    // CS LOW = send command.
    digitalWrite(LED_CS_PIN, LOW);

    // Send the command.
    SPI.transfer16(command);

    // End the command.
    digitalWrite(LED_CS_PIN, HIGH);
}

/*
 * Sets the brightness of the LED display.
 * 
 * @param brightness The new brightness value [0-15].
 */
void set_brightness(uint8_t brightness) {
    led_command(0x0A00 | (brightness & 0x0F));
}

/* 
 * Copies the configuration data from one structure to another.
 * 
 * @param dest Pointer to the configuration into which the values are copied.
 * @param src The configuration from which the values are sorced.
 * @return Pointer to the destination structure.
 */
flash_config_t *copy_config(flash_config_t *dest, const flash_config_t src) {
    dest->alarmTime = src.alarmTime;
    dest->alarmStatus = src.alarmStatus;
    dest->radioFrequency = src.radioFrequency;
    dest->brightness = src.brightness;
    dest->isLargeLED = src.isLargeLED;
    dest->is24Hour = src.is24Hour;
    dest->isUseRadio = src.isUseRadio;

    return dest;
}

/* 
 * Compares two configuration structures, returning true if they match.
 *
 * @param a The first structure to test.
 * @param b The second structure to test.
 * @return true if every field in the two structures match, false otherwise.
 */
bool compare_config(flash_config_t a, flash_config_t b) {
    if ((a.alarmTime != b.alarmTime) ||
        (a.alarmStatus != b.alarmStatus) ||
        (a.radioFrequency != b.radioFrequency) ||
        (a.brightness != b.brightness) ||
        (a.isLargeLED != b.isLargeLED) ||
        (a.is24Hour != b.is24Hour) ||
        (a.isUseRadio != b.isUseRadio)) {
        return false;
    } else {
        return true;
    }
}

/*
 * Enters the configuration menu.
 */
void enter_menu() {
    myState = state_t::MENU_ALARM_MINUTES;
    copy_config(&myNewConfiguration, myNewConfiguration);
    myFlashCounter = 0;
    myFlashValue = false;
    myCountdownTimer = 0;
}

/*
 * Exits the configuration menu, saving the configuration if necessary.
 */
void exit_menu() {
    myState = state_t::RUNNING;
    myFlashCounter = -1;
    myCountdownTimer = 0;
    myForceRedisplay = true;
    if (!compare_config(myNewConfiguration, myNewConfiguration)) {
        // The configuration has changed.
        debug_printf("Configuration values changed.\n");
        copy_config(&myNewConfiguration, myNewConfiguration);
        #ifndef DISABLE_EEPROM_WRITES
        EEPROM.put(0, myNewConfiguration);
        EEPROM.commit();
        #endif
        set_brightness(myNewConfiguration.brightness);
    }
}

/*
 * Sends the values for the 4 7-segment LED displays to the LED driver chip.
 * 
 * @param farLeft The value for the first 7-segment display on the far left.
 * @param middleLeft The value for the second 7-segment display on the left of
 *                   middle.
 * @param middleRight The value for the second 7-segment display on the right
 *                    of middle.
 * @param farRight The value for the first 7-segment display on the far right.
 * @param colon Flag as to whether to show the : in the middle of the clock.
 * @param pm Flag as to whether to show the LED for "PM".
 * @param hundreds Flag as to whether to show the "hundreds" LED.
 */
void display(uint8_t farLeft, uint8_t middleLeft, uint8_t middleRight, uint8_t farRight, 
             bool colon = true, bool pm = false, bool hundreds = false) {
    uint16_t dig0 = 0x0100 + FONT[farRight];
    uint16_t dig1 = 0x0200 + FONT[middleRight];
    uint16_t dig2 = 0x0300 + FONT[middleLeft];
    uint16_t dig3 = 0x0400 + FONT[farLeft];
    uint16_t dig4 = 0x0500;
    if (colon) {
        dig4 |= 0x0080;
    }
    if (pm) {
        if (myNewConfiguration.isLargeLED) {
            dig4 |= 0x0020;        
        } else {
            dig0 |= 0x0080;
        }
    }
    if (hundreds) {
        if (myNewConfiguration.isLargeLED) {
            dig4 |= 0x001;
        } else {
            dig1 |= 0x0080;
        }
    }
    led_command(dig0);
    led_command(dig1);
    led_command(dig2);
    led_command(dig3);
    led_command(dig4);
}

/*
 * Displays the current time on the 7-segment LEDs.
 * 
 * @param hour The hour to be displayed.
 * @param minute The minute to be displayed.
 * @param showHour Flag used to control whether the hour should be shown - used
 *                 in flashing.
 * @param showMinute Flag used to control whether the minutes should be shown -
 *                   used in flashing.
 * @param show24Hour Flag used to control whether we're showing 12- or 24-hour
 *                   hours.
 */
void display_time(uint8_t hour, 
                  uint8_t minute, 
                  bool showHour = true,
                  bool showMinute = true,
                  bool show24Hour = true) {
    uint8_t digits[4];
    //debug_printf("Displaying time %02hhu:%02hhu.\n", hour, minute);

    // Hour digits.
    if (showHour) {
        if (!show24Hour) {
            hour %= 12;
            if (hour == 0) {
                hour = 12;
            }
        }
        if (hour >= 10) {
            digits[0] = hour / 10;
        } else {
            digits[0] = DIGIT_BLANK;
        }
        digits[1] = hour % 10;
    } else {
        digits[0] = DIGIT_BLANK;
        digits[1] = DIGIT_BLANK;
    }

    // Minute digits.
    if (showMinute) {
        digits[2] = minute / 10;
        digits[3] = minute % 10;
    } else {
        digits[2] = DIGIT_BLANK;
        digits[3] = DIGIT_BLANK;
    }

    display(digits[0], digits[1], digits[2], digits[3], true, !show24Hour && (hour >= 12));
}

/* 
 * Update the 7 segment displays, if necessary.
 */
void update_display() {
    time_t now;
    tm *tm_val;
    switch (myState) {
        case state_t::INITIALISING:
                display(DIGIT_DASH, DIGIT_DASH, DIGIT_DASH, DIGIT_DASH, false);
            break;
        case state_t::RUNNING:
            // Tick the clock.
            now = time(nullptr);
            if (((now /60) != (myLastTimestamp / 60)) ||
                (myForceRedisplay == true)) {
                    // The minute has changed, or the display has changed.
                    //debug_printf("now / 60: %d, myLastTimestamp / 60: %d, myForce: %d.\n", 
                    //             (now / 60), (myLastTimestamp / 60), myForceRedisplay);
                    tm_val = localtime(&now);
                    display_time(tm_val->tm_hour, tm_val->tm_min, 
                                 true, true, myConfiguration.is24Hour);

                    // See if the alarm needs to sound.
                    // TODO: Alarms
                }
            break;
        case state_t::SHOW_ALARM:
            // Show the alarm time.
            if (myForceRedisplay == true) {
                display_time(myNewConfiguration.alarmTime / 60,
                             myNewConfiguration.alarmTime % 60,
                             true, true, myConfiguration.is24Hour);
            }
            break;
        case state_t::MENU_ALARM_MINUTES:
            // We're showing the alarm time, with flashing minutes.
            if (myForceRedisplay) {
                display_time(myNewConfiguration.alarmTime / 60,
                             myNewConfiguration.alarmTime % 60,
                             true,
                             myFlashValue,
                             myNewConfiguration.is24Hour);
            }
            break;
        case state_t::MENU_ALARM_HOURS:
            // We're showing the alarm time, with flashing hours.
            if (myForceRedisplay) {
                display_time(myNewConfiguration.alarmTime / 60, 
                             myNewConfiguration.alarmTime % 60,
                             myFlashValue,
                             true,
                             myNewConfiguration.is24Hour);
            }
            break;
        case state_t::MENU_ALARM_DAYS:
            // We're showing which days the alarm is to sound.
            if (myForceRedisplay) {
                if (myFlashValue) {
                    switch (myNewConfiguration.alarmStatus) {
                        case alarm_t::DISABLED:
                            // The alarm is disabled.
                            display(DIGIT_BLANK, 0x00, 0x0F, 0x0F, false);
                            break;
                        case alarm_t::WEEKDAYS:
                            // The alarm only sounds on weekdays.
                            display(DIGIT_BLANK, 1, DIGIT_DASH, 5, false);
                            break;
                        case alarm_t::ALL_DAYS:
                            // The alarm will sound every day.
                            display(DIGIT_BLANK, 0, DIGIT_DASH, 6, false);
                            break;
                        case alarm_t::TOMORROW:
                            // The alarm will sound tomorrow only.
                            now = time(nullptr);
                            tm_val = localtime(&now);
                            display(DIGIT_BLANK, DIGIT_BLANK, DIGIT_BLANK, 
                                    (uint8_t)((tm_val->tm_wday + 1) % 7), false);
                            break;
                    }
                } else {
                    display(DIGIT_BLANK, DIGIT_BLANK, DIGIT_BLANK, DIGIT_BLANK, false);
                }
            }
            break;
        case state_t::MENU_RADIO_WHOLE:
            // We're showing the radio frequency whole Hz.
            if (myNewConfiguration.isUseRadio) {
                if (myFlashValue) {
                    display(myNewConfiguration.radioFrequency / 1000,
                            (myNewConfiguration.radioFrequency / 100) % 10,
                            (myNewConfiguration.radioFrequency / 10) % 10,
                            myNewConfiguration.radioFrequency % 10,
                            false,
                            false,
                            true);
                } else {
                    display(0x00,
                            0x00,
                            0x00,
                            myNewConfiguration.radioFrequency % 10,
                            false,
                            false,
                            true);
                }
            } else if (myFlashValue) {
                display(DIGIT_R, FONT[0x00], FONT[0x0F], FONT[0x0F], false);
            } else {
                display(0x00, 0x00, 0x00, 0x00, false);
            }
            break;
        case state_t::MENU_RADIO_FRACTION:
            // We're showing the radio frequency fraction Hz.
            if (myNewConfiguration.isUseRadio) {
                if (myFlashValue) {
                    display(myNewConfiguration.radioFrequency / 1000,
                            (myNewConfiguration.radioFrequency / 100) % 10,
                            (myNewConfiguration.radioFrequency / 10) % 10,
                            myNewConfiguration.radioFrequency % 10,
                            false,
                            false,
                            true);
                } else {
                    display(myNewConfiguration.radioFrequency / 1000,
                            (myNewConfiguration.radioFrequency / 100) % 10,
                            (myNewConfiguration.radioFrequency / 10) % 10,
                            0x00,
                            false,
                            false,
                            true);
                }
            } else if (myFlashValue) {
                display(DIGIT_R, FONT[0x00], FONT[0x0F], FONT[0x0F], false);
            } else {
                display(0x00, 0x00, 0x00, 0x00, false);
            }
            break;
        case state_t::MENU_12_24_HOURS:
            if (myFlashValue) {
                if (myNewConfiguration.is24Hour) {
                    display(0x00, FONT[0x02], FONT[0x04], FONT[DIGIT_H]);
                } else {
                    display(0x00, FONT[0x01], FONT[0x02], FONT[DIGIT_H]);
                }
            } else {
                display(0x00, 0x00, 0x00, 0x00, false);
            }

            break;
        case state_t::MENU_BRIGHTNESS:
            if (myFlashValue) {
                display(FONT[0x0B],
                        FONT[DIGIT_R],
                        FONT[DIGIT_I],
                        FONT[myNewConfiguration.brightness % 0x10],
                        false);
            } else {
                display(0x00, 0x00, 0x00, 0x00, false);
            }
            break;
        case state_t::MENU_TYPE:
            if (myFlashValue) {
                display(FONT[DIGIT_L], 
                        FONT[0x0E],
                        FONT[0x0D],
                        FONT[(myNewConfiguration.isLargeLED ? 0x01 : 0x02)],
                        false);
            } else {
                display(0x00, 0x00, 0x00, 0x00, false);
            }
            break;
    }

    myForceRedisplay = false;
    myLastTimestamp = now;
}

/*
 * Interrupt Service Routine (ISR) for updating the rotary encoder's value in
 * response to the user rotating it.
 */
void ICACHE_RAM_ATTR rotaryTick() {
    myEncoder.tick();
}

/*
 * Handles the user rotating the rotary encoder.
 * 
 * @param amount The number of detents that the encoder has been rotated.
 *               Negative values are clockwise.
 */
void rotate(int32_t amount) {
    uint8_t hour;
    uint8_t minute;
    amount *= -1;
    debug_printf("rotate by %d.\n", amount);
    switch (myState) {
        case state_t::MENU_ALARM_MINUTES:
            // Change the minute value for the alarm.
            hour = myNewConfiguration.alarmTime / 60;
            minute = myNewConfiguration.alarmTime % 60;
            minute = (minute + amount + MINUTES_PER_HOUR) % MINUTES_PER_HOUR;
            myNewConfiguration.alarmTime = (hour * 60) + minute;
            myForceRedisplay = true;
            break;
        case state_t::MENU_ALARM_HOURS:
            // Change the hour value for the alarm.
            hour = myNewConfiguration.alarmTime / 60;
            minute = myNewConfiguration.alarmTime % 60;
            hour = (hour + amount + HOURS_PER_DAY) % HOURS_PER_DAY;
            myNewConfiguration.alarmTime = (hour * 60) + minute;
            myForceRedisplay = true;
            break;
        case state_t::MENU_ALARM_DAYS:
            // Change the alarm enabled days.
            switch (myNewConfiguration.alarmStatus) {
                case alarm_t::DISABLED:
                    if (amount > 0) {
                        myNewConfiguration.alarmStatus = alarm_t::WEEKDAYS;
                    } else {
                        myNewConfiguration.alarmStatus = alarm_t::TOMORROW;
                    }
                    break;
                case alarm_t::WEEKDAYS:
                    if (amount > 0) {
                        myNewConfiguration.alarmStatus = alarm_t::ALL_DAYS;
                    } else {
                        myNewConfiguration.alarmStatus = alarm_t::DISABLED;
                    }
                    break;
                case alarm_t::ALL_DAYS:
                    if (amount > 0) {
                        myNewConfiguration.alarmStatus = alarm_t::TOMORROW;
                    } else {
                        myNewConfiguration.alarmStatus = alarm_t::WEEKDAYS;
                    }
                    break;
                case alarm_t::TOMORROW:
                    if (amount > 0) {
                        myNewConfiguration.alarmStatus = alarm_t::DISABLED;
                    } else {
                        myNewConfiguration.alarmStatus = alarm_t::ALL_DAYS;
                    }
                    break;
            }
            myForceRedisplay = true;
            break;
        case state_t::MENU_RADIO_WHOLE:
            // Change the radio frequency whole digits.
            if (myNewConfiguration.isUseRadio) {
                uint8_t freqHz = myNewConfiguration.radioFrequency / 100;
                freqHz += amount;
                if (freqHz == (MAX_RADIO_FREQUENCY + 1)) {
                    // We've just gone past the end of the spectrum, so radio off.
                    myNewConfiguration.isUseRadio = false;
                } else if (freqHz > MAX_RADIO_FREQUENCY) {
                    // We've gone further past the end of the spectrum, wrap around.
                    freqHz -= MAX_RADIO_FREQUENCY + 2;
                    freqHz += MIN_RADIO_FREQUENCY;
                } else if (freqHz == (MIN_RADIO_FREQUENCY - 1)) {
                    // We've just gone past the start of the spectrum, so radio off.
                    myNewConfiguration.isUseRadio = false;
                } else if (freqHz <= MIN_RADIO_FREQUENCY) {
                    // We've gone further past the start of the spectrum, wrap around.
                    freqHz -= MIN_RADIO_FREQUENCY - 2;
                    freqHz += MAX_RADIO_FREQUENCY;
                }
                myNewConfiguration.radioFrequency %= 100;
                myNewConfiguration.radioFrequency += freqHz * 100;
            } else {
                myNewConfiguration.isUseRadio = true;
                myNewConfiguration.radioFrequency %= 100;
                if (amount > 0) {
                    myNewConfiguration.radioFrequency += MIN_RADIO_FREQUENCY;
                } else {
                    myNewConfiguration.radioFrequency += MAX_RADIO_FREQUENCY;
                }
            }
            break;
        case state_t::MENU_12_24_HOURS:
            myNewConfiguration.is24Hour = !myNewConfiguration.is24Hour;
            break;
        case state_t::MENU_BRIGHTNESS:
            myNewConfiguration.brightness = (myNewConfiguration.brightness + 1) % MAX_BRIGHTNESS;
            break;
        case state_t::MENU_TYPE:
            myNewConfiguration.isLargeLED = !myNewConfiguration.isLargeLED;
            break;
        default:
            // Do nothing.
            break;
    }
}

/*
 * Handles the user clicking on the button in the rotary encoder.
 */
void click() {
    debug_printf("click.\n");
    switch (myState) {
        case state_t::INITIALISING: // Fall through
        case state_t::RUNNING:
            // Show the alarm time.
            myState = state_t::SHOW_ALARM;
            myCountdownTimer = SHOW_ALARM_COUNTDOWN;
            break;
        case state_t::SHOW_ALARM:
            // Start setting the alarm values.
            enter_menu();
            break;
        case state_t::MENU_ALARM_MINUTES:
            // Move to the next alarm menu.
            myState = state_t::MENU_ALARM_HOURS;
            break;
        case state_t::MENU_ALARM_HOURS:
            // Move to the next alarm menu.
            myState = state_t::MENU_ALARM_DAYS;
            break;
        case state_t::MENU_ALARM_DAYS:
            myState = state_t::MENU_RADIO_WHOLE;
            break;
        case state_t::MENU_RADIO_WHOLE:
            myState = state_t::MENU_RADIO_FRACTION;
            break;
        case state_t::MENU_RADIO_FRACTION:
            myState = state_t::MENU_12_24_HOURS;
            break;
        case state_t::MENU_12_24_HOURS:
            myState = state_t::MENU_BRIGHTNESS;
            break;
        case state_t::MENU_BRIGHTNESS:
            myState = state_t::MENU_TYPE;
            break;
        case state_t::MENU_TYPE:
            // Menu is finished, back to running.
            exit_menu();
            break;
    }
    myForceRedisplay = true;
}

/*
 * Handles the user double-clicking on the button on the rotary encoder.
 */
void double_click() {
    debug_printf("double click.\n");
    switch (myState) {
        case state_t::RUNNING:    // Fall-through
        case state_t::SHOW_ALARM:
            // Start the menu for setting the alarm values.
            enter_menu();
            myForceRedisplay = true;
            break;
        case state_t::MENU_ALARM_MINUTES:  // Fall-through
        case state_t::MENU_ALARM_HOURS:    // Fall-through
        case state_t::MENU_ALARM_DAYS:     // Fall-through
        case state_t::MENU_RADIO_WHOLE:    // Fall-through
        case state_t::MENU_RADIO_FRACTION: // Fall-through
        case state_t::MENU_12_24_HOURS:    // Fall-through
        case state_t::MENU_BRIGHTNESS:     // Fall-through
        case state_t::MENU_TYPE:
            // Exit the menu.
            exit_menu();
            myForceRedisplay = true;
            break;
        default:
            // Do nothing.
            break;
    }
}

/*
 * Handles the expiry of the countdown timer.
 */
void countdown_expired() {
    debug_printf("countdown expired.\n");
    if (myState == state_t::SHOW_ALARM) {
        // We've finished showing the alarm.
        myState = state_t::RUNNING;
        myForceRedisplay = true;
    }
}

/*
 * Setup routine run at power-on and reset times.
 */
void setup() {

    //Serial.begin(115200);
    //Serial.println("Initialising ESP8266");

    // Initialise the configuration.
    EEPROM.begin(sizeof(flash_config_t));
    if (EEPROM.percentUsed() >= 0) {
        EEPROM.get(0, myNewConfiguration);
    } else {
        myConfiguration.alarmTime = 6 * 60;    // 6 AM
        myConfiguration.alarmStatus = alarm_t::DISABLED;
        myConfiguration.radioFrequency = 9930; // Triple-J Perth
        myConfiguration.brightness = 0x0F;     // Maximum brightness
        myConfiguration.is24Hour = true;
        myConfiguration.isLargeLED = false;
        myConfiguration.isUseRadio = false;
    }

    // Initialise the LED display.
    pinMode(LED_CS_PIN, OUTPUT);
    digitalWrite(LED_CS_PIN, HIGH);

    //Serial.println("Initialising SPI");
    SPI.begin();

    //Serial.println("Initialising LED");
    led_command(0x0900); // Decode mode: no decode
    led_command(0x0101); // Digit 0 = -
    led_command(0x0201); // Digit 1 = -
    led_command(0x0301); // Digit 2 = -
    led_command(0x0401); // Digit 3 = -
    led_command(0x0500); // Digit 4(dots) = (blank)
    led_command(0x0A08); // Medium intensity
    led_command(0x0B04); // Digit 0-4 enabled
    set_brightness(myConfiguration.brightness);
    led_command(0x0C01); // LEDs operational

    // Set up the WiFi connection.
    //Serial.println("Initialising WiFi");
    WiFiManager wifiManager;
    wifiManager.setTimeout(CONFIG_TIMEOUT);
    if (!wifiManager.autoConnect("ESPClock")) {
        // We couldn't connect to the network - it could be a transient network
        // problem, so try restarting...
        //Serial.println("Unable to auto-connect, restarting.");
        ESP.reset();
        delay(5000);
    }

    // Initialise the clock's state.
    //Serial.println("Initialising state.");
    myState = INITIALISING;

    // Start the NTP clock.
    //Serial.println("Initialising NTP.");
    settimeofday_cb(ntp_time_received_cb);
    configTime(0, 0, "10.0.1.1", "pool.ntp.org");
    setenv("TZ", "AWST-8", 1);
    tzset();

    // Enable OTA flashing.
    //Serial.println("Initialising OTA");
    ArduinoOTA.setPort(8266);
    ArduinoOTA.onStart([]() {
        debug_printf("OTA starting\n");
    });
    ArduinoOTA.onEnd([]() {
        debug_printf("OTA Complete\n");
    });
    ArduinoOTA.onError([](ota_error_t error) {
        debug_printf("Error[%u]: ", error);
        switch (error) {
            case OTA_AUTH_ERROR:
                debug_printf("Auth Failed.\n");
                break;
            case OTA_BEGIN_ERROR:
                debug_printf("Begin Failed.\n");
                break;
            case OTA_CONNECT_ERROR:
                debug_printf("Connect Failed.\n");
                break;
            case OTA_RECEIVE_ERROR:
                debug_printf("Receive Failed.\n");
                break;
            case OTA_END_ERROR:
                debug_printf("End Failed.\n");
                break;
        }
    });
    ArduinoOTA.begin();

    // Initialise the button.
    myButton.begin();

    // Initialise the buzzer.
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, HIGH);

    // Initialise the rotary encoder.
    attachInterrupt(digitalPinToInterrupt(ENCODER_A_PIN), rotaryTick, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENCODER_B_PIN), rotaryTick, CHANGE);

    //Serial.println("Clock started successfully.");
    debug_printf("Clock started successfully.\n");
}

/*
 * Called continuously by the controller to execute the program.
 */
void loop() {
    // Handle any OTA updates.
    ArduinoOTA.handle();

    // Update the flash counter.
    if (myFlashCounter >= 0) {
        myFlashCounter = (myFlashCounter + 1) % FLASH_COUNT_MAX;
        if (myFlashCounter == 0) {
            // The flash counter has wrapped.
            myFlashValue = !myFlashValue;
            debug_printf("flash counter - flash? %d\n", myFlashValue);
            myForceRedisplay = true;
        }
    }

    // Update the coundown timer.
    if (myCountdownTimer > 0) {
        myCountdownTimer--;
        if (myCountdownTimer == 0) {
            countdown_expired();
        }
    }

    // Read the encoder.
    //myEncoder.tick();
    static long lastEncoderPos = -1;
    long encoderPos = myEncoder.getPosition();
    //myEncoder.setPosition(0);
    if (encoderPos != lastEncoderPos) {
        // The encoder has been changed by the user since the last loop.
        digitalWrite(BUZZER_PIN, !digitalRead(BUZZER_PIN));
        debug_printf("Encoder moved to: %ld.\n", encoderPos);
        rotate(encoderPos - lastEncoderPos);
        lastEncoderPos = encoderPos;
    }

    // Read the top button.
    myButton.read();
    if (myButton.wasPressed()) {
        debug_printf("Button was pressed.\n");
        // Button has been pressed, but we need to check for click vs double-click.
        if (myWasPressed) {
            // This is the end of a double-click.
            double_click();
            myWasPressed = false;
        } else {
            // This is either a single-click or the start of a double-click.
            myWasPressed = true;
        }
    } 
    if (myWasPressed) {
        if (myButton.releasedFor(DOUBLE_CLICK_INTERVAL)) {
            // The double-click has timed out, so it's a click.
            click();
            myWasPressed = false;
        }
    }

    // Choose what to display based on the current state.
    update_display();

    // Wait for the next loop.
    delay(LOOP_DELAY);
}