# TallyTrek

![Board](https://img.shields.io/badge/Board-ESP32--S2%20Reverse%20TFT-E7352C?logo=espressif&logoColor=white)
![Arduino](https://img.shields.io/badge/Arduino-ESP32%20Core%203.3-00878F?logo=arduino&logoColor=white)
![Sensor](https://img.shields.io/badge/Sensor-RCWL--0516-orange)
![Logging](https://img.shields.io/badge/Logging-microSD%20CSV-informational)
![Power](https://img.shields.io/badge/Power-LiPo%20%2B%20Deep%20Sleep-yellow)
![License](https://img.shields.io/badge/License-MIT-green)

A low-cost trail counter that detects each trail user with a microwave sensor and logs the event, with a timestamp, to a .csv file on a microSD
card. A built-in display shows the counts, battery level, and sensor status, so you can check a counter in the field without a laptop.

TallyTrek is an updated version of NiftyCounter from Loren Konkus's [trail-counters](https://github.com/LorenKonkus/trail-counters) project. It
fixes counting, data loss, and power problems found in the original.

---

## Photos

| Assembled counter | Dashboard |
|---|---|
| ![Assembled counter: Feather and Adalogger stack with sensor](./assets/images/NiftyCounter.jpg) | ![Dashboard showing time, battery, sensor status, and counts](./assets/images/NiftyCounterDisplay.jpg) |

| NiftyCase, closed | NiftyCase, mounted |
|---|---|
| ![3D printed NiftyCase enclosure, closed](./assets/images/NiftyCaseClosed.jpg) | ![NiftyCase mounted on a tree](./assets/images/NiftyCaseTree.jpg) |

> These photos show the original NiftyCounter build. TallyTrek runs on the same hardware.

---

## Why

Trail builders and advocacy groups get asked how many people use their trails, and when. Commercial trail counters answer that well, but they cost
hundreds of dollars per sensor and often need proprietary hardware and software subscriptions. Counting trail users is a simple job: notice someone
passing and write down the time. Off-the-shelf boards can do that for much less, and a cheap counter can go in more places with less worry about
losing it.

---

## Hardware

### Parts

| Part | Notes |
|---|---|
| [Adafruit ESP32-S2 Reverse TFT Feather](https://www.adafruit.com/product/5345) | Processor, 240x135 display, three buttons, LiPo charger, and battery monitor. The -S3 version works too |
| [Adafruit Adalogger FeatherWing](https://www.adafruit.com/product/2922) | microSD card slot and battery-backed PCF8523 real-time clock. Stacks on the Feather |
| RCWL-0516 microwave sensor | Sees through plastic and wood. See [Sensor Overview](./docs/SensorOverview.md) |
| LiPo battery | Any 3.7V LiPo with a JST-PH connector |
| microSD card | FAT formatted |
| NiftyCase (optional) | 3D printed enclosure. See [Packaging Options](./docs/PackagingOptions.md) |

### Wiring

| RCWL-0516 pin | Feather pin |
|---|---|
| **VIN** | **BAT** (or 3V) |
| **GND** | **GND** |
| **OUT** | **12** |
| 3V3, CDS | Not connected |

> **⚠️ Power the sensor through VIN, not 3V3.** The sensor's 3V3 pin is the output of its own regulator. Feeding the battery into it skips the
> regulator, so the sensor's output can rise above the 3.6V the ESP32-S2's pins are rated for.

> **Sensor placement:** the side with the components on it is the front, so face it toward the trail. Keep the sensor a couple of inches from the
> boards and battery, with no metal in front of it or within 1 cm behind it. If you move OUT to another pin, it has to be an RTC-capable GPIO
> (0 through 21) so the sensor can wake the board from deep sleep, and `SENSOR_IN` in the firmware has to match.

---

## Installing the Firmware

### 1. Install the board package

In the Arduino IDE, open **Boards Manager** and install **esp32 by Espressif Systems**.

### 2. Install the libraries

Open **Library Manager** and install:

| Library | Used for |
|---|---|
| Adafruit ST7735 and ST7789 | Display |
| Adafruit GFX | Display graphics |
| Adafruit MAX1704X | Battery monitor |
| RTClib | Real-time clock |

The SD card library comes with the ESP32 board package.

### 3. Upload

1. Open the `TallyTrek` folder in the Arduino IDE
2. Select the **Adafruit Feather ESP32-S2 Reverse TFT** board and its port
3. Click **Upload**

Or, with arduino-cli:

```sh
arduino-cli compile --fqbn esp32:esp32:adafruit_feather_esp32s2_reversetft TallyTrek
arduino-cli upload --fqbn esp32:esp32:adafruit_feather_esp32s2_reversetft -p <port> TallyTrek
```

> **Board not showing up?** Its USB port disappears while it's in deep sleep. Press D1 or D2 to wake it, then upload.

---

## Using TallyTrek

### Dashboard

After startup, the dashboard shows the time, battery level, sensor status, and counts for the hour, the day, and since startup.

| Sensor status | Color | Meaning |
|---|---|---|
| Ready | White | Waiting for a trail user |
| Sensor Active | Yellow | The sensor is detecting (its output is high) |
| Downtime 10s…1s | Red | The sensor has dropped. The counter waits 10 seconds for more activity before recording the event |
| Sensor Off | White | The sensor was turned off from the menu |

Detections less than 10 seconds apart count as one event, so the counts go up when the downtime ends. To change the 10 seconds, edit
`SENSOR_DOWNTIME_MS` in the firmware.

### Buttons

| Button | Action |
|---|---|
| **D0** | Up in menus. Can't wake the board |
| **D1** | Opens the menu and selects. Wakes the display |
| **D2** | Down in menus. Wakes the display |

The display turns off after 4 minutes and the board goes into deep sleep. Trail users are still counted with the display off. The menu can turn the
sensor off, eject the SD card, reset the counts, and set the date and time.

### Collecting data

Each day's events go in a file named `CYYMMDD.csv` (for example, `C261010.csv` for October 10, 2026), with one row per event:

| Column | Contents |
|---|---|
| **Event Time** | `mm-dd-yy hh:mm:ss` |
| **Duration** | How long the sensor was active, in milliseconds |
| **Event Count** | Always 1 |
| **Hour Count** | Running count for the hour |
| **Day Count** | Running count for the day |
| **Cummulative Count** | Running count since startup (the header keeps the original spelling) |

To swap cards:

1. Press **D1** and choose **Eject SD Card**
2. Swap in a fresh, formatted card
3. Press **Reset**

> If the clock or SD card can't be found, the counter shows an error, sleeps, and tries again every 10 minutes.

---

## Documentation

| Document | Contents |
|---|---|
| [TallyTrek Guide](./docs/TallyTrekGuide.md) | Full guide, including what changed from NiftyCounter |
| [Sensor Overview](./docs/SensorOverview.md) | The kinds of sensors used for trail counting |
| [Packaging Options](./docs/PackagingOptions.md) | The NiftyCase and other enclosures, and mounting the sensor |
| [Helpful References](./docs/HelpfulReferences.md) | Trail counting guides, videos, and commercial systems |

---

## Repository Layout

```
tallytrek/
├── TallyTrek/
│   └── TallyTrek.ino        # firmware (Arduino sketch)
├── docs/                    # guide and background docs
├── assets/
│   ├── images/              # photos used in the docs
│   └── models/              # NiftyCase enclosure STLs
├── LICENSE
└── README.md
```

---

## License

Copyright 2026 Aiden Cherniske<br>
Copyright 2023 Loren Konkus

Licensed under the MIT License. See [`LICENSE`](./LICENSE) for the full text.
