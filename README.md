# IoT-based Smart Keybox with NFC Authentication

An IoT-based smart keybox with NFC authentication, MQTT connectivity, an LCD
status display, a relay-controlled lock, and monitoring for eight keys.

## Hardware

- ESP32 development board
- PN532 NFC reader
- 16x2 I2C LCD
- Relay module
- Buzzer
- Eight key-presence sensors or switches

## Arduino libraries

- [PubSubClient](https://github.com/knolleary/pubsubclient)
- [Adafruit PN532](https://github.com/adafruit/Adafruit-PN532)
- LiquidCrystal I2C

## Setup

1. Copy `secrets.example.h` to `secrets.h`.
2. Add your Wi-Fi and MQTT connection details to `secrets.h`.
3. Open `smartkeybox.ino` in the Arduino IDE.
4. Install the required libraries and select your ESP32 board.
5. Compile and upload the sketch.

`secrets.h` is ignored by Git so credentials are not committed.
