// LoopMaxPro console - the frame: header with the live state, the choice of the module, the
// LoopMaxPro space, access, restart, notices.
import { h, html, useEffect, useRef, useState } from '../vendor/preact-htm.js';
import { Bars, Btn, Modal, Notice, PasswordInput, Spinner, stateKind, useTick } from '../kit/ui.js';
import { Icon } from '../kit/icons.js';
import { prefs, useStore } from '../kit/store.js';
import { browserZone, zoneRules } from '../kit/tz.js';
import { t } from '../kit/i18n.js';
import { fmtDeviceTime, rssiBars } from '../kit/format.js';
import { act, awaitRestart, boot, command, confirmDialog, deviceClock, docOf, loadDoc, login, logout,
         store, toast, transport } from './live.js';
import { moduleCtx, systemParts } from './module-ui.js';

export function Brand({ small = false }) {
  return html`<span class=${'brand' + (small ? ' sm' : '')}>
    <span class="brand-mark"><${Icon} name="cpu" size=${small ? 18 : 26}/></span>
    <span class="brand-t">LoopMax<b>Pro</b></span>
  </span>`;
}

// ---------------------------------------------------------------- header

function StatusChips() {
  useTick(1000);
  const net = useStore(store, (s) => docOf(s, 'sys.net'));
  const clock = useStore(store, deviceClock);
  const mqtt = useStore(store, (s) => s.docs['sys.mqtt']);
  const cloud = useStore(store, (s) => s.docs['sys.cloud']);
  const online = useStore(store, (s) => s.online);
  const sta = net && net.sta;
  const chips = [];
  if (net) {
    if (sta && sta.connected) {
      chips.push(html`<span class="st ok" title=${sta.ssid + ' · ' + sta.ip + ' · ' + sta.rssi + ' dBm'}>
        <${Bars} level=${rssiBars(sta.rssi)}/><span class="st-t">${sta.ssid}</span></span>`);
    } else {
      chips.push(html`<span class=${'st ' + (net.state === 'fallback' ? 'warn' : 'info')}
          title=${net.ap ? net.ap.ssid + ' · ' + net.ap.ip : ''}>
        <${Icon} name="router" size=${16}/><span class="st-t">AP</span></span>`);
    }
    if (net.internetChecked !== undefined) {
      chips.push(html`<span class=${'st ' + (net.internet ? 'ok' : net.internetChecked ? 'err' : 'off')}
          title=${net.internet ? t('st.internet') : t('st.noInternet')}>
        <${Icon} name="globe" size=${16}/></span>`);
    }
  }
  if (mqtt && mqtt.enabled) {
    chips.push(html`<span class=${'st ' + (mqtt.connected ? 'ok' : 'warn')} title="MQTT">
      <${Icon} name="radio" size=${16}/></span>`);
  }
  if (cloud && cloud.attached) {
    chips.push(html`<span class=${'st ' + (cloud.connected ? 'ok' : 'warn')} title="Cloud">
      <${Icon} name=${cloud.connected ? 'cloud' : 'cloud-off'} size=${16}/></span>`);
  }
  chips.push(clock
    ? html`<span class="st" title=${clock.tz}><${Icon} name="clock" size=${16}/>
        <span class="st-t mono">${fmtDeviceTime(clock.unix, clock.offset, false)}</span></span>`
    : html`<span class="st warn" title=${t('clock.unknown')}><${Icon} name="clock" size=${16}/></span>`);
  chips.push(html`<span class=${'live ' + (online ? 'on' : 'off')}
    title=${online ? t('st.live') : t('st.offline')}></span>`);
  return html`<div class="chips-st">${chips}</div>`;
}

// The module being shown, and the others to choose from.
function ModulePicker({ modules, current, onPick }) {
  const [open, setOpen] = useState(false);
  const box = useRef(null);
  const states = useStore(store, (s) => s.report && s.report.modules && s.report.modules.list);
  useEffect(() => {
    if (!open) return undefined;
    const close = (e) => {
      if (box.current && !e.composedPath().includes(box.current)) setOpen(false);
    };
    window.addEventListener('pointerdown', close);
    return () => window.removeEventListener('pointerdown', close);
  }, [open]);
  const cur = modules.find((m) => m.name === current);
  if (!cur) return null;
  const stateOf = (name) => {
    const m = states && states.find((x) => x.name === name);
    return m ? m.state : 'Ready';
  };
  const many = modules.length > 1;
  return html`<div class="picker" ref=${box}>
    <button class=${'picker-b' + (many ? '' : ' single')} onClick=${() => many && setOpen(!open)}>
      <span class="emo">${cur.icon}</span><span class="picker-t">${cur.title}</span>
      ${many && html`<${Icon} name="chevron-down" size=${16}/>`}
    </button>
    ${open && html`<ul class="picker-l">${modules.map((m) => html`<li key=${m.name}>
      <button class=${m.name === current ? 'on' : ''} onClick=${() => {
        setOpen(false);
        onPick(m.name);
      }}>
        <span class="emo">${m.icon}</span><span>${m.title}</span>
        <i class=${'dot ' + stateKind(stateOf(m.name))}></i>
      </button></li>`)}</ul>`}
  </div>`;
}

export function Header({ route, modules, onModule, onSystem, onBack }) {
  const state = useStore(store, (s) => s.state);
  const report = useStore(store, (s) => s.report);
  const name = (report && report.device.name) || (state && (state.name || state.host)) || '';
  const inSystem = route.space === 'sys';
  return html`<header class="top">
    <div class="top-l">
      <${Brand} small/>
      <span class="dev" title=${state && state.host}>${name}</span>
      ${!inSystem && modules.length > 0 && html`<${ModulePicker} modules=${modules}
         current=${route.mod} onPick=${onModule}/>`}
      ${inSystem && html`<span class="where"><${Icon} name="settings" size=${16}/>${t('sys.title')}</span>`}
    </div>
    <div class="top-s"><${StatusChips}/></div>
    <div class="top-r">
      ${inSystem
        ? modules.length > 0 && html`<${Btn} icon="arrow-left" onClick=${onBack}>
            <span class="hide-sm">${t('sys.back')}</span><//>`
        : html`<${Btn} icon="settings" onClick=${onSystem} title=${t('sys.title')}>
            <span class="hide-sm">${t('sys.title')}</span><//>`}
      ${state && state.auth && state.auth.required && html`<${Btn} icon="log-out"
         title=${t('btn.logout')} onClick=${logout}/>`}
    </div>
  </header>`;
}

// ---------------------------------------------------------------- banners

// A device still on UTC, opened from a phone or a PC in another zone: the zone of the browser
// is offered, until it is taken or refused in this browser. The rules are found by the browser,
// for any zone of the world. Null when there is nothing to offer.
function zoneToOffer(clock, state) {
  const here = browserZone();
  if (!clock || clock.tz !== 'UTC' || !here || here === 'UTC' || here.startsWith('Etc/')) return null;
  if (prefs.get(zoneKey(state), '') === here) return null;
  const rules = zoneRules(here);
  return rules ? { zone: here, rules } : null;
}

function zoneKey(state) {
  return 'tzNo.' + ((state && state.host) || '');
}

function apKey(state) {
  return 'apOk.' + ((state && state.host) || '');
}

// The update the server offers to a device with the bridge, as the
// remote console shows it: a banner under the header with its button. Only on the device: the
// remote console has its own banner, and a second one would be a duplicate.
// What the server offers now: the device asks it, the answer comes in a few seconds.
async function askServer() {
  const r = await command('sys.cloud', 'checkUpdate', {});
  if (!r.ok) {
    toast(t('status.' + r.status), 'err', 5000);
    return null;
  }
  for (let i = 0; i < 40; i++) {
    const doc = await loadDoc('sys.cloud').catch(() => null);
    const detail = doc && doc.detail;
    if (detail && !detail.updateChecking) return detail.update || null;
    await new Promise((resolve) => setTimeout(resolve, 500));
  }
  return null;
}

function updateOffered(cloud) {
  const update = cloud && cloud.detail && cloud.detail.update;
  return update && transport().kind === 'local' ? update : null;
}

// What an update brings: a newer LoopMaxPro, a newer module, or both. The device says
// it in "changes": [{ title, to, from }]. "Zappy 2 1.0.1 (now 1.0.0)", one after the other.
export function updateWhat(update, installed) {
  const changes = Array.isArray(update.changes) && update.changes.length ? update.changes
    : [{ title: 'LoopMaxPro', to: update.version, from: installed }];
  return changes.map((c) => t('ban.updateItem', { title: c.title, to: c.to, from: c.from })).join(', ');
}

function UpdateBanner({ update }) {
  const report = useStore(store, (s) => s.report);
  const [busy, setBusy] = useState(false);
  const from = (report && report.device.fw) || '';
  const what = updateWhat(update, from);
  if (update.kind !== 'app') {
    return html`<${Notice} icon="download" action=${html`<${Btn} small icon="globe"
        onClick=${() => window.open('https://smartilab.it/firmware/', '_blank', 'noopener')}>
        ${t('ban.updateSite')}<//>`}>
      ${t('ban.updateUsb', { what })}<//>`;
  }
  // The server is asked again first: what is confirmed is what it offers now. When the offer
  // changed, the banner shows the new one and nothing starts.
  const start = async () => {
    setBusy(true);
    const now = await askServer();
    setBusy(false);
    if (!now || now.kind !== 'app' || updateWhat(now, from) !== what) return;
    const yes = await confirmDialog({ title: t('upd.title'),
                                      text: updateWhat(now, from) + '. ' + t('upd.text'),
                                      okLabel: t('ban.updateOk') });
    if (!yes) return;
    setBusy(true);
    const r = await act('sys.cloud', 'update', {});
    setBusy(false);
    if (r.ok) awaitRestart('update');
  };
  return html`<${Notice} icon="download" action=${html`<${Btn} small kind="primary" icon="download"
      busy=${busy} onClick=${start}>${t('ban.updateOk')}<//>`}>
    ${t('ban.update', { what })}<//>`;
}

export function Banners({ go }) {
  const online = useStore(store, (s) => s.online);
  const state = useStore(store, (s) => s.state);
  const net = useStore(store, (s) => docOf(s, 'sys.net'));
  const clock = useStore(store, (s) => docOf(s, 'sys.clock'));
  const update = useStore(store, (s) => updateOffered(s.docs['sys.cloud']));
  const uis = useStore(store, (s) => s.uis);
  const [, redraw] = useState(0);
  const out = [];
  if (!online) out.push(html`<${Notice} kind="err" icon="wifi-off">${t('ban.offline')}<//>`);
  if (online && update) out.push(html`<${UpdateBanner} update=${update}/>`);
  // What a bridge adds: a notice of its own, with the way to its tab.
  systemParts(uis).forEach(({ name, system }) => {
    if (system.Banner) out.push(h(system.Banner, { ctx: moduleCtx(name), go }));
  });
  if (state && state.fallback) out.push(html`<${Notice} kind="warn">${t('ban.fallback')}<//>`);
  // A device kept in access point on purpose: the banner can be hidden, in this browser, for
  // this device.
  if (state && state.mode === 'AP' && net && net.sta && !net.sta.ssid &&
      !prefs.get(apKey(state), false)) {
    const hide = () => {
      prefs.set(apKey(state), true);
      redraw((n) => n + 1);
    };
    out.push(html`<${Notice} icon="wifi" action=${html`<span class="row">
        <${Btn} small kind="primary" onClick=${() => go('network')}>${t('ban.setup')}<//>
        <${Btn} small data-ban="ap-hide" onClick=${hide}>${t('ban.hide')}<//></span>`}>
      ${t('ban.firstRun')}<//>`);
  }
  const offer = online ? zoneToOffer(clock, state) : null;
  if (offer) {
    const use = () => act('sys.clock', 'set', { tz: offer.zone, posix: offer.rules },
                          t('ban.zoneSet', { zone: offer.zone }))
      .then(() => loadDoc('sys.clock').catch(() => {}));
    const no = () => {
      prefs.set(zoneKey(state), offer.zone);
      redraw((n) => n + 1);
    };
    out.push(html`<${Notice} icon="clock" action=${html`<span class="row">
        <${Btn} small kind="primary" onClick=${use}>${t('ban.zoneUse', { zone: offer.zone })}<//>
        <${Btn} small onClick=${no}>${t('no')}<//></span>`}>${t('ban.zoneUtc', { zone: offer.zone })}<//>`);
  }
  return out.length ? html`<div class="banners">${out}</div>` : null;
}

// ---------------------------------------------------------------- notices and dialogs

export function ToastHost() {
  const toasts = useStore(store, (s) => s.toasts);
  return html`<div class="toasts" aria-live="polite">
    ${toasts.map((x) => html`<div key=${x.id} class=${'toast ' + x.kind}>
      <${Icon} name=${x.kind === 'ok' ? 'check' : x.kind === 'err' ? 'triangle-alert' : 'info'} size=${16}/>
      <span>${x.text}</span></div>`)}
  </div>`;
}

export function DialogHost() {
  const d = useStore(store, (s) => s.dialog);
  if (!d) return null;
  return html`<${Modal} title=${d.title} icon=${d.danger ? 'triangle-alert' : 'circle-help'}
      onClose=${() => d.done(false)}
      footer=${html`<${Btn} onClick=${() => d.done(false)}>${t('btn.cancel')}<//>
        <${Btn} kind=${d.danger ? 'danger' : 'primary'} icon="check" onClick=${() => d.done(true)}>
          ${d.okLabel}<//>`}>
    <p>${d.text}</p>
  <//>`;
}

// ---------------------------------------------------------------- whole-page states

export function Splash() {
  return html`<div class="full"><${Brand}/><${Spinner} size=${30}/></div>`;
}

export function BootError() {
  const code = useStore(store, (s) => s.bootError);
  return html`<div class="full">
    <${Brand}/>
    <div class="full-card">
      <${Icon} name="wifi-off" size=${34}/>
      <h3>${t('boot.noAnswer')}</h3>
      <p class="muted">${t('boot.noAnswerText')} (${code})</p>
      <${Btn} kind="primary" icon="refresh-cw" onClick=${boot}>${t('btn.retry')}<//>
    </div>
  </div>`;
}

export function Restarting() {
  const reason = useStore(store, (s) => s.restartReason);
  return html`<div class="full">
    <${Brand}/>
    <div class="full-card">
      ${reason === 'lost' ? html`<${Icon} name="triangle-alert" size=${34}/>` : html`<${Spinner} size=${34}/>`}
      <h3>${t('restart.' + reason)}</h3>
      <p class="muted">${t('restart.' + reason + 'Text')}</p>
      ${reason === 'lost' && html`<${Btn} kind="primary" icon="refresh-cw"
        onClick=${() => location.reload()}>${t('btn.retry')}<//>`}
    </div>
  </div>`;
}

export function Login() {
  const state = useStore(store, (s) => s.state);
  const [psw, setPsw] = useState('');
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState('');
  const submit = async (e) => {
    e.preventDefault();
    if (!psw) return;
    setBusy(true);
    setError('');
    try {
      await login(psw);
    } catch (err) {
      setError(err.status === 429 ? t('login.wait') : err.code === 'denied' ? t('login.wrong')
               : t('status.' + err.code));
    }
    setBusy(false);
  };
  return html`<div class="full">
    <${Brand}/>
    <form class="full-card login" onSubmit=${submit}>
      <${Icon} name="lock" size=${30}/>
      <h3>${(state && (state.name || state.host)) || ''}</h3>
      <p class="muted small">${t('login.user')}: <b>${(state && state.auth && state.auth.user) || 'admin'}</b></p>
      <input type="text" name="username" autocomplete="username" hidden
        value=${(state && state.auth && state.auth.user) || 'admin'}/>
      <${PasswordInput} label=${t('login.password')} value=${psw} onInput=${setPsw} autoFocus
        autocomplete="current-password" error=${error}/>
      <${Btn} kind="primary" type="submit" icon="key-round" busy=${busy} disabled=${!psw}>${t('login.enter')}<//>
      <p class="muted small">${t('login.help')}</p>
    </form>
  </div>`;
}

export function Footer() {
  const report = useStore(store, (s) => s.report);
  if (!report) return null;
  const d = report.device;
  return html`<footer class="foot">
    <span>LoopMaxPro ${d.fw}</span><span>${d.board}</span><span>${report.system.chip}</span>
    <a href="https://smartilab.it" target="_blank" rel="noopener">smartilab.it</a>
  </footer>`;
}
