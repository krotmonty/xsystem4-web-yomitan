// Keeps the game's save files as normal files on disk.
//
// The engine saves into OPFS (game/SaveData), which lives inside the browser.
// The local launcher (release/serve.ps1) also serves a "saves" folder at
// ./saves/, and this mirrors SaveData to it:
// - At startup, before the engine runs, the disk copies are the source of
//   truth and are written into OPFS.
// - While playing, saves the game writes are uploaded to disk.
// When there is no launcher (e.g. the hosted site), ./saves/ is missing and
// nothing happens.

import { OPFS_GAMEDIR } from './utils.js';

const ENDPOINT = './saves/';
const PUSH_INTERVAL = 3000;

interface DiskEntry {
    path: string;   // relative to SaveData, '/'-separated
    size: number;
    mtime: number;  // ms since epoch
}

type Signature = string;  // size:lastModified of the OPFS file

function signature(file: File): Signature {
    return `${file.size}:${file.lastModified}`;
}

function url(path: string) {
    return ENDPOINT + path.split('/').map(encodeURIComponent).join('/');
}

async function saveDir(create: boolean): Promise<FileSystemDirectoryHandle | null> {
    try {
        const root = await navigator.storage.getDirectory();
        const gameDir = await root.getDirectoryHandle(OPFS_GAMEDIR, { create });
        return await gameDir.getDirectoryHandle('SaveData', { create });
    } catch (e) {
        return null;
    }
}

async function fileHandle(dir: FileSystemDirectoryHandle, path: string, create: boolean) {
    const parts = path.split('/');
    const name = parts.pop()!;
    for (const part of parts) {
        dir = await dir.getDirectoryHandle(part, { create });
    }
    return dir.getFileHandle(name, { create });
}

async function* walk(dir: FileSystemDirectoryHandle, prefix = ''): AsyncGenerator<[string, FileSystemFileHandle]> {
    for await (const e of dir.values()) {
        if (e.kind === 'directory') {
            yield* walk(e as FileSystemDirectoryHandle, prefix + e.name + '/');
        } else {
            yield [prefix + e.name, e as FileSystemFileHandle];
        }
    }
}

function sameBytes(a: Uint8Array, b: Uint8Array) {
    if (a.length !== b.length) return false;
    for (let i = 0; i < a.length; i++) {
        if (a[i] !== b[i]) return false;
    }
    return true;
}

export class SaveSync {
    // Signatures of OPFS files as last synced with disk.
    private synced = new Map<string, Signature>();
    private pushing = false;

    private constructor() {}

    // Returns null when there is no save folder to sync with.
    static async create(): Promise<SaveSync | null> {
        try {
            const resp = await fetch(ENDPOINT);
            if (!resp.ok || !resp.headers.get('Content-Type')?.includes('json')) return null;
            const sync = new SaveSync();
            await sync.pull(await resp.json());
            return sync;
        } catch (e) {
            console.warn('Save sync disabled', e);
            return null;
        }
    }

    // Copies the disk saves into OPFS. Call before the engine starts.
    private async pull(entries: DiskEntry[]) {
        const dir = await saveDir(true);
        if (!dir) return;
        for (const entry of entries) {
            const resp = await fetch(url(entry.path));
            if (!resp.ok) continue;
            const data = new Uint8Array(await resp.arrayBuffer());
            const handle = await fileHandle(dir, entry.path, true);
            const current = new Uint8Array(await (await handle.getFile()).arrayBuffer());
            if (!sameBytes(current, data)) {
                const w = await handle.createWritable();
                await w.write(data);
                await w.close();
            }
            this.synced.set(entry.path, signature(await handle.getFile()));
        }
        console.log(`Loaded ${entries.length} save file(s) from disk`);
    }

    // Starts uploading saves the game writes.
    start() {
        setInterval(() => this.push(), PUSH_INTERVAL);
        document.addEventListener('visibilitychange', () => {
            if (document.visibilityState === 'hidden') this.push();
        });
        this.push();
    }

    async push() {
        if (this.pushing) return;
        this.pushing = true;
        try {
            const dir = await saveDir(false);
            if (!dir) return;
            for await (const [path, handle] of walk(dir)) {
                let file: File;
                try {
                    file = await handle.getFile();
                } catch (e) {
                    continue;  // The engine is writing it; try next time.
                }
                const sig = signature(file);
                if (this.synced.get(path) === sig) continue;
                const resp = await fetch(url(path), {
                    method: 'PUT',
                    body: await file.arrayBuffer(),
                    headers: { 'X-Mtime': String(file.lastModified) },
                    keepalive: file.size < 60000,
                });
                if (resp.ok) {
                    this.synced.set(path, sig);
                } else {
                    console.warn(`Failed to save ${path} to disk: ${resp.status}`);
                }
            }
        } catch (e) {
            console.warn('Save sync failed', e);
        } finally {
            this.pushing = false;
        }
    }
}
