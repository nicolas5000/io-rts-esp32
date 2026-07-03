(function () {
    function g(id) { return document.getElementById(id); }

    let fallbackStatusTimer = null;
    let mqttStatusTimer = null;

    function updateMqttStatusEl(status) {
        var el = g("mqtt-conn-status");
        if (!el) return;
        var map = {
            connected:    { text: t("status.mqtt.connected"),    color: "#27ae60" },
            connecting:   { text: t("status.mqtt.connecting"),   color: "#e67e22" },
            disconnected: { text: t("status.mqtt.disconnected"), color: "#888" },
            error:        { text: t("status.mqtt.error"),        color: "#e74c3c" },
            disabled:     { text: t("status.mqtt.disabled"),     color: "#888" },
        };
        var s = map[status] || { text: "○ " + status, color: "#888" };
        el.textContent = s.text;
        el.style.color = s.color;
    }

    async function pollMqttStatus(app) {
        var settingsView = g("view-settings");
        if (!settingsView || !settingsView.classList.contains("active")) return;
        try {
            var cfg = await window.MiOpenApi.requestJson("/api/mqtt");
            updateMqttStatusEl(cfg.status || "disconnected");
        } catch (e) { }
    }

    async function loadFallbackConfig(app) {
        try {
            const cfg = await window.MiOpenApi.requestJson("/api/wifi/fallback");
            app.elements.fallbackEnabled.checked      = !!cfg.enabled;
            app.elements.fallbackEnabled.dispatchEvent(new Event('change'));
            app.elements.fallbackRetriesBoot.value    = cfg.retries_boot    ?? 3;
            app.elements.fallbackRetriesRunning.value = cfg.retries_running ?? 3;
            app.elements.fallbackTimeout.value        = cfg.ap_timeout_s    ?? 600;
            app.elements.fallbackApSsid.value         = cfg.ap_ssid         ?? "";
            updateFallbackStatus(app, cfg);
        } catch (e) {
            console.error("Error fetching fallback config", e);
        }
    }

    function updateFallbackStatus(app, cfg) {
        const el = app.elements.fallbackStatus;
        if (!el) return;
        if (cfg.ap_running) {
            el.textContent = t("status.wifi.ap-active");
            el.style.color = "#e67e22";
        } else if (cfg.connected) {
            el.textContent = t("status.wifi.connected");
            el.style.color = "#27ae60";
        } else {
            el.textContent = t("status.wifi.not-connected");
            el.style.color = "#888";
        }
    }

    async function pollFallbackStatus(app) {
        var settingsView = g("view-settings");
        if (!settingsView || !settingsView.classList.contains("active")) return;
        try {
            const cfg = await window.MiOpenApi.requestJson("/api/wifi/fallback");
            updateFallbackStatus(app, cfg);
        } catch (e) {  }
    }

    async function saveFallbackConfig(app) {
        var statusEl = g("fallback-save-status");
        // Validate and optionally save password first
        var pwdNew     = app.elements.fallbackApPasswordNew.value;
        var pwdConfirm = app.elements.fallbackApPasswordConfirm.value;
        if (pwdNew || pwdConfirm) {
            if (pwdNew !== pwdConfirm) {
                if (statusEl) { statusEl.textContent = t("toast.passwords-no-match"); statusEl.style.color = "var(--red)"; }
                return;
            }
            if (pwdNew && pwdNew.length < 8) {
                if (statusEl) { statusEl.textContent = t("toast.password-too-short"); statusEl.style.color = "var(--red)"; }
                return;
            }
            try {
                const pr = await window.MiOpenApi.postJson("/api/misc/password", { password: pwdNew });
                if (!pr.success && !pr.ok) {
                    if (statusEl) { statusEl.textContent = pr.message || t("toast.save-failed"); statusEl.style.color = "var(--red)"; }
                    return;
                }
                app.elements.fallbackApPasswordNew.value = "";
                app.elements.fallbackApPasswordConfirm.value = "";
            } catch (e) {
                if (statusEl) { statusEl.textContent = t("toast.error-saving-password"); statusEl.style.color = "var(--red)"; }
                return;
            }
        }
        try {
            const r = await window.MiOpenApi.postJson("/api/wifi/fallback", {
                enabled:          app.elements.fallbackEnabled.checked,
                retries_boot:     parseInt(app.elements.fallbackRetriesBoot.value)    || 3,
                retries_running:  parseInt(app.elements.fallbackRetriesRunning.value) || 3,
                ap_timeout_s:     parseInt(app.elements.fallbackTimeout.value)        || 600,
                ap_ssid:          app.elements.fallbackApSsid.value.trim() || "io-rts-setup"
            });
            if (!r.success && !r.ok) { showToast(r.message || t("toast.save-failed"), "error"); return; }
            if (statusEl) { statusEl.textContent = ""; }
            showToast(t("toast.fallback-saved"), "success");
        } catch (e) {
            showToast(t("toast.error-saving-fallback"), "error");
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
            showToast(t("toast.ssid-empty"), "error");
            return;
        }

        const msg = t("confirm.wifi-change", { ssid: ssid });
        if (!confirm(msg)) return;

        if (statusEl) statusEl.textContent = "";

        const payload = { ssid: ssid };
        if (pwd) payload.password = pwd;

        try {
            const wr = await window.MiOpenApi.postJson("/api/wifi/config", payload);
            if (wr.status === "restarting") {
            } else if (!wr.success) {
                showToast(wr.message || "WiFi save failed.", "error");
                return;
            }
            showToast(t("toast.wifi-saved-restarting"), "info", 8000);
            const poll = setInterval(async function () {
                try {
                    const r = await window.MiOpenApi.requestJson("/api/ota/key");
                    if (r && r.key) { clearInterval(poll); window.location.reload(); }
                } catch (e) {  }
            }, 3000);
        } catch (e) {
            showToast(t("toast.error-saving-wifi", { message: e.message || e }), "error");
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
            var enabled = config.enabled !== false;
            app.elements.mqttEnabledInput.checked = enabled;
            app.elements.mqttEnabledToggle.classList.toggle("on", enabled);
            updateMqttStatusEl(config.status || "disconnected");
        } catch (error) {
            console.error("Error fetching MQTT config", error);
        }
    }

    async function updateMqttConfig(app, fromToggle) {
        var enabled = app.elements.mqttEnabledInput.checked;
        try {
            const r = await window.MiOpenApi.postJson("/api/mqtt", {
                enabled:   enabled,
                user:      app.elements.mqttUserInput.value.trim(),
                server:    app.elements.mqttServerInput.value.trim(),
                password:  app.elements.mqttPasswordInput.value,
                port:      app.elements.mqttPortInput.value,
                client_id: app.elements.mqttClientIdInput.value.trim(),
                topic:     app.elements.mqttTopicInput.value.trim(),
                discovery: app.elements.mqttDiscoveryInput.value.trim()
            });
            if (!r.success) { showToast(r.message || t("toast.save-failed"), "error"); return; }
            if (fromToggle) {
                if (!enabled) showToast(t("toast.mqtt-disabled"), "info");
            } else {
                showToast(enabled ? t("toast.mqtt-saved") : t("toast.mqtt-saved-reboot"), "success");
            }
            setTimeout(async function () {
                try {
                    var cfg = await window.MiOpenApi.requestJson("/api/mqtt");
                    updateMqttStatusEl(cfg.status || "disconnected");
                    if (enabled && cfg.status === "disabled") {
                        showToast(t("toast.reboot-required"), "info");
                    }
                } catch (e) { }
            }, 1500);
        } catch (error) {
            showToast(t("toast.error-saving-mqtt"), "error");
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

    function setNetworkStaticDisabled(app, disabled) {
        app.elements.netStaticFields.style.opacity       = disabled ? "0.45" : "1";
        app.elements.netStaticFields.style.pointerEvents = disabled ? "none"  : "";
        [app.elements.netIp, app.elements.netMask, app.elements.netGateway,
         app.elements.netDns1, app.elements.netSntp].forEach(function (el) {
            el.disabled = disabled;
        });
    }

    async function loadNetworkConfig(app) {
        try {
            const r = await window.MiOpenApi.requestJson("/api/network/config");
            app.elements.netHostname.value = r.hostname || "";
            var isDhcp = r.dhcp !== false;
            app.elements.netDhcp.checked = isDhcp;
            app.elements.netDhcpToggle.classList.toggle("on", isDhcp);
            setNetworkStaticDisabled(app, isDhcp);
            // Store actual DHCP-assigned values so the toggle can pre-fill them
            app.dhcpActual = {
                ip:      r.actual_ip      || "0.0.0.0",
                mask:    r.actual_mask    || "0.0.0.0",
                gateway: r.actual_gateway || "0.0.0.0",
                dns1:    r.actual_dns1    || "0.0.0.0"
            };
            if (isDhcp) {
                // Show what the device is actually using
                app.elements.netIp.value      = app.dhcpActual.ip;
                app.elements.netMask.value    = app.dhcpActual.mask;
                app.elements.netGateway.value = app.dhcpActual.gateway;
                app.elements.netDns1.value    = app.dhcpActual.dns1;
            } else {
                app.elements.netIp.value      = r.ip      || "";
                app.elements.netMask.value    = r.mask    || "";
                app.elements.netGateway.value = r.gateway || "";
                app.elements.netDns1.value    = r.dns1    || "";
            }
            app.elements.netSntp.value = r.sntp || "";
        } catch (e) { showToast(t("toast.load-network-failed"), "error"); }
    }

    async function saveNetworkConfig(app) {
        var payload = {
            hostname: app.elements.netHostname.value.trim(),
            dhcp:     app.elements.netDhcp.checked
        };
        if (!app.elements.netDhcp.checked) {
            payload.ip      = app.elements.netIp.value.trim();
            payload.mask    = app.elements.netMask.value.trim();
            payload.gateway = app.elements.netGateway.value.trim();
            payload.dns1    = app.elements.netDns1.value.trim();
            payload.sntp    = app.elements.netSntp.value.trim();
        }
        try {
            var r = await window.MiOpenApi.postJson("/api/network/config", payload);
            if (!r.success) { showToast(r.message || t("toast.save-failed"), "error"); return; }
            if (!confirm(t("confirm.network-reboot"))) return;
            showToast(t("toast.network-saved-restarting"), "info", 8000);
            window.MiOpenApi.postJson("/api/reboot", {}).catch(function(){});
        } catch (e) {
            showToast(t("toast.error-saving-network", { message: e.message || e }), "error");
        }
    }

    function initNetworkConfig(app) {
        app.elements.netHostname    = g("net-hostname");
        app.elements.netDhcp        = g("net-dhcp");
        app.elements.netDhcpToggle  = g("net-dhcp-toggle");
        app.elements.netStaticFields= g("net-static-fields");
        app.elements.netIp          = g("net-ip");
        app.elements.netMask        = g("net-mask");
        app.elements.netGateway     = g("net-gateway");
        app.elements.netDns1        = g("net-dns1");
        app.elements.netSntp        = g("net-sntp");

        app.elements.netDhcpToggle.addEventListener("click", function () {
            var wasDhcp = app.elements.netDhcp.checked;
            app.elements.netDhcp.checked = !wasDhcp;
            var isDhcp = app.elements.netDhcp.checked;
            app.elements.netDhcpToggle.classList.toggle("on", isDhcp);
            setNetworkStaticDisabled(app, isDhcp);
            // Switching DHCP → static: pre-fill fields with the live DHCP values
            if (wasDhcp && !isDhcp && app.dhcpActual) {
                app.elements.netIp.value      = app.dhcpActual.ip;
                app.elements.netMask.value    = app.dhcpActual.mask;
                app.elements.netGateway.value = app.dhcpActual.gateway;
                app.elements.netDns1.value    = app.dhcpActual.dns1;
            }
        });

        g("net-config-save").addEventListener("click", function () { saveNetworkConfig(app); });
        loadNetworkConfig(app);
    }

    async function loadIoConfig(app) {
        try {
            const r = await window.MiOpenApi.requestJson("/api/io/config");
            app.elements.ioNodeIdInput.value  = (r.node_id  || "").toUpperCase();
            app.elements.ioTxPowerInput.value = r.tx_power  ?? "";
            app.elements.ioPassiveModeCheckbox.checked = !!r.passive_mode;
            app.elements.ioPassiveToggle.classList.toggle("on", !!r.passive_mode);
        } catch (e) { showToast(t("toast.load-controller-failed"), "error"); }
    }

    async function saveIoConfig(app) {
        const nodeId  = app.elements.ioNodeIdInput.value.trim().toUpperCase();
        const txPower = parseInt(app.elements.ioTxPowerInput.value);

        if (nodeId && !/^[0-9A-F]{6}$/.test(nodeId)) {
            showToast(t("toast.node-address-invalid"), "error");
            return;
        }
        if (app.elements.ioTxPowerInput.value !== "" && (isNaN(txPower) || txPower < 0 || txPower > 20)) {
            showToast(t("toast.tx-power-invalid"), "error");
            return;
        }

        const payload = { passive_mode: app.elements.ioPassiveModeCheckbox.checked };
        if (nodeId)                              payload.node_id  = nodeId;
        if (app.elements.ioTxPowerInput.value)   payload.tx_power = txPower;

        try {
            const r = await window.MiOpenApi.postJson("/api/io/config", payload);
            if (!r.success) { showToast(r.message || t("toast.save-failed"), "error"); return; }
            showToast(t("toast.controller-saved"), "success");
        } catch (e) {
            showToast(t("toast.error-saving-controller", { message: e.message || e }), "error");
        }
    }

    function initIoConfig(app) {
        app.elements.ioNodeIdInput          = g("io-node-id");
        app.elements.ioTxPowerInput         = g("io-tx-power");
        app.elements.ioPassiveModeCheckbox  = g("io-passive-mode");
        app.elements.ioPassiveToggle        = g("io-passive-toggle");

        app.elements.ioPassiveToggle.addEventListener("click", function () {
            var chk = app.elements.ioPassiveModeCheckbox;
            chk.checked = !chk.checked;
            app.elements.ioPassiveToggle.classList.toggle("on", chk.checked);
        });

        g("io-config-save").addEventListener("click", function () { saveIoConfig(app); });
        loadIoConfig(app);
    }

    function initAccessPassword(app) {
        app.elements.fallbackApPasswordNew     = g("fallback-ap-password-new");
        app.elements.fallbackApPasswordConfirm = g("fallback-ap-password-confirm");
    }

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
        const modal = g("io-key-edit-modal");
        const input = g("io-key-new-input");
        const status = g("io-key-edit-status");
        input.value = prefill || "";
        status.textContent = "";
        modal.classList.add("open");
        input.focus();
    }

    function closeIoKeyEditModal() {
        g("io-key-edit-modal").classList.remove("open");
    }

    async function saveIoKey(app) {
        const input = g("io-key-new-input");
        const status = g("io-key-edit-status");
        const key = input.value.trim().toUpperCase();
        if (!/^[0-9A-F]{32}$/.test(key)) {
            status.textContent = t("status.key-must-be-32-hex");
            status.style.color = "var(--red)";
            return;
        }
        try {
            const r = await window.MiOpenApi.postJson("/api/io/key", { key: key });
            if (!r.success) {
                status.textContent = r.message || t("toast.save-failed");
                status.style.color = "var(--red)";
                return;
            }
            app.elements.ioKeyDisplay.value = key;
            app.elements.ioKeyStatus.textContent = t("toast.key-saved-reboot");
            app.elements.ioKeyStatus.style.color = "var(--green)";
            closeIoKeyEditModal();
        } catch (e) {
            status.textContent = t("toast.error-saving-key", { message: e.message || e });
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
        const modal = g("io-key-sniff-modal");
        g("io-sniff-instructions").style.display = "";
        g("io-sniff-countdown-row").style.display = "none";
        g("io-sniff-result-row").style.display = "none";
        g("io-sniff-status").textContent = "";
        g("io-sniff-start").style.display = "";
        g("io-sniff-use-key").style.display = "none";
        g("io-sniff-retry").style.display = "none";
        modal.classList.add("open");
    }

    async function startSniff(app) {
        g("io-sniff-start").style.display = "none";
        g("io-sniff-retry").style.display = "none";
        g("io-sniff-result-row").style.display = "none";
        g("io-sniff-status").textContent = "";
        g("io-sniff-instructions").style.display = "none";
        g("io-sniff-countdown-row").style.display = "";

        sniffSecondsLeft = 120;
        g("io-sniff-countdown").textContent = sniffSecondsLeft;

        try { await window.MiOpenApi.postJson("/api/io/sniff", { active: true }); } catch (e) {
            g("io-sniff-status").textContent = t("status.failed-to-start", { message: e.message || e });
            g("io-sniff-start").style.display = "";
            return;
        }

        sniffCountdownTimer = setInterval(function () {
            sniffSecondsLeft--;
            g("io-sniff-countdown").textContent = sniffSecondsLeft;
            if (sniffSecondsLeft <= 0) {
                stopSniffPoll();
                g("io-sniff-countdown-row").style.display = "none";
                g("io-sniff-status").textContent = t("status.no-key-captured");
                g("io-sniff-retry").style.display = "";
            }
        }, 1000);

        sniffPollTimer = setInterval(async function () {
            try {
                const r = await window.MiOpenApi.requestJson("/api/io/sniff");
                if (r && r.key) {
                    stopSniffPoll();
                    g("io-sniff-countdown-row").style.display = "none";
                    g("io-sniff-captured-key").textContent = r.key;
                    g("io-sniff-result-row").style.display = "";
                    g("io-sniff-use-key").dataset.key = r.key;
                    g("io-sniff-use-key").style.display = "";
                }
            } catch (e) { /* ignore poll errors */ }
        }, 2000);
    }

    function useSniffedKey(app) {
        const key = g("io-sniff-use-key").dataset.key;
        g("io-key-sniff-modal").classList.remove("open");
        stopSniffPoll();
        openIoKeyEditModal(app, key);
    }

    function initIoKey(app) {
        app.elements.ioKeyDisplay = g("io-key-display");
        app.elements.ioKeyStatus  = g("io-key-status");

        g("io-key-show").addEventListener("click", function () {
            const el = app.elements.ioKeyDisplay;
            el.type = el.type === "password" ? "text" : "password";
            this.textContent = el.type === "password" ? t("button.show") : t("button.hide");
        });

        g("io-key-edit").addEventListener("click", function () {
            openIoKeyEditModal(app, app.elements.ioKeyDisplay.value);
        });

        g("io-key-sniff").addEventListener("click", function () {
            openSniffModal();
        });

        g("io-key-edit-cancel").addEventListener("click", closeIoKeyEditModal);
        g("io-key-edit-save").addEventListener("click", function () { saveIoKey(app); });
        g("io-key-generate").addEventListener("click", function () {
            const bytes = new Uint8Array(16);
            crypto.getRandomValues(bytes);
            const hex = Array.from(bytes).map(function (b) { return b.toString(16).padStart(2, "0").toUpperCase(); }).join("");
            g("io-key-new-input").value = hex;
            g("io-key-edit-status").textContent = "";
        });
        g("io-key-edit-modal").addEventListener("click", function (e) {
            if (e.target === this) closeIoKeyEditModal();
        });

        g("io-sniff-cancel").addEventListener("click", async function () {
            await cancelSniff();
            g("io-key-sniff-modal").classList.remove("open");
        });
        g("io-key-sniff-modal").addEventListener("click", async function (e) {
            if (e.target === this) { await cancelSniff(); this.classList.remove("open"); }
        });
        g("io-sniff-start").addEventListener("click", function () { startSniff(app); });
        g("io-sniff-retry").addEventListener("click", function () { startSniff(app); });
        g("io-sniff-use-key").addEventListener("click", function () { useSniffedKey(app); });

        initLearnKey(app);
        initPairDeviceKey(app);
        initSendKey();
        loadIoKey(app);
    }

    function initReboot() {
        var btn = g("reboot-btn");
        if (!btn) return;
        btn.addEventListener("click", function () {
            if (!confirm(t("confirm.reboot"))) return;
            window.MiOpenApi.postJson("/api/reboot", {})
                .then(function () {
                    var rebootingToast = showToast(t("toast.rebooting"), "info", 60000);
                    var deadline = Date.now() + 60000;
                    function poll() {
                        if (Date.now() > deadline) {
                            if (rebootingToast) rebootingToast._dismiss();
                            showToast(t("toast.device-offline"), "error");
                            return;
                        }
                        fetch("/api/devices?" + Date.now(), { cache: "no-store" })
                            .then(function (r) {
                                if (r.ok) {
                                    if (rebootingToast) rebootingToast._dismiss();
                                    showToast(t("toast.device-online"), "success");
                                } else { setTimeout(poll, 2000); }
                            })
                            .catch(function () { setTimeout(poll, 2000); });
                    }
                    setTimeout(poll, 5000);
                })
                .catch(function () { showToast(t("toast.reboot-failed"), "error"); });
        });
    }

    function init(app) {

        app.elements.fallbackEnabled        = g("fallback-enabled");
        app.elements.fallbackRetriesBoot    = g("fallback-retries-boot");
        app.elements.fallbackRetriesRunning = g("fallback-retries-running");
        app.elements.fallbackTimeout        = g("fallback-timeout");
        app.elements.fallbackApSsid         = g("fallback-ap-ssid");
        app.elements.fallbackStatus         = g("fallback-status");
        app.loadFallbackConfig = function () { return loadFallbackConfig(app); };
        app.saveFallbackConfig = function () { return saveFallbackConfig(app); };
        g("fallback-save").addEventListener("click", function () { app.saveFallbackConfig(); });
        loadFallbackConfig(app);
        fallbackStatusTimer = setInterval(function () { pollFallbackStatus(app); }, 15000);
        mqttStatusTimer = setInterval(function () { pollMqttStatus(app); }, 3000);

        app.elements.mqttEnabledInput  = g("mqtt-enabled");
        app.elements.mqttEnabledToggle = g("mqtt-enabled-toggle");
        var mqttSaveInFlight = false;
        app.elements.mqttEnabledToggle.addEventListener("click", function () {
            if (mqttSaveInFlight) return;
            app.elements.mqttEnabledInput.checked = !app.elements.mqttEnabledInput.checked;
            var on = app.elements.mqttEnabledInput.checked;
            app.elements.mqttEnabledToggle.classList.toggle("on", on);
            mqttSaveInFlight = true;
            updateMqttConfig(app, true).finally(function () { mqttSaveInFlight = false; });
        });

        app.elements.wifiSsidInput     = g("wifi-ssid");
        app.elements.wifiPasswordInput = g("wifi-password");
        app.elements.wifiStatus        = g("wifi-config-status");
        app.loadWifiConfig  = function () { return loadWifiConfig(app); };
        app.saveWifiConfig  = function () { return saveWifiConfig(app); };
        g("wifi-config-save").addEventListener("click", function () { app.saveWifiConfig(); });
        loadWifiConfig(app);

        initNetworkConfig(app);
        initIoConfig(app);
        initAccessPassword(app);
        initIoKey(app);
        initReboot();

        var betaCheckbox = g("update-channel-beta");
        var betaLabel = g("update-channel-label");
        var betaToggle = g("update-channel-toggle");
        if (betaCheckbox && betaToggle) {
            var savedChannel = localStorage.getItem("updateChannel") || "stable";
            betaCheckbox.checked = savedChannel === "beta";
            betaLabel.textContent = betaCheckbox.checked ? t("button.include-beta") : t("button.stable-only");
            betaToggle.classList.toggle("on", betaCheckbox.checked);

            betaToggle.addEventListener("click", function () {
                betaCheckbox.checked = !betaCheckbox.checked;
                var channel = betaCheckbox.checked ? "beta" : "stable";
                localStorage.setItem("updateChannel", channel);
                betaLabel.textContent = betaCheckbox.checked ? t("button.include-beta") : t("button.stable-only");
                betaToggle.classList.toggle("on", betaCheckbox.checked);
            });
        }

        (function () {

            var scanBtn     = g("wifi-scan-btn");
            var scanResults = g("wifi-scan-results");
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
                scanBtn.textContent = t("button.scanning");
                scanResults.style.display = "none";
                scanResults.innerHTML = "";

                window.MiOpenApi.requestJson("/api/wifi/scan?" + Date.now())
                    .then(function (networks) {
                        if (!networks.length) {
                            scanResults.innerHTML = "<div style='padding:8px;color:#888;font-size:.9em;'>" + t("status.no-networks-found") + "</div>";
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
                    .catch(function () { showToast(t("toast.wifi-scan-failed"), "error"); })
                    .finally(function () {
                        scanBtn.disabled = false;
                        scanBtn.textContent = t("button.scan");
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

        var reloadInProgress = false;
        function reloadSettings() {
            stopSniffPoll();
            if (learnCountdownTimer) { clearInterval(learnCountdownTimer); learnCountdownTimer = null; }
            if (pairDeviceCountdownTimer) { clearInterval(pairDeviceCountdownTimer); pairDeviceCountdownTimer = null; }

            if (reloadInProgress) return;
            reloadInProgress = true;
            Promise.allSettled([
                loadWifiConfig(app),
                loadFallbackConfig(app),
                loadNetworkConfig(app),
                loadMqttConfig(app),
                loadIoConfig(app),
                loadIoKey(app),
                app.loadSyslogConfig ? app.loadSyslogConfig() : Promise.resolve()
            ]).finally(function () { reloadInProgress = false; });
        }
        document.addEventListener("viewShown", function (e) {
            if (e.detail && e.detail.view === "settings") reloadSettings();
        });

        app.uploadDevices = function () {
            return uploadSelectedFile(
                app, app.elements.devicesFileInput, "/api/upload/devices",
                t("toast.no-devices-file"), t("toast.devices-uploaded"),
                async function () {
                    await app.fetchAndDisplayDevices();
                    await app.fetchAndDisplayRemotes();
                }
            );
        };
        app.uploadRemotes = function () {
            return uploadSelectedFile(
                app, app.elements.remotesFileInput, "/api/upload/remotes",
                t("toast.no-remotes-file"), t("toast.remotes-uploaded"),
                function () { return app.fetchAndDisplayRemotes(); }
            );
        };
    }

    function onKeyCaptured(key) {
        if (!key) return;
        stopSniffPoll();
        g("io-sniff-countdown-row").style.display = "none";
        g("io-sniff-captured-key").textContent = key;
        g("io-sniff-result-row").style.display = "";
        g("io-sniff-use-key").dataset.key = key;
        g("io-sniff-use-key").style.display = "";
    }

    let learnCountdownTimer = null;
    let learnSecondsLeft = 0;

    function resetLearnModal() {
        if (learnCountdownTimer) { clearInterval(learnCountdownTimer); learnCountdownTimer = null; }
        g("io-learn-countdown-row").style.display = "none";
        g("io-learn-result-row").style.display = "none";
        g("io-learn-status").textContent = "";
        g("io-learn-start").style.display = "";
        g("io-learn-retry").style.display = "none";
        g("io-learn-use-key").style.display = "none";
    }

    async function cancelLearn() {
        if (learnCountdownTimer) { clearInterval(learnCountdownTimer); learnCountdownTimer = null; }
        try { await window.MiOpenApi.postJson("/api/learn/stop", {}); } catch (e) { /* ignore */ }
    }

    async function startLearn(app) {
        g("io-learn-start").style.display = "none";
        g("io-learn-retry").style.display = "none";
        g("io-learn-result-row").style.display = "none";
        g("io-learn-status").textContent = "";
        g("io-learn-countdown-row").style.display = "";

        learnSecondsLeft = 120;
        g("io-learn-countdown").textContent = learnSecondsLeft;

        try { await window.MiOpenApi.postJson("/api/learn/start", {}); } catch (e) {
            g("io-learn-status").textContent = e.message || "Failed to start.";
            g("io-learn-countdown-row").style.display = "none";
            g("io-learn-start").style.display = "";
            return;
        }

        learnCountdownTimer = setInterval(function () {
            learnSecondsLeft--;
            g("io-learn-countdown").textContent = learnSecondsLeft;
            if (learnSecondsLeft <= 0) {
                clearInterval(learnCountdownTimer); learnCountdownTimer = null;
                g("io-learn-countdown-row").style.display = "none";
                g("io-learn-status").textContent = t("status.no-key-received");
                g("io-learn-retry").style.display = "";
            }
        }, 1000);
    }

    function onLearnActive(remaining_s) {
        if (remaining_s !== undefined) {
            learnSecondsLeft = remaining_s;
            g("io-learn-countdown").textContent = learnSecondsLeft;
        }
    }

    function onLearnFailed() {
        if (learnCountdownTimer) { clearInterval(learnCountdownTimer); learnCountdownTimer = null; }
        g("io-learn-countdown-row").style.display = "none";
        g("io-learn-status").textContent = t("status.handshake-failed");
        g("io-learn-retry").style.display = "";
    }

    function onLearnKey(key) {
        if (!key) return;
        if (learnCountdownTimer) { clearInterval(learnCountdownTimer); learnCountdownTimer = null; }
        g("io-learn-countdown-row").style.display = "none";
        g("io-learn-captured-key").textContent = key;
        g("io-learn-result-row").style.display = "";
        g("io-learn-use-key").dataset.key = key;
        g("io-learn-use-key").style.display = "";
        g("io-learn-status").textContent = t("status.key-received");
    }

    function initLearnKey(app) {
        g("io-key-learn").addEventListener("click", function () {
            resetLearnModal();
            g("io-key-learn-modal").classList.add("open");
        });

        g("io-learn-cancel").addEventListener("click", async function () {
            await cancelLearn();
            g("io-key-learn-modal").classList.remove("open");
        });
        g("io-key-learn-modal").addEventListener("click", async function (e) {
            if (e.target === this) { await cancelLearn(); this.classList.remove("open"); }
        });
        g("io-learn-start").addEventListener("click", function () { startLearn(app); });
        g("io-learn-retry").addEventListener("click", function () { startLearn(app); });
        g("io-learn-use-key").addEventListener("click", async function () {
            const key = this.dataset.key;
            await cancelLearn();
            g("io-key-learn-modal").classList.remove("open");
            openIoKeyEditModal(app, key);
        });
    }

    let pairDeviceCountdownTimer = null;
    let pairDeviceSecondsLeft = 0;

    function resetPairDeviceModal() {
        if (pairDeviceCountdownTimer) { clearInterval(pairDeviceCountdownTimer); pairDeviceCountdownTimer = null; }
        g("io-pair-device-countdown-row").style.display = "none";
        g("io-pair-device-result-row").style.display = "none";
        g("io-pair-device-status").textContent = "";
        g("io-pair-device-start").style.display = "";
        g("io-pair-device-retry").style.display = "none";
        g("io-pair-device-use-key").style.display = "none";
    }

    async function cancelPairDevice() {
        if (pairDeviceCountdownTimer) { clearInterval(pairDeviceCountdownTimer); pairDeviceCountdownTimer = null; }
        try { await window.MiOpenApi.postJson("/api/pair-device/stop", {}); } catch (e) { /* ignore */ }
    }

    async function startPairDevice(app) {
        g("io-pair-device-start").style.display = "none";
        g("io-pair-device-retry").style.display = "none";
        g("io-pair-device-result-row").style.display = "none";
        g("io-pair-device-status").textContent = "";
        g("io-pair-device-countdown-row").style.display = "";

        pairDeviceSecondsLeft = 120;
        g("io-pair-device-countdown").textContent = pairDeviceSecondsLeft;

        try { await window.MiOpenApi.postJson("/api/pair-device/start", {}); } catch (e) {
            g("io-pair-device-status").textContent = t("status.failed-to-start", { message: e.message || e });
            g("io-pair-device-countdown-row").style.display = "none";
            g("io-pair-device-start").style.display = "";
            return;
        }

        pairDeviceCountdownTimer = setInterval(function () {
            pairDeviceSecondsLeft--;
            g("io-pair-device-countdown").textContent = pairDeviceSecondsLeft;
            if (pairDeviceSecondsLeft <= 0) {
                clearInterval(pairDeviceCountdownTimer); pairDeviceCountdownTimer = null;
                g("io-pair-device-countdown-row").style.display = "none";
                g("io-pair-device-status").textContent = t("status.no-key-received");
                g("io-pair-device-retry").style.display = "";
            }
        }, 1000);
    }

    function onPairDeviceActive(remaining_s) {
        if (remaining_s !== undefined) {
            pairDeviceSecondsLeft = remaining_s;
            g("io-pair-device-countdown").textContent = pairDeviceSecondsLeft;
        }
    }

    function onPairDeviceFailed() {
        if (pairDeviceCountdownTimer) { clearInterval(pairDeviceCountdownTimer); pairDeviceCountdownTimer = null; }
        g("io-pair-device-countdown-row").style.display = "none";
        g("io-pair-device-status").textContent = t("status.handshake-failed");
        g("io-pair-device-retry").style.display = "";
    }

    function onPairDeviceKey(key) {
        if (!key) return;
        if (pairDeviceCountdownTimer) { clearInterval(pairDeviceCountdownTimer); pairDeviceCountdownTimer = null; }
        g("io-pair-device-countdown-row").style.display = "none";
        g("io-pair-device-captured-key").textContent = key;
        g("io-pair-device-result-row").style.display = "";
        g("io-pair-device-use-key").dataset.key = key;
        g("io-pair-device-use-key").style.display = "";
        g("io-pair-device-status").textContent = t("status.key-received");
    }

    function initPairDeviceKey(app) {
        g("io-key-pair-device").addEventListener("click", function () {
            resetPairDeviceModal();
            g("io-key-pair-device-modal").classList.add("open");
        });
        g("io-pair-device-cancel").addEventListener("click", async function () {
            await cancelPairDevice();
            g("io-key-pair-device-modal").classList.remove("open");
        });
        g("io-key-pair-device-modal").addEventListener("click", async function (e) {
            if (e.target === this) { await cancelPairDevice(); this.classList.remove("open"); }
        });
        g("io-pair-device-start").addEventListener("click", function () { startPairDevice(app); });
        g("io-pair-device-retry").addEventListener("click", function () { startPairDevice(app); });
        g("io-pair-device-use-key").addEventListener("click", async function () {
            const key = this.dataset.key;
            await cancelPairDevice();
            g("io-key-pair-device-modal").classList.remove("open");
            openIoKeyEditModal(app, key);
        });
    }

    let sendKeyTimer = null;
    let sendKeySeconds = 0;

    function resetSendKeyModal() {
        if (sendKeyTimer) { clearInterval(sendKeyTimer); sendKeyTimer = null; }
        g("io-send-key-countdown-row").style.display = "none";
        g("io-send-key-status").textContent = "";
        g("io-send-key-start").style.display = "";
    }

    async function startSendKey() {
        if (sendKeyTimer) { clearInterval(sendKeyTimer); sendKeyTimer = null; }
        g("io-send-key-start").style.display = "none";
        g("io-send-key-status").textContent = "";
        g("io-send-key-countdown-row").style.display = "";
        sendKeySeconds = 30;
        g("io-send-key-countdown").textContent = sendKeySeconds;
        try { await window.MiOpenApi.postJson("/api/send-key/start", {}); } catch (e) {
            g("io-send-key-status").textContent = t("status.failed-to-start", { message: e.message || e });
            g("io-send-key-countdown-row").style.display = "none";
            g("io-send-key-start").style.display = "";
            return;
        }
        sendKeyTimer = setInterval(function () {
            sendKeySeconds--;
            g("io-send-key-countdown").textContent = sendKeySeconds;
            if (sendKeySeconds <= 0) {
                clearInterval(sendKeyTimer); sendKeyTimer = null;
                g("io-send-key-countdown-row").style.display = "none";
                g("io-send-key-status").textContent = t("status.session-ended");
                g("io-send-key-start").style.display = "";
            }
        }, 1000);
    }

    function onSendKeyDone() {
        if (sendKeyTimer) { clearInterval(sendKeyTimer); sendKeyTimer = null; }
        g("io-send-key-countdown-row").style.display = "none";
        g("io-send-key-status").textContent = t("status.session-ended");
        g("io-send-key-start").style.display = "";
    }

    function initSendKey() {
        g("io-key-send-key").addEventListener("click", function () {
            resetSendKeyModal();
            g("io-send-key-modal").classList.add("open");
        });
        g("io-send-key-cancel").addEventListener("click", async function () {
            if (sendKeyTimer) { clearInterval(sendKeyTimer); sendKeyTimer = null; }
            try { await window.MiOpenApi.postJson("/api/send-key/stop", {}); } catch (e) { /* ignore */ }
            g("io-send-key-modal").classList.remove("open");
        });
        g("io-send-key-modal").addEventListener("click", async function (e) {
            if (e.target !== this) return;
            if (sendKeyTimer) { clearInterval(sendKeyTimer); sendKeyTimer = null; }
            try { await window.MiOpenApi.postJson("/api/send-key/stop", {}); } catch (e) { /* ignore */ }
            this.classList.remove("open");
        });
        g("io-send-key-start").addEventListener("click", function () { startSendKey(); });

        window.MiOpenApi.requestJson("/api/somfy/credentials").then(function(r){var e=document.getElementById("somfy-email");if(e&&r.email)e.value=r.email;}).catch(function(){});
        var _se=document.getElementById("somfy-save");
        if(_se)_se.addEventListener("click",async function(){var e=(document.getElementById("somfy-email")||{}).value||"",p=(document.getElementById("somfy-password")||{}).value||"",s=document.getElementById("somfy-status");if(!e||!p){if(s)s.textContent="Enter both email and password.";return;}try{var r=await window.MiOpenApi.postJson("/api/somfy/credentials",{email:e,password:p});if(s)s.textContent=r.success?"Saved.":(r.message||"Failed.");}catch(x){if(s)s.textContent="Error saving credentials.";}});
    }

    window.MiOpenSettings = {
        init: init,
        refreshIoKey: function () { loadIoKey(window.MiOpenApp); },
        refreshDynamicLabels: function () {
            var app = window.MiOpenApp;
            if (!app) return;
            // Re-apply status strings that are rendered dynamically from JS
            var mqttEl = g("mqtt-conn-status");
            if (mqttEl && mqttEl._status) updateMqttStatusEl(mqttEl._status);
            // Beta toggle label
            var betaCheckbox = g("update-channel-beta");
            var betaLabel = g("update-channel-label");
            if (betaCheckbox && betaLabel) {
                betaLabel.textContent = betaCheckbox.checked ? t("button.include-beta") : t("button.stable-only");
            }
            // Show/hide button on io-key
            var showBtn = g("io-key-show");
            if (showBtn && app.elements && app.elements.ioKeyDisplay) {
                showBtn.textContent = app.elements.ioKeyDisplay.type === "password" ? t("button.show") : t("button.hide");
            }
        },
        onKeyCaptured: onKeyCaptured,
        onLearnActive: onLearnActive, onLearnFailed: onLearnFailed, onLearnKey: onLearnKey,
        onPairDeviceActive: onPairDeviceActive, onPairDeviceFailed: onPairDeviceFailed, onPairDeviceKey: onPairDeviceKey,
        onSendKeyDone: onSendKeyDone
    };

})();

// ── Syslog (settings section) ─────────────────────────────────────────────────
(function () {
    var _pingInterval = null;

    function updateSyslogStatusEl(status, extra) {
        var el = document.getElementById("syslog-conn-status");
        if (!el) return;
        var map = {
            checking:    { text: "\u25cc Checking…",  color: "#888" },
            reachable:   { text: "\u25cf Reachable",   color: "#27ae60" },
            unreachable: { text: "\u25cb Unreachable", color: "#e74c3c" },
            disabled:    { text: "",              color: "" },
            error:       { text: "\u2715 Error",       color: "#e74c3c" },
        };
        var s = map[status] || { text: "\u25cb " + status, color: "#888" };
        if (status === "reachable" && extra != null) s.text += " (" + extra + "\u00a0ms)";
        el.textContent = s.text;
        el.style.color = s.color;
    }

    function stopPingPolling() {
        if (_pingInterval) { clearInterval(_pingInterval); _pingInterval = null; }
    }

    async function pingAndUpdateStatus() {
        updateSyslogStatusEl("checking");
        try {
            var r = await window.MiOpenApi.postJson("/api/syslog/ping");
            if (r.reachable) {
                updateSyslogStatusEl("reachable", r.latency_ms);
            } else {
                updateSyslogStatusEl("unreachable");
            }
        } catch (e) {
            updateSyslogStatusEl("error");
        }
    }

    function startPingPolling(app) {
        stopPingPolling();
        if (!app.elements.syslogEnabledInput.checked ||
            !app.elements.syslogServerInput.value.trim()) return;
        pingAndUpdateStatus();
        _pingInterval = setInterval(pingAndUpdateStatus, 30 * 60 * 1000);
    }

    async function loadSyslogConfig(app) {
        try {
            const cfg = await window.MiOpenApi.requestJson("/api/syslog");
            app.elements.syslogEnabledInput.checked   = !!cfg.enabled;
            app.elements.syslogEnabledInput.dispatchEvent(new Event('change'));
            app.elements.syslogServerInput.value       = cfg.server    || "";
            app.elements.syslogPortInput.value         = cfg.port      || "514";
            app.elements.syslogFacilityInput.value     = cfg.facility  != null ? cfg.facility : "1";
            app.elements.syslogMinLevelInput.value     = cfg.min_level != null ? String(cfg.min_level) : "7";
            app.elements.syslogIdInput.value           = cfg.id        || "";
            if (cfg.enabled && cfg.server) startPingPolling(app);
            else { stopPingPolling(); updateSyslogStatusEl("disabled"); }
        } catch (error) {
            console.error("Error fetching syslog config", error);
        }
    }

    async function updateSyslogConfig(app) {
        try {
            const payload = {
                enabled:   app.elements.syslogEnabledInput.checked,
                server:    app.elements.syslogServerInput.value.trim(),
                port:      parseInt(app.elements.syslogPortInput.value, 10) || 514,
                facility:  parseInt(app.elements.syslogFacilityInput.value, 10),
                min_level: parseInt(app.elements.syslogMinLevelInput.value, 10)
            };
            const id = app.elements.syslogIdInput.value.trim();
            if (id) payload.id = id;
            const result = await window.MiOpenApi.postJson("/api/syslog", payload);
            if (!result.success) { showToast(result.message || "Syslog save failed.", "error"); return; }
            showToast(result.message || "Syslog settings saved.", "success");
            if (payload.enabled && payload.server) startPingPolling(app);
            else { stopPingPolling(); updateSyslogStatusEl("disabled"); }
        } catch (error) {
            showToast("Error saving syslog config: " + (error.message || error), "error");
        }
    }

    function init(app) {
        app.elements.syslogIdInput = document.getElementById("syslog-id");
        app.loadSyslogConfig   = function () { return loadSyslogConfig(app); };
        app.updateSyslogConfig = function () { return updateSyslogConfig(app); };

        app.elements.syslogEnabledInput.addEventListener("change", function () {
            if (!this.checked) { stopPingPolling(); updateSyslogStatusEl("disabled"); }
        });

        document.addEventListener("viewShown", function (e) {
            if (e.detail && e.detail.view === "settings") {
                if (app.elements.syslogEnabledInput.checked &&
                    app.elements.syslogServerInput.value.trim()) {
                    startPingPolling(app);
                }
            }
        });
    }

    window.MiOpenSyslog = { init: init };
})();