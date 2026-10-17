/* Page logic, see manifest.json for bundle order. */
  var prngS = [0, 0, 0, 0];

  function prngMul(a, b) {
    var r = [0, 0, 0, 0, 0, 0, 0, 0], i, j, k, t;
    for (j = 0; j < 4; j++) {
      k = 0;
      for (i = 0; i < 4; i++) {
        t = a[i] * b[j] + r[i + j] + k;
        r[i + j] = t & 0xffff;
        k = t >>> 16;
      }
      r[j + 4] = k;
    }
    r.length = 4;
    return r;
  }

  function prngAdd(a, n) {
    var r = [0, 0, 0, 0], k = n, i, t;
    for (i = 0; i < 4; i++) {
      t = a[i] + k;
      r[i] = t & 0xffff;
      k = t >>> 16;
    }
    return r;
  }

  function prngShr(a, n) {
    var r = [a[0], a[1], a[2], a[3], 0], i = 4, k = 0, j, s;
    for (; n > 16; n -= 16, i--) {
      for (j = 0; j < 4; j++) r[j] = r[j + 1];
    }
    for (; i > 0; i--) {
      s = r[i - 1];
      r[i - 1] = (s >>> n) | k;
      k = ((s & ((1 << n) - 1)) << (16 - n));
    }
    r.length = 4;
    return r;
  }

  function prngSeed(n) {
    n = (n - 1) | 0;
    prngS[0] = n & 0xffff;
    prngS[1] = n >>> 16;
    prngS[2] = 0;
    prngS[3] = 0;
  }

  function prngInt() {
    prngS = prngMul(prngS, [0x7f2d, 0x4c95, 0xf42d, 0x5851]);
    prngS = prngAdd(prngS, 1);
    var r = prngShr(prngS, 33);
    return (r[1] << 16) | r[0];
  }

  function prngGetU(u) {
    var r = (prngInt() % 0x7fffffff) / 0x7fffffff;
    return Math.floor(r * u) + 1;
  }

  function prngGetF() {
    return (prngInt() % 0x7fffffff) / 0x7fffffff;
  }

  function prngS8(bytes, off) {
    var n = bytes[off];
    return n > 0x7f ? (n - 256) >>> 0 : n;
  }

  function prngU16(bytes, off) {
    return ((bytes[off + 1] << 8) + bytes[off]) >>> 0;
  }

  function prngSfh(s) {
    var bytes = [], i, ch, next;
    for (i = 0; i < s.length; i++) {
      ch = s.charCodeAt(i);
      if (ch >= 0xd800 && ch <= 0xdbff && i + 1 < s.length) {
        next = s.charCodeAt(i + 1);
        if (next >= 0xdc00 && next <= 0xdfff) {
          ch = 0x10000 + ((ch - 0xd800) << 10) + (next - 0xdc00);
          i++;
        }
      }
      if (ch <= 0x7f) {
        bytes.push(ch);
      } else if (ch <= 0x7ff) {
        bytes.push(((ch >>> 6) & 0x1f) | 0xc0, (ch & 0x3f) | 0x80);
      } else if (ch <= 0xffff) {
        bytes.push(((ch >>> 12) & 0xf) | 0xe0,
          ((ch >>> 6) & 0x3f) | 0x80, (ch & 0x3f) | 0x80);
      } else if (ch <= 0x10ffff) {
        bytes.push(((ch >>> 18) & 0x7) | 0xf0,
          ((ch >>> 12) & 0x3f) | 0x80, ((ch >> 6) & 0x3f) | 0x80,
          (ch & 0x3f) | 0x80);
      }
    }
    if (!bytes.length) return null;
    var hash = bytes.length >>> 0, len = bytes.length >>> 2, off = 0, tmp;
    while (len--) {
      hash += prngU16(bytes, off);
      tmp = ((prngU16(bytes, off + 2) << 11) ^ hash) >>> 0;
      hash = ((hash << 16) ^ tmp) >>> 0;
      hash += hash >>> 11;
      off += 4;
    }
    switch ((bytes.length & 3) >>> 0) {
    case 3:
      hash += prngU16(bytes, off);
      hash = (hash ^ (hash << 16)) >>> 0;
      hash = (hash ^ (prngS8(bytes, off + 2) << 18)) >>> 0;
      hash += hash >>> 11;
      break;
    case 2:
      hash += prngU16(bytes, off);
      hash = (hash ^ (hash << 11)) >>> 0;
      hash += hash >>> 17;
      break;
    case 1:
      hash += prngS8(bytes, off);
      hash = (hash ^ (hash << 10)) >>> 0;
      hash += hash >>> 1;
      break;
    }
    hash = (hash ^ (hash << 3)) >>> 0;
    hash += hash >>> 5;
    hash = (hash ^ (hash << 4)) >>> 0;
    hash += hash >>> 17;
    hash = (hash ^ (hash << 25)) >>> 0;
    hash += hash >>> 6;
    return (0x100000000 + hash).toString(16).slice(1);
  }

  function prngHex2(v) {
    var h = v.toString(16);
    return h.length < 2 ? "0" + h : h;
  }

  function deriveColor(str) {
    prngSeed(parseInt(prngSfh(str), 16));
    var r = prngGetU(128), g = prngGetU(128), min = 0, max = 128, b;
    if (r + g < 128) min = 128 - r - g;
    else max = 255 - r - g;
    b = min + Math.floor(prngGetF() * (max - min));
    return "#" + prngHex2(0xff - r) + prngHex2(0xff - g) + prngHex2(0xff - b);
  }

  function chanColor(x) {
    /* iwinfo in OpenWrt reports BSSIDs in uppercase, hash the same way */
    return deriveColor((x.bssid || "").toUpperCase());
  }
