import * as crypto from 'node:crypto';
import * as fsPromises from 'node:fs/promises';
import * as http from 'node:http';
import * as process from 'node:process';
import * as esbuild from 'esbuild';
import { makeGbkSubset, makeSjisSubset } from './font-subset.js';

const logLevel = 'info';
const outdir = 'dist';

async function installExternalModules() {
    return Promise.all([
        fsPromises.copyFile('node_modules/@picocss/pico/css/pico.min.css', 'dist/lib/pico.min.css'),
    ]);
}

const fontCacheDir = 'build-fonts';
const notoCjkBaseUrl = 'https://raw.githubusercontent.com/notofonts/noto-cjk';
const downloadedFonts = {
    'NotoSansSC-Regular.otf': {
        url: `${notoCjkBaseUrl}/Sans2.004/Sans/SubsetOTF/SC/NotoSansSC-Regular.otf`,
        sha256: 'faa6c9df652116dde789d351359f3d7e5d2285a2b2a1f04a2d7244df706d5ea9',
    },
    'NotoSerifSC-Regular.otf': {
        url: `${notoCjkBaseUrl}/Serif2.003/Serif/SubsetOTF/SC/NotoSerifSC-Regular.otf`,
        sha256: 'e8f396decc1f0963a016a989c3d8852e863d1350996f573860a80767c83a1cd3',
    },
    'NotoCJK.license': {
        url: `${notoCjkBaseUrl}/Serif2.003/Serif/LICENSE`,
        sha256: '6a73f9541c2de74158c0e7cf6b0a58ef774f5a780bf191f2d7ec9cc53efe2bf2',
    },
};

// Downloads a file listed in downloadedFonts into fontCacheDir (if not cached yet)
// and returns its local path.
async function downloadFont(name) {
    const { url, sha256 } = downloadedFonts[name];
    const path = `${fontCacheDir}/${name}`;
    try {
        await fsPromises.access(path);
        return path;
    } catch (e) {}
    console.log(`Downloading ${url}`);
    const resp = await fetch(url);
    if (!resp.ok) throw new Error(`Failed to download ${url}: ${resp.status}`);
    const data = Buffer.from(await resp.arrayBuffer());
    const hash = crypto.createHash('sha256').update(data).digest('hex');
    if (hash !== sha256) throw new Error(`SHA-256 mismatch for ${url}: ${hash}`);
    await fsPromises.mkdir(fontCacheDir, { recursive: true });
    await fsPromises.writeFile(path, data);
    return path;
}

async function buildFontSubset() {
    await makeSjisSubset('xsystem4/fonts/HanaMinA.ttf', 'dist/fonts/HanaMinA-SJIS.ttf');
    await makeSjisSubset('xsystem4/fonts/VL-Gothic-Regular.ttf', 'dist/fonts/VL-Gothic-Regular-SJIS.ttf');
    // Fonts for GBK (simplified Chinese) games.
    await makeGbkSubset(await downloadFont('NotoSansSC-Regular.otf'), 'dist/fonts/NotoSansSC-Regular-GBK.otf');
    await makeGbkSubset(await downloadFont('NotoSerifSC-Regular.otf'), 'dist/fonts/NotoSerifSC-Regular-GBK.otf');
    await fsPromises.copyFile(await downloadFont('NotoCJK.license'), 'dist/fonts/NotoCJK.license');
}

async function runServer(ctx) {
    // Start esbuild's server on a random local port
    let { host, port } = await ctx.serve({ servedir: outdir });
    // Then start a proxy server on port 8080
    http.createServer((req, res) => {
        const options = {
            hostname: host,
            port: port,
            path: req.url,
            method: req.method,
            headers: req.headers,
        }

        // Forward each incoming request to esbuild
        const proxyReq = http.request(options, proxyRes => {
            if (req.url !== '/service-worker.js') {
                // Add COOP and COEP headers to the response from esbuild
                proxyRes.headers['Cross-Origin-Opener-Policy'] = 'same-origin';
                proxyRes.headers['Cross-Origin-Embedder-Policy'] = 'require-corp';
            }
            // Forward the response to the client
            res.writeHead(proxyRes.statusCode, proxyRes.headers)
            proxyRes.pipe(res, { end: true })
        })

        // Forward the body of the request to esbuild
        req.pipe(proxyReq, { end: true })
    }).listen(8080)
    console.log(`Server running at http://localhost:8080/`);
}

const configs = [
    {
        entryPoints: ['src/index.ts', 'src/install.ts', 'src/play.ts', 'src/worker/installer_worker.ts'],
        bundle: true,
        external: ['./xsystem4.js'],
        minify: true,
        format: 'esm',
        target: ['esnext'],
        outdir,
        sourcemap: true,
        logLevel,
    },
];

for (const config of configs) {
    if (process.argv[2] === '--watch') {
        (await esbuild.context(config)).watch();
    } else if (process.argv[2] === '--serve') {
        config.logLevel = 'warning';  // Don't print backend server URLs
        await runServer(await esbuild.context(config));
    } else {
        esbuild.build(config);
    }
}

await fsPromises.mkdir('dist/lib', { recursive: true });
await fsPromises.mkdir('dist/fonts', { recursive: true });
await installExternalModules();
await buildFontSubset();
