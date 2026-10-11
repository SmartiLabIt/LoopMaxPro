// LoopMaxPro console - system space: overview, network, log.
import { html, useEffect, useMemo, useRef, useState } from '../vendor/preact-htm.js';
import { Badge, Bars, Btn, Card, Empty, KV, Meter, Modal, Notice, PasswordInput, Segmented,
         Select, Spinner, Stat, StateBadge, TextInput, stateKind, useTick } from '../kit/ui.js';
import { Icon } from '../kit/icons.js';
import { useStore } from '../kit/store.js';
import { t } from '../kit/i18n.js';
import { fmtBytes, fmtDeviceDate, fmtDeviceTime, fmtDuration, fmtUptimeMs, percent,
         rssiBars } from '../kit/format.js';
import { act, command, confirmDialog, deviceClock, docOf, loadDoc, loadReport, store,
         toast } from './live.js';

// ---------------------------------------------------------------- overview

export function Overview({ go }) {
  const report = useStore(store, (s) => s.report);
  const net = useStore(store, (s) => docOf(s, 'sys.net'));
  const clock = useStore(store, deviceClock);
  useTick(1000);
  // Memory and uptime change all the time: read again while the overview is open.
  useEffect(() => {
    const id = setInterval(() => {
      if (document.visibilityState === 'visible') loadReport().catch(() => {});
    }, 10000);
    return () => clearInterval(id);
  }, []);
  if (!report) return null;
  const d = report.device;
  const sys = report.system;
  const fs = report.storage && report.storage.fs;
  const passed = report._at ? Math.floor((Date.now() - report._at) / 1000) : 0;
  const services = report.services || [];
  // Partial is often normal, a board without a reset button: only errors ask for attention.
  const troubled = services.filter((x) => x.state === 'Error');

  return html`<div class="space">
    <section class="hero">
      <div class="hero-ico"><${Icon} name="cpu" size=${34}/></div>
      <div class="hero-t">
        <h2 class="glow">${d.name || d.host}</h2>
        <p>${d.board} · ${sys.chip}${sys.cores ? ' · ' + sys.cores + ' core' : ''}
          ${sys.cpuMhz ? ' · ' + sys.cpuMhz + ' MHz' : ''}</p>
        <div class="hero-b">
          <${Badge} kind="info" icon="shield-check">LoopMaxPro ${d.fw}<//>
          <${Badge} kind=${d.mode === 'LAN' ? 'ok' : 'warn'} icon=${d.mode === 'LAN' ? 'router' : 'wifi'}>
            ${t('mode.' + d.mode)}<//>
          ${sys.debug && html`<${Badge} kind="warn">debug<//>`}
        </div>
      </div>
    </section>

    <div class="stats">
      <${Stat} icon="timer" label=${t('ov.uptime')} value=${fmtDuration(sys.uptime + passed)}
        sub=${t('ov.reset') + ': ' + t('reset.' + sys.resetReason)}/>
      <${Stat} icon="memory-stick" label=${t('ov.heap')} value=${fmtBytes(sys.heap.free)}
        sub=${t('ov.heapMin', { n: fmtBytes(sys.heap.min) })}/>
      <${Stat} icon=${net && net.sta && net.sta.connected ? 'wifi' : 'router'} label=${t('ov.network')}
        value=${net && net.sta && net.sta.connected ? net.sta.ssid : net && net.ap && net.ap.up ? net.ap.ssid : '–'}
        sub=${net && net.sta && net.sta.connected ? net.sta.ip : net && net.ap ? net.ap.ip : ''}/>
      <${Stat} icon="clock" label=${t('ov.time')}
        value=${clock ? fmtDeviceTime(clock.unix, clock.offset) : t('clock.unknown')}
        sub=${clock ? fmtDeviceDate(clock.unix, clock.offset) + ' · ' + clock.tz : ''}
        kind=${clock ? '' : 'warn'}/>
    </div>

    ${troubled.length > 0 && html`<${Notice} kind="warn"
        action=${html`<${Btn} small onClick=${() => go('services')}>${t('ov.seeServices')}<//>`}>
      ${t('ov.troubled', { n: troubled.length })}: ${troubled.map((x) => x.name).join(', ')}<//>`}

    <div class="grid2">
      <${Card} title=${t('ov.resources')} icon="gauge">
        <${Meter} label=${t('ov.heapUsed')} value=${sys.heap.total - sys.heap.free} max=${sys.heap.total}
          detail=${fmtBytes(sys.heap.total - sys.heap.free) + ' / ' + fmtBytes(sys.heap.total)}/>
        ${sys.psram && html`<${Meter} label=${t('ov.psram')} value=${sys.psram.total - sys.psram.free}
          max=${sys.psram.total} detail=${fmtBytes(sys.psram.total - sys.psram.free) + ' / ' + fmtBytes(sys.psram.total)}/>`}
        ${fs && html`<${Meter} label=${t('ov.storage')} value=${fs.used} max=${fs.total}
          detail=${fmtBytes(fs.used) + ' / ' + fmtBytes(fs.total)}/>`}
        <${KV} rows=${[
          [t('ov.largest'), sys.heap.largest !== undefined ? fmtBytes(sys.heap.largest) : null],
          [t('ov.heapLowest'), fmtBytes(sys.heap.min)],
          [t('ov.flash'), fmtBytes(sys.flash)],
          [t('ov.sdk'), sys.sdk],
          [t('ov.revision'), sys.revision],
          [t('ov.build'), d.build]]}/>
      <//>
      <${Card} title=${t('ov.device')} icon="info">
        <${KV} rows=${[
          [t('ov.name'), d.name || html`<i class="muted">${t('ov.noName')}</i>`],
          [t('ov.host'), d.host],
          [t('ov.board'), d.board],
          [t('ov.code'), d.code || null],
          [t('ov.key'), html`<code class="key">${d.key}</code>`],
          [t('ov.lang'), d.lang]]}/>
      <//>
    </div>

    <${Card} title=${t('sys.services')} icon="server"
        actions=${html`<${Btn} small icon="chevron-right" onClick=${() => go('services')}>${t('btn.details')}<//>`}>
      <div class="chips">
        ${services.map((x) => html`<span key=${x.name} class=${'chip ' + stateKind(x.state)} title=${x.reason}>
          <i></i>${x.name}</span>`)}
      </div>
    <//>

    <${Releases} fw=${d.fw}/>
  </div>`;
}

// The versions of LoopMaxPro this console knows: the build writes here the newest one, from
// CHANGES.md (NOTES_KEEP of tools/web/build.py; the server keeps the history). On the device it
// is the version it runs; on the server the console can be newer than the device, and says that
// the device does not have it yet.
const RELEASES = [];

function releaseOrder(a, b) {
  const x = String(a).split(/[.\-+]/), y = String(b).split(/[.\-+]/);
  for (let i = 0; i < 3; i++) {
    const d = (parseInt(x[i], 10) || 0) - (parseInt(y[i], 10) || 0);
    if (d) return d;
  }
  return 0;
}

// A line for each version, closed: the text is read by opening the one that matters. Only the
// newest ones at first, so that the card stays short however many versions there are.
const RELEASES_SHOWN = 4;

function Releases({ fw }) {
  const [all, setAll] = useState(false);
  if (!RELEASES.length) return null;
  // The version of the device is always in sight, even when it is an old one.
  const here = RELEASES.findIndex((r) => releaseOrder(r.v, fw) === 0);
  const shown = all ? RELEASES : RELEASES.slice(0, Math.max(RELEASES_SHOWN, here + 1));
  return html`<${Card} title=${t('ov.releases')} icon="scroll-text">
    <div class="rels">
      ${shown.map((r) => {
        const order = releaseOrder(r.v, fw);
        return html`<details key=${r.v} class="acc rel" data-rel=${r.v}
            data-here=${order === 0 ? '1' : '0'}>
          <summary>
            <b>${r.v}</b>
            <small>${[order === 0 ? t('ov.relHere') : order > 0 ? t('ov.relNewer') : '', r.date]
              .filter(Boolean).join(' · ')}</small>
            <${Icon} name="chevron-right" size=${16}/>
          </summary>
          <div class="rel-b">
            ${r.text && html`<p>${r.text}</p>`}
            ${r.notes.length > 0 && html`<ul>${r.notes.map((n, i) => html`<li key=${i}>${n}</li>`)}</ul>`}
          </div>
        </details>`;
      })}
    </div>
    ${shown.length < RELEASES.length && html`<${Btn} small icon="chevron-right" onClick=${() => setAll(true)}>
      ${t('ov.relAll', { n: RELEASES.length })}<//>`}
  <//>`;
}

// ---------------------------------------------------------------- network

// The voice assistants people know by name: said where the device can reach them, which is
// through Home Assistant (docs/network-and-home-assistant.md).
export function Brands() {
  return html`<div class="chips brands">
    ${['Amazon Alexa', 'Google Home', t('ha.siri')].map((n) => html`<span key=${n}
      class="chip brand">${n}</span>`)}
  </div>`;
}

export function Network({ go }) {
  const net = useStore(store, (s) => docOf(s, 'sys.net'));
  const web = useStore(store, (s) => s.docs['sys.web']);
  // A device that can present itself to Home Assistant says so in the state of its MQTT.
  const mqtt = useStore(store, (s) => s.docs['sys.mqtt'] && s.docs['sys.mqtt'].home
    ? s.docs['sys.mqtt'] : null);
  const told = !!mqtt && mqtt.connected && mqtt.home.enabled;
  const cfg = useStore(store, (s) => s.cfgs['sys.net']);
  const [joining, setJoining] = useState(null);
  const [apForm, setApForm] = useState(false);
  useEffect(() => {
    loadDoc('sys.net').catch(() => {});
    loadDoc('sys.web').catch(() => {});
  }, []);
  if (!net) return html`<div class="loading"><${Spinner} size=${28}/></div>`;
  const sta = net.sta || {};
  const ap = net.ap || {};
  const scan = net.scan || { state: 'idle', networks: [] };

  const setMode = async (mode) => {
    const yes = await confirmDialog({ title: t('net.modeTitle'), text: t('net.modeText.' + mode) });
    if (yes) act('sys.net', 'setMode', { mode }, t('net.modeDone'));
  };
  const scanNow = async () => {
    const r = await command('sys.net', 'scan', {});
    if (!r.ok) toast(t('status.' + r.status), 'err');
    loadDoc('sys.net').catch(() => {});
  };

  return html`<div class="space">
    ${ap.up && ap.pswDefault && html`<${Notice} kind="warn"
      action=${html`<${Btn} small onClick=${() => setApForm(true)}>${t('net.changeAp')}<//>`}>
      ${t('net.defaultPsw')}<//>`}

    <div class="grid2">
      <${Card} title=${t('net.client')} icon="wifi"
          sub=${t('net.state.' + net.state)}>
        ${sta.connected ? html`
          <div class="signal"><${Bars} level=${rssiBars(sta.rssi)}/><b>${sta.ssid}</b>
            <span>${sta.rssi} dBm</span></div>
          <${KV} rows=${[
            [t('net.ip'), sta.ip], [t('net.gateway'), sta.gateway], [t('net.mask'), sta.mask],
            [t('net.dns'), sta.dns], [t('net.channel'), sta.channel], [t('net.mac'), sta.mac],
            [t('net.internet'), net.internetChecked === undefined ? null
              : html`<${Badge} kind=${net.internet ? 'ok' : net.internetChecked ? 'err' : 'off'}>
                  ${net.internet ? t('yes') : net.internetChecked ? t('no') : t('net.unchecked')}<//>`]]}/>`
        : html`
          <p class="muted">${sta.ssid ? t('net.tryingTo', { ssid: sta.ssid }) : t('net.noNetwork')}</p>
          <${KV} rows=${[[t('net.mac'), sta.mac], [t('net.failures'), sta.failures || null]]}/>`}
      <//>

      <${Card} title=${t('net.ap')} icon="router"
          sub=${ap.up ? t('net.apUp') : t('net.apDown')}
          actions=${html`<${Btn} small icon="pencil" onClick=${() => setApForm(true)}>${t('btn.edit')}<//>`}>
        <${KV} rows=${[
          [t('net.ssid'), ap.ssid], [t('net.ip'), ap.ip],
          [t('net.clients'), ap.up ? ap.clients : null], [t('net.channel'), ap.channel || null],
          [t('net.captive'), ap.captive ? t('yes') : t('no')],
          [t('net.password'), ap.pswDefault ? html`<${Badge} kind="warn">${t('net.factory')}<//>`
                                            : html`<${Badge} kind="ok">${t('net.own')}<//>`]]}/>
      <//>
    </div>

    ${mqtt && html`<${Notice} icon="house" kind=${told ? 'ok' : 'info'}
        action=${html`<${Btn} small kind=${told ? 'ghost' : 'primary'} icon="radio"
          data-net="mqtt" onClick=${() => go('mqtt')}>${t('net.haButton')}<//>`}>
      <b>${t('ha.title')}</b>
      <p>${told ? t('net.haTold', { n: mqtt.home.things })
        : t(sta.connected ? 'net.haHint' : 'net.haHintAp')}</p>
      <${Brands}/>
    <//>`}

    <div class="grid2">
      <${Card} title=${t('net.mode')} icon="network">
        <${Segmented} value=${net.mode} onChange=${setMode} options=${[
          { id: 'AP', label: t('mode.AP'), icon: 'router' },
          { id: 'LAN', label: t('mode.LAN'), icon: 'wifi' }]}/>
        <p class="muted small">${t('net.modeHelp')}</p>
        ${net.join && net.join.phase !== 'none' && html`<${Notice}>
          ${t('net.joinPhase', { ssid: net.join.ssid || '' })}: <b>${t('net.phase.' + net.join.phase)}</b>
          ${net.join.leftSec ? ' · ' + net.join.leftSec + ' s' : ''}<//>`}
        ${net.join && net.join.last && net.join.last !== 'none' && html`<p class="muted small">
          ${t('net.joinLast')}: ${t('net.result.' + net.join.last)}</p>`}
      <//>
      <${Card} title=${t('net.addresses')} icon="link">
        <ul class="links">
          ${((web && web.urls) || []).map((u) => html`<li key=${u}><a href=${u} target="_blank"
            rel="noopener">${u}</a></li>`)}
          <li>mDNS: ${net.mdns ? html`<b>${net.host}</b>` : t('no')}</li>
        </ul>
      <//>
    </div>

    <${Card} title=${t('net.around')} icon="search"
        sub=${scan.state === 'running' ? t('net.scanning') : scan.ageSec !== undefined ? t('net.scanAge', { n: scan.ageSec }) : ''}
        actions=${html`<${Btn} small icon="refresh-cw" busy=${scan.state === 'running'} onClick=${scanNow}>
          ${t('net.scan')}<//>`}>
      ${scan.networks && scan.networks.length ? html`<ul class="nets">
        ${scan.networks.slice().sort((a, b) => b.rssi - a.rssi).map((n) => html`
          <li key=${n.ssid + n.channel} onClick=${() => setJoining(n)}>
            <${Bars} level=${rssiBars(n.rssi)}/>
            <b>${n.ssid || html`<i>${t('net.hidden')}</i>`}</b>
            <small>${n.rssi} dBm · ${t('net.ch')} ${n.channel}</small>
            ${n.secured && html`<${Icon} name="lock" size=${14}/>`}
            ${sta.connected && sta.ssid === n.ssid && html`<${Badge} kind="ok">${t('net.current')}<//>`}
            <${Icon} name="chevron-right" size=${16}/>
          </li>`)}
      </ul>` : html`<${Empty} icon="wifi-off">${t('net.noScan')}<//>`}
      <div class="card-foot"><${Btn} small icon="plus" onClick=${() => setJoining({ ssid: '', secured: true })}>
        ${t('net.other')}<//></div>
    <//>

    ${joining && html`<${JoinDialog} network=${joining} onClose=${() => setJoining(null)}/>`}
    ${apForm && html`<${ApDialog} ap=${ap} onClose=${() => setApForm(false)}/>`}
  </div>`;
}

function JoinDialog({ network, onClose }) {
  const [ssid, setSsid] = useState(network.ssid);
  const [psw, setPsw] = useState('');
  const [busy, setBusy] = useState('');
  const [result, setResult] = useState(null);
  const args = () => (psw ? { ssid, psw } : { ssid });
  const test = async () => {
    setBusy('test');
    setResult(null);
    const r = await command('sys.net', 'test', args());
    setBusy('');
    setResult(r.ok ? { kind: 'ok', text: t('net.testOk') } : { kind: 'err', text: t('net.testFail') + ' (' + t('status.' + r.status) + ')' });
  };
  const join = async () => {
    const yes = await confirmDialog({ title: t('net.joinTitle'), text: t('net.joinText', { ssid }) });
    if (!yes) return;
    setBusy('join');
    const r = await command('sys.net', 'join', args());
    setBusy('');
    if (r.ok) {
      toast(t('net.joinOk', { ssid }), 'ok', 6000);
      onClose();
    } else {
      setResult({ kind: 'err', text: t('net.joinFail') + ' (' + t('status.' + r.status) + ')' });
    }
  };
  return html`<${Modal} title=${t('net.joinTitle')} icon="wifi" onClose=${onClose}
      footer=${html`<${Btn} onClick=${onClose}>${t('btn.cancel')}<//>
        <${Btn} icon="check" busy=${busy === 'test'} disabled=${!ssid || !!busy} onClick=${test}>${t('net.test')}<//>
        <${Btn} kind="primary" icon="wifi" busy=${busy === 'join'} disabled=${!ssid || !!busy} onClick=${join}>
          ${t('net.join')}<//>`}>
    <${TextInput} label=${t('net.ssid')} value=${ssid} onInput=${setSsid} maxLength=${32}
      autoFocus=${!network.ssid}/>
    ${network.secured !== false && html`<${PasswordInput} label=${t('net.password')} value=${psw}
      onInput=${setPsw} autoFocus=${!!network.ssid}/>`}
    <p class="muted small">${t('net.joinHelp')}</p>
    ${result && html`<${Notice} kind=${result.kind}>${result.text}<//>`}
  <//>`;
}

function ApDialog({ ap, onClose }) {
  const [ssid, setSsid] = useState(ap.ssid);
  const [psw, setPsw] = useState('');
  const [busy, setBusy] = useState(false);
  const bad = psw && psw.length < 8;
  const save = async () => {
    const args = {};
    if (ssid !== ap.ssid) args.ssid = ssid;
    if (psw) args.psw = psw;
    if (!Object.keys(args).length) return onClose();
    setBusy(true);
    const r = await act('sys.net', 'setAp', args, t('net.apSaved'));
    setBusy(false);
    if (r.ok) onClose();
  };
  return html`<${Modal} title=${t('net.ap')} icon="router" onClose=${onClose}
      footer=${html`<${Btn} onClick=${onClose}>${t('btn.cancel')}<//>
        <${Btn} kind="primary" icon="check" busy=${busy} disabled=${!ssid || bad} onClick=${save}>
          ${t('btn.save')}<//>`}>
    <${TextInput} label=${t('net.ssid')} value=${ssid} onInput=${setSsid} maxLength=${32}/>
    <${PasswordInput} label=${t('net.apPassword')} value=${psw} onInput=${setPsw}
      isSet=${true} error=${bad ? t('net.pswShort') : ''} hint=${t('net.apPswHint')}/>
    <p class="muted small">${t('net.apRestart')}</p>
  <//>`;
}

// ---------------------------------------------------------------- log

const LEVELS = ['error', 'warn', 'info', 'debug', 'trace'];

export function Log() {
  const log = useStore(store, (s) => s.log);
  const logDoc = useStore(store, (s) => s.docs['sys.log']);
  const report = useStore(store, (s) => s.report);
  const [level, setLevel] = useState('trace');
  const [src, setSrc] = useState('');
  const [text, setText] = useState('');
  const [paused, setPaused] = useState(null);
  const box = useRef(null);
  useEffect(() => {
    loadDoc('sys.log').catch(() => {});
  }, []);

  const shown = paused || log;
  const maxLevel = LEVELS.indexOf(level);
  const needle = text.toLowerCase();
  const rows = useMemo(() => shown.filter((e) => LEVELS.indexOf(e.lvl) <= maxLevel &&
                                                  (!src || e.src === src) &&
                                                  (!needle || e.msg.toLowerCase().includes(needle))),
                       [shown, maxLevel, src, needle]);
  useEffect(() => {
    if (!paused && box.current) box.current.scrollTop = box.current.scrollHeight;
  }, [rows.length, paused]);

  const sources = logDoc ? logDoc.sources : [];
  const icons = {};
  sources.forEach((x) => (icons[x.name] = x.icon));

  const setDeviceLevel = (lvl) => act('sys.log', 'setLevel', { level: lvl }, t('saved'))
    .then(() => loadDoc('sys.log').catch(() => {}));
  const clear = async () => {
    const yes = await confirmDialog({ title: t('log.clear'), text: t('log.clearText'), danger: true });
    if (!yes) return;
    const r = await act('sys.log', 'clear', {}, t('done'));
    if (r.ok) store.set({ log: [] });
  };
  const download = () => {
    const lines = log.map((e) => `${fmtUptimeMs(e.ms)} ${e.lvl.toUpperCase().padEnd(5)} [${e.src}] ${e.msg}`);
    const blob = new Blob([lines.join('\n') + '\n'], { type: 'text/plain' });
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = ((report && report.device.host) || 'loopmax') + '-log.txt';
    a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 1000);
  };

  return html`<div class="space">
    <${Card} title=${t('sys.log')} icon="scroll-text"
        sub=${logDoc ? t('log.sub', { n: logDoc.count, cap: logDoc.capacity, lvl: logDoc.level }) : ''}
        actions=${html`
          <${Btn} small icon=${paused ? 'play' : 'pause'} onClick=${() => setPaused(paused ? null : log)}>
            ${paused ? t('log.resume') : t('log.pause')}<//>
          <${Btn} small icon="download" onClick=${download} title=${t('log.download')}/>
          <${Btn} small icon="trash-2" onClick=${clear} title=${t('log.clear')}/>`}>
      <div class="log-tools">
        <${Select} label=${t('log.show')} value=${level} onChange=${setLevel}
          options=${LEVELS.map((l) => ({ id: l, label: t('lvl.' + l) }))}/>
        <${Select} label=${t('log.source')} value=${src} onChange=${setSrc}
          options=${[{ id: '', label: t('log.all') }].concat(sources.map((x) => ({ id: x.name, label: (x.icon ? x.icon + ' ' : '') + x.name })))}/>
        <${TextInput} label=${t('log.search')} value=${text} onInput=${setText}/>
        ${logDoc && html`<${Select} label=${t('log.deviceLevel')} value=${logDoc.level}
          onChange=${setDeviceLevel} options=${LEVELS.map((l) => ({ id: l, label: t('lvl.' + l) }))}/>`}
      </div>
      <div class="log" ref=${box}>
        ${rows.length === 0 ? html`<${Empty} icon="scroll-text">${t('log.empty')}<//>` :
          rows.map((e) => html`<div key=${e.seq} class=${'ln ' + e.lvl}>
            <time>${fmtUptimeMs(e.ms)}</time><span class="lv">${e.lvl}</span>
            <span class="src">${icons[e.src] || ''} ${e.src}</span><span class="msg">${e.msg}</span>
          </div>`)}
      </div>
      ${paused && html`<p class="muted small">${t('log.paused', { n: log.length - paused.length })}</p>`}
    <//>
  </div>`;
}
