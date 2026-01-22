(function () {
  if (window.__POWERBOARD_BASE__) {
    return;
  }
  window.__POWERBOARD_BASE__ = true;

  function normalizeBase(input) {
    var url = (input || "").toString().trim();
    if (!url) {
      return "http://powerboard.local";
    }
    if (!/^https?:\/\//i.test(url)) {
      url = "http://" + url;
    }
    if (/^https?:\/\/192\.168\.4\.1(?=\/|$)/i.test(url)) {
      url = "http://powerboard.local";
    }
    while (url.endsWith("/")) {
      url = url.slice(0, -1);
    }
    return url;
  }

  function readQueryBase() {
    try {
      var params = new URLSearchParams(window.location.search || "");
      return params.get("base") || "";
    } catch (e) {
      return "";
    }
  }

  function getStoredBase() {
    try {
      return window.localStorage.getItem("pbBaseUrl") || "";
    } catch (e) {
      return "";
    }
  }

  function setStoredBase(base) {
    try {
      window.localStorage.setItem("pbBaseUrl", base);
    } catch (e) {}
  }

  var base = normalizeBase(readQueryBase() || getStoredBase() || "");
  setStoredBase(base);
  window.POWERBOARD_BASE_URL = base;

  window.pbGetBaseUrl = function () {
    return base;
  };

  window.pbSetBaseUrl = function (next) {
    base = normalizeBase(next);
    setStoredBase(base);
    try {
      var url = new URL(window.location.href);
      url.searchParams.set("base", base);
      window.location.href = url.toString();
    } catch (e) {
      window.location.href = "login.html?base=" + encodeURIComponent(base);
    }
  };

  window.pbLoginUrl = function () {
    var url = "login.html";
    if (base) {
      url += "?base=" + encodeURIComponent(base);
    }
    return url;
  };

  window.pbSetToken = function (token) {
    try {
      if (token) {
        window.localStorage.setItem("pbToken", token);
      } else {
        window.localStorage.removeItem("pbToken");
      }
    } catch (e) {}
  };

  window.pbGetToken = function () {
    try {
      return window.localStorage.getItem("pbToken") || "";
    } catch (e) {
      return "";
    }
  };

  window.pbClearToken = function () {
    try {
      window.localStorage.removeItem("pbToken");
    } catch (e) {}
  };

  var textEncoder = window.TextEncoder ? new TextEncoder() : null;
  var textDecoder = window.TextDecoder ? new TextDecoder("utf-8") : null;

  function utf8Encode(text) {
    var str = text == null ? "" : String(text);
    if (textEncoder) {
      return textEncoder.encode(str);
    }
    var encoded = encodeURIComponent(str);
    var bytes = [];
    for (var i = 0; i < encoded.length; i++) {
      var ch = encoded.charAt(i);
      if (ch === "%") {
        bytes.push(parseInt(encoded.substr(i + 1, 2), 16));
        i += 2;
      } else {
        bytes.push(ch.charCodeAt(0));
      }
    }
    return new Uint8Array(bytes);
  }

  function utf8Decode(bytes) {
    if (!bytes || !bytes.length) return "";
    if (textDecoder) {
      return textDecoder.decode(bytes);
    }
    var str = "";
    for (var i = 0; i < bytes.length; i++) {
      str += "%" + ("0" + bytes[i].toString(16)).slice(-2);
    }
    try {
      return decodeURIComponent(str);
    } catch (e) {
      return "";
    }
  }

  function encodeUint(major, value, out) {
    if (value < 24) {
      out.push((major << 5) | value);
      return;
    }
    if (value < 256) {
      out.push((major << 5) | 24, value & 0xff);
      return;
    }
    if (value < 65536) {
      out.push((major << 5) | 25, (value >> 8) & 0xff, value & 0xff);
      return;
    }
    if (value < 4294967296) {
      out.push(
        (major << 5) | 26,
        (value >> 24) & 0xff,
        (value >> 16) & 0xff,
        (value >> 8) & 0xff,
        value & 0xff
      );
      return;
    }
    var hi = Math.floor(value / 4294967296);
    var lo = value >>> 0;
    out.push(
      (major << 5) | 27,
      (hi >> 24) & 0xff,
      (hi >> 16) & 0xff,
      (hi >> 8) & 0xff,
      hi & 0xff,
      (lo >> 24) & 0xff,
      (lo >> 16) & 0xff,
      (lo >> 8) & 0xff,
      lo & 0xff
    );
  }

  function encodeFloat64(value, out) {
    var buf = new ArrayBuffer(8);
    var view = new DataView(buf);
    view.setFloat64(0, value, false);
    out.push(0xfb);
    for (var i = 0; i < 8; i++) {
      out.push(view.getUint8(i));
    }
  }

  function encodeCborValue(value, out) {
    if (value === null || value === undefined) {
      out.push(0xf6);
      return;
    }
    var type = typeof value;
    if (type === "boolean") {
      out.push(value ? 0xf5 : 0xf4);
      return;
    }
    if (type === "number") {
      if (Number.isFinite(value) && Math.floor(value) === value) {
        if (value >= 0) {
          encodeUint(0, value, out);
        } else {
          encodeUint(1, -1 - value, out);
        }
        return;
      }
      encodeFloat64(value, out);
      return;
    }
    if (type === "string") {
      var bytes = utf8Encode(value);
      encodeUint(3, bytes.length, out);
      for (var i = 0; i < bytes.length; i++) {
        out.push(bytes[i]);
      }
      return;
    }
    if (value instanceof Uint8Array) {
      encodeUint(2, value.length, out);
      for (var i = 0; i < value.length; i++) {
        out.push(value[i]);
      }
      return;
    }
    if (Array.isArray(value)) {
      encodeUint(4, value.length, out);
      for (var i = 0; i < value.length; i++) {
        encodeCborValue(value[i], out);
      }
      return;
    }
    if (type === "object") {
      var keys = Object.keys(value);
      encodeUint(5, keys.length, out);
      for (var i = 0; i < keys.length; i++) {
        var key = keys[i];
        encodeCborValue(String(key), out);
        encodeCborValue(value[key], out);
      }
      return;
    }
    out.push(0xf6);
  }

  function encodeCbor(value) {
    var out = [];
    encodeCborValue(value, out);
    return new Uint8Array(out);
  }

  function decodeFloat16(bits) {
    var sign = bits & 0x8000;
    var exp = (bits >> 10) & 0x1f;
    var frac = bits & 0x3ff;
    if (exp === 0) {
      if (frac === 0) return sign ? -0 : 0;
      return (sign ? -1 : 1) * Math.pow(2, -14) * (frac / 1024);
    }
    if (exp === 31) {
      return frac ? NaN : sign ? -Infinity : Infinity;
    }
    return (sign ? -1 : 1) * Math.pow(2, exp - 15) * (1 + frac / 1024);
  }

  function decodeCbor(buffer) {
    var view = buffer instanceof DataView ? buffer : new DataView(buffer);
    var len = view.byteLength;
    var idx = 0;

    function readUint(bytes) {
      if (bytes === 1) {
        var v1 = view.getUint8(idx);
        idx += 1;
        return v1;
      }
      if (bytes === 2) {
        var v2 = view.getUint16(idx, false);
        idx += 2;
        return v2;
      }
      if (bytes === 4) {
        var v4 = view.getUint32(idx, false);
        idx += 4;
        return v4;
      }
      if (bytes === 8) {
        var hi = view.getUint32(idx, false);
        var lo = view.getUint32(idx + 4, false);
        idx += 8;
        return hi * 4294967296 + lo;
      }
      return 0;
    }

    function readLen(ai) {
      if (ai < 24) return ai;
      if (ai === 24) return readUint(1);
      if (ai === 25) return readUint(2);
      if (ai === 26) return readUint(4);
      if (ai === 27) return readUint(8);
      return null;
    }

    function readBytes(count) {
      var start = idx;
      idx += count;
      return new Uint8Array(view.buffer, view.byteOffset + start, count);
    }

    function readBreak() {
      if (idx < len && view.getUint8(idx) === 0xff) {
        idx += 1;
        return true;
      }
      return false;
    }

    function decodeItem() {
      if (idx >= len) return null;
      var first = view.getUint8(idx++);
      var major = first >> 5;
      var ai = first & 0x1f;

      if (major === 0) {
        return readLen(ai);
      }
      if (major === 1) {
        return -1 - readLen(ai);
      }
      if (major === 2 || major === 3) {
        var length = readLen(ai);
        if (length === null) {
          var chunks = [];
          var total = 0;
          while (!readBreak()) {
            var part = decodeItem();
            if (part == null) break;
            var bytes = major === 3 ? utf8Encode(part) : part;
            chunks.push(bytes);
            total += bytes.length;
          }
          var merged = new Uint8Array(total);
          var offset = 0;
          for (var c = 0; c < chunks.length; c++) {
            merged.set(chunks[c], offset);
            offset += chunks[c].length;
          }
          return major === 3 ? utf8Decode(merged) : merged;
        }
        var bytes = readBytes(length);
        return major === 3 ? utf8Decode(bytes) : bytes;
      }
      if (major === 4) {
        var arr = [];
        var count = readLen(ai);
        if (count === null) {
          while (!readBreak()) {
            arr.push(decodeItem());
          }
          return arr;
        }
        for (var i = 0; i < count; i++) {
          arr.push(decodeItem());
        }
        return arr;
      }
      if (major === 5) {
        var obj = {};
        var pairs = readLen(ai);
        if (pairs === null) {
          while (!readBreak()) {
            var key = decodeItem();
            var val = decodeItem();
            obj[String(key)] = val;
          }
          return obj;
        }
        for (var j = 0; j < pairs; j++) {
          var k = decodeItem();
          var v = decodeItem();
          obj[String(k)] = v;
        }
        return obj;
      }
      if (major === 6) {
        decodeItem();
        return decodeItem();
      }
      if (major === 7) {
        if (ai === 20) return false;
        if (ai === 21) return true;
        if (ai === 22) return null;
        if (ai === 23) return undefined;
        if (ai === 25) {
          var half = readUint(2);
          return decodeFloat16(half);
        }
        if (ai === 26) {
          var val32 = view.getFloat32(idx, false);
          idx += 4;
          return val32;
        }
        if (ai === 27) {
          var val64 = view.getFloat64(idx, false);
          idx += 8;
          return val64;
        }
        return null;
      }
      return null;
    }

    return decodeItem();
  }

  function base64ToBytes(raw) {
    if (!raw) return new Uint8Array(0);
    var bin = window.atob(raw);
    var out = new Uint8Array(bin.length);
    for (var i = 0; i < bin.length; i++) {
      out[i] = bin.charCodeAt(i) & 0xff;
    }
    return out;
  }

  function bytesToBase64(bytes) {
    if (!bytes || !bytes.length) return "";
    if (!window.btoa) return "";
    var bin = "";
    for (var i = 0; i < bytes.length; i++) {
      bin += String.fromCharCode(bytes[i]);
    }
    return window.btoa(bin);
  }

  function decodeCborFromBase64(raw) {
    var bytes = base64ToBytes(raw);
    if (!bytes || !bytes.length) return null;
    return decodeCbor(bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.length));
  }

  function encodeCborToBase64(value) {
    return bytesToBase64(encodeCbor(value));
  }

  window.pbEncodeCbor = encodeCbor;
  window.pbDecodeCbor = decodeCbor;
  window.pbEncodeCborBase64 = encodeCborToBase64;
  window.pbDecodeCborBase64 = decodeCborFromBase64;
  window.pbReadCbor = async function (res) {
    if (!res) return null;
    var buf = await res.arrayBuffer();
    if (!buf || !buf.byteLength) return null;
    return decodeCbor(buf);
  };
  window.pbCborHeaders = function (extra) {
    var headers = new Headers(extra || {});
    if (!headers.has("Content-Type")) {
      headers.set("Content-Type", "application/cbor");
    }
    if (!headers.has("Accept")) {
      headers.set("Accept", "application/cbor");
    }
    return headers;
  };

  function toAbsoluteUrl(url) {
    if (typeof url === "string" && url.indexOf("/") === 0) {
      return base + url;
    }
    return url;
  }

  function addTokenHeader(init) {
    var token = window.pbGetToken ? window.pbGetToken() : "";
    if (!token) {
      return init || {};
    }
    var headers = new Headers((init && init.headers) ? init.headers : undefined);
    if (!headers.has("X-Session-Token")) {
      headers.set("X-Session-Token", token);
    }
    var next = init ? Object.assign({}, init) : {};
    next.headers = headers;
    return next;
  }

  if (window.fetch) {
    var origFetch = window.fetch.bind(window);
    window.fetch = function (input, init) {
      if (typeof input === "string") {
        var url = toAbsoluteUrl(input);
        return origFetch(url, addTokenHeader(init));
      }
      if (input && input.url) {
        var reqUrl = toAbsoluteUrl(input.url);
        if (reqUrl !== input.url) {
          try {
            input = new Request(reqUrl, input);
          } catch (e) {}
        }
        return origFetch(input, addTokenHeader(init));
      }
      return origFetch(input, addTokenHeader(init));
    };
  }

  if (window.EventSource) {
    var OrigEventSource = window.EventSource;
    window.EventSource = function (url, config) {
      var nextUrl = toAbsoluteUrl(url);
      return new OrigEventSource(nextUrl, config);
    };
    window.EventSource.prototype = OrigEventSource.prototype;
  }

  if (navigator && navigator.sendBeacon) {
    var origBeacon = navigator.sendBeacon.bind(navigator);
    navigator.sendBeacon = function (url, data) {
      var nextUrl = toAbsoluteUrl(url);
      return origBeacon(nextUrl, data);
    };
  }
})();
