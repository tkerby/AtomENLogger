#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <FastLED.h>
#include <WiFi.h>
#include <TinyGPS++.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <SD.h>
#include <esp_task_wdt.h>

//Watchdog timeout (seconds)
#define WDT_TIMEOUT 10
time_t last_ok = 0;

// provides the PRIx64 macro
#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#define GPS_RX 22
#define SD_MISO 33
#define SD_MOSI 19
#define SD_CLK 23

#define LED_PIN 27
#define NUM_LEDS 1
CRGB leds[NUM_LEDS];

// The TinyGPS++ object
TinyGPSPlus gps;



// Scan update time, 10 seems to be a good value.
#define SCAN_TIME_SECONDS 10

// When to forget old senders.
#define FORGET_AFTER_MINUTES 20


BLEScan *scanner;
std::map<std::string, unsigned long> seenNotifiers;

// Utility function to update the ESP32 system time from the GPS data. This ensures files are written with the correct dates
void setTimeFromGPS(){
  if (gps.date.isUpdated()){
    struct tm tm;
    tm.tm_year = gps.date.year()-1900; // Time object starts at 1900
    tm.tm_mon = gps.date.month()-1; // Jan considered month 0
    tm.tm_mday = gps.date.day();
    tm.tm_hour = gps.time.hour();
    tm.tm_min = gps.time.minute();
    tm.tm_sec = gps.time.second();
    time_t t = mktime(&tm);
    //printf("Setting time: %s", asctime(&tm));
    struct timeval now = { .tv_sec = t };
    settimeofday(&now, NULL); //Using UTC 
  }
    
}

// Utility function to convert integers such as date values so that they are padded with a leading 0 if < 10
String printDigits(int digits) {
  String digStr = "";

  if (digits < 10)
    digStr += '0';
  digStr += String(digits);

  return digStr;
}

// Utility function to convert bytes to their hexadecimal string representation
String hexlify(const uint8_t bytes[], int len)
{
  String output;
  output.reserve(len*2);
  for (int i=0; i<len; i++) {
    char hex[3];
    sprintf(hex, "%02x", bytes[i]);
    output.concat(hex);
  }
  return output;
}

// Utility function to clean up notifier database, removing enttries that havent been seen in FORGET_AFTER_MINUTES
void forgetOldNotifiers() {
  for (auto const &notifier : seenNotifiers) {
    if (notifier.second + (FORGET_AFTER_MINUTES * 60 * 1000) < millis()) {
      Serial.printf("-   %s \r\n", notifier.first.c_str());
      seenNotifiers.erase(notifier.first);
    }
  }
}

// Bluetooth Callback - Called when a device is discovered in the scan
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  
  // If it isnt an exposure notification then exit the callback
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    if (!advertisedDevice.haveServiceUUID()
        || !advertisedDevice.getServiceUUID().equals(BLEUUID((uint16_t) 0xfd6f))
      ) {
      return;
    }

    //Setup a JSON Doc to handle the data
    StaticJsonDocument<1024> doc;

    // Capture the BLE device address 
    doc["bdaddr"] = advertisedDevice.getAddress().toString();

    // Get the RSSI (returns an int)
    if (advertisedDevice.haveRSSI()) {
      doc["rssi"] = advertisedDevice.getRSSI();
    }

    // Get the servicedata containing the Prox ID and the encrypted metadata and convert to the rolling proximity indicator and additional encrypted metadata
    String servicedata;
    servicedata = hexlify((const uint8_t *)advertisedDevice.getServiceData().c_str(), advertisedDevice.getServiceData().size());
    doc["rpi"] = servicedata.substring(0,32);
    doc["aem"] = servicedata.substring(32,41);

    // If we have a valid GPS position then include it in the JSON document. 
    // Note: not using GPS age here as we want to still include approximate position if the user goes in a building. HDOP and Sats provide accuracy info
    if (gps.location.isValid() == true){
      doc["lat"] = gps.location.lat();
      doc["lon"] = gps.location.lng();
      doc["altitude"] = gps.altitude.meters();
      doc["speed"] = gps.speed.kmph();
      doc["hdop"] = gps.hdop.value();
      doc["date"] = gps.date.value();
      doc["time"] = gps.time.value();
    }

    // If we have a valid and reasonably up to date time then add time and date to the JSON document and output to a file
    // Note: Sats value included here to give an idea of progress towards first fix. Atom GPS contains battery backed RTC so continues to work indoors
    if ((gps.time.isValid() == true) && (gps.time.age() < 1500)){
      doc["date"] = String(gps.date.year()) + "-" + printDigits(gps.date.month()) + "-" + printDigits(gps.date.day());
      doc["time"] = printDigits(gps.time.hour()) + ":" + printDigits(gps.time.minute()) + ":" + printDigits(gps.time.second());
      doc["sats"] = gps.satellites.value();
      
      // Serialize the JSON to a string 
      String json;
      serializeJson(doc, json);
      
      // Create a new file each hour
      String fileName = "/";
      fileName += gps.date.year();
      fileName += printDigits(gps.date.month());
      fileName += printDigits(gps.date.day());
      fileName += printDigits(gps.time.hour());
      fileName += ".jsonl";

      File dataFile = SD.open(fileName, FILE_APPEND);

      // if the file is available, write to it:
      if (dataFile) {
        dataFile.println(json);
        dataFile.close();
      }
      // if the file isn't open, pop up an error:
      else {
        Serial.println("Error opening file for writing");
      }
      Serial.println(json);
    }

    //Clear the document ready for next use and release memory
    doc.clear(); 

    //Print to serial port and flash LED
    if (!seenNotifiers.count(advertisedDevice.getAddress().toString())) {
      // New notifier found.
      //Serial.printf("+   %s RSSI: %i\r\n", advertisedDevice.getAddress().toString().c_str(), advertisedDevice.getRSSI());
      leds[0] = CRGB::Red;
      FastLED.show();
      // Remember, with time.
    }
    else {
      // We've already seen this one. Use a blue flash when we have GPS and a Yellow Flash if not
      //Serial.printf("... %s RSSI: %i\r\n", advertisedDevice.getAddress().toString().c_str(), advertisedDevice.getRSSI());
      if (gps.location.isValid()){
        leds[0] = CRGB::Blue;
      }
      else{
        leds[0] = CRGB::Yellow; 
      }
      FastLED.show();
    }
    delay(10);
    leds[0] = CRGB::Black;
    FastLED.show();
    seenNotifiers[advertisedDevice.getAddress().toString()] = millis();
    last_ok = millis();


  }
};

// Bluetooth Callback for scanning complete - clear up results and start a new scan
static void scanCompleteCB(BLEScanResults scanResults) {
	printf("Scan complete!\n");
	printf("We found %d results\n", scanResults.getCount());
	scanResults.dump();

  // Cleanup.
  scanner->clearResults();
  forgetOldNotifiers();
  Serial.printf("Count: %d \r\n", seenNotifiers.size());
      // Scan.
  scanner->start(SCAN_TIME_SECONDS, scanCompleteCB);
} // scanCompleteCB

// Setup function. Called once on startup
void setup() {

  // Setup watchdog in case ESP32 program hangs. Note its more likely the bluetooth scanning will die - seperate mechanism in main loop handles this
  esp_task_wdt_init(WDT_TIMEOUT, true); //enable panic so ESP32 restarts
  esp_task_wdt_add(NULL); //add current thread to WDT watch

  // Start serial ports for consule and GPS
  Serial.begin(115200);
  Serial.println("Exposure Notification Scanner");

  Serial2.begin(9600, SERIAL_8N1, GPS_RX);
  WiFi.mode(WIFI_OFF);

  // Initialise SPI for SD card
  SPI.begin(SD_CLK,SD_MISO,SD_MOSI,-1);
  if(!SD.begin(-1, SPI, 40000000)){
    Serial.println("SD Initialization failed!");
  } 
  sdcard_type_t Type = SD.cardType();

  Serial.printf("SDCard Type = %d \r\n",Type);
  Serial.printf("SDCard Size = %d \r\n" , (int)(SD.cardSize()/1024/1024));

  // Initialize LED
  FastLED.addLeds<NEOPIXEL, LED_PIN>(leds, NUM_LEDS);

  // Initialize BLE scanner and start first scan
  BLEDevice::init("ESP");
  scanner = BLEDevice::getScan();
  scanner->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks(),true);
  scanner->setActiveScan(false); // Dont request additional data from devices - passive scan only
  scanner->setInterval(100);
  scanner->setWindow(99);
  scanner->start(SCAN_TIME_SECONDS, scanCompleteCB);

  Serial.println("Monitoring Started");
}

// Main loop
void loop() {

  // Feed the GPS
  while (Serial2.available() > 0){
    gps.encode(Serial2.read());
  }

  // Update system time
  setTimeFromGPS();

  // Reset built in WDT
  esp_task_wdt_reset();

  // Restart the ESP if we aren't getting bluetooth scans returned
  if (millis() - last_ok > (WDT_TIMEOUT * 1000)) {
    Serial.println("Application watchdog triggered");
    ESP.restart();
  }

  // Sleep for a bit. Low priority to allow other tasks to complete
  FreeRTOS::sleep(100);
}
