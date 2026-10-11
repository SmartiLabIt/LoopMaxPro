// LoopMaxPro console - system space: update, modules, services, time and place, MQTT,
// hardware, settings.
import { html, useEffect, useState } from '../vendor/preact-htm.js';
import { Badge, Btn, Card, Empty, JsonView, KV, Meter, Notice, PasswordInput, Select, Spinner,
         StateBadge, Switch, TextInput, useTick } from '../kit/ui.js';
import { CommandForm, commandHelp } from '../kit/form.js';
import { Icon } from '../kit/icons.js';
import { prefs, useStore } from '../kit/store.js';
import { LANGS, t } from '../kit/i18n.js';
import { fmtBytes, fmtDeviceDate, fmtDeviceTime, fmtDuration, percent } from '../kit/format.js';
import { sha256Hex } from '../kit/sha256.js';
import { ZONES, browserZone, zoneRules } from '../kit/tz.js';
import { act, awaitRestart, command, confirmDialog, deviceClock, docOf, loadCfg, loadDoc,
         loadManifest, loadReport, logout, store, toast, transport } from './live.js';
import { askPlace } from './place.js';
import { reasonText, stoppedText } from './module-ui.js';
import { Brands } from './sys-core.js';

function useDoc(target, deps = []) {
  const doc = useStore(store, (s) => docOf(s, target));
  useEffect(() => {
    loadDoc(target).catch(() => {});
  }, deps.concat([target]));
  return doc;
}

const Loading = () => html`<div class="loading"><${Spinner} size=${28}/></div>`;

// ---------------------------------------------------------------- update

export function Update() {
  const ota = useDoc('sys.ota');
  const web = useDoc('sys.web');
  const progress = useStore(store, (s) => s.ota);
  if (!ota) return html`<${Loading}/>`;
  const canUpload = web ? web.canUpload !== false : true;

  const confirmNow = () => act('sys.ota', 'confirm', {}, t('ota.confirmed'))
    .then(() => loadDoc('sys.ota').catch(() => {}));
  const rollback = async () => {
    const yes = await confirmDialog({ title: t('ota.rollback'), text: t('ota.rollbackText'), danger: true });
    if (!yes) return;
    const r = await act('sys.ota', 'rollback', {});
    if (r.ok) awaitRestart('update');
  };

  return html`<div class="space">
    ${ota.trial && html`<${Notice} kind="warn"
        action=${html`<${Btn} small kind="primary" icon="check" onClick=${confirmNow}>${t('ota.confirm')}<//>`}>
      ${t('ota.trialText')}<//>`}
    <div class="grid2">
      <${Card} title=${t('ota.firmware')} icon="cpu"
          actions=${ota.canRollback && html`<${Btn} small icon="rotate-ccw" onClick=${rollback}>
            ${t('ota.rollback')}<//>`}>
        <${KV} rows=${[
          [t('ota.installed'), html`<b>${ota.installed}</b>`],
          [t('ota.build'), ota.build],
          [t('ota.state'), html`<${Badge} kind=${ota.state === 'failed' ? 'err' : ota.state === 'idle' ? 'off' : 'info'}>
            ${t('ota.st.' + ota.state)}<//>`],
          [t('ota.trial'), ota.trial ? t('yes') : t('no')],
          [t('ota.installs'), ota.installs],
          [t('ota.failures'), ota.failures || null],
          [t('ota.lastError'), ota.error || null]]}/>
        ${progress && html`<${Meter} label=${t('ota.receiving')} value=${progress.percent} max=${100}
          detail=${progress.percent + '% · ' + fmtBytes(progress.bytes)} warnAt=${101} errAt=${101}/>`}
      <//>
      ${!ota.available ? html`<${Card} title=${t('ota.upload')} icon="upload">
          <${Notice} kind="warn">${t('ota.unavailable')}<//><//>`
        : canUpload ? html`<${FirmwareUpload}/>`
        : html`<${Card} title=${t('ota.upload')} icon="upload"><${Notice} kind="warn">${t('ota.noUpload')}<//><//>`}
    </div>
    ${ota.available && ota.fromUrl && html`<${FromUrl}/>`}
    ${canUpload && html`<${ConsoleFiles}/>`}
  </div>`;
}

function FirmwareUpload() {
  const [file, setFile] = useState(null);
  const [hash, setHash] = useState('');
  const [sent, setSent] = useState(null);
  const [error, setError] = useState('');
  const choose = async (e) => {
    const f = e.target.files && e.target.files[0];
    setError('');
    setHash('');
    setFile(f || null);
    if (!f) return;
    const bytes = new Uint8Array(await f.arrayBuffer());
    setHash(sha256Hex(bytes));
  };
  const send = async () => {
    const yes = await confirmDialog({ title: t('ota.install'), text: t('ota.installText', { name: file.name }) });
    if (!yes) return;
    setSent(0);
    setError('');
    try {
      await transport().upload('/api/upload/ota?target=app&sha256=' + hash, file,
                               (done, total) => setSent(percent(done, total)));
      toast(t('ota.installed'), 'ok');
      awaitRestart('update');
    } catch (e) {
      setSent(null);
      setError(t('status.' + e.code) + (e.status ? ' (HTTP ' + e.status + ')' : ''));
    }
  };
  return html`<${Card} title=${t('ota.upload')} icon="upload">
    <label class="drop">
      <input type="file" accept=".bin,application/octet-stream" onChange=${choose}/>
      <${Icon} name="upload" size=${26}/>
      <span>${file ? file.name : t('ota.choose')}</span>
      ${file && html`<small>${fmtBytes(file.size)}</small>`}
    </label>
    ${hash && html`<p class="muted small mono">SHA-256 ${hash}</p>`}
    ${sent !== null && html`<${Meter} label=${t('ota.sending')} value=${sent} max=${100}
      detail=${sent + '%'} warnAt=${101} errAt=${101}/>`}
    ${error && html`<${Notice} kind="err">${error}<//>`}
    <div class="form-a"><${Btn} kind="primary" icon="download" disabled=${!file || !hash || sent !== null}
      busy=${sent !== null} onClick=${send}>${t('ota.install')}<//></div>
    <p class="muted small">${t('ota.uploadHelp')}</p>
  <//>`;
}

function FromUrl() {
  const [url, setUrl] = useState('');
  const [hash, setHash] = useState('');
  const [busy, setBusy] = useState(false);
  const start = async () => {
    const yes = await confirmDialog({ title: t('ota.fromUrl'), text: t('ota.fromUrlText') });
    if (!yes) return;
    setBusy(true);
    const args = { url };
    if (hash) args.sha256 = hash.trim().toLowerCase();
    const r = await act('sys.ota', 'fromUrl', args, t('ota.installed'));
    setBusy(false);
    if (r.ok) awaitRestart('update');
  };
  return html`<${Card} title=${t('ota.fromUrl')} icon="globe">
    <div class="row2">
      <${TextInput} label=${t('ota.url')} value=${url} onInput=${setUrl} placeholder="https://…"/>
      <${TextInput} label=${t('ota.sha')} value=${hash} onInput=${setHash} hint=${t('ota.shaHint')}/>
    </div>
    <div class="form-a"><${Btn} kind="primary" icon="download" busy=${busy}
      disabled=${!/^https?:\/\/.+/.test(url)} onClick=${start}>${t('ota.install')}<//></div>
  <//>`;
}

function ConsoleFiles() {
  const [list, setList] = useState(null);
  const [busy, setBusy] = useState('');
  const read = async () => {
    const r = await command('sys.web', 'files', {});
    setList(r.ok ? r.result : { files: [], count: 0, bytes: 0 });
  };
  useEffect(() => {
    read();
  }, []);
  const upload = async (e) => {
    const files = Array.from(e.target.files || []);
    for (const f of files) {
      setBusy(f.name);
      try {
        const hash = sha256Hex(new Uint8Array(await f.arrayBuffer()));
        await transport().upload('/api/upload/file?name=' + encodeURIComponent(f.name) +
                                 '&sha256=' + hash, f);
      } catch (err) {
        toast(f.name + ': ' + t('status.' + err.code), 'err', 6000);
      }
    }
    setBusy('');
    e.target.value = '';
    read();
  };
  const remove = async (name) => {
    const yes = await confirmDialog({ title: t('files.remove'), text: name, danger: true,
                                      okLabel: t('btn.remove') });
    if (!yes) return;
    await act('sys.web', 'removeFile', { name }, t('removed'));
    read();
  };
  return html`<${Card} title=${t('files.title')} icon="hard-drive"
      sub=${list ? t('files.sub', { n: list.count, size: fmtBytes(list.bytes) }) : ''}
      actions=${html`<label class="btn ghost sm"><input type="file" multiple hidden onChange=${upload}/>
        ${busy ? html`<span class="spin"></span>` : html`<${Icon} name="upload" size=${15}/>`}
        <span>${t('files.upload')}</span></label>`}>
    ${!list ? html`<${Loading}/>` : list.files.length === 0 ? html`<${Empty}>${t('files.none')}<//>` :
      html`<ul class="files">${list.files.map((f) => html`<li key=${f.name}>
        <${Icon} name="scroll-text" size=${15}/><span>${f.name}</span><small>${fmtBytes(f.size)}</small>
        <${Btn} small icon="trash-2" title=${t('btn.remove')} onClick=${() => remove(f.name)}/>
      </li>`)}</ul>`}
    <p class="muted small">${t('files.help')}</p>
  <//>`;
}

// ---------------------------------------------------------------- modules

export function Modules({ openModule }) {
  // The last list read: the events of the modules read it again.
  const modules = useStore(store, (s) => docOf(s, 'sys.modules'));
  // A reason a bridge knows, the block of the product: said in its words, and not undone
  // by a restart.
  const uis = useStore(store, (s) => s.uis);
  const [busy, setBusy] = useState('');
  if (!modules) return html`<${Loading}/>`;
  const restart = async (m) => {
    const yes = await confirmDialog({ title: t('mod.restart'), text: t('mod.restartText') });
    if (!yes) return;
    setBusy(m.name);
    await act('sys.modules', 'restart', { module: m.name }, t('done'));
    setBusy('');
    loadReport().catch(() => {});
  };
  return html`<div class="space">
    <p class="muted">${t('mods.sub', { n: modules.count, max: modules.max })}</p>
    ${modules.list.length === 0 ? html`<${Empty} icon="puzzle">${t('mods.none')}<//>` : html`
    <div class="grid2">${modules.list.map((m) => html`<${Card} key=${m.name} cls="modcard"
        title=${html`<span class="emo">${m.icon}</span> ${m.title}`} sub=${m.name + ' · ' + m.version}
        actions=${html`<${StateBadge} state=${m.state} reason=${m.reason}/>`}>
      <${KV} rows=${[[t('mod.kind'), t('kind.' + m.kind)],
                     [t('mod.reason'), reasonText(uis, m.reason) ? t(reasonText(uis, m.reason))
                       : m.reason || null],
                     [t('mod.tick'), m.tickMs ? m.tickMs + ' ms' : t('mod.noTick')],
                     [t('mod.code'), m.code || null]]}/>
      <div class="form-a">
        ${!stoppedText(uis, m.reason) && html`<${Btn} small icon="rotate-ccw" busy=${busy === m.name}
          onClick=${() => restart(m)}>${t('mod.restart')}<//>`}
        <${Btn} small kind="primary" icon="chevron-right" onClick=${() => openModule(m.name)}>${t('btn.open')}<//>
      </div>
    <//>`)}</div>`}
  </div>`;
}

// ---------------------------------------------------------------- services

export function Services() {
  const services = useStore(store, (s) => s.report && s.report.services);
  const [open, setOpen] = useState('');
  if (!services) return html`<${Loading}/>`;
  return html`<div class="space">
    <div class="svc-list">
      ${services.map((x) => html`<div key=${x.name} class=${'svc' + (open === x.name ? ' open' : '')}>
        <button class="svc-h" onClick=${() => setOpen(open === x.name ? '' : x.name)}>
          <span class="emo">${x.icon}</span>
          <b>${x.name}</b>
          <${StateBadge} state=${x.state} reason=${x.reason}/>
          <small>${x.reason}</small>
          <span class="svc-m">${x.tickMs ? x.tickMs + ' ms' : ''}${x.tickMaxUs ? ' · max ' + x.tickMaxUs + ' µs' : ''}</span>
          <${Icon} name="chevron-down" size=${16} cls="chev"/>
        </button>
        ${open === x.name && html`<${ServiceDetail} service=${x}/>`}
      </div>`)}
    </div>
  </div>`;
}

function ServiceDetail({ service }) {
  const target = 'sys.' + service.name;
  const doc = useStore(store, (s) => s.docs[target]);
  const manifest = useStore(store, (s) => s.manifests[target]);
  useEffect(() => {
    loadDoc(target).catch(() => {});
    loadManifest(target).catch(() => {});
  }, [target]);
  const run = async (c, args) => {
    if (c.confirm) {
      const yes = await confirmDialog({ title: target + ' ' + c.name, text: commandHelp(target, c),
                                        danger: true });
      if (!yes) return;
    }
    const r = await act(target, c.name, args, t('done'));
    if (r.ok && Object.keys(r.result || {}).length) toast(JSON.stringify(r.result).slice(0, 200), 'info', 8000);
    loadDoc(target).catch(() => {});
  };
  return html`<div class="svc-b">
    <${KV} rows=${[
      [t('svc.deps'), service.deps.length ? service.deps.join(', ') : '–'],
      [t('svc.starts'), service.starts], [t('svc.failures'), service.failures],
      [t('svc.tick'), service.tickUs + ' µs, max ' + service.tickMaxUs + ' µs'],
      [t('svc.heap'), service.heap ? fmtBytes(service.heap) : null]]}/>
    <div class="grid2">
      <div><h4>${t('svc.document')}</h4>${doc ? html`<${JsonView} value=${strip(doc)}/>` : html`<${Loading}/>`}</div>
      <div><h4>${t('svc.commands')}</h4>
        ${!manifest ? html`<${Loading}/>` : manifest.commands.length === 0 ? html`<p class="muted">${t('svc.noCommands')}</p>` :
          manifest.commands.map((c) => html`<details class="acc" key=${c.name}>
            <summary><b>${c.name}</b><small>${commandHelp(target, c)}</small></summary>
            <${CommandForm} command=${c} scope=${target} onRun=${(args) => run(c, args)}/>
          </details>`)}
      </div>
    </div>
  </div>`;
}

function strip(doc) {
  return Object.fromEntries(Object.entries(doc).filter(([k]) => !k.startsWith('_')));
}

// ---------------------------------------------------------------- time and place

export function TimePlace() {
  const clockDoc = useDoc('sys.clock');
  const geo = useDoc('sys.geo');
  const clock = useStore(store, deviceClock);
  useTick(1000);
  const [tz, setTz] = useState(null);
  const [ntp, setNtp] = useState(null);
  if (!clockDoc) return html`<${Loading}/>`;

  const here = browserZone();
  const zoneNow = tz === null ? clockDoc.tz : tz;
  const known = ZONES.find((z) => z[0] === zoneNow);
  const options = ZONES.map((z) => ({ id: z[0], label: z[0] }));
  if (!known) options.unshift({ id: zoneNow, label: zoneNow });
  if (here && !options.find((o) => o.id === here)) options.push({ id: here, label: here });

  const sync = async () => {
    const r = await act('sys.clock', 'sync', {}, t('clock.synced'));
    if (r.ok) loadDoc('sys.clock').catch(() => {});
  };
  const fromBrowser = () => act('sys.clock', 'set', { unix: Math.floor(Date.now() / 1000) }, t('clock.set'))
    .then(() => loadDoc('sys.clock').catch(() => {}));
  const saveZone = () => {
    const rules = zoneRules(zoneNow);
    const args = {};
    if (rules && zoneNow !== clockDoc.tz) Object.assign(args, { tz: zoneNow, posix: rules });
    if (ntp !== null && ntp !== clockDoc.ntpServer) args.ntp = ntp;
    if (!Object.keys(args).length) return;
    act('sys.clock', 'set', args, t('saved')).then(() => {
      setTz(null);
      setNtp(null);
      loadDoc('sys.clock').catch(() => {});
    });
  };

  return html`<div class="space">
    <div class="grid2">
      <${Card} title=${t('clock.title')} icon="clock"
          actions=${html`<${Btn} small icon="refresh-cw" onClick=${sync}>${t('clock.sync')}<//>`}>
        <div class="bigtime">
          <b>${clock ? fmtDeviceTime(clock.unix, clock.offset) : '--:--:--'}</b>
          <span>${clock ? fmtDeviceDate(clock.unix, clock.offset) : t('clock.unknown')}</span>
        </div>
        <${KV} rows=${[
          [t('clock.source'), html`<${Badge} kind=${clockDoc.synced ? 'ok' : 'warn'}>${t('clock.src.' + clockDoc.source)}<//>`],
          [t('clock.zone'), clockDoc.tz],
          [t('clock.offset'), clockDoc.offset !== undefined ? 'UTC' + (clockDoc.offset >= 0 ? '+' : '') + (clockDoc.offset / 3600) : null],
          [t('clock.ntp'), clockDoc.ntp && clockDoc.ntp.available ? clockDoc.ntpServer : t('no')],
          [t('clock.answers'), clockDoc.ntp ? clockDoc.ntp.answers : null]]}/>
        <div class="form-a"><${Btn} small icon="monitor-smartphone" onClick=${fromBrowser}>${t('clock.fromBrowser')}<//></div>
      <//>
      <${Card} title=${t('clock.settings')} icon="settings">
        <${Select} label=${t('clock.zone')} value=${zoneNow} options=${options} onChange=${setTz}
          hint=${here && here !== zoneNow ? t('clock.browserZone', { zone: here }) : ''}/>
        <${TextInput} label=${t('clock.ntp')} value=${ntp === null ? clockDoc.ntpServer : ntp} onInput=${setNtp}/>
        <div class="form-a">
          ${here && here !== zoneNow && zoneRules(here) &&
            html`<${Btn} small onClick=${() => setTz(here)}>${t('clock.useBrowser')}<//>`}
          <${Btn} kind="primary" icon="check" disabled=${tz === null && ntp === null} onClick=${saveZone}>${t('btn.save')}<//>
        </div>
      <//>
    </div>
    ${geo && html`<${Geo} geo=${geo}/>`}
  </div>`;
}

function Geo({ geo }) {
  const cfg = useStore(store, (s) => s.cfgs['sys.geo']);
  const [draft, setDraft] = useState({});
  useEffect(() => {
    loadCfg('sys.geo').catch(() => {});
  }, []);
  const value = (k) => (k in draft ? draft[k] : cfg ? cfg[k] : '');
  const set = (k) => (v) => setDraft(Object.assign({}, draft, { [k]: v }));
  const save = async () => {
    const args = {};
    Object.keys(draft).forEach((k) => {
      args[k] = k === 'lat' || k === 'lon' ? Number(draft[k]) : draft[k];
    });
    const r = await act('sys.geo', 'set', args, t('saved'));
    if (r.ok) {
      setDraft({});
      loadCfg('sys.geo').catch(() => {});
      loadDoc('sys.geo').catch(() => {});
    }
  };
  // What the owner gave, a point or the names: forgotten, the network writes them again.
  const byOwner = geo.source === 'manual' || !!(cfg && cfg.named);
  const automatic = async () => {
    const yes = await confirmDialog({ title: t('geo.automatic'), text: t('geo.automaticText') });
    if (!yes) return;
    const r = await act('sys.geo', 'clear', {}, t('done'));
    if (r.ok) {
      setDraft({});
      loadCfg('sys.geo').catch(() => {});
      setTimeout(() => loadDoc('sys.geo').catch(() => {}), 1500);
    }
  };
  return html`<div class="grid2">
    <${Card} title=${t('geo.title')} icon="map-pin"
        actions=${html`<${Btn} small icon="search" disabled=${!geo.lookup || !geo.lookup.configured}
          onClick=${() => act('sys.geo', 'lookup', {}, t('done')).then(() => loadDoc('sys.geo').catch(() => {}))}>
          ${t('geo.lookup')}<//>`}>
      ${geo.known ? html`<${KV} rows=${[
        [t('geo.place'), [geo.city, geo.region, geo.country].filter(Boolean).join(', ')],
        [t('geo.coords'), geo.lat !== undefined ? geo.lat + ', ' + geo.lon : null],
        [t('geo.source'), html`<${Badge} kind=${geo.source === 'manual' || geo.source === 'gps' ? 'ok' : 'info'}>
          ${t('geo.src.' + geo.source)}<//>`],
        [t('geo.ip'), geo.publicIp || null], [t('geo.isp'), geo.isp || null]]}/>`
        : html`<${Empty} icon="map-pin">${t('geo.unknown')}<//>`}
    <//>
    <${Card} title=${t('geo.settings')} icon="settings">
      ${!cfg ? html`<${Loading}/>` : html`
      <div class="place-give">
        <${Btn} kind="primary" icon="map-pin" data-geo="give"
          onClick=${() => askPlace({ device: true })}>${t('geo.give')}<//>
        <small class="muted">${t('geo.giveHint')}</small>
      </div>
      <div class="row2">
        <${TextInput} label=${t('geo.city')} value=${value('city')} onInput=${set('city')}/>
        <${TextInput} label=${t('geo.country')} value=${value('country')} onInput=${set('country')} maxLength=${2}/>
      </div>
      <${TextInput} label=${t('geo.lookupUrl')} value=${value('lookupUrl')} onInput=${set('lookupUrl')}
        hint=${t('geo.lookupHint')}/>
      ${geo.source === 'manual' && html`<${Notice}>${t('geo.manualNote')}<//>`}
      <div class="form-a">
        ${byOwner && html`<${Btn} small icon="rotate-ccw" onClick=${automatic}>${t('geo.automatic')}<//>`}
        <${Btn} kind="primary" icon="check" disabled=${!Object.keys(draft).length}
          onClick=${save}>${t('btn.save')}<//></div>`}
    <//>
  </div>`;
}

// ---------------------------------------------------------------- MQTT

export function Mqtt() {
  const doc = useDoc('sys.mqtt');
  const cfg = useStore(store, (s) => s.cfgs['sys.mqtt']);
  const [draft, setDraft] = useState({});
  useEffect(() => {
    loadCfg('sys.mqtt').catch(() => {});
  }, []);
  if (!cfg) return html`<${Loading}/>`;
  const v = (k) => (k in draft ? draft[k] : cfg[k]);
  const set = (k) => (x) => setDraft(Object.assign({}, draft, { [k]: x }));
  const save = async () => {
    const broker = {};
    const topics = {};
    Object.keys(draft).forEach((k) => {
      const x = k === 'port' || k === 'keepAliveSec' ? Number(draft[k]) : draft[k];
      if (k === 'psw' && !x) return;
      (['base', 'keepAliveSec', 'events', 'home'].includes(k) ? topics : broker)[k] = x;
    });
    let ok = true;
    if (Object.keys(broker).length) ok = (await act('sys.mqtt', 'set', broker)).ok;
    if (ok && Object.keys(topics).length) ok = (await act('sys.mqtt', 'setTopics', topics)).ok;
    if (ok) {
      toast(t('saved'), 'ok');
      setDraft({});
      loadCfg('sys.mqtt').catch(() => {});
      loadDoc('sys.mqtt').catch(() => {});
    }
  };
  const live = doc && doc.available !== undefined;
  const home = doc && doc.home;
  // What the device is doing for Home Assistant now, in a word.
  const told = !home ? null
    : !cfg.enabled ? ['off', t('mqtt.off')]
    : !home.enabled ? ['off', t('ha.notTold')]
    : doc.connected ? ['ok', t('mqtt.connected') + ' · ' + t('mqtt.homeOn', { n: home.things })]
    : ['warn', t('mqtt.disconnected')];
  return html`<div class="space">
    ${cfg.host && !cfg.enabled && html`<${Notice} kind="warn" icon="radio">${t('mqtt.offNote')}<//>`}
    ${home && html`<${Card} title=${t('ha.title')} icon="house" sub=${t('ha.sub')} cls="ha"
        actions=${html`<${Badge} kind=${told[0]}>${told[1]}<//>`}>
      <p>${t('ha.what')}</p>
      <${Brands}/>
      <ol class="steps">
        <li>${t('ha.step1')}</li>
        <li>${t('ha.step2')}</li>
        <li>${t('ha.step3')}</li>
      </ol>
      <p class="muted small">${t('ha.voice')}</p>
    <//>`}
    <div class="grid2">
    <${Card} title=${t('mqtt.state')} icon="radio"
        actions=${live && cfg.enabled && html`<${Btn} small icon="refresh-cw"
          onClick=${() => act('sys.mqtt', 'reconnect', {}, t('done'))}>${t('mqtt.reconnect')}<//>`}>
      ${!live ? html`<${Notice}>${t('mqtt.waiting')}<//>` : html`<${KV} rows=${[
        [t('mqtt.connection'), html`<${Badge} kind=${doc.connected ? 'ok' : cfg.enabled ? 'warn' : 'off'}>
          ${doc.connected ? t('mqtt.connected') : cfg.enabled ? t('mqtt.disconnected') : t('mqtt.off')}<//>`],
        [t('mqtt.broker'), doc.host ? (doc.tls ? 'mqtts://' : 'mqtt://') + doc.host + ':' + doc.port : null],
        [t('mqtt.base'), doc.base ? html`<code>${doc.base}</code>` : null],
        [t('mqtt.sent'), doc.sent], [t('mqtt.received'), doc.received],
        [t('mqtt.dropped'), doc.dropped || null], [t('mqtt.error'), doc.error || null]]}/>`}
    <//>
    <${Card} title=${t('mqtt.settings')} icon="settings">
      <div class="field sw"><${Switch} on=${v('enabled')} onChange=${set('enabled')} label=${t('mqtt.enabled')}/></div>
      <div class="row2">
        <${TextInput} label=${t('mqtt.host')} value=${v('host')} onInput=${set('host')}/>
        <${TextInput} label=${t('mqtt.port')} type="number" value=${v('port')} onInput=${set('port')} min=${1} max=${65535}/>
      </div>
      <div class="field sw"><${Switch} on=${v('tls')} onChange=${set('tls')} label=${t('mqtt.tls')}/></div>
      <div class="row2">
        <${TextInput} label=${t('mqtt.user')} value=${v('user')} onInput=${set('user')}/>
        <${PasswordInput} label=${t('mqtt.psw')} value=${draft.psw || ''} onInput=${set('psw')} isSet=${cfg.pswSet}/>
      </div>
      <div class="row2">
        <${TextInput} label=${t('mqtt.base')} value=${v('base')} onInput=${set('base')} hint=${t('mqtt.baseHint')}/>
        <${TextInput} label=${t('mqtt.keepAlive')} type="number" value=${v('keepAliveSec')} onInput=${set('keepAliveSec')}/>
      </div>
      <div class="field sw"><${Switch} on=${v('events')} onChange=${set('events')} label=${t('mqtt.events')}/></div>
      ${'home' in cfg && html`<div class="field sw"><${Switch} on=${v('home')} onChange=${set('home')}
        label=${t('mqtt.homeSet')}/></div>`}
      <div class="form-a"><${Btn} kind="primary" icon="check" disabled=${!Object.keys(draft).length}
        onClick=${save}>${t('btn.save')}<//></div>
    <//>
  </div></div>`;
}

// ---------------------------------------------------------------- cloud

export function Cloud() {
  const doc = useDoc('sys.cloud');
  const [draft, setDraft] = useState({});
  if (!doc) return html`<${Loading}/>`;
  const v = (k) => (k in draft ? draft[k] : doc[k]);
  const set = (k) => (x) => setDraft(Object.assign({}, draft, { [k]: x }));
  const save = async () => {
    const args = {};
    Object.keys(draft).forEach((k) => { args[k] = Number(draft[k]); });
    if ((await act('sys.cloud', 'set', args)).ok) {
      toast(t('saved'), 'ok');
      setDraft({});
      loadDoc('sys.cloud').catch(() => {});
    }
  };
  const detail = doc.detail || {};
  // Only bridges that ask the server for updates say updateChecking.
  const updates = 'updateChecking' in detail;
  const check = () => act('sys.cloud', 'checkUpdate', {}).then(() => loadDoc('sys.cloud').catch(() => {}));
  return html`<div class="space"><div class="grid2">
    <${Card} title=${t('cloud.state')} icon=${doc.connected ? 'cloud' : 'cloud-off'}
        actions=${html`<span class="row">
          ${updates && html`<${Btn} small icon="refresh-cw" busy=${detail.updateChecking}
            onClick=${check}>${t('cloud.check')}<//>`}
          ${doc.connected && html`<${Btn} small icon="upload"
            onClick=${() => act('sys.cloud', 'report', {}, t('done'))}>${t('cloud.report')}<//>`}</span>`}>
      ${!doc.attached ? html`<${Notice}>${t('cloud.noBridge')}<//>` : html`<${KV} rows=${[
        [t('cloud.update'), !updates ? null : detail.updateChecking ? t('cloud.checking')
          : detail.update ? html`<b>${(detail.update.changes && detail.update.changes.length
              ? detail.update.changes : [{ title: 'LoopMaxPro', to: detail.update.version }])
              .map((c) => c.title + ' ' + c.to).join(', ')}</b>` : t('cloud.updateNone')],
        [t('cloud.connection'), html`<${Badge} kind=${doc.connected ? 'ok' : 'warn'}>
          ${doc.connected ? t('cloud.connected') : t('cloud.disconnected')}<//>`],
        [t('cloud.bridge'), doc.bridge],
        [t('cloud.server'), detail.server || null],
        [t('cloud.since'), doc.connected ? fmtDuration(doc.upSec) : null],
        [t('cloud.connections'), doc.connections],
        [t('cloud.sent'), doc.sent], [t('cloud.received'), doc.received],
        [t('cloud.refused'), doc.refused || null], [t('cloud.dropped'), doc.dropped || null],
        [t('cloud.failed'), doc.failed || null], [t('cloud.error'), detail.error || null]]}/>`}
    <//>
    <${Card} title=${t('cloud.settings')} icon="settings">
      <${TextInput} label=${t('cloud.heartbeat')} type="number" value=${v('heartbeatMin')}
        onInput=${set('heartbeatMin')} min=${0} max=${1440} hint=${t('cloud.heartbeatHint')}/>
      <${Select} label=${t('cloud.pushLog')} value=${String(v('pushLogLevel'))}
        onChange=${set('pushLogLevel')} options=${[{ id: '0', label: t('cloud.pushNone') },
          { id: '1', label: t('cloud.pushErrors') }, { id: '2', label: t('cloud.pushWarnings') }]}/>
      <div class="form-a"><${Btn} kind="primary" icon="check" disabled=${!Object.keys(draft).length}
        onClick=${save}>${t('btn.save')}<//></div>
    <//>
  </div></div>`;
}

// ---------------------------------------------------------------- hardware

export function Hardware() {
  const gpio = useDoc('sys.gpio');
  const bus = useDoc('sys.bus');
  const services = useStore(store, (s) => s.report && s.report.services);
  const hasLora = services && services.some((x) => x.name === 'lora');
  const [sda, setSda] = useState('');
  const [scl, setScl] = useState('');
  if (!gpio) return html`<${Loading}/>`;
  const scan = (b) => act('sys.bus', 'i2cScan', { bus: b }, t('hw.scanDone')).then(() => loadDoc('sys.bus').catch(() => {}));
  const open = () => act('sys.bus', 'i2cOpen', { sda: +sda, scl: +scl }, t('done')).then(() => loadDoc('sys.bus').catch(() => {}));
  return html`<div class="space">
    <${Card} title=${t('hw.pins')} icon="microchip" sub=${t('hw.pinsSub', { n: gpio.used, max: gpio.capacity })}>
      <div class="table-wrap"><table class="tbl">
        <thead><tr><th>GPIO</th><th>${t('hw.owner')}</th><th>${t('hw.label')}</th><th>${t('hw.mode')}</th><th>${t('hw.level')}</th></tr></thead>
        <tbody>${gpio.pins.map((p) => html`<tr key=${p.pin}>
          <td><b>${p.pin}</b></td><td>${p.owner}</td><td>${p.label || '–'}</td><td><code>${p.mode}</code></td>
          <td><span class=${'lvl ' + (p.level ? 'hi' : 'lo')}>${p.level ? 'HIGH' : 'LOW'}</span></td>
        </tr>`)}</tbody>
      </table></div>
    <//>
    ${bus && html`<div class="grid2">
      <${Card} title=${t('hw.i2c')} icon="cable">
        ${bus.i2c.map((b) => html`<div key=${b.bus} class="busrow">
          <b>I2C ${b.bus}</b>
          ${b.open ? html`<span>SDA ${b.sda} · SCL ${b.scl} · ${b.hz / 1000} kHz</span>
              <small>${(b.users || []).join(', ')}</small>
              <${Btn} small icon="search" busy=${bus.scan && bus.scan.state === 'running' && bus.scan.bus === b.bus}
                onClick=${() => scan(b.bus)}>${t('hw.scan')}<//>`
            : html`<${Badge} kind="off">${t('hw.closed')}<//>`}
        </div>`)}
        ${bus.scan && bus.scan.found && html`<div class="found">
          <small>${t('hw.found', { bus: bus.scan.bus })}</small>
          ${bus.scan.found.length ? bus.scan.found.map((a) => html`<code key=${a}>${a}</code>`) : html`<span class="muted">${t('hw.nothing')}</span>`}
        </div>`}
      <//>
      <${Card} title=${t('hw.openBus')} icon="plus">
        <div class="row2">
          <${TextInput} label="SDA" type="number" value=${sda} onInput=${setSda}/>
          <${TextInput} label="SCL" type="number" value=${scl} onInput=${setScl}/>
        </div>
        <p class="muted small">${t('hw.openHelp')}</p>
        <div class="form-a">
          <${Btn} onClick=${() => act('sys.bus', 'i2cClose', {}, t('done')).then(() => loadDoc('sys.bus').catch(() => {}))}>${t('hw.close')}<//>
          <${Btn} kind="primary" icon="cable" disabled=${sda === '' || scl === ''} onClick=${open}>${t('hw.open')}<//>
        </div>
      <//>
    </div>`}
    ${hasLora && html`<${Lora}/>`}
  </div>`;
}

function Lora() {
  const lora = useDoc('sys.lora');
  if (!lora) return null;
  const s = lora.stats || {};
  const lim = lora.limits || {};
  return html`<${Card} title="LoRa" icon="satellite-dish" sub=${lora.chip + ' · ' + lora.band + ' MHz'}
      actions=${html`<${Switch} on=${lora.enabled} label=${t('hw.radioOn')}
        onChange=${(v) => act('sys.lora', 'enable', { on: v }).then(() => loadDoc('sys.lora').catch(() => {}))}/>`}>
    <div class="grid2">
      <${KV} rows=${[
        [t('hw.state'), lora.state],
        [t('hw.freq'), (lora.freq / 1e6).toFixed(3) + ' MHz'],
        ['BW / SF / CR', (lora.bw / 1000) + ' kHz / SF' + lora.sf + ' / 4/' + lora.cr],
        [t('hw.power'), lora.power + ' dBm'],
        [t('hw.channelRssi'), lora.channelRssi !== undefined ? lora.channelRssi + ' dBm' : null]]}/>
      <div>
        <${KV} rows=${[[t('hw.sent'), s.sent], [t('hw.received'), s.received],
                       [t('hw.damaged'), s.damaged || null], [t('hw.refused'), s.refused || null]]}/>
        ${lim.airtimeHourMs && html`<${Meter} label=${t('hw.airtime')}
          value=${lim.airtimeHourMs - lim.airtimeLeftMs} max=${lim.airtimeHourMs}
          detail=${Math.round(lim.airtimeLeftMs / 1000) + ' s ' + t('hw.left')}/>`}
      </div>
    </div>
  <//>`;
}

// ---------------------------------------------------------------- settings

export function Settings() {
  const cfg = useStore(store, (s) => s.cfgs['sys.config']);
  const web = useDoc('sys.web');
  const [draft, setDraft] = useState({});
  const [pw, setPw] = useState({ old: '', new1: '', new2: '' });
  const [theme, setTheme] = useState(prefs.get('theme', 'auto'));
  useEffect(() => {
    loadCfg('sys.config').catch(() => {});
  }, []);
  if (!cfg) return html`<${Loading}/>`;
  const v = (k) => (k in draft ? draft[k] : cfg[k]);
  const set = (k) => (x) => setDraft(Object.assign({}, draft, { [k]: x }));
  const hostBad = draft.hostname !== undefined && !/^[a-z0-9]([a-z0-9-]{0,30}[a-z0-9])?$/i.test(draft.hostname);

  const saveDevice = async () => {
    const r = await act('sys.config', 'set', draft, t('saved'));
    if (r.ok) {
      setDraft({});
      loadCfg('sys.config').catch(() => {});
    }
  };
  const savePassword = async () => {
    const r = await act('sys.web', 'setPassword', { old: pw.old, new: pw.new1 }, t('set.pwSaved'));
    if (r.ok) setPw({ old: '', new1: '', new2: '' });
  };
  const restart = async () => {
    const yes = await confirmDialog({ title: t('set.restart'), text: t('set.restartText') });
    if (!yes) return;
    const r = await act('sys.system', 'restart', {});
    if (r.ok) awaitRestart('restart');
  };
  const factory = async () => {
    const yes = await confirmDialog({ title: t('set.factory'), text: t('set.factoryText'), danger: true,
                                      okLabel: t('set.factoryOk') });
    if (!yes) return;
    const r = await act('sys.system', 'factoryReset', {});
    if (r.ok) awaitRestart('factory');
  };
  const pickTheme = (x) => {
    setTheme(x);
    prefs.set('theme', x);
    store.set({ theme: x });
  };
  const pwBad = pw.new1 && pw.new1 !== pw.new2;

  return html`<div class="space"><div class="grid2">
    <${Card} title=${t('set.device')} icon="cpu">
      <${TextInput} label=${t('set.name')} value=${v('deviceName')} onInput=${set('deviceName')}
        maxLength=${32} hint=${t('set.nameHint')}/>
      <${TextInput} label=${t('set.host')} value=${v('hostname')} onInput=${set('hostname')} maxLength=${32}
        error=${hostBad ? t('set.hostBad') : ''} hint=${v('hostname') + '.local'}/>
      <${Select} label=${t('set.lang')} value=${v('lang')} onChange=${set('lang')}
        options=${LANGS.map((l) => ({ id: l.id, label: l.label }))} hint=${t('set.langHint')}/>
      <div class="form-a"><${Btn} kind="primary" icon="check" disabled=${!Object.keys(draft).length || hostBad}
        onClick=${saveDevice}>${t('btn.save')}<//></div>
    <//>
    <${Card} title=${t('set.access')} icon="key-round">
      ${web && web.auth && html`<${KV} rows=${[
        [t('set.user'), web.auth.user],
        [t('set.pwState'), web.auth.ownPassword ? html`<${Badge} kind="ok">${t('set.own')}<//>`
                                               : html`<${Badge} kind="warn">${t('set.apPw')}<//>`],
        [t('set.sessions'), web.auth.sessions + ' / ' + web.auth.sessionsMax]]}/>`}
      <${PasswordInput} label=${t('set.oldPw')} value=${pw.old} onInput=${(x) => setPw(Object.assign({}, pw, { old: x }))}
        autocomplete="current-password"/>
      <div class="row2">
        <${PasswordInput} label=${t('set.newPw')} value=${pw.new1} onInput=${(x) => setPw(Object.assign({}, pw, { new1: x }))}/>
        <${PasswordInput} label=${t('set.newPw2')} value=${pw.new2} onInput=${(x) => setPw(Object.assign({}, pw, { new2: x }))}
          error=${pwBad ? t('set.pwDiffer') : ''}/>
      </div>
      <p class="muted small">${t('set.pwHelp')}</p>
      <div class="form-a">
        <${Btn} icon="log-out" onClick=${() => act('sys.web', 'logoutAll', {}, t('done')).then(logout)}>${t('set.logoutAll')}<//>
        <${Btn} kind="primary" icon="check" disabled=${pwBad || (!pw.new1 && !pw.old)} onClick=${savePassword}>${t('set.savePw')}<//>
      </div>
    <//>
    <${Card} title=${t('set.look')} icon="sun">
      <div class="field"><label>${t('set.theme')}</label>
        <div class="seg">${['auto', 'dark', 'light'].map((x) => html`<button key=${x} type="button"
          class=${theme === x ? 'on' : ''} onClick=${() => pickTheme(x)}>
          <${Icon} name=${x === 'dark' ? 'moon' : x === 'light' ? 'sun' : 'monitor-smartphone'} size=${16}/>
          ${t('set.theme.' + x)}</button>`)}</div>
      </div>
      <p class="muted small">${t('set.lookHelp')}</p>
    <//>
    <${Card} title=${t('set.system')} icon="power">
      <p class="muted">${t('set.systemHelp')}</p>
      <div class="form-a">
        <${Btn} icon="rotate-ccw" onClick=${restart}>${t('set.restart')}<//>
        <${Btn} kind="danger" icon="triangle-alert" onClick=${factory}>${t('set.factory')}<//>
      </div>
    <//>
  </div></div>`;
}
