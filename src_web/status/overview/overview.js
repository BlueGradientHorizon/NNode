/* Page logic, see manifest.json for bundle order. */
  function renderOverview() {
    var s, y, sb, mb, gb, nb, d, mtotal, fsz;
    if (!lastSummary || !lastSys) return;
    s = lastSummary;
    y = lastSys;
    sb = document.getElementById("ov-system-body");
    sb.innerHTML = "";
    ovRow(sb, t("ov_model"), y.model || "?");
    ovRow(sb, t("ov_arch"), y.arch || "?");
    ovRow(sb, t("ov_target"), y.target || "?");
    ovRow(sb, t("ov_fw_nnode"), y.fw || "?");
    ovRow(sb, t("ov_fw_idf"), y.idf || "?");
    ovRow(sb, t("ov_chiprev"), String(y.chip_rev));
    ovRow(sb, t("ov_cpu"), y.cpu_mhz + " MHz x " + y.cores);
    ovRow(sb, t("ov_flash"), y.flash_mb + " MB");
    ovRow(sb, t("ov_mac"), y.mac || "?");
    ovRow(sb, t("ov_hostname"), y.hostname || "?");
    ovRow(sb, t("ov_reset"), y.reset || "?");
    d = new Date(y.time * 1000);
    ovRow(sb, t("ov_time"), fmtDateTime(y.time));
    ovRow(sb, t("ov_uptime"), fmtDur(s.uptime));
    ovRow(sb, t("ov_temp"), y.temp_c.toFixed(1) + " C");
    mb = document.getElementById("ov-mem-body");
    mb.innerHTML = "";
    mtotal = s.mem_used + s.mem_free;
    ovBarRow(mb, t("ov_used"), s.mem_used, mtotal, fmtKB);
    ovRow(mb, t("col_largest"), fmtKB(s.largest));
    ovRow(mb, t("col_min"), fmtKB(s.min));
    ovRow(mb, t("col_blocks"), s.ablocks + "/" + s.fblocks);
    gb = document.getElementById("ov-storage-body");
    gb.innerHTML = "";
    fsz = s.fs_blocks * s.fs_bsize;
    (y.partitions || []).forEach(function (p) {
      if (p.label === "storage") return;
      ovRow(gb, p.label + " (" + partType(p) + ")",
        fmtMiB(p.size) + " @ 0x" + p.offset.toString(16));
    });
    ovBarRow(gb, "/data (littlefs)", fsz - s.fs_bfree * s.fs_bsize, fsz, fmtMiB);
    if (lastNet) {
      nb = document.getElementById("ov-net-body");
      nb.innerHTML = "";
      ovRow(nb, "STA", lastNet.ssid || "-");
      ovRow(nb, "BSSID", (lastNet.bssid || "?") + " / ch " +
        lastNet.channel + " / " + lastNet.rssi + " dBm");
      ovRow(nb, "IP", lastNet.ip + " / " + lastNet.mask +
        " / gw " + lastNet.gw);
      ovRow(nb, t("ov_ap"), lastNet.ap_up ? "on" : "off");
      (lastNet.ifaces || []).forEach(function (x) {
        ovRow(nb, x.name, x.ip + " " + x.mac + (x.up ? "" : " (" + t("down") + ")"));
      });
    }
  }

  function renderSummary(j) {
    lastSummary = j;
    renderOverview();
  }

  function refreshOvToggles() {
    document.querySelectorAll(".ov-toggle").forEach(function (b) {
      var id = b.getAttribute("data-body");
      var hide = localStorage.getItem("ov-hide-" + id) === "1";
      var body = document.getElementById(id);
      if (body) body.style.display = hide ? "none" : "";
      b.textContent = hide ? t("ov_show") : t("ov_hide");
    });
  }

  document.querySelectorAll(".ov-toggle").forEach(function (b) {
    b.onclick = function () {
      var k = "ov-hide-" + b.getAttribute("data-body");
      localStorage.setItem(k, localStorage.getItem(k) === "1" ? "0" : "1");
      refreshOvToggles();
    };
  });
