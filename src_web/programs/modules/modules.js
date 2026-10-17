/* Page logic, see manifest.json for bundle order. */
  function renderModules(arr, inst) {
    var tb = document.getElementById("modules-body");
    tb.innerHTML = "";
    lastModFiles = arr || [];
    arr.forEach(function (x) {
      if (!x.is_elf) return;
      var tr = document.createElement("tr");
      tr.innerHTML = "<td></td><td></td><td></td><td></td><td></td>";
      tr.children[0].textContent = x.name;
      tr.children[1].textContent = fmtKB(x.size);
      tr.children[2].textContent = "-";
      tr.children[3].textContent = "-";
      /* File rows only start new instances; live ones hang below. */
      var br = mkbtn(t("run"), "btn-primary", (function (s) { return function () {
        fetch("/api/elf/run?name=" + encodeURIComponent(s), { method: "POST" })
          .then(function (r) { return r.json(); }).then(function (j) {
            if (j.ok) toast("toast_started", true);
            else toastErr(j);
            loadFiles();
            loadModules();
          }).catch(function () {
            toastErr();
          });
      }; })(x.name), false);
      tr.children[4].appendChild(br);
      tr.children[4].appendChild(document.createTextNode(" "));
      /* Settings opens only with a schema; otherwise the button rests. */
      var bs = mkbtn(t("settings"), "", (function (s) { return function () {
        openModConfig(s);
      }; })(x.name), !x.has_schema);
      tr.children[4].appendChild(bs);
      tb.appendChild(tr);
      (inst || []).forEach(function (o) {
        if (o.name !== x.name) return;
        var ir = document.createElement("tr");
        ir.innerHTML = "<td></td><td></td><td></td><td></td><td></td>";
        ir.children[0].textContent = "-";
        ir.children[1].textContent = "-";
        ir.children[2].textContent = o.id;
        ir.children[3].textContent = o.state === "exited" ? t("st_exited") : t("st_running");
        var bs = mkbtn(t("stop"), "", (function (id) { return function () {
          fetch("/api/elf/stop?id=" + id, { method: "POST" })
            .then(function (r) { return r.json(); }).then(function (j) {
              if (j.ok) toast("toast_stopped", true);
              else toastErr(j);
              loadFiles();
              loadModules();
              get("/api/tasks", renderTasks);
            }).catch(function () {
              toastErr();
            });
        }; })(o.id), false);
        var bk = mkbtn(t("kill"), "btn-danger", (function (o) { return function () {
          confirmModal(t("kill_confirm").replace("{id}", o.id).replace("{s}", o.name), function () {
            fetch("/api/elf/kill?id=" + o.id, { method: "POST" })
              .then(function (r) { return r.json(); }).then(function (j) {
                if (j.ok) toast("toast_killed", true);
                else toastErr(j);
                loadFiles();
                loadModules();
                get("/api/tasks", renderTasks);
              }).catch(function () {
                toastErr();
              });
          }, "kill");
        }; })(o), false);
        ir.children[4].appendChild(bs);
        ir.children[4].appendChild(document.createTextNode(" "));
        ir.children[4].appendChild(bk);
        tb.appendChild(ir);
      });
    });
  }
