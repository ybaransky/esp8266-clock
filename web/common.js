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
    var st = document.getElementById('st');
    var previous = st ? st.textContent : null;
    setTimeout(function () {
      // Preserve a more specific error installed by the request's own handler.
      if (st && st.textContent === previous) {
        st.textContent = 'API failed: ' + url + ' (' + reason + ')';
      }
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

// -- Timezone picker ----------------------------------------------------------
// For pages that also load /tz.js, which defines TZ_ZONES = [[name, rule]...]:
// every IANA zone with its POSIX rule. The device stores the name for people
// and the rule for its own conversions.

function browserZone() {
  try { return Intl.DateTimeFormat().resolvedOptions().timeZone || ''; }
  catch (e) { return ''; }
}

function tzRuleFor(name) {
  for (var i = 0; i < TZ_ZONES.length; i++) {
    if (TZ_ZONES[i][0] === name) return TZ_ZONES[i][1];
  }
  return null;
}

// Two native <select>s - region ("America"), then zone ("New York") - because
// phones render a select as a scrollable list, where a 461-entry datalist
// shows nothing until the user types. The zone select's values are the full
// IANA names.
function fillTzZones(regionId, zoneId, selectedName) {
  var region = $(regionId).value, options = [];
  for (var i = 0; i < TZ_ZONES.length; i++) {
    var name = TZ_ZONES[i][0];
    if (name.split('/')[0] !== region) continue;
    var city = name.slice(region.length + 1).replace(/_/g, ' ').replace(/\//g, ' / ');
    options.push('<option value="' + name + '"' + (name === selectedName ? ' selected' : '') +
                 '>' + city + '</option>');
  }
  $(zoneId).innerHTML = options.join('');
}

// Fills both selects and picks the saved zone, else this browser's zone, else
// UTC. Returns true when the browser's zone was used, so the page can say it
// was detected rather than saved.
function initTzPicker(regionId, zoneId, savedName) {
  var detected = !(savedName && tzRuleFor(savedName));
  var name = !detected ? savedName : (tzRuleFor(browserZone()) ? browserZone() : 'Etc/UTC');
  var regions = [];
  for (var i = 0; i < TZ_ZONES.length; i++) {
    var region = TZ_ZONES[i][0].split('/')[0];
    if (regions.indexOf(region) < 0) regions.push(region);
  }
  var selectedRegion = name.split('/')[0];
  $(regionId).innerHTML = regions.map(function (r) {
    return '<option' + (r === selectedRegion ? ' selected' : '') + '>' + r + '</option>';
  }).join('');
  $(regionId).onchange = function () { fillTzZones(regionId, zoneId, null); };
  fillTzZones(regionId, zoneId, name);
  return detected;
}

// Reads the picker as {name, posix}. A non-empty custom-rule field overrides
// the table; the device validates the rule either way. Throws on an unknown zone.
function readTzPicker(zoneId, customId) {
  var name = $(zoneId).value;
  var custom = customId ? $(customId).value.trim() : '';
  if (custom) return { name: name || 'Custom', posix: custom };
  var rule = tzRuleFor(name);
  if (!rule) throw new Error('Unknown timezone "' + name + '" - pick one from the list');
  return { name: name, posix: rule };
}

// "UTC-4", "UTC+5:30"
function formatUtcOffset(minutes) {
  var sign = minutes < 0 ? '-' : '+', abs = Math.abs(minutes);
  var h = Math.floor(abs / 60), m = abs % 60;
  return 'UTC' + sign + h + (m ? ':' + (m < 10 ? '0' : '') + m : '');
}

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
