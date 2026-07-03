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
var SHUTTER  = ["ROLLER_SHUTTER","BLIND","DUAL_SHUTTER","AWNING",
"HORIZONTAL_AWNING","EXTERNAL_VENETIAN_BLIND",
"CURTAIN_TRACK","SWINGING_SHUTTER"];
var VENETIAN = ["VENETIAN_BLIND","LOUVRE_BLIND"];
var WINDOW   = ["WINDOW_OPENER"];
var GATE     = ["GARAGE_OPENER","GATE_OPENER","ROLLING_DOOR_OPENER"];
var t = (device.type_name || "UNKNOWN").toUpperCase().replace(/[\s\-]+/g, "_");
if (SHUTTER.indexOf(t)  !== -1) return "shutter";
if (VENETIAN.indexOf(t) !== -1) return "venetian";
if (WINDOW.indexOf(t)   !== -1) return "window";
if (GATE.indexOf(t)     !== -1) return "gate";
if (t === "ON_OFF_SWITCH") return "switch";
if (t === "LIGHT") return device.subtype === 58 ? "switch" : "dimmer";
return "readonly";
}
async function runAction(app, deviceId, action, value) {
const payload = { deviceId: deviceId, action: action };
if (value !== undefined) payload.value = value;
const result = await window.MiOpenApi.postJson("/api/action", payload);
if (result.success === false) {
markUnreachable(deviceId);
throw new Error(result.message || "Action failed");
}
app.logStatus(result.message || ("Action " + action + " sent."), "debug");
return result;
}
function markUnreachable(deviceId) {
var el = document.querySelector('.device[data-id="' + deviceId + '"]');
if (el) el.classList.add("unreachable");
}
function openPct(pos) {
return pos < 0 ? null : 100 - pos;
}
function posStateLabel(pos) {
if (pos < 0)    return window.t ? window.t("label.pos_unknown") : "Unknown";
if (pos === 0)  return window.t ? window.t("label.pos_open")    : "Open";
if (pos === 100)return window.t ? window.t("label.pos_closed")  : "Closed";
return window.t ? window.t("label.pos_partial") : "Partial";
}
function buildPosIndicator(device) {
var op = openPct(device.position);
var fillW = op !== null ? op : 0;
var wrapper = document.createElement("div");
wrapper.className = "pos-indicator";
var topRow = document.createElement("div");
topRow.className = "pos-top-row";
var valEl = document.createElement("span");
valEl.className = "pos-value" + (op === null ? " unknown" : "");
valEl.textContent = op !== null ? device.position + "%" : "—";
var stateEl = document.createElement("span");
stateEl.className = "pos-state";
stateEl.textContent = posStateLabel(device.position);
topRow.appendChild(valEl);
topRow.appendChild(stateEl);
var strip = document.createElement("div");
strip.className = "light-strip";
var fill = document.createElement("div");
fill.className = "light-fill";
fill.style.width = fillW + "%";
strip.appendChild(fill);
wrapper.appendChild(topRow);
wrapper.appendChild(strip);
return wrapper;
}
function updateDeviceFill(deviceId, percent, inverted, estimated) {
var el = document.querySelector('.device[data-id="' + deviceId + '"]');
if (!el) return;
el.classList.remove("unreachable");
el.classList.toggle("estimating", !!estimated);
var fill   = el.querySelector(".light-fill");
var valEl  = el.querySelector(".pos-value");
var stateEl= el.querySelector(".pos-state");
var slider = el.querySelector(".card-slider[data-slider='position']");
var op     = percent < 0 ? null : (inverted ? percent : 100 - percent);
if (fill)   fill.style.width    = (op !== null ? op : 0) + "%";
if (valEl)  valEl.textContent   = percent >= 0 ? percent + "%" : "—";
if (stateEl)stateEl.textContent = posStateLabel(percent);
if (slider) slider.value        = percent;
}
function updateDeviceState(deviceId, isStopped) {
var el = document.querySelector('.device[data-id="' + deviceId + '"]');
if (el) el.classList.toggle("moving", isStopped === false);
}
function makeBtn(text, onClick) {
var btn = document.createElement("button");
btn.textContent = text;
btn.className = "card-btn";
btn.addEventListener("click", onClick);
return btn;
}
function makeTextBtn(text, onClick) {
var btn = document.createElement("button");
btn.textContent = text;
btn.className = "btn-text";
btn.addEventListener("click", onClick);
return btn;
}
function makeRow(buttons) {
var row = document.createElement("div");
row.className = "card-btn-row";
buttons.forEach(function (b) { row.appendChild(b); });
return row;
}
function makeSlider(app, device, action, initVal) {
var wrapper = document.createElement("div");
wrapper.className = "card-slider-row";
var lbl = document.createElement("span");
lbl.className = "card-slider-label";
lbl.textContent = action === "tilt" ? app.i18nText("label.tilt", "Tilt")
: action === "dim"              ? app.i18nText("label.dim",  "Dim")
: app.i18nText("label.position", "Pos");
var sl = document.createElement("input");
sl.type = "range"; sl.min = "0"; sl.max = "100";
sl.value = (initVal !== undefined && initVal >= 0) ? initVal : 0;
sl.className = "card-slider";
if (action === "position") {
sl.dataset.slider = "position";
}
sl.addEventListener("change", function () {
runAction(app, device.id, action, parseInt(sl.value, 10))
.catch(function (e) { showToast(e.message, "error"); });
});
wrapper.appendChild(lbl);
wrapper.appendChild(sl);
return wrapper;
}
function makeFavBtn(app, device) {
var fav = getFavPos(device.id);
var btn = document.createElement("button");
btn.className = "card-btn card-fav" + (fav !== null ? " has-favorite" : "");
btn.textContent = "★";
btn.setAttribute("aria-label", app.i18nText("button.favorite", "Favourite"));
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
btn.className = "card-btn card-fav" + (fav !== null ? " has-favorite" : "");
btn.title = fav !== null ? ("Favorite: " + fav + "%") : "No favorite set";
}
function buildControls(app, device, li, group) {
if (group === "shutter" || group === "venetian" || group === "window") {
li.appendChild(makeRow([
makeBtn("↑", function () { runAction(app, device.id, "open").catch(function (e) { showToast(e.message, "error"); }); }),
makeBtn("■", function () { runAction(app, device.id, "stop").catch(function (e) { showToast(e.message, "error"); }); }),
makeBtn("↓", function () { runAction(app, device.id, "close").catch(function (e) { showToast(e.message, "error"); }); }),
makeFavBtn(app, device)
]));
li.appendChild(makeSlider(app, device, "position", device.position));
if (group === "venetian") {
li.appendChild(makeSlider(app, device, "tilt", device.tilt));
}
} else if (group === "gate") {
li.appendChild(makeRow([
makeBtn("↑", function () { runAction(app, device.id, "open").catch(function (e) { showToast(e.message, "error"); }); }),
makeBtn("↓", function () { runAction(app, device.id, "close").catch(function (e) { showToast(e.message, "error"); }); })
]));
} else if (group === "switch" || group === "dimmer") {
li.appendChild(makeRow([
makeTextBtn(app.i18nText("button.on", "On"),  function () { runAction(app, device.id, "on").catch(function (e) { showToast(e.message, "error"); }); }),
makeTextBtn(app.i18nText("button.off", "Off"),function () { runAction(app, device.id, "off").catch(function (e) { showToast(e.message, "error"); }); })
]));
if (group === "dimmer") {
li.appendChild(makeSlider(app, device, "dim", 0));
}
} else {
var span = document.createElement("span");
span.className = "device-status-only";
span.textContent = app.i18nText("label.status_only", "Status only");
li.appendChild(span);
}
}
function closeDeviceEditModal() {
var m = document.getElementById("device-edit-modal");
if (m) m.classList.remove("open");
}
function devRow(labelText, subText, rightEl) {
var row = document.createElement("div");
row.className = "dev-row";
var L = document.createElement("div");
var lbl = document.createElement("div");
lbl.className = "dev-row-label";
lbl.textContent = labelText;
L.appendChild(lbl);
if (subText) {
var sub = document.createElement("div");
sub.className = "dev-row-sub";
sub.textContent = subText;
L.appendChild(sub);
}
row.appendChild(L);
if (rightEl) {
var R = document.createElement("div");
R.className = "dev-row-right";
(Array.isArray(rightEl) ? rightEl : [rightEl]).forEach(function (el) { R.appendChild(el); });
row.appendChild(R);
}
return row;
}
function devBtn(text, cls) {
var btn = document.createElement("button");
btn.textContent = text;
btn.className = "s-btn " + (cls || "");
return btn;
}
function openDeviceEditModal(app, device, group) {
var modal = document.getElementById("device-edit-modal");
if (!modal) return;
var nameEl = document.getElementById("dev-sheet-name");
var metaEl = document.getElementById("dev-sheet-meta");
var body   = document.getElementById("dev-sheet-body");
if (!nameEl || !metaEl || !body) return;
var hasPos = (group === "shutter" || group === "venetian" || group === "window" || group === "gate");
var hasFav = (group === "shutter" || group === "venetian" || group === "window");
nameEl.textContent = device.name;
metaEl.textContent = (device.type_name || app.i18nText("popup.type_unknown", "Unknown"))
+ (device.manufacturer ? " · " + device.manufacturer : "")
+ " · " + device.id
+ (device.inactive ? " · " + app.i18nText("popup.device_inactive_text", "inactive") : "");
body.innerHTML = "";
var nameInput = document.createElement("input");
nameInput.type = "text";
nameInput.className = "s-input";
nameInput.value = device.name;
nameInput.style.flex = "1";
var nameSave = devBtn(app.i18nText("button.save", "Save"), "primary");
nameSave.style.flexShrink = "0";
nameSave.onclick = function () {
var val = nameInput.value.trim();
if (!val) { showToast(app.i18nText("popup.rename_empty", "Name cannot be empty."), "error"); return; }
if (val === device.name) { closeDeviceEditModal(); return; }
window.MiOpenApi.postJson("/api/action", { deviceId: device.id, action: "rename", value: val })
.then(function (r) {
if (!r.success) { showToast(r.message || app.i18nText("popup.rename_failed", "Rename failed."), "error"); return; }
showToast(r.message || app.i18nText("popup.renamed", "Renamed."), "success");
device.name = val;
nameEl.textContent = val;
fetchAndDisplayDevices(app);
closeDeviceEditModal();
})
.catch(function (e) { showToast(e.message, "error"); });
};
var nameRow = document.createElement("div");
nameRow.className = "dev-name-row";
nameRow.appendChild(nameInput);
nameRow.appendChild(nameSave);
body.appendChild(nameRow);
if (device.inactive) {
var badge = document.createElement("span");
badge.className = "dev-status-badge";
badge.textContent = app.i18nText("badge.inactive", "Inactive");
body.appendChild(devRow(app.i18nText("popup.device_status", "Status"), null, badge));
var reactivateBtn = devBtn(app.i18nText("button.reactivate", "Re-activate"), "primary");
reactivateBtn.onclick = function () {
window.MiOpenApi.postJson("/api/action", { deviceId: device.id, action: "reactivateDevice" })
.then(function (r) {
if (!r.success) { showToast(r.message || "Re-activate failed.", "error"); return; }
showToast(app.i18nText("popup.device_reactivated", "Device re-activated."), "success");
closeDeviceEditModal();
fetchAndDisplayDevices(app);
})
.catch(function (e) { showToast(e.message, "error"); });
};
body.appendChild(devRow(app.i18nText("button.reactivate", "Re-activate"), app.i18nText("popup.reactivate_desc", "Restore controls and position tracking."), reactivateBtn));
} else {
if (hasPos) {
var posSpan = document.createElement("span");
posSpan.style.cssText = "font-size:13px;color:var(--text2);font-family:var(--mono);";
posSpan.textContent = device.position >= 0
? device.position + "% — " + posStateLabel(device.position)
: posStateLabel(-1);
body.appendChild(devRow(app.i18nText("popup.device_position", "Position"), null, posSpan));
}
if (hasFav) {
var invertToggle = document.createElement("div");
invertToggle.className = "s-toggle" + (device.is_inverted ? " on" : "");
invertToggle.onclick = function () {
window.MiOpenApi.postJson("/api/action", { deviceId: device.id, action: "invertOpenClose" })
.then(function (r) {
if (!r.success) { showToast(r.message || "Invert failed.", "error"); return; }
device.is_inverted = !device.is_inverted;
invertToggle.classList.toggle("on", device.is_inverted);
showToast(app.i18nText("popup.inverted", "Direction inverted."), "success");
})
.catch(function (e) { showToast(e.message, "error"); });
};
body.appendChild(devRow(
app.i18nText("label.invert_openclose", "Invert open/close"),
app.i18nText("popup.invert_desc", "Swap which end counts as fully open."),
invertToggle
));
}
if (hasPos) {
var quietToggle = document.createElement("div");
quietToggle.className = "s-toggle" + (device.is_quiet ? " on" : "");
quietToggle.onclick = function () {
var newVal = !device.is_quiet;
window.MiOpenApi.postJson("/api/action", { deviceId: device.id, action: "setQuiet", value: newVal })
.then(function (r) {
if (!r.success) { showToast(r.message || "Quiet mode failed.", "error"); return; }
device.is_quiet = newVal;
quietToggle.classList.toggle("on", device.is_quiet);
})
.catch(function (e) { showToast(e.message, "error"); });
};
body.appendChild(devRow(
app.i18nText("label.quiet_mode", "Quiet mode"),
app.i18nText("popup.quiet_desc", "Slower, quieter motor operation."),
quietToggle
));
}
if (hasFav) {
var favPos = getFavPos(device.id);
var favSub = favPos !== null ? "Currently: " + favPos + "%" : "No favorite set.";
var favSetBtn = devBtn(
device.position >= 0 ? t("popup.fav_set_to", {pos: device.position}) : app.i18nText("popup.fav_unknown", "Position unknown"),
""
);
if (device.position < 0) favSetBtn.disabled = true;
var favRow = devRow(app.i18nText("popup.favorite_position", "Favorite position"), favSub, favSetBtn);
favSetBtn.onclick = function () {
setFavPos(device.id, device.position);
updateFavButton(device.id);
var sub = favRow.querySelector(".dev-row-sub");
if (sub) sub.textContent = t("popup.fav_currently", {pos: device.position});
favSetBtn.textContent = t("popup.fav_set_to", {pos: device.position});
showToast(t("popup.fav_saved", {pos: device.position}), "success");
};
body.appendChild(favRow);
}
var idBtn = devBtn(app.i18nText("button.identify", "Identify"), "");
idBtn.onclick = function () {
window.MiOpenApi.postJson("/api/action", { deviceId: device.id, action: "identify" })
.then(function () { showToast(app.i18nText("popup.identifying", "Identify sent — watch for a brief movement."), "info"); })
.catch(function (e) { showToast(e.message, "error"); });
};
body.appendChild(devRow(app.i18nText("button.identify", "Identify"), app.i18nText("popup.device_identify_desc", "Triggers a brief movement to locate the device."), idBtn));
}
var danger = document.createElement("div");
danger.className = "dev-danger-zone";
var dangerLbl = document.createElement("div");
dangerLbl.className = "dev-danger-label";
dangerLbl.textContent = app.i18nText("popup.device_danger_zone", "Danger zone");
danger.appendChild(dangerLbl);
if (!device.inactive) {
var deactivateBtn = devBtn(app.i18nText("button.deactivate", "Deactivate"), "danger");
deactivateBtn.onclick = function () {
if (!confirm(app.i18nText("confirm.deactivate_device", "Deactivate \"{name}\"?").replace("{name}", device.name) + "\n"
+ app.i18nText("popup.deactivate_warning", "The device will be kept as inactive and can be re-activated later."))) return;
window.MiOpenApi.postJson("/api/action", { deviceId: device.id, action: "deactivateDevice" })
.then(function (r) {
if (!r.success) { showToast(r.message || "Deactivate failed.", "error"); return; }
showToast(app.i18nText("popup.device_deactivated", "Device deactivated."), "info");
closeDeviceEditModal();
fetchAndDisplayDevices(app);
})
.catch(function (e) { showToast(e.message, "error"); });
};
danger.appendChild(devRow(
app.i18nText("button.deactivate", "Deactivate"),
app.i18nText("popup.deactivate_desc", "Keeps device in list but removes controls. Reversible."),
deactivateBtn
));
}
var deleteBtn = devBtn(app.i18nText("button.delete", "Delete permanently"), "danger");
deleteBtn.onclick = function () {
if (!confirm(app.i18nText("confirm.delete_device", "Permanently delete \"{name}\"?").replace("{name}", device.name) + "\n"
+ app.i18nText("popup.delete_warning", "Permanent removal. Cannot be undone — requires factory reset to re-pair."))) return;
var doDelete = function () {
window.MiOpenApi.postJson("/api/action", { deviceId: device.id, action: "deleteDevice" })
.then(function (r) {
if (!r.success) { showToast(r.message || "Delete failed.", "error"); return; }
showToast(app.i18nText("popup.device_deleted", "Device permanently deleted."), "info");
closeDeviceEditModal();
fetchAndDisplayDevices(app);
})
.catch(function (e) { showToast(e.message, "error"); });
};
if (!device.inactive) {
window.MiOpenApi.postJson("/api/action", { deviceId: device.id, action: "deactivateDevice" })
.then(doDelete)
.catch(doDelete);
} else {
doDelete();
}
};
danger.appendChild(devRow(
app.i18nText("popup.device_delete_label", "Delete permanently"),
app.i18nText("popup.delete_warning", "Cannot be undone. Requires factory reset to re-pair."),
deleteBtn
));
body.appendChild(danger);
var closeBtn = document.getElementById("device-edit-close");
if (closeBtn) closeBtn.onclick = closeDeviceEditModal;
modal.onclick = function (e) { if (e.target === modal) closeDeviceEditModal(); };
modal.classList.add("open");
}
async function fetchAndDisplayDevices(app) {
const list = app.elements.deviceList;
if (!list.hasChildNodes()) {
var loadingLi = document.createElement("li");
loadingLi.id = "device-loading";
loadingLi.textContent = app.i18nText("popup.loading", "Loading…");
loadingLi.style.cssText = "padding:20px;color:var(--text3);text-align:center;grid-column:1/-1;";
list.appendChild(loadingLi);
}
try {
const devices = await window.MiOpenApi.requestJson("/api/devices");
app.state.devicesCache = devices;
list.textContent = "";
if (!devices.length) {
app.logStatus(app.i18nText("list.no_devices_found", "No devices found."), "info");
var empty = document.createElement("li");
empty.textContent = app.i18nText("list.no_devices_available", "No devices available.");
empty.style.cssText = "padding:20px;color:var(--text3);text-align:center;grid-column:1/-1;";
list.appendChild(empty);
return;
}
var active   = devices.filter(function (d) { return !d.inactive; });
var inactive = devices.filter(function (d) { return  d.inactive; });
[...active, ...inactive].forEach(function (device) {
var group = getDeviceGroup(device);
var hasPos = (group === "shutter" || group === "venetian" || group === "window");
var li = document.createElement("li");
li.classList.add("device");
li.dataset.id = device.id;
if (device.inactive) li.classList.add("inactive");
var warnDot = document.createElement("div");
warnDot.className = "warn-dot";
li.appendChild(warnDot);
var dot = document.createElement("div");
dot.className = "moving-dot";
li.appendChild(dot);
var top = document.createElement("div");
top.className = "card-top";
var nameBlock = document.createElement("div");
var nameEl = document.createElement("div");
nameEl.className = "card-name";
nameEl.textContent = device.name;
var typeEl = document.createElement("span");
typeEl.className = "card-badge";
typeEl.textContent = (device.type_name || "").toLowerCase();
nameBlock.appendChild(nameEl);
nameBlock.appendChild(typeEl);
var menuBtn = document.createElement("button");
menuBtn.textContent = "⋯";
menuBtn.className = "btn menu";
menuBtn.setAttribute("aria-label", app.i18nText("button.edit", "Edit"));
menuBtn.addEventListener("click", function () { openDeviceEditModal(app, device, group); });
top.appendChild(nameBlock);
top.appendChild(menuBtn);
li.appendChild(top);
if (device.inactive) {
var badge = document.createElement("span");
badge.className = "device-status-only";
badge.textContent = app.i18nText("badge.inactive", "inactive");
li.appendChild(badge);
} else {
if (hasPos) {
li.appendChild(buildPosIndicator(device));
}
var spacer = document.createElement("div");
spacer.className = "card-spacer";
li.appendChild(spacer);
buildControls(app, device, li, group);
if (device.position >= 0) {
updateDeviceFill(device.id, device.position, !!device.is_inverted);
}
updateDeviceState(device.id, device.is_stopped);
}
list.appendChild(li);
});
var countPill = document.getElementById("count-pill");
if (countPill) countPill.textContent = active.length + " " + (window.t ? window.t("nav.devices") : "devices");
app.logStatus("Device list updated.", "info");
} catch (error) {
app.logStatus("Error fetching devices: " + error.message, "error");
}
}
var pairingWizard = (function () {
var _app = null, _wizard = null, _statusEl = null, _btnsEl = null;
var _badge = null, _scanning = false, _pendingCaptureDeviceId = null;
function open(app) {
_app = app;
_wizard  = document.getElementById("pair-wizard");
_statusEl= document.getElementById("pair-wizard-status");
_btnsEl  = document.getElementById("pair-wizard-buttons");
_badge   = document.getElementById("pairing-badge");
document.getElementById("pair-wizard-close").onclick = cancel;
_wizard.classList.add("open");
showStep1();
}
function close() { if (_wizard) _wizard.classList.remove("open"); hideBadge(); _scanning = false; }
function cancel() {
if (_pendingCaptureDeviceId !== null) {
window.MiOpenApi.postJson("/api/remote/capture/cancel", {}).catch(function () {});
_pendingCaptureDeviceId = null;
}
close();
}
function showBadge(s) {
if (!_badge) return;
var m = Math.floor(s / 60), sc = s % 60;
_badge.textContent = "🔗 " + (m > 0 ? m + "m " : "") + sc + "s";
_badge.style.display = "";
_badge.onclick = function () { if (_wizard) _wizard.classList.add("open"); };
}
function hideBadge() { if (_badge) { _badge.style.display = "none"; _badge.onclick = null; } }
function setStatus(html) { if (_statusEl) _statusEl.innerHTML = html; }
function setButtons(btns) {
if (!_btnsEl) return;
_btnsEl.textContent = "";
btns.forEach(function (b) { _btnsEl.appendChild(b); });
}
function makeBtn(label, cls, onClick) {
var btn = document.createElement("button");
btn.textContent = label; btn.className = cls || "";
btn.addEventListener("click", onClick); return btn;
}
function showStep1() {
_pendingCaptureDeviceId = null; _scanning = false; hideBadge();
setStatus(_app.i18nText("popup.pair_step1_text", "Put the device into pairing mode, then press Start."));
setButtons([
makeBtn(_app.i18nText("button.start_discovery", "Start Discovery"), "pair", function () {
_scanning = true; showBadge(120);
setStatus(_app.i18nText("popup.pair_step2_scanning", "Scanning up to 2 minutes...") + " <strong>2m 0s</strong>");
setButtons([makeBtn(_app.i18nText("button.cancel", "Cancel"), "danger", cancel)]);
window.MiOpenApi.postJson("/api/pair/start", {}).catch(function (e) {
_scanning = false; hideBadge();
setStatus(_app.i18nText("popup.pair_failed", "Pairing request failed.") + " " + e.message);
showRetry();
});
}),
makeBtn(_app.i18nText("button.cancel", "Cancel"), "danger", cancel)
]);
}
function showRetry() {
setButtons([makeBtn(_app.i18nText("button.retry","Retry"),"pair",showStep1), makeBtn(_app.i18nText("button.cancel","Cancel"),"danger",cancel)]);
}
function onPairingActive(remainingS) {
if (!_scanning) return;
showBadge(remainingS);
if (_statusEl && _wizard && _wizard.classList.contains("open")) {
var m = Math.floor(remainingS/60), s = remainingS%60;
_statusEl.innerHTML = _app.i18nText("popup.pair_step2_scanning","Scanning up to 2 minutes...") + " <strong>" + (m>0?m+"m ":"") + s + "s</strong>";
}
}
function onDeviceAdded(deviceId, deviceName) {
if (!_wizard || !_wizard.classList.contains("open")) return;
_scanning = false; hideBadge(); _pendingCaptureDeviceId = deviceId;
setStatus(_app.i18nText("popup.pair_step3_success","Device paired: {name}").replace("{name}", deviceName));
setButtons([
makeBtn(_app.i18nText("button.link_remote","Link Remote"), "pair", function () { startCapture(deviceId); }),
makeBtn(_app.i18nText("button.skip","Done"), "", cancel)
]);
fetchAndDisplayDevices(_app);
}
function onPairFailed(data) {
if (!_wizard || !_wizard.classList.contains("open")) return;
_scanning = false; hideBadge();
if (data && data.status === "key_mismatch") {
setStatus('<span style="color:#c0392b">' + (data.message || _app.i18nText("popup.pair_key_mismatch","Device found but has a different system key. Factory reset the device and try again.")) + '</span>');
} else {
setStatus(_app.i18nText("popup.pair_timeout","No device found."));
}
showRetry();
}
function startCapture(deviceId) {
_pendingCaptureDeviceId = deviceId;
setStatus(_app.i18nText("popup.remote_capture_prompt","Press any button on the remote..."));
setButtons([makeBtn(_app.i18nText("button.cancel","Cancel"), "danger", cancel)]);
window.MiOpenApi.postJson("/api/remote/capture/start", {}).catch(function (e) { setStatus("Capture start failed: " + e.message); showRetry(); });
}
function onRemoteSeen(remoteId) {
if (!_wizard || !_wizard.classList.contains("open")) return;
var devId = _pendingCaptureDeviceId;
setStatus(_app.i18nText("popup.remote_captured","Remote detected: {id}").replace("{id}", remoteId));
setButtons([
makeBtn(_app.i18nText("button.link_remote","Link"), "pair", function () {
window.MiOpenApi.postJson("/api/remote/capture/cancel", {}).catch(function () {});
window.MiOpenApi.postJson("/api/action", { deviceId: devId, action: "linkRemote", remoteId: remoteId })
.then(function () { _app.logStatus("Remote " + remoteId + " linked.", "info"); fetchAndDisplayDevices(_app); cancel(); })
.catch(function (e) { _app.logStatus("Link failed: " + e.message, "error"); });
}),
makeBtn(_app.i18nText("button.skip","Skip"), "", function () { window.MiOpenApi.postJson("/api/remote/capture/cancel", {}).catch(function () {}); cancel(); })
]);
}
function onCaptureTimeout() {
if (!_wizard || !_wizard.classList.contains("open")) return;
var devId = _pendingCaptureDeviceId;
setStatus(_app.i18nText("popup.remote_capture_timeout","No remote detected within 30 seconds."));
setButtons([makeBtn(_app.i18nText("button.retry","Retry"), "pair", function () { startCapture(devId); }), makeBtn(_app.i18nText("button.skip","Done"), "", cancel)]);
}
return { open:open, cancel:cancel, onDeviceAdded:onDeviceAdded, onPairFailed:onPairFailed, onPairingActive:onPairingActive, onRemoteSeen:onRemoteSeen, onCaptureTimeout:onCaptureTimeout };
})();
function openSomfyImportModal(app,dvs){
var m=document.createElement("div");
m.style.cssText="position:fixed;inset:0;background:rgba(0,0,0,.55);z-index:1000;display:flex;align-items:center;justify-content:center;";
var h='<div style="background:var(--card);border-radius:12px;padding:20px;width:min(400px,92vw);max-height:80vh;overflow-y:auto;display:flex;flex-direction:column;gap:10px;"><div style="font-weight:600;font-size:15px;">Devices found in Somfy cloud ('+dvs.length+')</div>';
dvs.forEach(function(d){h+='<label style="display:flex;align-items:center;gap:10px;cursor:'+(d.already_added?'default':'pointer')+';opacity:'+(d.already_added?'.45':'1')+';"><input type="checkbox"'+(d.already_added?' disabled':'')+" data-p='"+JSON.stringify({id:d.id,name:d.name})+"'><span style=\"font-size:13px;\">"+d.name+" · "+d.id+(d.already_added?" (already added)":"")+"</span></label>";});
h+='<div style="display:flex;gap:8px;margin-top:6px;"><button class="s-btn primary" id="_sa">Add selected</button><button class="s-btn" id="_sc">Cancel</button></div></div>';
m.innerHTML=h;document.body.appendChild(m);
m.querySelector("#_sc").onclick=function(){document.body.removeChild(m);};
m.querySelector("#_sa").addEventListener("click",async function(){
var ab=this,sel=[].filter.call(m.querySelectorAll("input[data-p]"),function(c){return c.checked&&!c.disabled;}).map(function(c){return JSON.parse(c.dataset.p);});
if(!sel.length)return;ab.disabled=true;ab.textContent="Adding…";
try{var r=await window.MiOpenApi.postJson("/api/somfy/add",sel);document.body.removeChild(m);app.logStatus(r.message||"Devices added.",r.success?"info":"error");if(r.success)app.fetchAndDisplayDevices();}
catch(e){ab.disabled=false;ab.textContent="Add selected";app.logStatus("Error adding devices.","error");}
});
}
function init(app) {
app.fetchAndDisplayDevices = function () { return fetchAndDisplayDevices(app); };
app.updateDeviceFill  = updateDeviceFill;
app.updateDeviceState = updateDeviceState;
app.pairingWizard     = pairingWizard;
var pairBtn = document.getElementById("pair-device-btn");
if (pairBtn) pairBtn.addEventListener("click", function () { pairingWizard.open(app); });
var _si=document.getElementById("somfy-import-btn");
if(_si)_si.addEventListener("click",async function(){
var s=document.getElementById("somfy-status");
if(s)s.textContent="Contacting Somfy cloud…";_si.disabled=true;
try{var d=await window.MiOpenApi.postJson("/api/somfy/import",{});_si.disabled=false;
if(!Array.isArray(d)){if(s)s.textContent=d&&d.message?d.message:"Import failed.";return;}
if(!d.length){if(s)s.textContent="No io-homecontrol devices found in Somfy account.";return;}
if(s)s.textContent="";openSomfyImportModal(app,d);
}catch(e){_si.disabled=false;if(s)s.textContent="Could not reach Somfy cloud.";}
});
}
window.MiOpenDevices = { init: init };
})();