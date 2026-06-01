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

    // ── IO System Key ─────────────────────────────────────────────────────────

    let sniffPollTimer = null;
    let sniffCountdownTimer = null;
    let sniffSecondsLeft = 0;

    async function loadIoKey(app) {
        try {
            const r = await window.MiOpenApi.requestJson("/api/io/key");
            if (r && r.key) app.elements.ioKeyDisplay.value = r.key;
        } catch (e) { /* silently ignore — key may not be set */ }
    }

    function openIoKeyEditModal(app, prefill) {
        const modal = document.getElementById("io-key-edit-modal");
        const input = document.getElementById("io-key-new-input");
        const status = document.getElementById("io-key-edit-status");
        input.value = prefill || "";
        status.textContent = "";
        modal.style.display = "flex";
        input.focus();
    }

    function closeIoKeyEditModal() {
        document.getElementById("io-key-edit-modal").style.display = "none";
    }

    async function saveIoKey(app) {
        const input = document.getElementById("io-key-new-input");
        const status = document.getElementById("io-key-edit-status");
        const key = input.value.trim().toUpperCase();
        if (!/^[0-9A-F]{32}$/.test(key)) {
            status.textContent = "Key must be exactly 32 hex characters (0-9, A-F).";
            status.style.color = "#e74c3c";
            return;
        }
        try {
            await window.MiOpenApi.postJson("/api/io/key", { key: key });
            app.elements.ioKeyDisplay.value = key;
            app.elements.ioKeyStatus.textContent = "Key saved. Reboot to apply.";
            app.elements.ioKeyStatus.style.color = "#27ae60";
            closeIoKeyEditModal();
        } catch (e) {
            status.textContent = "Error saving key: " + (e.message || e);
            status.style.color = "#e74c3c";
        }
    }

    function stopSniffPoll() {
        if (sniffPollTimer) { clearInterval(sniffPollTimer); sniffPollTimer = null; }
        if (sniffCountdownTimer) { clearInterval(sniffCountdownTimer); sniffCountdownTimer = null; }
    }

    async function cancelSniff() {
        stopSniffPoll();
        try { await window.MiOpenApi.postJson("/api/io/sniff", { active: false }); } catch (e) { /* ignore */ }
    }

    function openSniffModal() {
        const modal = document.getElementById("io-key-sniff-modal");
        document.getElementById("io-sniff-instructions").style.display = "";
        document.getElementById("io-sniff-countdown-row").style.display = "none";
        document.getElementById("io-sniff-result-row").style.display = "none";
        document.getElementById("io-sniff-status").textContent = "";
        document.getElementById("io-sniff-start").style.display = "";
        document.getElementById("io-sniff-use-key").style.display = "none";
        document.getElementById("io-sniff-retry").style.display = "none";
        modal.style.display = "flex";
    }

    async function startSniff(app) {
        document.getElementById("io-sniff-start").style.display = "none";
        document.getElementById("io-sniff-retry").style.display = "none";
        document.getElementById("io-sniff-result-row").style.display = "none";
        document.getElementById("io-sniff-status").textContent = "";
        document.getElementById("io-sniff-instructions").style.display = "none";
        document.getElementById("io-sniff-countdown-row").style.display = "";

        sniffSecondsLeft = 120;
        document.getElementById("io-sniff-countdown").textContent = sniffSecondsLeft;

        try { await window.MiOpenApi.postJson("/api/io/sniff", { active: true }); } catch (e) {
            document.getElementById("io-sniff-status").textContent = "Failed to start: " + (e.message || e);
            document.getElementById("io-sniff-start").style.display = "";
            return;
        }

        sniffCountdownTimer = setInterval(function () {
            sniffSecondsLeft--;
            document.getElementById("io-sniff-countdown").textContent = sniffSecondsLeft;
            if (sniffSecondsLeft <= 0) {
                stopSniffPoll();
                document.getElementById("io-sniff-countdown-row").style.display = "none";
                document.getElementById("io-sniff-status").textContent = "No key captured. Try again.";
                document.getElementById("io-sniff-retry").style.display = "";
            }
        }, 1000);

        sniffPollTimer = setInterval(async function () {
            try {
                const r = await window.MiOpenApi.requestJson("/api/io/sniff");
                if (r && r.key) {
                    stopSniffPoll();
                    document.getElementById("io-sniff-countdown-row").style.display = "none";
                    document.getElementById("io-sniff-captured-key").textContent = r.key;
                    document.getElementById("io-sniff-result-row").style.display = "";
                    document.getElementById("io-sniff-use-key").dataset.key = r.key;
                    document.getElementById("io-sniff-use-key").style.display = "";
                }
            } catch (e) { /* ignore poll errors */ }
        }, 2000);
    }

    function useSniffedKey(app) {
        const key = document.getElementById("io-sniff-use-key").dataset.key;
        document.getElementById("io-key-sniff-modal").style.display = "none";
        stopSniffPoll();
        openIoKeyEditModal(app, key);
    }

    function initIoKey(app) {
        app.elements.ioKeyDisplay = document.getElementById("io-key-display");
        app.elements.ioKeyStatus  = document.getElementById("io-key-status");

        document.getElementById("io-key-show").addEventListener("click", function () {
            const el = app.elements.ioKeyDisplay;
            el.type = el.type === "password" ? "text" : "password";
            this.textContent = el.type === "password" ? "Show" : "Hide";
        });

        document.getElementById("io-key-edit").addEventListener("click", function () {
            openIoKeyEditModal(app, app.elements.ioKeyDisplay.value);
        });

        document.getElementById("io-key-sniff").addEventListener("click", function () {
            openSniffModal();
        });

        document.getElementById("io-key-edit-close").addEventListener("click", closeIoKeyEditModal);
        document.getElementById("io-key-edit-cancel").addEventListener("click", closeIoKeyEditModal);
        document.getElementById("io-key-edit-save").addEventListener("click", function () { saveIoKey(app); });
        document.getElementById("io-key-generate").addEventListener("click", function () {
            const bytes = new Uint8Array(16);
            crypto.getRandomValues(bytes);
            const hex = Array.from(bytes).map(function (b) { return b.toString(16).padStart(2, "0").toUpperCase(); }).join("");
            document.getElementById("io-key-new-input").value = hex;
            document.getElementById("io-key-edit-status").textContent = "";
        });

        document.getElementById("io-sniff-close").addEventListener("click", async function () {
            await cancelSniff();
            document.getElementById("io-key-sniff-modal").style.display = "none";
        });
        document.getElementById("io-sniff-cancel").addEventListener("click", async function () {
            await cancelSniff();
            document.getElementById("io-key-sniff-modal").style.display = "none";
        });
        document.getElementById("io-sniff-start").addEventListener("click", function () { startSniff(app); });
        document.getElementById("io-sniff-retry").addEventListener("click", function () { startSniff(app); });
        document.getElementById("io-sniff-use-key").addEventListener("click", function () { useSniffedKey(app); });

        loadIoKey(app);
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

        initIoKey(app);

        // WiFi scan
        (function () {
            var scanBtn     = document.getElementById("wifi-scan-btn");
            var scanResults = document.getElementById("wifi-scan-results");
            var ssidInput   = app.elements.wifiSsidInput;
            if (!scanBtn) return;

            function rssiToBar(rssi) {
                if (rssi >= -55) return "▂▄▆█";
                if (rssi >= -70) return "▂▄▆&nbsp;";
                if (rssi >= -80) return "▂▄&nbsp;&nbsp;";
                return "▂&nbsp;&nbsp;&nbsp;";
            }

            scanBtn.addEventListener("click", function () {
                scanBtn.disabled = true;
                scanBtn.textContent = "Scanning…";
                scanResults.style.display = "none";
                scanResults.innerHTML = "";

                fetch("/api/wifi/scan?" + Date.now(), { cache: "no-store" })
                    .then(function (r) { return r.json(); })
                    .then(function (networks) {
                        if (!networks.length) {
                            scanResults.innerHTML = "<div style='padding:8px;color:#888;font-size:.9em;'>No networks found.</div>";
                            scanResults.style.display = "block";
                            return;
                        }
                        networks.forEach(function (net) {
                            var row = document.createElement("div");
                            row.style.cssText = "display:flex;justify-content:space-between;align-items:center;padding:8px 10px;cursor:pointer;border-bottom:1px solid #eee;font-size:.9em;";
                            row.innerHTML = "<span>" + net.ssid + "</span>"
                                + "<span style='font-family:monospace;color:#888;font-size:.85em;'>" + rssiToBar(net.rssi) + " " + net.rssi + " dBm</span>";
                            row.addEventListener("mouseenter", function () { row.style.background = "var(--color-accent2,#eef)"; });
                            row.addEventListener("mouseleave", function () { row.style.background = ""; });
                            row.addEventListener("click", function () {
                                ssidInput.value = net.ssid;
                                scanResults.style.display = "none";
                            });
                            scanResults.appendChild(row);
                        });
                        scanResults.style.display = "block";
                    })
                    .catch(function () {
                        scanResults.innerHTML = "<div style='padding:8px;color:red;font-size:.9em;'>Scan failed.</div>";
                        scanResults.style.display = "block";
                    })
                    .finally(function () {
                        scanBtn.disabled = false;
                        scanBtn.textContent = "Scan";
                    });
            });

            // Close results when clicking outside
            document.addEventListener("click", function (e) {
                if (!scanResults.contains(e.target) && e.target !== scanBtn) {
                    scanResults.style.display = "none";
                }
            });
        })();

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

    function onKeyCaptured(key) {
        if (!key) return;
        stopSniffPoll();
        document.getElementById("io-sniff-countdown-row").style.display = "none";
        document.getElementById("io-sniff-captured-key").textContent = key;
        document.getElementById("io-sniff-result-row").style.display = "";
        document.getElementById("io-sniff-use-key").dataset.key = key;
        document.getElementById("io-sniff-use-key").style.display = "";
    }

    window.MiOpenSettings = { init: init, onKeyCaptured: onKeyCaptured };
})();
