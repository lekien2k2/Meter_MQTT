#include <WiFi.h>
#include <Wire.h>
#include <Preferences.h>
#include <LiquidCrystal_I2C.h>
#include <Arduino.h>
#include <PZEM004Tv30.h>
#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "page.h"
#include <HTTPClient.h>
#include <FirebaseESP32.h>
// i want to use time of internet
#include <NTPClient.h>
#include <WiFiUdp.h>

#define SWITCH_PIN 5
#define FLASH_NAME_SPACE "piot"
#define SSID_AP "ESP32 - Piot Client"
#define PASSWORD_AP "12345678"
#define MAX_AP_CONNECTIONS 1

HardwareSerial pzemSerial(2);
PZEM004Tv30 pzem(pzemSerial, 16, 17);

#define FIREBASE_HOST "https://doan2-a4c3b-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH "AIzaSyCFEvF05xfj_nUB-cF2rGGaB5XIq_WiRHQ"
FirebaseData fbdo;
String path = "/rooms/";

const char *db_address = "https://ap-southeast-1.aws.data.mongodb-api.com/app/data-rowcm/endpoint/data/v1/action/insertOne";

WiFiClient wifiClient;
Preferences preferences;
AsyncWebServer server(80);
bool isAPMode = false;

struct FlashData
{
  String ssid;
  String password;
  String device_id;
  String name;
};

FlashData flashData;

struct Data
{
  float voltage;
  float current;
  float power;
  float energy;
};

Data data;

byte wifi[8] = {
  0b00000,
  0b01110,
  0b10001,
  0b00100,
  0b01010,
  0b00000,
  0b00100,
  0b00000
};

bool flag_wifi = false;


JsonArray datafailedArr;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);
const int timeZoneOffset = 7 * 3600;

unsigned long lastDataSentTime = 0;

void sendDataToFirebase(void *pvParameters);
void checkSwitchButton(void *pvParameters);
void handleFormSubmit(AsyncWebServerRequest *request);
void reloadPreferences();
void sendDataToDB(float voltage, float current, float power, float energy);
void checkConnectWifi();
void reloadPreferences();
void readandprint(void *pvParameters);

LiquidCrystal_I2C lcd(0x27, 20, 4);

void setup()
{
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Loading... ");
  Serial.begin(115200);
  lcd.begin(20, 4);
  if (!preferences.begin(FLASH_NAME_SPACE, false))
  {
    Serial.println("Failed to open preferences");
  }

  isAPMode = digitalRead(SWITCH_PIN) == LOW;

  xTaskCreatePinnedToCore(
      checkSwitchButton,   /* Function to implement the task */
      "checkSwitchButton", /* Name of the task */
      2048,                /* Stack size in words */
      NULL,                /* Task input parameter */
      0,                   /* Priority of the task */
      NULL,                /* Task handle. */
      0);                  /* Core where the task should run */
  if (isAPMode)
  {
    lcd.setCursor(0, 0);
    lcd.print("AP Mode");
    lcd.setCursor(0, 1);
    lcd.print("SSID: ");
    lcd.setCursor(0, 2);
    lcd.print("Pass: ");
    lcd.setCursor(0, 3);
    lcd.print("IP: ");
    lcd.setCursor(6, 1);
    lcd.print(SSID_AP);
    lcd.setCursor(6, 2);
    lcd.print(PASSWORD_AP);
    lcd.setCursor(4, 3);
    lcd.print(WiFi.softAPIP());

    WiFi.softAP(SSID_AP, PASSWORD_AP, 1, false, MAX_AP_CONNECTIONS);

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
              {
            reloadPreferences();
            Serial.println("request index");
            String responseHTML = String(index_html);
            responseHTML.replace("$ssid", flashData.ssid);
            responseHTML.replace("$password", flashData.password);
            responseHTML.replace("$device_id", flashData.device_id);
            responseHTML.replace("$name", flashData.name);
            request->send(200, "text/html", responseHTML); });

    server.on("/submit", HTTP_POST, handleFormSubmit);
    server.begin();
    delay(1000);
    lcd.clear();
      xTaskCreatePinnedToCore(
      readandprint,   /* Function to implement the task */
      "readandprint", /* Name of the task */
      10000,          /* Stack size in words */
      NULL,           /* Task input parameter */
      0,              /* Priority of the task */
      NULL,           /* Task handle. */
      0);             /* Core where the task should run */
  }
  else
  {
    
    preferences.begin(FLASH_NAME_SPACE, false);

    reloadPreferences();

    lcd.setCursor(0, 0);
    lcd.print("Connecting to WiFi:");
    Serial.println("Connecting to WiFi");
    Serial.println(flashData.ssid);
    Serial.println(flashData.password);
    lcd.setCursor(0, 2);
    lcd.print("IP: ");
    lcd.setCursor(0, 3);
    lcd.print("Connecting...");
    lcd.setCursor(0, 1);
    lcd.print(flashData.ssid);

    WiFi.begin(flashData.ssid.c_str(), flashData.password.c_str());

    while (WiFi.status() != WL_CONNECTED)
    {
      delay(500);
      Serial.print(".");
    }
    lcd.setCursor(4, 2);
    lcd.print(WiFi.localIP());
    lcd.setCursor(0, 3);
    lcd.print("Connected");
    lcd.setCursor(12, 3);
    lcd.print("OK");
    lcd.clear();
xTaskCreatePinnedToCore(
      readandprint,   /* Function to implement the task */
      "readandprint", /* Name of the task */
      5000,           /* Stack size in words */
      NULL,           /* Task input parameter */
      0,              /* Priority of the task */
      NULL,           /* Task handle. */
      0               /* Core where the task should run */
    );

    timeClient.begin();
    timeClient.setTimeOffset(timeZoneOffset);
    timeClient.update();

    Firebase.begin(FIREBASE_HOST, FIREBASE_AUTH);
    sendDataToDB(pzem.voltage(), pzem.current(), pzem.power(), pzem.energy());
    xTaskCreatePinnedToCore(
      sendDataToFirebase,   /* Function to implement the task */
      "sendDataToFirebase", /* Name of the task */
      10000,                /* Stack size in words */
      &data,               /* Task input parameter */
      1,                   /* Priority of the task */
      NULL,                /* Task handle. */
      1                    /* Core where the task should run */
    );
      
  }
      
}

void loop()
{
}

void checkSwitchButton(void *pvparameter)
{
  Serial.println("checkSwitchButton");
  while (1)
  {
    // Serial.println("checkSwitchButton" + String(isAPMode));
    if (isAPMode)
  {
    if (digitalRead(SWITCH_PIN) == HIGH)
    {
      Serial.println("Switch button pressed 1");
      isAPMode = false;
      preferences.end();
      ESP.restart();
    }
  }
  else
  {
    checkConnectWifi();
    if (digitalRead(SWITCH_PIN) == LOW)
    {
      Serial.println("Switch button pressed 2");
      isAPMode = true;
      preferences.end();
      ESP.restart();
    }
  }
  }
}

void checkConnectWifi()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("WiFi disconnected");
    flag_wifi = false;
    WiFi.begin(flashData.ssid.c_str(), flashData.password.c_str());
    while (WiFi.status() != WL_CONNECTED)
    {
      delay(500);
      Serial.print(".");
    }
    Firebase.begin(FIREBASE_HOST, FIREBASE_AUTH);
  }
  else
  {
    // Serial.println("WiFi connected");
    flag_wifi = true;
  }
}

void handleFormSubmit(AsyncWebServerRequest *request)
{
  if (request->hasParam("ssid", true))
  {
    AsyncWebParameter *ssid = request->getParam("ssid", true);
    flashData.ssid = ssid->value();
  }
  if (request->hasParam("password", true))
  {
    AsyncWebParameter *password = request->getParam("password", true);
    flashData.password = password->value();
  }
  if (request->hasParam("device_id", true))
  {
    AsyncWebParameter *device_id = request->getParam("device_id", true);
    flashData.device_id = device_id->value();
  }
  if (request->hasParam("name", true))
  {
    AsyncWebParameter *name = request->getParam("name", true);
    flashData.name = name->value();
  }

  preferences.putString("ssid", flashData.ssid);
  preferences.putString("password", flashData.password);
  preferences.putString("device_id", flashData.device_id);
  preferences.putString("name", flashData.name);
  preferences.end();
  String message = "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>ESP32 - Piot Client</title></head><body><h1>ESP32 - Piot Client</h1><p>Setup successfully!</p></body></html>";
  request->send(200, "text/html", message);
  delay(1000);
  ESP.restart();
}

void sendDataToFirebase(void *pvParameters)
{
  Data *data = (Data *)pvParameters;
  float voltage = data->voltage;
  float current = data->current;
  float power = data->power;
  float energy = data->energy;
    Firebase.begin(FIREBASE_HOST, FIREBASE_AUTH);
      for(;;) // Infinite loop
    {
    Serial.println("sendDataToFirebase");
    if (!isnan(data->voltage))
    {
      if (Firebase.setFloat(fbdo, path + flashData.device_id + "/volt", data->voltage))
      {
        Serial.println("volt set successfully");
      }
      else
      {
        Serial.println("volt set failed");
        Serial.println("Reason: " + fbdo.errorReason());
      }
    }
    else
    {
      if (Firebase.setString(fbdo, path + flashData.device_id + "/volt", "NaN"))
      {
        Serial.println("volt set successfully");
      }
      else
      {
        Serial.println("volt set failed");
        Serial.println("Reason: " + fbdo.errorReason());
      }
    }

    if (!isnan(data->current))
    {
      if (Firebase.setFloat(fbdo, path + flashData.device_id + "/electric", data->current))
      {
        Serial.println("current set successfully");
      }
      else
      {
        Serial.println("current set failed");
        Serial.println("Reason: " + fbdo.errorReason());
      }
    }
    else
    {
      if (Firebase.setString(fbdo, path + flashData.device_id + "/electric", "NaN"))
      {
        Serial.println("current set successfully");
      }
      else
      {
        Serial.println("current set failed");
        Serial.println("Reason: " + fbdo.errorReason());
      }
    }

    if (!isnan(data->power))
    {
      if (Firebase.setFloat(fbdo, path + flashData.device_id + "/power", data->power))
      {
        Serial.println("power set successfully");
      }
      else
      {
        Serial.println("power set failed");
        Serial.println("Reason: " + fbdo.errorReason());
      }
    }
    else
    {
      if (Firebase.setString(fbdo, path + flashData.device_id + "/power", "NaN"))
      {
        Serial.println("power set successfully");
      }
      else
      {
        Serial.println("power set failed");
        Serial.println("Reason: " + fbdo.errorReason());
      }
    }

    if (!isnan(data->energy))
    {
      if (Firebase.setFloat(fbdo, path + flashData.device_id + "/energy", data->energy))
      {
        Serial.println("energy set successfully");
      }
      else
      {
        Serial.println("energy set failed");
        Serial.println("Reason: " + fbdo.errorReason());
      }
    }
    else
    {
      if (Firebase.setString(fbdo, path + flashData.device_id + "/energy", "NaN"))
      {
        Serial.println("energy set successfully");
      }
      else
      {
        Serial.println("energy set failed");
        Serial.println("Reason: " + fbdo.errorReason());
      }
    }
     unsigned long currentTime = millis();
    if (currentTime - lastDataSentTime > 15*60*1000)
    {
      sendDataToDB(voltage, current, power, energy);
      lastDataSentTime = currentTime;
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
   }
  }

void sendDataToDB(float voltage, float current, float power, float energy)
{
    if (datafailedArr.size() > 0)
    {
      for (int i = 0; i < datafailedArr.size(); i++)
      {
        String jsonData = datafailedArr[i];
        HTTPClient http;
        http.begin(db_address);
        http.addHeader("Content-Type", "application/json");
        http.addHeader("Access-Control-Request-Headers", "*");
        http.addHeader("api-key", "n4c6zKxqZ9PANO3eAJgOnRY1O3w8j498evDpDnGmLC7u2JAhkGg79hhikVJY54H1");
        int httpCode = http.POST(jsonData);
        String payload = http.getString();
        Serial.println("HTTP Code: " + String(httpCode));
        Serial.println("Server Response: " + payload);
        http.end();
        if (httpCode == 200 || httpCode == 201)
        {
          datafailedArr.remove(i);
        }
      }
    }
    timeClient.update();

    StaticJsonDocument<200> doc;
    doc["database"] = "test";
    doc["collection"] = "rooms";
    doc["dataSource"] = "Cluster0";
    doc["document"]["device_id"] = flashData.device_id;
    doc["document"]["device_name"] = flashData.name;
    doc["document"]["time"] = timeClient.getFormattedDate();
    // doc["document"]["voltage"] = voltage;
    // doc["document"]["current"] = current;
    doc["document"]["power"] = power;
    doc["document"]["energy"] = energy;

    // Chuyển đổi JSON document thành chuỗi JSON
    String jsonData;
    serializeJson(doc, jsonData);

    // Tạo đối tượng HTTPClient
    HTTPClient http;

    // Đặt URL của MongoDB API endpoint

    // Bắt đầu kết nối đến MongoDB API
    http.begin(db_address);

    // Đặt tiêu đề Content-Type là application/json
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Access-Control-Request-Headers", "*");
    http.addHeader("api-key", "n4c6zKxqZ9PANO3eAJgOnRY1O3w8j498evDpDnGmLC7u2JAhkGg79hhikVJY54H1");

    // Gửi yêu cầu POST với dữ liệu JSON
    // Serial.println("Sending data to DB" + jsonData);
    int httpCode = http.POST(jsonData);

    // Đọc phản hồi từ server
    String payload = http.getString();

    // In mã HTTP và phản hồi
    Serial.println("HTTP Code: " + String(httpCode));
    Serial.println("Server Response: " + payload);

    // Đóng kết nối
    http.end();

    if (httpCode != 201)
    {
      Serial.println("Send data to DB failed");
      datafailedArr.add(jsonData);
    }
    else
    {
      Serial.println("Send data to DB successfully");
    }
}

void reloadPreferences()
{
  flashData.ssid = preferences.getString("ssid", "");
  flashData.password = preferences.getString("password", "");
  flashData.device_id = preferences.getString("device_id", "");
  flashData.name = preferences.getString("name", "");
}

void readandprint(void *pvParameters)
{
  Serial.println("readandprint SATARADSdAS");
  lcd.clear();
  // unsigned long lastDataSentTime = 0;
  while (1)
  {
    // open uart port tx2
    pzemSerial.begin(9600, SERIAL_8N1, 16, 17);
    
    if(data.power >100 && pzem.power() < 100)
    {
      lcd.clear();
      data.power = pzem.power();
    }
    else{
      data.power = pzem.power();
    }
    Serial.println("readandprint");
    data.voltage = pzem.voltage();
    data.current = pzem.current();
    // data.power = pzem.power();
    data.energy = pzem.energy();
    // close uart port tx2
    pzemSerial.end();

    lcd.setCursor(0, 0);
    lcd.print("Volt: ");
    lcd.print(data.voltage);
    lcd.print("V");
    lcd.setCursor(0, 1);
    lcd.print("Current: ");
    lcd.print(data.current);
    lcd.print("A");
    lcd.setCursor(0, 2);
    lcd.print("Power: ");
    lcd.print(data.power);
    lcd.print("W");
    lcd.setCursor(0, 3);
    lcd.print("Energy: ");
    lcd.print(data.energy);
    lcd.print("kWh");
    // vTaskDelay(1000 / portTICK_PERIOD_MS);
    // lcd.clear();
    if (flag_wifi)
    {
      lcd.createChar(0, wifi);
      lcd.setCursor(18, 0);
      lcd.write(byte(0));
    }
    else
    {
      lcd.setCursor(18, 0);
      lcd.print("X");
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}