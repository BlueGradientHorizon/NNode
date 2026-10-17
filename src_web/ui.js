/* Reusable UI builders: buttons, modal, toast, rows, signal bars. */
  function ovRow(tb, label, value) {
    var tr = document.createElement("tr");
    tr.innerHTML = "<td></td><td></td>";
    tr.children[0].textContent = label;
    tr.children[1].textContent = value;
    tb.appendChild(tr);
  }

  function ovBarRow(tb, label, part, total, fmt) {
    var pct = total > 0 ? (100 * part / total) : 0;
    var tr = document.createElement("tr");
    tr.innerHTML = "<td></td><td></td>";
    tr.children[0].textContent = label;
    var val = document.createElement("div");
    val.className = "ov-val";
    val.textContent = fmt(part) + " / " + fmt(total) +
      " (" + pct.toFixed(0) + "%)";
    var bar = document.createElement("div");
    bar.className = "ov-bar";
    var fill = document.createElement("div");
    fill.className = "ov-fill";
    fill.style.width = pct.toFixed(1) + "%";
    bar.appendChild(fill);
    tr.children[1].appendChild(val);
    tr.children[1].appendChild(bar);
    tb.appendChild(tr);
  }

  function mkbtn(text, cls, onclick, disabled) {
    var b = document.createElement("button");
    b.type = "button";
    b.className = "btn " + (cls || "");
    b.textContent = text;
    if (disabled) b.disabled = true;
    if (onclick) b.onclick = onclick;
    return b;
  }

  function lnRow(info, label, value) {
    var d = document.createElement("div");
    d.className = "ln";
    var b = document.createElement("b");
    b.textContent = label + ": ";
    d.appendChild(b);
    d.appendChild(document.createTextNode(value));
    info.appendChild(d);
  }

  function openModal(title, bodyEl, footBtns) {
    var ov = document.getElementById("modal-ov");
    var layer = document.createElement("div");
    layer.className = "modal-layer";
    layer.style.paddingTop = (60 + ov.children.length * 15) + "px";
    layer.addEventListener("click", function (e) {
      if (e.target === layer) closeModal();
    });
    var modal = document.createElement("div");
    modal.className = "modal";
    modal.setAttribute("role", "dialog");
    var head = document.createElement("div");
    head.className = "modal-head";
    var ht = document.createElement("span");
    ht.textContent = title;
    head.appendChild(ht);
    if (!title) head.style.display = "none";
    var b = document.createElement("div");
    b.className = "modal-body";
    b.appendChild(bodyEl);
    var f = document.createElement("div");
    f.className = "modal-foot";
    (footBtns || []).forEach(function (x) { f.appendChild(x); });
    modal.appendChild(head);
    modal.appendChild(b);
    modal.appendChild(f);
    layer.appendChild(modal);
    ov.appendChild(layer);
    ov.hidden = false;
  }

  function closeModal() {
    var ov = document.getElementById("modal-ov");
    if (ov.lastChild) ov.lastChild.remove();
    if (!ov.lastChild) ov.hidden = true;
  }

  function closeAllModals() {
    var ov = document.getElementById("modal-ov");
    ov.innerHTML = "";
    ov.hidden = true;
  }
  /* A question with two buttons: text plus OK/Close */

  function confirmModal(msg, onYes, yesKey) {
    var body = document.createElement("div");
    body.textContent = msg;
    openModal("", body, [
      mkbtn(t(yesKey || "confirm_ok"), "btn-primary", function () {
        closeModal();
        onYes();
      }),
      mkbtn(t("dismiss"), "", closeModal)
    ]);
  }

  var toastTimer = 0;

  function toast(key, ok) {
    document.getElementById("toast-msg").textContent = t(key);
    var box = document.getElementById("toast-box");
    box.className = "toast" + (ok === true ? " ok" : ok === false ? " err" : "");
    document.getElementById("toast-ov").hidden = false;
    if (toastTimer) clearTimeout(toastTimer);
    toastTimer = setTimeout(function () {
      document.getElementById("toast-ov").hidden = true;
      toastTimer = 0;
    }, 1000);
  }

  document.getElementById("toast-ov").addEventListener("click", function () {
    if (toastTimer) clearTimeout(toastTimer);
    toastTimer = 0;
    document.getElementById("toast-ov").hidden = true;
  });
  /* Firmware error code (jj.error) to the err_<code> translation key.
   * Unknown codes and dropped connections give the generic toast_error. */

  function toastErr(jj) {
    var code = jj && typeof jj.error === "string" ? jj.error : "";
    var key = code ? "err_" + code : "toast_error";
    var l = lang();
    if (!I18N[l] || !I18N[l][key]) key = "toast_error";
    toast(key, false);
  }

  document.addEventListener("keydown", function (e) {
    if (e.key === "Escape" && !document.getElementById("modal-ov").hidden) closeModal();
  });

  function sigBars(rssi) {
    var n = rssi > -70 ? 4 : rssi > -80 ? 3 : rssi > -90 ? 2 : 1;
    var s = '<span class="sigbars">';
    var hs = [4, 7, 10, 13], i;
    for (i = 0; i < 4; i++) {
      s += '<i class="' + (i < n ? "f" : "") + '" style="height:' +
        hs[i] + 'px"></i>';
    }
    return s + "</span>";
  }
