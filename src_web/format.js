/* Reusable formatters: sizes, durations, MAC, partitions. */
  function fmtKB(bytes) { return (bytes / 1024).toFixed(0) + " KB"; }

  function fmtTime(s) {
    s = Math.floor(s);
    var h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60);
    return h + "h " + m + "m " + (s % 60) + "s";
  }

  function fmtMiB(bytes) { return (bytes / 1048576).toFixed(2) + " MiB"; }

  function fmtDur(s) {
    s = Math.floor(s);
    var d = Math.floor(s / 86400), h = Math.floor((s % 86400) / 3600),
      m = Math.floor((s % 3600) / 60);
    return (d ? d + "d " : "") + h + "h " + m + "m " + (s % 60) + "s";
  }

  function fmtMB(bytes) { return (bytes / 1048576).toFixed(2) + " MB"; }

  function partType(p) {
    if (p.type === 0) return p.subtype === 0 ? "factory" : "ota_" + (p.subtype - 16);
    if (p.type === 1) {
      if (p.subtype === 1) return "phy";
      if (p.subtype === 2) return "nvs";
      if (p.subtype === 131) return "littlefs";
      return "data";
    }
    return "?";
  }

  function partColor(p) {
    if (p.type === 0) return p.subtype === 0 ? "#06c" : "#080";
    if (p.subtype === 2) return "#888";
    if (p.subtype === 131) return "#a60";
    return "#555";
  }

  function fmtPkts(v) { return Number(v || 0).toLocaleString(lang()) + " " + t("pkts"); }

  function dispMac(m) { return (!m || m === "00:00:00:00:00:00") ? "-" : m; }

  function macOk(s) { return /^[0-9a-fA-F]{2}(:[0-9a-fA-F]{2}){5}$/.test(s || ""); }
