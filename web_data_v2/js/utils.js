// ── Toast ─────────────────────────────────────────────────────────────────────
(function () {
    var container = document.createElement("div");
    container.id = "toast-container";
    document.body.appendChild(container);

    function dismissToast(toast) {
        if (!toast || !toast.parentNode) return;
        toast.classList.add("toast-hide");
        setTimeout(function () {
            if (toast.parentNode) toast.parentNode.removeChild(toast);
        }, 300);
    }

    window.showToast = function (msg, type, duration) {
        var toast = document.createElement("div");
        toast.className = "toast" + (type ? " toast-" + type : "");
        toast.textContent = msg;
        container.appendChild(toast);
        var ms = (duration || 3000);
        var timer = setTimeout(function () { dismissToast(toast); }, ms);
        toast._dismiss = function () { clearTimeout(timer); dismissToast(toast); };
        return toast;
    };
})();

// ── i18n ──────────────────────────────────────────────────────────────────────
let I18N = {};
let FALLBACK = {};
let CURRENT_LANG = "en";
const SUPPORTED = ["nl", "en", "de", "fr"];
const _langCache = {};

async function _loadLang(lang) {
    if (_langCache[lang]) return _langCache[lang];
    const r = await fetch("/lang/" + lang + ".json");
    const data = r.ok ? await r.json() : {};
    _langCache[lang] = data;
    return data;
}

function interpolate(text, params = {}) {
    if (typeof text !== "string") return text;
    return text.replace(/\{(\w+)\}/g, (_, key) =>
        Object.prototype.hasOwnProperty.call(params, key) ? params[key] : `{${key}}`);
}

function t(key, params = {}) {
    const value = I18N[key] ?? FALLBACK[key] ?? key;
    return interpolate(value, params);
}

function applyI18n() {
    document.querySelectorAll("[data-i18n]").forEach(el => { el.textContent = t(el.dataset.i18n); });
    document.querySelectorAll("[data-i18n-placeholder]").forEach(el => { el.placeholder = t(el.dataset.i18nPlaceholder); });
    document.title = t("page.title");
}

async function setLang(lang) {
    const nextLang = SUPPORTED.includes(lang) ? lang : "en";
    const data = await _loadLang(nextLang);
    I18N = data;
    CURRENT_LANG = nextLang;
    localStorage.setItem("lang", nextLang);
    applyI18n();
    window.dispatchEvent(new CustomEvent("i18n:changed", { detail: { lang: CURRENT_LANG } }));
}

function getLang() { return CURRENT_LANG; }

(async () => {
    const saved = localStorage.getItem("lang");
    const auto = (navigator.language || "en").slice(0, 2).toLowerCase();
    const initial = saved || auto;
    const lang = SUPPORTED.includes(initial) ? initial : "en";
    const select = document.getElementById("lang");
    if (select) select.value = lang;
    // Load EN fallback and selected lang in parallel; skip second fetch if EN selected
    if (lang === "en") {
        FALLBACK = await _loadLang("en");
        I18N = FALLBACK;
        CURRENT_LANG = "en";
        localStorage.setItem("lang", "en");
        applyI18n();
        window.dispatchEvent(new CustomEvent("i18n:changed", { detail: { lang: "en" } }));
    } else {
        const [enData, langData] = await Promise.all([_loadLang("en"), _loadLang(lang)]);
        FALLBACK = enData;
        I18N = langData;
        CURRENT_LANG = lang;
        localStorage.setItem("lang", lang);
        applyI18n();
        window.dispatchEvent(new CustomEvent("i18n:changed", { detail: { lang: lang } }));
    }
})();

window.t = t;
window.setLang = setLang;
window.getLang = getLang;

// ── API ───────────────────────────────────────────────────────────────────────
(function () {
    function ensureJson(response) {
        return response.json().catch(function () { return {}; });
    }

    async function requestJson(url, options) {
        const requestOptions = options || {};
        const method = (requestOptions.method || "GET").toUpperCase();
        const requestUrl = method === "GET"
            ? url + (url.indexOf("?") === -1 ? "?" : "&") + "_=" + Date.now()
            : url;
        if (method === "GET") requestOptions.cache = "no-store";
        requestOptions.headers = otaHeaders(requestOptions.headers || {});
        const response = await fetch(requestUrl, requestOptions);
        const data = await ensureJson(response);
        if (!response.ok) throw new Error(data.message || ("HTTP error " + response.status));
        return data;
    }

    function otaHeaders(extra) {
        const headers = extra || {};
        const key = window.MiOpenApi && window.MiOpenApi.otaKey;
        if (key) headers["X-OTA-Key"] = key;
        return headers;
    }

    async function postJson(url, payload) {
        return requestJson(url, {
            method: "POST",
            headers: otaHeaders({ "Content-Type": "application/json" }),
            body: JSON.stringify(payload)
        });
    }

    async function uploadFile(url, file) {
        const formData = new FormData();
        formData.append("file", file);
        return requestJson(url, { method: "POST", headers: otaHeaders(), body: formData });
    }

    async function downloadFile(url, filename) {
        const response = await fetch(url, { headers: otaHeaders() });
        if (!response.ok) throw new Error("Network response was not ok");
        const blob = await response.blob();
        const link = document.createElement("a");
        link.href = window.URL.createObjectURL(blob);
        link.download = filename;
        link.click();
        window.URL.revokeObjectURL(link.href);
    }

    window.MiOpenApi = { downloadFile, postJson, requestJson, uploadFile };
})();

window.MiOpenBackup=(function(){
function init(){
var st=document.getElementById("backup-status");
function ss(m,ok){if(st){st.textContent=m;st.style.color=ok===true?"var(--green)":ok===false?"var(--red)":"";}};
function otaHdr(){var k=window.MiOpenApi&&window.MiOpenApi.otaKey;return k?{"X-OTA-Key":k}:{};};
var exp=document.getElementById("backup-export");
if(exp)exp.addEventListener("click",function(){
ss(t("toast.backup-exporting"));
fetch("/api/backup",{headers:otaHdr()})
.then(function(r){if(!r.ok)throw new Error("HTTP "+r.status);return r.blob();})
.then(function(b){var a=document.createElement("a");a.href=URL.createObjectURL(b);a.download="io-rts-backup.json";a.click();URL.revokeObjectURL(a.href);ss(t("toast.backup-exported"),true);})
.catch(function(e){ss(t("toast.backup-export-failed",{message:e.message}),false);});
});
var fi=document.getElementById("backup-file");
var imp=document.getElementById("backup-import-btn");
if(imp&&fi){
imp.addEventListener("click",function(){fi.click();});
fi.addEventListener("change",function(){
var f=fi.files[0];if(!f)return;
var rd=new FileReader();
rd.onload=function(ev){
if(!confirm(t("confirm.restore-backup"))){fi.value="";return;}
ss(t("toast.backup-importing"));
var h=Object.assign({"Content-Type":"application/json"},otaHdr());
fetch("/api/restore",{method:"POST",headers:h,body:ev.target.result})
.then(function(r){return r.json();})
.then(function(d){ss(d.message,d.success);showToast(d.message,d.success?"success":"error");})
.catch(function(e){ss(t("toast.backup-restore-failed",{message:e.message}),false);});
fi.value="";};
rd.readAsText(f);
});}
var rst=document.getElementById("factory-reset-btn");
if(rst)rst.addEventListener("click",function(){
if(!confirm(t("confirm.factory-reset-1")))return;
if(!confirm(t("confirm.factory-reset-2")))return;
ss("…");
fetch("/api/factory-reset",{method:"POST",headers:otaHdr()})
.then(function(r){return r.json();})
.then(function(d){ss(d.message,d.success);if(d.success)showToast(t("toast.factory-reset-rebooting"),"success");})
.catch(function(){ss("Factory reset sent.");});
});}
return{init:init};
})();
window.showMajorBanner=function(r,sk){
var b=document.getElementById("major-upgrade-banner");if(!b)return;
var tag=r.tag_name;
document.getElementById("mub-tag").textContent=tag;
var a=r.assets||[],fu=r.html_url;
for(var i=0;i<a.length;i++){if(a[i].name.indexOf("full")!==-1){fu=a[i].browser_download_url;break;}}
document.getElementById("mub-link").href=fu;
b.style.display="";
var d=document.getElementById("mub-dismiss");
if(d)d.onclick=function(){localStorage.setItem(sk,tag);b.style.display="none";};
};
