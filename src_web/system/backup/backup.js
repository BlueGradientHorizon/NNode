/* Page logic, see manifest.json for bundle order. */
  document.getElementById("bk-dl").onclick = function () {
    fetch("/api/backup").then(function (r) {
      if (!r.ok) throw 0;
      return r.blob();
    }).then(function (b) {
      var url = URL.createObjectURL(b);
      var a = document.createElement("a");
      a.href = url;
      a.download = "nnode-backup.cbor";
      document.body.appendChild(a);
      a.click();
      a.remove();
      setTimeout(function () { URL.revokeObjectURL(url); }, 1000);
      toast("toast_downloaded", true);
    }).catch(function () {
      toastErr();
    });
  };

  document.getElementById("bk-up2").onclick = function () {
    document.getElementById("bk-pick").click();
  };

  document.getElementById("bk-pick").onchange = function () {
    var pick = document.getElementById("bk-pick");
    var f = pick.files[0];
    if (!f) return;
    pick.value = "";
    confirmModal(t("bk_confirm"), function () {
      fetch("/api/backup/restore", { method: "POST", body: f })
        .then(function (r) { return r.json(); }).then(function (j) {
          if (j.ok) toast("toast_rebooting", true);
          else toastErr(j);
        }).catch(function () {
          toastErr();
        });
    });
  };

  document.getElementById("fr-go").onclick = function () {
    confirmModal(t("fr_confirm"), function () {
      fetch("/api/factory-reset", { method: "POST" })
        .then(function (r) { return r.json(); }).then(function (j) {
          if (j.ok) {
            toast("toast_rebooting", true);
          } else {
            toastErr(j);
          }
        }).catch(function () {
          toastErr();
        });
    });
  };

  /* A/B firmware slots. Polled with the page (see router pollFor). */
  function renderOta(j) {
    var box = document.getElementById("ota-slots");
    var rb, canRb = false, i, s, d, b;
    if (!box || !j) return;
    box.innerHTML = "";
    for (i = 0; i < (j.slots || []).length; i++) {
      s = j.slots[i];
      d = document.createElement("div");
      d.className = "ln";
      b = document.createElement("b");
      b.textContent = s.label + ": ";
      d.appendChild(b);
      d.appendChild(document.createTextNode(
        (s.version || "?") + " - " + t("ota_st_" + (s.state || "empty")) +
        (s.running ? " (" + t("ota_active") + ")" : "")));
      box.appendChild(d);
      if (!s.running && s.version) canRb = true;
    }
    rb = document.getElementById("ota-rb");
    if (rb) rb.disabled = !canRb;
  }

  function loadOtaStatus() {
    get("/api/ota/status", renderOta);
  }

  document.getElementById("ota-up").onclick = function () {
    document.getElementById("ota-pick").click();
  };

  document.getElementById("ota-pick").onchange = function () {
    var pick = document.getElementById("ota-pick");
    var f = pick.files[0];
    if (!f) return;
    pick.value = "";
    confirmModal(t("ota_confirm"), function () {
      /* fetch has no upload progress, XHR does. */
      var xhr = new XMLHttpRequest();
      var bar = document.getElementById("ota-prog");
      var fill = document.getElementById("ota-fill");
      bar.hidden = false;
      fill.style.width = "0%";
      xhr.upload.onprogress = function (e) {
        if (e.lengthComputable) {
          fill.style.width = Math.round(100 * e.loaded / e.total) + "%";
        }
      };
      xhr.onload = function () {
        var j = null;
        bar.hidden = true;
        try { j = JSON.parse(xhr.responseText); } catch (e) { /* fall through */ }
        if (xhr.status === 200 && j && j.ok) {
          fill.style.width = "100%";
          toast("toast_rebooting", true);
        } else {
          toastErr(j);
          loadOtaStatus();
        }
      };
      xhr.onerror = function () {
        bar.hidden = true;
        toastErr();
      };
      xhr.open("POST", "/api/ota/upload");
      xhr.send(f);
    });
  };

  document.getElementById("ota-rb").onclick = function () {
    confirmModal(t("ota_rb_confirm"), function () {
      fetch("/api/ota/rollback", { method: "POST" })
        .then(function (r) { return r.json(); }).then(function (j) {
          if (j.ok) {
            toast("toast_rebooting", true);
          } else {
            toastErr(j);
          }
        }).catch(function () {
          toastErr();
        });
    });
  };
