(function () {
    var HELP_SECTIONS = {
        "wifi":        ["what-it-does", "after-saving", "scan"],
        "fallback-ap": ["what-it-does", "hotspot-name", "password", "retries-boot", "retries-running", "timeout"],
        "network":     ["dhcp", "static-ip", "hostname", "sntp-server"],
        "mqtt":        ["what-it-does", "discovery", "tls", "topics"],
        "syslog":      ["what-it-does", "compatible-servers", "min-level"],
        "somfy":       ["what-it-does", "what-is-imported", "prerequisite", "credentials"],
        "controller":  ["node-address", "tx-power", "passive-mode"],
        "io-key":      ["what-it-is", "changing-key", "learn", "send-key", "pair", "sniff"],
        "ota-key":     ["what-it-is", "usage", "rotation"],
        "backup":      ["backup", "restore"]
    };
    var WARN_SECTIONS = { "io-key": { "changing-key": true } };

    function getHelpData(key) {
        var slugs = HELP_SECTIONS[key];
        if (!slugs) return null;
        var p = "help." + key + ".";
        return {
            title: t(p + "title"),
            sections: slugs.map(function (s) {
                return {
                    label: t(p + s + ".label"),
                    body:  t(p + s + ".body"),
                    warn:  !!(WARN_SECTIONS[key] && WARN_SECTIONS[key][s])
                };
            })
        };
    }

    var panel = null;
    var panelTitle = null;
    var panelBody  = null;
    var currentKey = null;

    function buildPanel() {
        var el = document.createElement("div");
        el.id = "help-panel";
        el.innerHTML =
            '<div class="help-panel-inner">' +
                '<div class="help-sheet-handle"></div>' +
                '<div class="help-panel-header">' +
                    '<span id="help-panel-title" class="help-panel-title"></span>' +
                    '<button id="help-panel-close" class="help-panel-close" aria-label="Close">&#x2715;</button>' +
                '</div>' +
                '<div id="help-panel-body" class="help-panel-body"></div>' +
            '</div>';
        document.body.appendChild(el);
        panelTitle = document.getElementById("help-panel-title");
        panelBody  = document.getElementById("help-panel-body");
        el.addEventListener("click", function (e) { if (e.target === el) closePanel(); });
        document.getElementById("help-panel-close").addEventListener("click", closePanel);
        document.addEventListener("keydown", function (e) {
            if (e.key === "Escape" && el.classList.contains("open")) closePanel();
        });
        return el;
    }

    function openPanel(key) {
        var data = getHelpData(key);
        if (!data) return;
        currentKey = key;
        if (!panel) panel = buildPanel();
        panelTitle.textContent = data.title;
        panelBody.innerHTML = "";
        data.sections.forEach(function (sec) {
            var block = document.createElement("div");
            block.className = "help-block" + (sec.warn ? " help-block-warn" : "");
            var lbl = document.createElement("div");
            lbl.className = "help-block-label";
            lbl.textContent = sec.label;
            var txt = document.createElement("div");
            txt.className = "help-block-text";
            txt.textContent = sec.body;
            block.appendChild(lbl);
            block.appendChild(txt);
            panelBody.appendChild(block);
        });
        panel.classList.add("open");
    }

    function closePanel() {
        currentKey = null;
        if (panel) panel.classList.remove("open");
    }

    function attachButton(el, key) {
        var label = el.querySelector(".row-label");
        if (!label) return;
        var btn = document.createElement("button");
        btn.className = "help-btn";
        btn.setAttribute("aria-label", "Help for " + key);
        btn.textContent = "?";
        btn.addEventListener("click", function (e) { e.stopPropagation(); openPanel(key); });
        label.appendChild(btn);
    }

    function init() {
        document.querySelectorAll("[data-help]").forEach(function (el) {
            attachButton(el, el.getAttribute("data-help"));
        });
    }

    window.addEventListener("i18n:changed", function () {
        if (panel && panel.classList.contains("open") && currentKey) {
            openPanel(currentKey);
        }
    });

    if (document.readyState === "loading") {
        document.addEventListener("DOMContentLoaded", init);
    } else {
        init();
    }

    window.MiOpenHelp = { open: openPanel, close: closePanel };
})();
