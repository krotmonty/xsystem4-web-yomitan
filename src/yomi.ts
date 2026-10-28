// In-game Yomitan text layer.
//
// The engine (xsystem4/src/sprite.c, textmap_*) reports every line of text
// drawn on screen, with its position in game coordinates. This class places
// invisible HTML text exactly over those lines, so browser pop-up dictionaries
// (Yomitan, 10ten, ...) can scan the game's text as if it were a web page.
//
// The layer only catches the mouse while Shift is held, so normal clicks
// still go to the game. Hold Shift and hover a word to look it up.
// Ctrl+Alt+Y shows the layer (for checking alignment). F11 fullscreens the
// whole page so the layer stays above the canvas.

type Line = [x: number, y: number, w: number, h: number, text: string];

interface TextMap {
    w: number;  // game screen width
    h: number;  // game screen height
    l: Line[];
}

const FORWARDED_EVENTS = ['mousedown', 'mouseup', 'click', 'dblclick', 'contextmenu', 'mousemove'];

export class YomiOverlay {
    private layer: HTMLDivElement;
    private map: TextMap = { w: 0, h: 0, l: [] };
    private elements: HTMLDivElement[] = [];

    constructor(private canvas: HTMLCanvasElement) {
        this.layer = document.createElement('div');
        this.layer.id = 'yomi-layer';
        this.layer.lang = 'ja';
        document.body.appendChild(this.layer);

        new ResizeObserver(() => this.layout()).observe(canvas);
        window.addEventListener('resize', () => this.layout());

        // Catch the mouse only while Shift is held.
        const setActive = (on: boolean) => this.layer.classList.toggle('active', on);
        window.addEventListener('keydown', (e) => {
            if (e.key === 'Shift') setActive(true);
            if (e.ctrlKey && e.altKey && e.code === 'KeyY') {
                this.layer.classList.toggle('debug');
                e.preventDefault();
            }
            // The engine handles F11 by making only the canvas fullscreen,
            // which hides this layer. Fullscreen the whole page instead.
            if (e.code === 'F11') {
                e.preventDefault();
                e.stopImmediatePropagation();
                if (!e.repeat) {
                    if (document.fullscreenElement) document.exitFullscreen();
                    else document.documentElement.requestFullscreen();
                }
            }
        }, true);
        window.addEventListener('keyup', (e) => {
            if (e.key === 'Shift') setActive(false);
        }, true);
        window.addEventListener('blur', () => setActive(false));
        // Keyboard events can be missed (e.g. while focus is in the popup).
        window.addEventListener('mousemove', (e) => setActive(e.shiftKey), true);

        // Pass clicks on the text through to the game.
        for (const type of FORWARDED_EVENTS) {
            this.layer.addEventListener(type, (e) => {
                const me = e as MouseEvent;
                if (type === 'contextmenu') me.preventDefault();
                this.canvas.dispatchEvent(new MouseEvent(type, me));
            });
        }
        this.layer.addEventListener('wheel', (e) => {
            this.canvas.dispatchEvent(new WheelEvent('wheel', e));
        }, { passive: true });
    }

    // Called from the engine whenever the text on screen changes.
    update(json: string) {
        try {
            this.map = JSON.parse(json);
        } catch (e) {
            console.warn('yomi: bad text map', e);
            return;
        }
        // Keep line elements, only changing their text, so a lookup in
        // progress isn't disturbed more than necessary.
        const lines = this.map.l;
        while (this.elements.length > lines.length)
            this.elements.pop()!.remove();
        while (this.elements.length < lines.length) {
            const el = document.createElement('div');
            el.className = 'yomi-line';
            this.layer.appendChild(el);
            this.elements.push(el);
        }
        lines.forEach((line, i) => {
            const el = this.elements[i];
            if (el.textContent !== line[4]) el.textContent = line[4];
        });
        this.layout();
    }

    private layout() {
        const { w: gw, h: gh, l: lines } = this.map;
        if (!gw || !gh) return;

        // Same letterboxing as gfx_update_screen_scale() in video.c.
        const rect = this.canvas.getBoundingClientRect();
        const scale = Math.min(rect.width / gw, rect.height / gh);
        const ox = rect.left + (rect.width - gw * scale) / 2;
        const oy = rect.top + (rect.height - gh * scale) / 2;

        lines.forEach(([x, y, w, h], i) => {
            const el = this.elements[i];
            const fontSize = h * scale;
            el.style.left = `${ox + x * scale}px`;
            el.style.top = `${oy + y * scale}px`;
            el.style.fontSize = `${fontSize}px`;
            el.style.lineHeight = `${fontSize}px`;
            el.style.height = `${fontSize}px`;
            el.style.transform = '';
            // Stretch the browser's text to the width the game drew it at,
            // so each character sits over its counterpart on screen.
            const natural = el.scrollWidth;
            const target = w * scale;
            if (natural > 0 && target > 0)
                el.style.transform = `scaleX(${target / natural})`;
        });
    }
}
