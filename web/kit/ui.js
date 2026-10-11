// LoopMaxPro console - the pieces every view is made of.
// The same pieces are given to the interface of the modules: a module looks like the rest of
// the console without a line of style of its own.
import { html, useEffect, useRef, useState } from '../vendor/preact-htm.js';
import { Icon } from './icons.js';
import { t } from './i18n.js';

// Draws the component again every period: clocks, times since.
export function useTick(ms = 1000) {
  const [, setN] = useState(0);
  useEffect(() => {
    const id = setInterval(() => setN((n) => n + 1), ms);
    return () => clearInterval(id);
  }, [ms]);
}

// What a press started, while it lasts: a handler that returns a promise keeps its control
// busy until the promise ends. So a button or a switch that sends a command shows it is
// working, and takes no other press, with nothing written where it is used.
export function usePress() {
  const [busy, setBusy] = useState(false);
  const alive = useRef(true);
  // Presses that are still going: quick ones after the first end at once, refused, and must
  // not end the first.
  const going = useRef(0);
  useEffect(() => () => { alive.current = false; }, []);
  const press = (fn, ...args) => {
    const r = fn(...args);
    if (r && typeof r.then === 'function') {
      going.current++;
      setBusy(true);
      const done = () => --going.current === 0 && alive.current && setBusy(false);
      r.then(done, done);
    }
    return r;
  };
  return [busy, press];
}

// A box with a title. actions: buttons on the right of the title.
export function Card({ title, icon, actions, children, cls = '', sub }) {
  return html`<section class=${'card ' + cls}>
    ${(title || actions) && html`<header class="card-h">
      ${icon && html`<span class="card-ico"><${Icon} name=${icon} size=${18}/></span>`}
      <div class="card-t"><h3>${title}</h3>${sub && html`<small>${sub}</small>`}</div>
      ${actions && html`<div class="card-a">${actions}</div>`}
    </header>`}
    <div class="card-b">${children}</div>
  </section>`;
}

// items: [{ id, label, icon, badge }]
export function Tabs({ items, active, onSelect, cls = '' }) {
  const bar = useRef(null);
  useEffect(() => {
    const el = bar.current && bar.current.querySelector('.tab.on');
    if (el && el.scrollIntoView) el.scrollIntoView({ block: 'nearest', inline: 'nearest' });
  }, [active]);
  return html`<nav class=${'tabs ' + cls} ref=${bar} role="tablist">
    ${items.map((it) => html`<button key=${it.id} role="tab" aria-selected=${it.id === active}
        class=${'tab' + (it.id === active ? ' on' : '')} onClick=${() => onSelect(it.id)}>
      ${it.icon && html`<${Icon} name=${it.icon} size=${18}/>`}
      <span>${it.label}</span>
      ${it.badge ? html`<i class="dot"></i>` : null}
    </button>`)}
  </nav>`;
}

// While what onChange started lasts, the switch stays where the hand put it and blinks.
export function Switch({ on, onChange, disabled = false, busy = false, label, big = false }) {
  const [own, press] = usePress();
  const [want, setWant] = useState(null);
  const going = busy || own;
  const change = (value) => {
    setWant(value);
    press(onChange, value);
  };
  return html`<label class=${'switch' + (big ? ' big' : '') + (going ? ' busy' : '') +
                            (disabled ? ' off' : '')}>
    <input type="checkbox" checked=${own && want !== null ? want : !!on} disabled=${disabled || going}
      onChange=${(e) => onChange && change(e.target.checked)}/>
    <span class="track"><span class="knob"></span></span>
    ${label && html`<span class="switch-l">${label}</span>`}
  </label>`;
}

// kind: ok, warn, err, info, off
export function Badge({ kind = 'info', children, icon, title }) {
  return html`<span class=${'badge ' + kind} title=${title}>
    ${icon && html`<${Icon} name=${icon} size=${13}/>`}${children}
  </span>`;
}

// The kind of badge for the state of a service or a module.
export function stateKind(state) {
  return { Ready: 'ok', Degraded: 'warn', Error: 'err', Waiting: 'info', Init: 'info',
           Starting: 'info', Stopped: 'off' }[state] || 'info';
}

export function StateBadge({ state, reason }) {
  return html`<${Badge} kind=${stateKind(state)} title=${reason}>${t('state.' + state)}<//>`;
}

// kind: primary, ghost, danger. busy: a spinner in place of the icon, and no clicks. It is
// busy by itself while what onClick started lasts, when onClick returns a promise.
export function Btn({ kind = 'ghost', icon, onClick, busy = false, disabled = false, children,
                      small = false, title, type = 'button', ...rest }) {
  const [own, press] = usePress();
  const going = busy || own;
  // data-* attributes reach the button: scripts find it by them, not by its text.
  const data = {};
  for (const k of Object.keys(rest)) if (k.startsWith('data-')) data[k] = rest[k];
  return html`<button type=${type} title=${title} ...${data}
      class=${'btn ' + kind + (small ? ' sm' : '') + (children ? '' : ' only') + (going ? ' busy' : '')}
      disabled=${disabled || going} onClick=${onClick && ((e) => press(onClick, e))}>
    ${going ? html`<${Spinner}/>` : icon && html`<${Icon} name=${icon} size=${small ? 15 : 17}/>`}
    ${children && html`<span>${children}</span>`}
  </button>`;
}

export function Spinner({ size = 16 }) {
  return html`<span class="spin" style=${`width:${size}px;height:${size}px`}></span>`;
}

// rows: [[label, value], ...]. A row with a null value is left out.
export function KV({ rows }) {
  return html`<dl class="kv">
    ${rows.filter((r) => r && r[1] !== null && r[1] !== undefined).map((r) => html`
      <div key=${r[0]}><dt>${r[0]}</dt><dd>${r[1]}</dd></div>`)}
  </dl>`;
}

// A bar that fills: memory, space, progress.
export function Meter({ value, max = 100, label, detail, warnAt = 80, errAt = 92 }) {
  const pct = max ? Math.max(0, Math.min(100, (value * 100) / max)) : 0;
  const kind = pct >= errAt ? 'err' : pct >= warnAt ? 'warn' : 'ok';
  return html`<div class="meter">
    ${(label || detail) && html`<div class="meter-l"><span>${label}</span><b>${detail}</b></div>`}
    <div class=${'meter-t ' + kind}><i style=${`width:${pct.toFixed(1)}%`}></i></div>
  </div>`;
}

export function Empty({ icon = 'info', children }) {
  return html`<div class="empty"><${Icon} name=${icon} size=${28}/><p>${children}</p></div>`;
}

// Something the user should know before going on.
export function Notice({ kind = 'info', icon, children, action }) {
  const ico = icon || { warn: 'triangle-alert', err: 'triangle-alert', ok: 'check' }[kind] || 'info';
  return html`<div class=${'notice ' + kind}>
    <${Icon} name=${ico} size=${18}/><div class="notice-t">${children}</div>${action}
  </div>`;
}

export function Modal({ title, icon, onClose, children, footer, wide = false }) {
  useEffect(() => {
    const onKey = (e) => e.key === 'Escape' && onClose && onClose();
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [onClose]);
  return html`<div class="modal-bg" onClick=${(e) => e.target === e.currentTarget && onClose &&
                                                   onClose()}>
    <div class=${'modal' + (wide ? ' wide' : '')} role="dialog" aria-modal="true">
      <header class="modal-h">
        ${icon && html`<${Icon} name=${icon} size=${20}/>`}<h3>${title}</h3>
        ${onClose && html`<button class="btn ghost only" onClick=${onClose}
                                  aria-label=${t('btn.close')}><${Icon} name="x"/></button>`}
      </header>
      <div class="modal-b">${children}</div>
      ${footer && html`<footer class="modal-f">${footer}</footer>`}
    </div>
  </div>`;
}

// ---------------------------------------------------------------- fields

let fieldSeq = 0;

function useFieldId() {
  const ref = useRef(null);
  if (!ref.current) ref.current = 'f' + ++fieldSeq;
  return ref.current;
}

export function Field({ label, hint, error, children, id }) {
  return html`<div class=${'field' + (error ? ' bad' : '')}>
    ${label && html`<label for=${id}>${label}</label>`}
    ${children}
    ${(error || hint) && html`<small>${error || hint}</small>`}
  </div>`;
}

export function TextInput({ label, value, onInput, hint, error, placeholder, type = 'text',
                            min, max, step, maxLength, autoFocus = false, disabled = false,
                            autocomplete = 'off' }) {
  const id = useFieldId();
  return html`<${Field} label=${label} hint=${hint} error=${error} id=${id}>
    <input id=${id} class="inp" type=${type} value=${value === undefined ? '' : value}
      placeholder=${placeholder} min=${min} max=${max} step=${step} maxLength=${maxLength}
      autoFocus=${autoFocus} disabled=${disabled} autocomplete=${autocomplete}
      onInput=${(e) => onInput(e.target.value)}/>
  <//>`;
}

// A password is never shown by the device: isSet tells whether one exists.
export function PasswordInput({ label, value, onInput, hint, error, isSet, autoFocus = false,
                                autocomplete = 'new-password' }) {
  const id = useFieldId();
  const [shown, setShown] = useState(false);
  const placeholder = isSet === undefined ? '' : isSet ? t('pw.set') : t('pw.unset');
  return html`<${Field} label=${label} hint=${hint} error=${error} id=${id}>
    <div class="inp-row">
      <input id=${id} class="inp" type=${shown ? 'text' : 'password'} value=${value || ''}
        placeholder=${placeholder} autoFocus=${autoFocus} autocomplete=${autocomplete}
        onInput=${(e) => onInput(e.target.value)}/>
      <button type="button" class="btn ghost only" onClick=${() => setShown(!shown)}
        aria-label=${t('pw.show')}><${Icon} name=${shown ? 'eye-off' : 'eye'}/></button>
    </div>
  <//>`;
}

// options: [{ id, label }]
export function Select({ label, value, options, onChange, hint, disabled = false }) {
  const id = useFieldId();
  return html`<${Field} label=${label} hint=${hint} id=${id}>
    <select id=${id} class="inp" value=${value} disabled=${disabled}
      onChange=${(e) => onChange(e.target.value)}>
      ${options.map((o) => html`<option key=${o.id} value=${o.id}>${o.label}</option>`)}
    </select>
  <//>`;
}

// A choice among few, drawn as buttons side by side.
export function Segmented({ value, options, onChange, disabled = false }) {
  const [own, press] = usePress();
  return html`<div class=${'seg' + (own ? ' busy' : '')}>
    ${options.map((o) => html`<button key=${o.id} type="button" disabled=${disabled || own}
        class=${o.id === value ? 'on' : ''} onClick=${() => o.id !== value && press(onChange, o.id)}>
      ${o.icon && html`<${Icon} name=${o.icon} size=${16}/>`}${o.label}
    </button>`)}
  </div>`;
}

// Signal strength as four bars.
export function Bars({ level }) {
  return html`<span class=${'bars b' + level} aria-label=${level + '/4'}>
    <i></i><i></i><i></i><i></i>
  </span>`;
}

// A value with its name, large: the tiles of the overview.
export function Stat({ icon, label, value, sub, kind = '' }) {
  return html`<div class=${'stat ' + kind}>
    <span class="stat-ico"><${Icon} name=${icon} size=${20}/></span>
    <div><small>${label}</small><b>${value}</b>${sub && html`<span>${sub}</span>`}</div>
  </div>`;
}

// Any JSON, readable: the raw documents in the services view.
export function JsonView({ value }) {
  return html`<pre class="json">${JSON.stringify(value, null, 2)}</pre>`;
}
