/* Page logic, see manifest.json for bundle order. */
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
