// LoopMaxPro console - the space of a module: what gives the device its purpose.
// A module without an interface of its own is drawn from what the firmware publishes: its
// channels, its schedules, its settings, its commands. Nothing here knows the module.
// A module with an interface of its own (module-ui.js) adds its tabs, or takes the place of
// a standard one with a tab of the same id.
import { html, useEffect, useState } from '../vendor/preact-htm.js';
import { Badge, Btn, Card, Empty, JsonView, KV, Modal, Notice, Spinner, StateBadge, Switch,
         TextInput, useTick } from '../kit/ui.js';
import { CommandForm, SchemaForm, commandHelp, fieldLabel } from '../kit/form.js';
import { Icon } from '../kit/icons.js';
import { useStore } from '../kit/store.js';
import { getLang, t, tMaybe } from '../kit/i18n.js';
import { act, command, confirmDialog, deviceClock, docOf, loadCfg, loadDoc, loadManifest,
         statusText, store, toast } from './live.js';
import { ScheduleEditor, channelPlan, daysText, localParts, scheduleActive,
         spanText } from './schedules.js';
import { ModuleUiTab, loadModuleUi, stoppedText, uiTabs } from './module-ui.js';

// Commands the views below already give a place to.
// The last four are of every module: the things it shows to who reads them as data
// (the server, a home system). Not actions of the device for who looks at this page.
const DRAWN = ['set', 'toggle', 'all', 'flip', 'name', 'invert', 'restore', 'sched.add',
               'sched.update', 'sched.remove', 'sched.clear', 'config.set', 'config.reset',
               'things', 'things.values', 'thing.set', 'things.kept'];
// Members of the document the views below already show.
const SHOWN = ['config', 'pins', 'output', 'restore', 'schedules', 'schedulesMax',
               'schedulesRunning', 'now', 'days'];

// The tabs a module has, from its document, its manifest and its own interface. A tab of the
// interface with the id of a standard one takes its place; the others come after the home.
export function moduleTabs(doc, manifest, ui) {
  const tabs = [{ id: 'home', label: t('mod.tab.home'), icon: 'layout-grid' }];
  if (doc && Array.isArray(doc.schedules)) {
    tabs.push({ id: 'schedules', label: t('mod.tab.schedules'), icon: 'calendar-clock' });
  }
  if (manifest && manifest.config && manifest.config.length) {
    tabs.push({ id: 'settings', label: t('mod.tab.settings'), icon: 'settings' });
  }
  tabs.push({ id: 'info', label: t('mod.tab.info'), icon: 'info' });
  const own = uiTabs(ui);
  const mine = (x, label, icon) => ({ id: x.id, label: x.label ? t(x.label) : label,
                                      icon: x.icon || icon });
  const drawn = tabs.map((std) => {
    const x = own.find((o) => o.id === std.id);
    return x ? mine(x, std.label, std.icon) : std;
  });
  const more = own.filter((x) => !tabs.some((std) => std.id === x.id))
    .map((x) => mine(x, x.id, 'puzzle'));
  drawn.splice(1, 0, ...more);
  return drawn;
}

// The document, the manifest and the own interface of a module, read when it is opened.
// ui: undefined while it is being read, null when the module has none.
export function useModule(name) {
  const target = 'mod.' + name;
  const doc = useStore(store, (s) => s.docs[target]);
  const manifest = useStore(store, (s) => s.manifests[target]);
  const ui = useStore(store, (s) => s.uis[name]);
  useEffect(() => loadModuleUi(name), [name]);
  useEffect(() => {
    // Read fresh when opened; what does not arrive, as when the device is busy with other
    // connections, is asked again a few times: without it the tabs of the module are missing.
    let alive = true;
    let timer = null;
    const attempt = (left, fresh) => {
      const s = store.get();
      const jobs = [];
      if (fresh || !s.docs[target]) jobs.push(loadDoc(target).catch(() => null));
      if (fresh || !s.manifests[target]) jobs.push(loadManifest(target).catch(() => null));
      Promise.all(jobs).then(() => {
        const now = store.get();
        if (alive && left > 0 && (!now.docs[target] || !now.manifests[target])) {
          timer = setTimeout(() => attempt(left - 1, false), 1500);
        }
      });
    };
    attempt(5, true);
    return () => {
      alive = false;
      clearTimeout(timer);
    };
  }, [target]);
  return { target, doc, manifest, ui };
}

export function ModuleSpace({ entry, tab }) {
  const { target, doc, manifest, ui } = useModule(entry.name);
  const listed = useStore(store, (s) => docOf(s, 'sys.modules'));
  const info = listed && listed.list ? listed.list.find((m) => m.name === entry.name) : null;

  // Stopped for a reason a bridge knows, the block of the product: the module answers nothing,
  // and nothing of it is shown. The way out is in the notice above.
  const uis = useStore(store, (s) => s.uis);
  const why = info ? stoppedText(uis, info.reason) : '';
  if (why) {
    return html`<div class="space mod"><${Notice} kind="err" icon="lock">${t(why)}<//></div>`;
  }

  if (!doc || !manifest || ui === undefined) {
    return html`<div class="loading"><${Spinner} size=${28}/></div>`;
  }
  const state = info ? info.state : 'Ready';
  const own = uiTabs(ui).find((x) => x.id === tab);
  return html`<div class="space mod">
    ${info && state !== 'Ready' && html`<${Notice} kind=${state === 'Error' ? 'err' : 'warn'}>
      <b>${t('state.' + state)}</b>${info.reason ? ': ' + info.reason : ''}<//>`}
    ${ui && ui.error && html`<${Notice} kind="warn">${t('mod.uiMissing')}<//>`}
    ${own ? html`<${ModuleUiTab} key=${own.id} name=${entry.name} tab=${own}/>`
      : tab === 'schedules' ? html`<${Schedules} target=${target} doc=${doc}/>`
      : tab === 'settings' ? html`<${ModuleSettings} target=${target} manifest=${manifest}/>`
      : tab === 'info' ? html`<${ModuleInfo} entry=${entry} info=${info} target=${target}
                                doc=${doc} manifest=${manifest}/>`
      : html`<${ModuleHome} target=${target} doc=${doc} manifest=${manifest}/>`}
  </div>`;
}

// ---------------------------------------------------------------- home

function ModuleHome({ target, doc, manifest }) {
  const commands = manifest.commands || [];
  // Commands with only a channel: a button on every channel, like pulse.
  const perChannel = commands.filter((c) => !DRAWN.includes(c.name) && c.args.length === 1 &&
                                            c.args[0].name === 'channel');
  const others = commands.filter((c) => !DRAWN.includes(c.name) && !perChannel.includes(c));
  const values = Object.keys(doc).filter((k) => !SHOWN.includes(k) && !k.startsWith('_') &&
                                                typeof doc[k] !== 'object');
  const scope = target;
  return html`
    ${isObject(doc.now) && html`<${NowBlock} scope=${scope} now=${doc.now}/>`}
    ${Array.isArray(doc.days) && doc.days.length > 0 &&
      html`<${DaysBlock} scope=${scope} days=${doc.days}/>`}
    ${Array.isArray(doc.pins) && html`<${PinBank} target=${target} doc=${doc}
       perChannel=${perChannel} canFlip=${commands.some((c) => c.name === 'flip')}/>`}
    <div class="grid2">
      ${values.length > 0 && html`<${Card} title=${t('mod.values')} icon="activity">
        <${KV} rows=${values.map((k) => [fieldLabel(scope, k), showValue(doc[k])])}/>
      <//>`}
      ${others.length > 0 && html`<${Card} title=${t('mod.actions')} icon="zap">
        ${others.map((c) => html`<details class="acc" key=${c.name}>
          <summary><b>${c.name}</b><small>${commandHelp(target, c)}</small></summary>
          <${CommandForm} command=${c} scope=${scope} onRun=${(args) => runCommand(target, c, args)}/>
        </details>`)}
      <//>`}
    </div>
    ${!Array.isArray(doc.pins) && !isObject(doc.now) && values.length === 0 &&
      others.length === 0 && html`<${Empty}>${t('mod.nothing')}<//>`}`;
}

// ---------------------------------------------------------------- now and days
// Two shapes a module can give its document, for what it reads rather than what it drives:
//   now:  { icon, value, unit, label, items: [{ key, value, unit, icon }] }
//   days: [{ date, icon, hi, lo, unit, items: [{ key, value, unit, icon }] }]
// Icons are names of the kit. Nothing here knows what the values are.

function isObject(v) {
  return v !== null && typeof v === 'object' && !Array.isArray(v);
}

function fmtNumber(v, digits = 1) {
  if (typeof v !== 'number') return v === undefined || v === null ? '–' : String(v);
  return new Intl.NumberFormat(getLang(), { maximumFractionDigits: digits }).format(v);
}

function Reading({ scope, item }) {
  return html`<div class="reading">
    ${item.icon && html`<${Icon} name=${item.icon} size=${16}/>`}
    <span>${fieldLabel(scope, item.key)}</span>
    <b>${fmtNumber(item.value)}${item.unit ? ' ' + item.unit : ''}</b>
  </div>`;
}

function NowBlock({ scope, now }) {
  const items = Array.isArray(now.items) ? now.items : [];
  return html`<section class="card now">
    <div class="now-main">
      ${now.icon && html`<span class="now-ico"><${Icon} name=${now.icon} size=${72}/></span>`}
      <div class="now-v">
        <b>${fmtNumber(now.value)}<small>${now.unit || ''}</small></b>
        ${now.label && html`<span class="now-l"><${Icon} name="map-pin" size=${14}/>${now.label}</span>`}
      </div>
    </div>
    ${items.length > 0 && html`<div class="now-items">
      ${items.map((item) => html`<${Reading} key=${item.key} scope=${scope} item=${item}/>`)}
    </div>`}
  </section>`;
}

function dayLabel(date, index) {
  if (index === 0) return t('day.today');
  const d = new Date(date + 'T12:00:00');
  if (isNaN(d)) return date;
  return new Intl.DateTimeFormat(getLang(), { weekday: 'short', day: 'numeric' }).format(d);
}

function DaysBlock({ scope, days }) {
  return html`<div class="dayrow">
    ${days.map((d, i) => html`<div class="daybox" key=${d.date || i}>
      <small>${dayLabel(d.date, i)}</small>
      ${d.icon && html`<${Icon} name=${d.icon} size=${32}/>`}
      <b>${fmtNumber(d.hi, 0)}°</b>
      <span>${fmtNumber(d.lo, 0)}°</span>
      ${(d.items || []).map((item) => html`<em key=${item.key} title=${fieldLabel(scope, item.key)}>
        ${item.icon && html`<${Icon} name=${item.icon} size=${12}/>`}
        ${fmtNumber(item.value, 0)}${item.unit || ''}</em>`)}
    </div>`)}
  </div>`;
}

// A word of the firmware, such as a state, is translated when the console knows it.
function showValue(v) {
  if (v === true) return t('yes');
  if (v === false) return t('no');
  return (typeof v === 'string' && tMaybe('val.' + v)) || String(v);
}

async function runCommand(target, command, args) {
  if (command.confirm) {
    const yes = await confirmDialog({ title: command.name, text: commandHelp(target, command),
                                      danger: true });
    if (!yes) return;
  }
  await act(target, command.name, args, t('done'));
}

// Channels: a large switch each, the name, the pin, what the schedules will do.
function PinBank({ target, doc, perChannel, canFlip }) {
  useTick(15000);
  const clock = useStore(store, deviceClock);
  const [pending, setPending] = useState({});
  const [editing, setEditing] = useState(null);
  useEffect(() => setPending({}), [doc]);

  const names = (list) => list.map((p) => p.name).join(', ');
  const flip = async (pin, on) => {
    setPending(Object.assign({}, pending, { [pin.i]: on }));
    const r = await command(target, 'set', { channel: pin.i, value: on });
    if (!r.ok) setPending((p) => Object.assign({}, p, { [pin.i]: undefined }));
    // Denied: a schedule holds the channel now. Said in words, not as a code.
    if (r.status === 'denied') toast(t('mod.deniedHeld', { list: pin.name }), 'err', 6000);
    else if (!r.ok && r.status !== 'wait') toast(statusText(r.status, r.field), 'err', 5000);
  };
  // The device leaves the channels a schedule holds now, and answers ok: the console says
  // which ones were left, and sends nothing when they all are.
  const all = async (on) => {
    const held = doc.pins.filter((p) => p.locked);
    if (held.length === doc.pins.length) {
      toast(t('mod.deniedHeldAll'), 'err', 6000);
      return;
    }
    const r = await act(target, 'all', { value: on });
    if (r.ok && held.length) toast(t('mod.deniedHeld', { list: names(held) }), 'err', 6000);
  };
  const schedules = doc.schedules || [];
  // Every channel into the opposite; those a schedule holds now stay as they are, as with
  // "all". No question first: it is undone with another press. The channels left are said
  // after; when they all are, nothing is sent.
  const flipAll = async () => {
    const left = doc.pins.filter((p) => p.locked);
    if (left.length === doc.pins.length) {
      toast(t('mod.deniedHeldAll'), 'err', 6000);
      return;
    }
    const r = await act(target, 'flip', {});
    if (r.ok && left.length) toast(t('mod.deniedHeld', { list: names(left) }), 'err', 6000);
  };
  const onCount = doc.pins.filter((p) => p.level).length;

  return html`<${Card} title=${t('mod.channels')} icon="toggle-right"
      sub=${doc.output ? t('mod.onCount', { n: onCount, of: doc.pins.length }) : t('mod.inputs')}
      actions=${doc.output && html`
        ${canFlip && html`<${Btn} small icon="refresh-cw" onClick=${flipAll}>${t('mod.flip')}<//>`}
        <${Btn} small icon="power" onClick=${() => all(true)}>${t('mod.allOn')}<//>
        <${Btn} small icon="power" onClick=${() => all(false)}>${t('mod.allOff')}<//>`}>
    <div class="pins">
      ${doc.pins.map((p) => {
        const want = pending[p.i];
        const on = want === undefined ? !!p.level : want;
        const plan = Array.isArray(doc.schedules) ? channelPlan(schedules, p.i, clock) : null;
        return html`<div key=${p.i} class=${'pin' + (on ? ' on' : '')}>
          <div class="pin-h">
            <span class="pin-ico"><${Icon} name="zap" size=${20}/></span>
            <div class="pin-n"><b>${p.name}</b><small>GPIO ${p.gpio}
              ${p.inverted ? ' · ' + t('mod.inverted') : ''}</small></div>
            <${Btn} small icon="pencil" title=${t('btn.edit')} onClick=${() => setEditing(p)}/>
          </div>
          <div class="pin-s">
            ${doc.output ? html`<${Switch} big on=${on} busy=${want !== undefined}
                disabled=${!!p.locked} onChange=${(v) => flip(p, v)}/>`
              : html`<${Badge} kind=${on ? 'ok' : 'off'}>${on ? t('mod.closed') : t('mod.open')}<//>`}
            <span class="pin-st">${on ? t('mod.on') : t('mod.off')}</span>
          </div>
          ${plan && html`<div class=${'pin-p ' + plan.kind}>
            <${Icon} name=${p.locked ? 'lock' : 'calendar-clock'} size=${14}/>${plan.text}
            ${p.locked ? ' · ' + t('mod.locked') : ''}</div>`}
          ${perChannel.length > 0 && html`<div class="pin-a">
            ${perChannel.map((c) => html`<${Btn} small key=${c.name} icon="play" title=${commandHelp(target, c)}
              onClick=${() => runCommand(target, c, { channel: p.i })}>${c.name}<//>`)}
          </div>`}
        </div>`;
      })}
    </div>
    ${doc.output && html`<div class="card-foot">
      <${Switch} on=${doc.restore} label=${t('mod.restore')}
        onChange=${(v) => act(target, 'restore', { value: v }, t('saved'))}/>
    </div>`}
    ${editing && html`<${PinEditor} target=${target} pin=${editing} output=${doc.output}
      onClose=${() => setEditing(null)}/>`}
  <//>`;
}

function PinEditor({ target, pin, output, onClose }) {
  const [name, setName] = useState(pin.name);
  const [inverted, setInverted] = useState(pin.inverted);
  const [busy, setBusy] = useState(false);
  const save = async () => {
    setBusy(true);
    let ok = true;
    if (name !== pin.name) ok = (await act(target, 'name', { channel: pin.i, name })).ok;
    if (ok && output && inverted !== pin.inverted) {
      ok = (await act(target, 'invert', { channel: pin.i, value: inverted })).ok;
    }
    setBusy(false);
    if (ok) {
      toast(t('saved'), 'ok');
      onClose();
    }
  };
  return html`<${Modal} title=${t('mod.channel') + ' ' + (pin.i + 1)} icon="pencil" onClose=${onClose}
      footer=${html`<${Btn} onClick=${onClose}>${t('btn.cancel')}<//>
        <${Btn} kind="primary" icon="check" busy=${busy} disabled=${!name.trim()}
          onClick=${save}>${t('btn.save')}<//>`}>
    <${TextInput} label=${t('mod.name')} value=${name} onInput=${setName} maxLength=${24}
      autoFocus=${true}/>
    ${output && html`<div class="field sw"><${Switch} on=${inverted} onChange=${setInverted}
      label=${t('mod.invertLong')}/></div>`}
    <${KV} rows=${[[t('mod.gpio'), pin.gpio], [t('mod.index'), pin.i]]}/>
  <//>`;
}

// ---------------------------------------------------------------- schedules

function Schedules({ target, doc }) {
  useTick(30000);
  const clock = useStore(store, deviceClock);
  const [editor, setEditor] = useState(null);
  const pins = doc.pins || [];
  const list = doc.schedules || [];
  const max = doc.schedulesMax || 0;
  const now = clock ? localParts(clock.unix, clock.offset) : null;

  const remove = async (s) => {
    const yes = await confirmDialog({ title: t('sched.remove'), text: t('sched.removeText'),
                                      danger: true, okLabel: t('btn.remove') });
    if (yes) await act(target, 'sched.remove', { id: s.id }, t('removed'));
  };
  const offset = clock ? clock.offset : 0;
  // The schedules of these numbers, as the list writes them.
  const describeIds = (ids) => (ids || []).map((id) => {
    const o = list.find((x) => x.id === id);
    return o ? spanText(o, offset) + ' · ' + (o.type === 'once' ? t('sched.once') : daysText(o.days))
             : '#' + id;
  }).join('; ');
  // Two enabled schedules never share time (the device refuses it): enabling one that would
  // asks first, then enables it and disables the others.
  const run = async (name, args) => {
    let r = await command(target, name, args);
    const clash = r.result && r.result.conflicts;
    if (!r.ok && r.status === 'busy' && clash && clash.length) {
      const yes = await confirmDialog({ title: t('sched.clashTitle'),
        text: t('sched.clashText', { list: describeIds(clash) }), okLabel: t('sched.clashOk') });
      if (!yes) return { ok: false, status: 'cancelled', field: '', result: {} };
      r = await command(target, name, Object.assign({}, args, { force: true }));
    }
    if (r.ok && name === 'sched.add' && r.result.enabled === false) {
      toast(t('sched.savedOff', { list: describeIds(r.result.conflicts) }), 'warn', 7000);
    } else if (r.ok) {
      toast(t('saved'), 'ok');
    } else if (r.status !== 'cancelled') {
      toast(statusText(r.status, r.field), 'err', 5000);
    }
    return r;
  };
  const save = (initial) => (args) => initial
    ? run('sched.update', Object.assign({ id: initial.id }, args))
    : run('sched.add', args);

  return html`
    ${!doc.schedulesRunning && html`<${Notice} kind="warn">${t('sched.stopped')}<//>`}
    ${pins.map((p) => {
      const mine = list.filter((s) => s.channel === p.i);
      return html`<${Card} key=${p.i} title=${p.name} icon="calendar-clock"
          sub=${t('sched.count', { n: mine.length, max })}
          actions=${html`<${Btn} small icon="plus" disabled=${mine.length >= max}
            onClick=${() => setEditor({ channel: p.i })}>${t('sched.add')}<//>`}>
        ${mine.length === 0 ? html`<p class="muted">${t('sched.none')}</p>` : html`
          <ul class="sched">${mine.map((s) => {
            const active = clock && scheduleActive(s, clock.unix, now.minute, now.weekday);
            return html`<li key=${s.id} class=${(s.enabled ? '' : 'dis ') + (active ? 'act' : '')}>
              <span class="sched-i"><${Icon} name=${s.type === 'once' ? 'timer' : 'calendar-clock'}/></span>
              <div class="sched-t">
                <b>${spanText(s, clock ? clock.offset : 0)}</b>
                <small>${s.type === 'once' ? t('sched.once') : daysText(s.days)}
                  ${active ? html` · <${Badge} kind="ok">${t('sched.active')}<//>` : ''}</small>
                ${s.conflicts && s.conflicts.length > 0 && html`<small class="sched-clash">
                  <${Icon} name="triangle-alert" size=${13}/>
                  <span>${t('sched.clash', { list: describeIds(s.conflicts) })}</span></small>`}
              </div>
              <${Switch} on=${s.enabled}
                onChange=${(v) => run('sched.update', { id: s.id, enabled: v })}/>
              <${Btn} small icon="pencil" title=${t('btn.edit')} onClick=${() => setEditor({ initial: s })}/>
              <${Btn} small icon="trash-2" title=${t('btn.remove')} onClick=${() => remove(s)}/>
            </li>`;
          })}</ul>`}
      <//>`;
    })}
    ${editor && html`<${ScheduleEditor} channels=${pins} channel=${editor.channel}
      initial=${editor.initial} clock=${clock} onSave=${save(editor.initial)}
      onClose=${() => setEditor(null)}/>`}`;
}

// ---------------------------------------------------------------- settings

function ModuleSettings({ target, manifest }) {
  const cfg = useStore(store, (s) => s.cfgs[target]);
  useEffect(() => {
    loadCfg(target).catch(() => {});
  }, [target]);
  if (!cfg) return html`<div class="loading"><${Spinner} size=${28}/></div>`;
  const reset = async () => {
    const yes = await confirmDialog({ title: t('mod.resetTitle'), text: t('mod.resetText'),
                                      danger: true });
    if (!yes) return;
    await act(target, 'config.reset', {}, t('saved'));
    loadCfg(target).catch(() => {});
  };
  const save = async (changed) => {
    const r = await act(target, 'config.set', changed, t('saved'));
    if (r.ok) loadCfg(target).catch(() => {});
    return r;
  };
  return html`<${Card} title=${t('mod.tab.settings')} icon="settings" cls="narrow">
    <${SchemaForm} schema=${manifest.config} values=${cfg} scope=${target}
      onSave=${save} onReset=${reset}/>
  <//>`;
}

// ---------------------------------------------------------------- about

function ModuleInfo({ entry, info, target, doc, manifest }) {
  const m = manifest.module || {};
  const [busy, setBusy] = useState(false);
  const restart = async () => {
    const yes = await confirmDialog({ title: t('mod.restart'), text: t('mod.restartText') });
    if (!yes) return;
    setBusy(true);
    await act('sys.modules', 'restart', { module: entry.name }, t('done'));
    setBusy(false);
    loadDoc(target).catch(() => {});
  };
  return html`<div class="grid2">
    <${Card} title=${m.title || entry.title} icon="puzzle"
        actions=${html`<${Btn} small icon="rotate-ccw" busy=${busy} onClick=${restart}>
          ${t('mod.restart')}<//>`}>
      <${KV} rows=${[
        [t('mod.name'), entry.name],
        [t('mod.version'), m.version || entry.version],
        [t('mod.kind'), t('kind.' + (m.kind || entry.kind))],
        [t('mod.state'), info && html`<${StateBadge} state=${info.state} reason=${info.reason}/>`],
        [t('mod.reason'), info && info.reason ? info.reason : null],
        [t('mod.tick'), info ? (info.tickMs ? info.tickMs + ' ms' : t('mod.noTick')) : null],
        [t('mod.code'), m.code || null],
        [t('mod.target'), html`<code>${target}</code>`]]}/>
    <//>
    <${Card} title=${t('mod.commands')} icon="zap" sub=${t('mod.commandsSub')}>
      ${(manifest.commands || []).map((c) => html`<details class="acc" key=${c.name}>
        <summary><b>${c.name}</b><small>${commandHelp(target, c)}</small></summary>
        ${c.anyArgs ? html`<p class="muted">${t('mod.anyArgs')}</p>`
          : html`<${CommandForm} command=${c} scope=${target}
              onRun=${(args) => runCommand(target, c, args)}/>`}
      </details>`)}
    <//>
    <${Card} title=${t('mod.document')} icon="scroll-text" cls="span2">
      <${JsonView} value=${Object.fromEntries(Object.entries(doc).filter(([k]) => !k.startsWith('_')))}/>
    <//>
  </div>`;
}
