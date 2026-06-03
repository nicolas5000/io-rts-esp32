(function () {

    // ── WiFi Fallback AP ──────────────────────────────────────────────────────

    let fallbackStatusTimer = null;

    async function loadFallbackConfig(app) {
        try {
            const cfg = await window.MiOpenApi.requestJson("/api/wifi/fallback");
            app.elements.fallbackEnabled.checked      = !!cfg.enabled;
            app.elements.fallbackRetriesBoot.value    = cfg.retries_boot    ?? 3;
            app.elements.fallbackRetriesRunning.value = cfg.retries_running ?? 3;
            app.elements.fallbackTimeout.value        = cfg.ap_timeout_s    ?? 600;
            updateFallbackStatus(app, cfg);
        } catch (e) {
            console.error("Error fetching fallback config", e);
        }
    }

    function updateFallbackStatus(app, cfg) {
        const el = app.elements.fallbackStatus;
        if (!el) return;
        if (cfg.ap_running) {
            el.textContent = "⚠ Fallback AP active (retry " + (cfg.retries_boot || "?") + ")";
            el.style.color = "#e67e22";
        } else if (cfg.connected) {
            el.textContent = "● Connected";
            el.style.color = "#27ae60";
        } else {
            el.textContent = "○ Not connected";
            el.style.color = "#888";
        }
    }

    async function pollFallbackStatus(app) {
        try {
            const cfg = await window.MiOpenApi.requestJson("/api/wifi/fallback");
            updateFallbackStatus(app, cfg);
        } catch (e) { /* ignore */ }
    }

    async function saveFallbackConfig(app) {
        try {
            await window.MiOpenApi.postJson("/api/wifi/fallback", {
                enabled:          app.elements.fallbackEnabled.checked,
                retries_boot:     parseInt(app.elements.fallbackRetriesBoot.value)    || 3,
                retries_running:  parseInt(app.elements.fallbackRetriesRunning.value) || 3,
                ap_timeout_s:     parseInt(app.elements.fallbackTimeout.value)        || 0
            });
            app.logStatus("WiFi fallback AP settings saved.", "debug");
        } catch (e) {
            app.logStatus("Error saving fallback config", "error");
        }
    }

    // ── WiFi Network ─────────────────────────────────────────────────────────

    async function loadWifiConfig(app) {
        try {
            const cfg = await window.MiOpenApi.requestJson("/api/wifi/config");
            app.elements.wifiSsidInput.value = cfg.ssid || "";
        } catch (e) {
            console.error("Error fetching WiFi config", e);
        }
    }

    async function saveWifiConfig(app) {
        const ssid = app.elements.wifiSsidInput.value.trim();
        const pwd  = app.elements.wifiPasswordInput.value;
        const statusEl = app.elements.wifiStatus;

        if (!ssid) {
            if (statusEl) { statusEl.textContent = "SSID cannot be empty."; statusEl.style.color = "#e74c3c"; }
            return;
        }

        const msg = "The device will restart and connect to ‘" + ssid + "’. "
            + "If credentials are wrong the fallback AP (io-rts-setup) will appear after a short delay.";
        if (!confirm(msg)) return;

        if (statusEl) { statusEl.textContent = "Saving…"; statusEl.style.color = "#888"; }

        const payload = { ssid: ssid };
        if (pwd) payload.password = pwd;

        try {
            await window.MiOpenApi.postJson("/api/wifi/config", payload);
            if (statusEl) { statusEl.textContent = "Restarting…"; statusEl.style.color = "#e67e22"; }
            // Poll until device comes back, then reload
            const poll = setInterval(async function () {
                try {
                    const r = await window.MiOpenApi.requestJson("/api/ota/key");
                    if (r && r.key) { clearInterval(poll); window.location.reload(); }
                } catch (e) { /* still rebooting */ }
            }, 3000);
        } catch (e) {
            if (statusEl) { statusEl.textContent = "Error: " + (e.message || e); statusEl.style.color = "#e74c3c"; }
        }
    }

    async function loadMqttConfig(app) {
        try {
            const config = await window.MiOpenApi.requestJson("/api/mqtt");
            app.elements.mqttUserInput.value = config.user || "";
            app.elements.mqttServerInput.value = config.server || "";
            app.elements.mqttPasswordInput.value = config.password || "";
            app.elements.mqttPortInput.value = config.port || "";
            app.elements.mqttDiscoveryInput.value = config.discovery || "";
        } catch (error) {
            console.error("Error fetching MQTT config", error);
        }
    }

    async function updateMqttConfig(app) {
        try {
            const result = await window.MiOpenApi.postJson("/api/mqtt", {
                user: app.elements.mqttUserInput.value,
                server: app.elements.mqttServerInput.value,
                password: app.elements.mqttPasswordInput.value,
                port: app.elements.mqttPortInput.value,
                discovery: app.elements.mqttDiscoveryInput.value
            });
            app.logStatus(result.message || "MQTT settings updated.", "debug");
        } catch (error) {
            app.logStatus("Error updating MQTT config", "error");
        }
    }

    async function uploadSelectedFile(app, input, url, missingMessage, successMessage, refreshFn) {
        const file = input.files[0];
        if (!file) { app.logStatus(missingMessage, "error"); return; }
        try {
            const result = await window.MiOpenApi.uploadFile(url, file);
            app.logStatus(result.message || successMessage, "debug");
            if (refreshFn) await refreshFn();
        } catch (error) {
            app.logStatus(error.message || successMessage, "error");
        }
    }

    function init(app) {
        // Fallback AP elements
        app.elements.fallbackEnabled        = document.getElementById("fallback-enabled");
        app.elements.fallbackRetriesBoot    = document.getElementById("fallback-retries-boot");
        app.elements.fallbackRetriesRunning = document.getElementById("fallback-retries-running");
        app.elements.fallbackTimeout        = document.getElementById("fallback-timeout");
        app.elements.fallbackStatus         = document.getElementById("fallback-status");

        app.loadFallbackConfig = function () { return loadFallbackConfig(app); };
        app.saveFallbackConfig = function () { return saveFallbackConfig(app); };

        document.getElementById("fallback-save").addEventListener("click", function () {
            app.saveFallbackConfig();
        });

        // Load on init and poll status every 5s
        loadFallbackConfig(app);
        fallbackStatusTimer = setInterval(function () { pollFallbackStatus(app); }, 5000);

        // WiFi Network elements
        app.elements.wifiSsidInput     = document.getElementById("wifi-ssid");
        app.elements.wifiPasswordInput = document.getElementById("wifi-password");
        app.elements.wifiStatus        = document.getElementById("wifi-config-status");
        app.loadWifiConfig  = function () { return loadWifiConfig(app); };
        app.saveWifiConfig  = function () { return saveWifiConfig(app); };
        document.getElementById("wifi-config-save").addEventListener("click", function () {
            app.saveWifiConfig();
        });
        loadWifiConfig(app);

        app.loadMqttConfig = function () { return loadMqttConfig(app); };
        app.updateMqttConfig = function () { return updateMqttConfig(app); };
        app.uploadDevices = function () {
            return uploadSelectedFile(
                app, app.elements.devicesFileInput, "/api/upload/devices",
                "No devices file selected", "Devices file uploaded",
                async function () {
                    await app.fetchAndDisplayDevices();
                    await app.fetchAndDisplayRemotes();
                }
            );
        };
        app.uploadRemotes = function () {
            return uploadSelectedFile(
                app, app.elements.remotesFileInput, "/api/upload/remotes",
                "No remotes file selected", "Remotes file uploaded",
                function () { return app.fetchAndDisplayRemotes(); }
            );
        };
    }

    window.MiOpenSettings = { init: init };
})();
