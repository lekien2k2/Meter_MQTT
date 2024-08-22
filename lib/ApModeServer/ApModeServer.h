/*
    * ApModeServer.h
    * Created by: LeKien, 2024. 8. 22.
    *  
*/


#ifndef AP_MODE_SERVER_H
#define AP_MODE_SERVER_H

#include <ESPAsyncWebServer.h>
#include "page.h"
#include <ArduinoJson.h>


struct ApModeServerConfig
{
    const char* ssid;
    const char* password;
    const char* ap_ssid;
    const char* ap_password;
    const char* device_id;
    const char* access_token;
    int pool_time;
};

class ApModeServer
{
    public:
        ApModeServer(ApModeServerConfig config);
        void begin();
        
        void handleRoot(AsyncWebServerRequest *request);
        void handleGetSeverConfig(AsyncWebServerRequest *request);
        void handleSetServerConfig(AsyncWebServerRequest *request);
        void handleNotFound(AsyncWebServerRequest *request);
        
    private:
        ApModeServerConfig _config;
        AsyncWebServer _server;
        bool _is_connected;
        bool _is_ap_mode;
};

#endif