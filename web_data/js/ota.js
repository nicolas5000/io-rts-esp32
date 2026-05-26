(function () {
    var STORAGE_KEY = "ota_key";
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
        var key = app.elements.otaKeyInput.value.trim();

        if (!file) { setStatus(app, "Please select a firmware .bin file.", "red"); return; }
        if (!key)  { setStatus(app, "Please enter the OTA key.", "red"); return; }

        localStorage.setItem(STORAGE_KEY, key);

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

    function init(app) {
        var saved = localStorage.getItem(STORAGE_KEY);
        if (saved && app.elements.otaKeyInput) {
            app.elements.otaKeyInput.value = saved;
        }
        if (app.elements.otaKeyInput) {
            app.elements.otaKeyInput.addEventListener("input", function () {
                localStorage.setItem(STORAGE_KEY, app.elements.otaKeyInput.value.trim());
            });
        }
        if (app.elements.otaProgress) {
            app.elements.otaProgress.style.display = "none";
        }
        app.uploadFirmware = function () { uploadFirmware(app); };
    }

    window.MiOpenOta = { init: init };
})();
