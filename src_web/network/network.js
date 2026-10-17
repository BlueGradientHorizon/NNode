/* Page logic, see manifest.json for bundle order. */
  var lastAp = null, lastStations = [], lastNets = null, lastScan = [];

  function ifaceByName(n) {
    var i;
    if (!lastNet || !lastNet.ifaces) return null;
    for (i = 0; i < lastNet.ifaces.length; i++) {
      if (lastNet.ifaces[i].name === n) return lastNet.ifaces[i];
    }
    return null;
  }

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

  function renderNets(j) {
    lastNets = j;
    knownNets = j.nets.map(function (x) { return x.ssid; });
    if (lastNet) renderWlNets(lastNet);
  }
