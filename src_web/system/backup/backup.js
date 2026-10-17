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
