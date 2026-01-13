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
