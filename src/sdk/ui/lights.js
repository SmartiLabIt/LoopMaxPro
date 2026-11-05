// LoopMaxPro - the interface of the lights of the SDK (sdk/lights.h), for every module that
// has them: RGB Controller with one light, GanjaLeaf with two. It takes the place of the
// standard home; schedules, settings and info stay the standard ones. With more lights, the
// switches of all of them on top and one light at a time below. The same file runs on the
// device and on the server: everything comes from ctx (docs/web-console.md).

const SWATCHES = ['#ff0000', '#ff6a00', '#ffd000', '#00ff40', '#00ffd5', '#0066ff', '#8a2be2',
                  '#ff00aa', '#ffb46b', '#ffffff'];
const ICONS = { christmas: 'tree-pine', rainbow: 'rainbow', breath: 'heart-pulse',
                blink: 'lightbulb', fade: 'blend', chase: 'refresh-cw', wheel: 'orbit',
                comet: 'wind', sparkle: 'star' };

// Only what the pieces of the console do not have: the light, the swatches, the sliders.
const STYLE = `
.lt-hero { display: flex; align-items: center; gap: 18px; }
.lt-orb { flex: none; width: 96px; height: 96px; border-radius: 50%; border: 1px solid var(--line);
  background: #111; transition: background .3s, box-shadow .3s, opacity .3s; }
.lt-orb.sm { width: 40px; height: 40px; }
.lt-orb.ring { -webkit-mask: radial-gradient(circle, transparent 45%, #000 46%);
  mask: radial-gradient(circle, transparent 45%, #000 46%); }
.lt-orb.fx-rainbow, .lt-orb.fx-wheel { background: conic-gradient(#f00, #ff0, #0f0, #0ff, #00f, #f0f, #f00);
  animation: lt-spin var(--lt-t, 6s) linear infinite; }
.lt-orb.fx-rainbow { animation-name: lt-hue; }
.lt-orb.fx-chase { background: conic-gradient(var(--lt-c) 0 330deg, #111 330deg);
  animation: lt-spin var(--lt-t, 1.2s) steps(12) infinite; }
.lt-orb.fx-comet { background: conic-gradient(transparent, var(--lt-c));
  animation: lt-spin var(--lt-t, 1s) linear infinite; }
.lt-orb.fx-christmas { animation: lt-xmas var(--lt-t, 5s) steps(1) infinite; }
.lt-orb.fx-fade { animation: lt-fade var(--lt-t, 6s) linear infinite; }
.lt-orb.fx-breath { animation: lt-breath var(--lt-t, 2.4s) ease-in-out infinite; }
.lt-orb.fx-blink { animation: lt-blink var(--lt-t, .8s) steps(1) infinite; }
.lt-orb.fx-sparkle { background: radial-gradient(circle at 30% 30%, var(--lt-c) 0 12%, transparent 13%),
  radial-gradient(circle at 70% 60%, var(--lt-c) 0 10%, transparent 11%),
  radial-gradient(circle at 45% 80%, var(--lt-c) 0 8%, transparent 9%), #111;
  animation: lt-blink var(--lt-t, .9s) ease-in-out infinite alternate; }
@keyframes lt-spin { to { transform: rotate(360deg); } }
@keyframes lt-hue { to { filter: hue-rotate(360deg); } }
@keyframes lt-xmas { 0% { background: #f00 } 20% { background: #0f0 } 40% { background: #00f }
  60% { background: #fff } 80% { background: #111 } }
@keyframes lt-fade { 0%, 100% { background: #f00 } 33% { background: #0f0 } 66% { background: #00f } }
@keyframes lt-breath { 0%, 100% { opacity: .15 } 50% { opacity: 1 } }
@keyframes lt-blink { 0% { opacity: 1 } 50% { opacity: .1 } }
@media (prefers-reduced-motion: reduce) { .lt-orb { animation: none !important; } }
.lt-state { flex: 1; min-width: 0; display: grid; gap: 10px; }
.lt-state b { font-size: 1.15rem; }
.lt-state small { color: var(--muted); }
.lt-list { display: grid; gap: 12px; }
.lt-item { display: flex; align-items: center; gap: 12px; }
.lt-name { flex: 1; min-width: 0; display: grid; }
.lt-name small { color: var(--muted); }
.lt-pickone { margin-top: 16px; }
.lt-row { display: flex; align-items: center; gap: 10px; margin-top: 14px; }
.lt-row span { width: 3.2em; text-align: right; font-variant-numeric: tabular-nums; color: var(--muted); }
.lt-range { flex: 1; min-width: 0; accent-color: var(--accent); height: 28px; }
.lt-pick { display: flex; align-items: center; gap: 14px; flex-wrap: wrap; }
.lt-pick input[type=color] { width: 88px; height: 56px; border: 1px solid var(--line);
  border-radius: var(--radius-s); background: var(--panel2); padding: 4px; cursor: pointer; }
.lt-hex { font-family: var(--mono); color: var(--muted); }
.lt-swatches { display: grid; grid-template-columns: repeat(5, minmax(0, 1fr)); gap: 10px; margin-top: 14px; }
.lt-sw { aspect-ratio: 1; min-height: 40px; border-radius: 12px; border: 2px solid transparent;
  box-shadow: inset 0 0 0 1px rgba(0,0,0,.25); cursor: pointer; }
.lt-sw.on { border-color: var(--text); }
.lt-fx { display: grid; grid-template-columns: repeat(5, minmax(0, 1fr)); gap: 10px; }
.lt-fx button { display: grid; justify-items: center; gap: 6px; padding: 12px 4px; min-height: 76px;
  border-radius: var(--radius-s); border: 1px solid var(--line); background: var(--panel2);
  color: var(--text); font: inherit; font-size: .85rem; cursor: pointer; }
.lt-fx button.on { border-color: var(--accent); box-shadow: var(--glow); color: var(--accent); }
@media (max-width: 640px) {
  .lt-fx { grid-template-columns: repeat(3, minmax(0, 1fr)); }
  .lt-orb:not(.sm) { width: 76px; height: 76px; }
}
`;

// Many changes while a finger moves: one command every ms at most, and always the last one.
function useThrottled(ctx, fn, ms) {
  const last = ctx.useRef(0);
  const timer = ctx.useRef(null);
  const next = ctx.useRef(null);
  ctx.useEffect(() => () => clearTimeout(timer.current), []);
  return (value) => {
    next.current = value;
    const wait = last.current + ms - Date.now();
    clearTimeout(timer.current);
    if (wait <= 0) {
      last.current = Date.now();
      fn(value);
    } else {
      timer.current = setTimeout(() => {
        last.current = Date.now();
        fn(next.current);
      }, wait);
    }
  };
}

// The seconds of an effect in the preview, from the speed: close to the light, not the same.
function effectSeconds(id, speed) {
  const s = Math.max(1, speed || 50);
  const ms = { christmas: 5 * (2100 - 20 * s), rainbow: 600000 / s, fade: 3 * (30000 / s + 300),
               breath: 40000 / s + 1500, blink: 20000 / s + 100, chase: 12 * 5000 / s,
               wheel: 200000 / s, comet: 12 * 4000 / s, sparkle: 45000 / s }[id] || 2000;
  return Math.min(ms, 60000) / 1000 + 's';
}

// The name of a light: its channel, translated when it is the name it was born with.
function lightName(ctx, doc, i) {
  const name = ((doc.pins || [])[i] || {}).name || String(i + 1);
  const key = 'light.name.' + name;
  const text = ctx.t(key);
  return text === key ? name : text;
}

function Orb({ ctx, light, color, brightness, small }) {
  const { html } = ctx;
  const on = !!light.on;
  const effect = light.mode === 'effect' ? light.effect : '';
  const tone = color || light.color;
  const style = on ? `--lt-c:${tone};--lt-t:${effectSeconds(effect, light.speed)};` +
      (effect ? (['breath', 'blink'].includes(effect) ? `background:${tone};` : '')
              : `background:${tone};box-shadow:0 0 ${small ? 14 : 36}px ${tone};`) +
      `filter:brightness(${0.35 + 0.65 * (brightness || light.brightness) / 100})` : '';
  return html`<div class=${'lt-orb' + (small ? ' sm' : '') + (light.kind === 'pixels' ? ' ring' : '') +
                           (on && effect ? ' fx-' + effect : '')} style=${style}></div>`;
}

function stateText(ctx, light) {
  if (!light.on) return ctx.t('light.off');
  return light.mode === 'effect' ? ctx.t('light.fx.' + light.effect) : ctx.t('light.fixed');
}

function PowerSwitch({ ctx, light, held, label }) {
  const { html, ui } = ctx;
  const [busy, setBusy] = ctx.useState(false);
  const power = async (value) => {
    setBusy(true);
    await ctx.act('power', { channel: light.i, value });
    setBusy(false);
  };
  return html`<${ui.Switch} big on=${!!light.on} busy=${busy} disabled=${held} onChange=${power}
    label=${label}/>`;
}

// Everything of one light: on and off when it is the only one, brightness, colour, effects.
function Editor({ ctx, doc, light, single }) {
  const { html, ui, t } = ctx;
  const i = light.i;
  const channel = (doc.pins || [])[i] || {};
  const held = !!channel.locked;
  // What the hand is choosing, shown at once; the document of the device takes over after.
  const [pick, setPick] = ctx.useState(null);
  const [bri, setBri] = ctx.useState(null);
  const [spd, setSpd] = ctx.useState(null);
  // Values that follow the finger: never two travelling, the last one always arrives.
  const sendColor = useThrottled(ctx, (hex) => ctx.stream('color', { channel: i, hex }, i), 150);
  const sendBri = useThrottled(ctx, (value) => ctx.stream('brightness', { channel: i, value }, i), 200);
  const sendSpd = useThrottled(ctx, (value) => ctx.stream('speed', { channel: i, value }, i), 200);
  ctx.useEffect(() => {
    setPick(null);
    setBri(null);
    setSpd(null);
  }, [light.color, light.brightness, light.speed]);

  const color = pick || light.color || '#ff0000';
  const brightness = bri !== null ? bri : light.brightness || 100;
  const speed = spd !== null ? spd : light.speed || 50;
  const effect = light.mode === 'effect' ? light.effect : '';
  const name = lightName(ctx, doc, i);

  return html`<div class="grid2">
      <${ui.Card} title=${single ? t('light.light') : name} icon="lightbulb">
        <div class="lt-hero">
          <${Orb} ctx=${ctx} light=${light} color=${color} brightness=${brightness}/>
          <div class="lt-state">
            <b>${stateText(ctx, light)}</b>
            ${single && html`<${PowerSwitch} ctx=${ctx} light=${light} held=${held}
              label=${light.on ? t('mod.on') : t('mod.off')}/>`}
            ${held && html`<small><${ui.Icon} name="lock" size=${13}/> ${t('mod.locked')}</small>`}
            ${light.kind === 'pixels' && html`<small>${t('light.leds', { n: light.count })}</small>`}
          </div>
        </div>
        <label class="lt-row">
          <${ui.Icon} name="sun" size=${18}/>
          <input class="lt-range" type="range" min="1" max="100" value=${brightness}
            aria-label=${t('light.brightness')}
            onInput=${(e) => { setBri(+e.target.value); sendBri(+e.target.value); }}/>
          <span>${brightness}%</span>
        </label>
      <//>
      <${ui.Card} title=${t('light.color')} icon="palette" sub=${t('light.colorSub')}>
        <div class="lt-pick">
          <input type="color" value=${color} aria-label=${t('light.color')}
            onInput=${(e) => { setPick(e.target.value); sendColor(e.target.value); }}/>
          <span class="lt-hex">${color.toUpperCase()}</span>
        </div>
        <div class="lt-swatches">
          ${SWATCHES.map((sw) => html`<button key=${sw} type="button" title=${sw}
            class=${'lt-sw' + (!effect && sw === color.toLowerCase() ? ' on' : '')}
            style=${'background:' + sw}
            onClick=${() => { setPick(sw); ctx.act('color', { channel: i, hex: sw }); }}></button>`)}
        </div>
      <//>
    </div>
    <${ui.Card} title=${t('light.effects')} icon="sparkles">
      <div class="lt-fx">
        ${(light.effects || []).map((id) => html`<button key=${id} type="button"
            class=${effect === id ? 'on' : ''} onClick=${() => ctx.act('effect', { channel: i, name: id })}>
          <${ui.Icon} name=${ICONS[id] || 'sparkles'} size=${26}/>${t('light.fx.' + id)}</button>`)}
      </div>
      <label class="lt-row">
        <${ui.Icon} name="gauge" size=${18}/>
        <input class="lt-range" type="range" min="1" max="100" value=${speed}
          aria-label=${t('light.speed')}
          onInput=${(e) => { setSpd(+e.target.value); sendSpd(+e.target.value); }}/>
        <span>${speed}</span>
      </label>
    <//>`;
}

function Home({ ctx }) {
  const { html, ui, t } = ctx;
  const doc = ctx.useDoc();
  const [sel, setSel] = ctx.useState(0);
  if (!doc) return html`<${ui.Spinner} size=${28}/>`;
  const lights = doc.lights || [];
  if (!lights.length) return html`<${ui.Empty}>${t('mod.nothing')}<//>`;
  const current = lights[Math.min(sel, lights.length - 1)];
  const single = lights.length === 1;

  return html`<style>${STYLE}</style>
    ${!single && html`<${ui.Card} title=${t('light.lights')} icon="lightbulb">
      <div class="lt-list">
        ${lights.map((l) => html`<div class="lt-item" key=${l.i}>
          <${Orb} ctx=${ctx} light=${l} small/>
          <div class="lt-name"><b>${lightName(ctx, doc, l.i)}</b><small>${stateText(ctx, l)}</small></div>
          <${PowerSwitch} ctx=${ctx} light=${l} held=${!!((doc.pins || [])[l.i] || {}).locked}/>
        </div>`)}
      </div>
      <div class="lt-pickone">
        <${ui.Segmented} value=${current.i} onChange=${setSel}
          options=${lights.map((l) => ({ id: l.i, label: lightName(ctx, doc, l.i),
                                        icon: l.kind === 'pixels' ? 'orbit' : 'lightbulb' }))}/>
      </div>
    <//>`}
    <${Editor} key=${current.i} ctx=${ctx} doc=${doc} light=${current} single=${single}/>
    <${ui.Card}>
      <${ui.Switch} on=${doc.restore} label=${t('light.restore')}
        onChange=${(value) => ctx.act('restore', { value }, t('saved'))}/>
    <//>`;
}

export default {
  uiApi: 1,
  texts: {
    it: {
      'light.light': 'Luce', 'light.lights': 'Luci', 'light.off': 'Spenta', 'light.fixed': 'Colore fisso',
      'light.brightness': 'Luminosità', 'light.speed': 'Velocità', 'light.leds': '{n} LED',
      'light.color': 'Colore', 'light.colorSub': 'Si vede subito sulla luce',
      'light.effects': 'Effetti',
      'light.fx.christmas': 'Natale', 'light.fx.rainbow': 'Arcobaleno', 'light.fx.breath': 'Respiro',
      'light.fx.blink': 'Lampeggio', 'light.fx.fade': 'Dissolvenza', 'light.fx.chase': 'Inseguimento',
      'light.fx.wheel': 'Arcobaleno rotante', 'light.fx.comet': 'Cometa', 'light.fx.sparkle': 'Scintille',
      'light.restore': 'Dopo un\'interruzione di corrente riparte com\'era',
      'light.name.Leaf': 'Foglia', 'light.name.Ring': 'Anello',
      'mod.rgb.pwmHz': 'Frequenza PWM (Hz)', 'mod.ganjaleaf.pwmHz': 'Frequenza PWM della foglia (Hz)',
      'cmd.color': 'Colore fisso: hex "#ff8000", oppure r, g e b. Accende la luce',
      'cmd.effect': 'Effetto: christmas, rainbow, breath, blink, fade; sui LED anche chase, wheel, comet, sparkle. Accende la luce',
      'cmd.brightness': 'Luminosità in percento, da 1 a 100',
      'cmd.speed': 'Velocità degli effetti, da 1 a 100',
      'cmd.power': 'Accende o spegne una luce, con l\'ultimo colore o effetto',
    },
    en: {
      'light.light': 'Light', 'light.lights': 'Lights', 'light.off': 'Off', 'light.fixed': 'Fixed colour',
      'light.brightness': 'Brightness', 'light.speed': 'Speed', 'light.leds': '{n} LEDs',
      'light.color': 'Colour', 'light.colorSub': 'The light follows at once',
      'light.effects': 'Effects',
      'light.fx.christmas': 'Christmas', 'light.fx.rainbow': 'Rainbow', 'light.fx.breath': 'Breath',
      'light.fx.blink': 'Blink', 'light.fx.fade': 'Fade', 'light.fx.chase': 'Chase',
      'light.fx.wheel': 'Turning rainbow', 'light.fx.comet': 'Comet', 'light.fx.sparkle': 'Sparkles',
      'light.restore': 'After a power cut it comes back as it was',
      'light.name.Leaf': 'Leaf', 'light.name.Ring': 'Ring',
      'mod.rgb.pwmHz': 'PWM frequency (Hz)', 'mod.ganjaleaf.pwmHz': 'PWM frequency of the leaf (Hz)',
    },
  },
  // Drawings from Lucide 0.460.0, ISC license.
  icons: {
    'tree-pine': '<path d="m17 14 3 3.3a1 1 0 0 1-.7 1.7H4.7a1 1 0 0 1-.7-1.7L7 14h-.3a1 1 0 0 1-.7-1.7L9 9h-.2A1 1 0 0 1 8 7.3L12 3l4 4.3a1 1 0 0 1-.8 1.7H15l3 3.3a1 1 0 0 1-.7 1.7H17Z"/><path d="M12 22v-3"/>',
    'rainbow': '<path d="M22 17a10 10 0 0 0-20 0"/><path d="M6 17a6 6 0 0 1 12 0"/><path d="M10 17a2 2 0 0 1 4 0"/>',
    'heart-pulse': '<path d="M19 14c1.49-1.46 3-3.21 3-5.5A5.5 5.5 0 0 0 16.5 3c-1.76 0-3 .5-4.5 2-1.5-1.5-2.74-2-4.5-2A5.5 5.5 0 0 0 2 8.5c0 2.3 1.5 4.05 3 5.5l7 7Z"/><path d="M3.22 12H9.5l.5-1 2 4.5 2-7 1.5 3.5h5.27"/>',
    'blend': '<circle cx="9" cy="9" r="7"/><circle cx="15" cy="15" r="7"/>',
    'palette': '<circle cx="13.5" cy="6.5" r=".5" fill="currentColor"/><circle cx="17.5" cy="10.5" r=".5" fill="currentColor"/><circle cx="8.5" cy="7.5" r=".5" fill="currentColor"/><circle cx="6.5" cy="12.5" r=".5" fill="currentColor"/><path d="M12 2C6.5 2 2 6.5 2 12s4.5 10 10 10c.926 0 1.648-.746 1.648-1.688 0-.437-.18-.835-.437-1.125-.29-.289-.438-.652-.438-1.125a1.64 1.64 0 0 1 1.668-1.668h1.996c3.051 0 5.555-2.503 5.555-5.554C21.965 6.012 17.461 2 12 2z"/>',
    'lightbulb': '<path d="M15 14c.2-1 .7-1.7 1.5-2.5 1-.9 1.5-2.2 1.5-3.5A6 6 0 0 0 6 8c0 1 .2 2.2 1.5 3.5.7.7 1.3 1.5 1.5 2.5"/><path d="M9 18h6"/><path d="M10 22h4"/>',
    'sparkles': '<path d="M9.937 15.5A2 2 0 0 0 8.5 14.063l-6.135-1.582a.5.5 0 0 1 0-.962L8.5 9.936A2 2 0 0 0 9.937 8.5l1.582-6.135a.5.5 0 0 1 .963 0L14.063 8.5A2 2 0 0 0 15.5 9.937l6.135 1.581a.5.5 0 0 1 0 .964L15.5 14.063a2 2 0 0 0-1.437 1.437l-1.582 6.135a.5.5 0 0 1-.963 0z"/><path d="M20 3v4"/><path d="M22 5h-4"/><path d="M4 17v2"/><path d="M5 18H3"/>',
    'orbit': '<circle cx="12" cy="12" r="3"/><circle cx="19" cy="5" r="2"/><circle cx="5" cy="19" r="2"/><path d="M10.4 21.9a10 10 0 0 0 9.941-15.416"/><path d="M13.5 2.1a10 10 0 0 0-9.841 15.416"/>',
    'star': '<path d="M11.525 2.295a.53.53 0 0 1 .95 0l2.31 4.679a2.123 2.123 0 0 0 1.595 1.16l5.166.756a.53.53 0 0 1 .294.904l-3.736 3.638a2.123 2.123 0 0 0-.611 1.878l.882 5.14a.53.53 0 0 1-.771.56l-4.618-2.428a2.122 2.122 0 0 0-1.973 0L6.396 21.01a.53.53 0 0 1-.77-.56l.881-5.139a2.122 2.122 0 0 0-.611-1.879L2.16 9.795a.53.53 0 0 1 .294-.906l5.165-.755a2.122 2.122 0 0 0 1.597-1.16z"/>',
  },
  tabs: [
    { id: 'home', icon: 'lightbulb', view: Home },
  ],
};
