/* Page logic, see manifest.json for bundle order. */
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
