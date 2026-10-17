/* Page logic, see manifest.json for bundle order. */
  function renderAsList(wrap, progs) {
    var avail;
    wrap.innerHTML = "";
    if (!progs.length) {
      var p = document.createElement("p");
      p.className = "descr";
      p.textContent = t("as_empty");
      wrap.appendChild(p);
    }
    var tbl = document.createElement("table");
    tbl.className = "table";
    var thead = document.createElement("thead");
    var hr = document.createElement("tr");
    ["\u2116", t("col_name"), ""].forEach(function (s) {
      var th = document.createElement("th");
      th.textContent = s;
      hr.appendChild(th);
    });
    thead.appendChild(hr);
    tbl.appendChild(thead);
    var tb = document.createElement("tbody");
    tbl.appendChild(tb);
    progs.forEach(function (s, idx) {
      var tr = document.createElement("tr");
      tr.innerHTML = "<td></td><td></td><td></td>";
      tr.children[0].textContent = idx + 1;
      tr.children[1].textContent = s;
      var up = mkbtn("\u2191", "btn-primary", function () {
        asOp("/api/elf/autostart/move?name=" + encodeURIComponent(s) + "&dir=up", wrap);
      }, idx === 0);
      up.title = t("as_up");
      var dn = mkbtn("\u2193", "btn-primary", function () {
        asOp("/api/elf/autostart/move?name=" + encodeURIComponent(s) + "&dir=down", wrap);
      }, idx === progs.length - 1);
      dn.title = t("as_down");
      var del = mkbtn(t("as_remove"), "btn-danger", function () {
        asOp("/api/elf/autostart/remove?name=" + encodeURIComponent(s), wrap);
      }, false);
      tr.children[2].appendChild(up);
      tr.children[2].appendChild(document.createTextNode(" "));
      tr.children[2].appendChild(dn);
      tr.children[2].appendChild(document.createTextNode(" "));
      tr.children[2].appendChild(del);
      tb.appendChild(tr);
    });
    wrap.appendChild(tbl);
    /* Add row always stays: disabled when nothing is left to add. */
    avail = lastModFiles.filter(function (x) {
      return x.is_elf && progs.indexOf(x.name) < 0;
    });
    var ar = document.createElement("p");
    var sel = document.createElement("select");
    avail.forEach(function (x) {
      var op = document.createElement("option");
      op.value = x.name;
      op.textContent = x.name;
      sel.appendChild(op);
    });
    sel.disabled = !avail.length;
    var add = mkbtn(t("add"), "btn-primary", function () {
      if (!sel.value) return;
      asOp("/api/elf/autostart/add?name=" + encodeURIComponent(sel.value), wrap);
    }, !avail.length);
    ar.appendChild(sel);
    ar.appendChild(document.createTextNode(" "));
    ar.appendChild(add);
    wrap.appendChild(ar);
  }

  function asOp(url, wrap) {
    fetch(url, { method: "POST" })
      .then(function (r) { return r.json(); }).then(function (j) {
        if (!j.ok) {
          toastErr(j);
          return;
        }
        get("/api/elf/autostart", function (o) {
          renderAsList(wrap, (o && o.progs) || []);
        });
      }).catch(function () {
        toastErr();
      });
  }

  function openAutostart() {
    get("/api/elf/autostart", function (o) {
      var wrap = document.createElement("div");
      renderAsList(wrap, (o && o.progs) || []);
      openModal(t("as_title"), wrap, [mkbtn(t("dismiss"), "", closeModal)]);
    });
  }

  document.getElementById("autostart-btn").onclick = function () {
    openAutostart();
  };
  /* Per-module settings: tabs and fields rendered from the schema the
   * firmware serves (/api/elf/config). Values apply at module start. */
