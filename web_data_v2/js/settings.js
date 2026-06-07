(function () {

    let fallbackStatusTimer = null;

    async function loadFallbackConfig(app) {
        try {
            const cfg = await window.MiOpenApi.requestJson("/api/wifi/fallback");
            app.elements.fallbackEnabled.checked      = !!cfg.enabled;
            app.elements.fallbackEnabled.dispatchEvent(new Event('change'));
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
            el.textContent = "⚠ Fallback AP active";
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
        var settingsView = document.getElementById("view-settings");
        if (!settingsView || !settingsView.classList.contains("active")) return;
        try {
            const cfg = await window.MiOpenApi.requestJson("/api/wifi/fallback");
            updateFallbackStatus(app, cfg);
        } catch (e) {  }
    }

    async function saveFallbackConfig(app) {
        try {
            const r = await window.MiOpenApi.postJson("/api/wifi/fallback", {
                enabled:          app.elements.fallbackEnabled.checked,
                retries_boot:     parseInt(app.elements.fallbackRetriesBoot.value)    || 3,
                retries_running:  parseInt(app.elements.fallbackRetriesRunning.value) || 3,
                ap_timeout_s:     parseInt(app.elements.fallbackTimeout.value)        || 0
            });
            if (!r.success) { showToast(r.message || "Save failed.", "error"); return; }
            showToast("Fallback AP settings saved.", "success");
        } catch (e) {
            showToast("Error saving fallback config.", "error");
        }
    }

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
            showToast("SSID cannot be empty.", "error");
            return;
        }

        const msg = "The device will restart and connect to '" + ssid + "'. "
            + "If credentials are wrong the fallback AP (io-rts-setup) will appear after a short delay.";
        if (!confirm(msg)) return;

        if (statusEl) statusEl.textContent = "";

        const payload = { ssid: ssid };
        if (pwd) payload.password = pwd;

        try {
            const wr = await window.MiOpenApi.postJson("/api/wifi/config", payload);
            if (!wr.success) { showToast(wr.message || "WiFi save failed.", "error"); return; }
            showToast("WiFi saved — restarting…", "info", 8000);
            const poll = setInterval(async function () {
                try {
                    const r = await window.MiOpenApi.requestJson("/api/ota/key");
                    if (r && r.key) { clearInterval(poll); window.location.reload(); }
                } catch (e) {  }
            }, 3000);
        } catch (e) {
            showToast("Error saving WiFi: " + (e.message || e), "error");
        }
    }

    async function loadMqttConfig(app) {
        try {
            const config = await window.MiOpenApi.requestJson("/api/mqtt");
            app.elements.mqttUserInput.value      = config.user      || "";
            app.elements.mqttServerInput.value    = config.server    || "";
            app.elements.mqttPasswordInput.value  = config.password  || "";
            app.elements.mqttPortInput.value      = config.port      || "";
            app.elements.mqttClientIdInput.value  = config.client_id || "";
            app.elements.mqttTopicInput.value     = config.topic     || "";
            app.elements.mqttDiscoveryInput.value = config.discovery || "";
            var statusEl = document.getElementById("mqtt-conn-status");
            if (statusEl) {
                statusEl.textContent = config.connected ? "● Connected" : "○ Not connected";
                statusEl.classList.toggle("connected", !!config.connected);
                statusEl.classList.toggle("offline", !config.connected);
            }
        } catch (error) {
            console.error("Error fetching MQTT config", error);
        }
    }

    async function updateMqttConfig(app) {
        try {
            const r = await window.MiOpenApi.postJson("/api/mqtt", {
                user:      app.elements.mqttUserInput.value,
                server:    app.elements.mqttServerInput.value,
                password:  app.elements.mqttPasswordInput.value,
                port:      app.elements.mqttPortInput.value,
                client_id: app.elements.mqttClientIdInput.value,
                topic:     app.elements.mqttTopicInput.value,
                discovery: app.elements.mqttDiscoveryInput.value
            });
            if (!r.success) { showToast(r.message || "MQTT save failed.", "error"); return; }
            showToast("MQTT settings saved. Reboot to apply.", "success");
        } catch (error) {
            showToast("Error saving MQTT config.", "error");
        }
    }

    async function uploadSelectedFile(app, input, url, missingMessage, successMessage, refreshFn) {
        const file = input.files[0];
        if (!file) { showToast(missingMessage, "error"); return; }
        try {
            const result = await window.MiOpenApi.uploadFile(url, file);
            if (!result.success) { showToast(result.message || "Upload failed.", "error"); return; }
            showToast(result.message || successMessage, "success");
            if (refreshFn) await refreshFn();
        } catch (error) {
            showToast(error.message || "Upload failed.", "error");
        }
    }

    // ── IO Controller Config ──────────────────────────────────────────────────

    async function loadIoConfig(app) {
        try {
            const r = await window.MiOpenApi.requestJson("/api/io/config");
            app.elements.ioNodeIdInput.value  = (r.node_id  || "").toUpperCase();
            app.elements.ioTxPowerInput.value = r.tx_power  ?? "";
            app.elements.ioPassiveModeCheckbox.checked = !!r.passive_mode;
            app.elements.ioPassiveToggle.classList.toggle("on", !!r.passive_mode);
        } catch (e) { /* silently ignore */ }
    }

    async function saveIoConfig(app) {
        const nodeId  = app.elements.ioNodeIdInput.value.trim().toUpperCase();
        const txPower = parseInt(app.elements.ioTxPowerInput.value);
        const statusEl = app.elements.ioConfigStatus;

        if (nodeId && !/^[0-9A-F]{6}$/.test(nodeId)) {
            statusEl.textContent = "Node address must be exactly 6 hex characters.";
            statusEl.style.color = "var(--red)";
            return;
        }
        if (app.elements.ioTxPowerInput.value !== "" && (isNaN(txPower) || txPower < 0 || txPower > 20)) {
            statusEl.textContent = "TX Power must be 0–20.";
            statusEl.style.color = "var(--red)";
            return;
        }

        const payload = { passive_mode: app.elements.ioPassiveModeCheckbox.checked };
        if (nodeId)                              payload.node_id  = nodeId;
        if (app.elements.ioTxPowerInput.value)   payload.tx_power = txPower;

        try {
            const r = await window.MiOpenApi.postJson("/api/io/config", payload);
            if (!r.success) { statusEl.textContent = r.message || "Save failed."; statusEl.style.color = "var(--red)"; return; }
            statusEl.textContent = "Saved. Reboot to apply.";
            statusEl.style.color = "var(--green)";
        } catch (e) {
            statusEl.textContent = "Error: " + (e.message || e);
            statusEl.style.color = "var(--red)";
        }
    }

    function initIoConfig(app) {
        app.elements.ioNodeIdInput          = document.getElementById("io-node-id");
        app.elements.ioTxPowerInput         = document.getElementById("io-tx-power");
        app.elements.ioPassiveModeCheckbox  = document.getElementById("io-passive-mode");
        app.elements.ioPassiveToggle        = document.getElementById("io-passive-toggle");
        app.elements.ioConfigStatus         = document.getElementById("io-config-status");

        app.elements.ioPassiveToggle.addEventListener("click", function () {
            var chk = app.elements.ioPassiveModeCheckbox;
            chk.checked = !chk.checked;
            app.elements.ioPassiveToggle.classList.toggle("on", chk.checked);
        });

        document.getElementById("io-config-save").addEventListener("click", function () { saveIoConfig(app); });
        loadIoConfig(app);
    }

    // ── Access Password ───────────────────────────────────────────────────────

    function initAccessPassword(app) {
        app.elements.accessPasswordNew     = document.getElementById("access-password-new");
        app.elements.accessPasswordConfirm = document.getElementById("access-password-confirm");
        app.elements.accessPasswordStatus  = document.getElementById("access-password-status");

        document.getElementById("access-password-save").addEventListener("click", async function () {
            const pwd     = app.elements.accessPasswordNew.value;
            const confirm = app.elements.accessPasswordConfirm.value;
            const statusEl = app.elements.accessPasswordStatus;

            if (pwd !== confirm) {
                statusEl.textContent = "Passwords do not match.";
                statusEl.style.color = "var(--red)";
                return;
            }
            if (pwd.length > 32) {
                statusEl.textContent = "Password too long (max 32 characters).";
                statusEl.style.color = "var(--red)";
                return;
            }

            try {
                const r = await window.MiOpenApi.postJson("/api/misc/password", { password: pwd });
                if (!r.success) { statusEl.textContent = r.message || "Save failed."; statusEl.style.color = "var(--red)"; return; }
                statusEl.textContent = pwd ? "Password saved. Reboot to apply." : "Password cleared. Reboot to apply.";
                statusEl.style.color = "var(--green)";
                app.elements.accessPasswordNew.value     = "";
                app.elements.accessPasswordConfirm.value = "";
            } catch (e) {
                statusEl.textContent = "Error: " + (e.message || e);
                statusEl.style.color = "var(--red)";
            }
        });
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
        modal.classList.add("open");
        input.focus();
    }

    function closeIoKeyEditModal() {
        document.getElementById("io-key-edit-modal").classList.remove("open");
    }

    async function saveIoKey(app) {
        const input = document.getElementById("io-key-new-input");
        const status = document.getElementById("io-key-edit-status");
        const key = input.value.trim().toUpperCase();
        if (!/^[0-9A-F]{32}$/.test(key)) {
            status.textContent = "Key must be exactly 32 hex characters (0-9, A-F).";
            status.style.color = "var(--red)";
            return;
        }
        try {
            const r = await window.MiOpenApi.postJson("/api/io/key", { key: key });
            if (!r.success) {
                status.textContent = r.message || "Save failed.";
                status.style.color = "var(--red)";
                return;
            }
            app.elements.ioKeyDisplay.value = key;
            app.elements.ioKeyStatus.textContent = "Key saved. Reboot to apply.";
            app.elements.ioKeyStatus.style.color = "var(--green)";
            closeIoKeyEditModal();
        } catch (e) {
            status.textContent = "Error saving key: " + (e.message || e);
            status.style.color = "var(--red)";
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
        modal.classList.add("open");
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
        document.getElementById("io-key-sniff-modal").classList.remove("open");
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

        document.getElementById("io-key-edit-cancel").addEventListener("click", closeIoKeyEditModal);
        document.getElementById("io-key-edit-save").addEventListener("click", function () { saveIoKey(app); });
        document.getElementById("io-key-generate").addEventListener("click", function () {
            const bytes = new Uint8Array(16);
            crypto.getRandomValues(bytes);
            const hex = Array.from(bytes).map(function (b) { return b.toString(16).padStart(2, "0").toUpperCase(); }).join("");
            document.getElementById("io-key-new-input").value = hex;
            document.getElementById("io-key-edit-status").textContent = "";
        });
        document.getElementById("io-key-edit-modal").addEventListener("click", function (e) {
            if (e.target === this) closeIoKeyEditModal();
        });

        document.getElementById("io-sniff-cancel").addEventListener("click", async function () {
            await cancelSniff();
            document.getElementById("io-key-sniff-modal").classList.remove("open");
        });
        document.getElementById("io-key-sniff-modal").addEventListener("click", async function (e) {
            if (e.target === this) { await cancelSniff(); this.classList.remove("open"); }
        });
        document.getElementById("io-sniff-start").addEventListener("click", function () { startSniff(app); });
        document.getElementById("io-sniff-retry").addEventListener("click", function () { startSniff(app); });
        document.getElementById("io-sniff-use-key").addEventListener("click", function () { useSniffedKey(app); });

        loadIoKey(app);
    }

    function initReboot() {
        var btn = document.getElementById("reboot-btn");
        if (!btn) return;
        btn.addEventListener("click", function () {
            if (!confirm("Reboot the device now?")) return;
            fetch("/api/reboot", { method: "POST" })
                .then(function () {
                    showToast("Rebooting…", "info", 60000);
                    var deadline = Date.now() + 60000;
                    function poll() {
                        if (Date.now() > deadline) {
                            showToast("Device did not come back online.", "error");
                            return;
                        }
                        fetch("/api/devices?" + Date.now(), { cache: "no-store" })
                            .then(function (r) {
                                if (r.ok) { showToast("Device is back online.", "success"); }
                                else { setTimeout(poll, 2000); }
                            })
                            .catch(function () { setTimeout(poll, 2000); });
                    }
                    setTimeout(poll, 5000);
                })
                .catch(function () { showToast("Reboot request failed.", "error"); });
        });
    }

    function init(app) {

        app.elements.fallbackEnabled        = document.getElementById("fallback-enabled");
        app.elements.fallbackRetriesBoot    = document.getElementById("fallback-retries-boot");
        app.elements.fallbackRetriesRunning = document.getElementById("fallback-retries-running");
        app.elements.fallbackTimeout        = document.getElementById("fallback-timeout");
        app.elements.fallbackStatus         = document.getElementById("fallback-status");
        app.loadFallbackConfig = function () { return loadFallbackConfig(app); };
        app.saveFallbackConfig = function () { return saveFallbackConfig(app); };
        document.getElementById("fallback-save").addEventListener("click", function () { app.saveFallbackConfig(); });
        loadFallbackConfig(app);
        fallbackStatusTimer = setInterval(function () { pollFallbackStatus(app); }, 15000);

        app.elements.wifiSsidInput     = document.getElementById("wifi-ssid");
        app.elements.wifiPasswordInput = document.getElementById("wifi-password");
        app.elements.wifiStatus        = document.getElementById("wifi-config-status");
        app.loadWifiConfig  = function () { return loadWifiConfig(app); };
        app.saveWifiConfig  = function () { return saveWifiConfig(app); };
        document.getElementById("wifi-config-save").addEventListener("click", function () { app.saveWifiConfig(); });
        loadWifiConfig(app);

        initIoConfig(app);
        initAccessPassword(app);
        initIoKey(app);
        initReboot();

        // Update channel toggle
        var betaCheckbox = document.getElementById("update-channel-beta");
        var betaLabel = document.getElementById("update-channel-label");
        var betaToggle = document.getElementById("update-channel-toggle");
        if (betaCheckbox && betaToggle) {
            var savedChannel = localStorage.getItem("updateChannel") || "stable";
            betaCheckbox.checked = savedChannel === "beta";
            betaLabel.textContent = betaCheckbox.checked ? "Include beta" : "Stable only";
            betaToggle.classList.toggle("on", betaCheckbox.checked);

            betaToggle.addEventListener("click", function () {
                betaCheckbox.checked = !betaCheckbox.checked;
                var channel = betaCheckbox.checked ? "beta" : "stable";
                localStorage.setItem("updateChannel", channel);
                betaLabel.textContent = betaCheckbox.checked ? "Include beta" : "Stable only";
                betaToggle.classList.toggle("on", betaCheckbox.checked);
            });
        }

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
                            row.className = "wifi-scan-row";
                            var nameSpan = document.createElement("span");
                            nameSpan.textContent = net.ssid;
                            var sigSpan = document.createElement("span");
                            sigSpan.className = "wifi-scan-signal";
                            sigSpan.innerHTML = rssiToBar(net.rssi) + " " + net.rssi + " dBm";
                            row.appendChild(nameSpan);
                            row.appendChild(sigSpan);
                            row.addEventListener("click", function () {
                                ssidInput.value = net.ssid;
                                scanResults.style.display = "none";
                            });
                            scanResults.appendChild(row);
                        });
                        scanResults.style.display = "block";
                    })
                    .catch(function () { showToast("WiFi scan failed.", "error"); })
                    .finally(function () {
                        scanBtn.disabled = false;
                        scanBtn.textContent = "Scan";
                    });
            });

            document.addEventListener("click", function (e) {
                if (!scanResults.contains(e.target) && e.target !== scanBtn) {
                    scanResults.style.display = "none";
                }
            });
        })();

        app.loadMqttConfig   = function () { return loadMqttConfig(app); };
        app.updateMqttConfig = function () { return updateMqttConfig(app); };

        app.uploadDevices = function () {
            return uploadSelectedFile(
                app, app.elements.devicesFileInput, "/api/upload/devices",
                "No devices file selected.", "Devices uploaded.",
                async function () {
                    await app.fetchAndDisplayDevices();
                    await app.fetchAndDisplayRemotes();
                }
            );
        };
        app.uploadRemotes = function () {
            return uploadSelectedFile(
                app, app.elements.remotesFileInput, "/api/upload/remotes",
                "No remotes file selected.", "Remotes uploaded.",
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
