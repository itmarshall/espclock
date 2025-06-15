#ifndef MAIN_H
#define MAIN_H

#include <Arduino.h>
// #include <ESP8266WiFi.h>          //ESP8266 Core WiFi Library (you most likely already have this in your sketch)

#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
// #include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include <IPAddress.h>
#include <WiFiUdp.h>
#include <Preferences.h>

// Time handling.
#include <time.h>
#include <sys/time.h>
#include <esp_sntp.h>

// #include <coredecls.h>
#include <ArduinoOTA.h>
#include <RotaryEncoder.h>
#include <JC_Button.h>
#include <NeoPixelBus.h>
#include <LittleFS.h>
#include <sunset.h>
#include <TEA5767.h>

// Web server.
#define WEBSERVER_H
// #include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>

// Disables the writing the configuration to flash for rapid testing/debugging.
// #define DISABLE_CONFIG_WRITES 1

// The version of the code, used for identification purposes.
const char VERSION[] = "1.0";

// The length of the version string (including '\0' character.)
const int VERSION_LEN = 4;

/*
 * Pin allocations:
 * Encoder A     - GPIO 16
 * Encoder B     - GPIO 17
 * Encoder SW    - GPIO 18
 * LEDs          - GPIO 19
 * Radio SDA     - GPIO 21
 * Radio SCL     - GPIO 22
 * Buzzer        - GPIO 23
 * Alarm Enable  - GPIO 25
 * Alarm Disable - GPIO 33
 * LDR           - GPIO 36
 */

// The pin used for the rotary encoder "A" input.
const uint8_t PIN_ENCODER_A = 16;

// The pin used for the rotary encoder "B" input.
const uint8_t PIN_ENCODER_B = 17;

// The pin used for the rotary encoder press switch input.
const uint8_t PIN_ENCODER_SW = 18;

// The pin used for the WS2812B LED string.
const uint8_t PIN_LEDS = 19;

// The serial data I2C line for the radio.
const uint8_t PIN_RADIO_SDA = 21;

// The serial clock I2C line for the radio.
const uint8_t PIN_RADIO_SCL = 22;

// The pin used for the alarm buzzer.
const uint8_t PIN_BUZZER = 23;

// The pin used for reading the alarm enable switch.
const uint8_t PIN_ALARM_ENABLE = 25;

// The pin used to disable all alarm functionality.
const uint8_t PIN_NO_ALARM = 33;

// The pin used for reading the light dependent resistor.
const uint8_t PIN_LDR = 36;

// The number of segments in a single digit.
const uint8_t SEGMENTS_PER_DIGIT = 7;

// The server to which debug UDP messages are sent.
const IPAddress DEBUG_SERVER = IPAddress(10, 0, 1, 253);

// The port on the server to which debug UDP messages are sent.
const uint16_t DEBUG_PORT = 65432;

// Key used for storing and retrieving the configuration.
const char* KEY_CONFIG = "config";

// The magic number to use for the configuration.
const uint32_t MAGIC = 0xc10c0001;

// The maximum length of the name of this device.
const int DEVICE_NAME_MAX_LEN = 40;

// The maximum length of a timezone name.
const int TIMEZONE_MAX_LEN = 28;

// The amount of time (in seconds) to wait for a network connections before 
// starting the configuration server,
const unsigned long CONFIG_TIMEOUT = 300;

// The amount of time (in milliseconds) that the user has to hold a button for
// before it is counted as a long press.
const uint32_t LONG_PRESS_INTERVAL = 2000;

// The amount of time (in milliseconds) to wait after a click for a 
// double-click to be triggered.
const uint32_t DOUBLE_CLICK_INTERVAL = 300;

// THe number of milliseconds to wait between loop executions.
const uint32_t LOOP_DELAY = 10;

// The number of loops to go through before the show alarm state ends.
const uint32_t SHOW_ALARM_COUNTDOWN = 3 * (1000 / LOOP_DELAY);

// The number of loops to go through before the show snooze state ends.
const uint32_t SHOW_SNOOZE_COUNTDOWN = 3 * (1000 / LOOP_DELAY);

// The number of loops to go through before the cancelled state ends.
const uint32_t SHOW_CANCEL_COUNTDOWN = 3 * (1000 / LOOP_DELAY);

// The number of loops to go through before an introduction state ends.
const uint32_t SHOW_INTRO_COUNTDOWN = 3 * (1000 / LOOP_DELAY);

// The number of loops to go through showing an IP number.
static const uint8_t SHOW_IP_COUNTDOWN = (1000 / LOOP_DELAY);

// The alarm duration after which the alarm is stopped automatically (seconds).
const uint16_t ALARM_DURATION = 600;

// The initial snooze duration after the alarm is stopped (seconds).
const uint16_t SNOOZE_DURATION = 300;

// The maximum allowed snooze value.
const uint16_t MAX_SNOOZE = 5999;

// The minimum tunable radio frequency (Hz).
const uint16_t MIN_RADIO_FREQUENCY = 88;

// The maximum tunable radio frequency (Hz).
const uint16_t MAX_RADIO_FREQUENCY = 107;

// The maximum brightness read from the LDR.
const uint16_t MAX_BRIGHTNESS_INPUT = 4095;

// The minimum brightness for the LEDs.
const uint8_t MIN_BRIGHTNESS = 0x01;

// The maximum brightness for the LEDs.
const uint8_t MAX_BRIGHTNESS = 0x0F;

// The maximum brightness for the LEDs, pre-converted to float.
const float MAX_BRIGHTNESS_F = (float)MAX_BRIGHTNESS;

// The number of loops to go through between brightness checks.
const int32_t BRIGHTNESS_CHECK_COUNTDOWN = 10;

// The number of hours in a day.
const uint8_t HOURS_PER_DAY = 24;

// The number of minutes in an hour.
const uint8_t MINUTES_PER_HOUR = 60;

// The number of seconds in a minute.
const uint8_t SECONDS_PER_MINUTE = 60;

// The number of seconds in an hour.
const uint16_t SECONDS_PER_HOUR = 3600;

// The latitude for sunrise/sunset calculations.
const double LATITUDE = -31.9514;

// The longitude for sunrise/sunset calculations.
const double LONGITUDE = 115.8617;

// The timezone offset for sunrise/sunset calculations.
const double TZ_OFFSET = 8;

// The time zone for time calculations.
const char* TIMEZONE = "AWST-8";

// The value for TM_WDAY for Sunday.
const int WDAY_SUNDAY = 0;

// The value for TM_WDAY for Saturday.
const int WDAY_SATURDAY = 6;

// The number of LEDs to use for the display.
const uint16_t LED_COUNT = 32;

// Black (off) colour used for the LED display.
static const HslColor BLACK(RgbColor(0, 0, 0));

// Menu bright colour. Used in pulsing animation and setting value.
static const HslColor MENU_BRIGHT(RgbColor(255));

// Menu dim colour. Used in pulsing animation.
static const HslColor MENU_DIM(RgbColor(64));

// Pure red colour.
static const HslColor COLOUR_R(RgbColor(255, 0, 0));

// Pure red colour.
static const HslColor COLOUR_G(RgbColor(0, 255, 0));

// Pure red colour.
static const HslColor COLOUR_B(RgbColor(0, 0, 255));

// The amount the hue will change between each digit.
static const float DIGIT_GROUP_MULTIPLIER = 1.0f / 6.0f;

// The amount the hue will change between each segment.
static const float SEGMENT_MULTIPLIER = 1.0f / 32.0f;

// The amount the hue will change for a full digit pattern each step.
static const float DIGIT_COLOUR_STEP = 0.005;

// The amount the hue will change for each segment pattern each step.
static const float SEGMENT_COLOUR_STEP = 0.002;

// The number of animation steps that a flashing pattern is on.
static const int FLASH_ON_STEPS = (750 / LOOP_DELAY);

// The number of animation steps that a flashing pattern is off.
static const int FLASH_OFF_STEPS = (250 / LOOP_DELAY);

// The number of animation steps for each half of a pulse sequence.
static const int PULSE_STEPS = (1000 / LOOP_DELAY);

// The maximum animation step when flashing before it restarts.
static const int MAX_ANIMATION_STEP_FLASH = FLASH_ON_STEPS + FLASH_OFF_STEPS;

// The maximum animation step when pulsing before it restarts.
static const int MAX_ANIMATION_STEP_PULSE = 2 * PULSE_STEPS;

// The maximum animation step when showing rainbow digits before it restarts.
static const int MAX_ANIMATION_STEP_RAINBOW_DIGITS = 1.0f / DIGIT_COLOUR_STEP;

// The maximum animation step when showing rainbow segments before it restarts.
static const int MAX_ANIMATION_STEP_RAINBOW_SEGMENTS = 1.0f / SEGMENT_COLOUR_STEP;

// The number of permittable alarm patterns, excluding the menu pattern.
static const uint8_t ALARM_PATTERN_COUNT = 4;

// The number of ticks in a short (on or off) duration.
const uint8_t BUZZER_SHORT_TICK_COUNT = 60 / LOOP_DELAY;

// The number of ticks in a long (on or off) duration.
const uint8_t BUZZER_LONG_TICK_COUNT = 540 / LOOP_DELAY;

// The number of ticks for each step in a buzzer sequence.
const uint8_t BUZZER_STEP_TICKS[] = {
    BUZZER_SHORT_TICK_COUNT,
    BUZZER_SHORT_TICK_COUNT,
    BUZZER_SHORT_TICK_COUNT,
    BUZZER_SHORT_TICK_COUNT,
    BUZZER_SHORT_TICK_COUNT,
    BUZZER_SHORT_TICK_COUNT,
    BUZZER_SHORT_TICK_COUNT,
    BUZZER_LONG_TICK_COUNT
};
const uint8_t BUZZER_STEP_COUNT = 8;

// The font index to use for the "A" character.
const uint8_t FONT_A = 10;

// The font index to use for the "b" character.
const uint8_t FONT_B = 11;

// The font index to use for the "c" character.
const uint8_t FONT_C = 12;

// The font index to use for the "d" character.
const uint8_t FONT_D = 13;

// The font index to use for the "E" character.
const uint8_t FONT_E = 14;

// The font index to use for the "F" character.
const uint8_t FONT_F = 15;

// The font index to use for the "G" character.
const uint8_t FONT_G = 16;

// The font index to use for the "H" character.
const uint8_t FONT_H = 17;

// The font index to use for the "i" character.
const uint8_t FONT_I = 18;

// The font index to use for the "L" character.
const uint8_t FONT_L = 19;

// The font index to use for the "n" character.
const uint8_t FONT_N = 20;

// The font index to use for the "r" character.
const uint8_t FONT_R = 21;

// The font index to use for the "-" character.
const uint8_t FONT_DASH = 22;

// The font index to use for blank (empty) character.
const uint8_t FONT_BLANK = 23;

//      a 
//     --- 
//  f | g | b
//     ---
//  e |   | c
//     ---
//      d
// Font digit bit order:  Xgfedcba
const uint8_t FONT[] = {0b00111111, // 0
                        0b00000110, // 1
                        0b01011011, // 2
                        0b01001111, // 3
                        0b01100110, // 4
                        0b01101101, // 5
                        0b01111101, // 6
                        0b00000111, // 7
                        0b01111111, // 8
                        0b01101111, // 9
                        0b01110111, // A
                        0b01111100, // b
                        0b01011000, // c
                        0b01011110, // d
                        0b01111001, // E
                        0b01110001, // F
                        0b00111101, // G
                        0b01110110, // H
                        0b00000100, // i
                        0b00111000, // L
                        0b01010100, // n
                        0b01010000, // r
                        0b01000000, // -
                        0b00000000, // (blank)
                        };

const uint8_t FONT_SEGMENT_ORDER[][7] = {{0, 1, 2, 3, 4, 5, 9}, // 0
                                         {9, 0, 1, 9, 9, 9, 9}, // 1
                                         {0, 1, 9, 4, 3, 9, 2}, // 2
                                         {0, 1, 3, 4, 9, 9, 2}, // 3
                                         {9, 0, 2, 9, 9, 0, 1}, // 4
                                         {0, 9, 3, 4, 9, 1, 2}, // 5
                                         {0, 9, 3, 4, 5, 1, 2}, // 6
                                         {0, 1, 2, 9, 9, 9, 9}, // 7
                                         {0, 1, 2, 3, 4, 5, 6}, // 8
                                         {2, 3, 4, 5, 9, 1, 0}, // 9
                                         {2, 3, 4, 9, 0, 1, 5}, // A
                                         {9, 9, 2, 3, 4, 0, 1}, // b
                                         {9, 9, 9, 0, 1, 9, 2}, // c
                                         {9, 0, 1, 2, 3, 9, 4}, // d
                                         {0, 9, 9, 4, 3, 0, 1}, // E
                                         {0, 9, 9, 9, 3, 0, 1}, // F
                                         {0, 9, 4, 3, 2, 1, 9}, // G
                                         {9, 3, 4, 9, 1, 0, 2}, // H
                                         {9, 9, 0, 9, 9, 9, 9}, // i
                                         {9, 9, 9, 2, 1, 0, 9}, // L
                                         {9, 9, 2, 9, 0, 9, 1}, // n
                                         {9, 9, 9, 9, 0, 9, 1}, // r
                                         {9, 9, 9, 9, 9, 9, 0}, // -
                                         {9, 9, 9, 9, 9, 9, 9}  // (blank)
                                        };

// The states that the clock may be in.
typedef enum {
    INITIALISING,
    SHOW_IP_1,
    SHOW_IP_2,
    SHOW_IP_3,
    SHOW_IP_4,
    RUNNING,
    SHOW_ALARM,
    SHOW_SNOOZE,
    MENU_ALARM_HOURS,
    MENU_ALARM_MINUTES,
    MENU_ALARM_DAYS,
    SETUP_MENU_RADIO_WHOLE,
    SETUP_MENU_RADIO_FRACTION,
    SETUP_MENU_12_24_HOURS,
    SETUP_MENU_BRIGHTNESS,
    SETUP_MENU_DAY_COLOUR_INTRO,
    SETUP_MENU_DAY_COLOUR_R,
    SETUP_MENU_DAY_COLOUR_G,
    SETUP_MENU_DAY_COLOUR_B,
    SETUP_MENU_NIGHT_COLOUR_INTRO,
    SETUP_MENU_NIGHT_COLOUR_R,
    SETUP_MENU_NIGHT_COLOUR_G,
    SETUP_MENU_NIGHT_COLOUR_B,
    SETUP_MENU_ALARM_PATTERN,
    CANCELLED
} state_t;

typedef enum {
    INACTIVE,
    ACTIVE,
    SNOOZE
} alarm_state_t;

// The setting values for the alarm.
typedef enum {
    ALARM_DISABLED,
    ONE_TIME,
    WEEKDAYS,
    ALL_DAYS
} alarm_t;

const char* ALARM_STRINGS[] = {
    "ALARM_DISABLED",
    "ONE_TIME",
    "WEEKDAYS",
    "ALL_DAYS"
};

const int MAX_ALARM_T_INDEX = static_cast<int>(alarm_t::ALL_DAYS);

typedef enum {
    SOLID_COLOUR,
    RAINBOW_DIGITS,
    RAINBOW_SEGMENTS,
    FLASHING,
    PULSING,
    MENU
} display_pattern_t;

const char* DISPLAY_PATTERN_STRINGS[] = {
    "SOLID_COLOUR",
    "RAINBOW_DIGITS",
    "RAINBOW_SEGMENTS",
    "FLASHING",
    "PULSING",
    "MENU"
};

const int MAX_DISPLAY_PATTERN_INDEX = static_cast<int>(display_pattern_t::MENU);

// Colour structure used in the configuration.
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} colour_t;

// The configuration for the clock.
typedef struct {
    uint32_t magic;
    char deviceName[DEVICE_NAME_MAX_LEN + 1];
    uint32_t alarmTime;
    alarm_t alarmActivation;
    uint16_t radioFrequency;
    uint8_t brightness;
    colour_t dayColour;
    colour_t nightColour;
    colour_t alarmColour;
    display_pattern_t dayPattern;
    display_pattern_t nightPattern;
    display_pattern_t alarmPattern;
    double latitude;
    double longitude;
    char timezone[TIMEZONE_MAX_LEN + 1];
    double offset;
    bool isAlarmDisabled;
    bool isRadioInstalled;
    bool is24Hour;
    bool isUseRadio;
    char version[VERSION_LEN + 1];
} flash_config_t;

#endif