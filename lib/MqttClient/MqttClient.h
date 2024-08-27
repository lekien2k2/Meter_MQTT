/*
  MqttClient.h - Library for MqttClient demo code.
  Created by LeKien, Aug 22, 2024.
  Released into the public domain.
*/

#ifndef MqttClient_h
#define MqttClient_h

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <map>
#include <functional>
#include <algorithm>
#include <queue>  

struct MqttConfig
{
  const char *wifiSSID;
  const char *wifiPassword;
  const char *mqttServer;
  int mqttPort;
  const char *deviceId;
  const char *accessToken;
};

struct Metric {
    std::string name;
    double value;
    uint64_t ts;
};

class MqttClient
{
public:
  MqttClient();
  MqttClient(const char *wifiSSID, const char *wifiPassword, const char *mqttServer, int mqttPort, const char *clientId, const char *username, const char *password);
  void init(const char *wifiSSID, const char *wifiPassword, const char *mqttServer, int mqttPort, const char *clientId, const char *username, const char *password);
  void begin();
  void loop();
  void stop();
  boolean connected();
  std::queue<Metric> metricQueue;

  void sendMetric(uint64_t timestamp, const char *key, double value);
  void sendMetric(uint64_t timestamp, const char *key, JsonObject value);

  void sendMetric(const char *key, double value);

  void sendAttribute(const char *key, double value);
  void sendAttribute(const char *key, const char *value);

  void on(const char *key, void (*callback)(String));

  ~MqttClient();
  TaskHandle_t mqttTaskHandle = NULL; // Task handle
private:
  void initWiFi();
  void reconnect();
  static void callback(char *topic, byte *message, unsigned int length);

  const char *_ssid;
  const char *_password;
  const char *_mqttServer;
  int _mqttPort;
  const char *_clientId;
  const char *_username;
  const char *_passwordMqtt;

  bool _is_stopped;
  WiFiClient _espClient;
  PubSubClient _client;

  String _sendMetricTopic;
  String _sendAttributeTopic;

  std::map<String, std::function<void(String)>> _callbacks;
  static MqttClient *_instance;
};

#endif
