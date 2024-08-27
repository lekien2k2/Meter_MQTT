#include <WiFi.h>
#include <Wire.h>
#include <Preferences.h>
#include <LiquidCrystal_I2C.h>
#include <Arduino.h>
#include <PZEM004Tv30.h>
#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include "page.h"
#include <HTTPClient.h>
#include <FirebaseESP32.h>
// i want to use time of internet
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <MqttClient.h>
#include <Eeprom24Cxx.h>
#define SWITCH_PIN 5
#define LED_PIN 2
#define FLASH_NAME_SPACE "piot"
#define SSID_AP "Smart Meter"
#define PASSWORD_AP "12345678"
#define MAX_AP_CONNECTIONS 1
#define MQTT_SERVER "broker.tbedev.cloud"
#define MQTT_PORT 1883
#define EEPROM_I2C_ADDRESS 0x50

#define MAX_KWH 1000000
#define MAX_WRITE 500000
#define MAX_SIZE_EEPROM 32000
#define TYPE_EEPROM 256

#define KWH_ADDRESS 0
#define KWH_ADDRESS_DEFAULT 12000
#define BIT_COUNTER_ADDRESS 4

#define DATA_NOT_SEND_ADDRESS 8
#define DATA_NOT_SEND_COUNTER 12
#define DATA_NOT_SEND_ADDRESS_DEFAULT 1000
#define DATA_NOT_SEND_ADDRESS_MAX 10000


#define LAST_TIME_ADDRESS 100
#define BOOT_TIME_ADDRESS 104

int currentAddress = 0; // Địa chỉ bắt đầu
int dataNotSyncAddress = DATA_NOT_SEND_ADDRESS_DEFAULT;
int dataNotSyncCounter = 0;
int writeCount = 0; // Đếm số lần ghi
int bitCounter = 0; // Biến đếm (bit counter)

static Eeprom24C eeprom(TYPE_EEPROM, EEPROM_I2C_ADDRESS); // AT24C02 - 2KB EEPROM

String user_name;
String proficiency;
bool name_received = false;
bool proficiency_received = false;

HardwareSerial pzemSerial(2);
PZEM004Tv30 pzem(pzemSerial, 16, 17);

WiFiClient wifiClient;
Preferences preferences;
AsyncWebServer server(80);
DNSServer dnsServer;

bool isAPMode = false;

struct FlashData
{
  String ssid;
  String password;
  String name;
  String device_id;
  String access_token;
  String client_id;
  String mqtt_username;
  String mqtt_password;
  int poll_interval;
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
    0b00000};

float kWh = 0.0;          // Giá trị kWh
float previous_kWh = 0.0; // Giá trị kWh trước đó

JsonArray datafailedArr;
std::vector<Metric> dataArr;
std::vector<Metric> dataArrTemp;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);
const int timeZoneOffset = 7 * 3600;

unsigned long lastDataSentTime = 0;
// const char* LAST_TIME_ADDRESS = "last_time";
// const char* BOOT_TIME_ADDRESS = "boot_time";

void sendDataToMqtt(void *pvParameters);
void checkSwitchButton(void *pvParameters);
void handleFormSubmit(AsyncWebServerRequest *request);
void reloadPreferences();
void readandprint(void *pvParameters);
unsigned long getCurrentTime();
unsigned long getStoredTime();
bool updateStoredTime();


String setupResponeHTML();

class CaptiveRequestHandler : public AsyncWebHandler
{
public:
  CaptiveRequestHandler() {}
  virtual ~CaptiveRequestHandler() {}

  bool canHandle(AsyncWebServerRequest *request)
  {
    // request->addInterestingHeader("ANY");
    return true;
  }

  void handleRequest(AsyncWebServerRequest *request)
  {
    request->send_P(200, "text/html", setupResponeHTML().c_str());
  }
};
// LiquidCrystal_I2C lcd(0x27, 20, 4);

MqttClient mqttClient;
void setup()
{
  // pinMode(SWITCH_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  Serial.begin(115200);
  Wire.begin(); // Khởi tạo giao tiếp I2C
  //   for(int i = 0; i < 4000; i++)
  // {
  //   ESP_LOGI("EEPROM", "Write 0 to %d", i*8);
  //  eeprom.write_8_byte(i*8, 0);
  // }
  // EEPROM.begin(512); // Bắt đầu EEPROM với kích thước cần thiết
  currentAddress = eeprom.read_4_byte(KWH_ADDRESS);     // Đọc địa chỉ hiện tại từ EEPROM
  bitCounter = eeprom.read_1_byte(BIT_COUNTER_ADDRESS); // Đọc biến đếm từ EEPROM
  if (!currentAddress)
  {
    currentAddress = KWH_ADDRESS_DEFAULT;
  }
  if (!bitCounter)
  {
    bitCounter = 0;
  }
  kWh = float(eeprom.read_4_byte(currentAddress)) / 100; // Đọc giá trị kWh từ EEPROM
  previous_kWh = kWh;

  dataNotSyncAddress = eeprom.read_4_byte(DATA_NOT_SEND_ADDRESS); // Đọc địa chỉ dữ liệu chưa gửi từ EEPROM
  if (!dataNotSyncAddress)
  {
    dataNotSyncAddress = DATA_NOT_SEND_ADDRESS_DEFAULT;
  }
  if (dataNotSyncAddress > DATA_NOT_SEND_ADDRESS_DEFAULT)
  {
    dataNotSyncCounter = eeprom.read_4_byte(DATA_NOT_SEND_COUNTER); // Đọc số lần gửi dữ liệu chưa gửi từ EEPROM
    if (dataNotSyncCounter > DATA_NOT_SEND_ADDRESS_MAX)
    {
      dataNotSyncCounter = 0;
    }
    if (dataNotSyncCounter > 0)
    {
      ESP_LOGI("EEPROM", "Data not sync counter: %d", dataNotSyncCounter);
      int dataNotSyncAddressTemp = DATA_NOT_SEND_ADDRESS_DEFAULT;
      for (int i = 0; i < dataNotSyncCounter; i++)
      {
        // dataArrTemp.push_back(.c_str());
        String serializedMetric = eeprom.read_string(dataNotSyncAddressTemp);

        // Tạo một document để parse chuỗi JSON
        DynamicJsonDocument doc(1024);

        // Parse chuỗi JSON thành JsonObject
        DeserializationError error = deserializeJson(doc, serializedMetric);
        if (error)
        {
          Serial.print(F("deserializeJson() failed: "));
          Serial.println(error.f_str());
          continue;
        }
        // Tạo đối tượng Metric từ dữ liệu đã parse
        Metric dataTemp;
        dataTemp.name = doc["name"].as<std::string>();
        dataTemp.ts = doc["ts"].as<uint64_t>();

        // Lấy JsonArray từ doc và lưu vào dataTemp.value
        dataTemp.value = doc["value"].as<JsonObject>();

        // Thêm đối tượng vào mảng
        dataArr.push_back(dataTemp);
        dataArrTemp.push_back(dataTemp);
      }
    }
  }

  if (!preferences.begin(FLASH_NAME_SPACE, false))
  {
    Serial.println("Failed to open preferences");
  }

  xTaskCreatePinnedToCore(
      checkSwitchButton,   /* Function to implement the task */
      "checkSwitchButton", /* Name of the task */
      2048,                /* Stack size in words */
      NULL,                /* Task input parameter */
      0,                   /* Priority of the task */
      NULL,                /* Task handle. */
      0);                  /* Core where the task should run */
                           // if (isAPMode)
                           // {
                           //   ESP_LOGE("AP Mode", "AP Mode");
                           //   WiFi.mode(WIFI_AP);
                           //   WiFi.softAP(SSID_AP, PASSWORD_AP, 1, false, MAX_AP_CONNECTIONS);

  //   server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
  //             { request->send_P(200, "text/html", setupResponeHTML().c_str()); });
  //   server.on("/submit", HTTP_POST, handleFormSubmit);
  //   dnsServer.start(53, "*", WiFi.softAPIP());
  //   server.addHandler(new CaptiveRequestHandler()).setFilter(ON_AP_FILTER);
  //   server.begin();
  //   delay(1000);
  //   xTaskCreatePinnedToCore(
  //       readandprint,   /* Function to implement the task */
  //       "readandprint", /* Name of the task */
  //       10000,          /* Stack size in words */
  //       NULL,           /* Task input parameter */
  //       0,              /* Priority of the task */
  //       NULL,           /* Task handle. */
  //       0);             /* Core where the task should run */
  // }
  // else
  // {

  

  reloadPreferences();
  Serial.println("Connecting to WiFi");
  Serial.println(flashData.ssid);
  Serial.println(flashData.password);

  // WiFi.begin(flashData.ssid.c_str(), flashData.password.c_str());

  // while (WiFi.status() != WL_CONNECTED)
  // {
  //   delay(500);
  //   Serial.print(".");
  // }
  mqttClient.init(flashData.ssid.c_str(), flashData.password.c_str(), MQTT_SERVER, MQTT_PORT, flashData.client_id.c_str(), flashData.mqtt_username.c_str(), flashData.mqtt_password.c_str());
  mqttClient.begin();

  timeClient.begin();
  // timeClient.setTimeOffset(timeZoneOffset);
  timeClient.update();
  if(updateStoredTime()){
    ESP_LOGI("Time", "Time updated");
  }
  else{
    ESP_LOGE("Time", "Time not updated");
  }
  xTaskCreatePinnedToCore(
      readandprint,   /* Function to implement the task */
      "readandprint", /* Name of the task */
      5000,           /* Stack size in words */
      NULL,           /* Task input parameter */
      0,              /* Priority of the task */
      NULL,           /* Task handle. */
      0);             /* Core where the task should run */
  // }
}

void loop()
{
  int time = millis();

  if (isAPMode)
  {
    dnsServer.processNextRequest();
    // Nháy nhanh hơn trong AP mode (mỗi 200ms đổi trạng thái)
    if ((time / 200) % 2 == 0)
    {
      digitalWrite(LED_PIN, HIGH);
    }
    else
    {
      digitalWrite(LED_PIN, LOW);
    }
  }
  else
  {
    // Nháy chậm hơn trong STA mode (mỗi 1000ms đổi trạng thái)
    if ((time / 1000) % 2 == 0)
    {
      digitalWrite(LED_PIN, HIGH);
    }
    else
    {
      digitalWrite(LED_PIN, LOW);
    }
  }
}

void checkSwitchButton(void *pvparameter)
{
  Serial.println("checkSwitchButton");
  const int holdTime = 3000; // Thời gian giữ để chuyển chế độ là 3000ms (3s)
  unsigned long pressStartTime = 0;
  bool isHolding = false;
  bool hasChangedMode = false;

  while (1)
  {
    int switchState = digitalRead(SWITCH_PIN);

    if (switchState == HIGH)
    {
      if (!isHolding)
      {
        pressStartTime = millis(); // Bắt đầu đo thời gian khi nút được nhấn
        isHolding = true;
        hasChangedMode = false;
        ESP_LOGI("Switch button", "Switch button pressed");
      }
      else if (millis() - pressStartTime >= holdTime && !hasChangedMode)
      {
        // Chuyển chế độ nếu nút được giữ trong 3 giây
        Serial.println("Switch button held for 3s - Changing mode");
        hasChangedMode = true; // Ensure this block only runs once per press

        if (isAPMode)
        {
          isAPMode = false;
          delay(1000);
          ESP.restart();
        }
        else
        {
          mqttClient.stop();
          if (mqttClient.mqttTaskHandle != NULL)
          {
            vTaskDelete(mqttClient.mqttTaskHandle); // Xóa task bằng handle
            mqttClient.mqttTaskHandle = NULL;       // Đặt handle về NULL để tránh xóa lại
          }
          WiFi.mode(WIFI_OFF);
          ESP_LOGE("AP Mode", "AP Mode");
          WiFi.mode(WIFI_AP);
          WiFi.softAP(SSID_AP, PASSWORD_AP, 1, false, MAX_AP_CONNECTIONS);

          server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
                    { request->send_P(200, "text/html", setupResponeHTML().c_str()); });
          server.on("/submit", HTTP_POST, handleFormSubmit);
          dnsServer.start(53, "*", WiFi.softAPIP());
          server.addHandler(new CaptiveRequestHandler()).setFilter(ON_AP_FILTER);
          server.begin();
          isAPMode = true;
          // xTaskCreatePinnedToCore(
          //     readandprint,   /* Function to implement the task */
          //     "readandprint", /* Name of the task */
          //     5000,           /* Stack size in words */
          //     NULL,           /* Task input parameter */
          //     0,              /* Priority of the task */
          //     NULL,           /* Task handle. */
          //     0);             /* Core where the task should run */
          //                     // }
        }
      }
    }
    else
    {
      isHolding = false; // Reset lại trạng thái nếu nút không còn được giữ
    }

    vTaskDelay(50 / portTICK_PERIOD_MS); // Chống dội nút bấm
  }
}

String setupResponeHTML()
{
  reloadPreferences();
  Serial.println("request index");
  // Serial.println(flashData.ssid);
  String responseHTML = String(index_html);
  // Serial.println(responseHTML);
  ESP_LOGI("Setup", "client_id: %s", flashData.client_id.c_str());
  responseHTML.replace("$ssid", flashData.ssid);
  responseHTML.replace("$password", flashData.password);
  responseHTML.replace("$device_id", flashData.device_id);
  responseHTML.replace("$name", flashData.name);
  responseHTML.replace("$mqtt_username", flashData.mqtt_username);
  responseHTML.replace("$mqtt_password", flashData.mqtt_password);
  responseHTML.replace("$access_token", flashData.access_token);
  responseHTML.replace("$client_id", flashData.client_id);
  // for (int i = 0; i < flashData.leight(); i++)
  // {
  //   responseHTML.replace("$" + flashData[i], flashData[flashData[i]]);
  // }

  // Serial.println("==================================================================");
  // Serial.println(responseHTML);
  return responseHTML;
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
  if (request->hasParam("mqtt_username", true))
  {
    AsyncWebParameter *mqtt_username = request->getParam("mqtt_username", true);
    flashData.mqtt_username = mqtt_username->value();
  }

  if (request->hasParam("mqtt_password", true))
  {
    AsyncWebParameter *mqtt_password = request->getParam("mqtt_password", true);
    flashData.mqtt_password = mqtt_password->value();
  }

  if (request->hasParam("access_token", true))
  {
    AsyncWebParameter *access_token = request->getParam("access_token", true);
    flashData.access_token = access_token->value();
  }

  if (request->hasParam("client_id", true))
  {
    AsyncWebParameter *client_id = request->getParam("client_id", true);
    flashData.client_id = client_id->value();
  }

  ESP_LOGI("Setup", "client_id: %s", flashData.client_id.c_str());

  preferences.putString("ssid", flashData.ssid);
  preferences.putString("password", flashData.password);
  preferences.putString("device_id", flashData.device_id);
  preferences.putString("name", flashData.name);
  // preferences.putString("access_token", flashData.access_token);
  preferences.putString("client_id", flashData.client_id);
  preferences.putString("mqtt_username", flashData.mqtt_username);
  preferences.putString("mqtt_password", flashData.mqtt_password);

  preferences.end();
  String message = "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>Smart Meter</title></head><body><h1>Smart Meter</h1><p>Setup successfully!</p></body></html>";
  request->send(200, "text/html", message);
  delay(1000);
  ESP.restart();
}

void sendDataToMqtt(void *pvParameters)
{
  Data *data = (Data *)pvParameters;
  double voltage = data->voltage;
  double current = data->current;
  double power = data->power;
  double energy = data->energy;
  ESP_LOGI("MqttClient", "Send data to mqtt: %f %f %f %f", voltage, current, power, energy);
  // mqttClient.sendMetric("voltage", voltage);
  // mqttClient.sendMetric("current", current);
  // mqttClient.sendMetric("power", power);
  // Serial.println("sendDataToMqtt");
  // mqttClient.sendMetric("kWh", energy);
  DynamicJsonDocument doc(1024);
  DynamicJsonDocument doc2(1024);
  JsonObject metricData = doc.to<JsonObject>();
  JsonObject obj = doc2.to<JsonObject>();
  JsonArray newDataArr = doc.createNestedArray("data");
  uint64_t timestamp = getCurrentTime() * 1000ULL;
  ESP_LOGI("MqttClient", "Send data to mqtt: %llu ", timestamp);
  // obj["Total kWh"] = energy;
  // obj["Voltage"] = voltage;
  // obj["Current"] = current;
  // obj["Power"] = power;
  std::vector<String> nameData;
  nameData.push_back("Total kWh");
  nameData.push_back("Voltage");
  nameData.push_back("Current");
  nameData.push_back("Power");

  std::vector<double> valueData;
  valueData.push_back(energy);
  valueData.push_back(voltage);
  valueData.push_back(current);
  valueData.push_back(power);
  for(int i = 0; i<nameData.size(); i++){
    metricData["name"] = nameData[i];
    metricData["value"] = valueData[i];
    metricData["ts"] = timestamp;
    Metric metric;
    metric.name = nameData[i].c_str();
    metric.value = valueData[i];
    metric.ts = timestamp;
  // Push vào queue
  if (mqttClient.connected())
  {
    mqttClient.metricQueue.push(metric);
    if(dataArrTemp.size() >= 1){
      for (int i = 0; i < dataArrTemp.size(); i++)
    {
      mqttClient.metricQueue.push(dataArrTemp[i]);
    }
    dataArrTemp.clear();
    dataArr.clear();
    for (int i = DATA_NOT_SEND_ADDRESS_DEFAULT; i < dataNotSyncAddress; i += 8)
    {
      eeprom.write_8_byte(i, 0);
    }
    eeprom.write_4_byte(DATA_NOT_SEND_ADDRESS, DATA_NOT_SEND_ADDRESS_DEFAULT);
    eeprom.write_4_byte(DATA_NOT_SEND_COUNTER, 0);
    dataNotSyncAddress = DATA_NOT_SEND_ADDRESS_DEFAULT;
    dataNotSyncCounter = 0;

    }
  }
  else
  {
    // Thêm metric vào dataArr
    dataArrTemp.push_back(metric);
    // Lưu giá trị đầu tiên vào EEPROM nếu chưa lưu
    if (dataArr.size() == 0)
    {
      dataArr.push_back(metric);
      String serializedMetric;
      serializeJson(metricData, serializedMetric);
      eeprom.write_string(dataNotSyncAddress, serializedMetric.c_str());
      dataNotSyncAddress += serializedMetric.length() + 1;
      dataNotSyncCounter = 1;
      eeprom.write_4_byte(DATA_NOT_SEND_ADDRESS, dataNotSyncAddress);
      eeprom.write_4_byte(DATA_NOT_SEND_COUNTER, dataNotSyncCounter);
      ESP_LOGI("MqttClient", "SAVE THE FIRST METRIC: %s", serializedMetric.c_str());
      ESP_LOGI("MqttClient", "Save metric: %s", serializedMetric.c_str());
    }
    else
    {
      // Lưu giá trị cuối cùng vào EEPROM nếu thời gian giữa 2 giá trị lớn hơn 24h
      if (metric.ts - dataArr.back().ts > 60 * 1000)
      {
        dataArr.push_back(metric);
        String serializedMetric;
        serializeJson(metricData, serializedMetric);
        eeprom.write_string(dataNotSyncAddress, serializedMetric.c_str());

        ESP_LOGI("MqttClient", "Save metric: %s", serializedMetric.c_str());
        ESP_LOGI("MqttClient", "SAVE METRIC WITH COUNTER: %d, ADDRESS: %d, MERTRIC: %s", dataNotSyncCounter, dataNotSyncAddress, serializedMetric.c_str());
        dataNotSyncAddress += serializedMetric.length() + 1;
        dataNotSyncCounter++;
        eeprom.write_4_byte(DATA_NOT_SEND_ADDRESS, dataNotSyncAddress);
        eeprom.write_4_byte(DATA_NOT_SEND_COUNTER, dataNotSyncCounter);
      }
    }
  }
  }
  


}

void reloadPreferences()
{
  preferences.begin(FLASH_NAME_SPACE, false);
  Serial.println("reloadPreferences");
  flashData.ssid = preferences.getString("ssid", "");
  flashData.password = preferences.getString("password", "");
  flashData.device_id = preferences.getString("device_id", "");
  flashData.name = preferences.getString("name", "");
  flashData.access_token = preferences.getString("access_token", "");
  flashData.client_id = preferences.getString("client_id", "");
  flashData.mqtt_username = preferences.getString("mqtt_username", "");
  flashData.mqtt_password = preferences.getString("mqtt_password", "");

  Serial.println(flashData.ssid);
}

void readandprint(void *pvParameters)
{
  Serial.println("readandprint");
  // unsigned long lastDataSentTime = 0;
  unsigned long lastTime = getCurrentTime();
  //  kWh += 0.01;
  Serial.println(kWh);
  while (1)
  {
    // open uart port tx2
    // pzemSerial.begin(9600, SERIAL_8N1, 16, 17);

    // if(data.power >100 && pzem.power() < 100)
    // {
    //   lcd.clear();
    //   data.power = pzem.power();
    // }
    // else{
    //   data.power = pzem.power();
    // }
    // Serial.println("readandprint");
    // data.voltage = pzem.voltage();
    // data.current = pzem.current();
    // // data.power = pzem.power();
    // data.energy = pzem.energy();
    // // close uart port tx2
    // pzemSerial.end();

    if (kWh - previous_kWh >= 0.01)
    {
      previous_kWh = kWh;

      // Kiểm tra nếu giá trị kWh vượt quá giới hạn
      if (kWh > MAX_KWH)
      {
        kWh = 0;                            // Đặt lại giá trị kWh về 0
        previous_kWh = 0;                   // Đặt lại giá trị kWh trước đó về 0
        bitCounter++;                       // Tăng biến đếm
        eeprom.write_1_byte(5, bitCounter); // Ghi biến đếm vào EEPROM
      }

      // Ghi giá trị kWh vào EEPROM
      ESP_LOGI("readandprint", "Writing to EEPROM kWh: %f", kWh);
      eeprom.write_4_byte(currentAddress, kWh * 100);

      writeCount++;

      // Kiểm tra nếu số lần ghi vượt quá maxWrites
      if (writeCount >= MAX_WRITE)
      {
        writeCount = 0;      // Đặt lại biến đếm
        currentAddress += 4; // Chuyển sang ô nhớ tiếp theo

        // Nếu đạt đến ô nhớ cuối cùng, quay về ô nhớ đầu tiên
        if (currentAddress >= MAX_SIZE_EEPROM)
        { // Kích thước của EEPROM 24C256
          currentAddress = 12;
        }
        ESP_LOGI("readandprint", "Current address: %d", currentAddress);
        eeprom.write_4_byte(0, currentAddress); // Lưu địa chỉ vào EEPROM
      }
    }

    unsigned long time = getCurrentTime();

    if (time - lastTime > 10 && isAPMode == false)
    {
      ESP_LOGI("readandprint", "Time: %d", time);
      data.energy = kWh;
      sendDataToMqtt(&data);
      lastTime = time;
    }
    // kWh += 0.01;

    // vTaskDelay(1000 / portTICK_PERIOD_MS);
    // lcd.clear();
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}



// Trong chế độ STA, sau khi lấy thời gian từ NTP
bool updateStoredTime() {
  
    unsigned long currentTime = timeClient.getEpochTime();
    if (currentTime == 0) {
        Serial.println("Failed to get time from NTP");
        return false;
    }
    ESP_LOGI("Time", "Current time: %lu", currentTime);

    // preferences.begin("time_storage", false);
    // preferences.begin(FLASH_NAME_SPACE, false);
    // bool success = preferences.putULong(LAST_TIME_ADDRESS, currentTime);
     eeprom.write_4_byte(LAST_TIME_ADDRESS, currentTime);
    // Lưu thời điểm boot
    // preferences.putULong(BOOT_TIME_ADDRESS, millis() / 1000);
    eeprom.write_4_byte(BOOT_TIME_ADDRESS, millis() / 1000);

    // preferences.end();

    return true;
}

// Trong chế độ AP, khi cần thời gian
unsigned long getStoredTime() {
    // preferences.begin("time_storage", true);
    // preferences.begin(FLASH_NAME_SPACE, false);
    // unsigned long storedTime = preferences.getULong(LAST_TIME_ADDRESS, 0);
    // unsigned long bootTime = preferences.getULong(BOOT_TIME_ADDRESS, 0);
    // preferences.end();

    unsigned long storedTime = eeprom.read_4_byte(LAST_TIME_ADDRESS);
    unsigned long bootTime = eeprom.read_4_byte(BOOT_TIME_ADDRESS);
    // ESP_LOGI("Time", "Stored time: %lu", storedTime);
    // if (storedTime == 0 || bootTime == 0) {
    //     Serial.println("No valid stored time found");
    //     return 0;
    // }

    unsigned long currentBootDuration = millis() / 1000;
    unsigned long storedBootDuration = currentBootDuration > bootTime ? currentBootDuration - bootTime : currentBootDuration;

    return storedTime + storedBootDuration;
}

// Sử dụng trong cả hai chế độ
unsigned long getCurrentTime() {
    if (isAPMode) {
        return getStoredTime();
    } else {
        unsigned long ntpTime = timeClient.getEpochTime();
        ESP_LOGI("Time", "NTP time: %lu", ntpTime);
        ESP_LOGI("Time", "Stored time: %lu", getStoredTime());
        if (!ntpTime) {
            Serial.println("NTP time not available, using stored time");
            return getStoredTime();
        }
        return ntpTime;
    }
}