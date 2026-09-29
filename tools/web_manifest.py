"""Single source of truth for web routes.

Used by build_web.py (packages sources into firmware) and dev_server.py
(serves the same sources raw during development).
"""

# route -> page source file in web/pages/
PAGES = {
    "/": "home.html",
    "/format": "format.html",
    "/settings": "settings.html",
    "/files": "files.html",
    "/time": "time.html",
    "/sunset": "sunset.html",
    "/messages": "messages.html",
    "/sound": "sound.html",
    "/location": "location.html",
    "/wifi": "wifi.html",
    "/view": "view.html",
}

# route -> (source file in web/, content type). Every page must reference each.
SHARED = {
    "/common.css": ("common.css", "text/css"),
    "/common.js": ("common.js", "application/javascript"),
}

# route -> content type, for assets built by tools/web_generated.py rather than
# stored in web/. Unlike SHARED, only the pages that need one reference it.
GENERATED = {
    "/tz.js": "application/javascript",
}
