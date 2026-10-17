/* Page logic, see manifest.json for bundle order. */
  document.getElementById("do-reboot").onclick = function () {
    confirmModal(t("rb_confirm"), function () {
      fetch("/api/reboot", { method: "POST" });
      toast("toast_rebooting", true);
    });
  };
