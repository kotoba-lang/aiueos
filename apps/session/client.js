window.__aiueosSessionAlive = true;
window.__aiueosSessionState = { bound: false };

function viewId() {
  var raw = location.hash || "#session";
  var cut = raw.split("?")[0];
  if (cut === "#" || cut === "#/" || cut === "") return "#session";
  if (cut.charAt(1) === "/") cut = "#" + cut.slice(2);
  return cut;
}

function show() {
  var h = viewId();
  ["session", "setup", "manage", "devices"].forEach(function (id) {
    var el = document.getElementById(id);
    if (el) el.hidden = ("#" + id) !== h;
  });
  if (h === "#manage") refreshStatus();
  if (h === "#devices") refreshDevices();
  if (h === "#setup") refreshSetup();
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

async function refreshStatus() {
  var el = document.getElementById("status");
  if (!el) return;
  var r = await fetch("/api/status");
  el.textContent = await r.text();
}

async function refreshDevices() {
  var el = document.getElementById("devices-out");
  if (!el) return;
  var r = await fetch("/api/devices");
  el.textContent = await r.text();
}

async function bind() {
  var ch = await (await fetch("/api/challenge", {
    method: "POST", headers: { "Content-Type": "application/json" }, body: "{}"
  })).json();
  var at = await (await fetch("/api/attest", {
    method: "POST", headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ nonce: ch.nonce })
  })).json();
  var st = await (await fetch("/setup.json")).json();
  var body = JSON.stringify({
    owner: document.getElementById("owner").value,
    token: st.token,
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

window.addEventListener("hashchange", show);
document.getElementById("bind").addEventListener("click", bind);
document.getElementById("cycle").addEventListener("click", cycle);
document.getElementById("read-cid").addEventListener("click", readCid);
document.getElementById("run-infer").addEventListener("click", runInfer);
show();
refreshSetup();
