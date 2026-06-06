(function () {
    var GITHUB_API = "https://api.github.com/repos/rspaargaren/io-rts-esp32/releases";
    var STORAGE_CHANNEL = "updateChannel";
    var STORAGE_DISMISSED = "updateDismissed";

    function getChannel() {
        return localStorage.getItem(STORAGE_CHANNEL) || "stable";
    }

    function parseVersion(tag) {
        return /^v\d+\.\d+\.\d+/.test(tag) ? tag : null;
    }

    function isNewer(latest, current) {
        if (!latest || !current) return false;
        var l = latest.replace(/^v/, "").split("-")[0].split(".").map(Number);
        var c = current.replace(/^v/, "").split("-")[0].split(".").map(Number);
        for (var i = 0; i < 3; i++) {
            var li = l[i] || 0, ci = c[i] || 0;
            if (li > ci) return true;
            if (li < ci) return false;
        }
        return false;
    }

    function findAssetUrl(release, suffix) {
        var assets = release.assets || [];
        for (var i = 0; i < assets.length; i++) {
            if (assets[i].name.indexOf(suffix) !== -1) {
                return assets[i].browser_download_url;
            }
        }
        return null;
    }

    function showBanner(release, currentVersion) {
        var banner = document.getElementById("update-banner");
        var versionEl = document.getElementById("update-banner-version");
        var linkEl = document.getElementById("update-banner-link");
        var startBtn = document.getElementById("update-start-btn");
        var dismissBtn = document.getElementById("update-dismiss");
        if (!banner) return;

        var tag = release.tag_name;
        var label = release.prerelease ? "Beta " + tag : "Stable " + tag;
        versionEl.textContent = "Update available: " + label;
        linkEl.href = release.html_url;
        banner.style.display = "";

        dismissBtn.onclick = function () {
            localStorage.setItem(STORAGE_DISMISSED, tag);
            banner.style.display = "none";
        };

        startBtn.onclick = function () {
            var firmwareUrl = findAssetUrl(release, "-firmware.bin");
            var webUrl = findAssetUrl(release, "-web.bin");
            if (!firmwareUrl || !webUrl) {
                alert("Release assets not found. Please update manually.");
                return;
            }
            runUpdate(firmwareUrl, webUrl, currentVersion);
        };
    }

    function setProgress(label, value) {
        var prog = document.getElementById("update-progress");
        var labelEl = document.getElementById("update-progress-label");
        var bar = document.getElementById("update-progress-bar");
        if (!prog) return;
        prog.style.display = "";
        if (labelEl) labelEl.textContent = label;
        if (bar && value !== null) bar.value = value;
    }

    function pollUntilOnline(deadline, onOnline, onTimeout) {
        if (Date.now() > deadline) { onTimeout(); return; }
        fetch("/api/info?" + Date.now(), { cache: "no-store" })
            .then(function (r) { if (r.ok) onOnline(); else setTimeout(function () { pollUntilOnline(deadline, onOnline, onTimeout); }, 3000); })
            .catch(function () { setTimeout(function () { pollUntilOnline(deadline, onOnline, onTimeout); }, 3000); });
    }

    function uploadBinary(url, endpoint, otaKey, onProgress) {
        return fetch(url)
            .then(function (r) {
                if (!r.ok) throw new Error("Download failed: " + r.status);
                return r.arrayBuffer();
            })
            .then(function (buf) {
                onProgress(50);
                return fetch(endpoint, {
                    method: "POST",
                    headers: {
                        "Content-Type": "application/octet-stream",
                        "X-OTA-Key": otaKey
                    },
                    body: buf
                });
            })
            .then(function (r) { return r.json(); })
            .then(function (d) {
                onProgress(100);
                if (d.status !== "rebooting") throw new Error(d.message || "Unexpected response");
            });
    }

    function runUpdate(firmwareUrl, webUrl, currentVersion) {
        var startBtn = document.getElementById("update-start-btn");
        var dismissBtn = document.getElementById("update-dismiss");
        if (startBtn) startBtn.disabled = true;
        if (dismissBtn) dismissBtn.style.display = "none";

        var otaKey = (document.getElementById("ota-key-display") || {}).value || "";

        setProgress("Downloading firmware…", 0);

        uploadBinary(firmwareUrl, "/api/ota", otaKey, function (p) {
            setProgress(p < 100 ? "Uploading firmware…" : "Firmware uploaded, rebooting…", p);
        })
        .then(function () {
            setProgress("Waiting for device to come back online…", null);
            return new Promise(function (resolve, reject) {
                pollUntilOnline(Date.now() + 60000, resolve, function () {
                    reject(new Error("Timed out waiting for device after firmware update"));
                });
            });
        })
        .then(function () {
            setProgress("Downloading web filesystem…", 0);
            return uploadBinary(webUrl, "/api/ota/web", otaKey, function (p) {
                setProgress(p < 100 ? "Uploading web filesystem…" : "Web uploaded, rebooting…", p);
            });
        })
        .then(function () {
            setProgress("Waiting for device to come back online…", null);
            return new Promise(function (resolve, reject) {
                pollUntilOnline(Date.now() + 60000, resolve, function () {
                    reject(new Error("Timed out waiting for device after web update"));
                });
            });
        })
        .then(function () {
            setProgress("Update complete! Reloading…", 100);
            setTimeout(function () { location.reload(); }, 1500);
        })
        .catch(function (err) {
            setProgress("Update failed: " + err.message, null);
            if (startBtn) startBtn.disabled = false;
            if (dismissBtn) dismissBtn.style.display = "";
        });
    }

    function checkForUpdates(currentVersion) {
        if (!parseVersion(currentVersion)) return;
        var dismissed = localStorage.getItem(STORAGE_DISMISSED);
        var channel = getChannel();

        fetch(GITHUB_API, { cache: "no-store" })
            .then(function (r) { return r.json(); })
            .then(function (releases) {
                var candidates = releases.filter(function (r) {
                    if (r.draft) return false;
                    if (r.prerelease && channel === "stable") return false;
                    return !!parseVersion(r.tag_name);
                });
                if (!candidates.length) return;
                var latest = candidates[0];
                if (latest.tag_name === dismissed) return;
                if (isNewer(latest.tag_name, currentVersion)) {
                    showBanner(latest, currentVersion);
                }
            })
            .catch(function () {});
    }

    function init(currentVersion) {
        checkForUpdates(currentVersion);
    }

    window.MiOpenUpdater = { init: init, getChannel: getChannel };
})();
