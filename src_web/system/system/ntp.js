/* Page logic, see manifest.json for bundle order. */
  function ntpRow(value) {
    var row = document.createElement("div");
    row.className = "ntp-row";
    var inp = document.createElement("input");
    inp.type = "text";
    inp.size = 28;
    inp.value = value || "";
    var del = document.createElement("button");
    del.type = "button";
    del.className = "btn del";
    del.textContent = "x";
    del.onclick = function () { row.remove(); };
    row.appendChild(inp);
    row.appendChild(del);
    return row;
  }

  function renderNtp(j) {
    document.getElementById("ntp-en").checked = !!j.enabled;
    var box = document.getElementById("ntp-list");
    box.innerHTML = "";
    j.servers.forEach(function (s) { box.appendChild(ntpRow(s)); });
    document.getElementById("ntp-status").textContent =
      (j.synced ? t("ntp_synced") : t("ntp_nosync"));
  }

  document.getElementById("ntp-plus").onclick = function () {
    var box = document.getElementById("ntp-list");
    var add = document.getElementById("ntp-add");
    if (!add.value) return;
    box.appendChild(ntpRow(add.value));
    add.value = "";
  };

  function ntpSyncNow() {
    fetch("/api/ntp/sync", { method: "POST" }).then(function () {
      setTimeout(function () { get("/api/ntp", renderNtp); }, 2000);
    });
  }

  document.getElementById("ntp-sync").onclick = ntpSyncNow;
