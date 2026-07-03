(function () {
    var GA = "https://api.github.com/repos/rspaargaren/io-rts-esp32/releases";
    var SC = "updateChannel";
    var SD = "updateDismissed";

    function getChannel() {
        return localStorage.getItem(SC) || "stable";
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

    function findAssetUrl(release, name) {
        var assets = release.assets || [];
        for (var i = 0; i < assets.length; i++) {
            if (assets[i].name === name) return assets[i].browser_download_url;
        }
        return null;
    }

    function showBanner(release, currentVersion, board) {
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
            localStorage.setItem(SD, tag);
            banner.style.display = "none";
        };

        startBtn.onclick = function () {
            var firmwareUrl = findAssetUrl(release, board + "-" + tag + "-firmware.bin");
            var webUrl      = findAssetUrl(release, board + "-" + tag + "-web.bin");
            if (!firmwareUrl || !webUrl) {
                alert("Release assets not found for board \"" + board + "\". Please update manually.");
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

    function otaFromUrl(url, type, otaKey, onStatus) {
        onStatus("Device downloading " + type + "…");
        return fetch("/api/ota/url", {
            method: "POST",
            headers: { "Content-Type": "application/json", "X-OTA-Key": otaKey },
            body: JSON.stringify({ url: url, type: type })
        })
        .then(function (r) {
            return r.text().then(function (text) {
                var d;
                try { d = JSON.parse(text); } catch (e) {
                    throw new Error("Server returned unexpected response: " + text.substring(0, 120));
                }
                if (d.status === "rebooting") { onStatus("Rebooting…"); return; }
                throw new Error(d.message || "Unexpected response");
            });
        });
    }

    function runUpdate(firmwareUrl, webUrl, currentVersion) {
        var startBtn = document.getElementById("update-start-btn");
        var dismissBtn = document.getElementById("update-dismiss");
        if (startBtn) startBtn.disabled = true;
        if (dismissBtn) dismissBtn.style.display = "none";

        setProgress("Starting update…", 0);

        fetch("/api/ota/key?" + Date.now(), { cache: "no-store" })
            .then(function (r) { return r.json(); })
            .then(function (d) { continueUpdate(d.key || "", firmwareUrl, webUrl); })
            .catch(function () { continueUpdate("", firmwareUrl, webUrl); });
    }

    function continueUpdate(otaKey, firmwareUrl, webUrl) {
        var startBtn = document.getElementById("update-start-btn");
        var dismissBtn = document.getElementById("update-dismiss");
        if (!otaKey) {
            setProgress("Failed: could not retrieve OTA key.", null);
            if (startBtn) startBtn.disabled = false;
            if (dismissBtn) dismissBtn.style.display = "";
            return;
        }

        otaFromUrl(firmwareUrl, "firmware", otaKey, function (s) { setProgress(s, 25); })
        .catch(function (err) {
            if (err instanceof TypeError) { setProgress("Rebooting…", null); return; }
            throw err;
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
            return otaFromUrl(webUrl, "web", otaKey, function (s) { setProgress(s, 75); });
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
            return fetch("/api/info?" + Date.now(), { cache: "no-store" })
                .then(function (r) { return r.json(); })
                .catch(function () { return {}; });
        })
        .then(function (info) {
            var ver = info.web_version ? " (web " + info.web_version + ")" : "";
            setProgress("Update complete" + ver + "! Reloading…", 100);
            setTimeout(function () { location.reload(); }, 1500);
        })
        .catch(function (err) {
            setProgress("Update failed: " + err.message, null);
            if (startBtn) startBtn.disabled = false;
            if (dismissBtn) dismissBtn.style.display = "";
        });
    }

    function checkForUpdates(currentVersion, board) {
        if (!parseVersion(currentVersion)) return;
        var dismissed = localStorage.getItem(SD);
        var channel = getChannel();

        return fetch(GA, { cache: "no-store" })
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
                    parseInt(latest.tag_name.slice(1))>parseInt(currentVersion.slice(1))?window.showMajorBanner&&window.showMajorBanner(latest,SD):showBanner(latest,currentVersion,board);
                    return true;
                }
            })
            .catch(function () {});
    }

    function init(currentVersion, board) {
        checkForUpdates(currentVersion, board);
    }

    window.MiOpenUpdater = { init: init, getChannel: getChannel, check: checkForUpdates };
})();
