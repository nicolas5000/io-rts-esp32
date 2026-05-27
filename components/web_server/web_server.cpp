#include "web_server.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_littlefs.h"
#include "esp_random.h"
#include "cJSON.h"
#include "nvs.h"
#include "esp_ota_ops.h"

#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <format>
#include <list>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "DeviceStorage.hpp"
#include "syslog.h"

#include "IoRtsManager.hpp"
#include "iohome_device.hpp"
#include "MqttConfig.hpp"
#include "SyslogConfig.hpp"

#if CONFIG_WEB_ENABLED

static const char *TAG = "web_server";

#define WEB_BASE_PATH   "/web"
#define FILE_BUF_SIZE   4096
#define BODY_MAX_LEN    2048
#define UPLOAD_MAX_LEN  32768
#define WS_MAX_CLIENTS  4

static IoRts::IoRtsManager *s_manager = nullptr;
static httpd_handle_t       s_server  = nullptr;
static int s_ws_fds[WS_MAX_CLIENTS];

// Diagnostic: track last WS upgrade fd and init frame result
static int  s_diag_last_fd       = -99;
static int  s_diag_init_err      = -99;
static int  s_diag_recv_err      = -99;  // non-GET recv error
static int  s_diag_recv_fd       = -99;
static int  s_diag_handler_calls = 0;    // incremented at top of ws_handler every call
static int  s_diag_last_method   = -99;  // req->method at top of ws_handler

// ─── Log forwarding (esp_log_set_vprintf → WebSocket) ───────────────────────

#define LOG_QUEUE_DEPTH  32
#define LOG_LINE_MAX     180

static QueueHandle_t        s_log_queue      = nullptr;
static vprintf_like_t       s_orig_vprintf   = nullptr;

static void strip_ansi(const char *src, char *dst, size_t dst_size)
{
    size_t j = 0;
    for (size_t i = 0; src[i] && j < dst_size - 1; i++) {
        if ((uint8_t)src[i] == 0x1B && src[i + 1] == '[') {
            i += 2;
            while (src[i] && src[i] != 'm') i++;
        } else {
            dst[j++] = src[i];
        }
    }
    dst[j] = '\0';
}

static int web_log_vprintf(const char *fmt, va_list args)
{
    va_list copy;
    va_copy(copy, args);
    int ret = s_orig_vprintf(fmt, args);
    if (s_log_queue) {
        bool has_clients = false;
        for (int i = 0; i < WS_MAX_CLIENTS; i++)
            if (s_ws_fds[i] != -1) { has_clients = true; break; }
        if (!has_clients && !syslog_is_active()) { va_end(copy); return ret; }
        char raw[LOG_LINE_MAX];
        vsnprintf(raw, sizeof(raw), fmt, copy);
        char line[LOG_LINE_MAX];
        strip_ansi(raw, line, sizeof(line));
        int len = (int)strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        if (len > 0)
            xQueueSend(s_log_queue, line, 0);
    }
    va_end(copy);
    return ret;
}

static void log_drain_task(void *)
{
    char line[LOG_LINE_MAX];
    while (true) {
        if (xQueueReceive(s_log_queue, line, portMAX_DELAY) == pdTRUE) {
            web_server_broadcast_log(line);
            syslog_send(line);
        }
    }
}

// ─── WebSocket ──────────────────────────────────────────────────────────────

// Job queued to the httpd task so the send always happens in the right context.
struct ws_send_job {
    int    fd;
    size_t len;
    char   buf[]; // flexible array — payload follows the struct
};

static void ws_send_job_fn(void *arg)
{
    ws_send_job *job = static_cast<ws_send_job *>(arg);
    httpd_ws_frame_t frame = {};
    frame.type    = HTTPD_WS_TYPE_TEXT;
    frame.payload = (uint8_t *)job->buf;
    frame.len     = job->len;
    esp_err_t err = httpd_ws_send_frame_async(s_server, job->fd, &frame);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ws_send fd=%d err=%s — removing client", job->fd, esp_err_to_name(err));
        for (int i = 0; i < WS_MAX_CLIENTS; i++)
            if (s_ws_fds[i] == job->fd) { s_ws_fds[i] = -1; break; }
    }
    free(job);
}

static void ws_send_str(int fd, const char *str)
{
    size_t len = strlen(str);
    ws_send_job *job = static_cast<ws_send_job *>(malloc(sizeof(ws_send_job) + len + 1));
    if (!job) return;
    job->fd  = fd;
    job->len = len;
    memcpy(job->buf, str, len + 1);
    if (httpd_queue_work(s_server, ws_send_job_fn, job) != ESP_OK) {
        free(job);
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
    s_diag_handler_calls++;
    s_diag_last_method = (int)req->method;

    int fd = httpd_req_to_sockfd(req);

    // Detect new client by absence in s_ws_fds (req->method is 0 for WS frames in
    // esp-idf, not HTTP_GET=1, so we cannot use the method to distinguish first call).
    bool is_new = true;
    for (int i = 0; i < WS_MAX_CLIENTS; i++)
        if (s_ws_fds[i] == fd) { is_new = false; break; }

    if (is_new) {
        s_diag_last_fd = fd;
        for (int i = 0; i < WS_MAX_CLIENTS; i++) {
            if (s_ws_fds[i] == -1) { s_ws_fds[i] = fd; break; }
        }
        // Drain the trigger frame (browser sends {"type":"hello"} on open)
        httpd_ws_frame_t hello = {};
        httpd_ws_recv_frame(req, &hello, 0);
        if (hello.len > 0) {
            uint8_t *buf = (uint8_t *)malloc(hello.len + 1);
            if (buf) { hello.payload = buf; httpd_ws_recv_frame(req, &hello, hello.len); free(buf); }
        }
        ESP_LOGI(TAG, "WS new client fd=%d", fd);
        ws_send_str(fd, "{\"type\":\"init\"}");
        return ESP_OK;
    }

    // Existing client — drain the incoming frame
    httpd_ws_frame_t frame = {};
    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
    if (err != ESP_OK) {
        s_diag_recv_err = (int)err;
        s_diag_recv_fd  = fd;
        ESP_LOGW(TAG, "WS recv err fd=%d: %s", fd, esp_err_to_name(err));
        for (int i = 0; i < WS_MAX_CLIENTS; i++)
            if (s_ws_fds[i] == fd) { s_ws_fds[i] = -1; break; }
        return err;
    }
    if (frame.len > 0) {
        uint8_t *buf = (uint8_t *)malloc(frame.len + 1);
        if (buf) {
            frame.payload = buf;
            httpd_ws_recv_frame(req, &frame, frame.len);
            frame.payload = nullptr;
            free(buf);
        }
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

void web_server_broadcast_device_event(const char *device_id, const char *event_type)
{
    if (!s_server) return;
    char buf[128];
    snprintf(buf, sizeof(buf), "{\"type\":\"%s\",\"id\":\"%s\"}", event_type, device_id);
    for (int i = 0; i < WS_MAX_CLIENTS; i++)
        if (s_ws_fds[i] != -1) ws_send_str(s_ws_fds[i], buf);
}

// Classify a log line as "error", "info", or "debug" for the web log filter.
// ESP log lines start with E/W/I/D/V followed by ' (timestamp) tag: message'.
static const char *log_classify(const char *line)
{
    char lvl = line[0];
    if (lvl == 'E' || lvl == 'W') return "error";
    if (lvl == 'I') {
        // ioRtsMan: device status callbacks — shown for all device events
        if (strstr(line, ") ioRtsMan:")) return "info";
        // io-hctrl: command 04 is the device's final position/status acknowledgement
        if (strstr(line, ") io-hctrl:") && strstr(line, "command 04")) return "info";
    }
    return "debug";
}

void web_server_broadcast_log(const char *message)
{
    if (!s_server) return;
    const char *level = log_classify(message);
    char escaped[200];
    int j = 0;
    for (int i = 0; message[i] && j < (int)sizeof(escaped) - 1; i++) {
        char c = message[i];
        if (c == '"') c = '\'';
        escaped[j++] = c;
    }
    escaped[j] = '\0';
    char buf[280];
    snprintf(buf, sizeof(buf), "{\"type\":\"log\",\"level\":\"%s\",\"message\":\"%s\"}", level, escaped);
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
        cJSON *obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "id", id.c_str());
        cJSON_AddStringToObject(obj, "name", dev.info.name);
        cJSON_AddBoolToObject(obj, "inactive", dev.is_deleted);

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
    } else if (strcmp(action, "deactivateDevice") == 0) {
        if (strlen(deviceId) > 0) { s_manager->DeactivateDevice(deviceId); ok = true; }
    } else if (strcmp(action, "reactivateDevice") == 0) {
        if (strlen(deviceId) > 0) { s_manager->ReactivateDevice(deviceId); ok = true; }
    } else if (strcmp(action, "deleteDevice") == 0) {
        if (strlen(deviceId) > 0) {
            ok = s_manager->DeleteDevice(deviceId);
            if (!ok) {
                cJSON_Delete(json);
                send_result(req, false, "Deactivate the device first before deleting.");
                return ESP_OK;
            }
        }
    } else {
        cJSON_Delete(json);
        send_result(req, false, "Unknown action");
        return ESP_OK;
    }

    cJSON_Delete(json);
    send_result(req, ok, ok ? "OK" : "Action failed");
    return ESP_OK;
}

// ─── GET /api/debug ─────────────────────────────────────────────────────────

static esp_err_t api_debug_get(httpd_req_t *req)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON *fds_arr = cJSON_AddArrayToObject(obj, "ws_fds");
    int active = 0;
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        cJSON_AddItemToArray(fds_arr, cJSON_CreateNumber(s_ws_fds[i]));
        if (s_ws_fds[i] != -1) active++;
    }
    cJSON_AddNumberToObject(obj, "ws_active", active);
    cJSON_AddBoolToObject(obj, "log_queue_ok", s_log_queue != nullptr);
    cJSON_AddNumberToObject(obj, "diag_last_fd",   s_diag_last_fd);
    cJSON_AddNumberToObject(obj, "diag_init_err",  s_diag_init_err);
    cJSON_AddStringToObject(obj, "diag_init_err_s", esp_err_to_name((esp_err_t)s_diag_init_err));
    cJSON_AddNumberToObject(obj, "diag_recv_err",  s_diag_recv_err);
    cJSON_AddStringToObject(obj, "diag_recv_err_s", esp_err_to_name((esp_err_t)s_diag_recv_err));
    cJSON_AddNumberToObject(obj, "diag_recv_fd",      s_diag_recv_fd);
    cJSON_AddNumberToObject(obj, "diag_handler_calls", s_diag_handler_calls);
    cJSON_AddNumberToObject(obj, "diag_last_method",   s_diag_last_method);
    // Try sending a test frame to all clients and report results
    cJSON *results = cJSON_AddArrayToObject(obj, "send_results");
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        if (s_ws_fds[i] == -1) continue;
        const char *test_msg = "{\"type\":\"debug_ping\"}";
        httpd_ws_frame_t frame = {};
        frame.type    = HTTPD_WS_TYPE_TEXT;
        frame.payload = (uint8_t *)test_msg;
        frame.len     = strlen(test_msg);
        esp_err_t err = httpd_ws_send_frame_async(s_server, s_ws_fds[i], &frame);
        cJSON *r = cJSON_CreateObject();
        cJSON_AddNumberToObject(r, "fd", s_ws_fds[i]);
        cJSON_AddStringToObject(r, "err", esp_err_to_name(err));
        cJSON_AddItemToArray(results, r);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "debug send fd=%d err=%s", s_ws_fds[i], esp_err_to_name(err));
            s_ws_fds[i] = -1;
        }
    }
    send_json(req, obj);
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

// ─── OTA key management ─────────────────────────────────────────────────────

#define OTA_KEY_LEN 32
static char s_ota_key[OTA_KEY_LEN + 1] = {};

static void ota_key_init(void)
{
    if (CONFIG_OTA_API_KEY[0] != '\0') {
        strncpy(s_ota_key, CONFIG_OTA_API_KEY, OTA_KEY_LEN);
        s_ota_key[OTA_KEY_LEN] = '\0';
        ESP_LOGI(TAG, "OTA key (menuconfig): set");
        return;
    }

    nvs_handle_t h;
    if (nvs_open("ota", NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "OTA: failed to open NVS");
        return;
    }
    size_t len = sizeof(s_ota_key);
    esp_err_t err = nvs_get_str(h, "api_key", s_ota_key, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        uint8_t rnd[16];
        for (int i = 0; i < 4; i++) {
            uint32_t r = esp_random();
            memcpy(rnd + i * 4, &r, 4);
        }
        for (int i = 0; i < 16; i++)
            snprintf(s_ota_key + i * 2, 3, "%02x", rnd[i]);
        s_ota_key[OTA_KEY_LEN] = '\0';
        nvs_set_str(h, "api_key", s_ota_key);
        nvs_commit(h);
        ESP_LOGI(TAG, "OTA key (generated): %s", s_ota_key);
    } else if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA key (loaded): %s", s_ota_key);
    } else {
        ESP_LOGE(TAG, "OTA: NVS read error: %s", esp_err_to_name(err));
    }
    nvs_close(h);
}

static bool ota_check_key(httpd_req_t *req)
{
    char key_hdr[OTA_KEY_LEN + 1] = {};
    esp_err_t err = httpd_req_get_hdr_value_str(req, "X-OTA-Key", key_hdr, sizeof(key_hdr));
    bool ok = (err == ESP_OK) && (memcmp(key_hdr, s_ota_key, OTA_KEY_LEN) == 0);
    if (!ok) {
        int fd = httpd_req_to_sockfd(req);
        struct sockaddr_in addr = {};
        socklen_t addrlen = sizeof(addr);
        getpeername(fd, (struct sockaddr *)&addr, &addrlen);
        ESP_LOGW(TAG, "OTA: unauthorized attempt from %s", inet_ntoa(addr.sin_addr));
    }
    return ok;
}

// ─── GET /api/ota/key ───────────────────────────────────────────────────────

static esp_err_t api_ota_key_get(httpd_req_t *req)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "key", s_ota_key);
    send_json(req, obj);
    return ESP_OK;
}

// ─── POST /api/ota ──────────────────────────────────────────────────────────

static esp_err_t api_ota_post(httpd_req_t *req)
{
    if (!ota_check_key(req)) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
        return ESP_OK;
    }

    if (req->content_len == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty payload");
        return ESP_OK;
    }

    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "OTA: writing to partition %s", part->label);

    esp_ota_handle_t ota_handle;
    esp_err_t err = esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        return ESP_OK;
    }

    char buf[1024];
    int total = 0;
    int remaining = (int)req->content_len;
    bool write_err = false;

    while (remaining > 0) {
        int to_recv = (remaining < (int)sizeof(buf)) ? remaining : (int)sizeof(buf);
        int n = httpd_req_recv(req, buf, to_recv);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0) {
            ESP_LOGE(TAG, "OTA receive error at byte %d", total);
            write_err = true;
            break;
        }
        err = esp_ota_write(ota_handle, buf, n);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            write_err = true;
            break;
        }
        total += n;
        remaining -= n;
    }

    if (write_err) {
        esp_ota_abort(ota_handle);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write failed");
        return ESP_OK;
    }

    err = esp_ota_end(ota_handle);
    ESP_LOGI(TAG, "esp_ota_end: %s (%d bytes written)", esp_err_to_name(err), total);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA verify failed");
        return ESP_OK;
    }

    err = esp_ota_set_boot_partition(part);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA set boot failed");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "OTA: success, rebooting...");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"rebooting\"}");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

// ─── GET /api/syslog ────────────────────────────────────────────────────────

static esp_err_t api_syslog_get(httpd_req_t *req)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddBoolToObject  (obj, "enabled",   Config::SyslogConfig::isEnabled());
    cJSON_AddStringToObject(obj, "server",    Config::SyslogConfig::GetServer().c_str());
    cJSON_AddNumberToObject(obj, "port",      Config::SyslogConfig::GetPort());
    cJSON_AddNumberToObject(obj, "facility",  Config::SyslogConfig::GetFacility());
    cJSON_AddNumberToObject(obj, "min_level", Config::SyslogConfig::GetMinLevel());
    send_json(req, obj);
    return ESP_OK;
}

// ─── POST /api/syslog ───────────────────────────────────────────────────────

static esp_err_t api_syslog_post(httpd_req_t *req)
{
    char *body = nullptr;
    if (read_body(req, &body) != ESP_OK) {
        send_result(req, false, "Failed to read body");
        return ESP_OK;
    }

    cJSON *json = cJSON_Parse(body);
    free(body);
    if (!json) { send_result(req, false, "Invalid JSON"); return ESP_OK; }

    cJSON *jEnabled  = cJSON_GetObjectItem(json, "enabled");
    cJSON *jServer   = cJSON_GetObjectItem(json, "server");
    cJSON *jPort     = cJSON_GetObjectItem(json, "port");
    cJSON *jFacility = cJSON_GetObjectItem(json, "facility");
    cJSON *jMinLevel = cJSON_GetObjectItem(json, "min_level");

    if (cJSON_IsBool(jEnabled))   Config::SyslogConfig::SetEnabled(cJSON_IsTrue(jEnabled));
    if (cJSON_IsString(jServer))  Config::SyslogConfig::SetServer(jServer->valuestring);
    if (cJSON_IsNumber(jPort))    Config::SyslogConfig::SetPort((uint16_t)jPort->valuedouble);
    if (cJSON_IsNumber(jFacility)) Config::SyslogConfig::SetFacility((uint8_t)jFacility->valuedouble);
    if (cJSON_IsNumber(jMinLevel)) Config::SyslogConfig::SetMinLevel((uint8_t)jMinLevel->valuedouble);

    cJSON_Delete(json);
    syslog_apply_config();
    send_result(req, true, "Syslog config saved");
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

// ─── GET /api/info ──────────────────────────────────────────────────────────

static esp_err_t api_info_get(httpd_req_t *req)
{
    const esp_app_desc_t *desc = esp_app_get_description();
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "version",      desc->version);
    cJSON_AddStringToObject(obj, "project",      desc->project_name);
    cJSON_AddStringToObject(obj, "compile_date", desc->date);
    cJSON_AddStringToObject(obj, "compile_time", desc->time);
    cJSON_AddStringToObject(obj, "idf_ver",      desc->idf_ver);
    send_json(req, obj);
    return ESP_OK;
}

// ─── POST /api/upload/web ───────────────────────────────────────────────────

static esp_err_t api_upload_web_post(httpd_req_t *req)
{
    if (!ota_check_key(req)) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
        return ESP_OK;
    }

    char query[256] = {};
    char rel_path[128] = {};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "path", rel_path, sizeof(rel_path)) != ESP_OK ||
        rel_path[0] != '/') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid ?path=");
        return ESP_OK;
    }
    if (strstr(rel_path, "..")) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid path");
        return ESP_OK;
    }

    char filepath[600];
    if (snprintf(filepath, sizeof(filepath), "%s%s", WEB_BASE_PATH, rel_path) >= (int)sizeof(filepath)) {
        httpd_resp_send_err(req, HTTPD_414_URI_TOO_LONG, "Path too long");
        return ESP_OK;
    }

    FILE *f = fopen(filepath, "w");
    if (!f) {
        ESP_LOGE(TAG, "web upload: cannot open %s", filepath);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Cannot open file for writing");
        return ESP_OK;
    }

    char buf[1024];
    int remaining = (int)req->content_len;
    int total = 0;
    bool write_err = false;
    while (remaining > 0) {
        int to_recv = (remaining < (int)sizeof(buf)) ? remaining : (int)sizeof(buf);
        int n = httpd_req_recv(req, buf, to_recv);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0) { write_err = true; break; }
        if (fwrite(buf, 1, n, f) != (size_t)n) { write_err = true; break; }
        total += n;
        remaining -= n;
    }
    fclose(f);

    if (write_err) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Write failed");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "web upload: %d bytes -> %s", total, filepath);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
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
    config.task_priority = tskIDLE_PRIORITY + 3; // below radio (8), IO processing (6), status updates (4)
    config.max_uri_handlers = 20;
    config.max_open_sockets = 13; // browser opens many parallel connections for static files + WS
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
    reg("/api/debug",             HTTP_GET,  api_debug_get);
    reg("/api/devices",           HTTP_GET,  api_devices_get);
    reg("/api/remotes",           HTTP_GET,  api_remotes_get);
    reg("/api/action",            HTTP_POST, api_action_post);
    reg("/api/mqtt",              HTTP_GET,  api_mqtt_get);
    reg("/api/mqtt",              HTTP_POST, api_mqtt_post);
    reg("/api/syslog",            HTTP_GET,  api_syslog_get);
    reg("/api/syslog",            HTTP_POST, api_syslog_post);
    reg("/api/download/devices",  HTTP_GET,  api_download_devices);
    reg("/api/download/remotes",  HTTP_GET,  api_download_remotes);
    reg("/api/upload/devices",    HTTP_POST, api_upload_devices);
    reg("/api/upload/remotes",    HTTP_POST, api_upload_remotes);
    reg("/api/ota",               HTTP_POST, api_ota_post);
    reg("/api/ota/key",           HTTP_GET,  api_ota_key_get);
    reg("/api/info",              HTTP_GET,  api_info_get);
    reg("/api/upload/web*",       HTTP_POST, api_upload_web_post);

    // Wildcard catch-all for static files
    reg("/*", HTTP_GET, static_file_handler);

    // Start log forwarding to WebSocket clients
    s_log_queue = xQueueCreate(LOG_QUEUE_DEPTH, LOG_LINE_MAX);
    xTaskCreate(log_drain_task, "log_drain", 4096, nullptr, 1, nullptr);
    s_orig_vprintf = esp_log_set_vprintf(web_log_vprintf);

    syslog_init(CONFIG_IP_LAYER_HOSTNAME);
    ota_key_init();

    ESP_LOGI(TAG, "HTTP server started");

    const esp_partition_t *running = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "Running partition: %s (offset 0x%08" PRIx32 ")", running->label, running->address);
    esp_ota_mark_app_valid_cancel_rollback();
}

#endif // CONFIG_WEB_ENABLED
