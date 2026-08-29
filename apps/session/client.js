window.__aiueosSessionAlive = true;
window.__aiueosSessionState = { bound: false };
window.__aiueosUiEngine = "kotoba-lang/browser";
window.__aiueosHostedAdapter = "html-js-verification-only";

function viewId() {
  var raw = location.hash || "#session";
  var cut = raw.split("?")[0];
  if (cut === "#" || cut === "#/" || cut === "") return "#session";
  if (cut.charAt(1) === "/") cut = "#" + cut.slice(2);
  return cut;
}

function show() {
  var h = viewId();
  if (h === "#itonami") h = "#operator";
  ["session", "desktop", "setup", "manage", "devices", "operator"].forEach(function (id) {
    var el = document.getElementById(id);
    if (el) el.hidden = ("#" + id) !== h;
  });
  if (h === "#manage") refreshStatus();
  if (h === "#devices") refreshDevices();
  if (h === "#setup") refreshSetup();
  if (h === "#desktop") refreshDesktop();
  if (h === "#session" || h === "#desktop") refreshGuest();
  if (h === "#operator") refreshOperator();
}

function pretty(x) {
  try { return JSON.stringify(typeof x === "string" ? JSON.parse(x) : x, null, 2); }
  catch (e) { return String(x); }
}

async function refreshSetup() {
  var el = document.getElementById("qr");
  if (!el) return;
  try {
    var r = await fetch("/setup.json");
    var j = await r.json();
    el.textContent = j.qr || JSON.stringify(j, null, 2);
  } catch (e) {
    el.textContent = String(e);
  }
}

async function startDeviceAuth(method) {
  var el = document.getElementById("auth-out");
  var approvalWindow = null;
  if (method === "passkey") {
    approvalWindow = window.open("about:blank", "aiueos-device-approval");
    if (approvalWindow) approvalWindow.opener = null;
  }
  if (el) el.textContent = "単回チャレンジを準備しています…";
  try {
    var r = await fetch("/api/device-auth/challenge", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ method: method })
    });
    var j = await r.json();
    if (!r.ok || !j.verification_uri_complete) {
      if (approvalWindow) approvalWindow.close();
      if (el) el.textContent = pretty(j);
      return;
    }
    var link = document.getElementById("device-auth-link");
    if (link) {
      link.href = j.verification_uri_complete;
      link.textContent = "承認ページを開く";
      link.hidden = false;
    }
    if (method === "phone-scan" && j.scan_payload) {
      var qr = document.getElementById("qr");
      var qrImage = document.getElementById("device-auth-qr");
      if (qr) qr.textContent = j.scan_payload;
      if (qrImage) {
        qrImage.src = "/api/device-auth/qr?flow=" + encodeURIComponent(j.flow_id);
        qrImage.hidden = false;
      }
    }
    if (method === "passkey") {
      if (approvalWindow) approvalWindow.location.replace(j.verification_uri_complete);
      else window.open(j.verification_uri_complete, "_blank", "noopener,noreferrer");
    }
    if (el) el.textContent = pretty(j);
    window.__aiueosDeviceAuth = {
      expiresAt: j.expires_at_ms,
      interval: j.poll_interval_seconds || 2
    };
    scheduleDeviceAuthPoll(1);
  } catch (e) {
    if (approvalWindow) approvalWindow.close();
    if (el) el.textContent = String(e);
  }
}

function scheduleDeviceAuthPoll(seconds) {
  if (window.__aiueosDeviceAuthTimer) {
    clearTimeout(window.__aiueosDeviceAuthTimer);
  }
  window.__aiueosDeviceAuthTimer = setTimeout(pollDeviceAuth, seconds * 1000);
}

async function pollDeviceAuth() {
  var flow = window.__aiueosDeviceAuth;
  var el = document.getElementById("auth-out");
  if (!flow) return;
  if (Date.now() >= flow.expiresAt) {
    window.__aiueosDeviceAuth = null;
    if (el) el.textContent = "承認の有効期限が切れました。もう一度始めてください。";
    return;
  }
  try {
    var r = await fetch("/api/device-auth/complete", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: "{}"
    });
    var j = await r.json();
    if (r.status === 202) {
      if (el) el.textContent = "Passkey の確認を待っています…\n" + pretty(j);
      scheduleDeviceAuthPoll(j.interval || flow.interval || 2);
      return;
    }
    window.__aiueosDeviceAuth = null;
    if (el) el.textContent = pretty(j);
    if (r.ok && j.decision === "grant") {
      var qr = document.getElementById("qr");
      var qrImage = document.getElementById("device-auth-qr");
      var link = document.getElementById("device-auth-link");
      if (qr) qr.textContent = "承認済み";
      if (qrImage) qrImage.hidden = true;
      if (link) link.hidden = true;
      refreshDevicePlan();
      refreshStatus();
      refreshDevices();
    }
  } catch (e) {
    if (el) el.textContent = "認証局への接続を再試行しています…";
    scheduleDeviceAuthPoll(flow.interval || 2);
  }
}

async function refreshDevicePlan() {
  var el = document.getElementById("device-plan-out");
  if (!el) return;
  try {
    var r = await fetch("/api/device-auth/plan");
    el.textContent = pretty(await r.text());
  } catch (e) {
    el.textContent = String(e);
  }
}

async function refreshStatus() {
  var el = document.getElementById("status");
  if (!el) return;
  var r = await fetch("/api/status");
  el.textContent = await r.text();
}


function applyIme(j) {
  var bar = document.getElementById("ime-bar");
  var pre = document.getElementById("ime-preedit");
  var buf = document.getElementById("ime-buf");
  var tog = document.getElementById("ime-toggle");
  var cands = document.getElementById("ime-candidates");
  if (!bar) return;
  var on = j.on !== undefined ? j.on : j["on?"];
  bar.setAttribute("data-ime", on ? "on" : "off");
  if (pre) pre.textContent = j.preedit || "";
  if (buf) buf.textContent = j.buf || "";
  if (cands) cands.textContent = j.candidates || "";
  if (tog) tog.textContent = on ? "IME 切" : "IME 入";
}

function bindImeOnce() {
  if (window.__aiueosImeBound) return;
  var stage = document.getElementById("wm-stage");
  var tog = document.getElementById("ime-toggle");
  window.__aiueosImeBound = true;
  if (stage) {
    stage.addEventListener("keydown", function (ev) {
      if (ev.metaKey || ev.ctrlKey || ev.altKey) return;
      var k = ev.key;
      if (!k) return;
      if (k.length === 1 || k === "Enter" || k === "Escape" || k === "Backspace" || k === " ") {
        ev.preventDefault();
        postKey(k === " " ? "Space" : k);
      }
    });
  }
  if (tog) {
    tog.addEventListener("click", function (ev) {
      ev.preventDefault();
      var bar = document.getElementById("ime-bar");
      var on = bar && bar.getAttribute("data-ime") === "on";
      postIme(!on);
    });
  }
}

function bindWmOnce() {
  if (window.__aiueosWmBound) return;
  var stage = document.getElementById("wm-stage");
  if (!stage) return;
  window.__aiueosWmBound = true;
  stage.addEventListener("click", function (ev) {
    var raiseEl = ev.target.closest("[data-raise]");
    if (raiseEl) {
      ev.preventDefault();
      postRaise(raiseEl.getAttribute("data-raise"));
      return;
    }
    if (ev.target.closest(".wm-guest")) {
      var b = stage.getBoundingClientRect();
      postPointer(Math.round(ev.clientX - b.left), Math.round(ev.clientY - b.top));
    }
  });
}

function applyWmWindows(j) {
  var wins = j.windows || [];
  wins.forEach(function (w) {
    var el = document.getElementById("wm-window-" + w.id);
    if (!el) return;
    var r = w.rect || {};
    el.style.left = (r.x || 0) + "px";
    el.style.top = (r.y || 0) + "px";
    el.style.width = (r.w || 320) + "px";
    el.style.height = (r.h || 240) + "px";
    el.style.zIndex = String(w.z || 1);
    var focused = !!w["focused?"];
    el.classList.toggle("is-focused", focused);
    var chip = el.querySelector(".dads-chip-label");
    if (chip) chip.hidden = !focused;
  });
}

async function refreshDesktop() {
  var el = document.getElementById("compositor-out");
  var input = document.getElementById("wm-input-out");
  bindWmOnce();
  bindImeOnce();
  try {
    var r = await fetch("/api/compositor/desktop");
    var j = await r.json();
    if (el) el.textContent = JSON.stringify(j, null, 2);
    applyWmWindows(j);
    applyIme(j);
    presentKami(j["kami-ir"] || j.kami_ir || j.kamiIr);
    if (input && !input.dataset.wmTouched) {
      input.textContent = pretty({
        wm: j["wm?"],
        front: (j["z-stack"] || [])[0],
        focused: j.focused,
        ime: j["ime-leftover"],
        on: j["on?"],
        preedit: j.preedit,
        decoration: j.decoration
      });
    }
  } catch (e) {
    if (el) el.textContent = String(e);
    if (input) input.textContent = String(e);
  }
}

async function postRaise(id) {
  var input = document.getElementById("wm-input-out");
  try {
    var r = await fetch("/api/compositor/raise", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ id: Number(id) })
    });
    var j = await r.json();
    if (input) {
      input.dataset.wmTouched = "1";
      input.textContent = pretty(j);
    }
    await refreshDesktop();
  } catch (e) {
    if (input) input.textContent = String(e);
  }
}

async function postKey(key) {
  var input = document.getElementById("wm-input-out");
  try {
    var r = await fetch("/api/compositor/key", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ key: key })
    });
    var j = await r.json();
    if (input) {
      input.dataset.wmTouched = "1";
      input.textContent = pretty(j);
    }
    applyIme(j);
    await refreshDesktop();
  } catch (e) {
    if (input) input.textContent = String(e);
  }
}

async function postIme(on) {
  var input = document.getElementById("wm-input-out");
  try {
    var r = await fetch("/api/compositor/ime", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ "on?": !!on })
    });
    var j = await r.json();
    if (input) {
      input.dataset.wmTouched = "1";
      input.textContent = pretty(j);
    }
    applyIme(j);
    await refreshDesktop();
  } catch (e) {
    if (input) input.textContent = String(e);
  }
}

async function postPointer(x, y) {
  var input = document.getElementById("wm-input-out");
  try {
    var r = await fetch("/api/compositor/pointer", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ x: x, y: y })
    });
    var j = await r.json();
    if (input) {
      input.dataset.wmTouched = "1";
      input.textContent = pretty(j);
    }
    await refreshDesktop();
  } catch (e) {
    if (input) input.textContent = String(e);
  }
}

async function presentKami(ir) {
  var canvas = document.getElementById("kami-viewport");
  var out = document.getElementById("kami-out");
  if (!canvas || !out) return;
  var present = window.aiueosKamiPresent;
  if (typeof present !== "function") {
    out.textContent = JSON.stringify({
      outcome: "refused",
      reason: "clear-only-desktop",
      engine: "kami.webgpu",
      note: "beginRenderPass sky-clear is the named red. Executor is kami.webgpu init!/draw!."
    }, null, 2);
    canvas.setAttribute("data-backend", "clear-only");
    canvas.setAttribute("data-executor", "absent");
    return;
  }
  out.textContent = "kami.webgpu init!…";
  try {
    var result = await present(canvas, ir || {});
    canvas.setAttribute("data-executor", "kami.webgpu");
    if (result && result.backend) {
      canvas.setAttribute("data-backend", result.backend);
    }
    out.textContent = JSON.stringify(result, null, 2);
  } catch (e) {
    canvas.setAttribute("data-backend", "error");
    canvas.setAttribute("data-executor", "kami.webgpu");
    out.textContent = JSON.stringify({
      outcome: "unmeasured",
      reason: String(e),
      engine: "kami.webgpu"
    }, null, 2);
  }
}

async function refreshDevices() {
  var el = document.getElementById("devices-out");
  if (!el) return;
  var r = await fetch("/api/devices");
  el.textContent = await r.text();
  refreshDevicePlan();
}

async function bind() {
  var ch = await (await fetch("/api/challenge", {
    method: "POST", headers: { "Content-Type": "application/json" }, body: "{}"
  })).json();
  var at = await (await fetch("/api/attest", {
    method: "POST", headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ nonce: ch.nonce })
  })).json();
  var body = JSON.stringify({
    owner: document.getElementById("owner").value,
    nonce: ch.nonce,
    signature: at.signature,
    path: "phone-http"
  });
  var res = await fetch("/api/bind", {
    method: "POST", headers: { "Content-Type": "application/json" }, body: body
  });
  var text = await res.text();
  document.getElementById("bind-out").textContent = text;
  window.__aiueosSessionState.bound = res.ok;
}

async function cycle() {
  var res = await fetch("/api/power", {
    method: "POST", headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ action: "cycle" })
  });
  document.getElementById("cycle-out").textContent = await res.text();
  refreshStatus();
}

async function readCid() {
  var el = document.getElementById("cid-out");
  el.textContent = "reading via session process…";
  var res = await fetch("/api/session/read-cid", {
    method: "POST", headers: { "Content-Type": "application/json" }, body: "{}"
  });
  el.textContent = pretty(await res.text());
}

async function runInfer() {
  var el = document.getElementById("infer-out");
  el.textContent = "inferring via session process (murakumo-main)…";
  var res = await fetch("/api/session/infer", {
    method: "POST", headers: { "Content-Type": "application/json" }, body: "{}"
  });
  el.textContent = pretty(await res.text());
}


async function refreshGuest() {
  var el = document.getElementById("guest-out");
  var desk = document.getElementById("guest-desktop-out");
  try {
    var r = await fetch("/api/session/guests");
    var text = await r.text();
    var j = JSON.parse(text);
    if (el) el.textContent = pretty(j);
    if (desk) {
      var g = (j.guests && j.guests[0]) || (j.refused && j.refused[0]) || j;
      desk.textContent = pretty(g);
    }
    if (hSessionEmpty(j)) maybeAutostartGuest(j);
  } catch (e) {
    if (el) el.textContent = String(e);
    if (desk) desk.textContent = String(e);
  }
}

function hSessionEmpty(j) {
  return j && j.count === 0 && (!j.guests || j.guests.length === 0)
    && (!j.refused || j.refused.length === 0);
}

var __aiueosGuestAutostarted = false;
function maybeAutostartGuest(j) {
  if (__aiueosGuestAutostarted) return;
  if (viewId() !== "#session") return;
  if (!hSessionEmpty(j)) return;
  __aiueosGuestAutostarted = true;
  postGuest("allow");
}

async function postGuest(grant) {
  var el = document.getElementById("guest-out");
  if (el) el.textContent = "asking grant for app/notes (" + grant + ")…";
  var res = await fetch("/api/session/guest", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ grant: grant })
  });
  var text = await res.text();
  if (el) el.textContent = pretty(text);
  var desk = document.getElementById("guest-desktop-out");
  if (desk) desk.textContent = pretty(text);
}

async function runGuest() { await postGuest("allow"); }
async function denyGuest() { await postGuest("deny"); }

async function refreshOperator() {
  var el = document.getElementById("operator-out");
  if (!el) return;
  try {
    var r = await fetch("/api/session/operator");
    el.textContent = pretty(await r.text());
  } catch (e) {
    el.textContent = String(e);
  }
}

async function postOperator(grant) {
  var el = document.getElementById("operator-out");
  if (el) el.textContent = "asking operator grant (" + grant + ") via process…";
  var res = await fetch("/api/session/operator", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ grant: grant })
  });
  if (el) el.textContent = pretty(await res.text());
}

async function runOperator() { await postOperator("allow"); }
async function denyOperator() { await postOperator("deny"); }

window.addEventListener("hashchange", show);
document.getElementById("bind").addEventListener("click", bind);
document.getElementById("start-passkey").addEventListener("click", function () {
  startDeviceAuth("passkey");
});
document.getElementById("start-phone-scan").addEventListener("click", function () {
  startDeviceAuth("phone-scan");
});
document.getElementById("cycle").addEventListener("click", cycle);
document.getElementById("read-cid").addEventListener("click", readCid);
document.getElementById("run-infer").addEventListener("click", runInfer);
document.getElementById("run-guest").addEventListener("click", runGuest);
document.getElementById("deny-guest").addEventListener("click", denyGuest);
var runOp = document.getElementById("run-operator");
var denyOp = document.getElementById("deny-operator");
if (runOp) runOp.addEventListener("click", runOperator);
if (denyOp) denyOp.addEventListener("click", denyOperator);
show();
refreshSetup();
