/* Page logic, see manifest.json for bundle order. */
  function openModConfig(name) {
    fetch("/api/elf/config?name=" + encodeURIComponent(name))
      .then(function (r) { return r.json(); }).then(function (o) {
        if (!o || !o.fields) {
          toastErr(o);
          return;
        }
        renderModConfig(name, o);
      }).catch(function () {
        toastErr();
      });
  }
  /* show_if evaluator: OR of AND of comparisons, no spaces in expr.
   * Fields are declared after their controllers, so one pass suffices. */

  function modCondTerm(term, vals) {
    var ops = ["==", "!=", "<=", ">=", "<", ">"];
    for (var k = 0; k < ops.length; k++) {
      var ix = term.indexOf(ops[k]);
      if (ix > 0) {
        var key = term.slice(0, ix), lit = term.slice(ix + ops[k].length);
        var cur = vals[key] || "";
        if (ops[k] === "==") return cur === lit;
        if (ops[k] === "!=") return cur !== lit;
        var a = Number(cur), b = Number(lit);
        if (isNaN(a) || isNaN(b)) return false;
        if (ops[k] === "<") return a < b;
        if (ops[k] === ">") return a > b;
        if (ops[k] === "<=") return a <= b;
        return a >= b;
      }
    }
    return false;
  }

  function modCond(expr, vals) {
    var ors = String(expr).split("||");
    for (var i = 0; i < ors.length; i++) {
      var ands = ors[i].split("&&"), ok = true;
      for (var j = 0; j < ands.length; j++) {
        if (!ands[j] || !modCondTerm(ands[j], vals)) { ok = false; break; }
      }
      if (ok) return true;
    }
    return false;
  }

  function modFieldWidget(f, cur, inputs) {
    var el;
    if (f.type === "bool") {
      el = document.createElement("input");
      el.type = "checkbox";
      el.checked = cur === "1";
    } else if (f.type === "enum" && f.options && f.options.length) {
      el = document.createElement("select");
      f.options.forEach(function (v) {
        var op = document.createElement("option");
        op.value = v;
        op.textContent = v;
        el.appendChild(op);
      });
      el.value = cur;
    } else if (f.type === "wpakey") {
      el = document.createElement("input");
      el.type = "password";
      el.size = 20;
      el.maxLength = 64;
      el.value = cur;
    } else if (f.type === "list") {
      el = document.createElement("input");
      el.type = "text";
      el.size = 30;
      el.maxLength = 128;
      el.placeholder = "a,b,c";
      el.value = cur;
    } else {
      el = document.createElement("input");
      el.type = "text";
      el.size = (f.type === "int" || f.type === "port") ? 12 : 20;
      el.maxLength = f.maxlen > 0 ? f.maxlen : 128;
      el.value = cur;
    }
    inputs[f.key] = el;
    return el;
  }
  /* One-line input requirements from the schema, if any. Selects
   * and checkboxes need none: their choices are on the screen. */

  function modHint(f) {
    if (f.type === "bool" || f.type === "enum") return "";
    if (f.type === "int") {
      if (f.has_min && f.has_max) return f.min + " .. " + f.max;
      if (f.has_min) return ">= " + f.min;
      if (f.has_max) return "<= " + f.max;
      return "";
    }
    if (f.type === "port") return "1 .. 65535";
    if (f.type === "string") {
      return "max " + (f.maxlen > 0 ? f.maxlen : 64) + " chars";
    }
    if (f.type === "hostname") return "letters, digits, - and .";
    if (f.type === "ip4addr") return "A.B.C.D";
    if (f.type === "netmask") return "e.g. 255.255.255.0";
    if (f.type === "cidr4") return "A.B.C.D/N";
    if (f.type === "macaddr") return "XX:XX:XX:XX:XX:XX";
    if (f.type === "range") {
      var h = "LO-HI";
      if (f.has_min && f.has_max) h += ", " + f.min + " .. " + f.max;
      return h;
    }
    if (f.type === "wpakey") return "8..63 chars or 64 hex";
    if (f.type === "list") {
      var s = "comma-separated";
      if (f.sub) s += ", " + f.sub;
      if (f.maxitems > 0) s += ", up to " + f.maxitems;
      return s;
    }
    return "";
  }

  function renderModConfig(name, o) {
    var wrap = document.createElement("div");
    var inputs = {};
    var rows = {};
    var tabs = o.tabs && o.tabs.length ? o.tabs : [{ id: "", title: "" }];
    var panes = {};
    var curTab = tabs[0].id;
    var bar = null;
    function curVals() {
      var v = {};
      (o.fields || []).forEach(function (f) {
        var el = inputs[f.key];
        if (!el) return;
        v[f.key] = el.type === "checkbox" ? (el.checked ? "1" : "0") : el.value;
      });
      return v;
    }
    function applyVis() {
      var vals = curVals(), counts = {};
      (o.fields || []).forEach(function (f) {
        var vis = !f.show_if || modCond(f.show_if, vals);
        if (rows[f.key]) rows[f.key].hidden = !vis;
        if (vis) counts[f.tab || ""] = (counts[f.tab || ""] || 0) + 1;
      });
      tabs.forEach(function (tb) {
        var pane = panes[tb.id];
        if (!pane) return;
        var empty = pane.querySelector("[data-empty]");
        if (!counts[tb.id]) {
          if (!empty) {
            var r = document.createElement("div");
            r.className = "frm-row";
            r.setAttribute("data-empty", "1");
            var s = document.createElement("span");
            s.className = "flab";
            var d = document.createElement("div");
            d.className = "fval";
            var p = document.createElement("p");
            p.textContent = t("cfg_hidden");
            d.appendChild(p);
            r.appendChild(s);
            r.appendChild(d);
            pane.appendChild(r);
          }
        } else if (empty) {
          pane.removeChild(empty);
        }
      });
    }
    if (tabs.length > 1) {
      bar = document.createElement("div");
      bar.className = "g-tabs";
      tabs.forEach(function (tb) {
        var b = document.createElement("button");
        b.type = "button";
        b.className = "g-tab" + (tb.id === curTab ? " on" : "");
        b.textContent = tb.title || tb.id;
        b.onclick = function () {
          curTab = tb.id;
          tabs.forEach(function (x) { panes[x.id].hidden = x.id !== curTab; });
          Array.prototype.forEach.call(bar.children, function (x) {
            x.className = "g-tab" + (x.textContent === (tb.title || tb.id) ? " on" : "");
          });
        };
        bar.appendChild(b);
      });
      wrap.appendChild(bar);
    }
    tabs.forEach(function (tb) {
      var pane = document.createElement("div");
      pane.className = "frm";
      pane.hidden = tb.id !== curTab;
      (o.fields || []).forEach(function (f) {
        if ((f.tab || "") !== tb.id) return;
        var r = document.createElement("div");
        r.className = "frm-row";
        var lab = document.createElement("span");
        lab.className = "flab";
        lab.textContent = f.label || f.key;
        var val = document.createElement("div");
        val.className = "fval";
        var p = document.createElement("p");
        var el = modFieldWidget(f, (o.values && o.values[f.key]) || f.default || "", inputs);
        el.oninput = applyVis;
        el.onchange = applyVis;
        p.appendChild(el);
        var hint = modHint(f);
        if (hint) {
          var hp = document.createElement("div");
          hp.className = "fhint";
          hp.textContent = hint;
          p.appendChild(hp);
        }
        val.appendChild(p);
        r.appendChild(lab);
        r.appendChild(val);
        rows[f.key] = r;
        pane.appendChild(r);
      });
      panes[tb.id] = pane;
      wrap.appendChild(pane);
    });
    applyVis();
    openModal(t("settings") + ": " + name, wrap, [
      mkbtn(t("save"), "btn-primary", function () {
        var body = {};
        (o.fields || []).forEach(function (f) {
          var el = inputs[f.key];
          body[f.key] = el.type === "checkbox" ? (el.checked ? "1" : "0") : el.value;
        });
        fetch("/api/elf/config/save?name=" + encodeURIComponent(name), {
          method: "POST",
          body: JSON.stringify(body)
        }).then(function (r) { return r.json(); }).then(function (jj) {
          if (jj.ok) {
            closeModal();
            toast("toast_saved", true);
          } else {
            toastErr(jj);
          }
        }).catch(function () {
          toastErr();
        });
      }),
      mkbtn(t("cfg_reset"), "", function () {
        (o.fields || []).forEach(function (f) {
          var el = inputs[f.key];
          var d = f.default || "";
          if (el.type === "checkbox") el.checked = d === "1";
          else el.value = d;
        });
        applyVis();
      }),
      mkbtn(t("dismiss"), "", closeModal)
    ]);
  }
