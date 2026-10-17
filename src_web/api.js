/* Shared data layer: fetch helpers, files/modules loading. */
  function get(url, ok) {
    fetch(url).then(function (r) {
      if (!r.ok) throw 0;
      return r.json();
    }).then(ok).catch(function () { /* stay silent until the next poll */ });
  }

  var curDir = "";

  var lastModFiles = [];

  function filesUrl(d) {
    return "/api/files" + (d ? "?dir=" + encodeURIComponent(d) : "");
  }

  function loadFiles() {
    get(filesUrl(curDir), function (arr) { renderAll(arr, curDir); });
  }

  function loadModules() {
    get("/api/files?dir=programs", function (arr) {
      get("/api/elf/instances", function (o) {
        renderModules(arr, (o && o.instances) || []);
      });
    });
  }

  function renderAll(arr, dir) {
    renderFiles(arr, dir || "");
    /* Modules live in programs/, keep their table on that listing. */
    if (!dir) loadModules();
  }
