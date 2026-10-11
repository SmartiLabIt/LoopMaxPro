# MeteoWifi: versions

What each version fixes and adds. The newest on top. Written in English: the notes are
shown as they are, in the console and on the server.

## 1.0.4 (2026-10-11)

- No change in how it works: comments and texts of the code

## 1.0.3 (2026-10-05)

- The screen is found on the boards that carry it on other pins. NodeMCU V3 boards with a screen that look the same have it on pins 4 and 5, or on 14 and 12: the module looks in both places. On the second kind the screen stayed dark
- The place where the screen answered is remembered: from the second start it is on in one second, without looking for it
- On every board the screen is on about two seconds earlier at the start
- A board without a screen says so once in its log and leaves the bus free, for who wants to look for a screen from the console

## 1.0.2 (2026-10-05)

- The names of what MeteoWifi shows are told in English and in Italian: the server uses them for Telegram, and Home Assistant keeps getting the one of the language of the device

## 1.0.1 (2026-10-05)

- Home Assistant: temperature, feels like, humidity and wind as sensors, and a button to refresh the weather, on the ESP32 boards

## 1.0.0

- First version: the weather now and for the next days, in the console and on the screen of the board
- The city is picked from a complete list, with province, region and country: any city in the world
