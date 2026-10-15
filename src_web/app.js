/* Nested routes #/section/page, /api/* polling. */
(function () {
  function storedLang() { return localStorage.getItem("lang") || "auto"; }
  function lang() {
    var s = storedLang();
    if (s === "ru" || s === "en") return s;
    var b = (navigator.language || "en").toLowerCase();
    return b.indexOf("ru") === 0 ? "ru" : "en";
  }
  function timeFmt() { return localStorage.getItem("timefmt") || "24"; }
  function fmtDateTime(ts) {
    var d = new Date(ts * 1000);
    if (isNaN(d.getTime())) return String(ts);
    return d.toLocaleString(lang(), { hour12: timeFmt() === "12" });
  }
  function t(k) {
    var l = lang();
    return (I18N[l] && I18N[l][k]) || k;
  }
  var SECTIONS = [
    { id: "status", pages: ["overview", "graphs", "processes", "log", "channels"] },
    { id: "system", pages: ["system", "backup", "reboot"] },
    { id: "programs", pages: ["modules"] },
    { id: "network", pages: ["interfaces", "wireless", "routes", "diagnostics"] },
    { id: "files", pages: ["files"] },
    { id: "console", pages: ["console"] }
  ];
  function findSection(id) {
    for (var i = 0; i < SECTIONS.length; i++) {
      if (SECTIONS[i].id === id) return SECTIONS[i];
    }
    return SECTIONS[0];
  }
  function routeFromHash() {
    var m = (window.location.hash || "").match(/^#\/([a-z]+)\/([a-z]+)/);
    var sec = findSection(m ? m[1] : "");
    var page = (m && sec.pages.indexOf(m[2]) >= 0) ? m[2] : sec.pages[0];
    return { sec: sec.id, page: page };
  }
  function closeMenus() {
    document.querySelectorAll("#sections .menu.open").forEach(function (m) {
      m.classList.remove("open");
    });
  }
  document.addEventListener("click", function () { closeMenus(); });
  function showRoute(r) {
    var sec = findSection(r.sec);
    var sb = document.getElementById("sections");
    sb.innerHTML = "";
    SECTIONS.forEach(function (s) {
      var multi = s.pages.length > 1;
      var wrap = document.createElement("div");
      wrap.className = "menu" + (multi ? " has-drop" : " single");
      var b = document.createElement("button");
      b.type = "button";
      b.className = "menu-btn";
      b.textContent = t("sec_" + s.id);
      if (!multi) {
        b.onclick = function () {
          var nr = "#/" + s.id + "/" + s.pages[0];
          if (window.location.hash === nr) showRoute({ sec: s.id, page: s.pages[0] });
          else window.location.hash = nr;
        };
      } else {
        b.setAttribute("aria-haspopup", "true");
        b.onclick = function (e) {
          e.stopPropagation();
          var was = wrap.classList.contains("open");
          closeMenus();
          if (!was) wrap.classList.add("open");
        };
      }
      wrap.appendChild(b);
      if (multi) {
        var drop = document.createElement("div");
        drop.className = "menu-drop";
        s.pages.forEach(function (p) {
          var pb = document.createElement("button");
          pb.type = "button";
          pb.textContent = t("p_" + p);
          pb.onclick = function (e) {
            e.stopPropagation();
            closeMenus();
            var nr = "#/" + s.id + "/" + p;
            if (window.location.hash === nr) showRoute({ sec: s.id, page: p });
            else window.location.hash = nr;
          };
          drop.appendChild(pb);
        });
        wrap.appendChild(drop);
      }
      sb.appendChild(wrap);
    });
    var pid = "page-" + sec.id + (sec.pages.length > 1 ? "-" + r.page : "");
    document.querySelectorAll("main section").forEach(function (s) {
      s.hidden = s.id !== pid;
    });
    /* Poll only own section: the old timer is stopped,
     * the new one fires the first request at once, then every 3 s. */
    var pg = sec.pages.indexOf(r.page) >= 0 ? r.page : sec.pages[0];
    setPoll(pollFor(sec.id, pg));
  }
  var pollTimer = 0;
  function setPoll(fn) {
    if (pollTimer) {
      clearInterval(pollTimer);
      pollTimer = 0;
    }
    if (fn) {
      fn();
      pollTimer = setInterval(fn, 3000);
    }
  }
  function pollFor(secId, page) {
    var k = secId + "/" + page;
    if (k === "status/overview") return function () {
      pollSummary(); get("/api/sys", renderSys); get("/api/net", renderNet);
    };
    if (k === "status/graphs") return function () {
      get("/api/graphs", renderGraphs);
    };
    if (k === "status/processes") return function () {
      get("/api/tasks", renderTasks);
    };
    if (k === "status/log") return function () { pollLog(); };
    if (k === "system/system") return function () {
      get("/api/sys", renderSys);
    };
    if (k === "programs/modules") return function () {
      get("/api/files", renderAll);
    };
    if (k === "network/interfaces") return function () {
      get("/api/net", renderNet); get("/api/ap/stations", renderStations);
    };
    if (k === "network/wireless") return function () {
      get("/api/net", renderNet); get("/api/nets", renderNets); pollAp();
      get("/api/ap/stations", renderStations);
    };
    if (k === "network/routes") return function () {
      get("/api/net", renderNet);
    };
    if (k === "files/files") return function () {
      get("/api/files", renderAll);
      get("/api/sys", renderFilesSys);
    };
    return null;
  }
  window.addEventListener("hashchange", function () {
    showRoute(routeFromHash());
    var r = routeFromHash();
    if (r.sec === "status" && r.page === "channels" &&
        document.getElementById("chan-body").children.length === 0) {
      chanRefresh();
    }
  });
  function applyLang(l) {
    var e;
    localStorage.setItem("lang", l);
    e = (l === "ru" || l === "en") ? l : lang();
    document.querySelectorAll("[data-i18n]").forEach(function (el) {
      var k = el.getAttribute("data-i18n");
      if (I18N[e] && I18N[e][k]) el.textContent = I18N[e][k];
    });
    document.documentElement.lang = e;
    var ls = document.getElementById("lang-select");
    if (ls) ls.value = l;
    showRoute(routeFromHash());
    refreshOvToggles();
    drawGTab();
    var rl = routeFromHash();
    setPoll(pollFor(rl.sec, rl.page));
  }

  function fmtKB(bytes) { return (bytes / 1024).toFixed(0) + " KB"; }
  function fmtTime(s) {
    s = Math.floor(s);
    var h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60);
    return h + "h " + m + "m " + (s % 60) + "s";
  }
  function fmtMiB(bytes) { return (bytes / 1048576).toFixed(2) + " MiB"; }
  function fmtDur(s) {
    s = Math.floor(s);
    var d = Math.floor(s / 86400), h = Math.floor((s % 86400) / 3600),
      m = Math.floor((s % 3600) / 60);
    return (d ? d + "d " : "") + h + "h " + m + "m " + (s % 60) + "s";
  }
  function get(url, ok) {
    fetch(url).then(function (r) {
      if (!r.ok) throw 0;
      return r.json();
    }).then(ok).catch(function () { /* stay silent until the next poll */ });
  }
  function ovRow(tb, label, value) {
    var tr = document.createElement("tr");
    tr.innerHTML = "<td></td><td></td>";
    tr.children[0].textContent = label;
    tr.children[1].textContent = value;
    tb.appendChild(tr);
  }
  function ovBarRow(tb, label, part, total, fmt) {
    var pct = total > 0 ? (100 * part / total) : 0;
    var tr = document.createElement("tr");
    tr.innerHTML = "<td></td><td></td>";
    tr.children[0].textContent = label;
    var val = document.createElement("div");
    val.className = "ov-val";
    val.textContent = fmt(part) + " / " + fmt(total) +
      " (" + pct.toFixed(0) + "%)";
    var bar = document.createElement("div");
    bar.className = "ov-bar";
    var fill = document.createElement("div");
    fill.className = "ov-fill";
    fill.style.width = pct.toFixed(1) + "%";
    bar.appendChild(fill);
    tr.children[1].appendChild(val);
    tr.children[1].appendChild(bar);
    tb.appendChild(tr);
  }
  function mkbtn(text, cls, onclick, disabled) {
    var b = document.createElement("button");
    b.type = "button";
    b.className = "btn " + (cls || "");
    b.textContent = text;
    if (disabled) b.disabled = true;
    if (onclick) b.onclick = onclick;
    return b;
  }
  var lastSummary = null, lastSys = null, lastNet = null;
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
  function fmtMB(bytes) { return (bytes / 1048576).toFixed(2) + " MB"; }
  function partType(p) {
    if (p.type === 0) return p.subtype === 0 ? "factory" : "ota_" + (p.subtype - 16);
    if (p.type === 1) {
      if (p.subtype === 1) return "phy";
      if (p.subtype === 2) return "nvs";
      if (p.subtype === 131) return "littlefs";
      return "data";
    }
    return "?";
  }
  function partColor(p) {
    if (p.type === 0) return p.subtype === 0 ? "#06c" : "#080";
    if (p.subtype === 2) return "#888";
    if (p.subtype === 131) return "#a60";
    return "#555";
  }
  function renderSys(j) {
    lastSys = j;
    renderOverview();
    document.getElementById("foot-ver").textContent =
      "NNode " + j.fw + " / IDF " + j.idf;
    document.getElementById("sys-time").textContent = fmtDateTime(j.time);
    var total = 0, i;
    for (i = 0; i < j.partitions.length; i++) total += j.partitions[i].size;
    var bar = document.getElementById("parts-bar");
    var leg = document.getElementById("parts-legend");
    bar.innerHTML = "";
    leg.innerHTML = "";
    j.partitions.forEach(function (p) {
      var seg = document.createElement("div");
      seg.className = "seg";
      seg.style.width = (100 * p.size / total).toFixed(2) + "%";
      seg.style.background = partColor(p);
      var tip = p.label + " (" + partType(p) + ")\n0x" +
        p.offset.toString(16) + " +" + fmtMB(p.size);
      if (p.label === "storage") {
        tip += "\n" + t("used_of") + ": " + fmtKB(j.fs_used) +
          " / " + t("free") + ": " + fmtKB(j.fs_total - j.fs_used);
      }
      seg.title = tip;
      bar.appendChild(seg);
      var item = document.createElement("span");
      item.className = "leg";
      item.title = tip;
      var sw = document.createElement("span");
      sw.className = "sw";
      sw.style.background = partColor(p);
      item.appendChild(sw);
      item.appendChild(document.createTextNode(" " + p.label + " " + fmtMB(p.size)));
      leg.appendChild(item);
    });
  }
  function renderTasks(arr) {
    var tb = document.getElementById("tasks-body");
    tb.innerHTML = "";
    arr.forEach(function (x) {
      var tr = document.createElement("tr");
      var cpu = x.cpu < 0 ? "-" : x.cpu.toFixed(1);
      tr.innerHTML = "<td></td><td></td><td></td><td></td><td></td><td></td>";
      var td = tr.children;
      td[0].textContent = x.name;
      td[1].textContent = x.state;
      td[2].textContent = x.prio;
      td[3].textContent = fmtKB(x.stack_hwm);
      td[4].textContent = cpu;
      td[5].textContent = x.core;
      tb.appendChild(tr);
    });
  }
  var gTab = localStorage.getItem("gtab") || "cpu";
  var lastGraphs = null;
  function gTabSeries() {
    var cpu = lastGraphs.cpu.map(function (v) { return v / 100; });
    var heap = lastGraphs.heap.map(function (v) { return v / 1024; });
    if (gTab === "heap") {
      return { key: "g_heap",
        series: [{ data: heap, color: "#1976d2", name: t("g_heap") }],
        fmt: function (v) { return v.toFixed(0) + " KB"; } };
    }
    if (gTab === "net") {
      return { key: "g_traffic",
        series: [{ data: lastGraphs.rx, color: "#00ac59", name: "RX" },
                 { data: lastGraphs.tx, color: "#1976d2", name: "TX" }],
        fmt: function (v) { return v.toFixed(0) + "/s"; } };
    }
    return { key: "g_cpu",
      series: [{ data: cpu, color: "#e8210d", name: "CPU" }],
      fmt: function (v) { return v.toFixed(1) + " %"; } };
  }
  function agoStr(total, frac) {
    var s = Math.round(total * frac);
    return s >= 60 ? Math.round(s / 60) + "m" : s + "s";
  }
  function drawGTab() {
    var pack, series, step, W = 600, H = 150, n = 0, max = 1, i, k;
    if (!lastGraphs) return;
    pack = gTabSeries();
    series = pack.series;
    step = lastGraphs.step || 3;
    document.getElementById("g-title").textContent = t(pack.key);
    document.querySelectorAll("#g-tabs .g-tab").forEach(function (b) {
      b.className = "g-tab" + (b.getAttribute("data-g") === gTab ? " on" : "");
    });
    series.forEach(function (s) {
      if (s.data.length > n) n = s.data.length;
      s.data.forEach(function (v) { if (v > max) max = v; });
    });
    var p = Math.pow(10, Math.floor(Math.log10(max)));
    var m = max / p;
    max = (m <= 1 ? 1 : m <= 2 ? 2 : m <= 5 ? 5 : 10) * p;
    var base = 4 + (H - 8);
    var total = n * step;
    var s = '<svg viewBox="0 0 600 168" preserveAspectRatio="none" class="graph">';
    for (k = 0; k <= 4; k++) {
      var y = 4 + (H - 8) * k / 4;
      s += '<line x1="34" y1="' + y + '" x2="600" y2="' + y + '" class="grid"/>';
    }
    for (k = 0; k <= 3; k++) {
      var gx = 34 + (W - 34) * k / 3;
      s += '<text x="' + gx.toFixed(1) + '" y="14" text-anchor="middle">' +
        agoStr(total, 1 - k / 3) + '</text>';
    }
    series.forEach(function (ln) {
      var pts = "";
      for (i = 0; i < ln.data.length; i++) {
        var x = n > 1 ? 34 + (W - 34) * i / (n - 1) : 34;
        var y = 4 + (H - 8) * (1 - ln.data[i] / max);
        pts += (i ? " " : "") + x.toFixed(1) + "," + y.toFixed(1);
      }
      if (pts) {
        var x0 = n > 1 ? 34 : 34, x1 = 600;
        s += '<polygon points="' + x0.toFixed(1) + ',' + base + ' ' + pts +
          ' ' + x1.toFixed(1) + ',' + base + '" style="fill:' + ln.color +
          ';fill-opacity:0.3;stroke:none"/>';
        s += '<polyline points="' + pts + '" class="line" style="stroke:' + ln.color + '"/>';
      }
    });
    s += '<text x="0" y="12">' + max + '</text>';
    s += '<text x="0" y="158">0</text></svg>';
    document.getElementById("g-graph").innerHTML = s;
    document.getElementById("g-caption").textContent =
      "(" + Math.round(total / 60) + " " + t("g_minwin") + ", " +
      step + " " + t("g_secint") + ")";
    var tb = document.getElementById("g-stats-body");
    tb.innerHTML = "";
    series.forEach(function (ln) {
      var d = ln.data, nn = d.length;
      var cur = nn ? d[nn - 1] : 0, peak = 0, sum = 0, ii;
      for (ii = 0; ii < nn; ii++) { if (d[ii] > peak) peak = d[ii]; sum += d[ii]; }
      var avg = nn ? sum / nn : 0;
      var tr = document.createElement("tr");
      tr.innerHTML = "<td></td><td></td><td></td><td></td>";
      var lab = document.createElement("span");
      lab.className = "g-series";
      lab.style.borderColor = ln.color;
      lab.textContent = ln.name;
      tr.children[0].appendChild(lab);
      tr.children[1].textContent = t("g_cur") + " " + pack.fmt(cur);
      tr.children[2].textContent = t("g_avg") + " " + pack.fmt(avg);
      tr.children[3].textContent = t("g_peak") + " " + pack.fmt(peak);
      tb.appendChild(tr);
    });
  }
  document.querySelectorAll("#g-tabs .g-tab").forEach(function (b) {
    b.onclick = function () {
      gTab = b.getAttribute("data-g");
      localStorage.setItem("gtab", gTab);
      drawGTab();
    };
  });
  function renderGraphs(j) {
    lastGraphs = j;
    drawGTab();
  }
  var logLevel = "V";
  function renderLog(j) {
    document.getElementById("log-out").textContent = j.lines.join("\n");
  }
  function pollLog() {
    get("/api/log?level=" + encodeURIComponent(logLevel), renderLog);
  }
  document.getElementById("log-level").onchange = function () {
    logLevel = document.getElementById("log-level").value;
    pollLog();
  };
  document.getElementById("log-clear").onclick = function () {
    fetch("/api/log/clear", { method: "POST" }).then(function () { pollLog(); });
  };
  function pollAp() {
    get("/api/ap", function (jj) { lastAp = jj; if (lastNet) renderWlNets(lastNet); });
  }
  function pollSummary() {
    /* Flaky board WiFi/TCP: a single retry after 500 ms */
    fetch("/api/summary").then(function (r) { return r.json(); }).then(function (j) {
      renderSummary(j);
    }).catch(function () {
      setTimeout(function () {
        fetch("/api/summary").then(function (r) { return r.json(); }).then(function (j) {
          renderSummary(j);
        }).catch(function () { /* stay silent until the next poll */ });
      }, 500);
    });
  }
  document.getElementById("time-sync").onclick = function () {
    var ts = Math.floor(Date.now() / 1000);
    fetch("/api/time", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ ts: ts })
    }).then(function () { get("/api/sys", renderSys); });
  };
  function renderNet(j) {
    lastNet = j;
    renderOverview();
    renderNetIf(j);
    renderNetWl(j);
  }
  var lastAp = null, lastStations = [], lastNets = null, lastScan = [];
  function ifaceByName(n) {
    var i;
    if (!lastNet || !lastNet.ifaces) return null;
    for (i = 0; i < lastNet.ifaces.length; i++) {
      if (lastNet.ifaces[i].name === n) return lastNet.ifaces[i];
    }
    return null;
  }
  function fmtPkts(v) { return Number(v || 0).toLocaleString(lang()) + " " + t("pkts"); }
  function dispMac(m) { return (!m || m === "00:00:00:00:00:00") ? "-" : m; }
  function macOk(s) { return /^[0-9a-fA-F]{2}(:[0-9a-fA-F]{2}){5}$/.test(s || ""); }
  function lnRow(info, label, value) {
    var d = document.createElement("div");
    d.className = "ln";
    var b = document.createElement("b");
    b.textContent = label + ": ";
    d.appendChild(b);
    d.appendChild(document.createTextNode(value));
    info.appendChild(d);
  }
  function renderIfaceCards() {
    var box, j, staIf, apIf, staUp, apUp;
    if (!lastNet) return;
    box = document.getElementById("iface-list");
    if (!box) return;
    j = lastNet;
    box.innerHTML = "";
    staIf = ifaceByName("sta");
    apIf = ifaceByName("ap");
    staUp = !!staIf && !!staIf.up && j.ip && j.ip !== "0.0.0.0";
    apUp = !!j.ap_up;
    (function () {
      var row = document.createElement("div");
      row.className = "iface-row";
      var icon = document.createElement("div");
      icon.className = "iface-icon";
      var nm = document.createElement("div");
      nm.className = "in-name sta";
      nm.textContent = "STA";
      var sub = document.createElement("div");
      sub.className = "in-sub";
      sub.textContent = "sta";
      icon.appendChild(nm);
      icon.appendChild(sub);
      var info = document.createElement("div");
      info.className = "iface-info";
      lnRow(info, t("if_protocol"), t("proto_dhcp"));
      lnRow(info, t("if_carrier"), staUp ? t("present") : t("absent"));
      lnRow(info, t("if_uptime"), fmtDur(j.sta_uptime || 0));
      lnRow(info, "MAC", staIf ? dispMac(staIf.mac) : "?");
      lnRow(info, "RX", fmtPkts(staIf ? staIf.rx : 0));
      lnRow(info, "TX", fmtPkts(staIf ? staIf.tx : 0));
      lnRow(info, "IPv4", j.cidr || (j.ip + " / " + j.mask));
      lnRow(info, t("encryption"), j.encr || "-");
      var act = document.createElement("div");
      act.className = "iface-actions";
      act.appendChild(mkbtn(t("restart"), "", function () {
        fetch("/api/renew", { method: "POST" }).then(function () {
          get("/api/net", renderNet);
        });
      }));
      act.appendChild(mkbtn(t("stop"), "", function () {
        fetch("/api/sta/disconnect", { method: "POST" }).then(function () {
          get("/api/net", renderNet);
          get("/api/nets", renderNets);
        });
      }, !staUp));
      row.appendChild(icon);
      row.appendChild(info);
      row.appendChild(act);
      box.appendChild(row);
    })();
    (function () {
      var row = document.createElement("div");
      row.className = "iface-row";
      var icon = document.createElement("div");
      icon.className = "iface-icon";
      var nm = document.createElement("div");
      nm.className = "in-name ap";
      nm.textContent = "AP";
      var sub = document.createElement("div");
      sub.className = "in-sub";
      sub.textContent = "ap";
      icon.appendChild(nm);
      icon.appendChild(sub);
      var info = document.createElement("div");
      info.className = "iface-info";
      lnRow(info, t("if_protocol"), t("proto_static"));
      lnRow(info, t("if_carrier"), apUp ? t("present") : t("absent"));
      lnRow(info, t("if_uptime"), fmtDur(j.ap_uptime || 0));
      lnRow(info, "MAC", apIf ? dispMac(apIf.mac) : "?");
      lnRow(info, "RX", fmtPkts(apIf ? apIf.rx : 0));
      lnRow(info, "TX", fmtPkts(apIf ? apIf.tx : 0));
      lnRow(info, "IPv4", apIf ? apIf.ip : "?");
      lnRow(info, t("clients"), String(lastStations.length));
      row.appendChild(icon);
      row.appendChild(info);
      box.appendChild(row);
    })();
  }
  function renderRoutes() {
    var tb;
    if (!lastNet) return;
    tb = document.getElementById("routes-body");
    if (!tb) return;
    tb.innerHTML = "";
    (lastNet.routes || []).forEach(function (x) {
      var tr = document.createElement("tr");
      tr.innerHTML = "<td></td><td></td><td></td>";
      tr.children[0].textContent = x.dst;
      tr.children[1].textContent = x.gw;
      tr.children[2].textContent = x.iface;
      tb.appendChild(tr);
    });
  }
  function renderNetIf(j) {
    renderIfaceCards();
    renderRoutes();
  }
  function wlFreq(ch) {
    if (!ch) return "-";
    if (ch === 14) return "2.484 GHz";
    return (2.407 + 0.005 * ch).toFixed(3) + " GHz";
  }
  function sigBox(rssi) {
    var box = document.createElement("div");
    box.className = "sigbox";
    box.innerHTML = sigBars(rssi);
    box.appendChild(document.createTextNode(rssi + "/-92 dBm"));
    return box;
  }
  function apAuthName(a) {
    if (a === "open") return "None";
    if (a === "wpa2") return "WPA2 PSK";
    if (a === "wpa_wpa2") return "WPA/WPA2 PSK";
    return a || "?";
  }
  /* Modal stack: each new modal opens above the open one with its own
   * dimming layer, 15px lower. closeModal removes the topmost. */
  function openModal(title, bodyEl, footBtns) {
    var ov = document.getElementById("modal-ov");
    var layer = document.createElement("div");
    layer.className = "modal-layer";
    layer.style.paddingTop = (60 + ov.children.length * 15) + "px";
    layer.addEventListener("click", function (e) {
      if (e.target === layer) closeModal();
    });
    var modal = document.createElement("div");
    modal.className = "modal";
    modal.setAttribute("role", "dialog");
    var head = document.createElement("div");
    head.className = "modal-head";
    var ht = document.createElement("span");
    ht.textContent = title;
    head.appendChild(ht);
    if (!title) head.style.display = "none";
    var b = document.createElement("div");
    b.className = "modal-body";
    b.appendChild(bodyEl);
    var f = document.createElement("div");
    f.className = "modal-foot";
    (footBtns || []).forEach(function (x) { f.appendChild(x); });
    modal.appendChild(head);
    modal.appendChild(b);
    modal.appendChild(f);
    layer.appendChild(modal);
    ov.appendChild(layer);
    ov.hidden = false;
  }
  function closeModal() {
    var ov = document.getElementById("modal-ov");
    if (ov.lastChild) ov.lastChild.remove();
    if (!ov.lastChild) ov.hidden = true;
  }
  function closeAllModals() {
    var ov = document.getElementById("modal-ov");
    ov.innerHTML = "";
    ov.hidden = true;
  }
  /* A question with two buttons: text plus OK/Close */
  function confirmModal(msg, onYes, yesKey) {
    var body = document.createElement("div");
    body.textContent = msg;
    openModal("", body, [
      mkbtn(t(yesKey || "confirm_ok"), "btn-primary", function () {
        closeModal();
        onYes();
      }),
      mkbtn(t("dismiss"), "", closeModal)
    ]);
  }
  var toastTimer = 0;
  function toast(key, ok) {
    document.getElementById("toast-msg").textContent = t(key);
    var box = document.getElementById("toast-box");
    box.className = "toast" + (ok === true ? " ok" : ok === false ? " err" : "");
    document.getElementById("toast-ov").hidden = false;
    if (toastTimer) clearTimeout(toastTimer);
    toastTimer = setTimeout(function () {
      document.getElementById("toast-ov").hidden = true;
      toastTimer = 0;
    }, 1000);
  }
  document.getElementById("toast-ov").addEventListener("click", function () {
    if (toastTimer) clearTimeout(toastTimer);
    toastTimer = 0;
    document.getElementById("toast-ov").hidden = true;
  });
  /* Firmware error code (jj.error) to the err_<code> translation key.
   * Unknown codes and dropped connections give the generic toast_error. */
  function toastErr(jj) {
    var code = jj && typeof jj.error === "string" ? jj.error : "";
    var key = code ? "err_" + code : "toast_error";
    var l = lang();
    if (!I18N[l] || !I18N[l][key]) key = "toast_error";
    toast(key, false);
  }
  document.addEventListener("keydown", function (e) {
    if (e.key === "Escape" && !document.getElementById("modal-ov").hidden) closeModal();
  });
  function openScanModal() {
    var wrap = document.createElement("div");
    var tbl = document.createElement("table");
    tbl.className = "table";
    var thead = document.createElement("thead");
    var hr = document.createElement("tr");
    [t("col_signal"), "SSID", t("channel"), t("ch_mode"), "BSSID", t("encryption"), ""]
      .forEach(function (s) {
        var th = document.createElement("th");
        th.textContent = s;
        hr.appendChild(th);
      });
    thead.appendChild(hr);
    tbl.appendChild(thead);
    var tb = document.createElement("tbody");
    tbl.appendChild(tb);
    wrap.appendChild(tbl);
    var loading = document.createElement("div");
    loading.className = "modal-scan";
    var sp = document.createElement("span");
    sp.className = "spin";
    loading.appendChild(sp);
    loading.appendChild(document.createTextNode(t("scanning")));
    wrap.appendChild(loading);
    openModal(t("scan_title"), wrap, [mkbtn(t("dismiss"), "", closeModal)]);
    get("/api/scan", function (arr) {
      loading.remove();
      lastScan = Array.isArray(arr) ? arr : [];
      renderScanRows(tb, lastScan);
      if (lastNet) renderWlNets(lastNet);
    });
  }
  function renderScanRows(tb, arr) {
    if (!Array.isArray(arr)) arr = [];
    tb.innerHTML = "";
    arr.forEach(function (x) {
      var tr = document.createElement("tr");
      tr.innerHTML = "<td></td><td></td><td></td><td></td><td></td><td></td><td></td>";
      tr.children[0].innerHTML = sigBars(x.rssi);
      tr.children[0].appendChild(document.createTextNode(x.rssi + " dBm"));
      var dot = document.createElement("span");
      dot.className = "ssid-dot";
      dot.style.background = chanColor(x);
      tr.children[1].appendChild(dot);
      tr.children[1].appendChild(document.createTextNode(x.ssid || "?"));
      tr.children[2].textContent = x.channel;
      tr.children[3].textContent = t("mode_master");
      tr.children[4].textContent = x.bssid;
      tr.children[5].textContent = x.auth;
      var btn = mkbtn(t("join"), "btn-primary", (function (s) { return function () {
        if (knownNets.indexOf(s) >= 0) {
          staConnect(s, null);
          closeModal();
        } else {
          openJoinModal(s);
        }
      }; })(x.ssid));
      tr.children[6].appendChild(btn);
      tb.appendChild(tr);
    });
  }
  function renderNetWl(j) {
    renderWlOverview(j);
    renderWlNets(j);
  }
  /* Joining a new network above the scan window: password plus
   * an optional bind to the over-the-air BSSID (saves the net at once). */
  function openJoinModal(ssid) {
    var si = scanInfo(ssid);
    var bssid = si && si.bssid ? si.bssid : "";
    var wrap = document.createElement("div");
    wrap.className = "frm";
    function joinRow(label, node) {
      var r = document.createElement("div");
      r.className = "frm-row";
      var lab = document.createElement("span");
      lab.className = "flab";
      lab.textContent = label;
      var val = document.createElement("div");
      val.className = "fval";
      var p = document.createElement("p");
      p.appendChild(node);
      val.appendChild(p);
      r.appendChild(lab);
      r.appendChild(val);
      wrap.appendChild(r);
      return r;
    }
    var inPass = document.createElement("input");
    inPass.type = "text"; inPass.size = 20; inPass.maxLength = 63;
    inPass.placeholder = t("net_keeppass_open");
    joinRow(t("password"), inPass);
    var inLock = document.createElement("input");
    inLock.type = "checkbox"; inLock.checked = false;
    var labLock = document.createElement("label");
    labLock.appendChild(inLock);
    labLock.appendChild(document.createTextNode(" " + t("net_lock_bssid") +
      (bssid ? " (" + bssid + ")" : "")));
    var lockRow = joinRow("", labLock);
    if (!bssid) lockRow.style.display = "none";
    openModal(ssid, wrap, [
      mkbtn(t("join"), "btn-primary", function () {
        var pass = inPass.value;
        if (inLock.checked && bssid) {
          var url = "/api/nets/add?ssid=" + encodeURIComponent(ssid);
          if (pass) url += "&pass=" + encodeURIComponent(pass);
          url += "&bssid=" + bssid;
          fetch(url, { method: "POST" })
            .then(function (r) { return r.json(); }).then(function (jj) {
              if (jj.ok) {
                closeAllModals();
                toast("toast_saved", true);
                staConnect(ssid, pass || null);
              } else {
                toastErr(jj);
              }
            }).catch(function () {
              toastErr();
            });
        } else {
          closeAllModals();
          staConnect(ssid, pass || null);
        }
      }),
      mkbtn(t("dismiss"), "", closeModal)
    ]);
  }
  function renderWlOverview(j) {
    var box = document.getElementById("wl-radio");
    if (!box) return;
    box.innerHTML = "";
    var row = document.createElement("div");
    row.className = "iface-row";
    var icon = document.createElement("div");
    icon.className = "iface-icon";
    var nm = document.createElement("div");
    nm.className = "in-name ap";
    nm.textContent = "radio";
    var sub = document.createElement("div");
    sub.className = "in-sub";
    sub.textContent = (typeof lastSys !== "undefined" && lastSys && lastSys.model) || "NNode";
    icon.appendChild(nm);
    icon.appendChild(sub);
    var info = document.createElement("div");
    info.className = "iface-info";
    var title = document.createElement("div");
    title.className = "wl-title";
    title.textContent = ((typeof lastSys !== "undefined" && lastSys && lastSys.model) || "NNode") + " 802.11b/g/n";
    info.appendChild(title);
    lnRow(info, t("channel"), j.channel + " (" + wlFreq(j.channel) + ") | " +
      t("bitrate") + ": -");
    var act = document.createElement("div");
    act.className = "iface-actions";
    act.appendChild(mkbtn(t("restart"), "", function () {
      if (j.ssid && j.ssid !== "-") staConnect(j.ssid, null);
      get("/api/net", renderNet);
    }));
    act.appendChild(mkbtn(t("scan"), "btn-primary", function () { openScanModal(); }));
    act.appendChild(mkbtn(t("add"), "btn-ok", function () { openAddModal(); }));
    row.appendChild(icon);
    row.appendChild(info);
    row.appendChild(act);
    box.appendChild(row);
  }
  function scanAuthName(a) {
    if (a === "open") return "None";
    if (a === "wep") return "WEP";
    if (a === "wpa") return "WPA PSK";
    if (a === "wpa2") return "WPA2 PSK";
    if (a === "wpa/wpa2") return "WPA/WPA2 PSK";
    if (a === "enterprise") return "Enterprise";
    if (a === "wpa3") return "WPA3 SAE";
    if (a === "wpa2/wpa3") return "WPA2/WPA3 PSK";
    if (a === "wapi") return "WAPI PSK";
    return a || "?";
  }
  function scanInfo(ssid) {
    var i;
    for (i = 0; i < lastScan.length; i++) {
      if (lastScan[i].ssid === ssid) return lastScan[i];
    }
    return null;
  }
  function storedNet(ssid) {
    var i, ns = lastNets ? lastNets.nets : [];
    for (i = 0; i < ns.length; i++) {
      if (ns[i].ssid === ssid) return ns[i];
    }
    return null;
  }
  function storedAuthName(a) {    if (a === "open") return "None";
    if (a === "wpa") return "WPA PSK";
    if (a === "wpa2") return "WPA2 PSK";
    if (a === "wpa3") return "WPA3 SAE";
    return "Auto";
  }
  function authSelect(cur) {
    var sel = document.createElement("select");
    ["auto", "open", "wpa", "wpa2", "wpa3"].forEach(function (a) {
      var o = document.createElement("option");
      o.value = a; o.textContent = a;
      if ((cur || "auto") === a) o.selected = true;
      sel.appendChild(o);
    });
    return sel;
  }
  function savedBssid(ssid) {
    var st = storedNet(ssid), si;
    if (st && st.bssid) return st.bssid;
    si = scanInfo(ssid);
    return (si && si.bssid) ? si.bssid : "-";
  }
  function savedEncr(ssid) {
    var st = storedNet(ssid), si;
    if (st && st.auth && st.auth !== "auto") return storedAuthName(st.auth);
    si = scanInfo(ssid);
    return si ? scanAuthName(si.auth) : "?";
  }
  function offBox() {
    var off = document.createElement("div");
    off.className = "sigbox off";
    off.textContent = "X " + t("down");
    return off;
  }
  function forgetNet(ssid) {
    if (!ssid) return;
    confirmModal(t("del_net_confirm").replace("{s}", ssid), function () {
      fetch("/api/nets/forget?ssid=" + encodeURIComponent(ssid), { method: "POST" })
        .then(function () {
          get("/api/nets", renderNets);
          get("/api/net", renderNet);
        });
    }, "delete");
  }
  function openAddModal() {
    var wrap = document.createElement("div");
    wrap.className = "frm";
    function addRow(label, node) {
      var r = document.createElement("div");
      r.className = "frm-row";
      var lab = document.createElement("span");
      lab.className = "flab";
      lab.textContent = label;
      var val = document.createElement("div");
      val.className = "fval";
      var p = document.createElement("p");
      p.appendChild(node);
      val.appendChild(p);
      r.appendChild(lab);
      r.appendChild(val);
      wrap.appendChild(r);
    }
    var inSsid = document.createElement("input");
    inSsid.type = "text"; inSsid.size = 20; inSsid.maxLength = 32;
    addRow("ESSID", inSsid);
    var inBssid = document.createElement("input");
    inBssid.type = "text"; inBssid.size = 20; inBssid.maxLength = 17;
    inBssid.placeholder = t("any");
    addRow("BSSID", inBssid);
    var inAuth = authSelect("auto");
    addRow(t("encryption"), inAuth);
    var inPass = document.createElement("input");
    inPass.type = "text"; inPass.size = 20; inPass.maxLength = 63;
    inPass.placeholder = t("net_keeppass_open");
    addRow(t("password"), inPass);
    openModal(t("net_add_title"), wrap, [
      mkbtn(t("save"), "btn-primary", function () {
        var ss = inSsid.value;
        if (!ss) {
          toast("toast_error", false);
          return;
        }
        if (inAuth.value !== "open" && inAuth.value !== "auto" && !inPass.value) {
          toast("toast_error", false);
          return;
        }
        if (inBssid.value && !macOk(inBssid.value)) {
          toast("toast_error", false);
          return;
        }
        var url = "/api/nets/add?ssid=" + encodeURIComponent(ss);
        if (inPass.value) url += "&pass=" + encodeURIComponent(inPass.value);
        if (inBssid.value) url += "&bssid=" + inBssid.value;
        if (inAuth.value !== "auto") url += "&auth=" + encodeURIComponent(inAuth.value);
        fetch(url, { method: "POST" })
          .then(function (r) { return r.json(); }).then(function (jj) {
            if (jj.ok) {
              closeModal();
              toast("toast_saved", true);
              get("/api/nets", renderNets);
            } else {
              toastErr(jj);
            }
          }).catch(function () {
            toastErr();
          });
      }),
      mkbtn(t("dismiss"), "", closeModal)
    ]);
  }
  function openNetModal(ssid) {
    var st = storedNet(ssid);
    var si = st ? null : scanInfo(ssid);
    var wrap = document.createElement("div");
    wrap.className = "frm";
    function netRow(label, node) {
      var r = document.createElement("div");
      r.className = "frm-row";
      var lab = document.createElement("span");
      lab.className = "flab";
      lab.textContent = label;
      var val = document.createElement("div");
      val.className = "fval";
      var p = document.createElement("p");
      p.appendChild(node);
      val.appendChild(p);
      r.appendChild(lab);
      r.appendChild(val);
      wrap.appendChild(r);
    }
    var inSsid = document.createElement("input");
    inSsid.type = "text"; inSsid.size = 20; inSsid.maxLength = 32;
    inSsid.value = ssid;
    netRow("ESSID", inSsid);
    var inBssid = document.createElement("input");
    inBssid.type = "text"; inBssid.size = 20; inBssid.maxLength = 17;
    inBssid.placeholder = t("any");
    inBssid.value = st && st.bssid ? st.bssid :
      (si && si.bssid ? si.bssid : "");
    netRow("BSSID", inBssid);
    var inAuth = authSelect(st && st.auth ? st.auth :
      (si ? si.auth : "auto"));
    /* the scan reports its own names (wpa/wpa2...), the select has only ours */
    if (["auto", "open", "wpa", "wpa2", "wpa3"].indexOf(inAuth.value) < 0) {
      inAuth.value = "auto";
    }
    netRow(t("encryption"), inAuth);
    var inPass = document.createElement("input");
    inPass.type = "text"; inPass.size = 20; inPass.maxLength = 63;
    inPass.placeholder = t("net_keeppass");
    netRow(t("password"), inPass);
    openModal(t("net_edit_title"), wrap, [
      mkbtn(t("save"), "btn-primary", function () {
        var ns = inSsid.value;
        var url = "/api/nets/update?ssid=" + encodeURIComponent(ssid);
        if (!ns) {
          toast("toast_error", false);
          return;
        }
        if (ns !== ssid) url += "&new_ssid=" + encodeURIComponent(ns);
        if (inPass.value) url += "&pass=" + encodeURIComponent(inPass.value);
        if (inBssid.value && !macOk(inBssid.value)) {
          toast("toast_error", false);
          return;
        }
        url += "&bssid=" + inBssid.value;
        url += "&auth=" + encodeURIComponent(inAuth.value);
        fetch(url, { method: "POST" })
          .then(function (r) { return r.json(); }).then(function (jj) {
            if (jj.ok) {
              closeModal();
              toast("toast_saved", true);
              get("/api/nets", renderNets);
              get("/api/net", renderNet);
            } else {
              toastErr(jj);
            }
          }).catch(function () {
            toastErr();
          });
      }),
      mkbtn(t("dismiss"), "", closeModal)
    ]);
  }
  function renderWlNets(j) {
    var box = document.getElementById("wl-nets");
    var staUp, apUp, apIf, cur;
    if (!box || !j) return;
    box.innerHTML = "";
    staUp = !!(j.ip && j.ip !== "0.0.0.0");
    apUp = !!j.ap_up;
    apIf = ifaceByName("ap");
    cur = (j.ssid && j.ssid !== "-") ? j.ssid : null;
    (function () {
      var row = document.createElement("div");
      row.className = "iface-row";
      var ab = document.createElement("div");
      ab.className = "sigbox";
      ab.textContent = apUp ?
        (lastStations.length + " " + t("clients")) : t("down");
      row.appendChild(ab);
      var info = document.createElement("div");
      info.className = "iface-info";
      var apSsid = lastAp ? lastAp.ssid : "?";
      lnRow(info, "SSID", apSsid + " | " + t("mode") + ": " + t("mode_master"));
      lnRow(info, "BSSID", (apIf ? dispMac(apIf.mac) : "-") +
        " | " + t("encryption") + ": " + (lastAp ? apAuthName(lastAp.auth) : "?"));
      var act = document.createElement("div");
      act.className = "iface-actions";
      act.appendChild(mkbtn(t("edit"), "btn-primary", function () { openApModal(); }));
      row.appendChild(info);
      row.appendChild(act);
      box.appendChild(row);
    })();
    if (cur) {
      (function () {
        var row = document.createElement("div");
        row.className = "iface-row";
        row.appendChild(staUp ? sigBox(j.rssi) : offBox());
        var info = document.createElement("div");
        info.className = "iface-info";
        lnRow(info, "SSID", cur + " | " + t("mode") + ": " + t("mode_client"));
        lnRow(info, "BSSID", dispMac(j.bssid) + " | " + t("encryption") +
          ": " + (j.encr || "-"));
        var act = document.createElement("div");
        act.className = "iface-actions";
        if (staUp) {
          act.appendChild(mkbtn(t("disconnect"), "", function () {
            fetch("/api/sta/disconnect", { method: "POST" }).then(function () {
              get("/api/net", renderNet);
              get("/api/nets", renderNets);
            });
          }));
        } else {
          act.appendChild(mkbtn(t("connect"), "btn-primary", function () {
            staConnect(cur, null);
          }));
        }
        act.appendChild(mkbtn(t("edit"), "", (function (s) { return function () {
          openNetModal(s);
        }; })(cur)));
        act.appendChild(mkbtn(t("delete"), "btn-danger", (function (s) { return function () {
          forgetNet(s);
        }; })(cur)));
        row.appendChild(info);
        row.appendChild(act);
        box.appendChild(row);
      })();
    }
    (lastNets ? lastNets.nets : []).forEach(function (x) {
      if (x.ssid === cur) return;
      var row = document.createElement("div");
      row.className = "iface-row";
      row.appendChild(offBox());
      var info = document.createElement("div");
      info.className = "iface-info";
      lnRow(info, "SSID", x.ssid + " | " + t("mode") + ": " + t("mode_client"));
      lnRow(info, "BSSID", savedBssid(x.ssid) +
        " | " + t("encryption") + ": " + savedEncr(x.ssid));
      var act = document.createElement("div");
      act.className = "iface-actions";
      act.appendChild(mkbtn(t("connect"), "btn-primary", (function (s) { return function () {
        staConnect(s, null);
      }; })(x.ssid)));
      act.appendChild(mkbtn(t("edit"), "", (function (s) { return function () {
        openNetModal(s);
      }; })(x.ssid)));
      act.appendChild(mkbtn(t("delete"), "btn-danger", (function (s) { return function () {
        forgetNet(s);
      }; })(x.ssid)));
      row.appendChild(info);
      row.appendChild(act);
      box.appendChild(row);
    });
  }
  var knownNets = [];
  function renderFilesSys(j) {
    var el = document.getElementById("files-space-text");
    var fill = document.getElementById("files-space-fill");
    if (!el || !fill || !j) return;
    var total = j.fs_total || 0;
    var used = j.fs_used || 0;
    var free = total > used ? total - used : 0;
    var pct = total > 0 ? (100 * used / total) : 0;
    el.textContent = t("disk_space") + ": " + pct.toFixed(0) + "% " +
      t("used_of") + " (" + fmtMiB(used) + " " + t("used_of") + " " +
      t("of_word") + " " + fmtMiB(total) + ", " + fmtMiB(free) + " " +
      t("free") + ")";
    fill.style.width = pct.toFixed(1) + "%";
  }
  function renderFiles(arr) {
    var tb = document.getElementById("files-body");
    tb.innerHTML = "";
    arr.forEach(function (x) {
      var tr = document.createElement("tr");
      tr.innerHTML = "<td></td><td></td><td></td>";
      tr.children[0].textContent = x.name;
      tr.children[1].textContent = x.size + " B";
      var bd = mkbtn(t("download"), "", function () {
        window.location = "/api/files/download?name=" + encodeURIComponent(x.name);
      });
      var bx = mkbtn(t("delete"), "btn-danger", (function (s) { return function () {
        confirmModal(t("del_file_confirm").replace("{s}", s), function () {
          fetch("/api/files/delete?name=" + encodeURIComponent(s), { method: "POST" })
            .then(function () {
              get("/api/files", renderAll);
              get("/api/sys", renderFilesSys);
            });
        }, "delete");
      }; })(x.name));
      tr.children[2].appendChild(bd);
      tr.children[2].appendChild(document.createTextNode(" "));
      tr.children[2].appendChild(bx);
      tb.appendChild(tr);
    });
  }
  function renderModules(arr) {
    var tb = document.getElementById("modules-body");
    tb.innerHTML = "";
    arr.forEach(function (x) {
      if (!x.is_elf) return;
      var tr = document.createElement("tr");
      tr.innerHTML = "<td></td><td></td><td></td><td></td>";
      tr.children[0].textContent = x.name;
      tr.children[1].textContent = fmtKB(x.size);
      tr.children[2].textContent = x.loaded ? t("loaded") : "-";
      var br = mkbtn(t("run"), "btn-primary", (function (s) { return function () {
        fetch("/api/elf/run?name=" + encodeURIComponent(s), { method: "POST" })
          .then(function (r) { return r.json(); }).then(function (j) {
            if (j.ok) toast("toast_started", true);
            else toastErr(j);
            get("/api/files", renderAll);
          }).catch(function () {
            toastErr();
          });
      }; })(x.name), x.loaded);
      var bu = mkbtn(t("stop"), "btn-danger", (function (s) { return function () {
        fetch("/api/elf/stop?name=" + encodeURIComponent(s), { method: "POST" })
          .then(function () {
            get("/api/files", renderAll);
            get("/api/tasks", renderTasks);
          });
      }; })(x.name), !x.loaded);
      tr.children[3].appendChild(br);
      tr.children[3].appendChild(document.createTextNode(" "));
      tr.children[3].appendChild(bu);
      tb.appendChild(tr);
    });
  }
  function renderAll(arr) {
    renderFiles(arr);
    renderModules(arr);
  }
  document.getElementById("file-upload").onclick = function () {
    var f = document.getElementById("file-pick").files[0];
    if (!f) return;
    fetch("/api/files/upload?name=" + encodeURIComponent(f.name), {
      method: "POST",
      body: f
    }).then(function (r) { return r.json(); }).then(function (j) {
      if (j.ok) toast("toast_uploaded", true);
      else toastErr(j);
      get("/api/files", renderAll);
      get("/api/sys", renderFilesSys);
    }).catch(function () {
      toastErr();
    });
  };
  function renderStations(arr) {
    arr = Array.isArray(arr) ? arr : [];
    lastStations = arr;
    renderIfaceCards();
    if (lastNet) renderWlNets(lastNet);
    var tb = document.getElementById("stations-body");
    var apSsid = lastAp ? lastAp.ssid : "ap";
    tb.innerHTML = "";
    arr.forEach(function (x) {
      var tr = document.createElement("tr");
      tr.innerHTML = "<td></td><td></td><td></td><td></td><td></td><td></td>";
      var nb = document.createElement("span");
      nb.className = "netbox";
      nb.textContent = t("ap_netbox").replace("{s}", apSsid);
      tr.children[0].appendChild(nb);
      tr.children[1].textContent = x.mac;
      tr.children[2].textContent = (x.ip && x.ip !== "-") ? x.ip : "-";
      tr.children[3].innerHTML = sigBars(x.rssi);
      tr.children[3].appendChild(document.createTextNode(x.rssi + "/-92 dBm"));
      tr.children[4].textContent = "-";
      tr.children[5].appendChild(mkbtn(t("wl_disconnect"), "btn-danger",
        (function (m) { return function () {
          fetch("/api/ap/deauth?mac=" + m, { method: "POST" })
            .then(function (r) { return r.json(); }).then(function (jj) {
              if (jj.ok) toast("toast_disconnected", true);
              else toastErr(jj);
              get("/api/ap/stations", renderStations);
            }).catch(function () {
              toastErr();
            });
        }; })(x.mac)));
      tb.appendChild(tr);
    });
  }
  function staConnect(ssid, pass) {
    var url = "/api/sta/connect?ssid=" + encodeURIComponent(ssid);
    if (pass) url += "&pass=" + encodeURIComponent(pass);
    fetch(url, { method: "POST" }).then(function () {
      get("/api/nets", renderNets);
      get("/api/net", renderNet);
    });
  }
  function renderNets(j) {
    lastNets = j;
    knownNets = j.nets.map(function (x) { return x.ssid; });
    if (lastNet) renderWlNets(lastNet);
  }
  document.getElementById("ns-go").onclick = function () {
    var host = document.getElementById("ns-host").value;
    var out = document.getElementById("diag-out");
    out.textContent = "";
    fetch("/api/nslookup?host=" + encodeURIComponent(host), { method: "POST" })
      .then(function (r) { return r.text(); })
      .then(function (txt) { out.textContent = txt; })
      .catch(function () { out.textContent = "error"; });
  };
  /* derive_color port from OpenWrt LuCI: prng.js + sfh() from cbi.js.
   * A verbatim port, JS semantics are the same everywhere, colors match LuCI
   * for the same BSSIDs one to one (including edge cases). */
  var prngS = [0, 0, 0, 0];
  function prngMul(a, b) {
    var r = [0, 0, 0, 0, 0, 0, 0, 0], i, j, k, t;
    for (j = 0; j < 4; j++) {
      k = 0;
      for (i = 0; i < 4; i++) {
        t = a[i] * b[j] + r[i + j] + k;
        r[i + j] = t & 0xffff;
        k = t >>> 16;
      }
      r[j + 4] = k;
    }
    r.length = 4;
    return r;
  }
  function prngAdd(a, n) {
    var r = [0, 0, 0, 0], k = n, i, t;
    for (i = 0; i < 4; i++) {
      t = a[i] + k;
      r[i] = t & 0xffff;
      k = t >>> 16;
    }
    return r;
  }
  function prngShr(a, n) {
    var r = [a[0], a[1], a[2], a[3], 0], i = 4, k = 0, j, s;
    for (; n > 16; n -= 16, i--) {
      for (j = 0; j < 4; j++) r[j] = r[j + 1];
    }
    for (; i > 0; i--) {
      s = r[i - 1];
      r[i - 1] = (s >>> n) | k;
      k = ((s & ((1 << n) - 1)) << (16 - n));
    }
    r.length = 4;
    return r;
  }
  function prngSeed(n) {
    n = (n - 1) | 0;
    prngS[0] = n & 0xffff;
    prngS[1] = n >>> 16;
    prngS[2] = 0;
    prngS[3] = 0;
  }
  function prngInt() {
    prngS = prngMul(prngS, [0x7f2d, 0x4c95, 0xf42d, 0x5851]);
    prngS = prngAdd(prngS, 1);
    var r = prngShr(prngS, 33);
    return (r[1] << 16) | r[0];
  }
  function prngGetU(u) {
    var r = (prngInt() % 0x7fffffff) / 0x7fffffff;
    return Math.floor(r * u) + 1;
  }
  function prngGetF() {
    return (prngInt() % 0x7fffffff) / 0x7fffffff;
  }
  function prngS8(bytes, off) {
    var n = bytes[off];
    return n > 0x7f ? (n - 256) >>> 0 : n;
  }
  function prngU16(bytes, off) {
    return ((bytes[off + 1] << 8) + bytes[off]) >>> 0;
  }
  function prngSfh(s) {
    var bytes = [], i, ch, next;
    for (i = 0; i < s.length; i++) {
      ch = s.charCodeAt(i);
      if (ch >= 0xd800 && ch <= 0xdbff && i + 1 < s.length) {
        next = s.charCodeAt(i + 1);
        if (next >= 0xdc00 && next <= 0xdfff) {
          ch = 0x10000 + ((ch - 0xd800) << 10) + (next - 0xdc00);
          i++;
        }
      }
      if (ch <= 0x7f) {
        bytes.push(ch);
      } else if (ch <= 0x7ff) {
        bytes.push(((ch >>> 6) & 0x1f) | 0xc0, (ch & 0x3f) | 0x80);
      } else if (ch <= 0xffff) {
        bytes.push(((ch >>> 12) & 0xf) | 0xe0,
          ((ch >>> 6) & 0x3f) | 0x80, (ch & 0x3f) | 0x80);
      } else if (ch <= 0x10ffff) {
        bytes.push(((ch >>> 18) & 0x7) | 0xf0,
          ((ch >>> 12) & 0x3f) | 0x80, ((ch >> 6) & 0x3f) | 0x80,
          (ch & 0x3f) | 0x80);
      }
    }
    if (!bytes.length) return null;
    var hash = bytes.length >>> 0, len = bytes.length >>> 2, off = 0, tmp;
    while (len--) {
      hash += prngU16(bytes, off);
      tmp = ((prngU16(bytes, off + 2) << 11) ^ hash) >>> 0;
      hash = ((hash << 16) ^ tmp) >>> 0;
      hash += hash >>> 11;
      off += 4;
    }
    switch ((bytes.length & 3) >>> 0) {
    case 3:
      hash += prngU16(bytes, off);
      hash = (hash ^ (hash << 16)) >>> 0;
      hash = (hash ^ (prngS8(bytes, off + 2) << 18)) >>> 0;
      hash += hash >>> 11;
      break;
    case 2:
      hash += prngU16(bytes, off);
      hash = (hash ^ (hash << 11)) >>> 0;
      hash += hash >>> 17;
      break;
    case 1:
      hash += prngS8(bytes, off);
      hash = (hash ^ (hash << 10)) >>> 0;
      hash += hash >>> 1;
      break;
    }
    hash = (hash ^ (hash << 3)) >>> 0;
    hash += hash >>> 5;
    hash = (hash ^ (hash << 4)) >>> 0;
    hash += hash >>> 17;
    hash = (hash ^ (hash << 25)) >>> 0;
    hash += hash >>> 6;
    return (0x100000000 + hash).toString(16).slice(1);
  }
  function prngHex2(v) {
    var h = v.toString(16);
    return h.length < 2 ? "0" + h : h;
  }
  function deriveColor(str) {
    prngSeed(parseInt(prngSfh(str), 16));
    var r = prngGetU(128), g = prngGetU(128), min = 0, max = 128, b;
    if (r + g < 128) min = 128 - r - g;
    else max = 255 - r - g;
    b = min + Math.floor(prngGetF() * (max - min));
    return "#" + prngHex2(0xff - r) + prngHex2(0xff - g) + prngHex2(0xff - b);
  }
  function chanColor(x) {
    /* iwinfo in OpenWrt reports BSSIDs in uppercase, hash the same way */
    return deriveColor((x.bssid || "").toUpperCase());
  }
  function sigBars(rssi) {
    var n = rssi > -70 ? 4 : rssi > -80 ? 3 : rssi > -90 ? 2 : 1;
    var s = '<span class="sigbars">';
    var hs = [4, 7, 10, 13], i;
    for (i = 0; i < 4; i++) {
      s += '<i class="' + (i < n ? "f" : "") + '" style="height:' +
        hs[i] + 'px"></i>';
    }
    return s + "</span>";
  }
  function chanRows() {
    var rows = [];
    if (lastNet && lastNet.ssid) {
      var mac = lastNet.bssid || "?";
      (lastNet.ifaces || []).forEach(function (x) {
        if (x.name === "sta") mac = x.mac;
      });
      /* as in OpenWrt: own interface is drawn separately with a -10 stub */
      rows.push({ ssid: lastNet.ssid, bssid: mac,
        channel: lastNet.channel, rssi: -10, width: 20,
        local: true });
    }
    return rows;
  }
  function drawSpec(arr) {
    /* 17x5 canvas: 2 empty columns + channels 1-14 + 1 empty;
     * grid across the whole canvas, no empty strip at the graph bottom */
    var CW = 46, L = 2 * CW, T = 30, B = 300, c;
    /* as in OpenWrt: channels strictly on the vertical grid lines */
    function X(ch) { return (ch + 1) * CW; }
    function Y(r) {
      var y = T + (B - T) * (0 - r) / 100;
      return Math.max(28, Math.min(B, y));
    }
    var step = CW;
    var s = '<svg viewBox="0 0 782 300" preserveAspectRatio="none" class="graph">';
    var k;
    for (k = 0; k <= 17; k++) {
      s += '<line x1="' + (k * CW) + '" y1="0" x2="' + (k * CW) +
        '" y2="' + B + '" class="grid"/>';
    }
    s += '<line x1="0" y1="0" x2="782" y2="0" class="grid"/>';
    [-25, -50, -75, -90].forEach(function (db) {
      s += '<line x1="0" y1="' + Y(db).toFixed(1) + '" x2="782" y2="' +
        Y(db).toFixed(1) + '" class="grid"/>';
      s += '<text x="4" y="' + (Y(db) - 5).toFixed(1) + '">' + db + " dbm</text>";
    });
    s += '<line x1="0" y1="' + B + '" x2="782" y2="' + B + '" class="grid"/>';
    for (c = 1; c <= 14; c++) {
      s += '<text x="' + (X(c) + 5).toFixed(1) + '" y="20">' + c + "</text>";
    }
    arr.forEach(function (x) {
      var col = chanColor(x);
      /* as in OpenWrt: cw = width/10 (2 for 20 MHz, 4 for 40 MHz),
       * base +-cw steps, plateau +-(cw-1) steps.
       * The 40 MHz center shifts by +-2 channels per second (above/below) */
      var cw = x.width === 40 ? 4 : 2;
      var bw = cw * step;
      var pw = (cw - 1) * step;
      var ctr = x.channel;
      if (x.width === 40) {
        if (x.second === 1) ctr += 2;
        else if (x.second === 2) ctr -= 2;
      }
      var xc = X(Math.max(1, Math.min(14, ctr))), y = Y(x.rssi);
      var name = x.local ? t("ch_local") : (x.ssid || "?");
      s += '<polygon points="' + (xc - bw).toFixed(1) + "," + B + " " +
        (xc - pw).toFixed(1) + "," + y.toFixed(1) + " " +
        (xc + pw).toFixed(1) + "," + y.toFixed(1) + " " +
        (xc + bw).toFixed(1) + "," + B + '" style="fill:' + col +
        ';fill-opacity:0.3;stroke:' + col + '"/>';
      s += '<text x="' + xc.toFixed(1) + '" y="' + (y - 6).toFixed(1) +
        '" text-anchor="middle" class="spec-lab" style="fill:' + col + '">' +
        name + "</text>";
    });
    s += "</svg>";
    document.getElementById("chan-spec").innerHTML = s;
  }
  function renderChanTable(arr) {
    var tb = document.getElementById("chan-body");
    tb.innerHTML = "";
    arr.forEach(function (x) {
      var col = chanColor(x);
      var tr = document.createElement("tr");
      tr.innerHTML = "<td></td><td></td><td></td><td></td><td></td><td></td>";
      var td = tr.children;
      td[0].innerHTML = sigBars(x.rssi);
      td[0].appendChild(document.createTextNode(x.rssi + " dBm"));
      var dot = document.createElement("span");
      dot.className = "ssid-dot";
      dot.style.background = col;
      td[1].appendChild(dot);
      td[1].appendChild(document.createTextNode(
        x.local ? t("ch_local") : (x.ssid || "?")));
      td[2].textContent = x.channel;
      td[3].textContent = (x.width || 20) + " MHz";
      td[4].textContent = t("mode_master");
      td[5].textContent = x.bssid;
      tb.appendChild(tr);
    });
  }
  function chanRefresh() {
    document.getElementById("chan-body").innerHTML = "";
    function go() {
      get("/api/scan", function (arr) {
        /* scan as is, the local interface as a separate row on top */
        lastScan = Array.isArray(arr) ? arr : [];
        if (lastNet) renderWlNets(lastNet);
        var rows = chanRows().concat(arr);
        drawSpec(rows);
        renderChanTable(rows);
      });
    }
    if (!lastNet) {
      get("/api/net", function (j) { renderNet(j); go(); });
    } else {
      go();
    }
  }
  document.getElementById("chan-refresh").onclick = chanRefresh;
  function diagRun(url) {
    var out = document.getElementById("diag-out");
    out.textContent = "";
    fetch(url, { method: "POST" })
      .then(function (r) {
        if (r.body && r.body.getReader) {
          var reader = r.body.getReader();
          var dec = new TextDecoder();
          var read = function () {
            return reader.read().then(function (x) {
              if (x.done) {
                out.textContent += dec.decode();
                return;
              }
              out.textContent += dec.decode(x.value, { stream: true });
              return read();
            });
          };
          return read();
        }
        return r.text().then(function (txt) { out.textContent = txt; });
      });
  }
  document.getElementById("ping-go").onclick = function () {
    var host = document.getElementById("ping-host").value;
    var count = parseInt(document.getElementById("ping-count").value, 10) || 4;
    var ivl = parseInt(document.getElementById("ping-interval").value, 10) || 1000;
    diagRun("/api/ping?host=" + encodeURIComponent(host) +
      "&count=" + count + "&interval=" + ivl);
  };
  document.getElementById("trace-go").onclick = function () {
    var host = document.getElementById("trace-host").value;
    diagRun("/api/traceroute?host=" + encodeURIComponent(host));
  };
  function openApModal() {
    var cur = lastAp;
    if (!cur) return;
    var wrap = document.createElement("div");
    wrap.className = "frm";
    function apRow(label, node) {
      var r = document.createElement("div");
      r.className = "frm-row";
      var lab = document.createElement("span");
      lab.className = "flab";
      lab.textContent = label;
      var val = document.createElement("div");
      val.className = "fval";
      var p = document.createElement("p");
      p.appendChild(node);
      val.appendChild(p);
      r.appendChild(lab);
      r.appendChild(val);
      wrap.appendChild(r);
      return p;
    }
    var inSsid = document.createElement("input");
    inSsid.type = "text"; inSsid.size = 20; inSsid.maxLength = 32;
    inSsid.value = cur.ssid || "";
    apRow("SSID", inSsid);
    var inAuth = document.createElement("select");
    ["open", "wpa2", "wpa_wpa2"].forEach(function (a) {
      var o = document.createElement("option");
      o.value = a; o.textContent = a;
      if (cur.auth === a) o.selected = true;
      inAuth.appendChild(o);
    });
    apRow("Auth", inAuth);
    var inPass = document.createElement("input");
    inPass.type = "text"; inPass.size = 20; inPass.maxLength = 63;
    inPass.value = cur.pass || "";
    apRow(t("password"), inPass);
    var inMode = document.createElement("select");
    [["auto", t("ap_auto")], ["always", t("ap_always")]].forEach(function (m) {
      var o = document.createElement("option");
      o.value = m[0]; o.textContent = m[1];
      if ((cur.mode || "auto") === m[0]) o.selected = true;
      inMode.appendChild(o);
    });
    apRow(t("ap_mode"), inMode);
    var inNat = document.createElement("input");
    inNat.type = "checkbox"; inNat.checked = !!cur.nat;
    var labNat = document.createElement("label");
    labNat.appendChild(inNat);
    labNat.appendChild(document.createTextNode(" " + t("ap_nat")));
    apRow("", labNat);
    openModal(t("ap_title"), wrap, [
      mkbtn(t("save"), "btn-primary", function () {
        fetch("/api/ap", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({
            ssid: inSsid.value,
            auth: inAuth.value,
            pass: inPass.value,
            mode: inMode.value,
            nat: inNat.checked ? 1 : 0
          })
        }).then(function (r) {
          return r.json().then(function (jj) { return { http: r.ok, body: jj }; });
        }).then(function (o) {
          if (o.http && o.body.ok) {
            closeModal();
            toast("toast_saved", true);
            get("/api/ap", function (jj) {
              lastAp = jj;
              if (lastNet) renderWlNets(lastNet);
            });
          } else {
            toastErr(o.body);
          }
        }).catch(function () {
          toastErr();
        });
      }),
      mkbtn(t("dismiss"), "", closeModal)
    ]);
  }
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
  var TZS = [
    ["UTC","UTC0"],
    ["Africa/Abidjan","GMT0"],
    ["Africa/Accra","GMT0"],
    ["Africa/Addis_Ababa","EAT-3"],
    ["Africa/Algiers","CET-1"],
    ["Africa/Asmara","EAT-3"],
    ["Africa/Bamako","GMT0"],
    ["Africa/Bangui","WAT-1"],
    ["Africa/Banjul","GMT0"],
    ["Africa/Bissau","GMT0"],
    ["Africa/Blantyre","CAT-2"],
    ["Africa/Brazzaville","WAT-1"],
    ["Africa/Bujumbura","CAT-2"],
    ["Africa/Cairo","EET-2EEST,M4.5.5/0,M10.5.4/24"],
    ["Africa/Casablanca","<+00>0"],
    ["Africa/Ceuta","CET-1CEST,M3.5.0,M10.5.0/3"],
    ["Africa/Conakry","GMT0"],
    ["Africa/Dakar","GMT0"],
    ["Africa/Dar_es_Salaam","EAT-3"],
    ["Africa/Djibouti","EAT-3"],
    ["Africa/Douala","WAT-1"],
    ["Africa/El_Aaiun","<+00>0"],
    ["Africa/Freetown","GMT0"],
    ["Africa/Gaborone","CAT-2"],
    ["Africa/Harare","CAT-2"],
    ["Africa/Johannesburg","SAST-2"],
    ["Africa/Juba","CAT-2"],
    ["Africa/Kampala","EAT-3"],
    ["Africa/Khartoum","CAT-2"],
    ["Africa/Kigali","CAT-2"],
    ["Africa/Kinshasa","WAT-1"],
    ["Africa/Lagos","WAT-1"],
    ["Africa/Libreville","WAT-1"],
    ["Africa/Lome","GMT0"],
    ["Africa/Luanda","WAT-1"],
    ["Africa/Lubumbashi","CAT-2"],
    ["Africa/Lusaka","CAT-2"],
    ["Africa/Malabo","WAT-1"],
    ["Africa/Maputo","CAT-2"],
    ["Africa/Maseru","SAST-2"],
    ["Africa/Mbabane","SAST-2"],
    ["Africa/Mogadishu","EAT-3"],
    ["Africa/Monrovia","GMT0"],
    ["Africa/Nairobi","EAT-3"],
    ["Africa/Ndjamena","WAT-1"],
    ["Africa/Niamey","WAT-1"],
    ["Africa/Nouakchott","GMT0"],
    ["Africa/Ouagadougou","GMT0"],
    ["Africa/Porto-Novo","WAT-1"],
    ["Africa/Sao_Tome","GMT0"],
    ["Africa/Tripoli","EET-2"],
    ["Africa/Tunis","CET-1"],
    ["Africa/Windhoek","CAT-2"],
    ["America/Adak","HST10HDT,M3.2.0,M11.1.0"],
    ["America/Anchorage","AKST9AKDT,M3.2.0,M11.1.0"],
    ["America/Anguilla","AST4"],
    ["America/Antigua","AST4"],
    ["America/Araguaina","<-03>3"],
    ["America/Argentina/Buenos_Aires","<-03>3"],
    ["America/Argentina/Catamarca","<-03>3"],
    ["America/Argentina/Cordoba","<-03>3"],
    ["America/Argentina/Jujuy","<-03>3"],
    ["America/Argentina/La_Rioja","<-03>3"],
    ["America/Argentina/Mendoza","<-03>3"],
    ["America/Argentina/Rio_Gallegos","<-03>3"],
    ["America/Argentina/Salta","<-03>3"],
    ["America/Argentina/San_Juan","<-03>3"],
    ["America/Argentina/San_Luis","<-03>3"],
    ["America/Argentina/Tucuman","<-03>3"],
    ["America/Argentina/Ushuaia","<-03>3"],
    ["America/Aruba","AST4"],
    ["America/Asuncion","<-03>3"],
    ["America/Atikokan","EST5"],
    ["America/Bahia","<-03>3"],
    ["America/Bahia_Banderas","CST6"],
    ["America/Barbados","AST4"],
    ["America/Belem","<-03>3"],
    ["America/Belize","CST6"],
    ["America/Blanc-Sablon","AST4"],
    ["America/Boa_Vista","<-04>4"],
    ["America/Bogota","<-05>5"],
    ["America/Boise","MST7MDT,M3.2.0,M11.1.0"],
    ["America/Cambridge_Bay","MST7MDT,M3.2.0,M11.1.0"],
    ["America/Campo_Grande","<-04>4"],
    ["America/Cancun","EST5"],
    ["America/Caracas","<-04>4"],
    ["America/Cayenne","<-03>3"],
    ["America/Cayman","EST5"],
    ["America/Chicago","CST6CDT,M3.2.0,M11.1.0"],
    ["America/Chihuahua","CST6"],
    ["America/Ciudad_Juarez","MST7MDT,M3.2.0,M11.1.0"],
    ["America/Costa_Rica","CST6"],
    ["America/Coyhaique","<-03>3"],
    ["America/Creston","MST7"],
    ["America/Cuiaba","<-04>4"],
    ["America/Curacao","AST4"],
    ["America/Danmarkshavn","GMT0"],
    ["America/Dawson","MST7"],
    ["America/Dawson_Creek","MST7"],
    ["America/Denver","MST7MDT,M3.2.0,M11.1.0"],
    ["America/Detroit","EST5EDT,M3.2.0,M11.1.0"],
    ["America/Dominica","AST4"],
    ["America/Edmonton","CST6"],
    ["America/Eirunepe","<-05>5"],
    ["America/El_Salvador","CST6"],
    ["America/Fort_Nelson","MST7"],
    ["America/Fortaleza","<-03>3"],
    ["America/Glace_Bay","AST4ADT,M3.2.0,M11.1.0"],
    ["America/Goose_Bay","AST4ADT,M3.2.0,M11.1.0"],
    ["America/Grand_Turk","EST5EDT,M3.2.0,M11.1.0"],
    ["America/Grenada","AST4"],
    ["America/Guadeloupe","AST4"],
    ["America/Guatemala","CST6"],
    ["America/Guayaquil","<-05>5"],
    ["America/Guyana","<-04>4"],
    ["America/Halifax","AST4ADT,M3.2.0,M11.1.0"],
    ["America/Havana","CST5CDT,M3.2.0/0,M11.1.0/1"],
    ["America/Hermosillo","MST7"],
    ["America/Indiana/Indianapolis","EST5EDT,M3.2.0,M11.1.0"],
    ["America/Indiana/Knox","CST6CDT,M3.2.0,M11.1.0"],
    ["America/Indiana/Marengo","EST5EDT,M3.2.0,M11.1.0"],
    ["America/Indiana/Petersburg","EST5EDT,M3.2.0,M11.1.0"],
    ["America/Indiana/Tell_City","CST6CDT,M3.2.0,M11.1.0"],
    ["America/Indiana/Vevay","EST5EDT,M3.2.0,M11.1.0"],
    ["America/Indiana/Vincennes","EST5EDT,M3.2.0,M11.1.0"],
    ["America/Indiana/Winamac","EST5EDT,M3.2.0,M11.1.0"],
    ["America/Inuvik","CST6"],
    ["America/Iqaluit","EST5EDT,M3.2.0,M11.1.0"],
    ["America/Jamaica","EST5"],
    ["America/Juneau","AKST9AKDT,M3.2.0,M11.1.0"],
    ["America/Kentucky/Louisville","EST5EDT,M3.2.0,M11.1.0"],
    ["America/Kentucky/Monticello","EST5EDT,M3.2.0,M11.1.0"],
    ["America/Kralendijk","AST4"],
    ["America/La_Paz","<-04>4"],
    ["America/Lima","<-05>5"],
    ["America/Los_Angeles","PST8PDT,M3.2.0,M11.1.0"],
    ["America/Lower_Princes","AST4"],
    ["America/Maceio","<-03>3"],
    ["America/Managua","CST6"],
    ["America/Manaus","<-04>4"],
    ["America/Marigot","AST4"],
    ["America/Martinique","AST4"],
    ["America/Matamoros","CST6CDT,M3.2.0,M11.1.0"],
    ["America/Mazatlan","MST7"],
    ["America/Menominee","CST6CDT,M3.2.0,M11.1.0"],
    ["America/Merida","CST6"],
    ["America/Metlakatla","AKST9AKDT,M3.2.0,M11.1.0"],
    ["America/Mexico_City","CST6"],
    ["America/Miquelon","<-03>3<-02>,M3.2.0,M11.1.0"],
    ["America/Moncton","AST4ADT,M3.2.0,M11.1.0"],
    ["America/Monterrey","CST6"],
    ["America/Montevideo","<-03>3"],
    ["America/Montserrat","AST4"],
    ["America/Nassau","EST5EDT,M3.2.0,M11.1.0"],
    ["America/New_York","EST5EDT,M3.2.0,M11.1.0"],
    ["America/Nome","AKST9AKDT,M3.2.0,M11.1.0"],
    ["America/Noronha","<-02>2"],
    ["America/North_Dakota/Beulah","CST6CDT,M3.2.0,M11.1.0"],
    ["America/North_Dakota/Center","CST6CDT,M3.2.0,M11.1.0"],
    ["America/North_Dakota/New_Salem","CST6CDT,M3.2.0,M11.1.0"],
    ["America/Nuuk","<-02>2<-01>,M3.5.0/-1,M10.5.0/0"],
    ["America/Ojinaga","CST6CDT,M3.2.0,M11.1.0"],
    ["America/Panama","EST5"],
    ["America/Paramaribo","<-03>3"],
    ["America/Phoenix","MST7"],
    ["America/Port-au-Prince","EST5EDT,M3.2.0,M11.1.0"],
    ["America/Port_of_Spain","AST4"],
    ["America/Porto_Velho","<-04>4"],
    ["America/Puerto_Rico","AST4"],
    ["America/Punta_Arenas","<-03>3"],
    ["America/Rankin_Inlet","CST6CDT,M3.2.0,M11.1.0"],
    ["America/Recife","<-03>3"],
    ["America/Regina","CST6"],
    ["America/Resolute","CST6CDT,M3.2.0,M11.1.0"],
    ["America/Rio_Branco","<-05>5"],
    ["America/Santarem","<-03>3"],
    ["America/Santiago","<-04>4<-03>,M9.1.6/24,M4.1.6/24"],
    ["America/Santo_Domingo","AST4"],
    ["America/Sao_Paulo","<-03>3"],
    ["America/Scoresbysund","<-02>2<-01>,M3.5.0/-1,M10.5.0/0"],
    ["America/Sitka","AKST9AKDT,M3.2.0,M11.1.0"],
    ["America/St_Barthelemy","AST4"],
    ["America/St_Johns","NST3:30NDT,M3.2.0,M11.1.0"],
    ["America/St_Kitts","AST4"],
    ["America/St_Lucia","AST4"],
    ["America/St_Thomas","AST4"],
    ["America/St_Vincent","AST4"],
    ["America/Swift_Current","CST6"],
    ["America/Tegucigalpa","CST6"],
    ["America/Thule","AST4ADT,M3.2.0,M11.1.0"],
    ["America/Tijuana","PST8PDT,M3.2.0,M11.1.0"],
    ["America/Toronto","EST5EDT,M3.2.0,M11.1.0"],
    ["America/Tortola","AST4"],
    ["America/Vancouver","MST7"],
    ["America/Whitehorse","MST7"],
    ["America/Winnipeg","EST5"],
    ["America/Yakutat","AKST9AKDT,M3.2.0,M11.1.0"],
    ["Antarctica/Casey","<+08>-8"],
    ["Antarctica/Davis","<+07>-7"],
    ["Antarctica/DumontDUrville","<+10>-10"],
    ["Antarctica/Macquarie","AEST-10AEDT,M10.1.0,M4.1.0/3"],
    ["Antarctica/Mawson","<+05>-5"],
    ["Antarctica/McMurdo","NZST-12NZDT,M9.5.0,M4.1.0/3"],
    ["Antarctica/Palmer","<-03>3"],
    ["Antarctica/Rothera","<-03>3"],
    ["Antarctica/Syowa","<+03>-3"],
    ["Antarctica/Troll","<+00>0<+02>-2,M3.5.0/1,M10.5.0/3"],
    ["Antarctica/Vostok","<+05>-5"],
    ["Arctic/Longyearbyen","CET-1CEST,M3.5.0,M10.5.0/3"],
    ["Asia/Aden","<+03>-3"],
    ["Asia/Almaty","<+05>-5"],
    ["Asia/Amman","<+03>-3"],
    ["Asia/Anadyr","<+12>-12"],
    ["Asia/Aqtau","<+05>-5"],
    ["Asia/Aqtobe","<+05>-5"],
    ["Asia/Ashgabat","<+05>-5"],
    ["Asia/Atyrau","<+05>-5"],
    ["Asia/Baghdad","<+03>-3"],
    ["Asia/Bahrain","<+03>-3"],
    ["Asia/Baku","<+04>-4"],
    ["Asia/Bangkok","<+07>-7"],
    ["Asia/Barnaul","<+07>-7"],
    ["Asia/Beirut","EET-2EEST,M3.5.0/0,M10.5.0/0"],
    ["Asia/Bishkek","<+06>-6"],
    ["Asia/Brunei","<+08>-8"],
    ["Asia/Chita","<+09>-9"],
    ["Asia/Colombo","<+0530>-5:30"],
    ["Asia/Damascus","<+03>-3"],
    ["Asia/Dhaka","<+06>-6"],
    ["Asia/Dili","<+09>-9"],
    ["Asia/Dubai","<+04>-4"],
    ["Asia/Dushanbe","<+05>-5"],
    ["Asia/Famagusta","EET-2EEST,M3.5.0/3,M10.5.0/4"],
    ["Asia/Gaza","EET-2EEST,M3.4.4/50,M10.4.4/50"],
    ["Asia/Hebron","EET-2EEST,M3.4.4/50,M10.4.4/50"],
    ["Asia/Ho_Chi_Minh","<+07>-7"],
    ["Asia/Hong_Kong","HKT-8"],
    ["Asia/Hovd","<+07>-7"],
    ["Asia/Irkutsk","<+08>-8"],
    ["Asia/Jakarta","WIB-7"],
    ["Asia/Jayapura","WIT-9"],
    ["Asia/Jerusalem","IST-2IDT,M3.4.4/26,M10.5.0"],
    ["Asia/Kabul","<+0430>-4:30"],
    ["Asia/Kamchatka","<+12>-12"],
    ["Asia/Karachi","PKT-5"],
    ["Asia/Kathmandu","<+0545>-5:45"],
    ["Asia/Khandyga","<+09>-9"],
    ["Asia/Kolkata","IST-5:30"],
    ["Asia/Krasnoyarsk","<+07>-7"],
    ["Asia/Kuala_Lumpur","<+08>-8"],
    ["Asia/Kuching","<+08>-8"],
    ["Asia/Kuwait","<+03>-3"],
    ["Asia/Macau","CST-8"],
    ["Asia/Magadan","<+11>-11"],
    ["Asia/Makassar","WITA-8"],
    ["Asia/Manila","PST-8"],
    ["Asia/Muscat","<+04>-4"],
    ["Asia/Nicosia","EET-2EEST,M3.5.0/3,M10.5.0/4"],
    ["Asia/Novokuznetsk","<+07>-7"],
    ["Asia/Novosibirsk","<+07>-7"],
    ["Asia/Omsk","<+06>-6"],
    ["Asia/Oral","<+05>-5"],
    ["Asia/Phnom_Penh","<+07>-7"],
    ["Asia/Pontianak","WIB-7"],
    ["Asia/Pyongyang","KST-9"],
    ["Asia/Qatar","<+03>-3"],
    ["Asia/Qostanay","<+05>-5"],
    ["Asia/Qyzylorda","<+05>-5"],
    ["Asia/Riyadh","<+03>-3"],
    ["Asia/Sakhalin","<+11>-11"],
    ["Asia/Samarkand","<+05>-5"],
    ["Asia/Seoul","KST-9"],
    ["Asia/Shanghai","CST-8"],
    ["Asia/Singapore","<+08>-8"],
    ["Asia/Srednekolymsk","<+11>-11"],
    ["Asia/Taipei","CST-8"],
    ["Asia/Tashkent","<+05>-5"],
    ["Asia/Tbilisi","<+04>-4"],
    ["Asia/Tehran","<+0330>-3:30"],
    ["Asia/Thimphu","<+06>-6"],
    ["Asia/Tokyo","JST-9"],
    ["Asia/Tomsk","<+07>-7"],
    ["Asia/Ulaanbaatar","<+08>-8"],
    ["Asia/Urumqi","<+06>-6"],
    ["Asia/Ust-Nera","<+10>-10"],
    ["Asia/Vientiane","<+07>-7"],
    ["Asia/Vladivostok","<+10>-10"],
    ["Asia/Yakutsk","<+09>-9"],
    ["Asia/Yangon","<+0630>-6:30"],
    ["Asia/Yekaterinburg","<+05>-5"],
    ["Asia/Yerevan","<+04>-4"],
    ["Atlantic/Azores","<-01>1<+00>,M3.5.0/0,M10.5.0/1"],
    ["Atlantic/Bermuda","AST4ADT,M3.2.0,M11.1.0"],
    ["Atlantic/Canary","WET0WEST,M3.5.0/1,M10.5.0"],
    ["Atlantic/Cape_Verde","<-01>1"],
    ["Atlantic/Faroe","WET0WEST,M3.5.0/1,M10.5.0"],
    ["Atlantic/Madeira","WET0WEST,M3.5.0/1,M10.5.0"],
    ["Atlantic/Reykjavik","GMT0"],
    ["Atlantic/South_Georgia","<-02>2"],
    ["Atlantic/St_Helena","GMT0"],
    ["Atlantic/Stanley","<-03>3"],
    ["Australia/Adelaide","ACST-9:30ACDT,M10.1.0,M4.1.0/3"],
    ["Australia/Brisbane","AEST-10"],
    ["Australia/Broken_Hill","ACST-9:30ACDT,M10.1.0,M4.1.0/3"],
    ["Australia/Darwin","ACST-9:30"],
    ["Australia/Eucla","<+0845>-8:45"],
    ["Australia/Hobart","AEST-10AEDT,M10.1.0,M4.1.0/3"],
    ["Australia/Lindeman","AEST-10"],
    ["Australia/Lord_Howe","<+1030>-10:30<+11>-11,M10.1.0,M4.1.0"],
    ["Australia/Melbourne","AEST-10AEDT,M10.1.0,M4.1.0/3"],
    ["Australia/Perth","AWST-8"],
    ["Australia/Sydney","AEST-10AEDT,M10.1.0,M4.1.0/3"],
    ["Etc/GMT","GMT0"],
    ["Etc/GMT+1","<-01>1"],
    ["Etc/GMT+10","<-10>10"],
    ["Etc/GMT+11","<-11>11"],
    ["Etc/GMT+12","<-12>12"],
    ["Etc/GMT+2","<-02>2"],
    ["Etc/GMT+3","<-03>3"],
    ["Etc/GMT+4","<-04>4"],
    ["Etc/GMT+5","<-05>5"],
    ["Etc/GMT+6","<-06>6"],
    ["Etc/GMT+7","<-07>7"],
    ["Etc/GMT+8","<-08>8"],
    ["Etc/GMT+9","<-09>9"],
    ["Etc/GMT-1","<+01>-1"],
    ["Etc/GMT-10","<+10>-10"],
    ["Etc/GMT-11","<+11>-11"],
    ["Etc/GMT-12","<+12>-12"],
    ["Etc/GMT-13","<+13>-13"],
    ["Etc/GMT-14","<+14>-14"],
    ["Etc/GMT-2","<+02>-2"],
    ["Etc/GMT-3","<+03>-3"],
    ["Etc/GMT-4","<+04>-4"],
    ["Etc/GMT-5","<+05>-5"],
    ["Etc/GMT-6","<+06>-6"],
    ["Etc/GMT-7","<+07>-7"],
    ["Etc/GMT-8","<+08>-8"],
    ["Etc/GMT-9","<+09>-9"],
    ["Europe/Amsterdam","CET-1CEST,M3.5.0,M10.5.0/3"],
    ["Europe/Andorra","CET-1CEST,M3.5.0,M10.5.0/3"],
    ["Europe/Astrakhan","<+04>-4"],
    ["Europe/Athens","EET-2EEST,M3.5.0/3,M10.5.0/4"],
    ["Europe/Belgrade","CET-1CEST,M3.5.0,M10.5.0/3"],
    ["Europe/Berlin","CET-1CEST,M3.5.0,M10.5.0/3"],
    ["Europe/Bratislava","CET-1CEST,M3.5.0,M10.5.0/3"],
    ["Europe/Brussels","CET-1CEST,M3.5.0,M10.5.0/3"],
    ["Europe/Bucharest","EET-2EEST,M3.5.0/3,M10.5.0/4"],
    ["Europe/Budapest","CET-1CEST,M3.5.0,M10.5.0/3"],
    ["Europe/Busingen","CET-1CEST,M3.5.0,M10.5.0/3"],
    ["Europe/Chisinau","EET-2EEST,M3.5.0/3,M10.5.0/4"],
    ["Europe/Copenhagen","CET-1CEST,M3.5.0,M10.5.0/3"],
    ["Europe/Dublin","IST-1GMT0,M10.5.0,M3.5.0/1"],
    ["Europe/Gibraltar","CET-1CEST,M3.5.0,M10.5.0/3"],
    ["Europe/Guernsey","GMT0BST,M3.5.0/1,M10.5.0"],
    ["Europe/Helsinki","EET-2EEST,M3.5.0/3,M10.5.0/4"],
    ["Europe/Isle_of_Man","GMT0BST,M3.5.0/1,M10.5.0"],
    ["Europe/Istanbul","<+03>-3"],
    ["Europe/Jersey","GMT0BST,M3.5.0/1,M10.5.0"],
    ["Europe/Kaliningrad","EET-2"],
    ["Europe/Kirov","MSK-3"],
    ["Europe/Kyiv","EET-2EEST,M3.5.0/3,M10.5.0/4"],
    ["Europe/Lisbon","WET0WEST,M3.5.0/1,M10.5.0"],
    ["Europe/Ljubljana","CET-1CEST,M3.5.0,M10.5.0/3"],
    ["Europe/London","GMT0BST,M3.5.0/1,M10.5.0"],
    ["Europe/Luxembourg","CET-1CEST,M3.5.0,M10.5.0/3"],
    ["Europe/Madrid","CET-1CEST,M3.5.0,M10.5.0/3"],
    ["Europe/Malta","CET-1CEST,M3.5.0,M10.5.0/3"],
    ["Europe/Mariehamn","EET-2EEST,M3.5.0/3,M10.5.0/4"],
    ["Europe/Minsk","<+03>-3"],
    ["Europe/Monaco","CET-1CEST,M3.5.0,M10.5.0/3"],
    ["Europe/Moscow","MSK-3"],
    ["Europe/Oslo","CET-1CEST,M3.5.0,M10.5.0/3"],
    ["Europe/Paris","CET-1CEST,M3.5.0,M10.5.0/3"],
    ["Europe/Podgorica","CET-1CEST,M3.5.0,M10.5.0/3"],
    ["Europe/Prague","CET-1CEST,M3.5.0,M10.5.0/3"],
    ["Europe/Riga","EET-2EEST,M3.5.0/3,M10.5.0/4"],
    ["Europe/Rome","CET-1CEST,M3.5.0,M10.5.0/3"],
    ["Europe/Samara","<+04>-4"],
    ["Europe/San_Marino","CET-1CEST,M3.5.0,M10.5.0/3"],
    ["Europe/Sarajevo","CET-1CEST,M3.5.0,M10.5.0/3"],
    ["Europe/Saratov","<+04>-4"],
    ["Europe/Simferopol","MSK-3"],
    ["Europe/Skopje","CET-1CEST,M3.5.0,M10.5.0/3"],
    ["Europe/Sofia","EET-2EEST,M3.5.0/3,M10.5.0/4"],
    ["Europe/Stockholm","CET-1CEST,M3.5.0,M10.5.0/3"],
    ["Europe/Tallinn","EET-2EEST,M3.5.0/3,M10.5.0/4"],
    ["Europe/Tirane","CET-1CEST,M3.5.0,M10.5.0/3"],
    ["Europe/Ulyanovsk","<+04>-4"],
    ["Europe/Vaduz","CET-1CEST,M3.5.0,M10.5.0/3"],
    ["Europe/Vatican","CET-1CEST,M3.5.0,M10.5.0/3"],
    ["Europe/Vienna","CET-1CEST,M3.5.0,M10.5.0/3"],
    ["Europe/Vilnius","EET-2EEST,M3.5.0/3,M10.5.0/4"],
    ["Europe/Volgograd","MSK-3"],
    ["Europe/Warsaw","CET-1CEST,M3.5.0,M10.5.0/3"],
    ["Europe/Zagreb","CET-1CEST,M3.5.0,M10.5.0/3"],
    ["Europe/Zurich","CET-1CEST,M3.5.0,M10.5.0/3"],
    ["Indian/Antananarivo","EAT-3"],
    ["Indian/Chagos","<+06>-6"],
    ["Indian/Christmas","<+07>-7"],
    ["Indian/Cocos","<+0630>-6:30"],
    ["Indian/Comoro","EAT-3"],
    ["Indian/Kerguelen","<+05>-5"],
    ["Indian/Mahe","<+04>-4"],
    ["Indian/Maldives","<+05>-5"],
    ["Indian/Mauritius","<+04>-4"],
    ["Indian/Mayotte","EAT-3"],
    ["Indian/Reunion","<+04>-4"],
    ["Pacific/Apia","<+13>-13"],
    ["Pacific/Auckland","NZST-12NZDT,M9.5.0,M4.1.0/3"],
    ["Pacific/Bougainville","<+11>-11"],
    ["Pacific/Chatham","<+1245>-12:45<+1345>,M9.5.0/2:45,M4.1.0/3:45"],
    ["Pacific/Chuuk","<+10>-10"],
    ["Pacific/Easter","<-06>6<-05>,M9.1.6/22,M4.1.6/22"],
    ["Pacific/Efate","<+11>-11"],
    ["Pacific/Fakaofo","<+13>-13"],
    ["Pacific/Fiji","<+12>-12"],
    ["Pacific/Funafuti","<+12>-12"],
    ["Pacific/Galapagos","<-06>6"],
    ["Pacific/Gambier","<-09>9"],
    ["Pacific/Guadalcanal","<+11>-11"],
    ["Pacific/Guam","ChST-10"],
    ["Pacific/Honolulu","HST10"],
    ["Pacific/Kanton","<+13>-13"],
    ["Pacific/Kiritimati","<+14>-14"],
    ["Pacific/Kosrae","<+11>-11"],
    ["Pacific/Kwajalein","<+12>-12"],
    ["Pacific/Majuro","<+12>-12"],
    ["Pacific/Marquesas","<-0930>9:30"],
    ["Pacific/Midway","SST11"],
    ["Pacific/Nauru","<+12>-12"],
    ["Pacific/Niue","<-11>11"],
    ["Pacific/Norfolk","<+11>-11<+12>,M10.1.0,M4.1.0/3"],
    ["Pacific/Noumea","<+11>-11"],
    ["Pacific/Pago_Pago","SST11"],
    ["Pacific/Palau","<+09>-9"],
    ["Pacific/Pitcairn","<-08>8"],
    ["Pacific/Pohnpei","<+11>-11"],
    ["Pacific/Port_Moresby","<+10>-10"],
    ["Pacific/Rarotonga","<-10>10"],
    ["Pacific/Saipan","ChST-10"],
    ["Pacific/Tahiti","<-10>10"],
    ["Pacific/Tarawa","<+12>-12"],
    ["Pacific/Tongatapu","<+13>-13"],
    ["Pacific/Wake","<+12>-12"],
    ["Pacific/Wallis","<+12>-12"],
  ];
  function loadSys() {
    get("/api/hostname", function (j) {
      document.getElementById("host-input").value = j.hostname;
    });
    get("/api/ntp", renderNtp);
    get("/api/tz", function (j) {
      var sel = document.getElementById("tz-select");
      var i, found = false;
      for (i = 0; i < sel.options.length; i++) {
        if (sel.options[i].text === j.zonename) {
          sel.selectedIndex = i;
          found = true;
          break;
        }
      }
      if (!found) sel.value = j.tz;
    });
  }
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
  document.getElementById("sys-save").onclick = function () {
    var srvs = [];
    var tzSel = document.getElementById("tz-select");
    var zone = tzSel.options[tzSel.selectedIndex].text;
    document.querySelectorAll("#ntp-list input").forEach(function (inp) {
      if (inp.value) srvs.push(inp.value);
    });
    /* POST JSON reading the error code from the body even on HTTP failure */
    function postJSON(url, obj, stage) {
      return fetch(url, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(obj)
      }).then(function (r) {
        return r.json().then(function (j) {
          if (!r.ok || !j.ok) throw (j.error || stage);
        });
      });
    }
    postJSON("/api/hostname",
      { hostname: document.getElementById("host-input").value }, "hostname")
    .then(function () {
      return postJSON("/api/tz", { tz: tzSel.value, zonename: zone }, "tz");
    }).then(function () {
      return postJSON("/api/ntp", {
        enabled: document.getElementById("ntp-en").checked ? 1 : 0,
        servers: srvs.join(" ")
      }, "ntp");
    }).then(function () {
      toast("toast_saved", true);
      get("/api/hostname", function (h) {
        document.getElementById("host-input").value = h.hostname;
      });
      get("/api/ntp", renderNtp);
      get("/api/sys", renderSys);
    }).catch(function (code) {
      toastErr(typeof code === "string" ? { error: code } : null);
    });
  };
  document.getElementById("do-reboot").onclick = function () {
    confirmModal(t("rb_confirm"), function () {
      fetch("/api/reboot", { method: "POST" });
      toast("toast_rebooting", true);
    });
  };
  document.getElementById("timefmt-select").value = timeFmt();
  document.getElementById("timefmt-select").onchange = function () {
    localStorage.setItem("timefmt", document.getElementById("timefmt-select").value);
    get("/api/sys", renderSys);
    renderOverview();
  };
  document.getElementById("lang-select").value = storedLang();
  document.getElementById("lang-select").onchange = function () {
    applyLang(document.getElementById("lang-select").value);
  };
  var sTab = localStorage.getItem("stab") || "general";
  function showSTab() {
    ["general", "timesync", "lang"].forEach(function (id) {
      document.getElementById("s-pane-" + id).hidden = id !== sTab;
    });
    document.querySelectorAll("#s-tabs .g-tab").forEach(function (b) {
      b.className = "g-tab" + (b.getAttribute("data-s") === sTab ? " on" : "");
    });
  }
  document.querySelectorAll("#s-tabs .g-tab").forEach(function (b) {
    b.onclick = function () {
      sTab = b.getAttribute("data-s");
      localStorage.setItem("stab", sTab);
      showSTab();
    };
  });
  (function () {
    var sel = document.getElementById("tz-select");
    TZS.forEach(function (z) {
      var o = document.createElement("option");
      o.value = z[1];
      o.textContent = z[0];
      sel.appendChild(o);
    });
  })();
  showSTab();
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
  refreshOvToggles();
  showRoute(routeFromHash());
  (function () {
    var r = routeFromHash();
    if (r.sec === "status" && r.page === "channels") chanRefresh();
  })();
  applyLang(storedLang());
  loadSys();
})();
