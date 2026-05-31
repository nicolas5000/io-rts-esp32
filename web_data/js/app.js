(function () {
    function ensureApiModule() {
        if (window.MiOpenApi) {
            return;
        }

        function ensureJson(response) {
            return response.json().catch(function () {
                return {};
            });
        }

        async function requestJson(url, options) {
            const requestOptions = options || {};
            const method = (requestOptions.method || "GET").toUpperCase();
            const requestUrl = method === "GET"
                ? url + (url.indexOf("?") === -1 ? "?" : "&") + "_=" + Date.now()
                : url;

            if (method === "GET") {
                requestOptions.cache = "no-store";
            }

            const response = await fetch(requestUrl, requestOptions);
            const data = await ensureJson(response);
            if (!response.ok) {
                throw new Error(data.message || ("HTTP error " + response.status));
            }
            return data;
        }

        window.MiOpenApi = {
            downloadFile: async function (url, filename) {
                const response = await fetch(url);
                if (!response.ok) {
                    throw new Error("Network response was not ok");
                }
                const blob = await response.blob();
                const link = document.createElement("a");
                link.href = window.URL.createObjectURL(blob);
                link.download = filename;
                link.click();
                window.URL.revokeObjectURL(link.href);
            },
            postJson: function (url, payload) {
                return requestJson(url, {
                    method: "POST",
                    headers: { "Content-Type": "application/json" },
                    body: JSON.stringify(payload)
                });
            },
            requestJson: requestJson,
            uploadFile: function (url, file) {
                const formData = new FormData();
                formData.append("file", file);
                return requestJson(url, {
                    method: "POST",
                    body: formData
                });
            }
        };
    }

    function createElements() {
        return {
            deviceList: document.getElementById("device-list"),
            devicesFileInput: document.getElementById("devices-file"),
            devicesUploadButton: document.getElementById("upload-devices"),
            downloadDevicesButton: document.getElementById("download-devices"),
            downloadRemotesButton: document.getElementById("download-remotes"),
            helpDeviceButton: document.getElementById("help-device"),
            helpRemoteButton: document.getElementById("help-remote"),
            logFilter: document.getElementById("select-log"),
            mqttDiscoveryInput: document.getElementById("mqtt-discovery"),
            mqttPasswordInput: document.getElementById("mqtt-password"),
            mqttPortInput: document.getElementById("mqtt-port"),
            mqttServerInput: document.getElementById("mqtt-server"),
            mqttUpdateButton: document.getElementById("mqtt-update"),
            mqttUserInput: document.getElementById("mqtt-user"),
            remotePopupButton: document.getElementById("remote-popup"),
            remotesFileInput: document.getElementById("remotes-file"),
            remotesUploadButton: document.getElementById("upload-remotes"),
            statusMessages: document.getElementById("status-messages"),
            syslogEnabledInput: document.getElementById("syslog-enabled"),
            syslogServerInput: document.getElementById("syslog-server"),
            syslogPortInput: document.getElementById("syslog-port"),
            syslogFacilityInput: document.getElementById("syslog-facility"),
            syslogMinLevelInput: document.getElementById("syslog-min-level"),
            syslogUpdateButton: document.getElementById("syslog-update"),
            otaKeyInput: document.getElementById("ota-key"),
            otaFileInput: document.getElementById("ota-file"),
            otaUploadButton: document.getElementById("ota-upload"),
            otaProgress: document.getElementById("ota-progress"),
            otaStatus: document.getElementById("ota-status"),
            themeToggle: document.getElementById("toggle-theme")
        };
    }

    function i18nText(key, fallback) {
        if (typeof window.t === "function") {
            const value = window.t(key);
            if (value && value !== key) {
                return value;
            }
        }
        return fallback || key;
    }

    function logLevelVisible(entryLevel, filter) {
        if (filter === "off") return false;
        if (filter === "info") return entryLevel === "info" || entryLevel === "error";
        return true; // "all"
    }

    function applyLogFilter(app) {
        const filter = app.state.logFilter;
        const hidden = filter === "off";
        app.elements.statusMessages.style.display = hidden ? "none" : "";
        if (!hidden) {
            Array.from(app.elements.statusMessages.children).forEach(function (el) {
                el.style.display = logLevelVisible(el.dataset.level, filter) ? "" : "none";
            });
        }
    }

    function logStatus(app, message, level) {
        // accept legacy boolean isError as second arg
        if (level === true) level = "error";
        if (!level || level === false) level = "debug";

        const filter = app.state.logFilter;
        if (filter === "off") return;

        const logEntry = document.createElement("p");
        logEntry.textContent = message;
        logEntry.dataset.level = level;
        if (level === "error") logEntry.style.color = "red";
        logEntry.style.display = logLevelVisible(level, filter) ? "" : "none";

        app.elements.statusMessages.appendChild(logEntry);
        app.elements.statusMessages.scrollTop = app.elements.statusMessages.scrollHeight;
        while (app.elements.statusMessages.children.length > 100) {
            app.elements.statusMessages.removeChild(app.elements.statusMessages.firstChild);
        }
    }

    function initLogFilter(app) {
        if (!app.elements.logFilter) return;
        app.elements.logFilter.addEventListener("change", function () {
            app.state.logFilter = app.elements.logFilter.value;
            applyLogFilter(app);
        });
    }

    function initTheme(app) {
        const savedTheme = localStorage.getItem("theme");
        if (savedTheme === "dark") {
            document.body.classList.add("dark-mode");
        }
        if (app.elements.themeToggle) {
            app.elements.themeToggle.addEventListener("click", function () {
                document.body.classList.toggle("dark-mode");
                localStorage.setItem("theme", document.body.classList.contains("dark-mode") ? "dark" : "light");
            });
        }
    }

    function initHelpButtons(app) {
        if (app.elements.helpDeviceButton) {
            app.elements.helpDeviceButton.addEventListener("click", function () {
                app.openPopup(
                    app.i18nText("popup.help_title", "Help"),
                    app.i18nText("popup.help_device", "Help device"),
                    [],
                    app.i18nText("help.device", "No help text").split("\n"),
                    { showSave: false }
                );
            });
        }
        if (app.elements.helpRemoteButton) {
            app.elements.helpRemoteButton.addEventListener("click", function () {
                app.openPopup(
                    app.i18nText("popup.help_title", "Help"),
                    app.i18nText("popup.help_remote", "Help remote"),
                    [],
                    app.i18nText("help.remote", "No help text").split("\n"),
                    { showSave: false }
                );
            });
        }
    }

    function initWebSocket(app) {
        const wsScheme = window.location.protocol === "https:" ? "wss" : "ws";
        const ws = new WebSocket(wsScheme + "://" + window.location.host + "/ws");
        ws.onmessage = function (event) {
            try {
                const data = JSON.parse(event.data);
                if (data.type === "log") {
                    app.logStatus(data.message, data.level || "debug");
                } else if (data.type === "position") {
                    app.updateDeviceFill(data.id, data.position);
                } else if (data.type === "init") {
                    app.fetchAndDisplayDevices();
                } else if (data.type === "device_deactivated" || data.type === "device_reactivated") {
                    app.fetchAndDisplayDevices();
                } else if (data.type === "device_deleted") {
                    var el = document.querySelector('.device[data-id="' + data.id + '"]');
                    if (el) el.closest("li") ? el.closest("li").remove() : el.remove();
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
                }
            } catch (e) { /* ignore malformed frames */ }
        };
        ws.onopen = function () {
            ws.send('{"type":"hello"}');
            app.logStatus("WebSocket connected", "info");
        };
        ws.onclose = function () { app.logStatus("WebSocket disconnected", "error"); };
        app.state.ws = ws;
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
                window.MiOpenApi.downloadFile("/api/download/devices", "devices.json").catch(function (error) {
                    app.logStatus("Error downloading: " + error.message, true);
                });
            });
        }
        if (app.elements.downloadRemotesButton) {
            app.elements.downloadRemotesButton.addEventListener("click", function () {
                window.MiOpenApi.downloadFile("/api/download/remotes", "RemoteMap.json").catch(function (error) {
                    app.logStatus("Error downloading: " + error.message, true);
                });
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

        const app = {
            elements: createElements(),
            i18nText: i18nText,
            logStatus: function (message, level) {
                logStatus(app, message, level);
            },
            state: {
                devicesCache: [],
                logFilter: "all",
                ws: null
            }
        };

        window.MiOpenPopup.init(app);
        window.MiOpenDevices.init(app);
        window.MiOpenRemotes.init(app);
        window.MiOpenSettings.init(app);
        window.MiOpenSyslog.init(app);
        window.MiOpenOta.init(app);
        window.MiOpenApp = app;

        initLogFilter(app);
        initTheme(app);
        initHelpButtons(app);
        initWebSocket(app);
        bindEvents(app);

        window.addEventListener("i18n:changed", function () {
            app.fetchAndDisplayDevices();
            app.fetchAndDisplayRemotes();
        });

        fetch("/api/info?" + Date.now(), { cache: "no-store" })
            .then(function (r) { return r.json(); })
            .then(function (d) {
                var el = document.getElementById("firmware-version");
                if (el) el.textContent = "Firmware " + d.version + "  •  " + d.compile_date + " " + d.compile_time + "  •  " + d.idf_ver;
            })
            .catch(function () {});

        app.logStatus("System started", "info");
        app.logStatus("Loading devices...", "debug");
        app.loadMqttConfig();
        app.loadSyslogConfig();
        app.fetchAndDisplayDevices();
        app.fetchAndDisplayRemotes();
    });
})();
