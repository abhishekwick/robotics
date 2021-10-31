
/***************************************************
  Adafruit invests time and resources providing this open source code,
  please support Adafruit and open-source hardware by purchasing
  products from Adafruit!

  Written by Limor Fried/Ladyada for Adafruit Industries.
  MIT license, all text above must be included in any redistribution
 ****************************************************/


#define FORMAT_SPIFFS_IF_FAILED true

// Wifi & Webserver
#include "WiFi.h"
#include "SPIFFS.h"
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "wifiConfig.h"
AsyncWebServer server(80);

const char* http_username = "admin1";
const char* http_password = "admin1";

// RTC
#include "RTClib.h"

RTC_PCF8523 rtc;
char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};


// EINK
#include "Adafruit_ThinkInk.h"

#define EPD_CS      15
#define EPD_DC      33
#define SRAM_CS     32
#define EPD_RESET   -1 // can set to -1 and share with microcontroller Reset!
#define EPD_BUSY    -1 // can set to -1 to not use a pin (will wait a fixed delay)

// 2.13" Monochrome displays with 250x122 pixels and SSD1675 chipset
ThinkInk_213_Mono_B72 display(EPD_DC, EPD_RESET, EPD_CS, SRAM_CS, EPD_BUSY);

// Motor Shield
#include <Adafruit_MotorShield.h>
Adafruit_MotorShield AFMS = Adafruit_MotorShield();
Adafruit_DCMotor *myMotor = AFMS.getMotor(4);
boolean pumpIsRunning = false;

// Soil Moisture
int moistureValue = 0; //value for storing moisture value
int soilPin = A2;//Declare a variable for the soil moisture sensor

//Temperature Sensor
#include <Wire.h>
#include "Adafruit_ADT7410.h"
// Create the ADT7410 temperature sensor object
Adafruit_ADT7410 tempsensor = Adafruit_ADT7410();

void setup() {
  /*


  */
  Serial.begin(9600);
  while (!Serial) {
    delay(10);
  }
  delay(1000);


  if (!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)) {
    // Follow instructions in README and install
    // https://github.com/me-no-dev/arduino-esp32fs-plugin
    Serial.println("SPIFFS Mount Failed");
    return;
  }
  if (!tempsensor.begin()) {
    Serial.println("Couldn't find ADT7410!");
    while (1);
  }

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi..");
  }
  Serial.println();
  Serial.print("Connected to the Internet");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  pinMode(LED_BUILTIN, OUTPUT);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    if (!request->authenticate(http_username, http_password))
      return request->requestAuthentication();
    request->send(SPIFFS, "/index.html", "text/html");
  });
  server.on("/dashboard", HTTP_GET, [](AsyncWebServerRequest * request) {
    if (!request->authenticate(http_username, http_password))
      return request->requestAuthentication();
    request->send(SPIFFS, "/dashboard.html", "text/html", false, processor);
  });

  //LED Control
  server.on("/LEDOn", HTTP_GET, [](AsyncWebServerRequest * request) {
    if (!request->authenticate(http_username, http_password))
      return request->requestAuthentication();
    digitalWrite(LED_BUILTIN, HIGH);
    request->send(SPIFFS, "/dashboard.html", "text/html", false, processor);
  });
  server.on("/LEDOff", HTTP_GET, [](AsyncWebServerRequest * request) {
    if (!request->authenticate(http_username, http_password))
      return request->requestAuthentication();
    digitalWrite(LED_BUILTIN, LOW);
    request->send(SPIFFS, "/dashboard.html", "text/html", false, processor);
  });

  // Pump Control
  server.on("/PumpOn", HTTP_GET, [](AsyncWebServerRequest * request) {
    if (!request->authenticate(http_username, http_password))
      return request->requestAuthentication();
    myMotor->run(FORWARD); // May need to change to BACKWARD
    pumpIsRunning = true;
    request->send(SPIFFS, "/dashboard.html", "text/html", false, processor);
  });
  server.on("/PumpOff", HTTP_GET, [](AsyncWebServerRequest * request) {
    if (!request->authenticate(http_username, http_password))
      return request->requestAuthentication();
    myMotor->run(RELEASE);
    pumpIsRunning = false;
    request->send(SPIFFS, "/dashboard.html", "text/html", false, processor);
  });

  server.on("/logOutput", HTTP_GET, [](AsyncWebServerRequest * request) {
    Serial.println("output");
    request->send(SPIFFS, "/logEvents.csv", "text/html", true);
  });

  server.begin();


  // RTC
  if (! rtc.begin()) {
    Serial.println("Couldn't find RTC");
    Serial.flush();
    //    abort();
  }

  // The following line can be uncommented if the time needs to be reset.
  rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

  rtc.start();

  //EINK
  display.begin(THINKINK_MONO);
  display.clearBuffer();

  AFMS.begin();
  myMotor->setSpeed(255);
  logEvent("System Initialisation...");
}

void loop() {
  int moisture = readSoil();
  waterPlant(moisture);
  updateEPD();


  // waits 180 seconds (3 minutes) as per guidelines from adafruit.
  delay(180000);
  display.clearBuffer();

}

void updateEPD() {

  // Indigenous Country Name
  drawText("Arrernte", EPD_BLACK, 2, 0, 0);


  // Config
  drawText(WiFi.localIP().toString(), EPD_BLACK, 1, 130, 80);
  drawText(getTimeAsString(), EPD_BLACK, 1, 130, 100);
  drawText(getDateAsString(), EPD_BLACK, 1, 130, 110);


  // Draw lines to divvy up the EPD
  display.drawLine(0, 20, 250, 20, EPD_BLACK);
  display.drawLine(125, 20, 125, 122, EPD_BLACK);
  display.drawLine(0, 75, 250, 75, EPD_BLACK);

  drawText("Moisture", EPD_BLACK, 2, 0, 25);
  drawText(String(moistureValue), EPD_BLACK, 4, 0, 45);

  drawText("Pump", EPD_BLACK, 2, 130, 25);
  if (pumpIsRunning) {
    drawText("ON", EPD_BLACK, 4, 130, 45);
  } else {
    drawText("OFF", EPD_BLACK, 4, 130, 45);
  }

  drawText("Temp \tC", EPD_BLACK, 2, 0, 80);
  drawText(String(tempsensor.readTempC()), EPD_BLACK, 4, 0, 95);

  logEvent("Updating the EPD");
  display.display();

}

String processor(const String& var) {
  Serial.println(var);

  if (var == "DATETIME") {
    String datetime = getTimeAsString() + " " + getDateAsString();
    return datetime;
  }
  if (var == "MOISTURE") {
    readSoil();
    return String(moistureValue);
  }
  if (var == "TEMPINC") {
    return String(tempsensor.readTempC());
  }
  if (var == "PUMPSTATE") {
    if (pumpIsRunning) {
      return "ON";
    } else {
      return "OFF";
    }
  }
  return String();
}

void drawText(String text, uint16_t color, int textSize, int x, int y) {
  display.setCursor(x, y);
  display.setTextColor(color);
  display.setTextSize(textSize);
  display.setTextWrap(true);
  display.print(text);
}

String getDateAsString() {
  DateTime now = rtc.now();

  // Converts the date into a human-readable format.
  char humanReadableDate[20];
  sprintf(humanReadableDate, "%02d/%02d/%02d", now.day(), now.month(), now.year());

  return humanReadableDate;
}

String getTimeAsString() {
  DateTime now = rtc.now();

  // Converts the time into a human-readable format.
  char humanReadableTime[20];
  sprintf(humanReadableTime, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());

  return humanReadableTime;
}

void logEvent(String dataToLog) {
  /*
     Log entries to a file stored in SPIFFS partition on the ESP32.
  */
  // Get the updated/current time
  DateTime rightNow = rtc.now();
  char csvReadableDate[25];
  sprintf(csvReadableDate, "%02d,%02d,%02d,%02d,%02d,%02d,",  rightNow.year(), rightNow.month(), rightNow.day(), rightNow.hour(), rightNow.minute(), rightNow.second());

  String logTemp = csvReadableDate + dataToLog + "\n"; // Add the data to log onto the end of the date/time

  const char * logEntry = logTemp.c_str(); //convert the logtemp to a char * variable

  //Add the log entry to the end of logevents.csv
  appendFile(SPIFFS, "/logEvents.csv", logEntry);

  // Output the logEvents - FOR DEBUG ONLY. Comment out to avoid spamming the serial monitor.
  //  readFile(SPIFFS, "/logEvents.csv");

  Serial.print("\nEvent Logged: ");
  Serial.println(logEntry);
}


//This is a function used to get the soil moisture content
int readSoil() {
  logEvent("Reading Moisture Value");
  moistureValue = analogRead(soilPin);//Read the SIG value form sensor
  return moistureValue;//send current moisture value
}

void waterPlant(int moistureValue) {
  /*
     Write a function which takes the moisture value,
     and if it's below a certain value, turns the pump on.
     The function is to be called waterPlant() which will
     take the moisture value as an argument, and return no value.

     @param moistureValue int measured from Moisture Sensor
     @return: void
  */
  if (moistureValue < 1000 ) {
    // motor/pump on
    myMotor->run(FORWARD); // May need to change to BACKWARD
    pumpIsRunning = true;
  } else {
    // motor/pump off
    myMotor->run(RELEASE);
    pumpIsRunning = false;
  }

}

//SPIFFS File Functions
void readFile(fs::FS &fs, const char * path) {
  Serial.printf("Reading file: %s\r\n", path);

  File file = fs.open(path);
  if (!file || file.isDirectory()) {
    Serial.println("- failed to open file for reading");
    return;
  }

  Serial.println("- read from file:");
  while (file.available()) {
    Serial.write(file.read());
  }
  file.close();
}

void writeFile(fs::FS &fs, const char * path, const char * message) {
  Serial.printf("Writing file: %s\r\n", path);

  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("- failed to open file for writing");
    return;
  }
  if (file.print(message)) {
    Serial.println("- file written");
  } else {
    Serial.println("- write failed");
  }
  file.close();
}

void appendFile(fs::FS &fs, const char * path, const char * message) {
  Serial.printf("Appending to file: %s\r\n", path);

  File file = fs.open(path, FILE_APPEND);
  if (!file) {
    Serial.println("- failed to open file for appending");
    return;
  }
  if (file.print(message)) {
    Serial.println("- message appended");
  } else {
    Serial.println("- append failed");
  }
  file.close();
}

void renameFile(fs::FS &fs, const char * path1, const char * path2) {
  Serial.printf("Renaming file %s to %s\r\n", path1, path2);
  if (fs.rename(path1, path2)) {
    Serial.println("- file renamed");
  } else {
    Serial.println("- rename failed");
  }
}

void deleteFile(fs::FS &fs, const char * path) {
  Serial.printf("Deleting file: %s\r\n", path);
  if (fs.remove(path)) {
    Serial.println("- file deleted");
  } else {
    Serial.println("- delete failed");
  }
}
