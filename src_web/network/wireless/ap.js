/* Page logic, see manifest.json for bundle order. */
  function openApModal() {
    var cur = lastAp;
    if (!cur) return;
    var wrap = document.createElement("div");
    wrap.className = "frm";
    function apRow(label, node) {
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
      return p;
    }
    var inSsid = document.createElement("input");
    inSsid.type = "text"; inSsid.size = 20; inSsid.maxLength = 32;
    inSsid.value = cur.ssid || "";
    apRow("SSID", inSsid);
    var inAuth = document.createElement("select");
    ["open", "wpa2", "wpa_wpa2"].forEach(function (a) {
      var o = document.createElement("option");
      o.value = a; o.textContent = a;
      if (cur.auth === a) o.selected = true;
      inAuth.appendChild(o);
    });
    apRow("Auth", inAuth);
    var inPass = document.createElement("input");
    inPass.type = "text"; inPass.size = 20; inPass.maxLength = 63;
    inPass.value = cur.pass || "";
    apRow(t("password"), inPass);
    var inMode = document.createElement("select");
    [["auto", t("ap_auto")], ["always", t("ap_always")]].forEach(function (m) {
      var o = document.createElement("option");
      o.value = m[0]; o.textContent = m[1];
      if ((cur.mode || "auto") === m[0]) o.selected = true;
      inMode.appendChild(o);
    });
    apRow(t("ap_mode"), inMode);
    var inNat = document.createElement("input");
    inNat.type = "checkbox"; inNat.checked = !!cur.nat;
    var labNat = document.createElement("label");
    labNat.appendChild(inNat);
    labNat.appendChild(document.createTextNode(" " + t("ap_nat")));
    apRow("", labNat);
    openModal(t("ap_title"), wrap, [
      mkbtn(t("save"), "btn-primary", function () {
        fetch("/api/ap", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({
            ssid: inSsid.value,
            auth: inAuth.value,
            pass: inPass.value,
            mode: inMode.value,
            nat: inNat.checked ? 1 : 0
          })
        }).then(function (r) {
          return r.json().then(function (jj) { return { http: r.ok, body: jj }; });
        }).then(function (o) {
          if (o.http && o.body.ok) {
            closeModal();
            toast("toast_saved", true);
            get("/api/ap", function (jj) {
              lastAp = jj;
              if (lastNet) renderWlNets(lastNet);
            });
          } else {
            toastErr(o.body);
          }
        }).catch(function () {
          toastErr();
        });
      }),
      mkbtn(t("dismiss"), "", closeModal)
    ]);
  }
