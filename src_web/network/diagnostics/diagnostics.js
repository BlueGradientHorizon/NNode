/* Page logic, see manifest.json for bundle order. */
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
