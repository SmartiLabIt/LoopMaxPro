// LoopMaxPro console - the element <lmp-console> and the way from a view to another.
// The console opens on the module, which gives the device its purpose; the system has its
// own space, reached from the header.
//
//   <lmp-console css="lmp.3f9a1c07.css" hash></lmp-console>
//   css: the style sheet, loaded inside the element. hash: the address follows the view.
//   base: where the device answers, when the page does not come from it.
//   manual: the page gives the transport itself, with element.start(transport).
import { html, render, useEffect, useState } from '../vendor/preact-htm.js';
import { Tabs } from '../kit/ui.js';
import { prefs, useStore } from '../kit/store.js';
import { browserZone, zoneRules } from '../kit/tz.js';
import { t } from '../kit/i18n.js';
import { ApiError, LocalTransport } from './transport.js';
import { boot, command, docOf, leave, loadDoc, store, transport, useTransport } from './live.js';
import { PlaceHost, askPlace, placeWanted, takePlaceReturn } from './place.js';
import { ModuleUiTab, UI_API, forgetModuleUi, listenForModuleUi, loadSystemUis, startSystemParts,
         systemTabs } from './module-ui.js';
import { Banners, BootError, DialogHost, Footer, Header, Login, Restarting, Splash,
         ToastHost } from './shell.js';
import { ModuleSpace, moduleTabs, useModule } from './module-view.js';
import { Log, Network, Overview } from './sys-core.js';
import { Devices } from './devices.js';
import { Cloud, Hardware, Modules, Mqtt, Services, Settings, TimePlace, Update } from './sys-more.js';

// The tabs of the LoopMaxPro space. need: the service that must be in the build. A bridge can
// add its own, after the one of the cloud (module-ui.js).
const SYSTEM_TABS = [
  { id: 'overview', icon: 'gauge' },
  { id: 'network', icon: 'wifi', need: 'net' },
  { id: 'devices', icon: 'radar', need: 'net' },
  { id: 'modules', icon: 'puzzle', need: 'modules' },
  { id: 'log', icon: 'scroll-text' },
  { id: 'update', icon: 'download', need: 'ota' },
  { id: 'time', icon: 'clock' },
  { id: 'mqtt', icon: 'radio', need: 'mqtt' },
  { id: 'cloud', icon: 'cloud', need: 'cloud' },
  { id: 'hardware', icon: 'microchip' },
  { id: 'services', icon: 'server' },
  { id: 'settings', icon: 'settings' },
];

function parseHash() {
  const parts = (location.hash || '').replace(/^#\/?/, '').split('/').filter(Boolean);
  if (parts[0] === 'sys') return { space: 'sys', tab: parts[1] || 'overview' };
  if (parts[0] === 'm' && parts[1]) return { space: 'mod', mod: parts[1], tab: parts[2] || 'home' };
  return null;
}

function hashOf(route) {
  return route.space === 'sys' ? '#/sys/' + route.tab : '#/m/' + route.mod + '/' + route.tab;
}

function Shell({ useHash }) {
  const state = useStore(store, (s) => s.state);
  const report = useStore(store, (s) => s.report);
  const modules = (state && state.modules) || [];
  const services = ((report && report.services) || []).map((x) => x.name);
  const uis = useStore(store, (s) => s.uis);
  const firstRoute = () => {
    const fromHash = useHash ? parseHash() : null;
    if (fromHash && (fromHash.space === 'sys' || modules.some((m) => m.name === fromHash.mod))) {
      return fromHash;
    }
    const last = prefs.get('module', '');
    const mod = modules.find((m) => m.name === last) || modules[0];
    return mod ? { space: 'mod', mod: mod.name, tab: 'home' } : { space: 'sys', tab: 'overview' };
  };
  const [route, setRoute] = useState(firstRoute);

  const go = (next) => {
    setRoute(next);
    if (next.space === 'mod') prefs.set('module', next.mod);
    if (useHash && location.hash !== hashOf(next)) history.replaceState(null, '', hashOf(next));
    const main = document.querySelector('lmp-console');
    if (main && main.scrollIntoView) window.scrollTo({ top: 0 });
  };
  useEffect(() => {
    if (!useHash) return undefined;
    const onHash = () => {
      const r = parseHash();
      if (r) setRoute(r);
    };
    window.addEventListener('hashchange', onHash);
    return () => window.removeEventListener('hashchange', onHash);
  }, []);
  // The state of broker and server is in the header: read it once, the events keep it.
  useEffect(() => {
    if (services.includes('mqtt')) loadDoc('sys.mqtt').catch(() => {});
    if (services.includes('cloud')) {
      // On the device, with a bridge that asks its server for updates (it says updateChecking):
      // the update offered now, not the one of the last question (the device asks once a day).
      // The answer arrives with the event cloud.update. A free module has no bridge: nobody
      // is asked.
      loadDoc('sys.cloud').then((doc) => {
        const asks = doc && doc.attached && doc.detail && 'updateChecking' in doc.detail;
        if (asks && transport().kind === 'local') {
          // On a device that says it is short of memory (its lanes), not while the console
          // loads: the question is a secure connection of the device to the server, and with
          // the pages still travelling it has no memory for both (seen on the first ESP32).
          // On every other device at once, as always.
          setTimeout(() => {
            if (!document.hidden) command('sys.cloud', 'checkUpdate', {}, { background: true });
          }, transport().lanes ? 20000 : 0);
        }
      }).catch(() => {});
    }
    // On the device, a board that does not have the time of the network takes the one of who
    // looks, with its time zone while the board never had one: a device on its own access
    // point sees no other clock. The time of the network, when it comes, wins.
    if (services.includes('clock') && transport().kind === 'local') {
      loadDoc('sys.clock').then((doc) => {
        if (!doc || (doc.synced && doc.source !== 'client')) return null;
        const args = { unix: Math.floor(Date.now() / 1000) };
        const zone = browserZone();
        const rules = zone ? zoneRules(zone) : null;
        if (!doc.synced && doc.tz === 'UTC' && rules) Object.assign(args, { tz: zone, posix: rules });
        return command('sys.clock', 'set', args, { background: true })
          .then(() => loadDoc('sys.clock'));
      }).catch(() => {});
    }
  }, [services.length]);
  // The interfaces of the bridges are read now: they can add tabs and notices to this space,
  // and do something when the console opens. That waits for the report: it says which
  // services the firmware has.
  useEffect(() => {
    loadSystemUis(modules);
  }, [modules.length]);
  useEffect(() => {
    if (report) startSystemParts(uis);
  }, [uis, !!report]);
  // Where the device is. Back from the page that read the GPS of the phone: the window
  // opens again with the point. Otherwise it opens by itself once, for a device in a network
  // that knows only the city the network tells, or nothing.
  useEffect(() => {
    if (!services.includes('geo')) return;
    if (takePlaceReturn()) return;
    if (placeWanted(store.get())) askPlace({ device: true, auto: true });
  }, [services.includes('geo')]);

  const sysTabs = SYSTEM_TABS.filter((x) => !x.need || services.includes(x.need))
    .map((x) => ({ id: x.id, icon: x.icon, label: t('sys.' + x.id) }));
  const added = systemTabs(uis, services);
  const afterCloud = sysTabs.findIndex((x) => x.id === 'cloud');
  sysTabs.splice(afterCloud < 0 ? sysTabs.length : afterCloud + 1, 0,
    ...added.map(({ tab }) => ({ id: tab.id, icon: tab.icon, label: t(tab.label || tab.id) })));
  const goSys = (tab) => go({ space: 'sys', tab });
  // A module the browser remembers may be gone with a new firmware: then the first one.
  const openModule = (name) => {
    const mod = modules.find((m) => m.name === name) || modules[0];
    if (mod) go({ space: 'mod', mod: mod.name, tab: 'home' });
  };

  return html`<div class=${'shell ' + route.space}>
    <${Header} route=${route} modules=${modules}
      onModule=${openModule} onSystem=${() => goSys('overview')}
      onBack=${() => openModule(route.mod || prefs.get('module', ''))}/>
    ${route.space === 'sys'
      ? html`<${Tabs} cls="sys-tabs" items=${sysTabs} active=${route.tab} onSelect=${goSys}/>`
      : html`<${ModuleTabs} key=${route.mod} name=${route.mod} tab=${route.tab}
          onSelect=${(tab) => go({ space: 'mod', mod: route.mod, tab })}/>`}
    <main class="main">
      <${Banners} go=${goSys}/>
      ${route.space === 'sys' ? html`<${SystemView} tab=${route.tab} go=${goSys} openModule=${openModule}
          added=${added}/>`
        : html`<${ModuleSpace} key=${route.mod} entry=${modules.find((m) => m.name === route.mod)}
            tab=${route.tab}/>`}
    </main>
    <${Footer}/>
  </div>`;
}

function ModuleTabs({ name, tab, onSelect }) {
  const { doc, manifest, ui } = useModule(name);
  return html`<${Tabs} cls="mod-tabs" items=${moduleTabs(doc, manifest, ui)} active=${tab}
    onSelect=${onSelect}/>`;
}

function SystemView({ tab, go, openModule, added }) {
  switch (tab) {
    case 'network': return html`<${Network} go=${go}/>`;
    case 'devices': return html`<${Devices} go=${go}/>`;
    case 'modules': return html`<${Modules} openModule=${openModule}/>`;
    case 'log': return html`<${Log}/>`;
    case 'update': return html`<${Update}/>`;
    case 'time': return html`<${TimePlace}/>`;
    case 'mqtt': return html`<${Mqtt}/>`;
    case 'cloud': return html`<${Cloud}/>`;
    case 'hardware': return html`<${Hardware}/>`;
    case 'services': return html`<${Services}/>`;
    case 'settings': return html`<${Settings}/>`;
    default: {
      // A tab a bridge added: drawn by its interface, with what the console gives a module.
      const own = (added || []).find((x) => x.tab.id === tab);
      if (own) return html`<${ModuleUiTab} key=${tab} name=${own.name} tab=${own.tab}/>`;
      return html`<${Overview} go=${go}/>`;
    }
  }
}

function App({ useHash }) {
  const bootState = useStore(store, (s) => s.boot);
  const lang = useStore(store, (s) => s.lang);
  const theme = useStore(store, (s) => s.theme);
  // A command is with the device: nothing else can be pressed until it is back (live.js).
  const busy = useStore(store, (s) => !!s.busy);
  const view = bootState === 'ready' ? html`<${Shell} key=${lang} useHash=${useHash}/>`
    : bootState === 'login' ? html`<${Login}/>`
    : bootState === 'restarting' ? html`<${Restarting}/>`
    : bootState === 'error' ? html`<${BootError}/>`
    : html`<${Splash}/>`;
  return html`<div class=${'lmp' + (busy ? ' cmd' : '')} data-theme=${theme || 'auto'} lang=${lang}
      aria-busy=${busy}>
    ${busy && html`<div class="cmd-bar" role="progressbar"><i></i></div>`}
    ${view}<${ToastHost}/><${PlaceHost}/><${DialogHost}/>
  </div>`;
}

class LmpConsole extends HTMLElement {
  connectedCallback() {
    // A page that gives its own transport says so with "manual" and calls start(): the
    // console of the server. A transport put on the element before the script arrived is
    // taken as well.
    if (this.hasAttribute('manual')) return;
    const given = Object.prototype.hasOwnProperty.call(this, 'transport') ? this.transport : null;
    this.start(given || new LocalTransport(this.getAttribute('base') || ''));
  }

  disconnectedCallback() {
    this.stop();
  }

  // Starts the console on this transport: the methods of docs/web-console.md. On a console that
  // is running it stops it first: the same element moves from a device to another.
  start(transport) {
    if (this.lmpMount) this.stop();
    // The style and the place of the console are made once: an element keeps its shadow root.
    const root = this.shadowRoot || this.attachShadow({ mode: 'open' });
    const css = this.getAttribute('css');
    if (css && !root.querySelector('link')) {
      const link = document.createElement('link');
      link.rel = 'stylesheet';
      link.href = css;
      root.appendChild(link);
    }
    this.lmpMount = document.createElement('div');
    root.appendChild(this.lmpMount);
    store.set({ theme: prefs.get('theme', 'auto') });
    useTransport(transport);
    render(html`<${App} useHash=${this.hasAttribute('hash')}/>`, this.lmpMount);
    boot();
  }

  // Stops the console: what it draws goes, the stream of the device is closed, what it knew of
  // the device is forgotten. Nothing is left listening. start() can be called again, on the
  // same transport or on another. Stopping a console that is not running does nothing.
  stop() {
    if (!this.lmpMount) return;
    render(null, this.lmpMount);
    this.lmpMount.remove();
    this.lmpMount = null;
    forgetModuleUi();
    leave();
  }

  get running() {
    return !!this.lmpMount;
  }
}

// What a page that hosts the console can use: the transport of the device, to build another
// on it, and the error its methods throw.
window.LMP = Object.assign(window.LMP || {}, { uiApi: UI_API, LocalTransport, ApiError });
listenForModuleUi();
if (!customElements.get('lmp-console')) customElements.define('lmp-console', LmpConsole);
