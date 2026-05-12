(function () {
    async function runAction(app, deviceId, action, value) {
        const payload = { deviceId: deviceId, action: action };
        if (value !== undefined) payload.value = value;
        const result = await window.MiOpenApi.postJson("/api/action", payload);
        app.logStatus(result.message || ("Action " + action + " sent."));
    }

    function updateDeviceFill(deviceId, percent) {
        const deviceEl = document.querySelector('.device[data-id="' + deviceId + '"]');
        if (!deviceEl) return;
        deviceEl.style.background = "linear-gradient(to top, var(--color-input) " +
            percent + "%, var(--color-accent3) " + percent + "%)";
    }

    function createDeviceButton(label, className, onClick) {
        const button = document.createElement("button");
        button.textContent = label;
        button.classList.add("btn", className);
        button.addEventListener("click", onClick);
        return button;
    }

    async function fetchAndDisplayDevices(app) {
        const deviceList = app.elements.deviceList;
        const deviceSelect = app.elements.commandDeviceSelect;

        try {
            const devices = await window.MiOpenApi.requestJson("/api/devices");
            app.state.devicesCache = devices;

            deviceList.textContent = "";
            deviceSelect.textContent = "";

            if (devices.length === 0) {
                app.logStatus("No devices found.");
                const listItem = document.createElement("li");
                listItem.textContent = "No devices available.";
                deviceList.appendChild(listItem);
                return;
            }

            devices.forEach(function (device) {
                const nameSpan = document.createElement("span");
                nameSpan.textContent = device.name;

                const typeBadge = document.createElement("span");
                typeBadge.textContent = device.type_name || "";
                typeBadge.className = "type-badge";

                const listItem = document.createElement("li");
                listItem.classList.add("device");
                listItem.dataset.id = device.id;
                listItem.appendChild(nameSpan);
                listItem.appendChild(typeBadge);

                listItem.appendChild(createDeviceButton("↑", "open", function () {
                    runAction(app, device.id, "open").catch(function (e) { app.logStatus(e.message, true); });
                }));
                listItem.appendChild(createDeviceButton("■", "stop", function () {
                    runAction(app, device.id, "stop").catch(function (e) { app.logStatus(e.message, true); });
                }));
                listItem.appendChild(createDeviceButton("↓", "down", function () {
                    runAction(app, device.id, "close").catch(function (e) { app.logStatus(e.message, true); });
                }));

                listItem.appendChild(createDeviceButton(
                    app.i18nText("button.edit", "edit"), "edit",
                    async function () {
                        try {
                            const fresh = await window.MiOpenApi.requestJson("/api/devices");
                            app.state.devicesCache = fresh;
                            const fd = fresh.find(function (d) { return d.id === device.id; });
                            if (fd) device = fd;
                        } catch (e) {
                            app.logStatus("Error refreshing: " + e.message, true);
                        }

                        app.openPopup(
                            app.i18nText("popup.edit_device_title", "Edit Device"),
                            app.i18nText("popup.adjust_name", "Adjust the name:"),
                            [
                                app.i18nText("popup.info_id", "ID: {value}").replace("{value}", device.id),
                                "Type: " + (device.type_name || "?"),
                                "Manufacturer: " + (device.manufacturer || "?"),
                                app.i18nText("popup.info_position", "Position: {value}%").replace("{value}", String(device.position >= 0 ? device.position : "unknown")),
                                "Low power: " + (device.is_low_power ? "yes" : "no")
                            ],
                            [""],
                            {
                                showSave: true,
                                showInput: true,
                                btnShowDelete: false,
                                defaultValue: device.name,
                                onSave: async function (newName) {
                                    if (!newName.trim() || newName === device.name) return;
                                    try {
                                        const result = await window.MiOpenApi.postJson("/api/action", {
                                            deviceId: device.id,
                                            action: "rename",
                                            value: newName.trim()
                                        });
                                        app.logStatus(result.message || "Device renamed.");
                                        await fetchAndDisplayDevices(app);
                                    } catch (e) {
                                        app.logStatus("Error renaming: " + e.message, true);
                                    }
                                }
                            }
                        );
                    }
                ));

                deviceList.appendChild(listItem);
                if (device.position >= 0) updateDeviceFill(device.id, device.position);

                const option = document.createElement("option");
                option.value = device.id;
                option.textContent = device.name;
                deviceSelect.appendChild(option);
            });

            app.logStatus("Device list updated.");
        } catch (error) {
            app.logStatus("Error fetching devices: " + error.message, true);
        }
    }

    async function sendCommand(app) {
        const selectedDeviceId = app.elements.commandDeviceSelect.value;
        const commandStr = app.elements.commandInput.value.trim();
        if (!selectedDeviceId) { app.logStatus("Please select a device.", true); return; }
        if (!commandStr) { app.logStatus("Please enter a command.", true); return; }

        app.logStatus('Sending "' + commandStr + '" to ' + selectedDeviceId + "...");
        try {
            const parts = commandStr.split(" ");
            const action = parts[0];
            const value = parts[1] !== undefined ? Number(parts[1]) : undefined;
            const result = await window.MiOpenApi.postJson("/api/action", {
                deviceId: selectedDeviceId,
                action: action,
                value: value
            });
            app.logStatus(result.success ? ("OK: " + (result.message || "Done.")) : ("Failed: " + (result.message || "?")), !result.success);
        } catch (error) {
            app.logStatus("Error: " + error.message, true);
        }
    }

    function init(app) {
        app.fetchAndDisplayDevices = function () { return fetchAndDisplayDevices(app); };
        app.sendCommand = function () { return sendCommand(app); };
        app.updateDeviceFill = updateDeviceFill;
    }

    window.MiOpenDevices = { init: init };
})();
