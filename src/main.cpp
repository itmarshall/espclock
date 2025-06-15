#include "main.h"

// The object used to send UDP debug messages.
WiFiUDP udp;

// The IP address of this device.
IPAddress myIPAddress;

// The current operating state of the clock.
state_t myState = state_t::INITIALISING;

// Whether we are currently in a menu.
boolean myIsInMenu = false;

// Whether the alarm is currently active (sounding).
alarm_state_t myAlarmState = alarm_state_t::INACTIVE;

// The number of seconds of alarm remaining.
int16_t myAlarmRemaining = 0;

// The number of seconds of snooze remaining.
int16_t mySnoozeRemaining = 0;

// The configuration for this clock.
flash_config_t myConfiguration;

// The new configuration values while the user is editing it in the menu.
flash_config_t myNewConfiguration;

// The last timestamp that we processed.
time_t myLastTimestamp = 0;

// The current hour of the day (0-23).
uint8_t myHour = 0;

// The current minute of the hour (0-60).
uint8_t myMinute = 0;

// The current minute of the day (0-1439).
uint16_t myMinuteOfDay = 0;

// The number of minutes into the day when sunrise occurs.
uint16_t mySunrise = 0;

// The number of minutes into the day when sunset occurs.
uint16_t mySunset = 0;

// Day (true) or night (false)?
boolean myIsDaytime = true;

// The current brightness of the clock.
uint8_t myBrightness = MAX_BRIGHTNESS;

// The counter until the next brightness check.
int32_t myBrightnessCounter = 0;

// Timer used for counting down to an event.
uint32_t myCountdownTimer = 0;

// Counter used to control digit flashing.
int32_t myFlashCounter = -1;

// The current state of the flash, true = on.
boolean myFlashValue = true;

// The animation step, if animations are active.
int myAnimationStep = 0;

// The buzzer step, if the buzzer is sounding.
uint8_t myBuzzerStep = 0;

// The number of ticks remaining in this step.
uint16_t myBuzzerTicksRemaining;

// Reads the position of the encoder.
RotaryEncoder myEncoder(PIN_ENCODER_A, PIN_ENCODER_B);

// The last encoder position.
uint32_t myLastPosition = -999;

// The button on the encoder.
Button myButton(PIN_ENCODER_SW);

// Flag to indicate that the button has been pressed.
// This allows us to differentiate single and double clicks.
boolean myWasPressed = false;

// Flag to indicate that the button has been held long enough to register as a
// long press.
boolean myIsLongPress = false;

// Flag to indicate if the user's alarm switch is set to enabled.
boolean myIsAlarmSwitchEnabled = true;

// Handles the updating of the LEDs for the 7 segment displays.
NeoPixelBus<NeoGrbFeature, NeoEsp32I2s1Ws2812xMethod> leds(LED_COUNT, PIN_LEDS);

// Preferences storage.
Preferences prefs;

// The sunrise/sunset calculator.
SunSet sun;

// The FM radio receiver.
TEA5767 radio;

// The web server used for configuration.
AsyncWebServer *webServer;

/*
 * Sends debugging information to the server via UDP.
 * 
 * @param format The "printf" formatting string.
 * @param ... The values to be used when referenced from the format string.
 */
/*
void debug_printf(const char *format, ...) {
    uint8_t buf[64];
    uint8_t *buffer = buf;

    // Format the supplied string as per the formatting rules.
    va_list arg;
    va_start(arg, format);
    size_t strlen = vsnprintf((char *)buf, sizeof(buf), format, arg);
    Serial.printf(format, arg);
    va_end(arg);

    // if (strlen > sizeof(buf) - 1) {
    //     // The formatted string is too big for the buffer, create a bigger one.
    //     buffer = new uint8_t[strlen + 1];
    //     if (buffer == nullptr) {
    //         // We couldn't allocate enough memory for it, do nothing.
    //         return;
    //     }
        
    //     va_start(arg, format);
    //     vsnprintf((char *)buffer, strlen + 1, format, arg);
    //     va_end(arg);
    // }

    // udp.beginPacket(DEBUG_SERVER, DEBUG_PORT);
    // udp.write(buffer, strlen);
    // udp.endPacket();

    if (buf != buffer) {
        // We had allocated a new buffer earlier, clear it now.
        delete[] buffer;
    }
}*/

/*
 * Syncs the sun calculations for the current time.
 *
 * @param tm_val The current time.
 */
void syncSunClock(const struct tm *tm_val) {
    sun.setCurrentDate(tm_val->tm_year + 1900, tm_val->tm_mon + 1, tm_val->tm_mday);
    double sunrise = sun.calcSunrise();
    double sunset = sun.calcSunset();
    mySunrise = static_cast<uint16_t>(sunrise);
    mySunset = static_cast<uint16_t>(sunset);

    #ifndef HIDE_DEBUG
    int srHour = mySunrise / 60;
    int srMinute = mySunrise - (srHour * 60);
    int ssHour = mySunset / 60;
    int ssMinute = mySunset - (ssHour * 60);
    Serial.printf("Updated sunrise: %d:%d (%d), sunset: %d:%d (%d).\n", 
        srHour, srMinute, mySunrise, ssHour, ssMinute, mySunset);
    #endif
}

/*
 * Checks whether it is day time or night time.
 *
 * @param tm_val The current time.
 */
void checkDaytime(const struct tm *tm_val) {
    if (myMinuteOfDay >= mySunset) {
        // It's after sunset, not daytime.
        myIsDaytime = false;
    } else if (myConfiguration.alarmActivation == alarm_t::ALARM_DISABLED ||
               (myConfiguration.alarmActivation == alarm_t::WEEKDAYS && 
                (tm_val->tm_wday == 0 || tm_val->tm_wday == 6))) {
        // No alarm today, use the sunrise time.
        myIsDaytime = myMinuteOfDay >= mySunrise;
    } else {
        // There is an alarm, treat "day" as after the alarm.
        myIsDaytime = myMinuteOfDay >= myConfiguration.alarmTime;
    }
}

/* 
 * Callback used when the NTP time has been updated.
 */
void ntp_time_received_cb(struct timeval *timeval) {
    time_t now;
    time(&now);
    Serial.printf("Received NTP update: %ld in state %d.\n", now, myState);

    if (myState == INITIALISING ||
        myState == SHOW_IP_1 ||
        myState == SHOW_IP_2 ||
        myState == SHOW_IP_3 ||
        myState == SHOW_IP_4) {
        // This is the first time we've had a time to display.
        myLastTimestamp = now - SECONDS_PER_HOUR;
        if (myState == INITIALISING) {
            myState = RUNNING;
        }
        
        tm *tm_val = localtime(&now);
        syncSunClock(tm_val);
        checkDaytime(tm_val);
    } else if (myState == SETUP_MENU_RADIO_WHOLE ||
               myState == SETUP_MENU_RADIO_FRACTION ||
               myState == SETUP_MENU_12_24_HOURS ||
               myState == SETUP_MENU_BRIGHTNESS ||
               myState == SETUP_MENU_DAY_COLOUR_INTRO ||
               myState == SETUP_MENU_DAY_COLOUR_R ||
               myState == SETUP_MENU_DAY_COLOUR_G ||
               myState == SETUP_MENU_DAY_COLOUR_B ||
               myState == SETUP_MENU_NIGHT_COLOUR_INTRO ||
               myState == SETUP_MENU_NIGHT_COLOUR_R ||
               myState == SETUP_MENU_NIGHT_COLOUR_G ||
               myState == SETUP_MENU_NIGHT_COLOUR_B ||
               myState == SETUP_MENU_ALARM_PATTERN) {
        // Set the last timestamp, so that we know that the time has been
        // received once we exit the setup menu.
        myLastTimestamp = now;
    } else {
        // TODO: Time changes.
    }
}

/*
 * Sets the brightness of the LED display.
 * 
 * @param brightness The new brightness value [0-4095].
 */
void set_brightness(uint16_t brightness) {
    float bri = (float)brightness / (float)MAX_BRIGHTNESS_INPUT;
    float cfg = (float)myConfiguration.brightness / MAX_BRIGHTNESS_F;
    myBrightness = (uint8_t)ceilf((bri * cfg) * MAX_BRIGHTNESS_F);
    if (myBrightness < MIN_BRIGHTNESS) { 
        myBrightness = MIN_BRIGHTNESS;
    }
}

/**
 * Starts sounding the alarm.
 */
void start_alarm() {
    myAlarmState = alarm_state_t::ACTIVE;
    myAlarmRemaining = ALARM_DURATION;
    mySnoozeRemaining = 0;
    
    if (myConfiguration.isRadioInstalled && myConfiguration.isUseRadio) {
        // Turn on the radio.
        radio.setBandFrequency(RADIO_BAND_FM, myConfiguration.radioFrequency);
        radio.setMute(false);
    } else {
        // Start the buzzer.
        myBuzzerStep = 0;
        myBuzzerTicksRemaining = BUZZER_STEP_TICKS[myBuzzerStep];
        digitalWrite(PIN_BUZZER, HIGH);
    }
}

void snooze_alarm() {
    myAlarmState = alarm_state_t::SNOOZE;
    myAlarmRemaining = 0;
    mySnoozeRemaining = SNOOZE_DURATION;

    // Turn off the radio/buzzer.
    if (myConfiguration.isRadioInstalled && myConfiguration.isUseRadio) {
        radio.setMute(true);
    } else {
        digitalWrite(PIN_BUZZER, LOW);
    }
}

void stop_alarm() {
    myAlarmState = alarm_state_t::INACTIVE;
    myAlarmRemaining = 0;
    mySnoozeRemaining = 0;

    // Turn off the radio/buzzer.
    if (myConfiguration.isRadioInstalled && myConfiguration.isUseRadio) {
        radio.setMute(true);
    } else {
        digitalWrite(PIN_BUZZER, LOW);
    }
}

/* 
 * Copies the configuration data from one structure to another.
 * 
 * @param dest Pointer to the configuration into which the values are copied.
 * @param src The configuration from which the values are sorced.
 * @return Pointer to the destination structure.
 */
flash_config_t *copy_config(flash_config_t *dest, const flash_config_t *src) {
    memcpy(dest, src, sizeof(flash_config_t));

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
    return memcmp(&a, &b, sizeof(flash_config_t)) == 0;
}

/*
 * Write the configuration to the flash storage.
 *
 * @param config The configuration to be written to flash storage.
 */
inline void write_config(flash_config_t *config) {
    #ifndef DISABLE_CONFIG_WRITES
    prefs.putBytes(KEY_CONFIG, config, sizeof(flash_config_t));
    #endif
}

/*
 * Enters the configuration menu.
 *
 * @param isSetupMenu Flag set when the menu to enter is the setup menu (true).
 *                    When false, the alarm menu is shown.
 */
void enter_menu(bool isSetupMenu = false) {
    myState = isSetupMenu ? state_t::SETUP_MENU_RADIO_WHOLE : state_t::MENU_ALARM_MINUTES;
    myIsInMenu = true;
    copy_config(&myNewConfiguration, &myConfiguration);
    myFlashCounter = 0;
    myFlashValue = false;
    myCountdownTimer = 0;
}

/*
 * Exits the configuration menu, saving the configuration if necessary.
 * 
 * @param discardChanges Flag set when any changes should be ignored.
 */
void exit_menu(boolean discardChanges = false, bool isSetupMenu = false) {
    myState = state_t::RUNNING;
    myIsInMenu = false;
    myFlashCounter = -1;
    myCountdownTimer = 0;
    if (!discardChanges && !compare_config(myConfiguration, myNewConfiguration)) {
        // The configuration has changed.
        Serial.printf("Configuration values changed.\n");
        copy_config(&myConfiguration, &myNewConfiguration);
        write_config(&myConfiguration);
    }
}

inline HslColor colourt_to_hsl_colour(colour_t colour) {
    HslColor c(RgbColor(colour.r, colour.g, colour.b));
    return c;
}

/**
 * Calculates the colour to be used for a digit or colon.
 * 
 * @param group The digit (0, 1, 3, 4, L-R) or colon (2, 5) being calculated.
 * @param pattern The pattern in use.
 * @param colour The base colour, if used by the pattern.
 */
HslColor calculate_digit_colour(uint8_t group, display_pattern_t pattern, colour_t colour) {
    switch (pattern) {
        case display_pattern_t::SOLID_COLOUR: {
            return colourt_to_hsl_colour(colour);
        }
        case display_pattern_t::RAINBOW_DIGITS: {
            HslColor c(RgbColor(255, 0, 0));
            c.H += (1.0f - (group * DIGIT_GROUP_MULTIPLIER)) + (myAnimationStep * DIGIT_COLOUR_STEP);
            c.H = c.H > 1.0f ? c.H - (int)c.H : c.H;
            return c;
        }
        case display_pattern_t::FLASHING: {
            if (myAnimationStep < FLASH_ON_STEPS) {
                return colourt_to_hsl_colour(colour);
            } else {
                return BLACK;
            }
        }
        case display_pattern_t::PULSING: {
            float frac = ((float)myAnimationStep) / PULSE_STEPS;
            if (frac > 1.0f) {
                frac = 2.0f - frac;
            }
            HslColor fc = colourt_to_hsl_colour(colour);
            HslColor hc = HslColor::LinearBlend<NeoHueBlendShortestDistance>(
                BLACK, fc, frac);
            return hc;
        }
        case display_pattern_t::MENU: {
            if ((myState == state_t::MENU_ALARM_HOURS && group <= 1) ||
                (myState == state_t::MENU_ALARM_MINUTES && (group == 3 || group == 4)) ||
                (myState == state_t::SETUP_MENU_RADIO_WHOLE && (group < 4)) ||
                (myState == state_t::SETUP_MENU_RADIO_FRACTION && (group != 4))) {
                return MENU_BRIGHT;
            } else if (myState == state_t::SETUP_MENU_DAY_COLOUR_R && group == 0) {
                return COLOUR_R;
            } else if (myState == state_t::SETUP_MENU_DAY_COLOUR_G && group == 0) {
                return COLOUR_G;
            } else if (myState == state_t::SETUP_MENU_DAY_COLOUR_B && group == 0) {
                return COLOUR_B;
            }

            float frac = ((float)myAnimationStep) / PULSE_STEPS;
            if (frac > 1.0f) {
                frac = 2.0f - frac;
            }
            return HslColor::LinearBlend<NeoHueBlendShortestDistance>(
                MENU_DIM, MENU_BRIGHT, frac);
        }
    }
    return colourt_to_hsl_colour(colour);
}

/**
 * Calculates the colour to be used for an individual segment.
 * 
 * @param previouslyLitSegments The number of segments lit before the digit this segment belongs to.
 * @param segmentIndex The index of the segment within the digit being lit, 0 = first.
 * @param fontIndex The index into the FONT array for the digit being selected. 0xFF = colon.
 */
HslColor calculate_segment_colour(int previouslyLitSegments, uint8_t segmentIndex, uint8_t fontIndex) {
    HslColor c(RgbColor(255, 0, 0));
    uint8_t segmentOrder;
    if (fontIndex == 0xFF) {
        segmentOrder = segmentIndex;
    } else {
        segmentOrder = FONT_SEGMENT_ORDER[fontIndex][segmentIndex];
    }
    c.H += (1.0f - ((previouslyLitSegments + segmentOrder) * SEGMENT_MULTIPLIER)) + 
            (myAnimationStep * SEGMENT_COLOUR_STEP);
    c.H = c.H > 1.0f ? c.H - (int)c.H : c.H;
    
    return c;
}

/**
 * Sets the colour for a single LED, scaling the brightness as necessary.
 * 
 * @param ledNumber The LED number (0-31) to set.
 * @param colour The colour to set the LED to.
 */
void set_led(uint16_t ledNumber, HslColor colour) {
    HslColor scaled;
    scaled.H = colour.H;
    scaled.S = colour.S;
    scaled.L = (colour.L * myBrightness) / MAX_BRIGHTNESS_F;
    leds.SetPixelColor(ledNumber, scaled);
}

/**
 * Sets the LED values for a single 7-segment digit.
 * 
 * @param ledBits The bit mask containing which LEDs to light.
 * @param firstLedNumber The index of the first LED in the digit.
 * @param digitColour The colour to set any lit segments to.
 */
void set_digit(uint8_t ledBits, uint16_t firstLedNumber, HslColor digitColour) {
    for (uint16_t ii = 0; ii < 7; ii++) {
        if ((ledBits & (1 << ii)) != 0) {
            set_led(firstLedNumber + ii, digitColour);
        } else {
            set_led(firstLedNumber + ii, BLACK);
        }
    }
}


/**
 * Sets the LED values for a single 7-segment digit.
 * 
 * @param fontIndex The index into the FONT array for the digit.
 * @param firstLedNumber The index of the first LED in the digit.
 */
int set_digit(uint8_t fontIndex, uint16_t firstLedNumber, int previouslyLitSegments) {
    int segmentsLit = previouslyLitSegments;
    const uint8_t ledBits = FONT[fontIndex];
    for (uint16_t ii = 0; ii < SEGMENTS_PER_DIGIT; ii++) {
        if ((ledBits & (1 << ii)) != 0) {
            segmentsLit++;
            HslColor colour = calculate_segment_colour(previouslyLitSegments, ii, fontIndex);
            set_led(firstLedNumber + ii, colour);
        }
        else
        {
            set_led(firstLedNumber + ii, BLACK);
        }
    }

    return segmentsLit;
}

/*
 * Sends the values for the 4 7-segment LED displays to the LED driver chip.
 * 
 * @param farLeft The value for the first 7-segment display on the far left.
 * @param middleLeft The value for the second 7-segment display on the left of
 *                   middle.
 * @param middleRight The value for the third 7-segment display on the right
 *                    of middle.
 * @param farRight The value for the first 7-segment display on the far right.
 * @param colon Flag as to whether to show the : in the middle of the clock.
 * @param pm Flag as to whether to show the LED for "PM".
 * @param alarmSet Flag as to whether to show the "alarm set" LED.
 */
void display(uint8_t farLeft, uint8_t middleLeft, uint8_t middleRight, uint8_t farRight, 
             bool colon = true, bool pm = false, bool alarmSet = false) {

    display_pattern_t pattern;
    colour_t baseColour;
    if (myIsInMenu) {
        pattern = display_pattern_t::MENU;
        baseColour.r = baseColour.g = baseColour.b = 0xFF;
    } else if (myAlarmState == alarm_state_t::ACTIVE) {
        pattern = myConfiguration.alarmPattern;
        baseColour = myConfiguration.alarmColour;
    } else if (myIsDaytime) {
        pattern = myConfiguration.dayPattern;
        baseColour = myConfiguration.dayColour;
    } else {
        pattern = myConfiguration.nightPattern;
        baseColour = myConfiguration.nightColour;
    }

    // Serial.printf(
    //     "display: %02x %02x %02x %02x c=%s pm=%s as=%s day=%s pat=%d animStep=%d.\n",
    //              farLeft & 0xFF,
    //              middleLeft & 0xFF,
    //              middleRight & 0xFF,
    //              farRight & 0xFF,
    //              colon ? "t" : "f",
    //              pm ? "t" : "f",
    //              alarmSet ? "t" : "f",
    //              myIsDaytime ? "t" : "f",
    //              pattern,
    //              myAnimationStep);

    if (pattern == display_pattern_t::RAINBOW_SEGMENTS) {
        int segmentsLit = 0;
        segmentsLit = set_digit(farLeft, 0, segmentsLit);
        segmentsLit = set_digit(middleLeft, 7, segmentsLit);
        
        if (colon) {
            HslColor colour = calculate_segment_colour(segmentsLit, 0, 0xFF);
            set_led(14, colour);
            segmentsLit++;
            
            colour = calculate_segment_colour(segmentsLit, 1, 0xFF);
            set_led(15, colour);
            segmentsLit++;
        } else {
            set_led(14, BLACK);
            set_led(15, BLACK);
        }

        segmentsLit = set_digit(middleRight, 16, segmentsLit);
        segmentsLit = set_digit(farRight, 23, segmentsLit);
        
        if (pm) {
            HslColor colour = calculate_segment_colour(segmentsLit, 0, 0xFF);
            set_led(30, colour);
            segmentsLit++;
        } else {
            set_led(30, BLACK);
        }

        if (alarmSet) {
            HslColor colour = calculate_segment_colour(segmentsLit, 1, 0xFF);
            set_led(31, colour);
        } else {
            set_led(31, BLACK);
        }
    } else {
        HslColor colour = calculate_digit_colour(0, pattern, baseColour);
        // Serial.printf("fl: h=%f s=%f l=%f)\n", colour.H, colour.S, colour.L);
        set_digit(FONT[farLeft], 0, colour);

        colour = calculate_digit_colour(1, pattern, baseColour);
        set_digit(FONT[middleLeft], 7, colour);

        if (colon) {
            colour = calculate_digit_colour(2, pattern, baseColour);
            set_led(14, colour);
            set_led(15, colour);
        } else {
            set_led(14, BLACK);
            set_led(15, BLACK);
        }

        colour = calculate_digit_colour(3, pattern, baseColour);
        set_digit(FONT[middleRight], 16, colour);

        colour = calculate_digit_colour(4, pattern, baseColour);
        set_digit(FONT[farRight], 23, colour);

        if (pm || alarmSet) {
            colour = calculate_digit_colour(5, pattern, baseColour);
            if (pm) {
                set_led(30, colour);
            } else {
                set_led(30, BLACK);
            }
            if (alarmSet) {
                set_led(31, colour);
            } else {
                set_led(31, BLACK);
            }
        } else {
            set_led(30, BLACK);
            set_led(31, BLACK);
        }
    }
    leds.Show();
}

/*
 * Displays the current time on the 7-segment LEDs.
 * 
 * @param hour The hour to be displayed.
 * @param minute The minute to be displayed.
 */
void display_time(uint8_t hour, uint8_t minute, bool show24Hour = true) {
    uint8_t digits[4];
    //Serial.printf("Displaying time %02hhu:%02hhu.\n", hour, minute);

    // Hour digits.
    boolean pm = (hour >= 12);
    if (!show24Hour) {
        hour %= 12;
        if (hour == 0) {
            hour = 12;
        }
    }
    if (hour >= 10) {
        digits[0] = hour / 10;
    } else {
        digits[0] = FONT_BLANK;
    }
    digits[1] = hour % 10;

    // Minute digits.
    digits[2] = minute / 10;
    digits[3] = minute % 10;

    bool showAlarm = myConfiguration.alarmActivation != alarm_t::ALARM_DISABLED && myIsAlarmSwitchEnabled;
    display(digits[0], digits[1], digits[2], digits[3], true, !show24Hour && pm, showAlarm);
}

void display_number(uint8_t number) {
    uint8_t hundreds = (number >= 100) ? number / 100 : FONT_BLANK;
    uint8_t tens = (number >= 10) ? (number % 100) / 10 : FONT_BLANK;
    uint8_t ones = number % 10;
    display(FONT_BLANK, hundreds, tens, ones);
}

/* 
 * Update the 7 segment displays, if necessary.
 */
void update_display() {
    switch (myState) {
        case state_t::INITIALISING: // Fall through
        case state_t::CANCELLED:
                display(FONT_DASH, FONT_DASH, FONT_DASH, FONT_DASH, false);
            break;
        case state_t::RUNNING:
            display_time(myHour, myMinute, myConfiguration.is24Hour);
            break;
        case state_t::SHOW_IP_1:
            display_number(myIPAddress[0]);
            break;
        case state_t::SHOW_IP_2:
            display_number(myIPAddress[1]);
            break;
        case state_t::SHOW_IP_3:
            display_number(myIPAddress[2]);
            break;
        case state_t::SHOW_IP_4:
            display_number(myIPAddress[3]);
            break;
        case state_t::SHOW_SNOOZE:
            // Show the sleep time remaining.
            display_time(mySnoozeRemaining / SECONDS_PER_MINUTE,
                         mySnoozeRemaining % SECONDS_PER_MINUTE);
            break;
        case state_t::SHOW_ALARM: // Fall-through
        case state_t::MENU_ALARM_MINUTES: // Fall-through
        case state_t::MENU_ALARM_HOURS:
            // Show the alarm time.
            display_time(myNewConfiguration.alarmTime / MINUTES_PER_HOUR,
                         myNewConfiguration.alarmTime % MINUTES_PER_HOUR,
                         myNewConfiguration.is24Hour);
            break;
        case state_t::MENU_ALARM_DAYS:
            // We're showing which days the alarm is to sound.
            switch (myNewConfiguration.alarmActivation) {
                case alarm_t::ALARM_DISABLED:
                    // The alarm is disabled.
                    display(FONT_BLANK, 0, FONT_F, FONT_F, false);
                    break;
                case alarm_t::WEEKDAYS:
                    // The alarm only sounds on weekdays.
                    display(FONT_BLANK, 1, FONT_DASH, 5, false, false, true);
                    break;
                case alarm_t::ALL_DAYS:
                    // The alarm will sound every day.
                    display(FONT_BLANK, 0, FONT_DASH, 6, false, false, true);
                    break;
                case alarm_t::ONE_TIME:
                    // The alarm will sound tomorrow only.
                    display(0, FONT_N, FONT_C, FONT_E, false, false, true);
                    break;
            }
            break;
        case state_t::SETUP_MENU_RADIO_WHOLE:    // Fall-through
        case state_t::SETUP_MENU_RADIO_FRACTION:
            // We're showing the radio frequency.
            if (myNewConfiguration.isUseRadio) {
                display(myNewConfiguration.radioFrequency / 1000,
                        (myNewConfiguration.radioFrequency / 100) % 10,
                        (myNewConfiguration.radioFrequency / 10) % 10,
                        myNewConfiguration.radioFrequency % 10,
                        false);
            } else {
                display(FONT_R, 0x00, 0x0F, 0x0F, false);
            }
            break;
        case state_t::SETUP_MENU_12_24_HOURS:
            if (myNewConfiguration.is24Hour) {
                display(FONT_BLANK, 0x02, 0x04, FONT_H, false);
            } else {
                display(FONT_BLANK, 0x01, 0x02, FONT_H, false);
            }
            break;
        case state_t::SETUP_MENU_BRIGHTNESS:
            display(FONT_B,
                    FONT_R,
                    FONT_I,
                    myNewConfiguration.brightness % MAX_BRIGHTNESS,
                    false);
            break;
    }
}

/*
 * Interrupt Service Routine (ISR) for updating the rotary encoder's value in
 * response to the user rotating it.
 */
void IRAM_ATTR rotary_tick() {
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
    uint8_t origMinute;
    uint16_t freqWhole;
    uint8_t freqFrac;
    amount *= -1;
    Serial.printf("rotate by %d in state %d.\n", amount, myState);
    switch (myState) {
        case state_t::SHOW_SNOOZE:
            // Increase/decrease the snooze.
            mySnoozeRemaining += (amount * SECONDS_PER_MINUTE);
            if (mySnoozeRemaining <= 0) {
                // Turn the alarm off.
                stop_alarm();
            } else if (mySnoozeRemaining > MAX_SNOOZE) {
                mySnoozeRemaining = MAX_SNOOZE;
            }
            break;
        case state_t::MENU_ALARM_MINUTES:
            // Change the minute value for the alarm.
            hour = myNewConfiguration.alarmTime / MINUTES_PER_HOUR;
            minute = myNewConfiguration.alarmTime % MINUTES_PER_HOUR;
            origMinute = minute;
            minute = (minute + amount + MINUTES_PER_HOUR) % MINUTES_PER_HOUR;
            if ((amount > 0) && (minute < origMinute)) {
                // The minute has wrapped around the top of the hour.
                hour = (hour + 1) % HOURS_PER_DAY;
            } else if ((amount < 0) && (minute > origMinute)) {
                // The minute has wrapped around the bottom of the hour.
                hour = (hour - 1 + HOURS_PER_DAY) % HOURS_PER_DAY;
            }
            myNewConfiguration.alarmTime = (hour * MINUTES_PER_HOUR) + minute;
            break;
        case state_t::MENU_ALARM_HOURS:
            // Change the hour value for the alarm.
            hour = myNewConfiguration.alarmTime / MINUTES_PER_HOUR;
            minute = myNewConfiguration.alarmTime % MINUTES_PER_HOUR;
            hour = (hour + amount + HOURS_PER_DAY) % HOURS_PER_DAY;
            myNewConfiguration.alarmTime = (hour * MINUTES_PER_HOUR) + minute;
            break;
        case state_t::MENU_ALARM_DAYS:
            // Change the alarm enabled days.
            switch (myNewConfiguration.alarmActivation) {
                case alarm_t::ALARM_DISABLED:
                    if (amount > 0) {
                        myNewConfiguration.alarmActivation = alarm_t::WEEKDAYS;
                    } else {
                        myNewConfiguration.alarmActivation = alarm_t::ONE_TIME;
                    }
                    break;
                case alarm_t::WEEKDAYS:
                    if (amount > 0) {
                        myNewConfiguration.alarmActivation = alarm_t::ALL_DAYS;
                    } else {
                        myNewConfiguration.alarmActivation = alarm_t::ALARM_DISABLED;
                    }
                    break;
                case alarm_t::ALL_DAYS:
                    if (amount > 0) {
                        myNewConfiguration.alarmActivation = alarm_t::ONE_TIME;
                    } else {
                        myNewConfiguration.alarmActivation = alarm_t::WEEKDAYS;
                    }
                    break;
                case alarm_t::ONE_TIME:
                    if (amount > 0) {
                        myNewConfiguration.alarmActivation = alarm_t::ALARM_DISABLED;
                    } else {
                        myNewConfiguration.alarmActivation = alarm_t::ALL_DAYS;
                    }
                    break;
            }
            break;
        case state_t::SETUP_MENU_RADIO_WHOLE:
            // Change the radio frequency whole digits.
            freqWhole = myNewConfiguration.radioFrequency / 10;
            freqFrac = myNewConfiguration.radioFrequency % 10;
            freqWhole += amount;
            while (freqWhole > MAX_RADIO_FREQUENCY) {
                freqWhole -= freqWhole - MAX_RADIO_FREQUENCY + MIN_RADIO_FREQUENCY - 1;
            }
            while (freqWhole < MIN_RADIO_FREQUENCY) {
                freqWhole = freqWhole + MAX_RADIO_FREQUENCY - MIN_RADIO_FREQUENCY + 1;
            }
            Serial.printf("New frequency: %d.\n", freqWhole);
            myNewConfiguration.radioFrequency = (freqWhole * 10) + freqFrac;
            Serial.printf("Final frequency: %ld.\n", myNewConfiguration.radioFrequency);
            break;
        case state_t::SETUP_MENU_RADIO_FRACTION:
            // Change the radio frequency fractional digit.
            freqWhole = myNewConfiguration.radioFrequency / 10;
            freqFrac = myNewConfiguration.radioFrequency % 10;
            freqFrac = (freqFrac + amount + 10) % 10;
            myNewConfiguration.radioFrequency = (freqWhole * 10) + freqFrac;
            Serial.printf("Frac: final frequency: %ld.\n", myNewConfiguration.radioFrequency);
            break;
        case state_t::SETUP_MENU_12_24_HOURS:
            if ((amount % 2) != 0) {
                myNewConfiguration.is24Hour = !myNewConfiguration.is24Hour;
            }
            break;
        case state_t::SETUP_MENU_BRIGHTNESS:
            myNewConfiguration.brightness = 
                (myNewConfiguration.brightness + amount + MAX_BRIGHTNESS) % MAX_BRIGHTNESS;
            break;
        case state_t::SETUP_MENU_DAY_COLOUR_R:
            // Colour will automatically wrap around as it's only 8 bits.
            myNewConfiguration.dayColour.r += amount;
            break;
        case state_t::SETUP_MENU_DAY_COLOUR_G:
            // Colour will automatically wrap around as it's only 8 bits.
            myNewConfiguration.dayColour.g += amount;
            break;
        case state_t::SETUP_MENU_DAY_COLOUR_B:
            // Colour will automatically wrap around as it's only 8 bits.
            myNewConfiguration.dayColour.b += amount;
            break;
        case state_t::SETUP_MENU_NIGHT_COLOUR_R:
            // Colour will automatically wrap around as it's only 8 bits.
            myNewConfiguration.nightColour.r += amount;
            break;
        case state_t::SETUP_MENU_NIGHT_COLOUR_G:
            // Colour will automatically wrap around as it's only 8 bits.
            myNewConfiguration.nightColour.g += amount;
            break;
        case state_t::SETUP_MENU_NIGHT_COLOUR_B:
            // Colour will automatically wrap around as it's only 8 bits.
            myNewConfiguration.nightColour.b += amount;
            break;
        case state_t::SETUP_MENU_ALARM_PATTERN: {
            uint8_t val = myNewConfiguration.alarmPattern;
            val = (val + amount + ALARM_PATTERN_COUNT) % ALARM_PATTERN_COUNT;
            myNewConfiguration.alarmPattern = static_cast<display_pattern_t>(val);
            break;
        }
        default:
            // Do nothing.
            break;
    }
}

/*
 * Handles the user clicking on the button in the rotary encoder.
 */
void click() {
    Serial.printf("click.\n");
    switch (myState) {
        case state_t::INITIALISING: // Fall through
        case state_t::RUNNING:
            if (myAlarmState == alarm_state_t::INACTIVE) {
                // Show the alarm time.
                myState = state_t::SHOW_ALARM;
                myCountdownTimer = SHOW_ALARM_COUNTDOWN;
            } else {
                // Show the snooze time.
                myState = state_t::SHOW_SNOOZE;
                myCountdownTimer = SHOW_SNOOZE_COUNTDOWN;
                if (myAlarmState == alarm_state_t::ACTIVE) {
                    snooze_alarm();
                }
            }
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
            // Menu is finished, back to running.
            exit_menu();
            break;
        case state_t::SETUP_MENU_RADIO_WHOLE:
            myState = state_t::SETUP_MENU_RADIO_FRACTION;
            break;
        case state_t::SETUP_MENU_RADIO_FRACTION:
            myState = state_t::SETUP_MENU_12_24_HOURS;
            break;
        case state_t::SETUP_MENU_12_24_HOURS:
            myState = state_t::SETUP_MENU_BRIGHTNESS;
            break;
        case state_t::SETUP_MENU_BRIGHTNESS:
            myState = state_t::SETUP_MENU_DAY_COLOUR_INTRO;
            myCountdownTimer = SHOW_INTRO_COUNTDOWN;
            break;
        case state_t::SETUP_MENU_DAY_COLOUR_INTRO:
            myState = state_t::SETUP_MENU_DAY_COLOUR_R;
            break;
        case state_t::SETUP_MENU_DAY_COLOUR_R:
            myState = state_t::SETUP_MENU_DAY_COLOUR_G;
            break;
        case state_t::SETUP_MENU_DAY_COLOUR_G:
            myState = state_t::SETUP_MENU_DAY_COLOUR_B;
            break;
        case state_t::SETUP_MENU_DAY_COLOUR_B:
            myState = state_t::SETUP_MENU_NIGHT_COLOUR_INTRO;
            myCountdownTimer = SHOW_INTRO_COUNTDOWN;
            break;
        case state_t::SETUP_MENU_NIGHT_COLOUR_INTRO:
            myState = state_t::SETUP_MENU_NIGHT_COLOUR_R;
            break;
        case state_t::SETUP_MENU_NIGHT_COLOUR_R:
            myState = state_t::SETUP_MENU_NIGHT_COLOUR_G;
            break;
        case state_t::SETUP_MENU_NIGHT_COLOUR_G:
            myState = state_t::SETUP_MENU_NIGHT_COLOUR_B;
            break;
        case state_t::SETUP_MENU_NIGHT_COLOUR_B:
            myState = state_t::SETUP_MENU_ALARM_PATTERN;
            break;
        case state_t::SETUP_MENU_ALARM_PATTERN:
            // Menu is finished, back to running.
            exit_menu();
            break;
        case state_t::CANCELLED:
            myState = state_t::RUNNING;
            break;
    }
}

/*
 * Handles the user double-clicking on the button on the rotary encoder.
 */
void double_click() {
    Serial.printf("double click.\n");
    switch (myState) {
        case state_t::RUNNING:    // Fall-through
        case state_t::SHOW_ALARM:
            // Start the menu for setting the alarm values.
            enter_menu();
            break;
        case state_t::SHOW_SNOOZE:
            // Stop the alarm and snooze.
            stop_alarm();
            break;
        case state_t::MENU_ALARM_MINUTES:            // Fall-through
        case state_t::MENU_ALARM_HOURS:              // Fall-through
        case state_t::MENU_ALARM_DAYS:               // Fall-through
        case state_t::SETUP_MENU_RADIO_WHOLE:        // Fall-through
        case state_t::SETUP_MENU_RADIO_FRACTION:     // Fall-through
        case state_t::SETUP_MENU_12_24_HOURS:        // Fall-through
        case state_t::SETUP_MENU_BRIGHTNESS:         // Fall-through
        case state_t::SETUP_MENU_DAY_COLOUR_INTRO:   // Fall-through
        case state_t::SETUP_MENU_DAY_COLOUR_R:       // Fall-through
        case state_t::SETUP_MENU_DAY_COLOUR_G:       // Fall-through
        case state_t::SETUP_MENU_DAY_COLOUR_B:       // Fall-through
        case state_t::SETUP_MENU_NIGHT_COLOUR_INTRO: // Fall-through
        case state_t::SETUP_MENU_NIGHT_COLOUR_R:     // Fall-through
        case state_t::SETUP_MENU_NIGHT_COLOUR_G:     // Fall-through
        case state_t::SETUP_MENU_NIGHT_COLOUR_B:     // Fall-through
        case state_t::SETUP_MENU_ALARM_PATTERN:
            // Exit the menu.
            exit_menu();
            break;
        default:
            // Do nothing.
            break;
    }
}

/*
 * Handles the user long-pressing on the button on the rotary encoder.
 * Long presses are used to cancel out of the menu.
 */
void long_press() {
    Serial.printf("long press.\n");
    switch (myState) {
        case state_t::MENU_ALARM_MINUTES:            // Fall-through
        case state_t::MENU_ALARM_HOURS:              // Fall-through
        case state_t::MENU_ALARM_DAYS:               // Fall-through
        case state_t::SETUP_MENU_RADIO_WHOLE:        // Fall-through
        case state_t::SETUP_MENU_RADIO_FRACTION:     // Fall-through
        case state_t::SETUP_MENU_12_24_HOURS:        // Fall-through
        case state_t::SETUP_MENU_BRIGHTNESS:         // Fall-through
        case state_t::SETUP_MENU_DAY_COLOUR_INTRO:   // Fall-through
        case state_t::SETUP_MENU_DAY_COLOUR_R:       // Fall-through
        case state_t::SETUP_MENU_DAY_COLOUR_G:       // Fall-through
        case state_t::SETUP_MENU_DAY_COLOUR_B:       // Fall-through
        case state_t::SETUP_MENU_NIGHT_COLOUR_INTRO: // Fall-through
        case state_t::SETUP_MENU_NIGHT_COLOUR_R:     // Fall-through
        case state_t::SETUP_MENU_NIGHT_COLOUR_G:     // Fall-through
        case state_t::SETUP_MENU_NIGHT_COLOUR_B:     // Fall-through
        case state_t::SETUP_MENU_ALARM_PATTERN:
            // Exit the menu, discarding changes.
            exit_menu(true);
            myState = state_t::CANCELLED;
            myCountdownTimer = SHOW_ALARM_COUNTDOWN;
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
    Serial.printf("countdown expired.\n");
    switch (myState) {
        case state_t::SHOW_IP_1:
            myState = state_t::SHOW_IP_2;
            myCountdownTimer = SHOW_IP_COUNTDOWN;
            break;
        case state_t::SHOW_IP_2:
            myState = state_t::SHOW_IP_3;
            myCountdownTimer = SHOW_IP_COUNTDOWN;
            break;
        case state_t::SHOW_IP_3:
            myState = state_t::SHOW_IP_4;
            myCountdownTimer = SHOW_IP_COUNTDOWN;
            break;
        case state_t::SHOW_IP_4:
            if (myLastTimestamp > 0) {
                myState = state_t::RUNNING;
            } else {
                myState = state_t::INITIALISING;
            }
            break;
        case state_t::SHOW_ALARM: // Fall-through
        case state_t::SHOW_SNOOZE: // Fall-through
        case state_t::CANCELLED:
            // It's time to show the clock again.
            myState = state_t::RUNNING;
            break;
        case state_t::SETUP_MENU_DAY_COLOUR_INTRO:
            myState = state_t::SETUP_MENU_DAY_COLOUR_R;
            break;
        case state_t::SETUP_MENU_NIGHT_COLOUR_INTRO:
            myState = state_t::SETUP_MENU_NIGHT_COLOUR_R;
            break;
    }
}

/**
 * Converts an alarm_t value into a representative string.
 * 
 * @param alarm The alarm to convert.
 * @return String representation of the alarm.
 */
const std::string alarmtToString(alarm_t alarm) {
    return ALARM_STRINGS[static_cast<int>(alarm)];
}

/**
 * Converts a display_pattern_t value into a representative string.
 * 
 * @param pattern The pattern to convert.
 * @return String representation of the pattern.
 */
const std::string displayPatternToString(display_pattern_t pattern) {
    return DISPLAY_PATTERN_STRINGS[static_cast<int>(pattern)];
}

/**
 * Converts a string to an alarm_t.
 * 
 * @param str The string to be converted.
 * @return The alarm_t that is represented by the string.
 */
alarm_t stringToAlarmt(std::string str) {
    for (int ii = 0; ii < MAX_ALARM_T_INDEX; ii++) {
        if (str == ALARM_STRINGS[ii]) {
            return static_cast<alarm_t>(ii);
        }
    }

    return alarm_t::ALARM_DISABLED;
}

/**
 * Converts a string to an display_pattern_t.
 * 
 * @param str The string to be converted.
 * @return The display_pattern_t that is represented by the string.
 */
display_pattern_t stringToPattern(std::string str) {
    for (int ii = 0; ii < MAX_DISPLAY_PATTERN_INDEX; ii++) {
        if (str == DISPLAY_PATTERN_STRINGS[ii]) {
            return static_cast<display_pattern_t>(ii);
        }
    }

    return display_pattern_t::SOLID_COLOUR;
}

/**
 * Converts a JSON array to a colour_t.
 * 
 * @param arr The JSON array containing red, green and blue values.
 * @return The colour_t representation of the colour data.
 */
colour_t arrayToColour(JsonArray arr) {
    colour_t colour;
    if (arr.size() != 3) {
        colour.r = 255;
        colour.g = 255;
        colour.b = 255;
    } else {
        colour.r = arr[0];
        colour.g = arr[1];
        colour.b = arr[2];
    }

    return colour;
}

/**
 * Sends a response to a request with a printf-style message.
 * 
 * @param request The request to which the reply is sent.
 * @param code The HTTP response code.
 * @param format The format of the message (printf-style.)
 * @param ... Variable arguments for filling in the format.
 */
void sendResponsePrintf(AsyncWebServerRequest *request, const int code, const char *format, ...) {
    // Prepare the buffer with the contents of the message.
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);

    // Send the response.
    if (request != NULL) {
        request->send(code, "text/plain", buffer);
    }
    // Clean up the variable length arguments.
    va_end(args);
}

/**
 * Retrieves a JSON representation of the current configuration.
 * 
 * @param request The web request retrieving the configuration.
 */
void getConfig(AsyncWebServerRequest *request) {
    #ifndef HIDE_DEBUG
    Serial.println("Retriving configuration for web client");
    #endif
    AsyncJsonResponse *response = new AsyncJsonResponse();
    JsonVariant root = response->getRoot();

    root["deviceName"] = myConfiguration.deviceName;
    root["alarmTime"] = myConfiguration.alarmTime;
    root["alarmActivation"] = alarmtToString(myConfiguration.alarmActivation);
    root["radioFrequency"] = myConfiguration.radioFrequency;
    root["brightness"] = myConfiguration.brightness;
    JsonArray dayColour = root["dayColour"].to<JsonArray>();
    dayColour.add(myConfiguration.dayColour.r);
    dayColour.add(myConfiguration.dayColour.g);
    dayColour.add(myConfiguration.dayColour.b);
    JsonArray nightColour = root["nightColour"].to<JsonArray>();
    nightColour.add(myConfiguration.nightColour.r);
    nightColour.add(myConfiguration.nightColour.g);
    nightColour.add(myConfiguration.nightColour.b);
    JsonArray alarmColour = root["alarmColour"].to<JsonArray>();
    alarmColour.add(myConfiguration.alarmColour.r);
    alarmColour.add(myConfiguration.alarmColour.g);
    alarmColour.add(myConfiguration.alarmColour.b);
    root["dayPattern"] = displayPatternToString(myConfiguration.dayPattern);
    root["nightPattern"] = displayPatternToString(myConfiguration.nightPattern);
    root["alarmPattern"] = displayPatternToString(myConfiguration.alarmPattern);
    root["latitude"] = myConfiguration.latitude;
    root["longitude"] = myConfiguration.longitude;
    root["timezone"] = myConfiguration.timezone;
    root["offset"] = myConfiguration.offset;
    root["isAlarmDisabled"] = myConfiguration.isAlarmDisabled;
    root["isRadioInstalled"] = myConfiguration.isRadioInstalled;
    root["is24Hour"] = myConfiguration.is24Hour;
    root["isUseRadio"] = myConfiguration.isUseRadio;
    root["version"] = myConfiguration.version;

    // Send the response back to the user.
    response->setLength();
    request->send(response);
}

/**
 * Writes a new configuration.
 * 
 * @param request The web request retrieving the configuration.
 * @param json The JSON data containing the new information.
 */
void writeConfig(AsyncWebServerRequest *request, JsonVariant &json) {
    #ifndef HIDE_DEBUG
    Serial.println("Received request to store the configuration.");
    #endif
    JsonObject jsonObj = json.as<JsonObject>();
    if (!jsonObj["deviceName"].is<JsonVariant>()) {
        // Missing or bad top-level information.
        sendResponsePrintf(request, 400, "Invalid top-level data");
        return;
    }

    // Copy the configuration.
    flash_config_t configuration;
    configuration.magic = MAGIC;
    const char *name = jsonObj["deviceName"];
    if ((name == NULL) || (strlen(name) == 0)) {
        // Missing or bad top-level information.
        sendResponsePrintf(request, 400, "Bad configuration.");
        return;
    } else {
        strncpy(configuration.deviceName, name, DEVICE_NAME_MAX_LEN);
        configuration.deviceName[DEVICE_NAME_MAX_LEN] = '\0';
    }

    configuration.alarmTime = jsonObj["alarmTime"];
    configuration.alarmActivation = stringToAlarmt(jsonObj["alarmActivation"]);
    configuration.radioFrequency = jsonObj["radioFrequency"];
    configuration.brightness = jsonObj["brightness"];
    configuration.dayColour = arrayToColour(jsonObj["dayColour"]);
    configuration.nightColour = arrayToColour(jsonObj["nightColour"]);
    configuration.alarmColour = arrayToColour(jsonObj["alarmColour"]);
    configuration.dayPattern = stringToPattern(jsonObj["dayPattern"]);
    configuration.nightPattern = stringToPattern(jsonObj["nightPattern"]);
    configuration.alarmPattern = stringToPattern(jsonObj["alarmPattern"]);
    configuration.latitude = jsonObj["latitude"];
    configuration.longitude = jsonObj["longitude"];
    strncpy(configuration.timezone, jsonObj["timezone"], TIMEZONE_MAX_LEN);
    configuration.timezone[TIMEZONE_MAX_LEN] = '\0';
    configuration.offset = jsonObj["offset"];
    configuration.isAlarmDisabled = myConfiguration.isAlarmDisabled;
    configuration.isRadioInstalled = myConfiguration.isRadioInstalled;
    configuration.is24Hour = jsonObj["is24Hour"];
    configuration.isUseRadio = jsonObj["isUseRadio"];
    strncpy(configuration.version, VERSION, VERSION_LEN);
    configuration.version[VERSION_LEN] = '\0';

    bool updateLocation = false;
    if ((strncmp(configuration.timezone, myConfiguration.timezone, TIMEZONE_MAX_LEN) != 0 ||
         configuration.latitude != myConfiguration.latitude ||
         configuration.longitude != myConfiguration.longitude) &&
        myState != state_t::INITIALISING) {
        updateLocation = true;
    }

    // Copy the structure to the configuration structure and write it to flash.
    copy_config(&myConfiguration, &configuration);
    if (myIsInMenu) {
        copy_config(&myNewConfiguration, &configuration);
    }
    write_config(&myConfiguration);

    if (updateLocation) {
        sun.setPosition(myConfiguration.latitude, myConfiguration.longitude, myConfiguration.offset);
        setenv("TZ", myConfiguration.timezone, 1);
        tzset();
        time_t now;
        time(&now);
        tm *tm_val = localtime(&now);
        syncSunClock(tm_val);
    }

    request->send(200);
}

/**
 * Set up WiFi, using WiFiManager to give the user a place to enter the
 * network credentials if necessary.
 */
void setupWifi() {
    WiFi.mode(WIFI_STA);
    WiFiManager wm;
    wm.setConfigPortalTimeout(CONFIG_TIMEOUT);
    boolean res = wm.autoConnect("ESPClock");
    if (!res) {
        // Startup failed, reboot.
        ESP.restart();
        delay(5000);
    }
    myIPAddress = WiFi.localIP();
}

/**
 * Starts the web server used for the normal operation of this device.
 */
bool setupWebServer() {
    // Set up the web server.
    webServer = new AsyncWebServer(80);

    // Set up the configuration retrieval.
    webServer->on("/config", HTTP_GET, getConfig);

    // Set up the device write handler.
    AsyncCallbackJsonWebHandler* handler = 
        new AsyncCallbackJsonWebHandler("/writeConfig", writeConfig);
    webServer->addHandler(handler);

    // Set up the static file sharing.
    webServer->serveStatic("/", LittleFS, "/").setDefaultFile("home.html").setCacheControl("max-age=3600");
    webServer->serveStatic("/icons", LittleFS, "/icons").setCacheControl("max-age=3600");

    // Set up the 404 handler.
    webServer->onNotFound([](AsyncWebServerRequest *request) {
        Serial.println("404, not found.");
        request->send(404);
    });

    // Start the web server.
    webServer->begin();
    Serial.printf("Web server started");
    return true;
}

/**
 * Sets up this device to receive OTA flash/firmware updates.
 */
void setupOTA() {
    // Setup over-the-air (OTA) flash updates.
    ArduinoOTA.onStart([]() {
        // Handles the starting of an OTA flash update.
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH) {
            type = "sketch";
        } else {
            type = "filesystem";
        }

        // Turn off the web server and LittleFS for uploads.
        webServer->end();
        LittleFS.end();
        Serial.printf("Start updating %s", type);
    });

    ArduinoOTA.onEnd([]() {
        // Handles the end of a OTA flash update.
        Serial.printf("OTA Update finished.");
    });

    ArduinoOTA.onError([](ota_error_t error) {
        // Handles an error in the OTA upload.
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) {
            Serial.printf("Auth Failed");
        } else if (error == OTA_BEGIN_ERROR) {
            Serial.printf("Begin Failed");
        } else if (error == OTA_CONNECT_ERROR) {
            Serial.printf("Connect Failed");
        } else if (error == OTA_RECEIVE_ERROR) {
            Serial.printf("Receive Failed");
        } else if (error == OTA_END_ERROR) {
            Serial.printf("End Failed");
        }
    });

    ArduinoOTA.begin();
}

/*
 * Setup routine run at power-on and reset times.
 */
void setup() {
    Serial.begin(115200);

    // Initialise the configuration.
    prefs.begin("esp-clock", false);
    size_t res = prefs.getBytes(KEY_CONFIG, &myConfiguration, sizeof(flash_config_t));
    if (res != sizeof(flash_config_t) || myConfiguration.magic != MAGIC) {
        // There is no saved configuration - set defaults.
        myConfiguration.magic = MAGIC;
        strcpy(myConfiguration.deviceName, "ESP Clock");
        myConfiguration.alarmTime = 6 * 60;   // 6 AM
        myConfiguration.alarmActivation = alarm_t::ALARM_DISABLED;
        myConfiguration.radioFrequency = 993; // Triple J Perth
        myConfiguration.brightness = 0x0F;    // Maximum brightness
        myConfiguration.dayColour.r = 0xFF;
        myConfiguration.dayColour.g = 0xFF;
        myConfiguration.dayColour.b = 0xFF;
        myConfiguration.nightColour.r = 0xFF;
        myConfiguration.nightColour.g = 0x00;
        myConfiguration.nightColour.b = 0x00;
        myConfiguration.alarmColour.r = 0x00;
        myConfiguration.alarmColour.g = 0x00;
        myConfiguration.alarmColour.b = 0xFF;
        myConfiguration.dayPattern = display_pattern_t::RAINBOW_DIGITS;
        myConfiguration.nightPattern = display_pattern_t::SOLID_COLOUR;
        myConfiguration.alarmPattern = display_pattern_t::RAINBOW_DIGITS;
        myConfiguration.latitude = LATITUDE;
        myConfiguration.longitude = LONGITUDE;
        strcpy(myConfiguration.timezone, "AWST-8");
        myConfiguration.offset = 8;
        myConfiguration.isAlarmDisabled = false;
        myConfiguration.isRadioInstalled = true;
        myConfiguration.is24Hour = true;
        myConfiguration.isUseRadio = false;
    }

    // Set the version string each time.
    strncpy(myConfiguration.version, VERSION, VERSION_LEN);
    myConfiguration.version[VERSION_LEN] = '\0';

    // Initialise the I/O pins.
    pinMode(PIN_BUZZER, OUTPUT);
    pinMode(PIN_ALARM_ENABLE, INPUT_PULLUP);
    pinMode(PIN_NO_ALARM, INPUT_PULLUP);
    digitalWrite(PIN_BUZZER, LOW);
    myIsAlarmSwitchEnabled = digitalRead(PIN_ALARM_ENABLE) == LOW;
    
    // Prepare the LEDs.
    leds.Begin();
    display(FONT_BLANK, FONT_BLANK, FONT_BLANK, FONT_BLANK, false, false, false);

    // Initialise the radio, if it exists.
    if (myConfiguration.isRadioInstalled) {
        Wire.setPins(PIN_RADIO_SDA, PIN_RADIO_SCL);
        if (!radio.initWire(Wire)) {
            // The radio couldn't be initialised.
            Serial.println("Radio couldn't be initialised.");
            myConfiguration.isRadioInstalled = false;
        } else {
            radio.setMute(true);
            radio.setMono(true);
            Serial.println("Radio initialised.");
        }
    }

    // Check if alarms are disabled permanently.
    myConfiguration.isAlarmDisabled = (digitalRead(PIN_NO_ALARM) == LOW);

    // Initialise the clock's state.
    pinMode(PIN_ENCODER_SW, INPUT_PULLUP);
    if (digitalRead(PIN_ENCODER_SW) == LOW) {
        // Holding down the button during power-on enters the setup menu.
        // This can't happen normally, as it's cumbersome to everyday operation.
        if (myConfiguration.isRadioInstalled) {
            myState = state_t::SETUP_MENU_RADIO_WHOLE;
        } else {
            myState = state_t::SETUP_MENU_12_24_HOURS;
        }
    } else {
        myState = state_t::INITIALISING;
    }
    
    // Set up the WiFi connection.
    setupWifi();

    if (myState == state_t::INITIALISING) {
        // Now we have an IP address, show it.
        myState = state_t::SHOW_IP_1;
        myCountdownTimer = SHOW_IP_COUNTDOWN;
    }
    
    // Set up the file system.
    if (!LittleFS.begin()) {
        Serial.println("Unable to start LittleFS.");
        delay(1000);
        ESP.restart();
    }

    // Set up the web server.
    if (!setupWebServer()) {
        delay(1000);
        ESP.restart();
    }

    setupOTA();

    // Initialise the sunset calculator.
    sun.setPosition(myConfiguration.latitude, myConfiguration.longitude, TZ_OFFSET);

    // Start the NTP clock.
    //Serial.println("Initialising NTP.");
    sntp_set_time_sync_notification_cb(ntp_time_received_cb);
    // settimeofday_cb(ntp_time_received_cb);
    configTime(0, 0, "10.0.1.1", "pool.ntp.org");
    setenv("TZ", myConfiguration.timezone, 1);
    tzset();

    // Enable OTA flashing.
    //Serial.println("Initialising OTA");
    ArduinoOTA.setPort(8266);
    ArduinoOTA.onStart([]() {
        Serial.printf("OTA starting\n");
    });
    ArduinoOTA.onEnd([]() {
        Serial.printf("OTA Complete\n");
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        switch (error) {
            case OTA_AUTH_ERROR:
                Serial.printf("Auth Failed.\n");
                break;
            case OTA_BEGIN_ERROR:
                Serial.printf("Begin Failed.\n");
                break;
            case OTA_CONNECT_ERROR:
                Serial.printf("Connect Failed.\n");
                break;
            case OTA_RECEIVE_ERROR:
                Serial.printf("Receive Failed.\n");
                break;
            case OTA_END_ERROR:
                Serial.printf("End Failed.\n");
                break;
        }
    });
    ArduinoOTA.begin();

    // Initialise the button.
    myButton.begin();

    // Initialise the buzzer.
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, HIGH);

    // Initialise the rotary encoder.
    attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_A), rotary_tick, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_B), rotary_tick, CHANGE);

    //Serial.println("Clock started successfully.");
    Serial.printf("Clock started successfully.\n");
}

/*
 * Called continuously by the controller to execute the program.
 */
void loop() {
    unsigned long loopStartTime = millis();

    // Handle any OTA updates.
    ArduinoOTA.handle();

    myBrightnessCounter--;
    if (myBrightnessCounter <= 0) {
        // Check the brightness.
        uint16_t ldr = analogRead(PIN_LDR);
        set_brightness(ldr);
        myBrightnessCounter = BRIGHTNESS_CHECK_COUNTDOWN;
    }

    // Update the coundown timer.
    if (myCountdownTimer > 0) {
        myCountdownTimer--;
        if (myCountdownTimer == 0) {
            countdown_expired();
        }
    }

    // Update the animation step.
    display_pattern_t pattern;
    if (myIsInMenu) {
        pattern = display_pattern_t::MENU;
    } else if (myAlarmState == alarm_state_t::ACTIVE) {
        pattern = myConfiguration.alarmPattern;
    } else if (myIsDaytime) {
        pattern = myConfiguration.dayPattern;
    } else {
        pattern = myConfiguration.nightPattern;
    }
    switch (pattern) {
        case display_pattern_t::FLASHING:
            myAnimationStep = (myAnimationStep + 1) % MAX_ANIMATION_STEP_FLASH;
            break;
        case display_pattern_t::PULSING:
            myAnimationStep = (myAnimationStep + 1) % MAX_ANIMATION_STEP_PULSE;
            break;
        case display_pattern_t::RAINBOW_DIGITS:
            myAnimationStep = (myAnimationStep + 1) % MAX_ANIMATION_STEP_RAINBOW_DIGITS;
            break;
        case display_pattern_t::RAINBOW_SEGMENTS:
            myAnimationStep = (myAnimationStep + 1) % MAX_ANIMATION_STEP_RAINBOW_SEGMENTS;
            break;
    }

    // Update the buzzer.
    if (myAlarmState == alarm_state_t::ACTIVE && !myConfiguration.isUseRadio) {
        if (myBuzzerTicksRemaining == 1) {
            myBuzzerStep = (myBuzzerStep + 1) % BUZZER_STEP_COUNT;
            digitalWrite(PIN_BUZZER, ((myBuzzerStep % 2) == 0 ? HIGH : LOW));
            myBuzzerTicksRemaining = BUZZER_STEP_TICKS[myBuzzerStep];
            
        } else if (myBuzzerTicksRemaining > 0) {
            myBuzzerTicksRemaining--;
        }
    }

    // Read the alarm enable switch.
    myIsAlarmSwitchEnabled = digitalRead(PIN_ALARM_ENABLE) == LOW;
    if (!myIsAlarmSwitchEnabled && myAlarmState != alarm_state_t::INACTIVE) {
        // Turn off the alarm.
        stop_alarm();
    }

    // Read the encoder.
    static long lastEncoderPos = -1;
    long encoderPos = myEncoder.getPosition();
    //myEncoder.setPosition(0);
    if (encoderPos != lastEncoderPos) {
        // The encoder has been changed by the user since the last loop.
        digitalWrite(PIN_BUZZER, !digitalRead(PIN_BUZZER));
        Serial.printf("Encoder moved to: %ld.\n", encoderPos);
        rotate(encoderPos - lastEncoderPos);
        lastEncoderPos = encoderPos;
    }

    // Read the top button.
    myButton.read();
    if (myButton.pressedFor(LONG_PRESS_INTERVAL)) {
        // This is a long press.
        if (!myIsLongPress) {
            // Register it as a new long-press.
            myIsLongPress = true;
            myWasPressed = false;
            long_press();
        }
    } else if (myButton.wasPressed()) {
        Serial.printf("Button was pressed.\n");
        // See if the releasing of the button was from a long press.
        if (myIsLongPress) {
            myIsLongPress = false;
        } else {
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
    } else if (myWasPressed) {
        if (myButton.releasedFor(DOUBLE_CLICK_INTERVAL)) {
            // The double-click has timed out, so it's a click.
            click();
            myWasPressed = false;
        }
    }

    // Update the time if necessary.
    if ((myState != state_t::INITIALISING) && (myLastTimestamp != 0)) {
        time_t now;
        time(&now);
        if (now != myLastTimestamp) {
            // The second has changd.
            if (myAlarmState == alarm_state_t::SNOOZE) {
                if (mySnoozeRemaining == 1) {
                    // The snooze timer has expired.
                    start_alarm();
                } else {
                    mySnoozeRemaining--;
                }
            }
            if ((now / SECONDS_PER_MINUTE) != (myLastTimestamp / SECONDS_PER_MINUTE)) {
                // The minute has changed.
                tm *tm_val = localtime(&now);
                myHour = tm_val->tm_hour;
                myMinute = tm_val->tm_min;
                myMinuteOfDay = (((uint16_t)myHour) * MINUTES_PER_HOUR) + myMinute;
                
                // Check for alarm.
                if (!myConfiguration.isAlarmDisabled &&
                    myIsAlarmSwitchEnabled &&
                    myMinuteOfDay == myConfiguration.alarmTime &&
                    (myConfiguration.alarmActivation == alarm_t::ALL_DAYS ||
                     myConfiguration.alarmActivation == alarm_t::ONE_TIME ||
                     (myConfiguration.alarmActivation == alarm_t::WEEKDAYS &&
                      tm_val->tm_wday != WDAY_SUNDAY &&
                      tm_val->tm_wday != WDAY_SATURDAY))) {
                    start_alarm();
                }

                if ((now / SECONDS_PER_HOUR) != (myLastTimestamp / SECONDS_PER_HOUR)) {
                    // The hour has changed, recalculate sunrise/sunset.
                    syncSunClock(tm_val);
                }

                // Determine if we're currently in daytime or nighttime.
                checkDaytime(tm_val);
            }

            // Update the last processed timestamp.
            myLastTimestamp = now;
        }
    }

    // Choose what to display based on the current state.
    update_display();

    // Determine how long the delay should be to match our tick duration.
    unsigned long now = millis();
    unsigned long executionDuration = now - loopStartTime;
    unsigned long delayTime = LOOP_DELAY - executionDuration;
    if ((delayTime != 0) && 
        (LOOP_DELAY > executionDuration) &&
        (delayTime <= (LOOP_DELAY + LOOP_DELAY))) {
        delay(delayTime);
    }
}