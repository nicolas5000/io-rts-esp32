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

    function pollUntilOnline(app, deadline) {
        if (Date.now() > deadline) {
            finishWithError(app, "Timed out waiting for device to come back online.");
            return;
        }
        fetch("/api/devices?" + Date.now(), { cache: "no-store" })
            .then(function (r) {
                if (r.ok) {
                    setStatus(app, "Done — device is back online.", "green");
                    app.elements.otaUploadButton.disabled = false;
                } else {
                    setTimeout(function () { pollUntilOnline(app, deadline); }, POLL_INTERVAL);
                }
            })
            .catch(function () {
                setTimeout(function () { pollUntilOnline(app, deadline); }, POLL_INTERVAL);
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
                pollUntilOnline(app, Date.now() + POLL_TIMEOUT);
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
            modal.style.display = "flex";
            newInput.focus();
        }

        function closeModal() {
            modal.style.display = "none";
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
                if (d.ok) {
                    status.textContent = "Key updated.";
                    status.style.color = "green";
                    var display = document.getElementById("ota-key-display");
                    if (display) display.value = newKey;
                    setTimeout(closeModal, 1200);
                } else {
                    status.textContent = "Error: " + (d.msg || "unknown");
                    status.style.color = "red";
                }
            })
            .catch(function () {
                status.textContent = "Network error.";
                status.style.color = "red";
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
            modal.style.display = "flex";
            newInput.focus();
        }

        function closeModal() {
            modal.style.display = "none";
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
                if (d.ok) {
                    status.textContent = "Key saved. Reboot the device to apply.";
                    status.style.color = "green";
                    var display = document.getElementById("io-key-display");
                    if (display) display.value = newKey;
                    setTimeout(closeModal, 2500);
                } else {
                    status.textContent = "Error: " + (d.msg || "unknown");
                    status.style.color = "red";
                }
            })
            .catch(function () {
                status.textContent = "Network error.";
                status.style.color = "red";
            })
            .finally(function () {
                confirmBtn.disabled = false;
            });
        });
    }

    function init(app) {
        fetchAndDisplayKey();
        initKeyModal();
        fetchAndDisplayIoKey();
        initIoKeyModal();

        if (app.elements.otaProgress) {
            app.elements.otaProgress.style.display = "none";
        }

        app.uploadFirmware = function () { uploadFirmware(app); };
    }

    window.MiOpenOta = { init: init };
})();
