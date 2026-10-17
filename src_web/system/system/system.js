/* Page logic, see manifest.json for bundle order. */
  var REPO_URL = "https://github.com/BlueGradientHorizon/nnode";

  /* Footer shows "NNode <version> <hash> / IDF <idf>" with the hash
   * linked to the project repository. fw versions predate the
   * "<version> <hash>" format, so a hash-less value renders as plain text. */
  function renderFooter(j) {
    var el = document.getElementById("foot-ver");
    var fw = j.fw || "?";
    var cut = fw.indexOf(" ");
    var ver = cut < 0 ? fw : fw.slice(0, cut);
    var hash = cut < 0 ? "" : fw.slice(cut + 1);
    el.innerHTML = "";
    el.appendChild(document.createTextNode("NNode " + ver + " "));
    if (hash) {
      var a = document.createElement("a");
      a.href = REPO_URL;
      a.target = "_blank";
      a.rel = "noopener";
      a.textContent = hash;
      el.appendChild(a);
      el.appendChild(document.createTextNode(" "));
    }
    el.appendChild(document.createTextNode(" / IDF " + j.idf));
  }

  function renderSys(j) {
    lastSys = j;
    renderOverview();
    renderFooter(j);
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

  document.getElementById("time-sync").onclick = function () {
    var ts = Math.floor(Date.now() / 1000);
    fetch("/api/time", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ ts: ts })
    }).then(function () { get("/api/sys", renderSys); });
  };

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
