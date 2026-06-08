(function () {
    var POLL_INTERVAL = 2000;
    var POLL_TIMEOUT = 60000;

    function setStatus(app, msg, color) {
        app.elements.otaStatus.textContent = msg;
        app.elements.otaStatus.style.color = color || "";
    }

    function finishWithError(app, msg) {
        setStatus(app, msg, "red");
        app.elements.otaUploadButton.disabled = false;
    }

    function pollUntilOnline(onDone, onTimeout, deadline) {
        if (Date.now() > deadline) { onTimeout(); return; }
        fetch("/api/devices?" + Date.now(), { cache: "no-store" })
            .then(function (r) {
                if (r.ok) { onDone(); }
                else { setTimeout(function () { pollUntilOnline(onDone, onTimeout, deadline); }, POLL_INTERVAL); }
            })
            .catch(function () {
                setTimeout(function () { pollUntilOnline(onDone, onTimeout, deadline); }, POLL_INTERVAL);
            });
    }

    function uploadFirmware(app) {
        var file = app.elements.otaFileInput.files[0];
        var keyDisplay = document.getElementById("ota-key-display");
        var key = keyDisplay ? keyDisplay.value.trim() : "";

        if (!file) { setStatus(app, "Please select a firmware .bin file.", "red"); return; }
        if (!key)  { setStatus(app, "OTA key not loaded yet.", "red"); return; }

        app.elements.otaUploadButton.disabled = true;
        app.elements.otaProgress.style.display = "";
        app.elements.otaProgress.value = 0;
        setStatus(app, "Uploading…");

        var xhr = new XMLHttpRequest();
        xhr.open("POST", "/api/ota");
        xhr.setRequestHeader("X-OTA-Key", key);
        xhr.setRequestHeader("Content-Type", "application/octet-stream");

        xhr.upload.onprogress = function (e) {
            if (e.lengthComputable) {
                app.elements.otaProgress.value = Math.round(e.loaded / e.total * 100);
            }
        };

        xhr.onload = function () {
            if (xhr.status === 200) {
                app.elements.otaProgress.value = 100;
                setStatus(app, "Rebooting…");
                pollUntilOnline(
                    function () { setStatus(app, "Done — device is back online.", "green"); app.elements.otaUploadButton.disabled = false; },
                    function () { finishWithError(app, "Timed out waiting for device to come back online."); },
                    Date.now() + POLL_TIMEOUT
                );
            } else if (xhr.status === 401) {
                finishWithError(app, "Wrong OTA key (401 Unauthorized).");
            } else if (xhr.status === 400) {
                finishWithError(app, "Upload failed: empty file.");
            } else {
                finishWithError(app, "OTA failed (" + xhr.status + "): " + (xhr.responseText || "unknown error"));
            }
        };

        xhr.onerror = function () {
            finishWithError(app, "Network error during upload.");
        };

        xhr.send(file);
    }

    function fetchAndDisplayKey() {
        var display = document.getElementById("ota-key-display");
        if (!display) return;
        fetch("/api/ota/key?" + Date.now(), { cache: "no-store" })
            .then(function (r) { return r.json(); })
            .then(function (d) {
                if (d.key) display.value = d.key;
            })
            .catch(function () {});
    }

    function initKeyModal() {
        var modal    = document.getElementById("ota-key-modal");
        var editBtn  = document.getElementById("ota-key-edit");
        var cancelBtn= document.getElementById("ota-key-cancel");
        var confirmBtn=document.getElementById("ota-key-confirm");
        var newInput = document.getElementById("ota-key-new");
        var status   = document.getElementById("ota-key-modal-status");

        if (!modal || !editBtn) return;

        function openModal() {
            newInput.value = "";
            status.textContent = "";
            status.style.color = "";
            modal.classList.add("open");
            newInput.focus();
        }

        function closeModal() {
            modal.classList.remove("open");
        }

        editBtn.addEventListener("click", openModal);
        cancelBtn.addEventListener("click", closeModal);

        modal.addEventListener("click", function (e) {
            if (e.target === modal) closeModal();
        });

        confirmBtn.addEventListener("click", function () {
            var newKey = newInput.value.trim();
            if (!newKey) {
                status.textContent = "Please enter a key.";
                status.style.color = "red";
                return;
            }
            confirmBtn.disabled = true;
            status.textContent = "Saving…";
            status.style.color = "";

            fetch("/api/ota/key", {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify({ key: newKey })
            })
            .then(function (r) { return r.json(); })
            .then(function (d) {
                if (d.success) {
                    status.textContent = "Key updated.";
                    status.style.color = "green";
                    var display = document.getElementById("ota-key-display");
                    if (display) display.value = newKey;
                    showToast("OTA key updated.", "success");
                    setTimeout(closeModal, 1000);
                } else {
                    status.textContent = "Error: " + (d.message || "unknown");
                    status.style.color = "red";
                    showToast("Error: " + (d.message || "unknown"), "error");
                }
            })
            .catch(function () {
                status.textContent = "Network error.";
                status.style.color = "red";
                showToast("Network error.", "error");
            })
            .finally(function () {
                confirmBtn.disabled = false;
            });
        });
    }

    function fetchAndDisplayIoKey() {
        var display = document.getElementById("io-key-display");
        if (!display) return;
        fetch("/api/io/key?" + Date.now(), { cache: "no-store" })
            .then(function (r) { return r.json(); })
            .then(function (d) {
                if (d.key) display.value = d.key;
            })
            .catch(function () {});
    }

    function initIoKeyModal() {
        var modal     = document.getElementById("io-key-modal");
        var editBtn   = document.getElementById("io-key-edit");
        var cancelBtn = document.getElementById("io-key-cancel");
        var confirmBtn= document.getElementById("io-key-confirm");
        var newInput  = document.getElementById("io-key-new");
        var status    = document.getElementById("io-key-modal-status");

        if (!modal || !editBtn) return;

        function openModal() {
            newInput.value = "";
            status.textContent = "";
            status.style.color = "";
            modal.classList.add("open");
            newInput.focus();
        }

        function closeModal() {
            modal.classList.remove("open");
        }

        editBtn.addEventListener("click", openModal);
        cancelBtn.addEventListener("click", closeModal);
        modal.addEventListener("click", function (e) {
            if (e.target === modal) closeModal();
        });

        confirmBtn.addEventListener("click", function () {
            var newKey = newInput.value.trim().toUpperCase();
            if (newKey.length !== 32) {
                status.textContent = "Key must be exactly 32 hex characters.";
                status.style.color = "red";
                return;
            }
            confirmBtn.disabled = true;
            status.textContent = "Saving…";
            status.style.color = "";

            fetch("/api/io/key", {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify({ key: newKey })
            })
            .then(function (r) { return r.json(); })
            .then(function (d) {
                if (d.success) {
                    status.textContent = "Key saved. Reboot to apply.";
                    status.style.color = "green";
                    var display = document.getElementById("io-key-display");
                    if (display) display.value = newKey;
                    showToast("IO key saved. Reboot to apply.", "success");
                    setTimeout(closeModal, 1500);
                } else {
                    status.textContent = "Error: " + (d.message || "unknown");
                    status.style.color = "red";
                    showToast("Error: " + (d.message || "unknown"), "error");
                }
            })
            .catch(function () {
                status.textContent = "Network error.";
                status.style.color = "red";
                showToast("Network error.", "error");
            })
            .finally(function () {
                confirmBtn.disabled = false;
            });
        });
    }

    function uploadWebUi(app) {
        var file = document.getElementById("ota-web-file") && document.getElementById("ota-web-file").files[0];
        var progress = document.getElementById("ota-web-progress");
        var status = document.getElementById("ota-web-status");
        var btn = document.getElementById("ota-web-upload");
        var keyDisplay = document.getElementById("ota-key-display");
        var key = keyDisplay ? keyDisplay.value.trim() : "";

        function setErr(msg) { status.textContent = msg; status.style.color = "red"; btn.disabled = false; }

        if (!file) { setErr("Please select a web UI .bin file."); return; }
        if (!key)  { setErr("OTA key not loaded yet."); return; }

        btn.disabled = true;
        progress.style.display = "";
        progress.value = 0;
        status.textContent = "Uploading…";
        status.style.color = "";

        var xhr = new XMLHttpRequest();
        xhr.open("POST", "/api/ota/web");
        xhr.setRequestHeader("X-OTA-Key", key);
        xhr.setRequestHeader("Content-Type", "application/octet-stream");

        xhr.upload.onprogress = function (e) {
            if (e.lengthComputable) progress.value = Math.round(e.loaded / e.total * 100);
        };

        xhr.onload = function () {
            if (xhr.status === 200) {
                progress.value = 100;
                status.textContent = "Rebooting…";
                status.style.color = "";
                pollUntilOnline(
                    function () { status.textContent = "Done — device is back online."; status.style.color = "green"; btn.disabled = false; },
                    function () { setErr("Timed out waiting for device to come back online."); },
                    Date.now() + POLL_TIMEOUT
                );
            } else if (xhr.status === 401) {
                setErr("Wrong OTA key (401 Unauthorized).");
            } else {
                setErr("Upload failed (" + xhr.status + "): " + (xhr.responseText || "unknown error"));
            }
        };

        xhr.onerror = function () { setErr("Network error during upload."); };
        xhr.send(file);
    }

    function init(app) {
        fetchAndDisplayKey();
        initKeyModal();

        if (app.elements.otaProgress) {
            app.elements.otaProgress.style.display = "none";
        }

        var webUploadBtn = document.getElementById("ota-web-upload");
        if (webUploadBtn) {
            webUploadBtn.addEventListener("click", function () { uploadWebUi(app); });
        }

        app.uploadFirmware = function () { uploadFirmware(app); };
    }

    window.MiOpenOta = { init: init };
})();
