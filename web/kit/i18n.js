// LoopMaxPro console - texts in the language of the device.
// The language is a setting of the device (sys.config lang): who changes it changes it for
// everybody, here and on the server. A text missing in a language is taken from English.
//
//   t('net.join')                      "Collega"
//   t('ota.bytes', { n: '1.2 MB' })    "{n} ricevuti" -> "1.2 MB ricevuti"
import { DICT_EN } from '../i18n/en.js';
import { DICT_IT } from '../i18n/it.js';

const DICTS = { en: DICT_EN, it: DICT_IT };
let currentLang = 'en';

export const LANGS = [
  { id: 'it', label: 'Italiano' },
  { id: 'en', label: 'English' },
];

export function setLang(lang) {
  currentLang = DICTS[lang] ? lang : 'en';
}

export function getLang() {
  return currentLang;
}

// The same keys for the same phrases (F31 of the server): a module whose phrases are in a file
// in common with another module says its prefix ('mod.expnet' -> 'share.net'), and a phrase that
// is one of the console says its key ('mod.espradio.search' -> 'net.scan'). The module keeps
// asking with its own keys; the phrase is kept once.
const SHARED = {};    // 'mod.expnet' -> ['share.net']
const SAME = {};      // 'mod.espradio.search' -> 'net.scan'

export function addShared(scope, prefixes, same) {
  if (prefixes && prefixes.length) SHARED[scope] = (SHARED[scope] || []).concat(prefixes);
  Object.keys(same || {}).forEach((key) => { SAME[key] = same[key]; });
}

function lookup(dict, key) {
  if (dict[key] !== undefined) return dict[key];
  const to = SAME[key];
  if (to !== undefined && dict[to] !== undefined) return dict[to];
  if (key.startsWith('mod.')) {
    const dot = key.indexOf('.', 4);
    const prefixes = dot > 0 ? SHARED[key.slice(0, dot)] : undefined;
    for (const p of prefixes || []) {
      const text = dict[p + key.slice(dot)];
      if (text !== undefined) return text;
    }
  }
  return undefined;
}

export function t(key, vars) {
  const dict = DICTS[currentLang] || DICT_EN;
  let text = lookup(dict, key);
  if (text === undefined) text = lookup(DICT_EN, key);
  if (text === undefined) return key;
  if (vars) text = text.replace(/\{(\w+)\}/g, (m, k) => (vars[k] === undefined ? m : vars[k]));
  return text;
}

// Texts brought by the interface of a module: { it: {...}, en: {...} }. The texts of the console
// stay as they are; a language the console does not have is left out.
export function addTexts(byLang) {
  Object.keys(byLang || {}).forEach((lang) => {
    const dict = DICTS[lang];
    const more = byLang[lang] || {};
    if (!dict) return;
    Object.keys(more).forEach((key) => {
      if (dict[key] === undefined && typeof more[key] === 'string') dict[key] = more[key];
    });
  });
}

// A text that may be missing: undefined instead of the key.
export function tMaybe(key) {
  const dict = DICTS[currentLang] || DICT_EN;
  const text = lookup(dict, key);
  return text !== undefined ? text : lookup(DICT_EN, key);
}
