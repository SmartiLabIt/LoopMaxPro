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

export function t(key, vars) {
  const dict = DICTS[currentLang] || DICT_EN;
  let text = dict[key];
  if (text === undefined) text = DICT_EN[key];
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
  return dict[key] !== undefined ? dict[key] : DICT_EN[key];
}
