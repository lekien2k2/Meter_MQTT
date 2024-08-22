#include "ApModeServer.h"

ApModeServer::ApModeServer(ApModeServerConfig config)
    : _config(config), _server(80)
{
    _is_connected = false;
    _is_ap_mode = false;
}

void ApModeServer::begin()
{
    _server.on("/", HTTP_GET, std::bind(&ApModeServer::handleRoot, this, std::placeholders::_1));
    _server.on("/get_server_config", HTTP_GET, std::bind(&ApModeServer::handleGetSeverConfig, this, std::placeholders::_1));
    _server.on("/set_server_config", HTTP_POST, std::bind(&ApModeServer::handleSetServerConfig, this, std::placeholders::_1));
    _server.onNotFound(std::bind(&ApModeServer::handleNotFound, this, std::placeholders::_1));
    _server.begin();
}


void ApModeServer::handleRoot(AsyncWebServerRequest *request)
{
    handleGetSeverConfig(request);
}

void ApModeServer::handleGetSeverConfig(AsyncWebServerRequest *request)
{
    String responseHTML = String(index_html);
    responseHTML.replace("{{ssid}}", _config.ssid);
    responseHTML.replace("{{password}}", _config.password);
    responseHTML.replace("{{ap_ssid}}", _config.ap_ssid);
    responseHTML.replace("{{ap_password}}", _config.ap_password);
    responseHTML.replace("{{device_id}}", _config.device_id);
    responseHTML.replace("{{access_token}}", _config.access_token);
    responseHTML.replace("{{pool_time}}", String(_config.pool_time));

    request->send(200, "text/html", responseHTML);
}

void ApModeServer::handleSetServerConfig(AsyncWebServerRequest *request)
{
    if (request->hasParam("ssid", true) && request->hasParam("password", true) && request->hasParam("ap_ssid", true) && request->hasParam("ap_password", true) && request->hasParam("device_id", true) && request->hasParam("access_token", true) && request->hasParam("pool_time", true))
    {
        _config.ssid = request->arg("ssid").c_str();
        _config.password = request->arg("password").c_str();
        _config.ap_ssid = request->arg("ap_ssid").c_str();
        _config.ap_password = request->arg("ap_password").c_str();
        _config.device_id = request->arg("device_id").c_str();
        _config.access_token = request->arg("access_token").c_str();
        _config.pool_time = request->arg("pool_time").toInt();
        request->send(200, "text/plain", "OK");
    }
    else
    {
        request->send(400, "text/plain", "Bad Request");
    }
}

void ApModeServer::handleNotFound(AsyncWebServerRequest *request)
{
    request->send(404, "text/plain", "Not found");
}


