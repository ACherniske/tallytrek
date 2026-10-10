# TallyTrek

TallyTrek is a low-cost trail counter. It detects each trail user with a microwave sensor and records the event, with a timestamp, in a .csv file
on a microSD card that you can collect and open in any spreadsheet. A small built-in display shows the current counts, battery level, and sensor
status, so you can check a counter in the field without a laptop.

TallyTrek is by Aiden Cherniske. It's based on NiftyCounter from Loren Konkus's
[trail-counters](https://github.com/LorenKonkus/trail-counters) project, and fixes counting, data loss, and power problems found in the original.

![NiftyCounter assembled](/assets/images/NiftyCounter.jpg)

## Why

Trail builders and advocacy groups get asked how many people use their trails, and when. Commercial trail counters answer that well, but they cost
hundreds of dollars per sensor and often need proprietary hardware and software subscriptions. Counting trail users is a simple job: notice someone
passing and write down the time. Off-the-shelf boards can do that for much less, and a cheap counter can go in more places with less worry about
losing it.

## Hardware

- [Adafruit ESP32-S2 Reverse TFT Feather](https://www.adafruit.com/product/5345) (the -S3 version works too): the processor, display, three
  buttons, LiPo charger, and battery monitor
- [Adafruit Adalogger FeatherWing](https://www.adafruit.com/product/2922): the microSD card slot and a battery-backed real-time clock
- RCWL-0516 microwave sensor
- A LiPo battery and a FAT-formatted microSD card
- Optional: the 3D printed NiftyCase enclosure (see [Packaging Options](PackagingOptions.md))

Wire the sensor to the Feather:

| RCWL-0516 | Feather |
|-----------|---------|
| VIN       | BAT (or 3V) |
| GND       | GND |
| OUT       | 12 |
| 3V3, CDS  | not connected |

The sensor's output pin has to be an RTC-capable GPIO (0 through 21) so it can wake the board from deep sleep. Keep the sensor a couple of inches from
the boards and battery, with its component side facing the trail. See [Sensor Overview](SensorOverview.md) for more on sensors.

## Installing the firmware

1. Install the Arduino IDE and add the "esp32 by Espressif Systems" board package.
2. Install the Adafruit ST7735 and ST7789, Adafruit GFX, Adafruit MAX1704X, and RTClib libraries.
3. Open the `TallyTrek` folder, select the "Adafruit Feather ESP32-S2 Reverse TFT" board, and upload.

Or, with arduino-cli:

```
arduino-cli compile --fqbn esp32:esp32:adafruit_feather_esp32s2_reversetft TallyTrek
arduino-cli upload --fqbn esp32:esp32:adafruit_feather_esp32s2_reversetft -p <port> TallyTrek
```

The board's USB port disappears while it's in deep sleep. Press D1 or D2 to wake it before uploading.

## Using TallyTrek

After startup the dashboard shows the time, battery level, sensor status, and counts for the hour, the day, and since startup. The sensor status
reads "Ready", "Sensor Active" in yellow while the sensor is detecting, or "Downtime" with a red countdown while the counter waits for the sensor to
stay quiet for 10 seconds. Detections less than 10 seconds apart count as one event.

The display turns off after 4 minutes and the board goes into deep sleep. D1 or D2 turns the display back on. Trail users are counted with the
display off. Press D1 for the menu, where you can turn the sensor off, eject the SD card, reset the counts, or set the clock.

Each day's events go in a file named `CYYMMDD.csv` with the event time, duration, and running counts for the hour, the day, and since startup. To
collect data, choose "Eject SD Card", swap in a fresh card, and press reset.

The full guide is in [TallyTrek/TallyTrek.md](TallyTrek/TallyTrek.md).

## Repository

- `TallyTrek/` - the firmware (`TallyTrek.ino`) and the full guide
- `NiftyCase*.stl` - the 3D printed enclosure
- [SensorOverview.md](SensorOverview.md) - the kinds of sensors used for trail counting
- [PackagingOptions.md](PackagingOptions.md) - enclosures and mounting
- [HelpfulReferences.md](HelpfulReferences.md) - trail counting guides, videos, and commercial systems

## License

MIT. TallyTrek is copyright 2026 Aiden Cherniske, and NiftyCounter, which it's based on, is copyright 2023 Loren Konkus. See [LICENSE](LICENSE).
