# Tiny local web server for "xsystem4 + Yomitan".
# Serves this folder at http://localhost:8642/ with the headers the game
# engine needs (cross-origin isolation), then opens the game page.
# Close this window to stop the server.
#
# Each game gets its own port: the browser keeps installed game files and
# saves per port, so installing a second game on the same port replaces the
# first one (saves included).
#
# Save files are also kept as normal files in the "saves" folder next to this
# script (the page syncs them through /saves/). The previous version of each
# overwritten save is kept in saves\.previous.

param([int]$port = 8642)

$ErrorActionPreference = 'Stop'
$root = Join-Path $PSScriptRoot 'site'
$saves = Join-Path $PSScriptRoot 'saves'
$prefix = "http://localhost:$port/"
$origin = "http://localhost:$port"

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
Write-Host 'First time: pick a ZIP of your game folder on the page. Then hold Shift and hover text to look it up.'
Write-Host 'Keep this window open while playing. Close it to stop.'
try { Start-Process "${prefix}play.html" } catch { Write-Host "Open ${prefix}play.html in Chrome or Edge." }

$rootFull = [System.IO.Path]::GetFullPath($root)
New-Item -ItemType Directory -Force $saves | Out-Null
$savesFull = [System.IO.Path]::GetFullPath($saves)
$previousFull = Join-Path $savesFull '.previous'

function Write-Bytes($res, [byte[]]$bytes, $type) {
    $res.ContentType = $type
    $res.ContentLength64 = $bytes.Length
    $res.OutputStream.Write($bytes, 0, $bytes.Length)
}

# GET /saves/ lists the save files, GET /saves/<path> reads one, and
# PUT /saves/<path> writes one (X-Mtime header: modification time in ms).
function Handle-Saves($ctx, [string]$rel) {
    $req = $ctx.Request
    $res = $ctx.Response
    # Only the game page itself may use this, not other websites.
    $site = $req.Headers['Sec-Fetch-Site']
    if (($site -and $site -ne 'same-origin') -or ($req.HttpMethod -ne 'GET' -and $req.Headers['Origin'] -ne $origin)) {
        $res.StatusCode = 403
        return
    }
    if ($rel -eq '') {
        if ($req.HttpMethod -ne 'GET') { $res.StatusCode = 405; return }
        $entries = foreach ($f in Get-ChildItem $savesFull -Recurse -File) {
            if ($f.FullName.StartsWith($previousFull + '\')) { continue }
            $p = $f.FullName.Substring($savesFull.Length + 1).Replace('\', '/')
            $mtime = ([DateTimeOffset]$f.LastWriteTimeUtc).ToUnixTimeMilliseconds()
            '{"path":' + (ConvertTo-Json -InputObject $p -Compress) + ',"size":' + $f.Length + ',"mtime":' + $mtime + '}'
        }
        Write-Bytes $res ([System.Text.Encoding]::UTF8.GetBytes('[' + ($entries -join ',') + ']')) 'application/json'
        return
    }
    $file = [System.IO.Path]::GetFullPath((Join-Path $savesFull $rel))
    if (-not $file.StartsWith($savesFull + '\') -or $file.StartsWith($previousFull + '\')) {
        $res.StatusCode = 400
        return
    }
    switch ($req.HttpMethod) {
        'GET' {
            if (Test-Path $file -PathType Leaf) {
                Write-Bytes $res ([System.IO.File]::ReadAllBytes($file)) 'application/octet-stream'
            } else {
                $res.StatusCode = 404
            }
        }
        'PUT' {
            $body = New-Object System.IO.MemoryStream
            $req.InputStream.CopyTo($body)
            New-Item -ItemType Directory -Force (Split-Path $file) | Out-Null
            if (Test-Path $file -PathType Leaf) {
                $prev = Join-Path $previousFull $file.Substring($savesFull.Length + 1)
                New-Item -ItemType Directory -Force (Split-Path $prev) | Out-Null
                Copy-Item $file $prev -Force
            }
            $tmp = "$file.tmp"
            [System.IO.File]::WriteAllBytes($tmp, $body.ToArray())
            Move-Item $tmp $file -Force
            $mtime = 0L
            if ([long]::TryParse($req.Headers['X-Mtime'], [ref]$mtime) -and $mtime -gt 0) {
                [System.IO.File]::SetLastWriteTimeUtc($file, [DateTimeOffset]::FromUnixTimeMilliseconds($mtime).UtcDateTime)
            }
            $res.StatusCode = 204
        }
        default { $res.StatusCode = 405 }
    }
}

while ($listener.IsListening) {
    $ctx = $listener.GetContext()
    $res = $ctx.Response
    try {
        $path = [System.Uri]::UnescapeDataString($ctx.Request.Url.AbsolutePath)
        if ($path.StartsWith('/saves/')) {
            $res.Headers.Add('Cache-Control', 'no-store')
            Handle-Saves $ctx $path.Substring('/saves/'.Length)
            continue
        }
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
