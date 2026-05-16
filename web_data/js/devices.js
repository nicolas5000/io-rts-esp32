(function () {
    async function runAction(app, deviceId, action, value) {
        const payload = { deviceId: deviceId, action: action };
        if (value !== undefined) payload.value = value;
        const result = await window.MiOpenApi.postJson("/api/action", payload);
        app.logStatus(result.message || ("Action " + action + " sent."), "debug");
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

        try {
            const devices = await window.MiOpenApi.requestJson("/api/devices");
            app.state.devicesCache = devices;

            deviceList.textContent = "";

            if (devices.length === 0) {
                app.logStatus("No devices found.", "info");
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
                    runAction(app, device.id, "open").catch(function (e) { app.logStatus(e.message, "error"); });
                }));
                listItem.appendChild(createDeviceButton("■", "stop", function () {
                    runAction(app, device.id, "stop").catch(function (e) { app.logStatus(e.message, "error"); });
                }));
                listItem.appendChild(createDeviceButton("↓", "down", function () {
                    runAction(app, device.id, "close").catch(function (e) { app.logStatus(e.message, "error"); });
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
                            app.logStatus("Error refreshing: " + e.message, "error");
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
                                        app.logStatus(result.message || "Device renamed.", "debug");
                                        await fetchAndDisplayDevices(app);
                                    } catch (e) {
                                        app.logStatus("Error renaming: " + e.message, "error");
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

            app.logStatus("Device list updated.", "info");
        } catch (error) {
            app.logStatus("Error fetching devices: " + error.message, "error");
        }
    }

    function init(app) {
        app.fetchAndDisplayDevices = function () { return fetchAndDisplayDevices(app); };
        app.updateDeviceFill = updateDeviceFill;
    }

    window.MiOpenDevices = { init: init };
})();
