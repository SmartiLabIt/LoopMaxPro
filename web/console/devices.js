// LoopMaxPro console - system space: the other devices of the system.
// The device looks for them, not the browser, which cannot search a network. The ones on its
// network answer its question (docs/network-and-home-assistant.md); the ones that are not in a network yet are known by
// their access point, among the networks it sees around.
import { html, useEffect, useState } from '../vendor/preact-htm.js';
import { Bars, Btn, Card, Empty, Notice, useTick } from '../kit/ui.js';
import { Icon } from '../kit/icons.js';
import { useStore } from '../kit/store.js';
import { t } from '../kit/i18n.js';
import { rssiBars } from '../kit/format.js';
import { command, docOf, loadDoc, statusText, store, toast, transport } from './live.js';

// The list of the network is asked again when the page opens, unless it is this fresh.
const NEAR_FRESH_SEC = 60;
// While the device says it is searching, its state is read again this often.
const FOLLOW_MS = 1500;

// The access point of a device of the system. The device marks them; one with a LoopMaxPro of
// before the mark is known by the name its access point is born with.
const isDeviceAp = (n) => n.device === true || /^LoopMax-/.test(n.ssid || '');

// How long ago a list was made, in words that do not change every second.
function listAge(net, list) {
  if (!list || list.state !== 'done' || list.ageSec === undefined) return '';
  const sec = list.ageSec + (net._at ? Math.floor((Date.now() - net._at) / 1000) : 0);
  const min = Math.floor(sec / 60);
  if (min < 1) return t('dev.now');
  return t('dev.ago', { t: min < 60 ? min + ' min' : Math.floor(min / 60) + ' ' + t('u.h') });
}

// Where the console of another device opens from here: its address in the network. A transport
// can say otherwise with peerUrl(device): the server opens its own page of a device it knows.
function peerLink(d) {
  const api = transport();
  const given = api && typeof api.peerUrl === 'function' ? api.peerUrl(d) : null;
  return given || 'http://' + d.ip + (d.port ? ':' + d.port : '') + '/';
}

// One of the two searches of the device, "near" or "scan", until it ends. An event tells the
// end; meanwhile the state of the device is read as well, so that a way to the device that
// brings the events late, or loses one, shows the result all the same.
async function searchFor(what) {
  const started = Date.now();
  let ended = null;
  const asked = command('sys.net', what, {}, { background: true }).then((r) => (ended = r));
  let seenRunning = false;
  while (!ended) {
    await Promise.race([asked, new Promise((resolve) => setTimeout(resolve, FOLLOW_MS))]);
    if (ended) break;
    const doc = await loadDoc('sys.net').catch(() => null);
    const list = doc && doc[what];
    if (!list || ended) continue;
    // A list made after the question is the answer; one of before is not.
    const sec = (Date.now() - started) / 1000;
    if (list.state === 'running') seenRunning = true;
    else if (list.state === 'done' && list.ageSec <= sec + 1) return { ok: true, status: 'ok', field: '', result: {} };
    else if (list.state === 'failed' && seenRunning) return { ok: false, status: 'io', field: '', result: {} };
  }
  await loadDoc('sys.net').catch(() => {});
  return ended;
}

export function Devices({ go }) {
  const net = useStore(store, (s) => docOf(s, 'sys.net'));
  const [busy, setBusy] = useState('');   // '', 'near', 'scan': the search that is going
  useTick(30000);

  const sta = (net && net.sta) || {};
  // On the network with the Wi-Fi or with a cable: the others are looked for the same way.
  const onNetwork = !!sta.connected || !!(net && net.eth && net.eth.connected);
  const near = net && net.near;
  const scan = (net && net.scan) || { state: 'idle', networks: [] };
  const canNear = !!near && onNetwork;

  // One after the other: while the radio looks for the networks around, it does not hear the
  // answers of the other devices. They are questions, they change nothing on the device: the
  // console is not held while they last.
  const run = async (what) => {
    const bad = [];
    for (const name of ['near', 'scan']) {
      if (!what[name]) continue;
      setBusy(name);
      const r = await searchFor(name);
      if (!r.ok) bad.push(r);
    }
    setBusy('');
    return bad;
  };
  const search = async () => {
    const bad = await run({ near: canNear, scan: true });
    if (bad.length) toast(statusText(bad[0].status, bad[0].field), 'err', 5000);
  };

  // Opened: the list of the network is asked again when it is old, it costs the device little.
  // The networks around only when they were never looked for: that search keeps the radio
  // busy, and the button is there for it.
  useEffect(() => {
    let gone = false;
    loadDoc('sys.net').then((doc) => {
      if (gone || !doc) return;
      const n = doc.near;
      const stale = n && n.state !== 'running' && (n.state !== 'done' || (n.ageSec || 0) > NEAR_FRESH_SEC);
      const onNetwork = (doc.sta && doc.sta.connected) || (doc.eth && doc.eth.connected);
      run({ near: !!(onNetwork && stale),
            scan: !!(doc.scan && doc.scan.state === 'idle') });
    }).catch(() => {});
    return () => { gone = true; };
  }, []);

  // A search started by the device itself, or from another console: followed until it ends.
  const going = !busy && ((near && near.state === 'running') || scan.state === 'running');
  useEffect(() => {
    if (!going) return undefined;
    const id = setInterval(() => loadDoc('sys.net').catch(() => {}), FOLLOW_MS);
    return () => clearInterval(id);
  }, [going]);

  if (!net) return null;
  const aps = (scan.networks || []).filter((n) => isDeviceAp(n) && n.ssid !== (net.ap && net.ap.ssid));
  const nearGoing = busy === 'near' || (near && near.state === 'running');
  const scanGoing = busy === 'scan' || scan.state === 'running';
  const remote = transport() && transport().kind !== 'local';

  return html`<div class="space">
    <section class=${'finder' + (busy ? ' on' : '')}>
      <span class="finder-ico"><${Icon} name="radar" size=${26}/></span>
      <div class="finder-t">
        <h3>${t('dev.title')}</h3>
        <p>${t(busy === 'near' ? 'dev.askingNet' : busy === 'scan' ? 'dev.askingAir' : 'dev.intro')}</p>
      </div>
      <${Btn} kind="primary" icon="search" busy=${!!busy} data-dev="search" onClick=${search}>
        ${t('dev.search')}<//>
    </section>

    <div class="grid2">
    <${Card} title=${t('dev.near')} icon="network" cls="dev-near"
        sub=${nearGoing ? t('net.scanning') : canNear && near.state === 'done'
          ? t('dev.found', { n: near.devices.length }) + ' · ' + listAge(net, near) : ''}>
      ${!near ? html`<${Notice}>${t('dev.noSearch')}<//>`
        : !onNetwork ? html`<${Notice} icon="wifi" action=${html`<${Btn} small kind="primary"
              onClick=${() => go('network')}>${t('ban.setup')}<//>`}>${t('dev.notConnected')}<//>`
        : near.devices.length ? html`<ul class="devs">
            ${near.devices.map((d) => html`<li key=${d.id || d.host}><${Peer} d=${d}/></li>`)}</ul>`
        : near.state === 'failed' && !nearGoing ? html`<${Notice} kind="warn">${t('dev.failed')}<//>`
        : html`<${Empty} icon="network">${t(nearGoing ? 'dev.askingNet'
            : near.state === 'done' ? 'dev.nearNone' : 'dev.notYet')}<//>`}
      ${near && onNetwork && html`<p class="muted small">
        ${remote && near.devices.length ? t('dev.homeOnly') + ' ' : ''}${t('dev.nearNote')}</p>`}
    <//>

    <${Card} title=${t('dev.setup')} icon="wifi" cls="dev-setup"
        sub=${scanGoing ? t('net.scanning') : scan.state === 'done'
          ? t('dev.found', { n: aps.length }) + ' · ' + listAge(net, scan) : ''}>
      ${aps.length ? html`<ul class="devs">
          ${aps.slice().sort((a, b) => b.rssi - a.rssi).map((n) => html`<li key=${n.ssid}>
            <div class="dev-i" data-dev-ap=${n.ssid}>
              <span class="dev-ico"><${Bars} level=${rssiBars(n.rssi)}/></span>
              <div class="dev-t"><b>${n.ssid}</b>
                <small>${n.rssi} dBm · ${t('net.ch')} ${n.channel}</small></div>
            </div></li>`)}</ul>`
        : scan.state === 'failed' && !scanGoing ? html`<${Notice} kind="warn">${t('dev.failed')}<//>`
        : html`<${Empty} icon="wifi-off">${t(scanGoing ? 'dev.askingAir'
            : scan.state === 'done' ? 'dev.setupNone' : 'dev.notYet')}<//>`}
      <p class="muted small">${t('dev.setupWhat')}</p>
      ${aps.length > 0 && html`<ol class="steps">
        <li>${t('dev.step1')}</li>
        <li>${t('dev.step2', { url: 'http://' + ((net.ap && net.ap.ip) || '192.168.4.1') })}</li>
        <li>${t('dev.step3')}</li>
      </ol>`}
    <//>
    </div>
  </div>`;
}

// Another device on the network: who it is, what it is, where. The whole row opens its console.
function Peer({ d }) {
  // What it is: the title it announces; its modules for one that says no title.
  const what = d.title || (d.mod || '').split(',').filter(Boolean).join(', ');
  const name = d.name || d.title || d.host;
  return html`<a class="dev-i" href=${peerLink(d)} target="_blank" rel="noopener"
      title=${t('dev.open')} data-dev-id=${d.id}>
    <span class="dev-ico">${d.icon || html`<${Icon} name="cpu" size=${20}/>`}</span>
    <div class="dev-t">
      <b>${name}</b>
      <span>${[name !== what && what, name !== d.host && d.host && d.host + '.local']
        .filter(Boolean).join(' · ')}</span>
      <small>${[d.ip, d.fw && 'v' + d.fw, d.board].filter(Boolean).join(' · ')}</small>
    </div>
    <${Icon} name="chevron-right" size=${16}/>
  </a>`;
}
