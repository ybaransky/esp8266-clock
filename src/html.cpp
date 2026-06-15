#include "html.h"

const char INDEX_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Yurij Clock</title>
  <style>
    body { font-family: sans-serif; max-width: 420px; margin: 40px auto; padding: 0 16px; background: #fff; }
    h1   { font-size: 1.3rem; color: #333; }
    .card { background: #f4f4f4; border-radius: 8px; padding: 16px 20px; margin: 12px 0; }
    .label { color: #777; font-size: 0.8rem; text-transform: uppercase; letter-spacing: .05em; margin-bottom: 4px; }
    .value { font-size: 2.2rem; font-weight: 700; color: #111; }
    .unit  { font-size: 1rem; color: #999; margin-left: 2px; }
    #diffChart { width: 100%; height: 140px; display: block; background: #fff; border-radius: 6px; }
    #histChart { width: 100%; height: 140px; display: block; background: #fff; border-radius: 6px; }
  </style>
</head>
<body>
  <h1>Yurij Clock</h1>
</body>
</html>
)rawliteral";
