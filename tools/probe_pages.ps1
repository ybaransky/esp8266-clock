# Repeatedly fetches every page and API route from the clock and verifies each
# response arrived complete. Run from a laptop connected to the clock (AP mode:
# join the clock's network first). Converts "pages sometimes look broken" into
# a measurable per-route failure rate; correlate failures with the serial log.
#
#   .\tools\probe_pages.ps1                          # 10 passes against 192.168.4.1
#   .\tools\probe_pages.ps1 -DeviceIp 192.168.1.50 -Iterations 30
#
# Failure classes detected:
#   curl-error   transport failed: connect/timeout, short body vs Content-Length
#                (exit 18), or truncated gzip stream (--compressed decode error)
#   http-<code>  non-200 status
#   no-html-end  body received "complete" but does not end with </html> -
#                the on-device String-truncation signature (server-side OOM)

param(
    [string]$DeviceIp = "192.168.4.1",
    [int]$Iterations = 10,
    [int]$TimeoutSec = 15
)

$htmlRoutes = @("/", "/settings", "/format", "/time", "/sunset",
                "/messages", "/location", "/wifi", "/config", "/view")
$apiRoutes  = @("/api/config", "/api/formats", "/api/time",
                "/api/files", "/api/wifi/status")

$tempDir = Join-Path $env:TEMP "clock_probe"
New-Item -ItemType Directory -Force $tempDir | Out-Null
$bodyFile = Join-Path $tempDir "body.tmp"

$results = @{}
foreach ($route in ($htmlRoutes + $apiRoutes)) {
    $results[$route] = [pscustomobject]@{
        Route = $route; Pass = 0; Fail = 0; Errors = @{}; MaxMs = 0
    }
}

Write-Host "Probing http://$DeviceIp - $Iterations passes over $($results.Count) routes"

for ($i = 1; $i -le $Iterations; $i++) {
    foreach ($route in ($htmlRoutes + $apiRoutes)) {
        $r = $results[$route]
        Remove-Item $bodyFile -ErrorAction SilentlyContinue

        $metrics = & curl.exe -s --compressed --max-time $TimeoutSec `
            -o $bodyFile -w "%{http_code} %{size_download} %{time_total}" `
            "http://$DeviceIp$route"
        $curlExit = $LASTEXITCODE

        $httpCode = 0; $elapsedMs = 0
        if ($metrics -match '^(\d+) (\d+) ([\d.]+)$') {
            $httpCode = [int]$Matches[1]
            $elapsedMs = [int]([double]$Matches[3] * 1000)
        }
        if ($elapsedMs -gt $r.MaxMs) { $r.MaxMs = $elapsedMs }

        $error_ = $null
        if ($curlExit -ne 0) {
            $error_ = "curl-error($curlExit)"
        } elseif ($httpCode -ne 200) {
            $error_ = "http-$httpCode"
        } elseif ($htmlRoutes -contains $route) {
            $tail = ""
            if (Test-Path $bodyFile) {
                $raw = [System.IO.File]::ReadAllText($bodyFile)
                if ($raw.Length -gt 0) { $tail = $raw.Trim() }
            }
            if (-not $tail.EndsWith("</html>")) { $error_ = "no-html-end" }
        }

        if ($null -eq $error_) {
            $r.Pass++
        } else {
            $r.Fail++
            if (-not $r.Errors.ContainsKey($error_)) { $r.Errors[$error_] = 0 }
            $r.Errors[$error_]++
            Write-Host ("  FAIL pass {0} {1,-18} {2} ({3} ms)" -f $i, $route, $error_, $elapsedMs) -ForegroundColor Red
        }
    }
    Write-Host ("pass {0}/{1} done" -f $i, $Iterations)
}

Write-Host ""
Write-Host ("{0,-18} {1,5} {2,5} {3,8}  {4}" -f "route", "pass", "fail", "max(ms)", "errors")
foreach ($route in ($htmlRoutes + $apiRoutes)) {
    $r = $results[$route]
    $errorText = ($r.Errors.GetEnumerator() | ForEach-Object { "$($_.Key)x$($_.Value)" }) -join " "
    $color = "Green"; if ($r.Fail -gt 0) { $color = "Red" }
    Write-Host ("{0,-18} {1,5} {2,5} {3,8}  {4}" -f $r.Route, $r.Pass, $r.Fail, $r.MaxMs, $errorText) -ForegroundColor $color
}
