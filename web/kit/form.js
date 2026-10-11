// LoopMaxPro console - forms drawn from what the device says.
// The firmware publishes, for every setting and every argument, its type, its limits and its
// default: the console builds the form from that, and knows nothing of the module.
//
//   <${SchemaForm} schema=${manifest.config} values=${cfg} onSave=${(changed) => ...}/>
//   <${CommandForm} command=${manifestCommand} onRun=${(args) => ...}/>
import { html, useEffect, useState } from '../vendor/preact-htm.js';
import { Btn, PasswordInput, Switch, TextInput } from './ui.js';
import { getLang, t, tMaybe } from './i18n.js';

const INTEGER = ['u8', 'u16', 'u32', 'i32', 'i64'];
const NUMBER = INTEGER.concat(['f32', 'f64']);

// "pulseMs" -> "Pulse ms": the name when no text is known.
function humanize(key) {
  const words = key.replace(/([a-z0-9])([A-Z])/g, '$1 $2').replace(/[._]/g, ' ');
  return words.charAt(0).toUpperCase() + words.slice(1);
}

// The label of a field: the text of the module or of the console, or the name made readable.
// What a command does, in the language of the console: the firmware says it in English.
// cmd.<target>.<name> for a command of one target, cmd.<name> for the parts of the SDK.
export function commandHelp(target, command) {
  return tMaybe('cmd.' + target + '.' + command.name) ||
    (target.startsWith('mod.') && tMaybe('cmd.' + command.name)) || command.help || '';
}

export function fieldLabel(scope, key) {
  return tMaybe(scope + '.' + key) || tMaybe('field.' + key) || humanize(key);
}

function limitsHint(f) {
  if (!NUMBER.includes(f.type)) {
    return f.type === 'str' && f.max ? t('form.maxChars', { n: f.max }) : '';
  }
  const huge = 1e15;
  const hasMin = f.min !== undefined && Math.abs(f.min) < huge;
  const hasMax = f.max !== undefined && Math.abs(f.max) < huge;
  if (hasMin && hasMax) return t('form.range', { min: f.min, max: f.max });
  return '';
}

// A value typed in a field, as the device wants it. undefined when it is not valid.
function parseValue(f, text) {
  if (f.type === 'bool') return !!text;
  if (f.type === 'str') {
    if (f.max !== undefined && text.length > f.max) return undefined;
    if (f.min !== undefined && text.length < f.min) return undefined;
    return text;
  }
  if (text === '' || text === null) return undefined;
  const n = Number(text);
  if (!isFinite(n)) return undefined;
  if (INTEGER.includes(f.type) && !Number.isInteger(n)) return undefined;
  if (f.min !== undefined && n < f.min) return undefined;
  if (f.max !== undefined && n > f.max) return undefined;
  return n;
}

function oneField(f, scope, value, onValue, error, isSet) {
  const label = fieldLabel(scope, f.key || f.name);
  if (f.locked) {
    // A setting of a fuller version of the module: there, off, and it says why.
    const why = '🔒 ' + (tMaybe(scope + '.' + f.key + '.locked') || t('form.locked'));
    if (f.type !== 'bool') {
      return html`<${TextInput} key=${f.key} label=${label} value=${value} hint=${why} disabled=${true}
        onInput=${() => {}}/>`;
    }
    return html`<div class="field sw locked" key=${f.key}>
      <${Switch} on=${!!value} disabled=${true} label=${label}/>
      <small>${why}</small>
    </div>`;
  }
  if (f.type === 'bool') {
    return html`<div class="field sw" key=${f.key || f.name}>
      <${Switch} on=${!!value} onChange=${onValue} label=${label}/>
    </div>`;
  }
  if (f.secret) {
    return html`<${PasswordInput} key=${f.key || f.name} label=${label} value=${value}
      onInput=${onValue} isSet=${isSet} error=${error}/>`;
  }
  const numeric = NUMBER.includes(f.type);
  return html`<${TextInput} key=${f.key || f.name} label=${label} value=${value}
    onInput=${onValue} type=${numeric ? 'number' : 'text'} hint=${limitsHint(f)}
    min=${numeric ? f.min : undefined} max=${numeric ? f.max : undefined}
    step=${INTEGER.includes(f.type) ? 1 : 'any'} maxLength=${f.type === 'str' ? f.max : undefined}
    error=${error}/>`;
}

// Settings of a target. Only what changed is given to onSave; onSave returns the outcome of
// the command, and the field it names is shown in red.
export function SchemaForm({ schema, values, scope = 'cfg', onSave, onReset, busy = false }) {
  const [draft, setDraft] = useState({});
  const [errors, setErrors] = useState({});
  const [saving, setSaving] = useState(false);
  useEffect(() => setDraft({}), [values]);

  const fields = (schema || []).filter((f) => !f.readOnly);
  const open = fields.filter((f) => !f.locked);
  const shown = (f) => {
    if (f.key in draft) return draft[f.key];
    if (f.secret) return '';
    const v = values ? values[f.key] : undefined;
    return v === undefined ? f.def : v;
  };
  const changed = {};
  const bad = {};
  open.forEach((f) => {
    if (!(f.key in draft)) return;
    if (f.secret && draft[f.key] === '') return;
    const v = parseValue(f, draft[f.key]);
    if (v === undefined) bad[f.key] = limitsHint(f) || t('form.invalid');
    else if (!values || v !== values[f.key]) changed[f.key] = v;
  });
  const dirty = Object.keys(changed).length > 0;

  const save = async (e) => {
    e.preventDefault();
    if (!dirty || Object.keys(bad).length) return;
    setSaving(true);
    const r = await onSave(changed);
    setSaving(false);
    if (r && !r.ok && r.field) setErrors({ [r.field]: t('form.refused') });
    else if (r && r.ok) {
      setErrors({});
      setDraft({});
    }
  };

  return html`<form class="form" onSubmit=${save}>
    ${fields.map((f) => oneField(f, scope, shown(f),
                                 (v) => setDraft(Object.assign({}, draft, { [f.key]: v })),
                                 bad[f.key] || errors[f.key],
                                 values ? values[f.key + 'Set'] : undefined))}
    <div class="form-a">
      ${onReset && html`<${Btn} icon="rotate-ccw" onClick=${onReset}>${t('btn.defaults')}<//>`}
      ${dirty && html`<${Btn} onClick=${() => setDraft({})}>${t('btn.cancel')}<//>`}
      <${Btn} kind="primary" type="submit" icon="check" busy=${saving || busy}
        disabled=${!dirty || Object.keys(bad).length > 0}>${t('btn.save')}<//>
    </div>
  </form>`;
}

// A command of any target, with the arguments the manifest describes.
// What a field of a command starts with. An argument called lang starts with the language of
// the console: a name typed by the user is in that language.
function fallback(a) {
  if (a.type === 'bool') return false;
  if (a.name === 'lang' && a.type === 'str') return getLang();
  return '';
}

export function CommandForm({ command, scope = 'arg', onRun }) {
  const [draft, setDraft] = useState({});
  const [busy, setBusy] = useState(false);
  const args = command.args || [];
  const values = {};
  let invalid = false;
  args.forEach((a) => {
    const text = a.name in draft ? draft[a.name] : fallback(a);
    if (a.type !== 'bool' && text === '') {
      if (a.required) invalid = true;
      return;
    }
    const v = parseValue(Object.assign({ key: a.name }, a), text);
    if (v === undefined) invalid = true;
    else values[a.name] = v;
  });
  const run = async (e) => {
    e.preventDefault();
    if (invalid) return;
    setBusy(true);
    await onRun(values);
    setBusy(false);
  };
  return html`<form class="form cmd" onSubmit=${run}>
    ${args.map((a) => oneField(Object.assign({ key: a.name }, a), scope,
                               a.name in draft ? draft[a.name] : fallback(a),
                               (v) => setDraft(Object.assign({}, draft, { [a.name]: v })), ''))}
    <div class="form-a">
      <${Btn} kind=${command.confirm ? 'danger' : 'primary'} type="submit" icon="play"
        busy=${busy} disabled=${invalid}>${command.name}<//>
    </div>
  </form>`;
}
