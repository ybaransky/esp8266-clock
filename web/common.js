// Shared helpers for all clock pages. Loaded synchronously in <head> so the
// error beacons are installed before any page script runs. Served gzipped
// with an immutable cache header; tools/build_web.py stamps a content hash
// into each page's reference, so editing this file busts the cache.

function $(id) { return document.getElementById(id); }

// Beacon browser-side failures to the device so they land in the serial log
// next to the server-side request lines.
function clog(data) {
  data.page = location.pathname;
  try { navigator.sendBeacon('/api/client-log', JSON.stringify(data)); } catch (e) {}
}

window.onerror = function (msg, src, line) {
  clog({ err: String(msg).slice(0, 120), line: line || 0 });
};

// Report failed /api fetches to the #st status line and the device log.
(function () {
  var origFetch = window.fetch;
  function fail(url, reason) {
    if (url !== '/api/client-log') clog({ fetch: url, err: String(reason).slice(0, 80) });
    setTimeout(function () {
      var st = document.getElementById('st');
      if (st) st.textContent = 'API failed: ' + url + ' (' + reason + ')';
    }, 0);
  }
  window.fetch = function (input, options) {
    var url = typeof input === 'string' ? input : input.url;
    return origFetch.call(this, input, options).then(function (r) {
      if (url.indexOf('/api/') === 0 && !r.ok) fail(url, 'HTTP ' + r.status);
      return r;
    }, function (e) {
      // Deliberate client-side aborts (poll timeouts) are not device failures.
      if (url.indexOf('/api/') === 0 && !(e && e.name === 'AbortError')) {
        fail(url, e && e.message ? e.message : 'network error');
      }
      throw e;
    });
  };
})();

// Beacon abnormally slow page loads with a phase breakdown (connect,
// time-to-first-byte, body download, total).
addEventListener('load', function () {
  var nav = performance.getEntriesByType('navigation')[0];
  if (nav && nav.loadEventStart > 3000) {
    clog({
      slow: 1,
      conn: Math.round(nav.connectEnd - nav.connectStart),
      ttfb: Math.round(nav.responseStart - nav.requestStart),
      dl: Math.round(nav.responseEnd - nav.responseStart),
      load: Math.round(nav.loadEventStart)
    });
  }
});

// Page-load time in the bottom corner, next to the footer links.
addEventListener('load', function () {
  setTimeout(function () {
    var span = document.createElement('span');
    var links = document.querySelectorAll('a');
    span.textContent = (performance.now() / 1000).toFixed(2);
    span.style.cssText = 'float:right;color:#444;font:inherit';
    var parent = location.pathname !== '/' && links.length
        ? links[links.length - 1].parentElement
        : document.body.appendChild(document.createElement('div'));
    parent.appendChild(span);
  }, 0);
});

var _statusTimer = null;
function setStatus(msg, clearMs) {
  if (_statusTimer) { clearTimeout(_statusTimer); _statusTimer = null; }
  var st = $('st');
  if (st) st.textContent = msg || '';
  if (clearMs) {
    _statusTimer = setTimeout(function () { _statusTimer = null; setStatus(''); }, clearMs);
  }
}

// fetch that bypasses caches, rejects on HTTP errors, and parses JSON.
function api(url, options) {
  return fetch(url, Object.assign({ cache: 'no-store' }, options || {})).then(function (r) {
    if (!r.ok) throw new Error(url + ' HTTP ' + r.status);
    return r.json();
  });
}

function apiPost(url, body) {
  return api(url, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body)
  });
}

// fetch that parses the body even on HTTP errors, so server-provided
// {"error": ...} messages reach the caller instead of a bare status code.
function jsonFetch(url, options) {
  return fetch(url, options).then(function (r) {
    return r.json().then(function (d) {
      if (!r.ok && !d.error) d.error = 'HTTP ' + r.status;
      return d;
    });
  });
}

function validZip(value) { return /^[0-9]{5}$/.test(value); }

// Tell the device when a config value could not be represented in a form
// field, so silent browser-side value rejection shows up in the serial log.
function reportFieldMismatch(page, field, configValue, acceptedValue, reason) {
  fetch('/api/field-mismatch', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      page: page, field: field, configValue: String(configValue),
      acceptedValue: String(acceptedValue), reason: reason
    })
  }).catch(function () {});
}

// Browsers normalize datetime-local values (seconds may be dropped); compare
// canonical forms so normalization is not misreported as rejection.
function canonicalFieldValue(el, value) {
  var text = value === undefined || value === null ? '' : String(value);
  if (el.type === 'datetime-local') {
    var m = text.match(/^(\d{4}-\d{2}-\d{2})T(\d{2}:\d{2})(?::(\d{2}))?/);
    return m ? m[1] + 'T' + m[2] + ':' + (m[3] || '00') : text;
  }
  return text;
}

function setFieldFromConfig(page, id, field, configValue, fieldValue) {
  var el = $(id);
  var wanted = fieldValue === undefined || fieldValue === null ? '' : String(fieldValue);
  el.value = wanted;
  var rejected = canonicalFieldValue(el, el.value) !== canonicalFieldValue(el, wanted);
  var invalid = el.checkValidity && !el.checkValidity();
  var conversionLost = configValue !== undefined && configValue !== null &&
      String(configValue) !== '' && wanted === '';
  if (rejected || invalid || conversionLost) {
    reportFieldMismatch(page, field, configValue, el.value,
        invalid ? (el.validationMessage || 'invalid value')
                : (conversionLost ? 'conversion produced empty value'
                                  : 'browser rejected value'));
  }
}
