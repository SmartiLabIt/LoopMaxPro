// LoopMaxPro console - what the console knows of the device, kept up to date.
// One report at the start, then the events: an event says what changed, and only the
// document that changed is read again. Nothing asks the device again and again, except the
// memory figures while the overview is open.
import { createStore } from '../kit/store.js';
import { setLang, t } from '../kit/i18n.js';
import { ApiError } from './transport.js';

// Codes of the device, in the order of lmp::Status: the events carry the number.
export const STATUS_NAMES = ['ok', 'pending', 'busy', 'invalid', 'not_found', 'no_mem', 'io',
                             'timeout', 'unsupported', 'denied', 'error'];
const LOG_KEEP = 500;

// What the console knows of a device, before it knows anything: also what it goes back to
// when it is stopped, to be started on another device.
const initial = () => ({
  boot: 'loading',     // loading, login, ready, error, restarting
  bootError: '',
  restartReason: '',
  lang: 'en',
  state: null,         // GET /api/state: public
  report: null,        // GET /api/report
  docs: {},            // target -> document
  cfgs: {},            // target -> settings
  manifests: {},       // target -> commands and schemas
  uis: {},             // module -> its own interface; null without one; { error } when it failed
  log: [],             // newest last
  logSeq: 0,
  online: true,        // the live stream is open
  ota: null,           // { percent, bytes } while an image arrives
  toasts: [],
  dialog: null,
  place: null,         // what the window of the position is asking for (place.js)
  busy: null,          // { target, name } while a command of the user is with the device
});

export const store = createStore(initial());

let api = null;
const waiting = new Map();   // ref of an asynchronous command -> resolve
// A command that ends at once tells its end before its answer is read: the ends nobody waited
// for yet are kept a few seconds, ref -> [status, when]. Without this the caller waited a minute.
const ended = new Map();
const refreshTimers = {};

export function useTransport(transport) {
  api = transport;
  api.onAuthLost = () => {
    if (api === transport && store.get().boot === 'ready') store.set({ boot: 'login' });
  };
}

// An answer that arrives after the console was stopped, or started on another device, is of
// the device of before: it must not enter what the console knows of the new one. Every
// function that asks and then writes takes the transport first and checks it after.
const still = (transport) => api !== null && api === transport;

export function transport() {
  return api;
}

// ---------------------------------------------------------------- reading

export async function loadState() {
  const from = api;
  const state = await from.get('state', 6000);
  if (!still(from)) return state;
  // A device short of memory says how many requests it holds at once.
  from.lanes = state.lanes || 0;
  setLang(state.lang);
  store.set({ state, lang: state.lang });
  return state;
}

// Parts of the report and the documents they are: a new report is newer than them.
const REPORT_PARTS = { 'sys.system': 'system', 'sys.net': 'net', 'sys.clock': 'clock',
                       'sys.storage': 'storage', 'sys.geo': 'geo', 'sys.modules': 'modules' };

// The board answers 507 when an answer does not fit in its memory: 3 KB on ESP8266.
// The report is then asked in two parts.
async function readReport(from) {
  try {
    return await from.get('report');
  } catch (e) {
    if (e.status !== 507) throw e;
  }
  const main = await from.get('report?parts=device,system,storage,clock,net,geo,modules');
  const services = await from.get('report?parts=services');
  return Object.assign(main, { services: services.services || [] });
}

export async function loadReport() {
  const from = api;
  const report = await readReport(from);
  if (!still(from)) return report;
  report._at = Date.now();
  store.set((s) => {
    const docs = Object.assign({}, s.docs);
    Object.keys(REPORT_PARTS).forEach((target) => delete docs[target]);
    return { report, docs };
  });
  return report;
}

const inFlight = new Map();

// The same request asked twice while the first is travelling gets the first answer.
function once(key, run) {
  if (inFlight.has(key)) return inFlight.get(key);
  const p = run().finally(() => inFlight.delete(key));
  inFlight.set(key, p);
  return p;
}

export function loadDoc(target) {
  return once('doc:' + target, async () => {
    const from = api;
    const doc = await from.get('doc/' + target);
    if (!still(from)) return doc;
    doc._at = Date.now();
    store.set((s) => ({ docs: Object.assign({}, s.docs, { [target]: doc }) }));
    return doc;
  });
}

export async function loadCfg(target) {
  const from = api;
  const cfg = await from.get('cfg/' + target);
  if (!still(from)) return cfg;
  store.set((s) => ({ cfgs: Object.assign({}, s.cfgs, { [target]: cfg }) }));
  return cfg;
}

// Commands and schemas do not change while the firmware runs: read once.
export function loadManifest(target) {
  const known = store.get().manifests[target];
  if (known) return Promise.resolve(known);
  return once('manifest:' + target, async () => {
    const from = api;
    let manifest;
    try {
      manifest = await from.get('manifest/' + target);
    } catch (e) {
      if (e.status !== 507) throw e;
      // Without the help texts: the commands still have their names and arguments.
      manifest = await from.get('manifest/' + target + '?help=0');
    }
    if (!still(from)) return manifest;
    store.set((s) => ({ manifests: Object.assign({}, s.manifests, { [target]: manifest }) }));
    return manifest;
  });
}

// Reads a document a moment later: many events close together become one request.
export function refreshSoon(what, delay = 150) {
  clearTimeout(refreshTimers[what]);
  refreshTimers[what] = setTimeout(() => {
    delete refreshTimers[what];
    if (!api) return;
    const run = what === 'report' ? loadReport()
      : what === 'state' ? loadState()
      : what.startsWith('cfg:') ? loadCfg(what.slice(4))
      : loadDoc(what);
    run.catch(() => {});
  }, delay);
}

// The document of a target: what the report has, replaced by the last one read.
export function docOf(s, target) {
  if (s.docs[target]) return s.docs[target];
  const r = s.report;
  if (!r) return null;
  const part = REPORT_PARTS[target];
  return part ? r[part] || null : null;
}

// The clock of the device now: the last time read, moved on by the time passed since.
// Null while the device does not know what time it is.
export function deviceClock(s) {
  const c = docOf(s, 'sys.clock');
  if (!c || !c.unix || !c.synced) return null;
  const at = s.docs['sys.clock'] ? s.docs['sys.clock']._at : s.report && s.report._at;
  const passed = at ? Math.floor((Date.now() - at) / 1000) : 0;
  return { unix: c.unix + passed, offset: c.offset || 0, tz: c.tz };
}

// ---------------------------------------------------------------- commands

// One command at a time. A board is slow, and more so through the server: a button pressed
// again and again while the first command travels piles commands up on it. So every command
// passes here, those of the console and those of the interface of any module:
//   - a command of the user takes the console (store.busy) from when it leaves to when the
//     device has done it and the console shows the result: no other press is taken (app.js,
//     console.css). A board on the same network answers in 70 ms, too little to be seen, and
//     what it did arrives a moment later with an event: so the block lasts BLOCK_MIN_MS at
//     least, and for a module until its document is read again (BLOCK_DOC_MS at most);
//   - a second one that arrives all the same (the keyboard, the code of a module) is not sent:
//     it gets { ok: false, status: 'wait' }, and nothing is said;
//   - { stream: key }: a value that follows a finger, a slider. Never two travelling: while one
//     does, only the last value is kept, and it leaves when the first is back. No block;
//   - { background: true }: what the console asks by itself, not a press. No block.
const BLOCK_MIN_MS = 450;
const BLOCK_DOC_MS = 1500;
const streams = new Map();   // target, command, key -> { next } while a value travels

// Resolves when the console has the result of a command of a module on the page: its document
// read again after the command. A command that changes nothing has no new document: the wait
// ends by itself.
function settled(target, before, started) {
  return new Promise((resolve) => {
    let off = null;
    let timer = null;
    const end = () => {
      if (off) off();
      clearTimeout(timer);
      const left = BLOCK_MIN_MS - (Date.now() - started);
      if (left > 0) setTimeout(resolve, left);
      else resolve();
    };
    if (!target.startsWith('mod.') || store.get().docs[target] !== before) {
      end();
      return;
    }
    timer = setTimeout(end, BLOCK_DOC_MS);
    off = store.subscribe((s) => {
      if (s.docs[target] !== before) end();
    });
  });
}

export async function command(target, name, args, opts = {}) {
  if (opts.stream !== undefined) return streamCommand(target, name, args, String(opts.stream));
  if (opts.background) return send(target, name, args);
  if (store.get().busy) return { ok: false, status: 'wait', field: '', result: {} };
  const mine = { target, name };
  const started = Date.now();
  const before = store.get().docs[target];
  store.set({ busy: mine });
  try {
    const r = await send(target, name, args);
    if (store.get().busy === mine) {
      await (r.ok ? settled(target, before, started) : settled('', before, started));
    }
    return r;
  } finally {
    if (store.get().busy === mine) store.set({ busy: null });
  }
}

function streamCommand(target, name, args, key) {
  const id = target + ' ' + name + ' ' + key;
  const going = streams.get(id);
  if (going) {
    going.next = args;
    return going.done;
  }
  const state = { next: undefined, done: null };
  state.done = (async () => {
    let r = await send(target, name, args);
    while (state.next !== undefined && streams.get(id) === state) {
      const last = state.next;
      state.next = undefined;
      r = await send(target, name, last);
    }
    if (streams.get(id) === state) {
      streams.delete(id);
      if (!r.ok) toast(statusText(r.status, r.field), 'err', 5000);
    }
    return r;
  })();
  streams.set(id, state);
  return state.done;
}

// Resolves with { ok, status, result, field }; a command that ends later is followed until
// its event arrives. It never throws for a refusal of the device.
async function send(target, name, args) {
  let answer;
  if (!api) return { ok: false, status: 'offline', field: '', result: {} };
  try {
    answer = await api.command(target, name, args);
  } catch (e) {
    return { ok: false, status: e.code || 'offline', field: e.field || '', result: {} };
  }
  const out = { ok: !!answer.ok, status: answer.status, field: answer.field || '',
                result: answer.result || {} };
  if (answer.status !== 'pending' || !answer.ref) return out;
  const early = ended.get(answer.ref);
  ended.delete(answer.ref);
  const outcome = early && Date.now() - early[1] < 5000 ? early[0] : await new Promise((resolve) => {
    const timer = setTimeout(() => {
      waiting.delete(answer.ref);
      resolve('timeout');
    }, 60000);
    waiting.set(answer.ref, (status) => {
      clearTimeout(timer);
      resolve(status);
    });
  });
  return { ok: outcome === 'ok', status: outcome, field: '', result: out.result };
}

// The text of an outcome, for a notice.
export function statusText(status, field) {
  const text = t('status.' + status);
  return field ? `${text}: ${field}` : text;
}

// ---------------------------------------------------------------- notices and dialogs

let toastId = 0;

export function toast(text, kind = 'info', ms = 3500) {
  const id = ++toastId;
  store.set((s) => ({ toasts: s.toasts.concat([{ id, text, kind }]) }));
  setTimeout(() => store.set((s) => ({ toasts: s.toasts.filter((x) => x.id !== id) })), ms);
}

// Runs a command and says how it went. Returns the outcome of command().
export async function act(target, name, args, okText) {
  const r = await command(target, name, args);
  if (r.ok) {
    if (okText) toast(okText, 'ok');
  } else if (r.status !== 'wait') {
    // 'wait': not sent, another command was travelling. The console was showing it.
    toast(statusText(r.status, r.field), 'err', 5000);
  }
  return r;
}

// Asks before something that cannot be undone. Resolves true or false.
export function confirmDialog({ title, text, okLabel, danger = false }) {
  return new Promise((resolve) => {
    store.set({ dialog: { title, text, okLabel: okLabel || t('btn.confirm'), danger,
                          done: (yes) => { store.set({ dialog: null }); resolve(yes); } } });
  });
}

// ---------------------------------------------------------------- events

function moduleName(index) {
  const s = store.get().state;
  const m = s && s.modules && s.modules[index];
  return m ? m.name : null;
}

function onEvent(e) {
  switch (e.id) {
    case 'cmd': {
      const done = waiting.get(e.value);
      if (done) {
        waiting.delete(e.value);
        done(STATUS_NAMES[e.b] || 'error');
      } else {
        ended.set(e.value, [STATUS_NAMES[e.b] || 'error', Date.now()]);
        if (ended.size > 20) ended.delete(ended.keys().next().value);
      }
      break;
    }
    case 'svc':
      refreshSoon('report', 300);
      break;
    case 'mode':
      refreshSoon('state');
      refreshSoon('sys.net');
      break;
    case 'config':
      refreshSoon('state');
      refreshSoon('report', 400);
      Object.keys(store.get().cfgs).forEach((target) => refreshSoon('cfg:' + target, 400));
      break;
    case 'time':
      refreshSoon('sys.clock');
      break;
    case 'ota.progress':
      store.set({ ota: { percent: e.b, bytes: e.value } });
      break;
    case 'ota.done':
      store.set({ ota: null });
      refreshSoon('sys.ota');
      break;
    case 'reset':
      awaitRestart(e.a === 1 ? 'factory' : 'restart');
      break;
    case 'module.data':
    case 'module.state': {
      const name = moduleName(e.a);
      if (name) refreshSoon('mod.' + name, 50);
      if (e.id === 'module.state') refreshSoon('sys.modules');
      break;
    }
    default:
      if (e.id.startsWith('net.') || e.id.startsWith('internet.')) {
        refreshSoon('sys.net');
        // The addresses of the console change with the network.
        if (store.get().docs['sys.web']) refreshSoon('sys.web', 400);
      }
      else if (e.id.startsWith('mqtt.')) refreshSoon('sys.mqtt');
      else if (e.id.startsWith('cloud.')) refreshSoon('sys.cloud');
      else if (e.id.startsWith('lora.')) refreshSoon('sys.lora', 300);
      else if (store.get().docs['sys.' + e.id]) {
        // An event with the name of a document of the system that is in use here: a service
        // the console does not know changed. Its document is read again, and the modules,
        // which can stop and start with it.
        refreshSoon('sys.' + e.id);
        refreshSoon('sys.modules', 400);
      }
  }
}

function onLog(entry) {
  store.set((s) => {
    if (entry.seq && entry.seq <= s.logSeq) return null;
    const log = s.log.length >= LOG_KEEP ? s.log.slice(s.log.length - LOG_KEEP + 1) : s.log.slice();
    log.push(entry);
    return { log, logSeq: entry.seq || s.logSeq };
  });
}

// Reads the entries written since the last one the console has: at the start, and after the
// stream was lost for a while.
export async function catchUpLog() {
  const from = api;
  const since = store.get().logSeq;
  let page;
  try {
    page = await from.get('log?since=' + since + '&max=100');
  } catch (e) {
    return;
  }
  if (!still(from)) return;
  (page.items || []).forEach(onLog);
}

let wasOffline = false;

export function startLive() {
  const from = api;
  from.listen({
    event: (e) => still(from) && onEvent(e),
    log: (entry) => still(from) && onLog(entry),
    open: () => {
      if (!still(from)) return;
      store.set({ online: true });
      if (wasOffline) {
        wasOffline = false;
        loadReport().catch(() => {});
        catchUpLog();
      }
    },
    error: () => {
      if (!still(from) || store.get().boot !== 'ready') return;
      wasOffline = true;
      store.set({ online: false });
    },
  });
}

export function stopLive() {
  if (api) api.close();
}

// ---------------------------------------------------------------- start and restart

export async function boot() {
  const from = api;
  store.set({ boot: 'loading', bootError: '' });
  let state;
  try {
    state = await loadState();
  } catch (e) {
    if (still(from)) store.set({ boot: 'error', bootError: e instanceof ApiError ? e.code : 'offline' });
    return;
  }
  if (!still(from)) return;
  if (state.auth && state.auth.required) {
    let me = null;
    try {
      me = from.token ? await from.get('auth/me') : null;
    } catch (e) {
      me = null;
    }
    if (!still(from)) return;
    if (!me || !me.auth) {
      store.set({ boot: 'login' });
      return;
    }
  }
  await enter();
}

// After the login, or at once when none is needed.
export async function enter() {
  const from = api;
  try {
    await loadReport();
  } catch (e) {
    if (!still(from)) return;
    if (e.status === 401) {
      store.set({ boot: 'login' });
      return;
    }
    store.set({ boot: 'error', bootError: e.code || 'offline' });
    return;
  }
  if (!still(from)) return;
  store.set({ boot: 'ready', online: true });
  catchUpLog();
  startLive();
}

export async function login(password) {
  const from = api;
  await from.login(password);
  if (still(from)) await enter();
}

export async function logout() {
  const from = api;
  stopLive();
  await from.logout();
  if (still(from)) store.set({ boot: 'login', report: null, docs: {}, cfgs: {} });
}

// The device is about to restart: wait until it answers again, then load the console anew.
let restarting = false;
let restartTimer = null;

export function awaitRestart(reason = 'restart') {
  if (restarting || !api) return;
  restarting = true;
  const from = api;
  stopLive();
  store.set({ boot: 'restarting', restartReason: reason });
  const started = Date.now();
  const poll = async () => {
    if (!still(from)) return;
    if (Date.now() - started > 120000) {
      store.set({ restartReason: 'lost' });
      return;
    }
    try {
      await from.get('state', 1500);
      if (still(from)) location.reload();
    } catch (e) {
      if (still(from)) restartTimer = setTimeout(poll, 2000);
    }
  };
  restartTimer = setTimeout(poll, 4000);
}

// ---------------------------------------------------------------- stop

// Leaves the device: the stream is closed, what was waiting is let go, and the console knows
// nothing again. After it the console can be started on another device, in the same page: the
// console of the server moves from a device to another without loading the page again.
export function leave() {
  const from = api;
  api = null;
  if (from) {
    from.onAuthLost = null;
    try {
      from.close();
    } catch (e) {
      // A transport that was never opened has nothing to close.
    }
  }
  Object.keys(refreshTimers).forEach((what) => {
    clearTimeout(refreshTimers[what]);
    delete refreshTimers[what];
  });
  clearTimeout(restartTimer);
  restartTimer = null;
  restarting = false;
  wasOffline = false;
  inFlight.clear();
  streams.clear();
  // Who waits for the end of a command is told it will not come from here.
  const pending = Array.from(waiting.values());
  waiting.clear();
  pending.forEach((done) => done('offline'));
  const open = store.get().dialog;
  if (open) open.done(false);
  // The theme is of who looks, not of the device: it stays.
  store.set(Object.assign(initial(), { theme: store.get().theme }));
}
