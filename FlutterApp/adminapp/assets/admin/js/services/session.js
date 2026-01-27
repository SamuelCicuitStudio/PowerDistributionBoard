const STORAGE_KEYS = {
  token: "pb.session.token",
  role: "pb.session.role",
  base: "pb.session.base",
};

const textEncoder = new TextEncoder();
const textDecoder = new TextDecoder();
const CBOR_BREAK = Symbol("cbor_break");

let cachedToken;
let cachedRole;
let cachedBase = initBase();

function initSessionFromQuery() {
  try {
    const url = new URL(window.location.href);
    const params = url.searchParams;
    const token = params.get("token");
    const role = params.get("role");
    let changed = false;

    if (token) {
      setToken(token);
      params.delete("token");
      changed = true;
    }
    if (role) {
      setRole(role);
      params.delete("role");
      changed = true;
    }

    if (changed) {
      const next = url.pathname + (params.toString() ? `?${params}` : "") + url.hash;
      window.history.replaceState({}, "", next);
    }
  } catch (e) {
    // ignore malformed URL scenarios
  }
}

initSessionFromQuery();

function getStorage() {
  try {
    return window.localStorage;
  } catch (e) {
    return null;
  }
}

function getStored(key) {
  const store = getStorage();
  if (!store) return "";
  return store.getItem(key) || "";
}

function setStored(key, value) {
  const store = getStorage();
  if (!store) return;
  if (!value) {
    store.removeItem(key);
  } else {
    store.setItem(key, value);
  }
}

function normalizeBase(input) {
  const raw = String(input || "").trim();
  if (!raw) return "";
  const withScheme = /^https?:\/\//i.test(raw) ? raw : `http://${raw}`;
  try {
    const url = new URL(withScheme);
    return url.origin;
  } catch (e) {
    return "";
  }
}

function initBase() {
  const params = new URLSearchParams(window.location.search);
  const queryBase = params.get("base");
  if (queryBase) {
    const normalized = normalizeBase(queryBase);
    if (normalized) {
      setStored(STORAGE_KEYS.base, normalized);
      return normalized;
    }
  }
  const origin = window.location.origin;
  if (origin && origin !== "null") {
    const host = window.location.hostname.toLowerCase();
    const isLoopback =
      host === "localhost" ||
      host === "127.0.0.1" ||
      host === "::1" ||
      host === "0.0.0.0" ||
      host === "appassets.powerboard.local";
    if (!isLoopback) {
      setStored(STORAGE_KEYS.base, origin);
      return origin;
    }
  }
  const stored = getStored(STORAGE_KEYS.base);
  if (stored) {
    const normalizedStored = normalizeBase(stored);
    if (normalizedStored && normalizedStored.includes("powerboard.local")) {
      return normalizedStored;
    }
  }
  const fallback = "http://powerboard.local";
  setStored(STORAGE_KEYS.base, fallback);
  return fallback;
}

function getBase() {
  if (cachedBase === undefined) cachedBase = initBase();
  return cachedBase || "";
}

function setBase(value) {
  const normalized = normalizeBase(value);
  cachedBase = normalized || "";
  setStored(STORAGE_KEYS.base, cachedBase);
  return cachedBase;
}

function getToken() {
  if (cachedToken === undefined) cachedToken = getStored(STORAGE_KEYS.token);
  return cachedToken || "";
}

function setToken(token) {
  cachedToken = token ? String(token) : "";
  setStored(STORAGE_KEYS.token, cachedToken);
}

function clearToken() {
  cachedToken = "";
  cachedRole = "";
  setStored(STORAGE_KEYS.token, "");
  setStored(STORAGE_KEYS.role, "");
}

function getRole() {
  if (cachedRole === undefined) cachedRole = getStored(STORAGE_KEYS.role);
  return cachedRole || "";
}

function setRole(role) {
  cachedRole = role ? String(role) : "";
  setStored(STORAGE_KEYS.role, cachedRole);
}

function apiUrl(path) {
  if (!path) return getBase();
  if (/^https?:\/\//i.test(path)) return path;
  const base = getBase();
  if (!base) return path;
  const trimmed = base.replace(/\/+$/, "");
  const suffix = path.startsWith("/") ? path : `/${path}`;
  return `${trimmed}${suffix}`;
}

function cborHeaders(options = {}) {
  const headers = {};
  if (options.accept !== false) headers.Accept = "application/cbor";
  if (options.body !== false) headers["Content-Type"] = "application/cbor";
  if (options.auth !== false) {
    const token = getToken();
    if (token) headers["X-Session-Token"] = token;
  }
  return headers;
}

function fetchWithSession(path, options = {}) {
  const headers = new Headers(options.headers || {});
  if (!headers.has("X-Session-Token")) {
    const token = getToken();
    if (token) headers.set("X-Session-Token", token);
  }
  return fetch(apiUrl(path), { ...options, headers });
}

async function readCbor(response) {
  if (!response) return null;
  const contentType = response.headers?.get("content-type") || "";
  if (contentType.includes("application/json")) {
    try {
      return await response.json();
    } catch (e) {
      return null;
    }
  }
  const buffer = await response.arrayBuffer();
  if (!buffer || buffer.byteLength === 0) return null;
  try {
    return decodeCbor(new Uint8Array(buffer));
  } catch (e) {
    console.warn("CBOR decode failed:", e);
    return null;
  }
}

async function disconnectSession() {
  const body = encodeCbor({ action: "disconnect" });
  const headers = cborHeaders();
  return fetchWithSession("/disconnect", {
    method: "POST",
    headers,
    body,
  });
}

function buildLoginUrl() {
  const url = new URL("login.html", window.location.href);
  const base = getBase();
  if (base) url.searchParams.set("base", base);
  return url.toString();
}

function requireSession() {
  if (getToken()) return true;
  window.location.href = buildLoginUrl();
  return false;
}

function encodeCbor(value) {
  const bytes = [];
  encodeValue(value, bytes);
  return new Uint8Array(bytes);
}

function encodeValue(value, bytes) {
  if (value === null) {
    bytes.push(0xf6);
    return;
  }
  if (value === undefined) {
    bytes.push(0xf7);
    return;
  }
  if (typeof value === "boolean") {
    bytes.push(value ? 0xf5 : 0xf4);
    return;
  }
  if (typeof value === "number") {
    if (!Number.isFinite(value)) {
      bytes.push(0xf6);
      return;
    }
    if (Number.isSafeInteger(value)) {
      if (value >= 0) {
        encodeTypeAndLength(0, value, bytes);
      } else {
        encodeTypeAndLength(1, -1 - value, bytes);
      }
      return;
    }
    bytes.push(0xfb);
    const buffer = new ArrayBuffer(8);
    new DataView(buffer).setFloat64(0, value, false);
    bytes.push(...new Uint8Array(buffer));
    return;
  }
  if (typeof value === "string") {
    const data = textEncoder.encode(value);
    encodeTypeAndLength(3, data.length, bytes);
    bytes.push(...data);
    return;
  }
  if (value instanceof Uint8Array) {
    encodeTypeAndLength(2, value.length, bytes);
    bytes.push(...value);
    return;
  }
  if (Array.isArray(value)) {
    encodeTypeAndLength(4, value.length, bytes);
    value.forEach((item) => encodeValue(item, bytes));
    return;
  }
  if (typeof value === "object") {
    const keys = Object.keys(value).filter(
      (key) => value[key] !== undefined,
    );
    encodeTypeAndLength(5, keys.length, bytes);
    keys.forEach((key) => {
      encodeValue(key, bytes);
      encodeValue(value[key], bytes);
    });
    return;
  }
  bytes.push(0xf6);
}

function encodeTypeAndLength(major, length, bytes) {
  if (length < 24) {
    bytes.push((major << 5) | length);
    return;
  }
  if (length < 0x100) {
    bytes.push((major << 5) | 24, length);
    return;
  }
  if (length < 0x10000) {
    bytes.push((major << 5) | 25, (length >> 8) & 0xff, length & 0xff);
    return;
  }
  if (length < 0x100000000) {
    bytes.push(
      (major << 5) | 26,
      (length >> 24) & 0xff,
      (length >> 16) & 0xff,
      (length >> 8) & 0xff,
      length & 0xff,
    );
    return;
  }
  bytes.push((major << 5) | 27);
  const hi = Math.floor(length / 0x100000000);
  const lo = length >>> 0;
  bytes.push(
    (hi >> 24) & 0xff,
    (hi >> 16) & 0xff,
    (hi >> 8) & 0xff,
    hi & 0xff,
    (lo >> 24) & 0xff,
    (lo >> 16) & 0xff,
    (lo >> 8) & 0xff,
    lo & 0xff,
  );
}

function decodeCbor(bytes) {
  const reader = new CborReader(bytes);
  const value = reader.read();
  return value === CBOR_BREAK ? null : value;
}

class CborReader {
  constructor(bytes) {
    this.bytes = bytes;
    this.view = new DataView(
      bytes.buffer,
      bytes.byteOffset,
      bytes.byteLength,
    );
    this.offset = 0;
  }

  read() {
    if (this.offset >= this.bytes.length) {
      throw new Error("CBOR: unexpected end");
    }
    const initial = this.bytes[this.offset++];
    if (initial === 0xff) return CBOR_BREAK;
    const major = initial >> 5;
    const addl = initial & 0x1f;
    const length = this.readLength(addl);

    switch (major) {
      case 0:
        return length;
      case 1:
        return -1 - length;
      case 2:
        return this.readBytes(length);
      case 3:
        return this.readText(length);
      case 4:
        return this.readArray(length);
      case 5:
        return this.readMap(length);
      case 6:
        return this.read();
      case 7:
        return this.readSimple(addl, length);
      default:
        throw new Error("CBOR: unsupported type");
    }
  }

  readLength(addl) {
    if (addl < 24) return addl;
    if (addl === 24) return this.readUint(1);
    if (addl === 25) return this.readUint(2);
    if (addl === 26) return this.readUint(4);
    if (addl === 27) return this.readUint(8);
    if (addl === 31) return null;
    throw new Error("CBOR: invalid length");
  }

  readUint(bytes) {
    let value = 0;
    for (let i = 0; i < bytes; i += 1) {
      value = value * 256 + this.bytes[this.offset++];
    }
    return value;
  }

  readBytes(length) {
    if (length === null) {
      const chunks = [];
      while (true) {
        const item = this.read();
        if (item === CBOR_BREAK) break;
        chunks.push(item);
      }
      const total = chunks.reduce((sum, chunk) => sum + chunk.length, 0);
      const out = new Uint8Array(total);
      let offset = 0;
      chunks.forEach((chunk) => {
        out.set(chunk, offset);
        offset += chunk.length;
      });
      return out;
    }
    const start = this.offset;
    const end = start + length;
    this.offset = end;
    return this.bytes.slice(start, end);
  }

  readText(length) {
    if (length === null) {
      let value = "";
      while (true) {
        const part = this.read();
        if (part === CBOR_BREAK) break;
        value += typeof part === "string" ? part : textDecoder.decode(part);
      }
      return value;
    }
    const bytes = this.readBytes(length);
    return textDecoder.decode(bytes);
  }

  readArray(length) {
    const result = [];
    if (length === null) {
      while (true) {
        const item = this.read();
        if (item === CBOR_BREAK) break;
        result.push(item);
      }
      return result;
    }
    for (let i = 0; i < length; i += 1) {
      result.push(this.read());
    }
    return result;
  }

  readMap(length) {
    const result = {};
    if (length === null) {
      while (true) {
        const key = this.read();
        if (key === CBOR_BREAK) break;
        const value = this.read();
        if (value === CBOR_BREAK) break;
        result[key] = value;
      }
      return result;
    }
    for (let i = 0; i < length; i += 1) {
      const key = this.read();
      const value = this.read();
      result[key] = value;
    }
    return result;
  }

  readSimple(addl, length) {
    switch (addl) {
      case 20:
        return false;
      case 21:
        return true;
      case 22:
        return null;
      case 23:
        return undefined;
      case 24:
        return length;
      case 25:
        return this.readFloat16();
      case 26:
        return this.readFloat32();
      case 27:
        return this.readFloat64();
      case 31:
        return CBOR_BREAK;
      default:
        return length;
    }
  }

  readFloat16() {
    const half = this.readUint(2);
    const sign = (half & 0x8000) ? -1 : 1;
    const exponent = (half >> 10) & 0x1f;
    const fraction = half & 0x3ff;
    if (exponent === 0) {
      return sign * Math.pow(2, -14) * (fraction / 1024);
    }
    if (exponent === 31) {
      return fraction === 0 ? sign * Infinity : NaN;
    }
    return sign * Math.pow(2, exponent - 15) * (1 + fraction / 1024);
  }

  readFloat32() {
    const value = this.view.getFloat32(this.offset, false);
    this.offset += 4;
    return value;
  }

  readFloat64() {
    const value = this.view.getFloat64(this.offset, false);
    this.offset += 8;
    return value;
  }
}

window.pbGetBase = getBase;
window.pbSetBase = setBase;
window.pbApiUrl = apiUrl;
window.pbGetToken = getToken;
window.pbSetToken = setToken;
window.pbClearToken = clearToken;
window.pbGetRole = getRole;
window.pbSetRole = setRole;
window.pbCborHeaders = cborHeaders;
window.pbEncodeCbor = encodeCbor;
window.pbReadCbor = readCbor;
window.pbFetch = fetchWithSession;
window.pbDisconnect = disconnectSession;
window.pbRequireSession = requireSession;

export {
  apiUrl,
  cborHeaders,
  clearToken,
  disconnectSession,
  encodeCbor,
  fetchWithSession,
  getBase,
  getRole,
  getToken,
  readCbor,
  requireSession,
  setBase,
  setRole,
  setToken,
};
