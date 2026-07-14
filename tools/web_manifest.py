"""Single source of truth for web routes.

Used by build_web.py (packages sources into firmware) and dev_server.py
(serves the same sources raw during development).
"""

# route -> page source file in web/pages/
PAGES = {
    "/": "home.html",
    "/format": "format.html",
    "/settings": "settings.html",
    "/config": "files.html",
    "/time": "time.html",
    "/sunset": "sunset.html",
    "/messages": "messages.html",
    "/location": "location.html",
    "/wifi": "wifi.html",
    "/view": "view.html",
}

# route -> (source file in web/, content type)
SHARED = {
    "/common.css": ("common.css", "text/css"),
    "/common.js": ("common.js", "application/javascript"),
}
