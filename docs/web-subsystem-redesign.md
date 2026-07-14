# Web Subsystem Redesign

## Why

The web subsystem currently has **three coexisting page-delivery mechanisms**:

1. **Dynamic String-built pages** — `/` (`handleRoot`) and `/format`
   (`handleFormatForm` / `handleFormatSave`) are assembled on the heap per
   request (`page.reserve(2400)` / `reserve(7500)`). A failed `String`
   reallocation silently truncates the page — an entire failure class we have
   already had to instrument for.
2. **Gzipped PROGMEM pages** — `tools/generate_web_pages.py` regex-scrapes raw
   literals out of `src/html.cpp` and emits `generated_web_pages.h`. These are
   served for `/settings`, `/config`, `/time`, `/sunset`, `/messages`,
   `/location`, `/wifi`, `/view`.
3. **Raw PROGMEM literals in `html.cpp`** — 12 pages, of which **4 are dead**
   (`INDEX_HTML`, `SETTINGS_HTML` raw form, `CONFIG_HTML`, `TIME_SYNC_HTML`)
   and one (`COMPACT_FORMAT_HTML`) is gzipped by the build script but never
   routed, because `/format` is served dynamically instead.

On top of that:

- **Duplicate write paths.** Form POSTs (`/mode`, `/demo`, `POST /format`)
  duplicate JSON APIs (`/api/mode`, `/api/demo/test`, `POST /api/config`),
  including a second, parallel config-save path (`handleFormatSave`) that
  bypasses `config_serializer`'s patch semantics.
- **~1.2 KB of identical boilerplate pasted into every page**: the `clog`
  beacon, `window.onerror` hook, the fetch-wrapper error reporter, the
  slow-load beacon, and the page-timing footer — each copied per page, some
  with drifted variants.
- **Near-identical but drifting CSS** in every page (`#3a9` vs `#397` buttons,
  differing paddings, three different `label` treatments). Uniformity requires
  editing 10+ blocks.
- **JavaScript is hand-minified inside C++ raw strings** — no syntax
  highlighting, no linting, no formatting, no comments, and every edit
  requires a full firmware flash to test.

## Design

### One delivery model

Every page is a **static, gzipped, PROGMEM asset**. All dynamic data flows
through the existing JSON APIs. No server-side HTML assembly of any kind.

```
Browser ── GET /page        → static gzipped shell (markup + tiny page JS)
        ── GET /common.css  → shared stylesheet   (immutable-cached)
        ── GET /common.js   → shared JS helpers   (immutable-cached)
        ── fetch /api/...   → JSON in, JSON out
```

This kills the dynamic-String failure class outright, and means
`sendGzipProgmem()`'s actual-bytes-written truncation detection covers **every**
page, not just some.

### Real source files

Page sources move out of C++ into a `web/` directory:

```
web/
  common.css          shared stylesheet (single source of visual truth)
  common.js           shared helpers (see below)
  pages/
    home.html         /            (replaces dynamic handleRoot)
    settings.html     /settings
    format.html       /format      (replaces dynamic form; based on COMPACT_FORMAT_HTML)
    time.html         /time        (from COMPACT_TIME_HTML)
    messages.html     /messages
    location.html     /location
    sunset.html       /sunset
    wifi.html         /wifi
    files.html        /config      (from CONFIG_JSON_HTML; file name matches what it is)
    view.html         /view
```

`src/html.cpp` and `src/html.h` are deleted. Pages become normal HTML/JS/CSS:
highlighted, lintable, formatted, commented. Minification (and byte-shaving)
becomes the build's job, not the author's.

### `common.js` (~1.5 KB minified, written once)

- `$(id)` helper.
- `api(url, opts)` — fetch wrapper: `cache:'no-store'`, JSON parsing,
  non-2xx → thrown error, failures reported to `#st` **and** beaconed to
  `/api/client-log`. This replaces both the pasted fetch-wrapper and the
  per-page ad-hoc `.catch()` messages, so every page reports API failures the
  same way.
- `setStatus(msg, clearMs)` with the auto-clear timer.
- `clog()` + `window.onerror` + slow-load beacon (the AP-mode debugging
  instrumentation, kept, but in one place).
- Page-timing footer.
- `reportFieldMismatch` / `setFieldFromConfig` (currently pasted into two
  pages).

Per-page `<script>` blocks then contain **only** that page's load/save/UI
logic.

### `common.css`

One stylesheet: body frame, `h1/h2`, buttons (one accent color), inputs,
selects, `hr`, `#st`, links, `.grid`/`.row` layout helpers, and the few
page-specific classes (network list, file table). Pages stop carrying any
`<style>` block except genuinely page-unique rules (target: none).

### Caching

- **Pages**: `Cache-Control: private, no-cache` (unchanged — config edits must
  show immediately; bfcache still works).
- **`/common.css`, `/common.js`**: served gzipped with
  `Cache-Control: public, max-age=31536000, immutable`. The build script
  stamps each page's `<link>`/`<script>` tags with a content hash
  (`/common.css?v=a1b2c3`), so a changed asset is a new URL and stale caches
  are impossible. After the first page load ever, a browser never refetches
  them — so the shared assets cost the single-threaded server nothing in
  steady state, and each page's own transfer gets *smaller* than today.

### Build script: `tools/build_web.py` (replaces `generate_web_pages.py`)

Per page: read `web/pages/*.html`, substitute the asset-hash placeholders,
light-minify (strip comments/leading whitespace — gzip does the rest), gzip,
verify round-trip, and emit one generated header containing a **table**, not
per-page symbol names:

```cpp
struct WebAsset {
  const char* path;         // "/format"
  const char* contentType;  // "text/html" | "text/css" | "application/javascript"
  const uint8_t* data;      // gzipped PROGMEM
  size_t size;
  bool immutable;           // cache policy
};
extern const WebAsset kWebAssets[];
extern const size_t kWebAssetCount;
```

The script fails the build if a page references an id in `common.js`'s
required set or omits the placeholders — cheap structural checks that are
impossible with regex-scraped C++ literals.

### Server side (`web_server.cpp`)

- Page registration collapses to one loop over `kWebAssets`; the ten
  hand-written `server_.on(...)` page lambdas go away.
- **Delete**: `handleRoot`, `handleFormatForm`, `handleFormatSave`,
  `handleModeForm`, `handleDemoForm`, `htmlEscape`, `sendDynamicHtml`,
  `appendFormatSelect`, and the `/mode`, `/demo`, `POST /format` routes.
- **Keep unchanged**: the API handler classes (`ConfigApi`, `TimeApi`,
  `FileApi`, `LocationApi`, `WifiApi`), `HttpResponder`, captive-portal
  probes/redirect, traffic summary, deferred reboot.
- **Add**: `GET /api/status` → `{"name":"<ssid>","mode":"clock"}` (a dozen
  lines in `ConfigApi`). The home page fetches this once for its title and
  the blinking current-mode button; that is the only dynamic data `/` needs.

### Home page behavior

`home.html` is static; on load it calls `/api/status` and marks the active
mode button. Mode buttons POST `/api/mode` (as the current JS home page
already does); Demo posts `/api/demo/test`. Same UX, one write path.

### Developer loop (the debuggability payoff)

`tools/dev_server.py` (~40 lines, stdlib only): serves `web/` files raw with
placeholders resolved, and proxies `/api/*` to a real device:

```
python tools/dev_server.py --device 192.168.1.42
```

Edit a page, hit reload, test against live device APIs — **no reflash**.
Firmware flashing is only needed when the page set is finalized or an API
changes. Browser devtools now show readable source with real line numbers,
so `window.onerror` beacons become actionable.

## Migration plan

Each phase builds and runs on-device independently.

0. **Reset to stock webserver behavior** — commit 3c3928f added the
   truncation *instrumentation* and a set of *behavior-changing* settings in
   the same change, so cause and fix candidates are currently entangled.
   Revert to library defaults: remove `WiFiClient::setDefaultNoDelay(true)`,
   `setPhyMode(11G)`, `setOutputPower(17.0)`, the AP channel survey, the
   AP-mode `setAutoReconnect(false)`/`disconnect()`, and the forced
   `WIFI_NONE_SLEEP` calls. Keep the observation-only logging (request /
   completion / client-log beacons) and the manual `write_P` send path in
   `sendGzipProgmem` — it is what produces the `TRUNCATED wrote X of Y bytes`
   evidence. Re-test the truncation scenario on stock settings; reintroduce a
   setting only with an observed symptom as evidence, one at a time.
   *(Done 2026-07-13.)*
1. **Extract** — create `web/` from the *live* variants (gzip-served pages
   plus `INDEX_HTML`'s JS home page as `home.html`, `COMPACT_FORMAT_HTML` as
   `format.html`), factor out `common.css`/`common.js`, write
   `tools/build_web.py` + asset table + registration loop. Delete `html.cpp`,
   `html.h`, `generate_web_pages.py`, and the four dead page variants.
   *(Done 2026-07-13, verified on device.)*
2. **Unify** — add `/api/status`; switch `/` and `/format` to their static
   pages; delete the dynamic builders and form-POST routes.
   *(Done 2026-07-13.)*
3. **Polish** — uniformity pass on `common.css`; add the dev proxy server;
   decide whether `reportFieldMismatch` instrumentation stays after the
   AP-mode truncation bug is closed.
   *(Done 2026-07-13: `tools/dev_server.py` serves `web/` raw and proxies
   `/api/*` to a device, sharing its route map with the build via
   `tools/web_manifest.py`; the CSS uniformity pass happened during the
   styling round. Decision: `reportFieldMismatch` stays — it detects
   config-vs-browser value mismatches, which is orthogonal to the closed
   truncation bug, and costs a few lines in one place.)*

## Costs / risks

- **Flash**: net *decrease* — dead pages go away, boilerplate is stored once
  instead of ~12×, and raw + gzip double storage ends.
- **Heap**: improves — no more multi-KB `String` page assembly per request.
- **First-load requests**: 3 instead of 1 (page + css + js) for a browser
  with a cold cache; immutable caching makes this a one-time cost per
  browser. If AP-mode captive-portal webviews turn out not to cache, the
  fallback is trivial: flip the build script to inline the two assets into
  each page — sources and server code are unchanged either way.
- **Behavior change**: `POST /format` and `POST /mode` form endpoints
  disappear; nothing but the deleted pages used them.
