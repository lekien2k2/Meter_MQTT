#include "MqttClient.h"

MqttClient *MqttClient::_instance = nullptr;

MqttClient::MqttClient()
    : _client(_espClient)
{
    _client.setServer(_mqttServer, _mqttPort);
    _client.setCallback(callback);

    _sendMetricTopic = "v1/devices/";
    _sendMetricTopic += _clientId;
    _sendMetricTopic += "/metrics";

    _sendAttributeTopic = "v1/devices/";
    _sendAttributeTopic += _clientId;
    _sendAttributeTopic += "/attributes";

    _instance = this;
}

MqttClient::MqttClient(const char *wifiSSID, const char *wifiPassword, const char *mqttServer, int mqttPort, const char *clientId, const char *username, const char *password)
    : _ssid(wifiSSID), _password(wifiPassword), _mqttServer(mqttServer), _mqttPort(mqttPort), _clientId(clientId), _username(username), _passwordMqtt(password), _client(_espClient)
{
    _client.setServer(_mqttServer, _mqttPort);
    _client.setCallback(callback);

    _sendMetricTopic = "v1/devices/";
    _sendMetricTopic += _clientId;
    _sendMetricTopic += "/metrics";

    _sendAttributeTopic = "v1/devices/";
    _sendAttributeTopic += _clientId;
    _sendAttributeTopic += "/attributes";

    _instance = this;
}

MqttClient::~MqttClient()
{
    _client.disconnect();
    ESP_LOGI("MqttClient", "Disconnected");

}

void MqttClient::init(const char *wifiSSID, const char *wifiPassword, const char *mqttServer, int mqttPort, const char *clientId, const char *username, const char *password)
{
    _ssid = wifiSSID;
    _password = wifiPassword;
    _mqttServer = mqttServer;
    _mqttPort = mqttPort;
    _clientId = clientId;
    _username = username;
    _passwordMqtt = password;

    _client.setServer(_mqttServer, _mqttPort);
    _client.setCallback(callback);

    _sendMetricTopic = "v1/devices/";
    _sendMetricTopic += _clientId;
    _sendMetricTopic += "/metrics";

    _sendAttributeTopic = "v1/devices/";
    _sendAttributeTopic += _clientId;
    _sendAttributeTopic += "/attributes";

    _instance = this;
}

void MqttClient::begin()
{
    // Serial.begin(115200);
    initWiFi();
    _is_stopped = false;
    xTaskCreatePinnedToCore(
        [](void *pvParameters)
        {
            MqttClient *peClient = static_cast<MqttClient *>(pvParameters);
            for (;;)
            {
                if (!peClient->_is_stopped)
                {
                    peClient->loop();
                }          
                vTaskDelay(10);
            }
        },
        "MqttClientTask",
        10000,
        this,
        1,
        &mqttTaskHandle,
        1 // Chạy trên core 1
    );
}

void MqttClient::loop()
{

    if (!_client.connected())
    {
        reconnect();
    }
    _client.loop();
    if (!metricQueue.empty())
    {
        Metric metric = metricQueue.front();
        sendMetric(metric.ts, metric.name.c_str(), metric.value);
        metricQueue.pop();
    }
}

void MqttClient::stop()
{
    _client.disconnect();
    _is_stopped = true;
    ESP_LOGI("MqttClient", "Disconnected");
        // Xóa task để giải phóng tài nguyên
    // if (mqttTaskHandle != NULL)
    // {
    //     vTaskDelete(mqttTaskHandle); // Xóa task bằng handle
    //     mqttTaskHandle = NULL;       // Đặt handle về NULL để tránh xóa lại
    // }
    WiFi.disconnect();
    ESP_LOGI("MqttClient", "WiFi disconnected");

}

boolean MqttClient::connected()
{
    return _client.connected();
}

void MqttClient::initWiFi()
{
    WiFi.mode(WIFI_STA);
    WiFi.begin(_ssid, _password);
    ESP_LOGI("MqttClient", "Connecting to the WiFi network");
    while (WiFi.status() != WL_CONNECTED)
    {
        Serial.print('.');
        delay(1000);
        ESP_LOGI("MqttClient", "WiFi status: %d", WiFi.status());
        if(WiFi.status() == WL_DISCONNECTED || WiFi.status() == WL_CONNECTION_LOST || WiFi.status() == WL_CONNECT_FAILED) {
            ESP_LOGE("MqttClient", "WiFi disconnected");
            ESP.restart();
        }
    }
    ESP_LOGI("MqttClient", "Connected to the WiFi network");
    ESP_LOGI("MqttClient", "IP address: %s", WiFi.localIP().toString().c_str());
}

void MqttClient::reconnect()
{
    while (!_client.connected())
    {
        if (WiFi.status() != WL_CONNECTED)
        {
            initWiFi();
        }
        ESP_LOGI("MqttClient", "Attempting MQTT connection...");
        if (_client.connect(_clientId, _username, _passwordMqtt))
        {
            ESP_LOGI("MqttClient", "connected");
            String topic = "v1/devices/";
            topic += _clientId;
            topic += "/attributes/set";
            _client.subscribe(topic.c_str());
        }
        else
        {
            ESP_LOGE("MqttClient", "failed, rc=%d try again in 5 seconds", _client.state());
            delay(5000);
        }
    }
}

void MqttClient::callback(char *topic, byte *message, unsigned int length)
{
    String messageTemp;
    for (int i = 0; i < length; i++)
    {
        messageTemp += (char)message[i];
    }

    ESP_LOGD("MqttClient", "Message arrived on topic: %s. Message: %s", topic, messageTemp.c_str());

    // Parse the JSON message
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, messageTemp);
    if (error)
    {
        ESP_LOGE("MqttClient", "deserializeJson() failed: %s", error.c_str());
        return;
    }

    JsonObject obj = doc.as<JsonObject>();
    for (JsonPair kv : obj)
    {
        String key = kv.key().c_str();
        String value = kv.value().as<String>();

        if (_instance->_callbacks.find(key) != _instance->_callbacks.end())
        {
            _instance->_callbacks[key](value);
        }
    }
}

void MqttClient::sendMetric(uint64_t timestamp, const char *key, double value)
{
    if (!_client.connected())
    {
        return;
    }
    JsonDocument doc;
    doc["ts"] = timestamp;

    JsonObject metrics = doc["metrics"].to<JsonObject>();
    metrics[key] = value;

    char buffer[256];
    serializeJson(doc, buffer);

    _client.publish(_sendMetricTopic.c_str(), buffer);
    ESP_LOGI("MqttClient", "Send metric: %s", buffer);
}

void MqttClient::sendMetric(uint64_t timestamp, const char *key, JsonObject value)
{
    // ESP_LOGI("MqttClient", "Send metric: %s", value);
    if (!_client.connected())
    {
        return;
    }
    JsonDocument doc;
    doc["ts"] = timestamp;

    JsonObject metrics = doc["metrics"].to<JsonObject>();
    metrics[key] = value;

    char buffer[256];
    serializeJson(doc, buffer);

    _client.publish(_sendMetricTopic.c_str(), buffer);
    ESP_LOGI("MqttClient", "Send metric: %s", buffer);
}

void MqttClient::sendMetric(const char *key, double value)
{
    if (!_client.connected())
    {
        return;
    }
    JsonDocument doc;

    JsonObject metrics = doc["metrics"].to<JsonObject>();
    metrics[key] = value;

    char buffer[256];
    serializeJson(doc, buffer);

    _client.publish(_sendMetricTopic.c_str(), buffer);
}

void MqttClient::sendAttribute(const char *key, double value)
{
    if (!_client.connected())
    {
        return;
    }
    JsonDocument doc;

    JsonObject attributes = doc["attributes"].to<JsonObject>();
    attributes[key] = value;

    char buffer[256];
    serializeJson(doc, buffer);

    _client.publish(_sendAttributeTopic.c_str(), buffer);
}

void MqttClient::sendAttribute(const char *key, const char *value)
{
    if (!_client.connected())
    {
        return;
    }
    JsonDocument doc;

    JsonObject attributes = doc["attributes"].to<JsonObject>();
    attributes[key] = value;

    char buffer[256];
    serializeJson(doc, buffer);

    _client.publish(_sendAttributeTopic.c_str(), buffer);
}

void MqttClient::on(const char *key, void (*callback)(String))
{
    _callbacks[key] = std::bind(callback, std::placeholders::_1);
}