/* Page logic, see manifest.json for bundle order. */
  function renderFilesSys(j) {
    var el = document.getElementById("files-space-text");
    var fill = document.getElementById("files-space-fill");
    if (!el || !fill || !j) return;
    var total = j.fs_total || 0;
    var used = j.fs_used || 0;
    var free = total > used ? total - used : 0;
    var pct = total > 0 ? (100 * used / total) : 0;
    el.textContent = t("disk_space") + ": " + pct.toFixed(2) + "% " +
      t("used_of") + " (" + fmtMiB(used) + " " + t("used_of") + " " +
      t("of_word") + " " + fmtMiB(total) + ", " + fmtMiB(free) + " " +
      t("free") + ")";
    fill.style.width = pct.toFixed(1) + "%";
  }

  function renderFiles(arr, dir) {
    var tb = document.getElementById("files-body");
    var pl = document.getElementById("files-path");
    dir = dir || "";
    tb.innerHTML = "";
    pl.textContent = "/data" + (dir ? "/" + dir : "");
    function navTo(d) {
      curDir = d;
      loadFiles();
    }
    function navLink(label, d) {
      var a = document.createElement("a");
      a.href = "#";
      a.textContent = label;
      a.onclick = function (e) {
        if (e) e.preventDefault();
        navTo(d);
        return false;
      };
      return a;
    }
    if (dir) {
      var up = dir.indexOf("/") >= 0 ? dir.slice(0, dir.lastIndexOf("/")) : "";
      var tr = document.createElement("tr");
      tr.innerHTML = "<td></td><td></td><td></td>";
      tr.children[0].appendChild(navLink("../", up));
      tr.children[1].textContent = "-";
      tb.appendChild(tr);
    }
    /* Directories first, files after. */
    arr.forEach(function (x) {
      if (!x.dir) return;
      var tr = document.createElement("tr");
      tr.innerHTML = "<td></td><td></td><td></td>";
      var full = dir ? dir + "/" + x.name : x.name;
      tr.children[0].appendChild(navLink(x.name + "/", full));
      tr.children[1].textContent = "-";
      var bx = mkbtn(t("delete"), "btn-danger", (function (s) { return function () {
        confirmModal(t("del_file_confirm").replace("{s}", s), function () {
          fetch("/api/files/delete?name=" + encodeURIComponent(s), { method: "POST" })
            .then(function () {
              loadFiles();
              get("/api/sys", renderFilesSys);
            });
        }, "delete");
      }; })(full));
      tr.children[2].appendChild(bx);
      tb.appendChild(tr);
    });
    arr.forEach(function (x) {
      if (x.dir) return;
      var tr = document.createElement("tr");
      tr.innerHTML = "<td></td><td></td><td></td>";
      var full = dir ? dir + "/" + x.name : x.name;
      tr.children[0].textContent = x.name;
      tr.children[1].textContent = x.size >= 1024 ?
        (x.size / 1024).toFixed(2) + " KB" : x.size + " B";
      var bd = mkbtn(t("download"), "", (function (s) { return function () {
        window.location = "/api/files/download?name=" + encodeURIComponent(s);
      }; })(full));
      var bx2 = mkbtn(t("delete"), "btn-danger", (function (s) { return function () {
        confirmModal(t("del_file_confirm").replace("{s}", s), function () {
          fetch("/api/files/delete?name=" + encodeURIComponent(s), { method: "POST" })
            .then(function () {
              loadFiles();
              get("/api/sys", renderFilesSys);
            });
        }, "delete");
      }; })(full));
      tr.children[2].appendChild(bd);
      tr.children[2].appendChild(document.createTextNode(" "));
      tr.children[2].appendChild(bx2);
      tb.appendChild(tr);
    });
  }

  document.getElementById("file-upload").onclick = function () {
    var f = document.getElementById("file-pick").files[0];
    if (!f) return;
    var up = curDir ? curDir + "/" + f.name : f.name;
    fetch("/api/files/upload?name=" + encodeURIComponent(up), {
      method: "POST",
      body: f
    }).then(function (r) { return r.json(); }).then(function (j) {
      if (j.ok) toast("toast_uploaded", true);
      else toastErr(j);
      loadFiles();
      get("/api/sys", renderFilesSys);
    }).catch(function () {
      toastErr();
    });
  };

  document.getElementById("dir-make").onclick = function () {
    var d = document.getElementById("dir-pick");
    var nm = d.value.replace(/^\s+|\s+$/g, "");
    if (!nm) return;
    var full = curDir ? curDir + "/" + nm : nm;
    fetch("/api/files/mkdir?name=" + encodeURIComponent(full), { method: "POST" })
      .then(function (r) { return r.json(); }).then(function (j) {
        if (j.ok) {
          d.value = "";
          toast("toast_uploaded", true);
        } else toastErr(j);
        loadFiles();
      }).catch(function () {
        toastErr();
      });
  };
