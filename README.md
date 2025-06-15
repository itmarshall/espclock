# ESP Clock
ESP32 powered radio alarm clock, using WS2812 LEDs for each segment of the display.

This also has the functionality to be used as a plain clock with no alarms.

The GPIO pins currently defined:
| GPIO | Direction | Description |
| ---- | --------- | ----------- |
| 16 | Input | Rotary encoder A |
| 17 | Input | Rotary encoder B |
| 18 | Input | Rotary encoder SPST switch |
| 19 | Output | WS2812B LED output |
| 21 | Output | Radio SDA (if fitted) |
| 22 | Output | RADIO SCL (if fitted) |
| 23 | Output | Buzzer output (if fitted) |
| 25 | Input | Alarm enabled SPST switch |
| 33 | Input | Disables all alarm functionality (including in web UI) if pulled LOW |
| 39 | Input| LDR brightness input (analog, 0-3.3V) |

## Platform
This project uses Platform.io for builds using the Arduino platform. 

All code is under the GPL v2 licence.