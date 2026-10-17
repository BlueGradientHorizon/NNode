/* Page logic, see manifest.json for bundle order. */
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
