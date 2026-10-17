/* Page logic, see manifest.json for bundle order. */
  function renderIfaceCards() {
    var box, j, staIf, apIf, staUp, apUp;
    if (!lastNet) return;
    box = document.getElementById("iface-list");
    if (!box) return;
    j = lastNet;
    box.innerHTML = "";
    staIf = ifaceByName("sta");
    apIf = ifaceByName("ap");
    staUp = !!staIf && !!staIf.up && j.ip && j.ip !== "0.0.0.0";
    apUp = !!j.ap_up;
    (function () {
      var row = document.createElement("div");
      row.className = "iface-row";
      var icon = document.createElement("div");
      icon.className = "iface-icon";
      var nm = document.createElement("div");
      nm.className = "in-name sta";
      nm.textContent = "STA";
      var sub = document.createElement("div");
      sub.className = "in-sub";
      sub.textContent = "sta";
      icon.appendChild(nm);
      icon.appendChild(sub);
      var info = document.createElement("div");
      info.className = "iface-info";
      lnRow(info, t("if_protocol"), t("proto_dhcp"));
      lnRow(info, t("if_carrier"), staUp ? t("present") : t("absent"));
      lnRow(info, t("if_uptime"), fmtDur(j.sta_uptime || 0));
      lnRow(info, "MAC", staIf ? dispMac(staIf.mac) : "?");
      lnRow(info, "RX", fmtPkts(staIf ? staIf.rx : 0));
      lnRow(info, "TX", fmtPkts(staIf ? staIf.tx : 0));
      lnRow(info, "IPv4", j.cidr || (j.ip + " / " + j.mask));
      lnRow(info, t("encryption"), j.encr || "-");
      var act = document.createElement("div");
      act.className = "iface-actions";
      act.appendChild(mkbtn(t("restart"), "", function () {
        fetch("/api/renew", { method: "POST" }).then(function () {
          get("/api/net", renderNet);
        });
      }));
      act.appendChild(mkbtn(t("stop"), "", function () {
        fetch("/api/sta/disconnect", { method: "POST" }).then(function () {
          get("/api/net", renderNet);
          get("/api/nets", renderNets);
        });
      }, !staUp));
      row.appendChild(icon);
      row.appendChild(info);
      row.appendChild(act);
      box.appendChild(row);
    })();
    (function () {
      var row = document.createElement("div");
      row.className = "iface-row";
      var icon = document.createElement("div");
      icon.className = "iface-icon";
      var nm = document.createElement("div");
      nm.className = "in-name ap";
      nm.textContent = "AP";
      var sub = document.createElement("div");
      sub.className = "in-sub";
      sub.textContent = "ap";
      icon.appendChild(nm);
      icon.appendChild(sub);
      var info = document.createElement("div");
      info.className = "iface-info";
      lnRow(info, t("if_protocol"), t("proto_static"));
      lnRow(info, t("if_carrier"), apUp ? t("present") : t("absent"));
      lnRow(info, t("if_uptime"), fmtDur(j.ap_uptime || 0));
      lnRow(info, "MAC", apIf ? dispMac(apIf.mac) : "?");
      lnRow(info, "RX", fmtPkts(apIf ? apIf.rx : 0));
      lnRow(info, "TX", fmtPkts(apIf ? apIf.tx : 0));
      lnRow(info, "IPv4", apIf ? apIf.ip : "?");
      lnRow(info, t("clients"), String(lastStations.length));
      row.appendChild(icon);
      row.appendChild(info);
      box.appendChild(row);
    })();
  }

  function renderNetIf(j) {
    renderIfaceCards();
    renderRoutes();
  }
