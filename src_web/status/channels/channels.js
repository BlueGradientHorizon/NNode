/* Page logic, see manifest.json for bundle order. */
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
