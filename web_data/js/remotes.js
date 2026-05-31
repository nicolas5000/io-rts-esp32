(function () {
    function findLinkedDeviceNames(app, remote) {
        if (!remote.devices || !remote.devices.length) return "0 devices";
        return remote.devices.map(function (deviceRef) {
            const device = app.state.devicesCache.find(function (d) {
                return d.id === deviceRef || d.name === deviceRef;
            });
            return device ? device.name : deviceRef;
        }).join(", ");
    }

    async function fetchAndDisplayRemotes(app) {
        const tbody = document.querySelector("#remote-table tbody");
        try {
            const remotes = await window.MiOpenApi.requestJson("/api/remotes");
            tbody.textContent = "";

            if (!remotes.length) {
                const row = document.createElement("tr");
                const cell = document.createElement("td");
                cell.colSpan = 4;
                cell.textContent = "No remotes available.";
                row.appendChild(cell);
                tbody.appendChild(row);
                return;
            }

            const fragment = document.createDocumentFragment();
            remotes.forEach(function (remote) {
                const row = document.createElement("tr");

                const idCell = document.createElement("td");
                const idTag = document.createElement("div");
                idTag.className = "remote-id";
                idTag.textContent = remote.id;
                idCell.appendChild(idTag);

                const nameCell = document.createElement("td");
                nameCell.textContent = remote.name;

                const devicesCell = document.createElement("td");
                devicesCell.textContent = findLinkedDeviceNames(app, remote);

                const actionsCell = document.createElement("td");
                const editButton = document.createElement("button");
                editButton.className = "btn edit bg";
                editButton.textContent = app.i18nText("button.edit", "Edit");
                editButton.addEventListener("click", function () {
                    editRemote(app, remote.id, remote.name);
                });
                actionsCell.appendChild(editButton);

                row.appendChild(idCell);
                row.appendChild(nameCell);
                row.appendChild(devicesCell);
                row.appendChild(actionsCell);
                fragment.appendChild(row);
            });
            tbody.appendChild(fragment);
        } catch (error) {
            app.logStatus("Error fetching remotes: " + error.message, "error");
        }
    }

    function editRemote(app, remoteId, remoteName) {
        app.openPopup(
            app.i18nText("popup.edit_remote_title", "Edit Remote"),
            app.i18nText("popup.adjust_name_devices", "Adjust the name/devices:"),
            [
                app.i18nText("popup.remote_id", "Remote ID: {value}").replace("{value}", remoteId),
                app.i18nText("popup.remote_name", "Name: {value}").replace("{value}", remoteName)
            ],
            [""],
            {
                showInput: true,
                showDevicePopup: true,
                btnShowDelete: true,
                showSave: true,
                defaultValue: remoteName,
                pairLabel: app.i18nText("popup.link_unlink_remote", "Link / Unlink the remote"),
                pairBtnName: app.i18nText("button.link", "Link"),
                unpairBtnName: app.i18nText("button.unlink", "Unlink"),
                onPair: async function (deviceId) {
                    try {
                        const result = await window.MiOpenApi.postJson("/api/action", {
                            action: "linkRemote",
                            remoteId: remoteId,
                            deviceId: deviceId
                        });
                        app.logStatus(result.message || "Remote linked.", "debug");
                        await fetchAndDisplayRemotes(app);
                    } catch (e) { app.logStatus("Error linking: " + e.message, "error"); }
                },
                onUnpair: async function (deviceId) {
                    try {
                        const result = await window.MiOpenApi.postJson("/api/action", {
                            action: "unlinkRemote",
                            remoteId: remoteId,
                            deviceId: deviceId
                        });
                        app.logStatus(result.message || "Remote unlinked.", "debug");
                        await fetchAndDisplayRemotes(app);
                    } catch (e) { app.logStatus("Error unlinking: " + e.message, "error"); }
                },
                onDelete: async function () {
                    try {
                        const result = await window.MiOpenApi.postJson("/api/action", {
                            action: "deleteRemote",
                            remoteId: remoteId
                        });
                        app.logStatus(result.message || "Remote removed.", "debug");
                        await fetchAndDisplayRemotes(app);
                    } catch (e) { app.logStatus("Error removing remote: " + e.message, "error"); }
                }
            }
        );
    }

    function openAddRemotePopup(app) {
        app.openPopup(
            app.i18nText("popup.add_remote_title", "Add Remote"),
            app.i18nText("popup.remote_id_label", "Remote ID:"),
            [app.i18nText("popup.here_add_remote", "Add your remote here")],
            [""],
            {
                showSave: true,
                showInput: true,
                showTiming: true,
                btnShowDelete: false,
                btnShowCancel: false,
                timingLabel: app.i18nText("popup.remote_name_label", "Remote Name:"),
                onSave: async function (addr, newName) {
                    const id = addr.trim().toUpperCase();
                    if (!id) { app.logStatus("Please provide a remote ID.", "error"); return; }
                    if (!newName || !newName.trim()) { app.logStatus("Please provide a remote name.", "error"); return; }
                    try {
                        const result = await window.MiOpenApi.postJson("/api/action", {
                            action: "addRemote",
                            remoteId: id,
                            remoteName: newName.trim()
                        });
                        app.logStatus(result.message || "Remote added.", "debug");
                        await fetchAndDisplayRemotes(app);
                    } catch (e) { app.logStatus("Error adding remote: " + e.message, "error"); }
                }
            }
        );
    }

    function init(app) {
        app.fetchAndDisplayRemotes = function () { return fetchAndDisplayRemotes(app); };
        app.openAddRemotePopup = function () { openAddRemotePopup(app); };
        window.editRemote = function (remoteId, remoteName) { editRemote(app, remoteId, remoteName); };
    }

    window.MiOpenRemotes = { init: init };
})();
