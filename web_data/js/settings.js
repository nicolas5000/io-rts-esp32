(function () {
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
