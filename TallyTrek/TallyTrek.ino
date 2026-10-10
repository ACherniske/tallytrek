/*
  TallyTrek - A low cost Trail Counter with a user interface

  TallyTrek is based on NiftyCounter by Loren Konkus
  (https://github.com/LorenKonkus/trail-counters).

  A low cost trail use counter using off the shelf hardware and 
  inexpensive, readily available microwave sensors that's appropriate when you
  just want to place a few counters out on trails in the forest and are able
  to visit them every month or two to switch batteries and harvest the microSD card. 

  Required components:
  - adafruit Adafruit ESP32-S2 Reverse TFT feather board
  - adafruit Adalogger feather wing
  - a microwave sensor. Tested using RCWL-0516 and HFS-DC06F, but similar devices should
    work. Really, any sensor that provides a high detection pulse should work.

  Time stamped trail use data is recorded in a .csv file on the SD card that can be 
  viewed and summarized in a spreadsheet. Current trail use statistics can be viewed by
  using the TFT display.

  Copyright © 2026 Aiden Cherniske
  Copyright © 2023 Loren Konkus
 
  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:
 
  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.
 
 */

#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789
#include "Adafruit_MAX1704X.h"
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include "RTClib.h"
#include "driver/rtc_io.h"

// The Adafruit Adalogger board uses a 8523 as the Real time clock
RTC_PCF8523 rtc;

// The Adafruit ESP32-S2 card's buit in Battery monitor
Adafruit_MAX17048 batteryMonitor;
boolean batteryMonitorReady = false;

// The Adafruit Reverse TFT card's built in display
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// Power management. After a button press, stay awake with the display on for
// DISPLAY_IDLE_COUNTER_RESET seconds. Any sensor change keeps us awake (with the display
// off) for at least SENSOR_IDLE_COUNTER_RESET seconds, and we never sleep in the middle of
// a trail user event or its downtime.
#define SENSOR_IDLE_COUNTER_RESET 5
#define DISPLAY_IDLE_COUNTER_RESET 240
volatile int idleCounter = SENSOR_IDLE_COUNTER_RESET;
boolean displayActive = false;

// We use this digital in for the trail user sensor active detection. This can change,
// but it needs to be a GPIO that has RTC functionality.
#define SENSOR_IN 12

// Deep sleep wakeup pins: the sensor and the normally low D1 and D2 buttons
#define SENSOR_PIN_MASK (1ULL << SENSOR_IN)
#define BUTTON_PINS_MASK ((1ULL << 1) | (1ULL << 2))

// Chip select for the microSD card
#define SD_CARD_SELECT 10

// Slots for user counts
RTC_DATA_ATTR int totalSinceStart = 0;
RTC_DATA_ATTR int totalThisDay = 0;
RTC_DATA_ATTR int totalThisHour = 0;
RTC_DATA_ATTR int hourTrip = -1;
RTC_DATA_ATTR int dayTrip = -1;

// Slots for current sensor events. The interupt routine times each event. When the
// sensor drops, the event's downtime starts: if the sensor fires again within
// SENSOR_DOWNTIME_MS, it's part of the same event, so a trail user who sets it off a few
// times in a row counts once. Once the sensor has been quiet for the whole downtime, the
// main loop queues the event's duration and records whatever is in the queue.
#define SENSOR_DOWNTIME_MS 10000
RTC_DATA_ATTR boolean sensorEnabled = true;
volatile boolean sensorEventActive = false;   // the sensor's output is high
volatile boolean sensorEventDowntime = false; // the sensor dropped, waiting out the downtime
volatile unsigned long sensorEventStartTime = 0;
volatile unsigned long sensorEventEndTime = 0;
#define EVENT_QUEUE_SIZE 16
volatile unsigned long eventQueue[EVENT_QUEUE_SIZE];
volatile int eventQueueHead = 0; // next slot the interupt routine fills
volatile int eventQueueTail = 0; // next slot the main loop records
portMUX_TYPE eventMux = portMUX_INITIALIZER_UNLOCKED;

// slots for the event log file
File logfile;
RTC_DATA_ATTR boolean recordingEvents = false;
RTC_DATA_ATTR char logFileName[20];

volatile int buttonPressed = 0;
#define UP_BUTTON 1
#define SELECT_BUTTON 2
#define DOWN_BUTTON 3

int activePage = 0;
#define MENU_PAGE 1
#define DASHBOARD_PAGE 2
#define SD_PAGE 3
#define SET_CLOCK_PAGE 4

int menuItemSelected = 1;
#define DASHBOARD_MENUITEM 1
#define SENSOR_MENUITEM 2
#define SD_MENUITEM 3
#define RESET_MENUITEM 4
#define SETCLOCK_MENUITEM 5
#define MAX_MENUITEM 5

int setClockLabelSelected = 0;
int setClockValueSelected = -1;
int setClockValues[5];
const String setClockLabels[6] = {"Year:","Month:","Day:","Hour:","Minute:","Save"};
const int setClockValuesMin[5] = {23,1,1,0,0};
const int setClockValuesMax[5] = {99,12,31,23,59};

//
// Add a finished event to the queue for the main loop to record. The caller must
// hold eventMux. If the queue is somehow full, the event is dropped.
//
void ARDUINO_ISR_ATTR queueSensorEvent(unsigned long duration) {
  int next = (eventQueueHead + 1) % EVENT_QUEUE_SIZE;
  if (next != eventQueueTail) {
    eventQueue[eventQueueHead] = duration;
    eventQueueHead = next;
  }
}

//
// Take the oldest finished event off the queue. Returns false if there isn't one.
//
boolean popSensorEvent(unsigned long *duration) {
  boolean found = false;
  portENTER_CRITICAL(&eventMux);
  if (eventQueueTail != eventQueueHead) {
    *duration = eventQueue[eventQueueTail];
    eventQueueTail = (eventQueueTail + 1) % EVENT_QUEUE_SIZE;
    found = true;
  }
  portEXIT_CRITICAL(&eventMux);
  return found;
}

//
// The sensor has dropped, so start the event's downtime. The caller must hold eventMux.
//
void ARDUINO_ISR_ATTR startSensorDowntime() {
  sensorEventEndTime = millis();
  sensorEventActive = false;
  sensorEventDowntime = true;
}

//
// Once the sensor has been quiet for the whole downtime, the event is over: queue its
// duration (first rise to last drop) to be recorded.
//
void checkSensorDowntime() {
  portENTER_CRITICAL(&eventMux);
  if (sensorEventDowntime && millis() - sensorEventEndTime >= SENSOR_DOWNTIME_MS) {
    queueSensorEvent(sensorEventEndTime - sensorEventStartTime);
    sensorEventDowntime = false;
  }
  portEXIT_CRITICAL(&eventMux);
}

//
// True while a trail user event is in progress (including its downtime) or waiting to
// be recorded
//
boolean sensorBusy() {
  portENTER_CRITICAL(&eventMux);
  boolean busy = sensorEventActive || sensorEventDowntime || (eventQueueHead != eventQueueTail);
  portEXIT_CRITICAL(&eventMux);
  return busy;
}

//
// Interupt routine called when a proximity sensor event occurs. At the
// start of the event we'll remember the start time, and when the sensor
// drops we'll start the downtime. If the sensor fires again during the
// downtime, it's merged into the same event. An event that started while
// the sensors were enabled is still recorded if they're turned off before
// it ends, but it isn't extended after that.
//
void ARDUINO_ISR_ATTR onSensorChanged() {
  portENTER_CRITICAL_ISR(&eventMux);
  if (digitalRead(SENSOR_IN) == HIGH) {
    if (sensorEnabled && sensorEventDowntime) {
      sensorEventDowntime = false;
      sensorEventActive = true;
    } else if (sensorEnabled && !sensorEventActive) {
      sensorEventStartTime = millis();
      sensorEventActive = true;
    }
  } else if (sensorEventActive) {
    startSensorDowntime();
  }
  if (idleCounter < SENSOR_IDLE_COUNTER_RESET) {
    idleCounter = SENSOR_IDLE_COUNTER_RESET;
  }
  portEXIT_CRITICAL_ISR(&eventMux);
}

//
// Interupt routine called whenever a button is pressed. Too much work to
// do in an interupt routine so if a button is pressed we'll note it for 
// the next loop iteration.
//
// On the reverse TFT feather board, D0 is normally high, D1, D2 are normally
// low.
//
// Debounce: this is called on every edge of every button. A press only counts
// if that button had been settled for BUTTON_DEBOUNCE_MS, so the bounce when
// a button is pressed or released can't register as another press.
//
#define BUTTON_DEBOUNCE_MS 50
void ARDUINO_ISR_ATTR onButtonPressed() {
  static const DRAM_ATTR int buttonCodes[3] = {UP_BUTTON, SELECT_BUTTON, DOWN_BUTTON};
  static boolean wasPressed[3] = {false, false, false};
  static unsigned long lastChange[3] = {0, 0, 0};
  unsigned long interupt_time = millis();

  for (int pin = 0; pin < 3; pin++) {   // buttons D0, D1, D2 are GPIO 0, 1, 2
    boolean pressed = (digitalRead(pin) == (pin == 0 ? LOW : HIGH));
    if (pressed != wasPressed[pin]) {
      if (pressed && interupt_time - lastChange[pin] > BUTTON_DEBOUNCE_MS) {
        buttonPressed = buttonCodes[pin];
        idleCounter = DISPLAY_IDLE_COUNTER_RESET;
      }
      wasPressed[pin] = pressed;
      lastChange[pin] = interupt_time;
    }
  }
}

void setup() {

  Serial.begin(115200);

  // We can get here either by a normal reset startup or by waking up from a deep sleep.
  // When in deep sleep, memory and connections can be lost so they need to be re-established,
  // but we don't want the verbose UI startup dialog or delays when waking
  boolean coldStart = true;
  uint64_t wakePins = 0;
  switch(esp_sleep_get_wakeup_cause()) {
    case ESP_SLEEP_WAKEUP_EXT1 :
      coldStart = false;
      wakePins = esp_sleep_get_ext1_wakeup_status();
      break;
    case ESP_SLEEP_WAKEUP_TIMER : coldStart = false; break; // retrying after a fatal error
    default : break;
  }

  // Configure the sensor and button inputs before anything reads them. After a deep
  // sleep wakeup these pins are still routed to the RTC, and digitalRead() isn't
  // reliable until pinMode() hands them back. The sensor gets a pull-down so a
  // disconnected or broken sensor wire reads LOW instead of floating and counting noise.
  pinMode(SENSOR_IN, INPUT_PULLDOWN);
  pinMode(0, INPUT_PULLUP);
  pinMode(1, INPUT_PULLDOWN);
  pinMode(2, INPUT_PULLDOWN);

  // If a trail user woke us up, start timing the event now. The time it took to boot
  // is lost, so durations of wakeup events read a little short. If the sensor has
  // already dropped by the time the interupt is attached, we'll record it then.
  if ((wakePins & SENSOR_PIN_MASK) && sensorEnabled) {
    sensorEventStartTime = millis();
    sensorEventActive = true;
  }

  // set LED to be an output pin
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  // We won't use the NeoPixel
  pinMode(NEOPIXEL_POWER, OUTPUT);
  digitalWrite(NEOPIXEL_POWER, LOW);

  // Setup the TFT Display. TFT_I2C_POWER also powers the I2C bus the clock and battery
  // monitor are on, so it's on whenever we're awake. The display itself is only turned
  // on for a cold start or a D1/D2 press - trail users are counted in the dark. The
  // press that woke us just wakes the display, it doesn't act on the page.
  pinMode(TFT_BACKLITE, OUTPUT);
  digitalWrite(TFT_BACKLITE, LOW);
  pinMode(TFT_I2C_POWER, OUTPUT);
  digitalWrite(TFT_I2C_POWER, HIGH);
  pinMode(TFT_CS, OUTPUT);     // the display shares the SPI bus with the SD card, so
  digitalWrite(TFT_CS, HIGH);  // keep it deselected even when it isn't initialized
  delay(10);
  if (coldStart || (wakePins & BUTTON_PINS_MASK)) {
    idleCounter = DISPLAY_IDLE_COUNTER_RESET;
    tftOn();
  }

  // Start the startup splash screen
  if (displayActive) {
    tft.setCursor(0, 0);
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(2);
  }
  if (coldStart) {
    tft.println("TallyTrek");
    tft.println("version 1.0");
    tft.setTextSize(1);
    tft.println("\nCopyright (c) 2026 Aiden Cherniske");
    tft.println("Based on NiftyCounter by Loren Konkus\n");
  }

  // Initialize the RTC
  if (! rtc.begin()) {
    if (displayActive) {
      tft.setTextColor(ST77XX_RED);
      tft.setTextSize(2);
      tft.println("Fatal Error");
      tft.println("No Real Time Clock");
    }
    Serial.println("Couldn't find RTC");
    Serial.flush();
    fatalSleep();
  }
  if (! rtc.initialized() || rtc.lostPower()) {
    Serial.println("RTC is NOT initialized, setting the time");
    // When time needs to be set on a new device, or after a power loss, the
    // following line sets the RTC to the date & time this sketch was compiled
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    if (displayActive) {
      tft.setTextColor(ST77XX_YELLOW);
      tft.setTextSize(1);
      tft.println("Real Time Clock initialized");
    }
  }
  rtc.start();
  
  if (coldStart) {
    tft.setTextColor(ST77XX_GREEN);
    tft.setTextSize(1);
    tft.print("\nCurrent time: ");
    tft.setTextColor(ST77XX_WHITE);
    tft.println(getTimestampString(getCurrentTime()));
  }

  // Initialize the battery monitor
  batteryMonitorReady = batteryMonitor.begin();
  if (!batteryMonitorReady) {
    if (displayActive) {
      tft.setTextColor(ST77XX_RED);
      tft.setTextSize(1);
      tft.println("Battery monitor not available");
    }
  } else {
    if (coldStart) {
      delay(1000); // battery monitor needs a moment to collect itself... 
      tft.setTextColor(ST77XX_GREEN);
      tft.setTextSize(1);
      tft.print("\nBattery Status: ");
      tft.setTextColor(ST77XX_WHITE);
      tft.print(batteryMonitor.cellVoltage(),1);
      tft.print("v, ");
      tft.print(batteryMonitor.cellPercent(),0);
      tft.println("%");
    }
  }

  // Initialize the SD card
  pinMode(SD_CARD_SELECT, OUTPUT);
  if (!SD.begin(SD_CARD_SELECT)) {
    if (displayActive) {
      tft.setTextColor(ST77XX_RED);
      tft.setTextSize(2);
      tft.println("Insert SD Card and");
      tft.println("press reset to");
      tft.println("proceed.");
    }
    fatalSleep();
  }

  // create or open the event log .csv file for today's date
  if (coldStart) {
    eventsFileName(logFileName, getCurrentTime());
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_GREEN);
    if (!SD.exists(logFileName)) {
      tft.print("\nCreating file: ");
    } else {
      tft.print("\nOpening file: ");
    }
    tft.setTextColor(ST77XX_WHITE);
    tft.println(logFileName);
  }

  if (!openLogFile()) {
    if (displayActive) {
      tft.setTextColor(ST77XX_RED);
      tft.setTextSize(2);
      tft.println("Error opening log");
      tft.print("file. Check SD card");
    }
    fatalSleep();
  }
  recordingEvents = true;

  // Proximity detector digital in
  attachInterrupt(digitalPinToInterrupt(SENSOR_IN), onSensorChanged, CHANGE);

  // If a trail user woke us up and the sensor has already dropped, the falling edge came
  // before the interupt was attached, so start that event's downtime by hand.
  portENTER_CRITICAL(&eventMux);
  if (sensorEventActive && digitalRead(SENSOR_IN) == LOW) {
    startSensorDowntime();
  }
  portEXIT_CRITICAL(&eventMux);

  // Buttons
  attachInterrupt(digitalPinToInterrupt(0), onButtonPressed, CHANGE);
  attachInterrupt(digitalPinToInterrupt(1), onButtonPressed, CHANGE);
  attachInterrupt(digitalPinToInterrupt(2), onButtonPressed, CHANGE);

  // Initialization complete
  if (coldStart) {
    tft.setTextColor(ST77XX_GREEN);
    tft.setTextSize(1);
    tft.println("\nStartup Successful");
    delay (5000);
  }

  if (displayActive) {
    activePage = 0;
    displayDashboardPage();
  }
}

//
// Running mode
//
void loop() {

  // Manage the timeout. If we've gone DISPLAY_IDLE_COUNTER_RESET loops without
  // a button press, turn off the display and sleep. Users can press D1 or D2 to wake it again.
  if (idleCounter>0) {
    idleCounter--;
  }
  if (idleCounter == 0 && !sensorBusy()) {
    // Idle time expired and we're not in the middle of processing an event, so close up
    // files and enter a deep sleep. If an event started while the file was closing,
    // reopen it and keep going instead.
    if (logfile) {
      logfile.close();
    }
    if (!sensorBusy()) {
      enterDeepSleep();
    }
    if (recordingEvents && !openLogFile()) {
      recordingEvents = false;
    }
  } else if (idleCounter == 0) {
    // Waiting for a trail user event to finish - no need to keep the display lit meanwhile
    tftOff();
  }

  // A button press while the display is dark just turns it back on
  if (buttonPressed != 0 && !displayActive) {
    tftOn();
    displayDashboardPage();
    buttonPressed = 0;
  }

  // If there was a button pressed, then take some action in the UI. UIs are messy - this is about
  // as complicated as I'd want to build before moving to a finite state machine
  if (buttonPressed != 0) {
    switch (activePage) {
      
      case DASHBOARD_PAGE:
        switch (buttonPressed) {
          
          case SELECT_BUTTON:
            displayMenuPage();
            break;
        }
        break;
      
      case SD_PAGE:
        switch (buttonPressed) {
          
          case SELECT_BUTTON:
            if (doLoadSDCard()) {
              displayMenuPage();
            } else {
              doRemoveSDCard(); // put the instructions back up
            }
            break;
        }
        break;
      
      case SET_CLOCK_PAGE:
        switch (buttonPressed) {
          
          case UP_BUTTON:
            if (setClockLabelSelected >= 0) {
              if (--setClockLabelSelected < 0) {
                setClockLabelSelected = 5;
              }
            }
            if (setClockValueSelected >= 0) {
              if (++setClockValues[setClockValueSelected] > setClockValuesMax[setClockValueSelected]) {
                setClockValues[setClockValueSelected] = setClockValuesMin[setClockValueSelected];
              }
            }
            break;
                   
          case DOWN_BUTTON:
            if (setClockLabelSelected >= 0) {
              if (++setClockLabelSelected > 5) {
                setClockLabelSelected = 0;
              }
            }
            if (setClockValueSelected >= 0) {
              if (--setClockValues[setClockValueSelected] < setClockValuesMin[setClockValueSelected]) {
                setClockValues[setClockValueSelected] = setClockValuesMax[setClockValueSelected];
              }
            }
            break;
                    
          case SELECT_BUTTON:
            if (setClockLabelSelected == 5) {
              doUpdateClock();
              displayMenuPage();
              break;
            }
            if (setClockLabelSelected >= 0) {
              setClockValueSelected = setClockLabelSelected;
              setClockLabelSelected = -1;
              break;
            }
            if (setClockValueSelected >= 0) {
              setClockLabelSelected = setClockValueSelected + 1;
              setClockValueSelected = -1;
            }
            break;
        }
        break;
      
      case MENU_PAGE:
        switch (buttonPressed) {
          
          case UP_BUTTON:
            if (--menuItemSelected < 1) {
              menuItemSelected = MAX_MENUITEM;
            }
            break;
                    
          case DOWN_BUTTON:
            if (++menuItemSelected > MAX_MENUITEM) {
              menuItemSelected = 1;
            }
            break;
         
          case SELECT_BUTTON:
            switch (menuItemSelected) {
            
              case DASHBOARD_MENUITEM:
                displayDashboardPage();
                break;
                
              case SENSOR_MENUITEM:
                doToggleSensors();
                break;
                
              case SD_MENUITEM:
                doRemoveSDCard();
                break;
                
              case RESET_MENUITEM:
                doResetCounters();
                break;
                
              case SETCLOCK_MENUITEM:
                displaySetClock();
                break;
            }
            break;
         
        }
        break;
    }
    buttonPressed = 0;
  }
  
  // Refresh the active page. The SD page has no dynamic content, so it's left alone.
  if (displayActive) {
    switch (activePage) {
      case MENU_PAGE:
        displayMenuPage();
        break;
      case SET_CLOCK_PAGE:
        displaySetClock();
        break;
      case DASHBOARD_PAGE:
        displayDashboardPage();
        break;
    }
  }
  
  //
  // Reset the hourly and daily counters if needed
  //
  DateTime now = getCurrentTime();
  int today = now.year() * 10000 + now.month() * 100 + now.day();
  if (today != dayTrip) {
    totalThisDay = 0;
    dayTrip = today;
    totalThisHour = 0;
    hourTrip = now.hour();
  }
  if (now.hour() != hourTrip) {
    totalThisHour = 0;
    hourTrip = now.hour();
  }

  //
  // Record any events that are over
  //
  checkSensorDowntime();
  unsigned long sensorEventDuration;
  while (popSensorEvent(&sensorEventDuration)) {
    if (recordingEvents) {
      totalSinceStart++;
      totalThisDay++;
      totalThisHour++;
      String eventLogRecord = getTimestampString(now);
      eventLogRecord += ",";
      eventLogRecord += sensorEventDuration;
      eventLogRecord += ",1,";
      eventLogRecord += totalThisHour;
      eventLogRecord += ",";
      eventLogRecord += totalThisDay;
      eventLogRecord += ",";
      eventLogRecord += totalSinceStart;
      logfile.println(eventLogRecord);
      logfile.flush();
    }
  }

  delay(1000);               // wait for a second
}

//
// Get the current time
//
DateTime getCurrentTime() {
  return rtc.now();
}

//
// Get a time string in format mm-dd-yy hh:mm:ss, which seems to make 
// most spreadsheets happy. RTCs vary and may or may not support 4 digit dates,
// so we always just use last 2 for consistency.
//
String getTimestampString(DateTime aTime) {
  int thisYear = aTime.year();
  if (thisYear > 2000) {
    thisYear = thisYear - 2000;
  }
  char buffer[20];
  sprintf(buffer,"%02d-%02d-%02d %02d:%02d:%02d",aTime.month(),aTime.day(),thisYear,aTime.hour(),aTime.minute(),aTime.second());
  return String(buffer);
}

//
// Compute a FAT filename for the events log
//
void eventsFileName(char* buffer, DateTime aTime) {
  int aYear = aTime.year();
  if (aYear > 2000) {
    aYear = aYear - 2000;
  }
  sprintf(buffer,"/C%02d%02d%02d.csv",aYear,aTime.month(),aTime.day());
}

//
// Routine to turn on the TFT display. The display loses power in deep sleep, so it's
// initialized each time it's turned on and whatever page was showing has to be redrawn.
//
void tftOn() {
  if (!displayActive) {
    tft.init(135, 240); // Init ST7789 240x135
    tft.setRotation(3); // landscape mode with buttons on left
    tft.fillScreen(ST77XX_BLACK);
    digitalWrite(TFT_BACKLITE, HIGH);
    displayActive = true;
    activePage = 0;
  }
}

//
// Turn off the TFT display backlight. There's a timer so when not in use the display
// is turned off to save power. The TFT_I2C_POWER supply stays on while we're awake
// because the clock and battery monitor need it; it's cut when we go to sleep.
//
void tftOff() {
  digitalWrite(TFT_BACKLITE, LOW);
  displayActive = false;
}

//
// Enter a deep sleep to save power between events. We'll wake up (and run setup()
// again) for either a sensor input or a button press. Note that we don't wake for
// button 0 presses - that's normally high button so it would wake us immediately.
// We don't wake for the sensor either while it's turned off.
//
void enterDeepSleep() {
  tftOff();
  digitalWrite(TFT_I2C_POWER, LOW);
  uint64_t wakeMask = BUTTON_PINS_MASK;
  if (sensorEnabled) {
    wakeMask |= SENSOR_PIN_MASK;
  }
  esp_sleep_enable_ext1_wakeup(wakeMask, ESP_EXT1_WAKEUP_ANY_HIGH);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  rtc_gpio_pullup_en(GPIO_NUM_0);
  rtc_gpio_pulldown_en(GPIO_NUM_1);
  rtc_gpio_pulldown_en(GPIO_NUM_2);
  rtc_gpio_pulldown_en((gpio_num_t)SENSOR_IN);
  esp_deep_sleep_start();
}

//
// Something needed for counting (the clock or the SD card) isn't working. Rather than
// hang here with the display on until the battery dies, leave the message up for a
// moment and go to sleep. We'll try again after FATAL_RETRY_SECONDS, or sooner if a
// button is pressed or a trail user passes by.
//
#define FATAL_RETRY_SECONDS 600
void fatalSleep() {
  if (displayActive) {
    delay(10000);
  }
  esp_sleep_enable_timer_wakeup(FATAL_RETRY_SECONDS * 1000000ULL);
  enterDeepSleep();
}

//
// Open the event log .csv file for appending, creating it with a header line if it
// doesn't exist yet. The file is named for the date it's opened, and since it's
// reopened on every wakeup, a new file starts each day.
//
boolean openLogFile() {
  eventsFileName(logFileName, getCurrentTime());
  boolean newfile = !SD.exists(logFileName);
  logfile = SD.open(logFileName, FILE_APPEND);
  if (!logfile) {
    return false;
  }
  if (newfile) {
    logfile.println("Event Time, Duration, Event Count, Hour Count, Day Count, Cummulative Count");
    logfile.flush();
  }
  return true;
}

// 
// User requested that the sensors be enabled or disabled
//
void doToggleSensors() {

  // change the sensor enable to whatever it isn't
  sensorEnabled = !sensorEnabled;

  // tell the user
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(30, 60);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setTextSize(2);
  if (sensorEnabled) {
    tft.print("Sensors enabled");
  } else {
    tft.print("Sensors offline");
  }
  delay (5000);
  tft.fillScreen(ST77XX_BLACK);
}

// 
// User requested that counters be reset to 0
//
void doResetCounters() {

  // reset counters
  totalSinceStart = 0;
  totalThisDay = 0;
  totalThisHour = 0;

  // tell the user
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(30, 60);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setTextSize(2);
  tft.print("Counters reset");
  delay (5000);
  tft.fillScreen(ST77XX_BLACK);  
}

// 
// User wants to update the date and time
//
void displaySetClock() {
  if (activePage != SET_CLOCK_PAGE) {
    DateTime aTime = getCurrentTime();
    setClockValues[0] = aTime.year();
    if (setClockValues[0] > 2000) {
      setClockValues[0] = setClockValues[0] - 2000;
    }
    setClockValues[1] = aTime.month();
    setClockValues[2] = aTime.day();
    setClockValues[3] = aTime.hour();
    setClockValues[4] = aTime.minute();
  
    // Initial page display
    tft.fillScreen(ST77XX_BLACK);
    tft.drawChar(0, 0, 30, ST77XX_CYAN, ST77XX_BLACK, 2);
    tft.drawChar(0, 60, 15, ST77XX_CYAN, ST77XX_BLACK, 2);
    tft.drawChar(0, 125, 31, ST77XX_CYAN, ST77XX_BLACK, 2);
    tft.setCursor(30, 0);
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(2);
    tft.print("  Set Clock");
    
    setClockLabelSelected = 0;
    setClockValueSelected = -1;
    activePage = SET_CLOCK_PAGE;
  }
  
  // Display the dynamic content
  for (int i=0; i<6; i++) {
    displaySetClockLabel(i);
  } 
  for (int i=0; i<5; i++) {
    displaySetClockValue(i);
  }
}

//
// On the Set Clock page, display the label for one of the clock fields
//
void displaySetClockLabel(int item) {
  if (setClockLabelSelected == item) {
    tft.setTextColor(ST77XX_BLACK, ST77XX_GREEN);
  } else {
    tft.setTextColor(ST77XX_GREEN, ST77XX_BLACK);
  }
  tft.setCursor(30,25+item*17);
  tft.print(setClockLabels[item]);
}

//
// On the Set Clock page, display the current value of one of the clock fields
void displaySetClockValue(int item) {
  if (setClockValueSelected == item) {
    tft.setTextColor(ST77XX_BLACK, ST77XX_GREEN);
  } else {
    tft.setTextColor(ST77XX_GREEN, ST77XX_BLACK);
  }
  tft.setCursor(140,25+item*17);
  if (setClockValues[item]<10) {
    tft.print("0");
  }
  tft.print(setClockValues[item]);
}

//
// User of the Set Clock page has updated the time - set the RTC clock to reflect it
//
void doUpdateClock() {
  // Don't let the user set a day the month doesn't have (every 4th year is a leap
  // year for all the years the Set Clock page allows)
  const int daysInMonth[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
  int maxDay = daysInMonth[setClockValues[1] - 1];
  if (setClockValues[1] == 2 && setClockValues[0] % 4 == 0) {
    maxDay = 29;
  }
  if (setClockValues[2] > maxDay) {
    setClockValues[2] = maxDay;
  }
  DateTime newTime = DateTime(setClockValues[0]+2000,setClockValues[1],setClockValues[2],setClockValues[3],setClockValues[4],0);
  rtc.adjust(newTime);
}

// 
// User requested that the SD card be dismounted so that they can replace it
//
void doRemoveSDCard() {

  if (recordingEvents) {
    if (logfile) {
      logfile.flush();
      logfile.close();
    }
    SD.end();
    recordingEvents = false;
  }

  // tell the user
  tft.fillScreen(ST77XX_BLACK);
  tft.drawChar(0, 0, 30, ST77XX_CYAN, ST77XX_BLACK, 2);
  tft.drawChar(0, 60, 15, ST77XX_CYAN, ST77XX_BLACK, 2);
  tft.drawChar(0, 125, 31, ST77XX_CYAN, ST77XX_BLACK, 2);
  tft.setCursor(30, 10);
  tft.setTextColor(ST77XX_GREEN, ST77XX_BLACK);
  tft.setTextSize(2);
  tft.println("SD card can be");
  tft.setCursor(30, 30);
  tft.println("safely removed.");
  tft.setCursor(30, 60);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.println("Insert new ");
  tft.setCursor(30, 80);
  tft.println("formatted card");
  tft.setCursor(30, 100);
  tft.println("and press restart");
//  tft.println("and press *");
  activePage = SD_PAGE;
}

//
// User has inserted an SD card and is ready to start using it
//
// The card was unmounted with SD.end() when it was ejected, so SD.begin()
// mounts whatever card is in the slot now. This hasn't been tested with a
// swapped card on hardware yet - the screen instructions still say press
// restart, which always works.
//
boolean doLoadSDCard() {
  if (logfile) {
    logfile.close();
  }
  SD.end();
  if (!SD.begin(SD_CARD_SELECT)) {
    tft.fillScreen(ST77XX_BLACK);
    tft.setCursor(30, 60);
    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    tft.setTextSize(2);
    tft.print("SD card error");
    delay (5000);
    return false;
  }

  // append to (never overwrite) today's event log .csv file
  if (!openLogFile()) {
    tft.fillScreen(ST77XX_BLACK);
    tft.setCursor(30, 60);
    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    tft.setTextSize(2);
    tft.print("log file error");
    delay (5000);
    return false;
  }

  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(30, 60);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setTextSize(2);
  tft.print("log file ready");
  delay (5000);
  
  recordingEvents = true;
  return true;
}

//
// Display the Menu page
//
void displayMenuPage() {
  if (activePage != MENU_PAGE) {
    // Display button icons 
    tft.fillScreen(ST77XX_BLACK);
    tft.drawChar(0, 0, 30, ST77XX_CYAN, ST77XX_BLACK, 2);
    tft.drawChar(0, 60, 15, ST77XX_CYAN, ST77XX_BLACK, 2);
    tft.drawChar(0, 125, 31, ST77XX_CYAN, ST77XX_BLACK, 2);
  }
  // Display menu items
  displayMenuItem(DASHBOARD_MENUITEM,"Back to Display");
  if (sensorEnabled) {
    displayMenuItem(SENSOR_MENUITEM,"Sensor: On ");
  } else {
    displayMenuItem(SENSOR_MENUITEM,"Sensor: Off");
  }
  displayMenuItem(SD_MENUITEM,"Eject SD Card");
  displayMenuItem(RESET_MENUITEM,"Reset Counts");
  displayMenuItem(SETCLOCK_MENUITEM,"Set Date/Time");
  activePage = MENU_PAGE;
}

void displayMenuItem(int itemNumber, String itemText) {
  if (menuItemSelected == itemNumber) {
    tft.setTextColor(ST77XX_BLACK, ST77XX_GREEN);
  } else {
    tft.setTextColor(ST77XX_GREEN, ST77XX_BLACK);
  }
  tft.setCursor(30, (itemNumber-1)*20);
  tft.setTextSize(2);
  tft.print(itemText);
}

//
// Display the default overview page
//
void displayDashboardPage() {

  // If this is the initial time the page is requested, populate the static content
  if (activePage != DASHBOARD_PAGE) {
    tft.fillScreen(ST77XX_BLACK);
    tft.drawChar(0, 0, 30, ST77XX_CYAN, ST77XX_BLACK, 2);
    tft.drawChar(0, 60, 15, ST77XX_CYAN, ST77XX_BLACK, 2);
    tft.drawChar(0, 125, 31, ST77XX_CYAN, ST77XX_BLACK, 2);
    tft.setCursor(30, 0);
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(2);
    tft.print("TallyTrek\n");
    tft.setTextSize(1);
    tft.setCursor(125, 39);
    tft.setTextColor(ST77XX_GREEN);
    tft.print("Battery:");
    tft.setTextSize(2);
    tft.setCursor(30, 60);
    tft.print("This hour:");
    tft.setCursor(30, 80);
    tft.print("This day:");
    tft.setCursor(30, 100);
    tft.print("Total:");
    activePage = DASHBOARD_PAGE;
  }

  // Populate the dynamic content
  tft.setTextColor(ST77XX_GREEN, ST77XX_BLACK);
  tft.setTextSize(1);
  tft.setCursor(30, 25);
  tft.print(getTimestampString(getCurrentTime()));
  
  // Sensor status. Yellow means the sensor itself is detecting; red means it has dropped
  // and we're waiting out the downtime before recording the event.
  portENTER_CRITICAL(&eventMux);
  boolean active = sensorEventActive;
  boolean downtime = sensorEventDowntime;
  unsigned long quietTime = millis() - sensorEventEndTime;
  portEXIT_CRITICAL(&eventMux);
  tft.fillRect(30, 39, 85, 10, ST77XX_BLACK);
  tft.setCursor(30, 39);
  if (active) {
    tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
    tft.print("Sensor Active");
  } else if (downtime) {
    unsigned long remaining = quietTime < SENSOR_DOWNTIME_MS ? SENSOR_DOWNTIME_MS - quietTime : 0;
    tft.setTextColor(ST77XX_RED, ST77XX_BLACK);
    tft.print("Downtime ");
    tft.print((remaining + 999) / 1000);
    tft.print("s");
  } else if (sensorEnabled) {
    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    tft.print("Ready");
  } else {
    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    tft.print("Sensor Off");
  }
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);

  tft.fillRect(175, 39, 65, 10, ST77XX_BLACK);
  tft.setCursor(175, 39);
  if (batteryMonitorReady) {
    tft.print(batteryMonitor.cellVoltage(),1);
    tft.print("v ");
    tft.print(batteryMonitor.cellPercent(),0);
    tft.print("%");
  } else {
    tft.print("n/a");
  }

  tft.setTextSize(2);
  tft.fillRect(160, 60, 80, 16, ST77XX_BLACK);
  tft.setCursor(160, 60);
  tft.print(totalThisHour);
 
  tft.setCursor(160, 80);
  tft.fillRect(160, 80, 80, 16, ST77XX_BLACK);
  tft.print(totalThisDay);

  tft.setCursor(160, 100);
  tft.fillRect(160, 100, 80, 16, ST77XX_BLACK);
  tft.print(totalSinceStart);
}
