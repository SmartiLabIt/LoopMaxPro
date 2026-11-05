// LoopMaxPro console - the interface a module brings with it.
// A module that wants more than the standard view gives the console one file: texts, icons,
// tabs of its own, a home of its own. The same file runs on the device and on the server: it
// sees no address and no HTTP, only the ctx below (docs/web-console.md).
//
//   export default {
//     uiApi: 1,
//     texts: { it: { 'mod.demo.counters': 'Contatori' }, en: { 'mod.demo.counters': 'Counters' } },
//     icons: { 'gauge-2': '<path d="..."/>' },
//     tabs: [
//       { id: 'counters', icon: 'activity', label: 'mod.demo.counters', view: Counters },
//       { id: 'home', mount(el, ctx) { ...; return () => { ... }; } },   // takes the home's place
//     ],
//   };
//
// view: a Preact component, drawn with ctx.html; it gets { ctx }. mount: anything else, drawn
// into el; what it returns (a function, or an object with destroy) runs when the tab closes.
// The build wraps the file and names the module: the file does not know where it is served.
//
// A module of the kind "bridge" can add to the LoopMaxPro space too. Its file is read when the
// console starts, and says what it adds with "system":
//
//   system: {
//     tabs: [{ id: 'extra', icon: 'star', label: 'sys.extra', need: 'extra', view: Extra }],
//     Banner,                          // a component above every page: it gets { ctx, go }
//     start(ctx) {},                   // once, when the console opens on a device
//     stopped: { word: 'text.key' },   // what to say of a module stopped for that reason
//     reasons: { word: 'text.key' },   // the same reason in two words, for a list
//   }
//
// need: the service that must be in the firmware for the tab to be there. go(id) opens a tab
// of the LoopMaxPro space.
import { h, html, render, useCallback, useEffect, useErrorBoundary, useMemo, useRef,
         useState } from '../vendor/preact-htm.js';
import { Badge, Btn, Card, Empty, Field, KV, Meter, Modal, Notice, Segmented, Select, Spinner,
         Stat, Switch, TextInput } from '../kit/ui.js';
import { CommandForm, SchemaForm, fieldLabel } from '../kit/form.js';
import { Icon, addIcons } from '../kit/icons.js';
import { useStore } from '../kit/store.js';
import { addShared, addTexts, getLang, t } from '../kit/i18n.js';
import { act, command, confirmDialog, deviceClock, docOf, loadCfg, loadDoc, store, toast,
         transport } from './live.js';
import { askPlace, precisePlace } from './place.js';

// The version of the contract between the console and the interface of a module.
export const UI_API = 1;
const UI_WAIT_MS = 15000;

const uiWaiting = {};   // name -> resolve, while the file travels

const isSys = (target) => typeof target === 'string' && target.startsWith('sys.');

// A document of the system, read now and again every everyMs while the component lives. Until it
// is there it is asked every 2 seconds: what the console loads at the start can take its place
// (on a small board the network of the device stayed empty for 20 seconds).
function useSysDoc(target, everyMs) {
  const doc = useStore(store, (s) => (isSys(target) ? s.docs[target] : undefined));
  const there = !!doc;
  useEffect(() => {
    if (!isSys(target)) return undefined;
    loadDoc(target).catch(() => {});
    const every = there ? everyMs : 2000;
    if (!every) return undefined;
    const timer = setInterval(() => loadDoc(target).catch(() => {}), every);
    return () => clearInterval(timer);
  }, [target, everyMs, there]);
  return doc;
}

// A file that arrived: checked, its texts and icons joined to the console's.
function acceptUi(entry) {
  const [name, def] = Array.isArray(entry) ? entry : [];
  if (!name) return;
  let ui = def;
  if (!def || typeof def !== 'object') {
    ui = { error: 'format' };
  } else if (def.uiApi !== UI_API) {
    ui = { error: 'api' };
  } else {
    if (def.texts) addTexts(def.texts);
    // Its phrases in a file in common with another module, and the phrases of the console it
    // uses with their keys (F31): the build puts the first, the module says the second.
    addShared('mod.' + name, def.shared, def.same);
    if (def.icons) addIcons(def.icons);
    ui = Object.assign({}, def, { tabs: Array.isArray(def.tabs) ? def.tabs : [] });
  }
  store.set((s) => ({ uis: Object.assign({}, s.uis, { [name]: ui }) }));
  if (uiWaiting[name]) {
    uiWaiting[name]();
    delete uiWaiting[name];
  }
}

// A module file is a script: it puts itself in window.LMP_UI. The files the page loaded
// before the console are taken now, the later ones as they come.
export function listenForModuleUi() {
  const early = Array.isArray(window.LMP_UI) ? window.LMP_UI : [];
  window.LMP_UI = { push: acceptUi };
  early.forEach(acceptUi);
}

// The console was stopped: nobody waits for an interface any more.
export function forgetModuleUi() {
  Object.keys(uiWaiting).forEach((name) => {
    uiWaiting[name]();
    delete uiWaiting[name];
  });
  Object.keys(started).forEach((name) => delete started[name]);
  Object.keys(bridges).forEach((name) => delete bridges[name]);
}

// Reads the interface of a module, once. Where it is, the transport knows: on the device a
// file of its console, on the server wherever the server keeps it. None: the standard view.
export function loadModuleUi(name) {
  const known = store.get().uis[name];
  if (known !== undefined || uiWaiting[name]) return;
  const api = transport();
  const url = api && api.moduleUi ? api.moduleUi(name) : null;
  if (!url) {
    store.set((s) => ({ uis: Object.assign({}, s.uis, { [name]: null }) }));
    return;
  }
  const failed = () => {
    if (!uiWaiting[name]) return;
    delete uiWaiting[name];
    store.set((s) => ({ uis: Object.assign({}, s.uis, { [name]: { error: 'load', tabs: [] } }) }));
  };
  const timer = setTimeout(failed, UI_WAIT_MS);
  uiWaiting[name] = () => clearTimeout(timer);
  // The file is the largest thing the page asks: through the lanes of the device, as a request.
  const load = () => new Promise((done) => {
    const script = document.createElement('script');
    script.src = url;
    script.async = true;
    script.onload = done;
    script.onerror = () => {
      failed();
      done();
    };
    document.head.appendChild(script);
  });
  if (api.lane) api.lane(load);
  else load();
}

// What the interface of a module gets from the console. Nothing here depends on where the
// console runs: the same calls on the device and on the server.
export function moduleCtx(name) {
  const target = 'mod.' + name;
  return {
    name, target, uiApi: UI_API,
    // 'local' on the device, 'cloud' on the server: for a text, never for a different path.
    where: () => (transport() ? transport().kind : 'local'),
    // Preact and htm, the ones of the console.
    h, html, render, useState, useEffect, useRef, useMemo, useCallback,
    // The pieces of the console, with its look.
    ui: { Badge, Btn, Card, Empty, Field, KV, Meter, Modal, Notice, Segmented, Select, Spinner,
          Stat, Switch, TextInput, Icon, SchemaForm, CommandForm },
    t, lang: getLang,
    label: (key) => fieldLabel(target, key),
    // The document of the module: now, in a component, or at every change.
    doc: () => store.get().docs[target] || null,
    useDoc: () => useStore(store, (s) => s.docs[target]),
    onDoc(fn) {
      let last = store.get().docs[target];
      if (last) fn(last);
      return store.subscribe((s) => {
        const d = s.docs[target];
        if (d && d !== last) {
          last = d;
          fn(d);
        }
      });
    },
    manifest: () => store.get().manifests[target] || null,
    // The time of the device, moved on in the browser. Null while it does not know it.
    clock: () => deviceClock(store.get()),
    useClock: () => useStore(store, deviceClock),
    // Commands of the module: { ok, status, result, field }. act() also says how it went.
    command: (cmd, args) => command(target, cmd, args),
    act: (cmd, args, okText) => act(target, cmd, args, okText),
    // A command that only reads, as the pages of a long list: it does not block the console and
    // more of them can travel together.
    read: (cmd, args) => command(target, cmd, args, { background: true }),
    // A value that follows a finger, a slider: never two travelling, the last one always
    // arrives, the console is not blocked. key: what the value is of, a channel.
    stream: (cmd, args, key = '') => command(target, cmd, args, { stream: key }),
    // The system, for a module that shows the device itself: a document of a 'sys.' target, in
    // a component, read again every everyMs (0: once); a command of it that does not block.
    useSysDoc: (sysTarget, everyMs = 0) => useSysDoc(sysTarget, everyMs),
    sysRead: (sysTarget, cmd, args) => (isSys(sysTarget)
      ? command(sysTarget, cmd, args, { background: true })
      : Promise.resolve({ ok: false, status: 'denied', field: '', result: {} })),
    // A command of the system that changes something, for a module that offers it among its
    // settings: it blocks and says how it went, as act() does.
    sysAct: (sysTarget, cmd, args, okText) => (isSys(sysTarget)
      ? act(sysTarget, cmd, args, okText)
      : Promise.resolve({ ok: false, status: 'denied', field: '', result: {} })),
    // True when the firmware has that service: 'mqtt', 'cloud'. The report says it.
    hasService: (service) => ((store.get().report || {}).services || [])
      .some((x) => x.name === service),
    // True while a command is with the device: the console blocks itself, a module needs it
    // only to draw something of its own.
    useBusy: () => useStore(store, (s) => !!s.busy),
    // The settings, without secrets; saving takes only what changed.
    config: () => loadCfg(target),
    saveConfig: (values) => act(target, 'config.set', values, t('saved')),
    toast, confirm: confirmDialog,
    // The window that asks where something is: the GPS of who looks, or coordinates written or
    // pasted. For something the module keeps: { title, now, command, args }; saving sends the
    // command to the module with args and the point (lat, lon). Without arguments: the device
    // itself. placed(): the device has a point of its own, not the city the network tells.
    askPlace: (what) => askPlace(what && what.command
      ? { title: what.title, now: what.now, target, name: what.command, args: what.args || {} }
      : { device: true }),
    placed: () => precisePlace(docOf(store.get(), 'sys.geo')),
    // 'AP' on its own access point, 'LAN' in a network: on the access point nobody needs to
    // know where the device is.
    mode: () => (store.get().state && store.get().state.mode) || '',
  };
}

// The tabs the interface of a module adds; its home takes the place of the standard one.
export function uiTabs(ui) {
  return ui && !ui.error ? ui.tabs.filter((x) => x && x.id && (x.view || x.mount)) : [];
}

// ---------------------------------------------------------------- the LoopMaxPro space

const started = {};   // name -> true: its start() ran since the console opened
const bridges = {};   // name -> true: a module of the kind "bridge"

// The interfaces read when the console starts: the ones of the bridges, which can add to the
// LoopMaxPro space.
export function loadSystemUis(modules) {
  let more = false;
  (modules || []).forEach((m) => {
    if (m.kind !== 'bridge') return;
    if (!bridges[m.name]) more = true;
    bridges[m.name] = true;
    loadModuleUi(m.name);
  });
  // A file that arrived before its module was known as a bridge: who draws looks again.
  if (more) store.set((s) => ({ uis: Object.assign({}, s.uis) }));
}

// What the interfaces of the bridges read so far add to the LoopMaxPro space:
// [{ name, system }]. The "system" of any other module is not looked at.
export function systemParts(uis) {
  return Object.keys(uis || {})
    .filter((name) => bridges[name] && uis[name] && !uis[name].error && uis[name].system)
    .map((name) => ({ name, system: uis[name].system }));
}

// Their tabs, for the services of this firmware: [{ name, tab }].
export function systemTabs(uis, services) {
  const tabs = [];
  systemParts(uis).forEach(({ name, system }) => {
    (Array.isArray(system.tabs) ? system.tabs : []).forEach((tab) => {
      if (tab && tab.id && tab.view && (!tab.need || services.includes(tab.need))) {
        tabs.push({ name, tab });
      }
    });
  });
  return tabs;
}

// The text for a module stopped for a reason one of them knows. Empty: nobody knows it.
export function stoppedText(uis, reason) {
  if (!reason) return '';
  const part = systemParts(uis).find(({ system }) => system.stopped && system.stopped[reason]);
  return part ? part.system.stopped[reason] : '';
}

// The same reason in two words, for the list of the modules. Empty: nobody knows it.
export function reasonText(uis, reason) {
  if (!reason) return '';
  const part = systemParts(uis).find(({ system }) => system.reasons && system.reasons[reason]);
  return part ? part.system.reasons[reason] : '';
}

// Runs, once, what each of them does when the console opens on a device.
export function startSystemParts(uis) {
  systemParts(uis).forEach(({ name, system }) => {
    if (started[name] || typeof system.start !== 'function') return;
    started[name] = true;
    try {
      system.start(moduleCtx(name));
    } catch (e) {
      // What breaks in the interface of a module stays there: the console goes on.
    }
  });
}

function Mounted({ tab, ctx }) {
  const el = useRef(null);
  useEffect(() => {
    const done = tab.mount(el.current, ctx);
    return () => {
      if (typeof done === 'function') done();
      else if (done && typeof done.destroy === 'function') done.destroy();
    };
  }, [tab]);
  return html`<div class="mod-ui" ref=${el}></div>`;
}

// A tab of the module. What breaks inside stays inside: the console goes on.
export function ModuleUiTab({ name, tab }) {
  const ctx = useMemo(() => moduleCtx(name), [name]);
  const [error, reset] = useErrorBoundary();
  if (error) {
    return html`<${Notice} kind="err" action=${html`<${Btn} small icon="refresh-cw"
        onClick=${reset}>${t('btn.retry')}<//>`}>
      ${t('mod.uiBroken', { msg: String(error.message || error) })}<//>`;
  }
  return tab.view ? h(tab.view, { ctx }) : html`<${Mounted} key=${tab.id} tab=${tab} ctx=${ctx}/>`;
}
