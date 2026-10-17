/* Routing, polling orchestration, shared net/summary state. */
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
      loadModules();
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
      loadFiles();
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

  var lastSummary = null, lastSys = null, lastNet = null;

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

  function renderNet(j) {
    lastNet = j;
    renderOverview();
    renderNetIf(j);
    renderNetWl(j);
  }
