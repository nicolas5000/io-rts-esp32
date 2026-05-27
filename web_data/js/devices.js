(function () {
    var FAV_PREFIX = "fav_pos_";

    function getFavPos(id) {
        var v = localStorage.getItem(FAV_PREFIX + id);
        return v !== null ? parseInt(v, 10) : null;
    }

    function setFavPos(id, pos) {
        localStorage.setItem(FAV_PREFIX + id, pos);
    }

    function getDeviceGroup(device) {
        var SHUTTER = ["ROLLER_SHUTTER", "BLIND", "DUAL_SHUTTER", "AWNING",
                       "HORIZONTAL_AWNING", "EXTERNAL_VENETIAN_BLIND",
                       "CURTAIN_TRACK", "SWINGING_SHUTTER"];
        var VENETIAN = ["VENETIAN_BLIND", "LOUVRE_BLIND"];
        var WINDOW = ["WINDOW_OPENER"];
        var GATE = ["GARAGE_OPENER", "GATE_OPENER", "ROLLING_DOOR_OPENER"];
        // Normalize "Roller shutter" → "ROLLER_SHUTTER" to match the API's human-readable names
        var t = (device.type_name || "UNKNOWN").toUpperCase().replace(/[\s\-]+/g, "_");
        if (SHUTTER.indexOf(t) !== -1) return "shutter";
        if (VENETIAN.indexOf(t) !== -1) return "venetian";
        if (WINDOW.indexOf(t) !== -1) return "window";
        if (GATE.indexOf(t) !== -1) return "gate";
        if (t === "ON_OFF_SWITCH") return "switch";
        if (t === "LIGHT") return device.subtype === 58 ? "switch" : "dimmer";
        return "readonly";
    }

    async function runAction(app, deviceId, action, value) {
        const payload = { deviceId: deviceId, action: action };
        if (value !== undefined) payload.value = value;
        const result = await window.MiOpenApi.postJson("/api/action", payload);
        app.logStatus(result.message || ("Action " + action + " sent."), "debug");
    }

    function updateDeviceFill(deviceId, percent) {
        const deviceEl = document.querySelector('.device[data-id="' + deviceId + '"]');
        if (!deviceEl) return;
        const fill = 100 - percent;
        deviceEl.style.background = "linear-gradient(to top, var(--color-input) " +
            fill + "%, var(--color-accent3) " + fill + "%)";
        var slider = deviceEl.querySelector('input[data-slider="position"]');
        if (slider) slider.value = percent;
    }

    function createDeviceButton(label, className, onClick) {
        const button = document.createElement("button");
        button.textContent = label;
        button.classList.add("btn", className);
        button.addEventListener("click", onClick);
        return button;
    }

    function createTextButton(label, onClick) {
        var btn = document.createElement("button");
        btn.textContent = label;
        btn.className = "btn-text";
        btn.addEventListener("click", onClick);
        return btn;
    }

    function createSlider(app, device, action, initialValue) {
        var wrapper = document.createElement("div");
        wrapper.className = "slider-row";
        var lbl = document.createElement("span");
        lbl.className = "slider-label";
        lbl.textContent = action === "tilt"
            ? app.i18nText("label.tilt", "Tilt")
            : action === "dim"
                ? app.i18nText("label.dim", "Dim")
                : app.i18nText("label.position", "Pos");
        var slider = document.createElement("input");
        slider.type = "range";
        slider.min = "0";
        slider.max = "100";
        slider.value = (initialValue !== undefined && initialValue >= 0) ? initialValue : 0;
        slider.className = "device-slider";
        if (action === "position") slider.dataset.slider = "position";
        slider.addEventListener("change", function () {
            runAction(app, device.id, action, parseInt(slider.value, 10))
                .catch(function (e) { app.logStatus(e.message, "error"); });
        });
        wrapper.appendChild(lbl);
        wrapper.appendChild(slider);
        return wrapper;
    }

    function createFavButton(app, device) {
        var fav = getFavPos(device.id);
        var btn = document.createElement("button");
        btn.className = "btn-fav" + (fav !== null ? " has-favorite" : "");
        btn.textContent = fav !== null ? "★" : "☆";
        btn.title = fav !== null
            ? (app.i18nText("button.favorite", "Favorite") + ": " + fav + "%")
            : app.i18nText("popup.no_favorite_set", "No favorite set");
        btn.dataset.favDevice = device.id;
        btn.addEventListener("click", function () {
            var pos = getFavPos(device.id);
            if (pos === null) {
                app.logStatus(app.i18nText("popup.no_favorite_set", "No favorite set — use Edit to set one."), "info");
                return;
            }
            runAction(app, device.id, "position", pos)
                .catch(function (e) { app.logStatus(e.message, "error"); });
        });
        return btn;
    }

    function updateFavButton(deviceId) {
        var btn = document.querySelector('button[data-fav-device="' + deviceId + '"]');
        if (!btn) return;
        var fav = getFavPos(deviceId);
        btn.className = "btn-fav" + (fav !== null ? " has-favorite" : "");
        btn.textContent = fav !== null ? "★" : "☆";
        btn.title = fav !== null ? ("Favorite: " + fav + "%") : "No favorite set";
    }

    function createButtonRow(buttons) {
        var row = document.createElement("div");
        row.className = "device-btn-row";
        buttons.forEach(function (b) { row.appendChild(b); });
        return row;
    }

    function buildControls(app, device, listItem, group) {
        if (group === "shutter" || group === "venetian" || group === "window") {
            listItem.appendChild(createButtonRow([
                createDeviceButton("↑", "open", function () {
                    runAction(app, device.id, "open").catch(function (e) { app.logStatus(e.message, "error"); });
                }),
                createDeviceButton("■", "stop", function () {
                    runAction(app, device.id, "stop").catch(function (e) { app.logStatus(e.message, "error"); });
                }),
                createDeviceButton("↓", "down", function () {
                    runAction(app, device.id, "close").catch(function (e) { app.logStatus(e.message, "error"); });
                }),
                createFavButton(app, device)
            ]));
            listItem.appendChild(createSlider(app, device, "position", device.position));
            if (group === "venetian") {
                listItem.appendChild(createSlider(app, device, "tilt", device.tilt));
            }

        } else if (group === "gate") {
            listItem.appendChild(createButtonRow([
                createDeviceButton("↑", "open", function () {
                    runAction(app, device.id, "open").catch(function (e) { app.logStatus(e.message, "error"); });
                }),
                createDeviceButton("↓", "down", function () {
                    runAction(app, device.id, "close").catch(function (e) { app.logStatus(e.message, "error"); });
                })
            ]));

        } else if (group === "switch" || group === "dimmer") {
            listItem.appendChild(createButtonRow([
                createTextButton(app.i18nText("button.on", "On"), function () {
                    runAction(app, device.id, "on").catch(function (e) { app.logStatus(e.message, "error"); });
                }),
                createTextButton(app.i18nText("button.off", "Off"), function () {
                    runAction(app, device.id, "off").catch(function (e) { app.logStatus(e.message, "error"); });
                })
            ]));
            if (group === "dimmer") {
                listItem.appendChild(createSlider(app, device, "dim", 0));
            }

        } else {
            var statusEl = document.createElement("span");
            statusEl.className = "device-status-only";
            statusEl.textContent = app.i18nText("label.status_only", "Status only");
            listItem.appendChild(statusEl);
        }
    }

    function buildEditPopup(app, device, group) {
        (async function () {
            var freshDevice = device;
            try {
                const fresh = await window.MiOpenApi.requestJson("/api/devices");
                app.state.devicesCache = fresh;
                const fd = fresh.find(function (d) { return d.id === device.id; });
                if (fd) freshDevice = fd;
            } catch (e) {
                app.logStatus("Error refreshing: " + e.message, "error");
            }

            var hasPosition = (group === "shutter" || group === "venetian" || group === "window");
            var favPos = getFavPos(freshDevice.id);
            var infoLines = [
                app.i18nText("popup.info_id", "ID: {value}").replace("{value}", freshDevice.id),
                "Type: " + (freshDevice.type_name || "?"),
                "Manufacturer: " + (freshDevice.manufacturer || "?"),
                app.i18nText("popup.info_position", "Position: {value}%")
                    .replace("{value}", String(freshDevice.position >= 0 ? freshDevice.position : "unknown")),
                "Low power: " + (freshDevice.is_low_power ? "yes" : "no")
            ];
            if (hasPosition && favPos !== null) {
                infoLines.push(
                    app.i18nText("popup.info_favorite", "Favorite: {value}%").replace("{value}", favPos)
                );
            }

            if (freshDevice.inactive) {
                // Inactive device: Re-activate (pair button) + Delete (delete button with confirm)
                app.openPopup(
                    app.i18nText("popup.edit_device_title", "Edit Device"),
                    app.i18nText("popup.device_is_inactive", "This device is inactive."),
                    infoLines,
                    [""],
                    {
                        showSave: false,
                        showInput: false,
                        btnShowDelete: true,
                        deleteInfo: app.i18nText("popup.delete_warning",
                            "Permanent removal. The physical device cannot be re-paired without factory reset."),
                        pairLabel: app.i18nText("popup.reactivate_label", "Restore device to active state:"),
                        pairBtnName: app.i18nText("button.reactivate", "Re-activate"),
                        onPair: async function () {
                            try {
                                await window.MiOpenApi.postJson("/api/action", {
                                    deviceId: freshDevice.id, action: "reactivateDevice"
                                });
                                app.logStatus(app.i18nText("popup.device_reactivated", "Device re-activated."), "info");
                                await fetchAndDisplayDevices(app);
                            } catch (e) {
                                app.logStatus("Error: " + e.message, "error");
                            }
                        },
                        onDelete: async function () {
                            try {
                                await window.MiOpenApi.postJson("/api/action", {
                                    deviceId: freshDevice.id, action: "deleteDevice"
                                });
                                app.logStatus(app.i18nText("popup.device_deleted", "Device permanently deleted."), "info");
                                await fetchAndDisplayDevices(app);
                            } catch (e) {
                                app.logStatus("Error: " + e.message, "error");
                            }
                        }
                    }
                );
                return;
            }

            var opts = {
                showSave: true,
                showInput: true,
                btnShowDelete: true,
                deleteBtnLabel: app.i18nText("button.deactivate", "Deactivate"),
                deleteInfo: app.i18nText("popup.deactivate_warning",
                    "The device will be kept as inactive and can be re-activated later."),
                defaultValue: freshDevice.name,
                onSave: async function (newName) {
                    if (!newName.trim() || newName === freshDevice.name) return;
                    try {
                        const result = await window.MiOpenApi.postJson("/api/action", {
                            deviceId: freshDevice.id,
                            action: "rename",
                            value: newName.trim()
                        });
                        app.logStatus(result.message || "Device renamed.", "debug");
                        await fetchAndDisplayDevices(app);
                    } catch (e) {
                        app.logStatus("Error renaming: " + e.message, "error");
                    }
                },
                onDelete: async function () {
                    try {
                        await window.MiOpenApi.postJson("/api/action", {
                            deviceId: freshDevice.id, action: "deactivateDevice"
                        });
                        app.logStatus(app.i18nText("popup.device_deactivated", "Device deactivated."), "info");
                        await fetchAndDisplayDevices(app);
                    } catch (e) {
                        app.logStatus("Error: " + e.message, "error");
                    }
                }
            };

            if (hasPosition) {
                var posText = freshDevice.position >= 0 ? freshDevice.position + "%" : "?";
                opts.pairLabel = app.i18nText("popup.favorite_position", "Favorite position");
                opts.pairBtnName = app.i18nText("button.set_favorite", "Set as ★") + " (" + posText + ")";
                opts.onPair = function () {
                    if (freshDevice.position >= 0) {
                        setFavPos(freshDevice.id, freshDevice.position);
                        updateFavButton(freshDevice.id);
                        app.logStatus(
                            app.i18nText("popup.favorite_position", "Favorite position") +
                            ": " + freshDevice.position + "%",
                            "info"
                        );
                    } else {
                        app.logStatus("Position unknown, cannot set favorite.", "error");
                    }
                };
            }

            app.openPopup(
                app.i18nText("popup.edit_device_title", "Edit Device"),
                app.i18nText("popup.adjust_name", "Adjust the name:"),
                infoLines,
                [""],
                opts
            );
        })();
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
                var group = getDeviceGroup(device);

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

                if (device.inactive) {
                    listItem.classList.add("inactive");
                    var inactiveBadge = document.createElement("span");
                    inactiveBadge.className = "type-badge inactive-badge";
                    inactiveBadge.textContent = app.i18nText("badge.inactive", "inactive");
                    listItem.appendChild(inactiveBadge);
                } else {
                    buildControls(app, device, listItem, group);
                }

                listItem.appendChild(createDeviceButton(
                    app.i18nText("button.edit", "edit"), "edit",
                    function () { buildEditPopup(app, device, group); }
                ));

                deviceList.appendChild(listItem);
                if (!device.inactive && device.position >= 0) updateDeviceFill(device.id, device.position);
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
