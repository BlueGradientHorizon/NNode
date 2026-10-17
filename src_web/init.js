/* Startup sequence, runs last. */
  showSTab();

  refreshOvToggles();

  showRoute(routeFromHash());

  (function () {
    var r = routeFromHash();
    if (r.sec === "status" && r.page === "channels") chanRefresh();
  })();

  applyLang(storedLang());

  loadSys();
