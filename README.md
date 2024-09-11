# CYD-timetable
Firmware for Cheap Yellow Display (ESP 32) to show next subway trains from desired station

![CYD with loaded app](/img/cyd-live.jpg)

## Components

- CYD based on ESP32
- [3D Printed case](https://www.printables.com/model/685845-enclosure-for-sunton-esp32-2432s028r-cheap-yellow-)
- 4 threaded inserts
- 4 screws

## Software

Self-created firmware. Has some bugs, but is working still. Features:

- Shows next 3 SL trains from your station(hardcoded in firmware) to desired direction (to/from T-Centralen)
- Show current/feeling temperature and wind
- Dim the screen in 30 seconds. Wakes up on touch the screen
