// LoopMaxPro console - the window that asks where something is.
// "Something" is the device, or what a module keeps: a thing with a place of its own. One window for
// all: the GPS of who looks, or the coordinates written or pasted in one field.
//
// A device in a network must have a place (LAN and cloud); on its own access point nobody
// needs it, and the window never opens by itself there. The order of the sources is of the
// device: a GPS on the board, then the owner (this window), then the network.
//
// The GPS of who looks: a browser gives it only to a page served in https. A console served
// in https asks it itself; the one of the device is not (http), so from there the button goes
// to a page in https that reads the GPS and comes back with the point in the address
// (#place=lat,lon), when the bridge of the device names one. What was being asked waits in
// the session of the browser meanwhile.
import { html, useState } from '../vendor/preact-htm.js';
import { Btn, Modal, Notice, TextInput } from '../kit/ui.js';
import { useStore } from '../kit/store.js';
import { t } from '../kit/i18n.js';
import { act, command, docOf, loadDoc, store, transport } from './live.js';

// The page that reads the GPS for a console served in http. It answers by going back to the
// address given in "back", with #place=<lat>,<lon>, or #place=denied, or #place=unavailable.
// The console knows no such page by itself: the bridge of the device names it in the document
// of the cloud (detail.placeHelper), or the page that hosts the console in
// window.LMP.placeHelper. Without one, the GPS is asked only where the browser gives it.
const WAITING = 'lmp.place';

function helperUrl(s) {
  const cloud = docOf(s || store.get(), 'sys.cloud');
  const named = (window.LMP && window.LMP.placeHelper)
    || (cloud && cloud.detail && cloud.detail.placeHelper) || '';
  return /^https:\/\//.test(named) ? named : '';
}

// "41.891933, 12.511334", as Google Maps copies it. A decimal comma, a link of Maps and the
// answer of the helper page are read too. Null when it is not a point on the Earth.
export function readPoint(text) {
  const s = String(text || '').trim();
  const inLink = /@(-?\d+\.\d+),(-?\d+\.\d+)/.exec(s) || /[?&]q=(-?\d+\.\d+),(-?\d+\.\d+)/.exec(s);
  let pair = inLink ? [inLink[1], inLink[2]] : null;
  if (!pair) {
    const found = s.match(/-?\d+(?:[.,]\d+)?/g) || [];
    if (found.length !== 2) return null;
    pair = found;
  }
  const lat = Number(pair[0].replace(',', '.'));
  const lon = Number(pair[1].replace(',', '.'));
  if (!isFinite(lat) || !isFinite(lon) || Math.abs(lat) > 90 || Math.abs(lon) > 180) return null;
  if (lat === 0 && lon === 0) return null;
  return { lat: Math.round(lat * 1e6) / 1e6, lon: Math.round(lon * 1e6) / 1e6 };
}

// A point, not the city the network tells.
export function precisePlace(geo) {
  return !!geo && !!geo.known && (geo.source === 'manual' || geo.source === 'gps');
}

// The device should be asked where it is: in a network, without a point, never asked before.
export function placeWanted(s) {
  const geo = docOf(s, 'sys.geo');
  const api = transport();
  if (!geo || !api || geo.asked || precisePlace(geo)) return false;
  return api.kind !== 'local' || !!(s.state && s.state.mode !== 'AP');
}

// Opens the window. For the device: { device: true, auto }. For what a module keeps:
// { title, now, target, name, args }, and saving sends the command name to target with args
// and the point (lat, lon).
export function askPlace(what) {
  store.set({ place: Object.assign({}, what) });
}

// Back from the page that read the GPS: the address has the point, the session what it was for.
// True when the window was opened again.
export function takePlaceReturn() {
  const m = /^#place=([^&]*)/.exec(location.hash || '');
  if (!m) return false;
  let what = null;
  try {
    what = JSON.parse(sessionStorage.getItem(WAITING) || 'null');
    sessionStorage.removeItem(WAITING);
  } catch (e) {
    // A browser that keeps nothing: the point is lost, the owner asks again.
  }
  history.replaceState(null, '', location.href.split('#')[0]);
  if (!what) return false;
  let answer = '';
  try {
    answer = decodeURIComponent(m[1]);
  } catch (e) {
    answer = '';
  }
  const point = readPoint(answer);
  askPlace(Object.assign(what, point ? { point: point.lat + ', ' + point.lon, got: true }
                                     : { failed: answer === 'denied' ? 'denied' : 'none' }));
  return true;
}

function goToHelper(what) {
  try {
    sessionStorage.setItem(WAITING, JSON.stringify({
      device: !!what.device, auto: !!what.auto, title: what.title || '', now: what.now || '',
      target: what.target || '', name: what.name || '', args: what.args || {} }));
  } catch (e) {
    // Without the session the way back cannot say what the point was for.
  }
  location.href = helperUrl() + '?back=' + encodeURIComponent(location.href.split('#')[0]);
}

// What the device knows now, in words.
function nowText(geo) {
  if (!geo || !geo.known) return t('place.nowNone');
  if (precisePlace(geo)) return t('place.nowPoint', { point: geo.lat + ', ' + geo.lon });
  return t('place.nowCity', { city: [geo.city, geo.country].filter(Boolean).join(', ') || '?' });
}

function PlaceDialog({ what }) {
  const state = useStore(store, (s) => s.state);
  const geo = useStore(store, (s) => docOf(s, 'sys.geo'));
  const [text, setText] = useState(what.point || '');
  // null, 'busy', 'denied', 'none', or the metres of the fix.
  const [gps, setGps] = useState(what.failed || (what.got ? 0 : null));
  const point = readPoint(text);
  const local = !!transport() && transport().kind === 'local';
  const secure = !!(window.isSecureContext && navigator.geolocation);
  // On its own access point the device has no way to the page that reads the GPS, and
  // without a bridge that names one there is no such page.
  const helper = useStore(store, (s) => helperUrl(s));
  const canGps = secure || (local && !!state && state.mode !== 'AP' && !!helper);
  // A GPS on the board comes first: with one the owner gives no point to the device.
  const byBoard = !!what.device && !!geo && geo.source === 'gps';

  const close = () => store.set({ place: null });
  const later = () => {
    if (what.device && what.auto) {
      command('sys.geo', 'later', {}, { background: true })
        .then(() => loadDoc('sys.geo')).catch(() => {});
    }
    close();
  };
  const save = async () => {
    const r = what.device
      ? await act('sys.geo', 'set', point, t('place.saved'))
      : await act(what.target, what.name, Object.assign({}, what.args, point), t('place.saved'));
    if (!r.ok) return;
    close();
    if (what.device) loadDoc('sys.geo').catch(() => {});
  };
  const fromGps = () => {
    if (!secure) {
      goToHelper(what);
      return;
    }
    setGps('busy');
    navigator.geolocation.getCurrentPosition(
      (p) => {
        setText(p.coords.latitude.toFixed(6) + ', ' + p.coords.longitude.toFixed(6));
        setGps(Math.max(1, Math.round(p.coords.accuracy || 0)));
      },
      (e) => setGps(e && e.code === 1 ? 'denied' : 'none'),
      { enableHighAccuracy: true, timeout: 20000, maximumAge: 0 });
  };

  const title = what.device ? t('place.title') : (what.title || t('place.title'));
  const told = gps === 'denied' ? html`<${Notice} kind="warn" icon="map-pin">${t('place.gpsDenied')}<//>`
    : gps === 'none' ? html`<${Notice} kind="warn" icon="map-pin">${t('place.gpsNone')}<//>`
    : typeof gps === 'number' ? html`<${Notice} kind="ok" icon="map-pin">
        ${gps > 0 ? t('place.gpsGot', { m: gps }) : t('place.gpsBack')}<//>`
    : null;

  return html`<${Modal} title=${title} icon="map-pin" onClose=${later}
      footer=${html`<${Btn} data-place="later" onClick=${later}>${t('place.later')}<//>
        <${Btn} kind="primary" icon="check" data-place="save" disabled=${!point || byBoard}
          onClick=${save}>${t('btn.save')}<//>`}>
    <p class="place-now" data-place="now">${what.device ? nowText(geo) : (what.now || t('place.nowNone'))}</p>
    ${byBoard ? html`<${Notice} icon="map-pin">${t('place.byBoard')}<//>` : html`
      ${canGps && html`<div class="place-gps">
        <${Btn} kind="primary" icon="map-pin" data-place="gps" busy=${gps === 'busy'}
          onClick=${fromGps}>${t('place.gps')}<//>
        ${!secure && html`<small class="muted">${t('place.gpsLeaves')}</small>`}
      </div>`}
      ${told}
      <${TextInput} label=${t(canGps ? 'place.orField' : 'place.field')} value=${text}
        placeholder="41.891933, 12.511334" hint=${t('place.fieldHint')}
        error=${text.trim() && !point ? t('place.bad') : ''}
        onInput=${(v) => { setText(v); if (typeof gps === 'number') setGps(null); }}/>`}
  <//>`;
}

export function PlaceHost() {
  const what = useStore(store, (s) => s.place);
  return what ? html`<${PlaceDialog} what=${what}/>` : null;
}
