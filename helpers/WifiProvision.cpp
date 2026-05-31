#include "sdkconfig.h"
#include "WifiProvision.hpp"

#ifdef CONFIG_CONNECTIVITY_CHOICE_WIFI

#include "NetworkConfig.hpp"
#include "NetworkHelpers.hpp"

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

#include <cstring>
#include <cstdlib>
#include <cstdint>

static const char *TAG = "wifi_provision";
static const char *PROVISION_AP_SSID = "io-rts-setup";
static bool sIsFallback = false;

namespace Helpers
{
    void WifiProvision::StartAP()
    {
        // netif and event loop are not yet initialised in the no-credentials boot path
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());

        esp_netif_create_default_wifi_ap();

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));

        wifi_config_t ap_config = {};
        strncpy((char *)ap_config.ap.ssid, PROVISION_AP_SSID, sizeof(ap_config.ap.ssid));
        ap_config.ap.ssid_len        = strlen(PROVISION_AP_SSID);
        ap_config.ap.channel         = 1;
        ap_config.ap.max_connection  = 4;
        ap_config.ap.beacon_interval = 200;
        // WPA2 requires a password of at least 8 characters
        if (strlen(CONFIG_CMD_LINE_MANAGEMENT_DEFAULT_PWD) >= 8) {
            ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
            strncpy((char *)ap_config.ap.password, CONFIG_CMD_LINE_MANAGEMENT_DEFAULT_PWD,
                    sizeof(ap_config.ap.password) - 1);
        } else {
            ap_config.ap.authmode = WIFI_AUTH_OPEN;
            ESP_LOGW(TAG, "CLI password < 8 chars — provisioning AP is OPEN (change password for WPA2)");
        }

        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
        ESP_ERROR_CHECK(esp_wifi_start());

        ESP_LOGI(TAG, "Provisioning AP started: SSID=io-rts-setup (WPA2), IP=192.168.4.1");

        StartProvisionServer(false);
        StartDnsServer();
    }

    // --- Provisioning HTTP server ---

    static const char PROVISION_HTML[] =
        "<!DOCTYPE html><html><head><meta charset=utf-8>"
        "<meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>io-rts-esp32 Setup</title>"
        "<style>body{font-family:sans-serif;max-width:400px;margin:40px auto;padding:0 16px}"
        "input{width:100%;box-sizing:border-box;padding:8px;margin:6px 0 8px;border:1px solid #ccc;border-radius:4px}"
        ".btn{width:100%;padding:10px;border:none;border-radius:4px;font-size:1em;cursor:pointer;margin-bottom:8px}"
        ".btn-primary{background:#1976d2;color:#fff}"
        ".btn-scan{background:#e3f2fd;color:#1976d2;border:1px solid #90caf9}"
        "#nets{margin:0 0 8px;padding:0;list-style:none}"
        "#nets li{padding:8px;border:1px solid #ddd;border-radius:4px;margin-bottom:4px;cursor:pointer}"
        "#nets li:hover{background:#e3f2fd}"
        ".rssi{float:right;color:#888;font-size:.85em}"
        "p{color:#555;font-size:.9em}</style></head>"
        "<body><h2>io-rts-esp32 Setup</h2>"
        "<button class='btn btn-scan' onclick='scan()'>&#x1F4F6; Scan for networks</button>"
        "<ul id=nets></ul>"
        "<form method=post action=/connect>"
        "<label>WiFi SSID<input id=ssid name=ssid type=text maxlength=32 required autocomplete=off></label>"
        "<label>Password"
        "<div style='position:relative'>"
        "<input id=pw name=password type=password maxlength=64 style='padding-right:40px'>"
        "<button type=button onclick=\"var i=document.getElementById('pw');i.type=i.type=='password'?'text':'password';this.textContent=i.type=='password'?'&#x1F441;':'&#x1F576;'\" "
        "style='position:absolute;right:4px;top:50%;transform:translateY(-50%);background:none;border:none;cursor:pointer;font-size:1.1em;padding:2px'>&#x1F441;</button>"
        "</div></label>"
        "<button class='btn btn-primary' type=submit>Connect</button>"
        "</form>"
        "<p>The device will restart after saving. Reconnect to your home network.</p>"
        "<script>"
        "function scan(){"
        "var ul=document.getElementById('nets');"
        "ul.innerHTML='<li>Scanning…</li>';"
        "fetch('/scan').then(function(r){return r.json();})"
        ".then(function(nets){"
        "ul.innerHTML='';"
        "if(!nets.length){ul.innerHTML='<li>No networks found</li>';return;}"
        "nets.forEach(function(n){"
        "var li=document.createElement('li');"
        "li.innerHTML=n.ssid+'<span class=rssi>'+n.rssi+'dBm</span>';"
        "li.onclick=function(){document.getElementById('ssid').value=n.ssid;};"
        "ul.appendChild(li);});})"
        ".catch(function(){ul.innerHTML='<li>Scan failed</li>';});}"
        "</script>"
        "</body></html>";

    static const char PROVISION_SAVED_HTML[] =
        "<!DOCTYPE html><html><head><meta charset=utf-8><title>Saved</title>"
        "<style>body{font-family:sans-serif;max-width:400px;margin:40px auto;padding:0 16px;text-align:center}"
        ".ok{color:#388e3c;font-size:2.5em}"
        "p{color:#555;text-align:left}"
        "#status{color:#1976d2;font-size:.9em;margin-top:16px}</style></head>"
        "<body><div class=ok>&#x2713;</div><h2>Credentials saved!</h2>"
        "<p>The device is restarting. <strong>Reconnect your phone to your home WiFi network</strong>, "
        "then this page will automatically open the device dashboard.</p>"
        "<div id=status>Waiting for device&hellip;</div>"
        "<script>"
        "var n=0;"
        "function check(){"
        "fetch('http://io-rts-esp32.local/',{mode:'no-cors',cache:'no-store'})"
        ".then(function(){window.location='http://io-rts-esp32.local/';});"
        "n++;document.getElementById('status').textContent="
        "'Waiting for device'+'.'.repeat((n%3)+1);"
        "}"
        "setTimeout(function(){setInterval(check,3000);},5000);"
        "</script>"
        "</body></html>";

    static const char PROVISION_ERROR_HTML[] =
        "<!DOCTYPE html><html><head><meta charset=utf-8><title>Error</title></head>"
        "<body><h2>Error</h2><p>SSID must be between 1 and 32 characters.</p>"
        "<a href=/>Back</a></body></html>";

    // URL-decode a percent-encoded string in-place, returns pointer to buf
    static char *url_decode(char *buf, size_t len)
    {
        char *src = buf, *dst = buf;
        char *end = buf + len;
        while (src < end && *src) {
            if (*src == '+') {
                *dst++ = ' '; src++;
            } else if (*src == '%' && src + 2 < end) {
                char hex[3] = {src[1], src[2], 0};
                *dst++ = (char)strtol(hex, nullptr, 16);
                src += 3;
            } else {
                *dst++ = *src++;
            }
        }
        *dst = '\0';
        return buf;
    }

    // Extract a form field value from url-encoded body; out must be at least out_len bytes
    static bool form_get_field(const char *body, const char *key, char *out, size_t out_len)
    {
        size_t key_len = strlen(key);
        const char *p = body;
        while (p && *p) {
            if (strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
                const char *val = p + key_len + 1;
                const char *end = strchr(val, '&');
                size_t val_len = end ? (size_t)(end - val) : strlen(val);
                if (val_len >= out_len) val_len = out_len - 1;
                memcpy(out, val, val_len);
                out[val_len] = '\0';
                url_decode(out, val_len);
                return true;
            }
            p = strchr(p, '&');
            if (p) p++;
        }
        return false;
    }

    // Catch-all 404 handler: redirect to the setup form (captive portal trigger)
    static esp_err_t provision_404_handler(httpd_req_t *req, httpd_err_code_t err)
    {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    static esp_err_t provision_scan_handler(httpd_req_t *req)
    {
        // Briefly switch to APSTA so scanning works while AP stays up
        esp_wifi_set_mode(WIFI_MODE_APSTA);
        vTaskDelay(pdMS_TO_TICKS(100)); // let mode settle

        wifi_scan_config_t scan_cfg = {};
        scan_cfg.scan_type = WIFI_SCAN_TYPE_PASSIVE;
        esp_wifi_scan_start(&scan_cfg, true); // blocking scan

        uint16_t ap_count = 0;
        esp_wifi_scan_get_ap_num(&ap_count);
        if (ap_count > 20) ap_count = 20;

        wifi_ap_record_t *records = new wifi_ap_record_t[ap_count > 0 ? ap_count : 1];
        esp_wifi_scan_get_ap_records(&ap_count, records);

        // Build JSON: [{"ssid":"...","rssi":-xx}, ...]
        // Use a simple fixed buffer; skip SSIDs with quotes to avoid injection
        char buf[1024] = "[";
        int pos = 1;
        bool first = true;
        for (uint16_t i = 0; i < ap_count && pos < 900; i++) {
            const char *ssid = (const char *)records[i].ssid;
            if (!ssid[0]) continue; // skip hidden networks
            // skip SSIDs containing quote or backslash
            bool safe = true;
            for (int j = 0; ssid[j]; j++)
                if (ssid[j] == '"' || ssid[j] == '\\') { safe = false; break; }
            if (!safe) continue;
            if (!first) buf[pos++] = ',';
            pos += snprintf(buf + pos, sizeof(buf) - pos,
                            "{\"ssid\":\"%s\",\"rssi\":%d}", ssid, (int)records[i].rssi);
            first = false;
        }
        buf[pos++] = ']';
        buf[pos] = '\0';
        delete[] records;

        // Switch back to pure AP mode
        esp_wifi_set_mode(WIFI_MODE_AP);

        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    static const char *wifi_reason_text(uint8_t r) {
        switch (r) {
            case 200: return "Signal lost";
            case 201: return "Router not found";
            case 202: return "Wrong password";
            case 203: return "Association failed";
            default:  return "Connection error";
        }
    }

    static esp_err_t provision_status_handler(httpd_req_t *req)
    {
        std::string target = sIsFallback ? Config::NetworkConfig::GetWifiSSID() : "";
        int retry  = Helpers::NetworkHelpers::GetWifiRetryCount();
        uint8_t reason = Helpers::NetworkHelpers::GetLastDisconnectReason();
        int remaining  = Helpers::NetworkHelpers::GetApTimeoutRemainingS();

        char buf[256];
        snprintf(buf, sizeof(buf),
            "{\"mode\":\"%s\",\"target_ssid\":\"%s\","
            "\"retry_count\":%d,\"last_reason\":%d,\"last_reason_text\":\"%s\","
            "\"ap_timeout_remaining_s\":%d}",
            sIsFallback ? "fallback" : "provisioning",
            target.c_str(),
            retry,
            (int)reason,
            wifi_reason_text(reason),
            remaining);

        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    static const char FALLBACK_BANNER[] =
        "<div style=\"background:#fff3e0;border:1px solid #ffb300;border-radius:4px;"
        "padding:10px;margin-bottom:16px\" id=\"status-banner\">"
        "<strong>&#9888; Connection lost</strong><br>"
        "Trying to reconnect to <strong id=\"target-ssid\">...</strong>&hellip;"
        " Attempt <span id=\"attempt-count\">?</span>.<br>"
        "<small>The device reconnects automatically when the router is available."
        " Use the form below only if credentials have changed.</small>"
        "</div>"
        "<script>"
        "function refreshStatus(){"
        "fetch('/status').then(r=>r.json()).then(d=>{"
        "document.getElementById('target-ssid').textContent=d.target_ssid||'?';"
        "document.getElementById('attempt-count').textContent=d.retry_count||'?';"
        "});}"
        "refreshStatus();setInterval(refreshStatus,5000);"
        "</script>";

    static esp_err_t provision_get_handler(httpd_req_t *req)
    {
        httpd_resp_set_type(req, "text/html");
        if (!sIsFallback) {
            httpd_resp_send(req, PROVISION_HTML, HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        }
        // Fallback mode: inject banner before the form
        // Send HTML in two parts: head+open-body, then banner, then rest of form
        static const char SPLIT_MARKER[] = "<body>";
        const char *split = strstr(PROVISION_HTML, SPLIT_MARKER);
        if (!split) {
            httpd_resp_send(req, PROVISION_HTML, HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        }
        size_t head_len = split - PROVISION_HTML + strlen(SPLIT_MARKER);
        httpd_resp_send_chunk(req, PROVISION_HTML, head_len);
        httpd_resp_send_chunk(req, FALLBACK_BANNER, HTTPD_RESP_USE_STRLEN);
        httpd_resp_send_chunk(req, PROVISION_HTML + head_len, HTTPD_RESP_USE_STRLEN);
        httpd_resp_send_chunk(req, nullptr, 0);
        return ESP_OK;
    }

    static esp_err_t provision_post_handler(httpd_req_t *req)
    {
        char body[512] = {};
        int received = httpd_req_recv(req, body, sizeof(body) - 1);
        if (received <= 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
            return ESP_FAIL;
        }
        body[received] = '\0';

        char ssid[64] = {};     // oversized to detect values > 32 chars
        char password[65] = {};
        form_get_field(body, "ssid", ssid, sizeof(ssid));
        form_get_field(body, "password", password, sizeof(password));

        size_t ssid_len = strlen(ssid);
        if (ssid_len == 0 || ssid_len > 32) {
            httpd_resp_set_type(req, "text/html");
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_send(req, PROVISION_ERROR_HTML, HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        }

        Config::NetworkConfig::SetWifiSSID(ssid);
        Config::NetworkConfig::SetWifiPassword(password);
        ESP_LOGI(TAG, "Provisioned: SSID=%s — restarting", ssid);

        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, PROVISION_SAVED_HTML, HTTPD_RESP_USE_STRLEN);

        // Restart from a one-shot timer so the HTTP response sends cleanly first
        esp_timer_handle_t t;
        esp_timer_create_args_t ta = {};
        ta.callback = [](void *){ esp_restart(); };
        ta.name = "prov_rst";
        if (esp_timer_create(&ta, &t) == ESP_OK)
            esp_timer_start_once(t, 8000 * 1000); // 8s in µs — allow time for infra (e.g. test AP) to be ready
        else
            esp_restart(); // fallback if timer creation fails
        return ESP_OK;
    }

    void WifiProvision::StartProvisionServer(bool isFallback)
    {
        sIsFallback = isFallback;
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.stack_size       = 6144;
        config.max_uri_handlers = 5;
        config.task_priority    = tskIDLE_PRIORITY + 2;
        // In fallback mode the main web server already owns port 80 and the default
        // ctrl socket port (32768); use different values for both to avoid conflict.
        if (isFallback) {
            config.server_port = 8081;
            config.ctrl_port   = 32769;
        }

        httpd_handle_t server = nullptr;
        if (httpd_start(&server, &config) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start provisioning HTTP server");
            return;
        }

        httpd_uri_t get_root = {};
        get_root.uri     = "/";
        get_root.method  = HTTP_GET;
        get_root.handler = provision_get_handler;

        httpd_uri_t post_connect = {};
        post_connect.uri     = "/connect";
        post_connect.method  = HTTP_POST;
        post_connect.handler = provision_post_handler;

        httpd_uri_t get_scan = {};
        get_scan.uri     = "/scan";
        get_scan.method  = HTTP_GET;
        get_scan.handler = provision_scan_handler;

        httpd_uri_t get_status = {};
        get_status.uri     = "/status";
        get_status.method  = HTTP_GET;
        get_status.handler = provision_status_handler;

        httpd_register_uri_handler(server, &get_root);
        httpd_register_uri_handler(server, &get_scan);
        httpd_register_uri_handler(server, &post_connect);
        httpd_register_uri_handler(server, &get_status);
        httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, provision_404_handler);

        ESP_LOGI(TAG, "Provisioning HTTP server started");
    }

    // --- DNS captive portal ---

    static void dns_task(void *arg)
    {
        static const uint32_t CAPTIVE_IP = PP_HTONL(LWIP_MAKEU32(192, 168, 4, 1));

        int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock < 0) {
            ESP_LOGE(TAG, "DNS: socket failed");
            vTaskDelete(nullptr);
            return;
        }

        struct sockaddr_in addr = {};
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons(53);
        addr.sin_addr.s_addr = INADDR_ANY;

        if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            ESP_LOGE(TAG, "DNS: bind failed");
            close(sock);
            vTaskDelete(nullptr);
            return;
        }

        ESP_LOGI(TAG, "DNS captive portal started on port 53");

        uint8_t buf[512];
        struct sockaddr_in client;
        socklen_t client_len = sizeof(client);

        while (true) {
            int len = recvfrom(sock, buf, sizeof(buf), 0,
                               (struct sockaddr *)&client, &client_len);
            if (len < 12) continue; // too short to be a valid DNS query

            // Parse query type from the question section
            // Skip header (12 bytes), then skip QNAME labels to find QTYPE
            int qtype_pos = 12;
            while (qtype_pos < len && buf[qtype_pos] != 0) {
                uint8_t label_len = buf[qtype_pos] & 0x3F;
                if ((buf[qtype_pos] & 0xC0) == 0xC0) { qtype_pos += 2; break; } // pointer
                qtype_pos += 1 + label_len;
            }
            qtype_pos++;              // skip null terminator
            uint16_t qtype = (qtype_pos + 1 < len) ?
                             ((buf[qtype_pos] << 8) | buf[qtype_pos + 1]) : 0;

            // Build response: copy query, then set QR=1 and append answer for A records
            uint8_t resp[512];
            int resp_len = len;
            if (resp_len > (int)sizeof(resp)) resp_len = sizeof(resp);
            memcpy(resp, buf, resp_len);

            // Flags: QR=1, AA=1, RD preserved
            resp[2] = 0x81;  // QR=1, OPCODE=0, AA=1, TC=0, RD=1
            resp[3] = 0x80;  // RA=1

            if (qtype == 1 && resp_len + 16 <= (int)sizeof(resp)) {
                // A record: set ANCOUNT=1, append answer
                resp[6] = 0; resp[7] = 1;  // ANCOUNT = 1

                // Answer: name pointer to question (0xC00C), type A, class IN
                resp[resp_len++] = 0xC0; resp[resp_len++] = 0x0C; // name pointer
                resp[resp_len++] = 0x00; resp[resp_len++] = 0x01; // type A
                resp[resp_len++] = 0x00; resp[resp_len++] = 0x01; // class IN
                resp[resp_len++] = 0x00; resp[resp_len++] = 0x00; // TTL high
                resp[resp_len++] = 0x00; resp[resp_len++] = 60;   // TTL low (60s)
                resp[resp_len++] = 0x00; resp[resp_len++] = 0x04; // RDLENGTH = 4
                memcpy(&resp[resp_len], &CAPTIVE_IP, 4);
                resp_len += 4;
            } else {
                // Non-A query: return empty answer section
                resp[6] = 0; resp[7] = 0;
            }

            sendto(sock, resp, resp_len, 0,
                   (struct sockaddr *)&client, client_len);
        }
    }

    void WifiProvision::StartDnsServer()
    {
        xTaskCreate(dns_task, "dns_task", 4096, nullptr,
                    tskIDLE_PRIORITY + 1, nullptr);
    }
}

#endif // CONFIG_CONNECTIVITY_CHOICE_WIFI
