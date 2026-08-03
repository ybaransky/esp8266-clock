# Web Subsystem Design

Status: implemented current design. The filename is retained for existing
links; this is no longer a redesign proposal.

## Delivery model

Every page is a static source file under `web/`:

- `web/pages/*.html`
- `web/common.css`
- `web/common.js`

`tools/build_web.py` runs before the firmware build, stamps shared-asset hashes
into pages, gzips every asset, and generates a PROGMEM asset table.
`tools/web_manifest.py` is the only route-to-file map and is shared by the
firmware packager and development server.

No handler constructs HTML. Dynamic state moves exclusively through JSON APIs.

## Runtime ownership

`ClockApplication` owns `WebPortal`. `WebPortal` owns:

- `ESP8266WebServer`
- `HttpResponder`
- captive-portal `DNSServer`
- `ConfigApi`
- `TimeApi`
- `LocationApi`
- `FileApi`
- `WifiApi`

`WebPortal::handleClients()` runs on every loop iteration so HTTP and captive
DNS remain responsive.

## Caching

HTML pages are served with no-cache behavior. Shared CSS and JavaScript are
hash-versioned and immutable, allowing long browser caching without serving a
stale shared asset after a firmware update.

## Configuration and errors

Pages load state from `GET /api/config` and save patches through
`POST /api/config`. `config_serializer` is the firmware's single source of JSON
field names and patch semantics. Shared `common.js` helpers report browser/API
failures to the serial log and preserve a page handler's more specific server
error message.

## Live page development

Run:

```bash
python tools/dev_server.py --device <clock-ip>
```

The development server serves the same page sources while proxying device API
requests, allowing HTML/CSS/JavaScript iteration without reflashing.
