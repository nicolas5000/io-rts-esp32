#include "web_server.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_littlefs.h"
#include "cJSON.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <format>
#include <list>

#include "DeviceStorage.hpp"

#include "IoRtsManager.hpp"
#include "iohome_device.hpp"
#include "MqttConfig.hpp"

static const char *TAG = "web_server";

#if CONFIG_WEB_ENABLED

#define WEB_BASE_PATH   "/web"
#define FILE_BUF_SIZE   4096
#define BODY_MAX_LEN    2048
#define UPLOAD_MAX_LEN  32768
#define WS_MAX_CLIENTS  4

static IoRts::IoRtsManager *s_manager = nullptr;
static httpd_handle_t       s_server  = nullptr;
static int s_ws_fds[WS_MAX_CLIENTS];

// ─── WebSocket ──────────────────────────────────────────────────────────────

static void ws_send_str(int fd, const char *str)
{
    httpd_ws_frame_t frame = {};
    frame.type    = HTTPD_WS_TYPE_TEXT;
    frame.payload = (uint8_t *)str;
    frame.len     = strlen(str);
    esp_err_t err = httpd_ws_send_frame_async(s_server, fd, &frame);
    if (err != ESP_OK) {
        for (int i = 0; i < WS_MAX_CLIENTS; i++)
            if (s_ws_fds[i] == fd) { s_ws_fds[i] = -1; break; }
    }
}

static void ws_add_client(int fd)
{
    for (int i = 0; i < WS_MAX_CLIENTS; i++)
        if (s_ws_fds[i] == fd) return;
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        if (s_ws_fds[i] == -1) {
            s_ws_fds[i] = fd;
            ws_send_str(fd, "{\"type\":\"init\"}");
            ESP_LOGI(TAG, "WS client connected fd=%d", fd);
            break;
        }
    }
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ws_add_client(httpd_req_to_sockfd(req));
        return ESP_OK;
    }
    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_TEXT;
    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
    if (err != ESP_OK) {
        int fd = httpd_req_to_sockfd(req);
        for (int i = 0; i < WS_MAX_CLIENTS; i++)
            if (s_ws_fds[i] == fd) { s_ws_fds[i] = -1; break; }
        return err;
    }
    if (frame.len > 0) {
        uint8_t *buf = (uint8_t *)malloc(frame.len + 1);
        if (buf) { httpd_ws_recv_frame(req, &frame, frame.len); free(buf); }
    }
    return ESP_OK;
}

void web_server_broadcast_position(const char *device_id, int position, bool is_stopped)
{
    if (!s_server) return;
    char buf[128];
    snprintf(buf, sizeof(buf),
        "{\"type\":\"position\",\"id\":\"%s\",\"position\":%d,\"stopped\":%s}",
        device_id, position, is_stopped ? "true" : "false");
    for (int i = 0; i < WS_MAX_CLIENTS; i++)
        if (s_ws_fds[i] != -1) ws_send_str(s_ws_fds[i], buf);
}

void web_server_broadcast_log(const char *message)
{
    if (!s_server) return;
    // Escape the message minimally (replace " with ')
    char buf[256];
    int j = 0;
    buf[j++] = '{'; buf[j++] = '"'; buf[j++] = 't'; buf[j++] = 'y'; buf[j++] = 'p'; buf[j++] = 'e'; buf[j++] = '"'; buf[j++] = ':'; buf[j++] = '"'; buf[j++] = 'l'; buf[j++] = 'o'; buf[j++] = 'g'; buf[j++] = '"'; buf[j++] = ','; buf[j++] = '"'; buf[j++] = 'm'; buf[j++] = 's'; buf[j++] = 'g'; buf[j++] = '"'; buf[j++] = ':'; buf[j++] = '"';
    for (int i = 0; message[i] && j < (int)sizeof(buf) - 3; i++) {
        char c = message[i];
        if (c == '"') c = '\'';
        buf[j++] = c;
    }
    buf[j++] = '"'; buf[j++] = '}'; buf[j] = '\0';
    for (int i = 0; i < WS_MAX_CLIENTS; i++)
        if (s_ws_fds[i] != -1) ws_send_str(s_ws_fds[i], buf);
}

// ─── Helpers ────────────────────────────────────────────────────────────────

static const char *content_type_for(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    if (strcmp(ext, ".html") == 0) return "text/html";
    if (strcmp(ext, ".css")  == 0) return "text/css";
    if (strcmp(ext, ".js")   == 0) return "application/javascript";
    if (strcmp(ext, ".json") == 0) return "application/json";
    if (strcmp(ext, ".svg")  == 0) return "image/svg+xml";
    if (strcmp(ext, ".png")  == 0) return "image/png";
    if (strcmp(ext, ".ico")  == 0) return "image/x-icon";
    return "application/octet-stream";
}

static esp_err_t read_body(httpd_req_t *req, char **out)
{
    size_t len = req->content_len;
    if (len == 0 || len > BODY_MAX_LEN) return ESP_FAIL;
    char *buf = (char *)malloc(len + 1);
    if (!buf) return ESP_ERR_NO_MEM;
    int n = httpd_req_recv(req, buf, len);
    if (n <= 0) { free(buf); return ESP_FAIL; }
    buf[n] = '\0';
    *out = buf;
    return ESP_OK;
}

static void send_json(httpd_req_t *req, cJSON *root)
{
    char *str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, str ? str : "{}");
    free(str);
}

static void send_result(httpd_req_t *req, bool success, const char *message)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddBoolToObject(obj, "success", success);
    cJSON_AddStringToObject(obj, "message", message);
    send_json(req, obj);
}

// ─── Static file handler ────────────────────────────────────────────────────

static esp_err_t static_file_handler(httpd_req_t *req)
{
    char filepath[600];
    const char *uri = req->uri;
    if (strcmp(uri, "/") == 0) uri = "/index.html";

    if (strlen(WEB_BASE_PATH) + strlen(uri) >= sizeof(filepath)) {
        httpd_resp_send_err(req, HTTPD_414_URI_TOO_LONG, "URI too long");
        return ESP_FAIL;
    }
    snprintf(filepath, sizeof(filepath), "%s%s", WEB_BASE_PATH, uri);

    struct stat st;
    if (stat(filepath, &st) != 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_FAIL;
    }

    FILE *f = fopen(filepath, "r");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Cannot open file");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, content_type_for(filepath));
    char *buf = (char *)malloc(FILE_BUF_SIZE);
    if (!buf) { fclose(f); return ESP_FAIL; }

    size_t n;
    while ((n = fread(buf, 1, FILE_BUF_SIZE, f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, n) != ESP_OK) break;
    }
    fclose(f);
    free(buf);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

// ─── GET /api/devices ───────────────────────────────────────────────────────

static esp_err_t api_devices_get(httpd_req_t *req)
{
    cJSON *arr = cJSON_CreateArray();

    s_manager->mIoDevicesMutex.lock();
    for (const auto &[id, dev] : s_manager->mIoDevices) {
        if (dev.is_deleted) continue;

        cJSON *obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "id", id.c_str());
        cJSON_AddStringToObject(obj, "name", dev.info.name);

        int pos  = (dev.position == iohome::UNKNOWN_POSITION) ? -1 : (int)dev.position;
        int tilt = (dev.tilt     == iohome::UNKNOWN_POSITION) ? -1 : (int)dev.tilt;
        cJSON_AddNumberToObject(obj, "position", pos);
        cJSON_AddNumberToObject(obj, "tilt", tilt);
        cJSON_AddNumberToObject(obj, "type", (int)dev.info.device_type);
        cJSON_AddStringToObject(obj, "type_name",
            iohome::IoDeviceType(dev.info.device_type).c_str());
        cJSON_AddNumberToObject(obj, "subtype", dev.info.device_subtype);
        cJSON_AddStringToObject(obj, "manufacturer",
            iohome::IoDeviceManufacturer(dev.info.manufacturer).c_str());
        cJSON_AddBoolToObject(obj, "is_low_power",  dev.info.is_low_power);
        cJSON_AddBoolToObject(obj, "is_stopped",    dev.is_stopped);
        cJSON_AddBoolToObject(obj, "tilt_supported",
            iohome::deviceTypeSupportsTilt(dev.info.device_type));
        cJSON_AddItemToArray(arr, obj);
    }
    s_manager->mIoDevicesMutex.unlock();

    send_json(req, arr);
    return ESP_OK;
}

// ─── GET /api/remotes ───────────────────────────────────────────────────────

static esp_err_t api_remotes_get(httpd_req_t *req)
{
    std::map<std::string, Helpers::StoredIoDevice> storedDevices;
    Helpers::DeviceStorage::LoadAllIoDevices(storedDevices);

    // Build remote → [deviceIDs] map from device storage
    std::map<std::string, std::list<std::string>> remoteToDevices;
    for (const auto &[deviceID, storedDevice] : storedDevices)
        for (const std::string &remoteID : storedDevice.linked_remotes)
            remoteToDevices[remoteID].push_back(deviceID);

    cJSON *arr = cJSON_CreateArray();
    for (const auto &[remoteID, devices] : remoteToDevices) {
        cJSON *obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "id",   remoteID.c_str());
        cJSON_AddStringToObject(obj, "name", remoteID.c_str());
        cJSON *deviceArr = cJSON_AddArrayToObject(obj, "devices");
        for (const std::string &deviceID : devices)
            cJSON_AddItemToArray(deviceArr, cJSON_CreateString(deviceID.c_str()));
        cJSON_AddItemToArray(arr, obj);
    }
    send_json(req, arr);
    return ESP_OK;
}

// ─── POST /api/action ───────────────────────────────────────────────────────

static esp_err_t api_action_post(httpd_req_t *req)
{
    char *body = nullptr;
    if (read_body(req, &body) != ESP_OK) {
        send_result(req, false, "Failed to read body");
        return ESP_OK;
    }

    cJSON *json = cJSON_Parse(body);
    free(body);
    if (!json) { send_result(req, false, "Invalid JSON"); return ESP_OK; }

    cJSON *jDeviceId = cJSON_GetObjectItem(json, "deviceId");
    cJSON *jAction   = cJSON_GetObjectItem(json, "action");
    cJSON *jValue    = cJSON_GetObjectItem(json, "value");
    cJSON *jRemoteId = cJSON_GetObjectItem(json, "remoteId");

    const char *action   = cJSON_IsString(jAction)   ? jAction->valuestring   : "";
    const char *deviceId = cJSON_IsString(jDeviceId) ? jDeviceId->valuestring : "";
    int value = cJSON_IsNumber(jValue) ? (int)jValue->valuedouble : -1;

    bool ok = false;

    if (strcmp(action, "open") == 0) {
        ok = s_manager->mIoHome->OpenDevice(deviceId);
    } else if (strcmp(action, "close") == 0) {
        ok = s_manager->mIoHome->CloseDevice(deviceId);
    } else if (strcmp(action, "stop") == 0) {
        ok = s_manager->mIoHome->StopDevice(deviceId);
    } else if (strcmp(action, "position") == 0 && value >= 0 && value <= 100) {
        ok = s_manager->mIoHome->SetDevicePosition(deviceId, (uint8_t)value);
    } else if (strcmp(action, "tilt") == 0 && value >= 0 && value <= 100) {
        ok = s_manager->mIoHome->SetDeviceTilt(deviceId, (uint8_t)value);
    } else if (strcmp(action, "identify") == 0) {
        ok = s_manager->mIoHome->IdentifyDevice(deviceId);
    } else if (strcmp(action, "rename") == 0) {
        cJSON *jName = cJSON_GetObjectItem(json, "value");
        const char *newName = cJSON_IsString(jName) ? jName->valuestring : "";
        if (strlen(newName) > 0) {
            ok = s_manager->mIoHome->SetDeviceName(deviceId, newName);
        } else {
            send_result(req, false, "Empty name");
            cJSON_Delete(json);
            return ESP_OK;
        }
    } else if (strcmp(action, "linkRemote") == 0) {
        const char *remoteId = cJSON_IsString(jRemoteId) ? jRemoteId->valuestring : "";
        ok = s_manager->LinkRemoteToDevice(remoteId, deviceId);
    } else if (strcmp(action, "unlinkRemote") == 0) {
        const char *remoteId = cJSON_IsString(jRemoteId) ? jRemoteId->valuestring : "";
        s_manager->RemoveIoRemote(remoteId);
        ok = true;
    } else if (strcmp(action, "deleteRemote") == 0) {
        const char *remoteId = cJSON_IsString(jRemoteId) ? jRemoteId->valuestring : "";
        if (strlen(remoteId) > 0) { s_manager->RemoveIoRemote(remoteId); ok = true; }
    } else if (strcmp(action, "addRemote") == 0) {
        ok = true; // name-only registration; link separately via linkRemote
    } else {
        cJSON_Delete(json);
        send_result(req, false, "Unknown action");
        return ESP_OK;
    }

    cJSON_Delete(json);
    send_result(req, ok, ok ? "OK" : "Action failed");
    return ESP_OK;
}

// ─── GET /api/mqtt ──────────────────────────────────────────────────────────

static esp_err_t api_mqtt_get(httpd_req_t *req)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "user",      Config::MqttConfig::GetClientUsername().c_str());
    cJSON_AddStringToObject(obj, "server",    Config::MqttConfig::GetBrokerAddress().c_str());
    cJSON_AddNumberToObject(obj, "port",      Config::MqttConfig::GetBrokerPort());
    cJSON_AddStringToObject(obj, "password",  Config::MqttConfig::GetClientPassword().c_str());
    cJSON_AddStringToObject(obj, "discovery", Config::MqttConfig::GetDiscoveryPrefix().c_str());
    send_json(req, obj);
    return ESP_OK;
}

// ─── POST /api/mqtt ─────────────────────────────────────────────────────────

static esp_err_t api_mqtt_post(httpd_req_t *req)
{
    char *body = nullptr;
    if (read_body(req, &body) != ESP_OK) {
        send_result(req, false, "Failed to read body");
        return ESP_OK;
    }

    cJSON *json = cJSON_Parse(body);
    free(body);
    if (!json) { send_result(req, false, "Invalid JSON"); return ESP_OK; }

    auto get_str = [&](const char *key) -> std::string {
        cJSON *item = cJSON_GetObjectItem(json, key);
        return cJSON_IsString(item) ? std::string(item->valuestring) : "";
    };

    std::string user      = get_str("user");
    std::string server    = get_str("server");
    std::string password  = get_str("password");
    std::string discovery = get_str("discovery");
    cJSON *jPort = cJSON_GetObjectItem(json, "port");

    if (!user.empty())      Config::MqttConfig::SetClientUsername(user);
    if (!server.empty())    Config::MqttConfig::SetBrokerAddress(server);
    if (!password.empty())  Config::MqttConfig::SetClientPassword(password);
    if (!discovery.empty()) Config::MqttConfig::SetDiscoveryPrefix(discovery);
    if (cJSON_IsNumber(jPort))
        Config::MqttConfig::SetBrokerPort((uint16_t)jPort->valuedouble);
    else if (cJSON_IsString(jPort)) {
        int p = atoi(jPort->valuestring);
        if (p > 0) Config::MqttConfig::SetBrokerPort((uint16_t)p);
    }

    cJSON_Delete(json);
    send_result(req, true, "MQTT config saved. Reboot to apply.");
    return ESP_OK;
}

// ─── Download / Upload helpers ──────────────────────────────────────────────

static esp_err_t read_multipart_content(httpd_req_t *req, char **out)
{
    size_t body_len = req->content_len;
    if (body_len == 0 || body_len > UPLOAD_MAX_LEN) return ESP_FAIL;

    char *body = (char *)malloc(body_len + 1);
    if (!body) return ESP_ERR_NO_MEM;

    int n = httpd_req_recv(req, body, body_len);
    if (n <= 0) { free(body); return ESP_FAIL; }
    body[n] = '\0';

    // Find end of part headers (\r\n\r\n)
    const char *content_start = strstr(body, "\r\n\r\n");
    if (!content_start) { free(body); return ESP_FAIL; }
    content_start += 4;

    // Content ends before the closing boundary marker (\r\n--)
    const char *content_end = strstr(content_start, "\r\n--");
    size_t content_len = content_end ? (size_t)(content_end - content_start) : strlen(content_start);

    char *result = (char *)malloc(content_len + 1);
    if (!result) { free(body); return ESP_ERR_NO_MEM; }
    memcpy(result, content_start, content_len);
    result[content_len] = '\0';

    free(body);
    *out = result;
    return ESP_OK;
}

// Helper: serialize a StoredIoDevice to a cJSON object (same fields as DeviceStorage)
static cJSON *stored_device_to_json(const std::string &deviceID, const Helpers::StoredIoDevice &sd)
{
    const iohome::IoDevice &dev = sd.device;
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "id", deviceID.c_str());
    cJSON_AddStringToObject(obj, "name", dev.info.name);

    std::string nodeIdHex;
    for (int i = 0; i < iohome::NODE_ID_SIZE; i++)
        nodeIdHex += std::format("{:02X}", dev.info.node_id[i]);
    cJSON_AddStringToObject(obj, "node_id", nodeIdHex.c_str());

    cJSON_AddNumberToObject(obj, "device_type",    (uint8_t)dev.info.device_type);
    cJSON_AddNumberToObject(obj, "device_subtype", dev.info.device_subtype);
    cJSON_AddNumberToObject(obj, "manufacturer",   (uint8_t)dev.info.manufacturer);
    cJSON_AddStringToObject(obj, "info1", dev.info.info1);
    cJSON_AddStringToObject(obj, "info2", dev.info.info2);
    cJSON_AddBoolToObject(obj,   "open_close_inverted", dev.info.is_openclose_inverted);
    cJSON_AddBoolToObject(obj,   "is_low_power",        dev.info.is_low_power);

    cJSON *remotes = cJSON_AddArrayToObject(obj, "remotes");
    for (const std::string &r : sd.linked_remotes)
        cJSON_AddItemToArray(remotes, cJSON_CreateString(r.c_str()));
    return obj;
}

// Helper: populate a StoredIoDevice from a cJSON object (same fields as DeviceStorage)
static bool json_to_stored_device(cJSON *item, std::string &deviceID, Helpers::StoredIoDevice &sd)
{
    cJSON *idItem = cJSON_GetObjectItem(item, "id");
    if (!cJSON_IsString(idItem)) return false;
    deviceID = idItem->valuestring;

    iohome::IoDevice &dev = sd.device;
    memset(&dev, 0, sizeof(dev));
    dev.position = iohome::UNKNOWN_POSITION;
    dev.target   = iohome::UNKNOWN_POSITION;
    dev.tilt     = iohome::UNKNOWN_POSITION;
    dev.is_stopped = true;

    cJSON *nameItem = cJSON_GetObjectItem(item, "name");
    if (cJSON_IsString(nameItem))
        strncpy(dev.info.name, nameItem->valuestring, iohome::CMD_PARAM_NAME_MAXSIZE - 1);

    cJSON *nodeIdItem = cJSON_GetObjectItem(item, "node_id");
    if (cJSON_IsString(nodeIdItem)) {
        std::string hex(nodeIdItem->valuestring);
        for (int i = 0; i < iohome::NODE_ID_SIZE && (i * 2 + 1) < (int)hex.length(); i++)
            dev.info.node_id[i] = (uint8_t)strtol(hex.substr(i * 2, 2).c_str(), nullptr, 16);
    }

    cJSON *typeItem = cJSON_GetObjectItem(item, "device_type");
    if (cJSON_IsNumber(typeItem))
        dev.info.device_type = (iohome::DeviceType)(uint8_t)typeItem->valuedouble;

    cJSON *subtypeItem = cJSON_GetObjectItem(item, "device_subtype");
    if (cJSON_IsNumber(subtypeItem))
        dev.info.device_subtype = (uint8_t)subtypeItem->valuedouble;

    cJSON *mfItem = cJSON_GetObjectItem(item, "manufacturer");
    if (cJSON_IsNumber(mfItem))
        dev.info.manufacturer = (iohome::Manufacturer)(uint8_t)mfItem->valuedouble;

    cJSON *info1Item = cJSON_GetObjectItem(item, "info1");
    if (cJSON_IsString(info1Item))
        strncpy(dev.info.info1, info1Item->valuestring, iohome::CMD_PARAM_INFO1_MAXSIZE - 1);

    cJSON *info2Item = cJSON_GetObjectItem(item, "info2");
    if (cJSON_IsString(info2Item))
        strncpy(dev.info.info2, info2Item->valuestring, iohome::CMD_PARAM_INFO2_MAXSIZE - 1);

    cJSON *invertItem = cJSON_GetObjectItem(item, "open_close_inverted");
    if (cJSON_IsBool(invertItem))
        dev.info.is_openclose_inverted = cJSON_IsTrue(invertItem);

    cJSON *lpItem = cJSON_GetObjectItem(item, "is_low_power");
    dev.info.is_low_power = cJSON_IsBool(lpItem) ? cJSON_IsTrue(lpItem) : true;

    sd.linked_remotes.clear();
    cJSON *remotesArr = cJSON_GetObjectItem(item, "remotes");
    if (cJSON_IsArray(remotesArr)) {
        cJSON *r = nullptr;
        cJSON_ArrayForEach(r, remotesArr)
            if (cJSON_IsString(r))
                sd.linked_remotes.push_back(r->valuestring);
    }
    return true;
}

// ─── GET /api/download/devices ──────────────────────────────────────────────

static esp_err_t api_download_devices(httpd_req_t *req)
{
    std::map<std::string, Helpers::StoredIoDevice> storedDevices;
    Helpers::DeviceStorage::LoadAllIoDevices(storedDevices);

    cJSON *arr = cJSON_CreateArray();
    for (const auto &[deviceID, sd] : storedDevices)
        cJSON_AddItemToArray(arr, stored_device_to_json(deviceID, sd));

    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"devices.json\"");
    send_json(req, arr);
    return ESP_OK;
}

// ─── GET /api/download/remotes ──────────────────────────────────────────────

static esp_err_t api_download_remotes(httpd_req_t *req)
{
    std::map<std::string, Helpers::StoredIoDevice> storedDevices;
    Helpers::DeviceStorage::LoadAllIoDevices(storedDevices);

    cJSON *obj = cJSON_CreateObject();
    for (const auto &[deviceID, sd] : storedDevices) {
        for (const std::string &remoteID : sd.linked_remotes) {
            cJSON *arr = cJSON_GetObjectItem(obj, remoteID.c_str());
            if (!arr) { arr = cJSON_CreateArray(); cJSON_AddItemToObject(obj, remoteID.c_str(), arr); }
            cJSON_AddItemToArray(arr, cJSON_CreateString(deviceID.c_str()));
        }
    }

    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"RemoteMap.json\"");
    send_json(req, obj);
    return ESP_OK;
}

// ─── POST /api/upload/devices ───────────────────────────────────────────────

static esp_err_t api_upload_devices(httpd_req_t *req)
{
    char *content = nullptr;
    if (read_multipart_content(req, &content) != ESP_OK) {
        send_result(req, false, "Failed to read upload (too large or bad format?)");
        return ESP_OK;
    }

    cJSON *arr = cJSON_Parse(content);
    free(content);
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(arr);
        send_result(req, false, "Invalid JSON: expected array");
        return ESP_OK;
    }

    int count = 0;
    cJSON *item = nullptr;
    cJSON_ArrayForEach(item, arr) {
        std::string deviceID;
        Helpers::StoredIoDevice sd = {};
        if (!json_to_stored_device(item, deviceID, sd)) continue;

        Helpers::DeviceStorage::SaveIoDevice(deviceID, sd);
        s_manager->mIoHome->RestoreDevice(deviceID, sd.device);

        s_manager->mIoDevicesMutex.lock();
        auto it = s_manager->mIoDevices.find(deviceID);
        if (it != s_manager->mIoDevices.end()) it->second = sd.device;
        else s_manager->mIoDevices.insert({deviceID, sd.device});
        s_manager->mIoDevicesMutex.unlock();

        for (const std::string &remoteID : sd.linked_remotes)
            s_manager->mIoHome->LinkRemoteToDevice(remoteID, deviceID);

        count++;
    }
    cJSON_Delete(arr);

    char msg[64];
    snprintf(msg, sizeof(msg), "Restored %d device(s). Reboot to fully apply.", count);
    send_result(req, true, msg);
    return ESP_OK;
}

// ─── POST /api/upload/remotes ───────────────────────────────────────────────

static esp_err_t api_upload_remotes(httpd_req_t *req)
{
    char *content = nullptr;
    if (read_multipart_content(req, &content) != ESP_OK) {
        send_result(req, false, "Failed to read upload (too large or bad format?)");
        return ESP_OK;
    }

    cJSON *obj = cJSON_Parse(content);
    free(content);
    if (!cJSON_IsObject(obj)) {
        cJSON_Delete(obj);
        send_result(req, false, "Invalid JSON: expected object {remoteId: [deviceIds]}");
        return ESP_OK;
    }

    int count = 0;
    for (cJSON *child = obj->child; child; child = child->next) {
        const char *remoteID = child->string;
        if (!remoteID || !cJSON_IsArray(child)) continue;
        cJSON *deviceItem = nullptr;
        cJSON_ArrayForEach(deviceItem, child) {
            if (cJSON_IsString(deviceItem)) {
                s_manager->LinkRemoteToDevice(remoteID, deviceItem->valuestring);
                count++;
            }
        }
    }
    cJSON_Delete(obj);

    char msg[64];
    snprintf(msg, sizeof(msg), "Restored %d remote link(s).", count);
    send_result(req, true, msg);
    return ESP_OK;
}

// ─── Server startup ─────────────────────────────────────────────────────────

void web_server_start(void *ioRtsManager)
{
    s_manager = static_cast<IoRts::IoRtsManager *>(ioRtsManager);
    for (int i = 0; i < WS_MAX_CLIENTS; i++) s_ws_fds[i] = -1;

    // Mount web LittleFS partition
    esp_vfs_littlefs_conf_t conf = {};
    conf.base_path = WEB_BASE_PATH;
    conf.partition_label = "web";
    conf.format_if_mount_failed = true;
    conf.dont_mount = false;

    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount web partition: %s", esp_err_to_name(err));
        return;
    }

    // Start HTTP server
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.max_uri_handlers = 16;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.enable_so_linger = false;

    httpd_handle_t server = NULL;
    err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        return;
    }
    s_server = server;

    // Helper lambda to register a plain HTTP route
    auto reg = [&](const char *uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t *)) {
        httpd_uri_t r = {};
        r.uri = uri; r.method = method; r.handler = handler;
        httpd_register_uri_handler(server, &r);
    };

    // WebSocket endpoint
    {
        httpd_uri_t r = {};
        r.uri = "/ws"; r.method = HTTP_GET; r.handler = ws_handler;
#if CONFIG_HTTPD_WS_SUPPORT
        r.is_websocket = true;
#endif
        httpd_register_uri_handler(server, &r);
    }

    // API routes (before wildcard)
    reg("/api/devices",           HTTP_GET,  api_devices_get);
    reg("/api/remotes",           HTTP_GET,  api_remotes_get);
    reg("/api/action",            HTTP_POST, api_action_post);
    reg("/api/mqtt",              HTTP_GET,  api_mqtt_get);
    reg("/api/mqtt",              HTTP_POST, api_mqtt_post);
    reg("/api/download/devices",  HTTP_GET,  api_download_devices);
    reg("/api/download/remotes",  HTTP_GET,  api_download_remotes);
    reg("/api/upload/devices",    HTTP_POST, api_upload_devices);
    reg("/api/upload/remotes",    HTTP_POST, api_upload_remotes);

    // Wildcard catch-all for static files
    reg("/*", HTTP_GET, static_file_handler);

    ESP_LOGI(TAG, "HTTP server started");
}

#endif // CONFIG_WEB_ENABLED
