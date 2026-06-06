(function () {
    function ensureApiModule() {
        if (window.MiOpenApi) return;
        function ensureJson(r) { return r.json().catch(function () { return {}; }); }
        async function requestJson(url, options) {
            var opts = options || {};
            var method = (opts.method || "GET").toUpperCase();
            var reqUrl = method === "GET" ? url + (url.indexOf("?") === -1 ? "?" : "&") + "_=" + Date.now() : url;
            if (method === "GET") opts.cache = "no-store";
            var response = await fetch(reqUrl, opts);
            var data = await ensureJson(response);
            if (!response.ok) throw new Error(data.message || ("HTTP error " + response.status));
            return data;
        }
        window.MiOpenApi = {
            downloadFile: async function (url, filename) {
                var r = await fetch(url);
                if (!r.ok) throw new Error("Network response was not ok");
                var blob = await r.blob();
                var a = document.createElement("a");
                a.href = window.URL.createObjectURL(blob);
                a.download = filename;
                a.click();
                window.URL.revokeObjectURL(a.href);
            },
            postJson: function (url, payload) {
                return requestJson(url, { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(payload) });
            },
            requestJson: requestJson,
            uploadFile: function (url, file) {
                var fd = new FormData();
                fd.append("file", file);
                return requestJson(url, { method: "POST", body: fd });
            }
        };
    }

    function createElements() {
        return {
            deviceList:           document.getElementById("device-list"),
            devicesFileInput:     document.getElementById("devices-file"),
            devicesUploadButton:  document.getElementById("upload-devices"),
            downloadDevicesButton:document.getElementById("download-devices"),
            downloadRemotesButton:document.getElementById("download-remotes"),
            helpDeviceButton:     document.getElementById("help-device"),
            helpRemoteButton:     document.getElementById("help-remote"),
            logFilter:            document.getElementById("select-log"),
            mqttDiscoveryInput:   document.getElementById("mqtt-discovery"),
            mqttPasswordInput:    document.getElementById("mqtt-password"),
            mqttPortInput:        document.getElementById("mqtt-port"),
            mqttServerInput:      document.getElementById("mqtt-server"),
            mqttUpdateButton:     document.getElementById("mqtt-update"),
            mqttUserInput:        document.getElementById("mqtt-user"),
            remotePopupButton:    document.getElementById("remote-popup"),
            remotesFileInput:     document.getElementById("remotes-file"),
            remotesUploadButton:  document.getElementById("upload-remotes"),
            statusMessages:       document.getElementById("status-messages"),
            syslogEnabledInput:   document.getElementById("syslog-enabled"),
            syslogServerInput:    document.getElementById("syslog-server"),
            syslogPortInput:      document.getElementById("syslog-port"),
            syslogFacilityInput:  document.getElementById("syslog-facility"),
            syslogMinLevelInput:  document.getElementById("syslog-min-level"),
            syslogUpdateButton:   document.getElementById("syslog-update"),
            otaKeyInput:          document.getElementById("ota-key-display"),
            otaFileInput:         document.getElementById("ota-file"),
            otaUploadButton:      document.getElementById("ota-upload"),
            otaProgress:          document.getElementById("ota-progress"),
            otaStatus:            document.getElementById("ota-status"),
            themeToggle:          document.getElementById("toggle-theme")
        };
    }

    function i18nText(key, fallback) {
        if (typeof window.t === "function") {
            var value = window.t(key);
            if (value && value !== key) return value;
        }
        return fallback || key;
    }

    function logLevelVisible(entryLevel, filter) {
        if (filter === "off")  return false;
        if (filter === "info") return entryLevel === "info" || entryLevel === "error";
        return true;
    }

    function applyLogFilter(app) {
        var filter = app.state.logFilter;
        var hidden = filter === "off";
        app.elements.statusMessages.style.display = hidden ? "none" : "";
        if (!hidden) {
            Array.from(app.elements.statusMessages.children).forEach(function (el) {
                el.style.display = logLevelVisible(el.dataset.level, filter) ? "" : "none";
            });
        }
    }

    function logStatus(app, message, level) {
        if (level === true)  level = "error";
        if (!level || level === false) level = "debug";
        var filter = app.state.logFilter;
        if (filter === "off") return;
        var p = document.createElement("p");
        p.textContent = message;
        p.dataset.level = level;
        p.style.display = logLevelVisible(level, filter) ? "" : "none";
        app.elements.statusMessages.appendChild(p);
        app.elements.statusMessages.scrollTop = app.elements.statusMessages.scrollHeight;
        while (app.elements.statusMessages.children.length > 100) {
            app.elements.statusMessages.removeChild(app.elements.statusMessages.firstChild);
        }
    }

    function initLogFilter(app) {
        // Wire the log filter buttons in v2 layout
        document.querySelectorAll(".log-filter-btn").forEach(function (btn) {
            btn.addEventListener("click", function () {
                document.querySelectorAll(".log-filter-btn").forEach(function (b) { b.classList.remove("active"); });
                btn.classList.add("active");
                app.state.logFilter = btn.dataset.filter || "all";
                applyLogFilter(app);
            });
        });
    }

    function initTheme() {
        var THEMES = [
            { id:"charcoal", icon:"●", next:"Navy"   },
            { id:"navy",     icon:"◑", next:"Light"  },
            { id:"light",    icon:"○", next:"Purple" },
            { id:"purple",   icon:"◆", next:"Dark"   }
        ];
        var saved = localStorage.getItem("v2theme") || "charcoal";
        var idx = THEMES.findIndex(function (t) { return t.id === saved; });
        if (idx < 0) idx = 0;

        function applyTheme() {
            var t = THEMES[idx];
            document.documentElement.setAttribute("data-theme", t.id);
            localStorage.setItem("v2theme", t.id);
            var icon = document.getElementById("theme-icon");
            var lbl  = document.getElementById("theme-label");
            if (icon) icon.textContent = t.icon;
            if (lbl)  lbl.textContent  = THEMES[idx].next;
        }

        applyTheme();

        var btn = document.getElementById("theme-btn");
        if (btn) {
            btn.addEventListener("click", function () {
                idx = (idx + 1) % THEMES.length;
                applyTheme();
            });
        }
    }

    function updateMovingPill(app) {
        var pill = document.getElementById("moving-pill");
        if (!pill) return;
        if (app.state.movingSet.size > 0) {
            pill.textContent = app.state.movingSet.size + " moving";
            pill.classList.remove("hidden");
        } else {
            pill.classList.add("hidden");
        }
    }

    function initWebSocket(app) {
        var wsScheme = window.location.protocol === "https:" ? "wss" : "ws";
        var reconnectDelay = 1000;

        function connect() {
        var ws = new WebSocket(wsScheme + "://" + window.location.host + "/ws");
        ws.onmessage = function (event) {
            try {
                var data = JSON.parse(event.data);
                if (data.type === "log") {
                    app.logStatus(data.message, data.level || "debug");
                } else if (data.type === "position") {
                    var cached = (app.state.devicesCache || []).find(function (d) { return d.id === data.id; });
                    app.updateDeviceFill(data.id, data.position, cached ? !!cached.is_inverted : false);
                    app.updateDeviceState(data.id, data.is_stopped);
                    if (data.is_stopped === false) {
                        app.state.movingSet.add(data.id);
                    } else {
                        app.state.movingSet.delete(data.id);
                    }
                    updateMovingPill(app);
                } else if (data.type === "init") {
                    app.fetchAndDisplayDevices();
                } else if (data.type === "device_deactivated" || data.type === "device_reactivated") {
                    app.fetchAndDisplayDevices();
                } else if (data.type === "device_deleted") {
                    var el = document.querySelector('.device[data-id="' + data.id + '"]');
                    if (el) { var li = el.closest("li"); if (li) li.remove(); else el.remove(); }
                    app.logStatus("Device removed.", "info");
                } else if (data.type === "device_added") {
                    if (app.pairingWizard) app.pairingWizard.onDeviceAdded(data.id, data.name || data.id);
                    app.fetchAndDisplayDevices();
                } else if (data.type === "pairing_active") {
                    if (app.pairingWizard) app.pairingWizard.onPairingActive(data.remaining_s || 0);
                } else if (data.type === "pair_failed") {
                    if (app.pairingWizard) app.pairingWizard.onPairFailed();
                } else if (data.type === "remote_seen") {
                    if (app.pairingWizard) app.pairingWizard.onRemoteSeen(data.id);
                } else if (data.type === "remote_capture_timeout") {
                    if (app.pairingWizard) app.pairingWizard.onCaptureTimeout();
                } else if (data.type === "io_key_captured") {
                    if (window.MiOpenSettings && window.MiOpenSettings.onKeyCaptured) {
                        window.MiOpenSettings.onKeyCaptured(data.key);
                    }
                }
            } catch (e) { /* ignore parse errors */ }
        };
        ws.onopen = function () {
            reconnectDelay = 1000;
            ws.send('{"type":"hello"}');
            app.logStatus("WebSocket connected", "info");
            var dot = document.getElementById("conn-dot");
            if (dot) { dot.classList.remove("offline"); }
        };
        ws.onclose = function () {
            app.logStatus("WebSocket disconnected — reconnecting in " + (reconnectDelay / 1000) + "s", "error");
            var dot = document.getElementById("conn-dot");
            if (dot) dot.classList.add("offline");
            setTimeout(connect, reconnectDelay);
            reconnectDelay = Math.min(reconnectDelay * 2, 30000);
        };
        app.state.ws = ws;
        } // end connect()
        connect();
    }

    function bindEvents(app) {
        if (app.elements.mqttUpdateButton) {
            app.elements.mqttUpdateButton.addEventListener("click", app.updateMqttConfig);
        }
        if (app.elements.devicesUploadButton) {
            app.elements.devicesUploadButton.addEventListener("click", app.uploadDevices);
        }
        if (app.elements.remotesUploadButton) {
            app.elements.remotesUploadButton.addEventListener("click", app.uploadRemotes);
        }
        if (app.elements.downloadDevicesButton) {
            app.elements.downloadDevicesButton.addEventListener("click", function () {
                window.MiOpenApi.downloadFile("/api/download/devices", "devices.json").catch(function (e) { app.logStatus("Error downloading: " + e.message, true); });
            });
        }
        if (app.elements.downloadRemotesButton) {
            app.elements.downloadRemotesButton.addEventListener("click", function () {
                window.MiOpenApi.downloadFile("/api/download/remotes", "RemoteMap.json").catch(function (e) { app.logStatus("Error downloading: " + e.message, true); });
            });
        }
        if (app.elements.remotePopupButton) {
            app.elements.remotePopupButton.addEventListener("click", app.openAddRemotePopup);
        }
        if (app.elements.syslogUpdateButton) {
            app.elements.syslogUpdateButton.addEventListener("click", app.updateSyslogConfig);
        }
        if (app.elements.otaUploadButton) {
            app.elements.otaUploadButton.addEventListener("click", app.uploadFirmware);
        }
    }

    document.addEventListener("DOMContentLoaded", function () {
        ensureApiModule();
        initTheme();

        var app = {
            elements: createElements(),
            i18nText: i18nText,
            logStatus: function (message, level) { logStatus(app, message, level); },
            state: { devicesCache: [], logFilter: "all", ws: null, movingSet: new Set() }
        };

        window.MiOpenPopup.init(app);
        window.MiOpenDevices.init(app);
        window.MiOpenRemotes.init(app);
        window.MiOpenSettings.init(app);
        window.MiOpenSyslog.init(app);
        window.MiOpenOta.init(app);
        window.MiOpenApp = app;

        initLogFilter(app);
        initWebSocket(app);
        bindEvents(app);

        // Forward wheel events from outside the app column to the main scroll area
        document.addEventListener("wheel", function (e) {
            var main = document.querySelector("main");
            if (main && !main.contains(e.target)) {
                main.scrollTop += e.deltaY;
            }
        }, { passive: true });

        // Language selector
        var langSel = document.getElementById("lang");
        if (langSel) {
            langSel.addEventListener("change", function () { window.setLang(this.value); });
        }

        window.addEventListener("i18n:changed", function () {
            app.fetchAndDisplayDevices();
            app.fetchAndDisplayRemotes();
        });

        fetch("/api/info?" + Date.now(), { cache: "no-store" })
            .then(function (r) { return r.json(); })
            .then(function (d) {
                var el = document.getElementById("firmware-version");
                if (el) el.textContent = d.version + " · " + d.compile_date;
                if (window.MiOpenUpdater) window.MiOpenUpdater.init(d.version);
            })
            .catch(function () {});

        app.logStatus("System started", "info");
        app.logStatus("Loading devices…", "debug");
        app.loadMqttConfig();
        app.loadSyslogConfig();
        app.fetchAndDisplayDevices();
        app.fetchAndDisplayRemotes();
    });
})();
