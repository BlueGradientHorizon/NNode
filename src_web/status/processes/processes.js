/* Page logic, see manifest.json for bundle order. */
  function renderTasks(arr) {
    var tb = document.getElementById("tasks-body");
    tb.innerHTML = "";
    arr.forEach(function (x) {
      var tr = document.createElement("tr");
      var cpu = x.cpu < 0 ? "-" : x.cpu.toFixed(1);
      tr.innerHTML = "<td></td><td></td><td></td><td></td><td></td><td></td>";
      var td = tr.children;
      td[0].textContent = x.name;
      td[1].textContent = x.state;
      td[2].textContent = x.prio;
      td[3].textContent = fmtKB(x.stack_hwm);
      td[4].textContent = cpu;
      td[5].textContent = x.core;
      tb.appendChild(tr);
    });
  }
