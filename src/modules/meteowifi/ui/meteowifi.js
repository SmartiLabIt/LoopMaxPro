// LoopMaxPro - the interface of MeteoWifi: the tab where the city of the weather is chosen.
// The home, the settings and the info stay the standard ones.
//
// The towns are looked for by the browser, not by the device: a name alone can be a hundred
// towns (San Marco: 48 in Italy only), and the device has no room to hold them or to show them.
// The browser asks the search service of Open-Meteo, shows every town it gives with province,
// region and country, the most populous first, and who looks chooses; the device then gets
// the name with its coordinates, and has nothing to guess. Any town of the world the service
// knows. Without Internet on the phone the device looks for the name itself and takes the
// most likely town (command "location").
// It imports nothing: Preact and the pieces of the console come from ctx (docs/web-console.md).

const SEARCH = 'https://geocoding-api.open-meteo.com/v1/search';
const MOST = 100;   // the service gives no more for one question

const STYLE = `
.mw-now { display: flex; align-items: center; gap: 10px; flex-wrap: wrap; }
.mw-now b { font-size: 1.1rem; }
.mw-now .btn { margin-left: auto; }
.mw-form { display: flex; gap: 10px; align-items: flex-end; }
.mw-form .field { flex: 1; min-width: 0; margin: 0; }
.mw-help { color: var(--muted); font-size: .85rem; margin: 8px 0 0; }
.mw-count { color: var(--muted); font-size: .85rem; margin: 14px 0 6px; }
.mw-list { list-style: none; margin: 0; padding: 0; display: grid; gap: 8px; }
.mw-town { width: 100%; display: flex; align-items: center; gap: 12px; text-align: left;
  padding: 12px 14px; min-height: 56px; border-radius: var(--radius-s); border: 1px solid var(--line);
  background: var(--panel2); color: var(--text); font: inherit; cursor: pointer; }
.mw-town:hover, .mw-town:focus-visible { border-color: var(--accent); }
.mw-town:disabled { opacity: .6; cursor: default; }
.mw-t { flex: 1; min-width: 0; display: grid; gap: 2px; }
.mw-t b { overflow-wrap: anywhere; }
.mw-t small { color: var(--muted); overflow-wrap: anywhere; }
.mw-p { flex: none; color: var(--muted); font-size: .85rem; font-variant-numeric: tabular-nums;
  white-space: nowrap; }
`;

// "Milano", "Milano, IT" or "Milano IT": the two letters of a country narrow the search.
function parse(text) {
  const clean = text.trim().replace(/\s+/g, ' ');
  const m = /^(.*?)[\s,]+([A-Za-z]{2})$/.exec(clean);
  if (m && m[1].replace(/,$/, '').trim()) {
    return { name: m[1].replace(/,$/, '').trim(), country: m[2].toUpperCase() };
  }
  return { name: clean.replace(/,$/, '').trim(), country: '' };
}

async function ask(name, lang, country) {
  const url = SEARCH + '?name=' + encodeURIComponent(name) + '&count=' + MOST + '&language=' + lang +
              '&format=json' + (country ? '&countryCode=' + country : '');
  const r = await fetch(url);
  if (!r.ok) throw new Error('http ' + r.status);
  const body = await r.json();
  return Array.isArray(body.results) ? body.results : [];
}

// The service looks for a name in the language asked, and only there: "Roma" in English is not
// the capital, which is "Rome". So the name is asked in the language of the console, in the one
// of the country written and in English, and every town ends in one list, once.
async function findTowns(text, lang) {
  const { name, country } = parse(text);
  if (name.length < 2) return [];
  const langs = [];
  [lang, country.toLowerCase(), (navigator.language || '').slice(0, 2).toLowerCase(), 'en']
    .forEach((l) => { if (/^[a-z]{2}$/.test(l) && !langs.includes(l)) langs.push(l); });
  const answers = await Promise.all(langs.map((l) => ask(name, l, country).catch(() => null)));
  if (answers.every((a) => a === null)) throw new Error('unreachable');
  let towns = [];
  const seen = new Set();
  answers.forEach((list) => (list || []).forEach((town) => {
    if (!town || !town.name || seen.has(town.id)) return;
    seen.add(town.id);
    towns.push(town);
  }));
  // Two letters that were not a country ("Bagno a Ripoli FI" has none in Finland).
  if (!towns.length && country && /\s/.test(text.trim()) && !text.includes(',')) {
    const whole = text.trim().replace(/\s+/g, ' ');
    const again = await Promise.all(langs.map((l) => ask(whole, l, '').catch(() => null)));
    again.forEach((list) => (list || []).forEach((town) => {
      if (!town || !town.name || seen.has(town.id)) return;
      seen.add(town.id);
      towns.push(town);
    }));
  }
  // The most populous first: Milano before the hamlets and the towns abroad with its name.
  const wanted = name.toLowerCase();
  const exact = (town) => (town.name.toLowerCase() === wanted ? 1 : 0);
  towns.sort((a, b) => (b.population || 0) - (a.population || 0) || exact(b) - exact(a) ||
                       a.name.localeCompare(b.name));
  return towns;
}

// Province and region, then the country: what tells two towns with the same name apart.
function whereIs(town) {
  const parts = [];
  [town.admin3, town.admin2, town.admin1].forEach((p) => {
    if (p && p !== town.name && !parts.includes(p)) parts.push(p);
  });
  const land = town.country || town.country_code || '';
  return parts.slice(-2).join(', ') + (parts.length && land ? ' · ' : '') + land;
}

function Place({ ctx }) {
  const { html, ui, t, useState } = ctx;
  const doc = ctx.useDoc();
  const [text, setText] = useState('');
  const [towns, setTowns] = useState(null);   // null: nothing asked yet
  const [busy, setBusy] = useState(false);
  const [failed, setFailed] = useState(false);
  const [taking, setTaking] = useState(null);
  if (!doc) return html`<${ui.Spinner} size=${28}/>`;

  const current = doc.place || (doc.now && doc.now.label) || '';
  const people = (n) => new Intl.NumberFormat(ctx.lang(), { notation: 'compact',
                                                           maximumFractionDigits: 1 }).format(n);
  const search = async (e) => {
    if (e) e.preventDefault();
    if (text.trim().length < 2 || busy) return;
    setBusy(true);
    setFailed(false);
    try {
      setTowns(await findTowns(text, ctx.lang()));
    } catch (err) {
      setTowns(null);
      setFailed(true);
    }
    setBusy(false);
  };
  const take = async (town) => {
    setTaking(town.id);
    const label = (town.name.slice(0, 44) + (town.country_code ? ', ' + town.country_code : ''));
    const r = await ctx.saveConfig({ place: label, lat: Number(town.latitude.toFixed(5)),
                                     lon: Number(town.longitude.toFixed(5)) });
    setTaking(null);
    if (r.ok) {
      setTowns(null);
      setText('');
    }
  };
  // The phone does not reach the service: the device looks for the name, and takes the most
  // likely town.
  const byDevice = async () => {
    setBusy(true);
    const r = await ctx.act('location', { name: text.trim(), lang: ctx.lang() }, t('saved'));
    setBusy(false);
    if (r.ok) setFailed(false);
  };
  const here = () => ctx.act('location', {}, t('saved'));

  return html`<style>${STYLE}</style>
    <${ui.Card} title=${t('mod.meteowifi.place')} icon="map-pin" cls="narrow">
      <div class="mw-now">
        <${ui.Icon} name="map-pin" size=${18}/>
        <b>${current || t('mod.meteowifi.none')}</b>
        ${doc.place && html`<${ui.Btn} small icon="map-pin" onClick=${here}>
          ${t('mod.meteowifi.here')}<//>`}
      </div>
      ${!doc.place && html`<p class="mw-help">${t('mod.meteowifi.hereNow')}</p>`}
    <//>
    <${ui.Card} title=${t('mod.meteowifi.change')} icon="search" cls="narrow">
      <form class="mw-form" onSubmit=${search}>
        <${ui.TextInput} label=${t('mod.meteowifi.name')} value=${text} onInput=${setText}
          placeholder=${t('mod.meteowifi.example')} maxLength=${48}/>
        <${ui.Btn} kind="primary" icon="search" type="submit" busy=${busy}
          disabled=${text.trim().length < 2}>${t('mod.meteowifi.search')}<//>
      </form>
      <p class="mw-help">${t('mod.meteowifi.help')}</p>
      ${failed && html`<${ui.Notice} kind="warn" action=${html`<${ui.Btn} small icon="search"
          busy=${busy} onClick=${byDevice}>${t('mod.meteowifi.byDevice')}<//>`}>
        ${t('mod.meteowifi.unreachable')}<//>`}
      ${towns && towns.length === 0 && html`<${ui.Notice} kind="warn">
        ${t('mod.meteowifi.nothing')}<//>`}
      ${towns && towns.length > 0 && html`
        <p class="mw-count">${t(towns.length >= MOST ? 'mod.meteowifi.many' : 'mod.meteowifi.count',
                                { n: towns.length })}</p>
        <ul class="mw-list">${towns.map((town) => html`<li key=${town.id}>
          <button type="button" class="mw-town" disabled=${taking !== null} onClick=${() => take(town)}>
            <span class="mw-t"><b>${town.name}</b><small>${whereIs(town)}</small></span>
            ${town.population > 0 && html`<span class="mw-p">${people(town.population)}
              ${' ' + t('mod.meteowifi.people')}</span>`}
            ${taking === town.id ? html`<${ui.Spinner}/>` : html`<${ui.Icon} name="chevron-right" size=${16}/>`}
          </button></li>`)}</ul>`}
    <//>`;
}

export default {
  uiApi: 1,
  texts: {
    it: {
      'mod.meteowifi.place': 'Città', 'mod.meteowifi.change': 'Cambia città',
      'mod.meteowifi.name': 'Nome della città', 'mod.meteowifi.example': 'Milano oppure Milano, IT',
      'mod.meteowifi.search': 'Cerca',
      'mod.meteowifi.help': 'Qualsiasi città del mondo. Con la sigla della nazione (IT, FR, US) l\'elenco si restringe. Tocca la città giusta nell\'elenco.',
      'mod.meteowifi.none': 'nessuna città',
      'mod.meteowifi.here': 'Usa la posizione del dispositivo',
      'mod.meteowifi.hereNow': 'Il meteo è quello della posizione del dispositivo.',
      'mod.meteowifi.count': '{n} città trovate, le più grandi prima',
      'mod.meteowifi.many': 'Le prime {n} città, le più grandi prima: aggiungi la nazione per restringere',
      'mod.meteowifi.nothing': 'Nessuna città con questo nome. Controlla come è scritto.',
      'mod.meteowifi.people': 'ab.',
      'mod.meteowifi.unreachable': 'Questo telefono o PC non raggiunge il servizio di ricerca. Il dispositivo può cercare il nome da solo e prendere la città più probabile.',
      'mod.meteowifi.byDevice': 'Cerca dal dispositivo',
    },
    en: {
      'mod.meteowifi.place': 'City', 'mod.meteowifi.change': 'Change city',
      'mod.meteowifi.name': 'Name of the city', 'mod.meteowifi.example': 'Milan or Milan, IT',
      'mod.meteowifi.search': 'Search',
      'mod.meteowifi.help': 'Any city of the world. With the two letters of the country (IT, FR, US) the list gets shorter. Tap the right city in the list.',
      'mod.meteowifi.none': 'no city',
      'mod.meteowifi.here': 'Use the position of the device',
      'mod.meteowifi.hereNow': 'The weather is the one of the position of the device.',
      'mod.meteowifi.count': '{n} cities found, the largest first',
      'mod.meteowifi.many': 'The first {n} cities, the largest first: add the country to narrow',
      'mod.meteowifi.nothing': 'No city with this name. Check how it is written.',
      'mod.meteowifi.people': 'pop.',
      'mod.meteowifi.unreachable': 'This phone or PC does not reach the search service. The device can look for the name itself and take the most likely city.',
      'mod.meteowifi.byDevice': 'Search from the device',
    },
  },
  tabs: [
    { id: 'place', icon: 'map-pin', label: 'mod.meteowifi.place', view: Place },
  ],
};
