(function () {
    var _app;
    var _mode = "add";
    var _capturedId = "";
    var _editLinkedDevices = [];
    var _countdownTimer = null;
    var _captureActive = false;

    // ── helpers ────────────────────────────────────────────────────────────────

    function findLinkedDeviceNames(remote) {
        if (!remote.devices || !remote.devices.length) return "—";
        return remote.devices.map(function (ref) {
            var d = (_app.state.devicesCache || []).find(function (x) { return x.id === ref || x.name === ref; });
            return d ? d.name : ref;
        }).join(", ");
    }

    async function fetchAndDisplayRemotes(app) {
        var tbody = document.querySelector("#remote-table tbody");
        try {
            var remotes = await window.MiOpenApi.requestJson("/api/remotes");
            tbody.textContent = "";
            var countEl = document.getElementById("remotes-count");
            if (countEl) countEl.textContent = remotes.length ? "(" + remotes.length + ")" : "";
            if (!remotes.length) {
                var row = document.createElement("tr");
                var cell = document.createElement("td");
                cell.colSpan = 3;
                cell.textContent = "No remotes yet.";
                row.appendChild(cell);
                tbody.appendChild(row);
                return;
            }
            var frag = document.createDocumentFragment();
            remotes.forEach(function (remote) {
                var row = document.createElement("tr");

                var idCell = document.createElement("td");
                var tag = document.createElement("div");
                tag.className = "remote-id";
                tag.textContent = remote.id;
                idCell.appendChild(tag);

                var devCell = document.createElement("td");
                devCell.textContent = findLinkedDeviceNames(remote);

                var actCell = document.createElement("td");
                var editBtn = document.createElement("button");
                editBtn.className = "btn edit bg";
                editBtn.textContent = app.i18nText("button.edit", "Edit");
                editBtn.addEventListener("click", (function (r) {
                    return function () { openWizard(app, "edit", r.id, r.devices || []); };
                })(remote));
                actCell.appendChild(editBtn);

                row.appendChild(idCell);
                row.appendChild(devCell);
                row.appendChild(actCell);
                frag.appendChild(row);
            });
            tbody.appendChild(frag);
        } catch (e) {
            app.logStatus("Error fetching remotes: " + e.message, "error");
        }
    }

    // ── capture helpers ────────────────────────────────────────────────────────

    function cancelCapture() {
        _captureActive = false;
        if (_countdownTimer) { clearInterval(_countdownTimer); _countdownTimer = null; }
        window.MiOpenApi.postJson("/api/remote/capture/cancel", {}).catch(function () {});
    }

    function startCapture() {
        var statusEl = document.getElementById("arm-capture-status");
        var countdown = document.getElementById("arm-countdown");
        var retryBtn  = document.getElementById("arm-retry-btn");
        retryBtn.style.display = "none";
        statusEl.textContent = "Press any button on the remote…";
        statusEl.style.color = "";

        var seconds = 30;
        countdown.textContent = seconds + "s";

        if (_countdownTimer) clearInterval(_countdownTimer);
        _captureActive = true;
        _countdownTimer = setInterval(function () {
            seconds--;
            countdown.textContent = seconds + "s";
            if (seconds <= 0) {
                clearInterval(_countdownTimer);
                _countdownTimer = null;
                _captureActive = false;
                statusEl.textContent = "No remote detected. Retry or enter ID manually.";
                statusEl.style.color = "var(--red)";
                retryBtn.style.display = "";
            }
        }, 1000);

        window.MiOpenApi.postJson("/api/remote/capture/start", {}).catch(function (e) {
            if (_countdownTimer) clearInterval(_countdownTimer);
            _countdownTimer = null;
            _captureActive = false;
            statusEl.textContent = "Start failed: " + e.message;
            statusEl.style.color = "var(--red)";
            retryBtn.style.display = "";
        });
    }

    // ── step management ────────────────────────────────────────────────────────

    var STEPS = ["arm-step-choose", "arm-step-capture", "arm-step-manual", "arm-step-devices"];

    function showStep(id) {
        STEPS.forEach(function (s) { document.getElementById(s).style.display = "none"; });
        document.getElementById(id).style.display = "";
    }

    // ── device checklist ───────────────────────────────────────────────────────

    function buildDeviceList(linkedDevices) {
        var container = document.getElementById("arm-device-list");
        container.innerHTML = "";
        var devices = (_app.state.devicesCache || []).filter(function (d) { return !d.inactive; });
        if (!devices.length) {
            container.textContent = "No devices paired yet.";
            return;
        }
        devices.forEach(function (device) {
            var label = document.createElement("label");
            label.className = "arm-device-item";
            var cb = document.createElement("input");
            cb.type = "checkbox";
            cb.value = device.id;
            cb.className = "arm-device-cb";
            if (linkedDevices.indexOf(device.id) !== -1 || linkedDevices.indexOf(device.name) !== -1) {
                cb.checked = true;
            }
            var span = document.createElement("span");
            span.textContent = device.name;
            label.appendChild(cb);
            label.appendChild(span);
            container.appendChild(label);
        });
    }

    // ── wizard open/close ──────────────────────────────────────────────────────

    function openWizard(app, mode, remoteId, linkedDevices) {
        _app = app;
        _mode = mode || "add";
        _capturedId = remoteId || "";
        _editLinkedDevices = linkedDevices || [];

        document.getElementById("arm-modal-title").textContent = mode === "edit" ? "Edit Remote" : "Add Remote";
        document.getElementById("arm-devices-error").textContent = "";
        document.getElementById("arm-save-btn").disabled = false;

        var deleteBtn = document.getElementById("arm-delete-btn");
        var backBtn   = document.getElementById("arm-devices-back");
        var idLabel   = document.getElementById("arm-remote-id-label");

        if (mode === "edit") {
            deleteBtn.style.display = "";
            backBtn.style.display = "none";
            idLabel.textContent = "Remote: " + remoteId;
            idLabel.style.display = "";
            buildDeviceList(_editLinkedDevices);
            showStep("arm-step-devices");
        } else {
            deleteBtn.style.display = "none";
            backBtn.style.display = "";
            idLabel.style.display = "none";
            showStep("arm-step-choose");
        }

        document.getElementById("arm-modal").classList.add("open");
    }

    function closeWizard() {
        cancelCapture();
        _capturedId = "";
        document.getElementById("arm-modal").classList.remove("open");
    }

    // ── save ───────────────────────────────────────────────────────────────────

    async function saveWizard() {
        var err    = document.getElementById("arm-devices-error");
        var saveBtn = document.getElementById("arm-save-btn");
        err.textContent = "";
        saveBtn.disabled = true;

        var checkedIds = Array.from(document.querySelectorAll(".arm-device-cb:checked")).map(function (cb) { return cb.value; });

        try {
            if (_mode === "add") {
                for (var i = 0; i < checkedIds.length; i++) {
                    var r = await window.MiOpenApi.postJson("/api/action", { action: "linkRemote", remoteId: _capturedId, deviceId: checkedIds[i] });
                    if (!r.success) throw new Error(r.message || "Link failed for " + checkedIds[i]);
                }
                showToast("Remote " + _capturedId + " added.", "success");
            } else {
                // Remove from all, then re-link to checked
                await window.MiOpenApi.postJson("/api/action", { action: "unlinkRemote", remoteId: _capturedId });
                var linkErrors = [];
                for (var j = 0; j < checkedIds.length; j++) {
                    try {
                        await window.MiOpenApi.postJson("/api/action", { action: "linkRemote", remoteId: _capturedId, deviceId: checkedIds[j] });
                    } catch (linkErr) {
                        linkErrors.push(checkedIds[j]);
                    }
                }
                if (linkErrors.length > 0) {
                    err.textContent = "Some devices could not be linked: " + linkErrors.join(", ") + ". The remote may be partially linked.";
                    saveBtn.disabled = false;
                    await fetchAndDisplayRemotes(_app);
                    return;
                }
                showToast("Remote " + _capturedId + " updated.", "success");
            }
            closeWizard();
            await fetchAndDisplayRemotes(_app);
        } catch (e) {
            err.textContent = "Error: " + e.message;
            saveBtn.disabled = false;
        }
    }

    // ── event wiring ───────────────────────────────────────────────────────────

    function initEvents() {
        var modal = document.getElementById("arm-modal");

        modal.addEventListener("click", function (e) { if (e.target === modal) closeWizard(); });

        // Step 1: choose
        document.getElementById("arm-btn-capture").addEventListener("click", function () {
            showStep("arm-step-capture");
            startCapture();
        });
        document.getElementById("arm-btn-cancel").addEventListener("click", closeWizard);
        document.getElementById("arm-btn-manual").addEventListener("click", function () {
            document.getElementById("arm-manual-input").value = "";
            document.getElementById("arm-manual-error").textContent = "";
            showStep("arm-step-manual");
        });

        // Step 2 capture
        document.getElementById("arm-skip-btn").addEventListener("click", function () {
            cancelCapture();
            document.getElementById("arm-manual-input").value = "";
            document.getElementById("arm-manual-error").textContent = "";
            showStep("arm-step-manual");
        });
        document.getElementById("arm-retry-btn").addEventListener("click", function () {
            document.getElementById("arm-retry-btn").style.display = "none";
            startCapture();
        });

        // Step 2 manual
        document.getElementById("arm-manual-back").addEventListener("click", function () {
            showStep("arm-step-choose");
        });
        document.getElementById("arm-manual-next").addEventListener("click", function () {
            var val = document.getElementById("arm-manual-input").value.trim().toUpperCase();
            var errEl = document.getElementById("arm-manual-error");
            if (!/^[0-9A-F]{6}$/.test(val)) {
                errEl.textContent = "Must be exactly 6 hex characters (0–9, A–F).";
                return;
            }
            errEl.textContent = "";
            _capturedId = val;
            document.getElementById("arm-remote-id-label").textContent = "Remote: " + val;
            document.getElementById("arm-remote-id-label").style.display = "";
            document.getElementById("arm-devices-error").textContent = "";
            buildDeviceList([]);
            showStep("arm-step-devices");
        });

        // Step 3 devices
        document.getElementById("arm-devices-back").addEventListener("click", function () {
            showStep("arm-step-choose");
        });
        document.getElementById("arm-save-btn").addEventListener("click", saveWizard);

        document.getElementById("arm-delete-btn").addEventListener("click", async function () {
            if (!confirm("Remove remote " + _capturedId + " from all devices?")) return;
            try {
                await window.MiOpenApi.postJson("/api/action", { action: "deleteRemote", remoteId: _capturedId });
                showToast("Remote removed.", "success");
                closeWizard();
                await fetchAndDisplayRemotes(_app);
            } catch (e) {
                document.getElementById("arm-devices-error").textContent = "Error: " + e.message;
            }
        });
    }

    // ── WS callbacks ───────────────────────────────────────────────────────────

    function onRemoteSeen(id) {
        if (!_captureActive) return;
        cancelCapture();
        var statusEl = document.getElementById("arm-capture-status");
        statusEl.textContent = "Remote detected: " + id;
        statusEl.style.color = "var(--green)";
        _capturedId = id;
        setTimeout(function () {
            document.getElementById("arm-remote-id-label").textContent = "Remote: " + id;
            document.getElementById("arm-remote-id-label").style.display = "";
            document.getElementById("arm-devices-error").textContent = "";
            buildDeviceList([]);
            showStep("arm-step-devices");
        }, 800);
    }

    function onCaptureTimeout() {
        if (!_captureActive) return;
        cancelCapture();
        var statusEl = document.getElementById("arm-capture-status");
        statusEl.textContent = "No remote detected. Retry or enter ID manually.";
        statusEl.style.color = "var(--red)";
        document.getElementById("arm-retry-btn").style.display = "";
    }

    // ── init ───────────────────────────────────────────────────────────────────

    function init(app) {
        _app = app;
        initEvents();
        app.fetchAndDisplayRemotes = function () { return fetchAndDisplayRemotes(app); };
        app.openAddRemotePopup = function () { openWizard(app, "add"); };
        window.editRemote = function (remoteId, linkedDevices) { openWizard(app, "edit", remoteId, linkedDevices || []); };
    }

    window.MiOpenRemotes = { init: init, onRemoteSeen: onRemoteSeen, onCaptureTimeout: onCaptureTimeout };
})();
