/* Page logic, see manifest.json for bundle order. */
  function wlFreq(ch) {
    if (!ch) return "-";
    if (ch === 14) return "2.484 GHz";
    return (2.407 + 0.005 * ch).toFixed(3) + " GHz";
  }

  function sigBox(rssi) {
    var box = document.createElement("div");
    box.className = "sigbox";
    box.innerHTML = sigBars(rssi);
    box.appendChild(document.createTextNode(rssi + "/-92 dBm"));
    return box;
  }

  function apAuthName(a) {
    if (a === "open") return "None";
    if (a === "wpa2") return "WPA2 PSK";
    if (a === "wpa_wpa2") return "WPA/WPA2 PSK";
    return a || "?";
  }
  /* Modal stack: each new modal opens above the open one with its own
   * dimming layer, 15px lower. closeModal removes the topmost. */

  function openScanModal() {
    var wrap = document.createElement("div");
    var tbl = document.createElement("table");
    tbl.className = "table";
    var thead = document.createElement("thead");
    var hr = document.createElement("tr");
    [t("col_signal"), "SSID", t("channel"), t("ch_mode"), "BSSID", t("encryption"), ""]
      .forEach(function (s) {
        var th = document.createElement("th");
        th.textContent = s;
        hr.appendChild(th);
      });
    thead.appendChild(hr);
    tbl.appendChild(thead);
    var tb = document.createElement("tbody");
    tbl.appendChild(tb);
    wrap.appendChild(tbl);
    var loading = document.createElement("div");
    loading.className = "modal-scan";
    var sp = document.createElement("span");
    sp.className = "spin";
    loading.appendChild(sp);
    loading.appendChild(document.createTextNode(t("scanning")));
    wrap.appendChild(loading);
    openModal(t("scan_title"), wrap, [mkbtn(t("dismiss"), "", closeModal)]);
    get("/api/scan", function (arr) {
      loading.remove();
      lastScan = Array.isArray(arr) ? arr : [];
      renderScanRows(tb, lastScan);
      if (lastNet) renderWlNets(lastNet);
    });
  }

  function renderScanRows(tb, arr) {
    if (!Array.isArray(arr)) arr = [];
    tb.innerHTML = "";
    arr.forEach(function (x) {
      var tr = document.createElement("tr");
      tr.innerHTML = "<td></td><td></td><td></td><td></td><td></td><td></td><td></td>";
      tr.children[0].innerHTML = sigBars(x.rssi);
      tr.children[0].appendChild(document.createTextNode(x.rssi + " dBm"));
      var dot = document.createElement("span");
      dot.className = "ssid-dot";
      dot.style.background = chanColor(x);
      tr.children[1].appendChild(dot);
      tr.children[1].appendChild(document.createTextNode(x.ssid || "?"));
      tr.children[2].textContent = x.channel;
      tr.children[3].textContent = t("mode_master");
      tr.children[4].textContent = x.bssid;
      tr.children[5].textContent = x.auth;
      var btn = mkbtn(t("join"), "btn-primary", (function (s) { return function () {
        if (knownNets.indexOf(s) >= 0) {
          staConnect(s, null);
          closeModal();
        } else {
          openJoinModal(s);
        }
      }; })(x.ssid));
      tr.children[6].appendChild(btn);
      tb.appendChild(tr);
    });
  }

  function renderNetWl(j) {
    renderWlOverview(j);
    renderWlNets(j);
  }
  /* Joining a new network above the scan window: password plus
   * an optional bind to the over-the-air BSSID (saves the net at once). */

  function openJoinModal(ssid) {
    var si = scanInfo(ssid);
    var bssid = si && si.bssid ? si.bssid : "";
    var wrap = document.createElement("div");
    wrap.className = "frm";
    function joinRow(label, node) {
      var r = document.createElement("div");
      r.className = "frm-row";
      var lab = document.createElement("span");
      lab.className = "flab";
      lab.textContent = label;
      var val = document.createElement("div");
      val.className = "fval";
      var p = document.createElement("p");
      p.appendChild(node);
      val.appendChild(p);
      r.appendChild(lab);
      r.appendChild(val);
      wrap.appendChild(r);
      return r;
    }
    var inPass = document.createElement("input");
    inPass.type = "text"; inPass.size = 20; inPass.maxLength = 63;
    inPass.placeholder = t("net_keeppass_open");
    joinRow(t("password"), inPass);
    var inLock = document.createElement("input");
    inLock.type = "checkbox"; inLock.checked = false;
    var labLock = document.createElement("label");
    labLock.appendChild(inLock);
    labLock.appendChild(document.createTextNode(" " + t("net_lock_bssid") +
      (bssid ? " (" + bssid + ")" : "")));
    var lockRow = joinRow("", labLock);
    if (!bssid) lockRow.style.display = "none";
    openModal(ssid, wrap, [
      mkbtn(t("join"), "btn-primary", function () {
        var pass = inPass.value;
        if (inLock.checked && bssid) {
          var url = "/api/nets/add?ssid=" + encodeURIComponent(ssid);
          if (pass) url += "&pass=" + encodeURIComponent(pass);
          url += "&bssid=" + bssid;
          fetch(url, { method: "POST" })
            .then(function (r) { return r.json(); }).then(function (jj) {
              if (jj.ok) {
                closeAllModals();
                toast("toast_saved", true);
                staConnect(ssid, pass || null);
              } else {
                toastErr(jj);
              }
            }).catch(function () {
              toastErr();
            });
        } else {
          closeAllModals();
          staConnect(ssid, pass || null);
        }
      }),
      mkbtn(t("dismiss"), "", closeModal)
    ]);
  }

  function renderWlOverview(j) {
    var box = document.getElementById("wl-radio");
    if (!box) return;
    box.innerHTML = "";
    var row = document.createElement("div");
    row.className = "iface-row";
    var icon = document.createElement("div");
    icon.className = "iface-icon";
    var nm = document.createElement("div");
    nm.className = "in-name ap";
    nm.textContent = "radio";
    var sub = document.createElement("div");
    sub.className = "in-sub";
    sub.textContent = (typeof lastSys !== "undefined" && lastSys && lastSys.model) || "NNode";
    icon.appendChild(nm);
    icon.appendChild(sub);
    var info = document.createElement("div");
    info.className = "iface-info";
    var title = document.createElement("div");
    title.className = "wl-title";
    title.textContent = ((typeof lastSys !== "undefined" && lastSys && lastSys.model) || "NNode") + " 802.11b/g/n";
    info.appendChild(title);
    lnRow(info, t("channel"), j.channel + " (" + wlFreq(j.channel) + ") | " +
      t("bitrate") + ": -");
    var act = document.createElement("div");
    act.className = "iface-actions";
    act.appendChild(mkbtn(t("restart"), "", function () {
      if (j.ssid && j.ssid !== "-") staConnect(j.ssid, null);
      get("/api/net", renderNet);
    }));
    act.appendChild(mkbtn(t("scan"), "btn-primary", function () { openScanModal(); }));
    act.appendChild(mkbtn(t("add"), "btn-ok", function () { openAddModal(); }));
    row.appendChild(icon);
    row.appendChild(info);
    row.appendChild(act);
    box.appendChild(row);
  }

  function scanAuthName(a) {
    if (a === "open") return "None";
    if (a === "wep") return "WEP";
    if (a === "wpa") return "WPA PSK";
    if (a === "wpa2") return "WPA2 PSK";
    if (a === "wpa/wpa2") return "WPA/WPA2 PSK";
    if (a === "enterprise") return "Enterprise";
    if (a === "wpa3") return "WPA3 SAE";
    if (a === "wpa2/wpa3") return "WPA2/WPA3 PSK";
    if (a === "wapi") return "WAPI PSK";
    return a || "?";
  }

  function scanInfo(ssid) {
    var i;
    for (i = 0; i < lastScan.length; i++) {
      if (lastScan[i].ssid === ssid) return lastScan[i];
    }
    return null;
  }

  function storedNet(ssid) {
    var i, ns = lastNets ? lastNets.nets : [];
    for (i = 0; i < ns.length; i++) {
      if (ns[i].ssid === ssid) return ns[i];
    }
    return null;
  }

  function storedAuthName(a) {    if (a === "open") return "None";
    if (a === "wpa") return "WPA PSK";
    if (a === "wpa2") return "WPA2 PSK";
    if (a === "wpa3") return "WPA3 SAE";
    return "Auto";
  }

  function authSelect(cur) {
    var sel = document.createElement("select");
    ["auto", "open", "wpa", "wpa2", "wpa3"].forEach(function (a) {
      var o = document.createElement("option");
      o.value = a; o.textContent = a;
      if ((cur || "auto") === a) o.selected = true;
      sel.appendChild(o);
    });
    return sel;
  }

  function savedBssid(ssid) {
    var st = storedNet(ssid), si;
    if (st && st.bssid) return st.bssid;
    si = scanInfo(ssid);
    return (si && si.bssid) ? si.bssid : "-";
  }

  function savedEncr(ssid) {
    var st = storedNet(ssid), si;
    if (st && st.auth && st.auth !== "auto") return storedAuthName(st.auth);
    si = scanInfo(ssid);
    return si ? scanAuthName(si.auth) : "?";
  }

  function offBox() {
    var off = document.createElement("div");
    off.className = "sigbox off";
    off.textContent = "X " + t("down");
    return off;
  }

  function forgetNet(ssid) {
    if (!ssid) return;
    confirmModal(t("del_net_confirm").replace("{s}", ssid), function () {
      fetch("/api/nets/forget?ssid=" + encodeURIComponent(ssid), { method: "POST" })
        .then(function () {
          get("/api/nets", renderNets);
          get("/api/net", renderNet);
        });
    }, "delete");
  }

  function openAddModal() {
    var wrap = document.createElement("div");
    wrap.className = "frm";
    function addRow(label, node) {
      var r = document.createElement("div");
      r.className = "frm-row";
      var lab = document.createElement("span");
      lab.className = "flab";
      lab.textContent = label;
      var val = document.createElement("div");
      val.className = "fval";
      var p = document.createElement("p");
      p.appendChild(node);
      val.appendChild(p);
      r.appendChild(lab);
      r.appendChild(val);
      wrap.appendChild(r);
    }
    var inSsid = document.createElement("input");
    inSsid.type = "text"; inSsid.size = 20; inSsid.maxLength = 32;
    addRow("ESSID", inSsid);
    var inBssid = document.createElement("input");
    inBssid.type = "text"; inBssid.size = 20; inBssid.maxLength = 17;
    inBssid.placeholder = t("any");
    addRow("BSSID", inBssid);
    var inAuth = authSelect("auto");
    addRow(t("encryption"), inAuth);
    var inPass = document.createElement("input");
    inPass.type = "text"; inPass.size = 20; inPass.maxLength = 63;
    inPass.placeholder = t("net_keeppass_open");
    addRow(t("password"), inPass);
    openModal(t("net_add_title"), wrap, [
      mkbtn(t("save"), "btn-primary", function () {
        var ss = inSsid.value;
        if (!ss) {
          toast("toast_error", false);
          return;
        }
        if (inAuth.value !== "open" && inAuth.value !== "auto" && !inPass.value) {
          toast("toast_error", false);
          return;
        }
        if (inBssid.value && !macOk(inBssid.value)) {
          toast("toast_error", false);
          return;
        }
        var url = "/api/nets/add?ssid=" + encodeURIComponent(ss);
        if (inPass.value) url += "&pass=" + encodeURIComponent(inPass.value);
        if (inBssid.value) url += "&bssid=" + inBssid.value;
        if (inAuth.value !== "auto") url += "&auth=" + encodeURIComponent(inAuth.value);
        fetch(url, { method: "POST" })
          .then(function (r) { return r.json(); }).then(function (jj) {
            if (jj.ok) {
              closeModal();
              toast("toast_saved", true);
              get("/api/nets", renderNets);
            } else {
              toastErr(jj);
            }
          }).catch(function () {
            toastErr();
          });
      }),
      mkbtn(t("dismiss"), "", closeModal)
    ]);
  }

  function openNetModal(ssid) {
    var st = storedNet(ssid);
    var si = st ? null : scanInfo(ssid);
    var wrap = document.createElement("div");
    wrap.className = "frm";
    function netRow(label, node) {
      var r = document.createElement("div");
      r.className = "frm-row";
      var lab = document.createElement("span");
      lab.className = "flab";
      lab.textContent = label;
      var val = document.createElement("div");
      val.className = "fval";
      var p = document.createElement("p");
      p.appendChild(node);
      val.appendChild(p);
      r.appendChild(lab);
      r.appendChild(val);
      wrap.appendChild(r);
    }
    var inSsid = document.createElement("input");
    inSsid.type = "text"; inSsid.size = 20; inSsid.maxLength = 32;
    inSsid.value = ssid;
    netRow("ESSID", inSsid);
    var inBssid = document.createElement("input");
    inBssid.type = "text"; inBssid.size = 20; inBssid.maxLength = 17;
    inBssid.placeholder = t("any");
    inBssid.value = st && st.bssid ? st.bssid :
      (si && si.bssid ? si.bssid : "");
    netRow("BSSID", inBssid);
    var inAuth = authSelect(st && st.auth ? st.auth :
      (si ? si.auth : "auto"));
    /* the scan reports its own names (wpa/wpa2...), the select has only ours */
    if (["auto", "open", "wpa", "wpa2", "wpa3"].indexOf(inAuth.value) < 0) {
      inAuth.value = "auto";
    }
    netRow(t("encryption"), inAuth);
    var inPass = document.createElement("input");
    inPass.type = "text"; inPass.size = 20; inPass.maxLength = 63;
    inPass.placeholder = t("net_keeppass");
    netRow(t("password"), inPass);
    openModal(t("net_edit_title"), wrap, [
      mkbtn(t("save"), "btn-primary", function () {
        var ns = inSsid.value;
        var url = "/api/nets/update?ssid=" + encodeURIComponent(ssid);
        if (!ns) {
          toast("toast_error", false);
          return;
        }
        if (ns !== ssid) url += "&new_ssid=" + encodeURIComponent(ns);
        if (inPass.value) url += "&pass=" + encodeURIComponent(inPass.value);
        if (inBssid.value && !macOk(inBssid.value)) {
          toast("toast_error", false);
          return;
        }
        url += "&bssid=" + inBssid.value;
        url += "&auth=" + encodeURIComponent(inAuth.value);
        fetch(url, { method: "POST" })
          .then(function (r) { return r.json(); }).then(function (jj) {
            if (jj.ok) {
              closeModal();
              toast("toast_saved", true);
              get("/api/nets", renderNets);
              get("/api/net", renderNet);
            } else {
              toastErr(jj);
            }
          }).catch(function () {
            toastErr();
          });
      }),
      mkbtn(t("dismiss"), "", closeModal)
    ]);
  }

  function renderWlNets(j) {
    var box = document.getElementById("wl-nets");
    var staUp, apUp, apIf, cur;
    if (!box || !j) return;
    box.innerHTML = "";
    staUp = !!(j.ip && j.ip !== "0.0.0.0");
    apUp = !!j.ap_up;
    apIf = ifaceByName("ap");
    cur = (j.ssid && j.ssid !== "-") ? j.ssid : null;
    (function () {
      var row = document.createElement("div");
      row.className = "iface-row";
      var ab = document.createElement("div");
      ab.className = "sigbox";
      ab.textContent = apUp ?
        (lastStations.length + " " + t("clients")) : t("down");
      row.appendChild(ab);
      var info = document.createElement("div");
      info.className = "iface-info";
      var apSsid = lastAp ? lastAp.ssid : "?";
      lnRow(info, "SSID", apSsid + " | " + t("mode") + ": " + t("mode_master"));
      lnRow(info, "BSSID", (apIf ? dispMac(apIf.mac) : "-") +
        " | " + t("encryption") + ": " + (lastAp ? apAuthName(lastAp.auth) : "?"));
      var act = document.createElement("div");
      act.className = "iface-actions";
      act.appendChild(mkbtn(t("edit"), "btn-primary", function () { openApModal(); }));
      row.appendChild(info);
      row.appendChild(act);
      box.appendChild(row);
    })();
    if (cur) {
      (function () {
        var row = document.createElement("div");
        row.className = "iface-row";
        row.appendChild(staUp ? sigBox(j.rssi) : offBox());
        var info = document.createElement("div");
        info.className = "iface-info";
        lnRow(info, "SSID", cur + " | " + t("mode") + ": " + t("mode_client"));
        lnRow(info, "BSSID", dispMac(j.bssid) + " | " + t("encryption") +
          ": " + (j.encr || "-"));
        var act = document.createElement("div");
        act.className = "iface-actions";
        if (staUp) {
          act.appendChild(mkbtn(t("disconnect"), "", function () {
            fetch("/api/sta/disconnect", { method: "POST" }).then(function () {
              get("/api/net", renderNet);
              get("/api/nets", renderNets);
            });
          }));
        } else {
          act.appendChild(mkbtn(t("connect"), "btn-primary", function () {
            staConnect(cur, null);
          }));
        }
        act.appendChild(mkbtn(t("edit"), "", (function (s) { return function () {
          openNetModal(s);
        }; })(cur)));
        act.appendChild(mkbtn(t("delete"), "btn-danger", (function (s) { return function () {
          forgetNet(s);
        }; })(cur)));
        row.appendChild(info);
        row.appendChild(act);
        box.appendChild(row);
      })();
    }
    (lastNets ? lastNets.nets : []).forEach(function (x) {
      if (x.ssid === cur) return;
      var row = document.createElement("div");
      row.className = "iface-row";
      row.appendChild(offBox());
      var info = document.createElement("div");
      info.className = "iface-info";
      lnRow(info, "SSID", x.ssid + " | " + t("mode") + ": " + t("mode_client"));
      lnRow(info, "BSSID", savedBssid(x.ssid) +
        " | " + t("encryption") + ": " + savedEncr(x.ssid));
      var act = document.createElement("div");
      act.className = "iface-actions";
      act.appendChild(mkbtn(t("connect"), "btn-primary", (function (s) { return function () {
        staConnect(s, null);
      }; })(x.ssid)));
      act.appendChild(mkbtn(t("edit"), "", (function (s) { return function () {
        openNetModal(s);
      }; })(x.ssid)));
      act.appendChild(mkbtn(t("delete"), "btn-danger", (function (s) { return function () {
        forgetNet(s);
      }; })(x.ssid)));
      row.appendChild(info);
      row.appendChild(act);
      box.appendChild(row);
    });
  }

  var knownNets = [];

  function staConnect(ssid, pass) {
    var url = "/api/sta/connect?ssid=" + encodeURIComponent(ssid);
    if (pass) url += "&pass=" + encodeURIComponent(pass);
    fetch(url, { method: "POST" }).then(function () {
      get("/api/nets", renderNets);
      get("/api/net", renderNet);
    });
  }
