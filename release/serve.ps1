# Tiny local web server for "xsystem4 + Yomitan".
# Serves this folder at http://localhost:8642/ with the headers the game
# engine needs (cross-origin isolation), then opens the game page.
# Close this window to stop the server.

$ErrorActionPreference = 'Stop'
$root = Join-Path $PSScriptRoot 'site'
$port = 8642
$prefix = "http://localhost:$port/"

$mime = @{
    '.html' = 'text/html; charset=utf-8'
    '.js'   = 'text/javascript; charset=utf-8'
    '.mjs'  = 'text/javascript; charset=utf-8'
    '.css'  = 'text/css; charset=utf-8'
    '.json' = 'application/json'
    '.webmanifest' = 'application/manifest+json'
    '.wasm' = 'application/wasm'
    '.svg'  = 'image/svg+xml'
    '.png'  = 'image/png'
    '.jpg'  = 'image/jpeg'
    '.ico'  = 'image/x-icon'
    '.ttf'  = 'font/ttf'
    '.otf'  = 'font/otf'
    '.woff2' = 'font/woff2'
    '.txt'  = 'text/plain; charset=utf-8'
}

$listener = New-Object System.Net.HttpListener
$listener.Prefixes.Add($prefix)
try {
    $listener.Start()
} catch {
    Write-Host "Could not start the server on port $port. Is it already running?" -ForegroundColor Red
    Write-Host $_
    Read-Host 'Press Enter to close'
    exit 1
}

Write-Host "xsystem4 + Yomitan is running at ${prefix}play.html"
Write-Host 'First time: pick a ZIP of your Rance VI folder on the page. Then hold Shift and hover text to look it up.'
Write-Host 'Keep this window open while playing. Close it to stop.'
try { Start-Process "${prefix}play.html" } catch { Write-Host "Open ${prefix}play.html in Chrome or Edge." }

$rootFull = [System.IO.Path]::GetFullPath($root)
while ($listener.IsListening) {
    $ctx = $listener.GetContext()
    $res = $ctx.Response
    try {
        $path = [System.Uri]::UnescapeDataString($ctx.Request.Url.AbsolutePath)
        if ($path.EndsWith('/')) { $path += 'index.html' }
        $file = [System.IO.Path]::GetFullPath((Join-Path $rootFull $path.TrimStart('/')))
        $res.Headers.Add('Cross-Origin-Opener-Policy', 'same-origin')
        $res.Headers.Add('Cross-Origin-Embedder-Policy', 'require-corp')
        $res.Headers.Add('Cross-Origin-Resource-Policy', 'same-origin')
        $res.Headers.Add('Cache-Control', 'no-cache')
        if ($file.StartsWith($rootFull) -and (Test-Path $file -PathType Leaf)) {
            $ext = [System.IO.Path]::GetExtension($file).ToLower()
            $res.ContentType = if ($mime.ContainsKey($ext)) { $mime[$ext] } else { 'application/octet-stream' }
            $bytes = [System.IO.File]::ReadAllBytes($file)
            $res.ContentLength64 = $bytes.Length
            $res.OutputStream.Write($bytes, 0, $bytes.Length)
        } else {
            $res.StatusCode = 404
        }
    } catch {
        $res.StatusCode = 500
    } finally {
        $res.Close()
    }
}
