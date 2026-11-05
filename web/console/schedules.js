// LoopMaxPro console - schedules of a bank of pins: what they mean and how they are edited.
// The rules are the ones of the firmware (sdk/pin_schedule.cpp): weekly on the chosen days,
// across midnight when off comes before on, or once between two moments.
import { html, useState } from '../vendor/preact-htm.js';
import { Btn, Modal, Notice, Segmented, Select, Switch, TextInput } from '../kit/ui.js';
import { fmtMinutes, pad2, parseMinutes } from '../kit/format.js';
import { t } from '../kit/i18n.js';

// The days as they are shown, Monday first. The bit of a day: 1 << day, Sunday is 0.
export const WEEK_ORDER = [1, 2, 3, 4, 5, 6, 0];
export const EVERY_DAY = 0x7f;

// Minute of the day and weekday of a moment of the device.
export function localParts(unix, offset) {
  const d = new Date((unix + (offset || 0)) * 1000);
  return { minute: d.getUTCHours() * 60 + d.getUTCMinutes(), weekday: d.getUTCDay() };
}

export function scheduleActive(s, unix, minute, weekday) {
  if (!s.enabled) return false;
  if (s.type === 'once') return unix >= s.on && unix < s.off;
  const today = 1 << (weekday % 7);
  const yesterday = 1 << ((weekday + 6) % 7);
  if (s.on < s.off) return !!(s.days & today) && minute >= s.on && minute < s.off;
  if (s.on > s.off) {
    return (!!(s.days & today) && minute >= s.on) || (!!(s.days & yesterday) && minute < s.off);
  }
  return false;
}

// Minutes until the schedule turns on next time. null when it never will.
function minutesToStart(s, unix, minute, weekday) {
  if (!s.enabled) return null;
  if (s.type === 'once') return s.on > unix ? Math.ceil((s.on - unix) / 60) : null;
  for (let d = 0; d <= 7; d++) {
    const day = (weekday + d) % 7;
    if (!(s.days & (1 << day))) continue;
    if (d === 0 && s.on <= minute) continue;
    return d * 1440 + s.on - minute;
  }
  return null;
}

function dayName(day) {
  return t('day.' + day);
}

function onceText(unix, offset) {
  const d = new Date((unix + (offset || 0)) * 1000);
  return pad2(d.getUTCDate()) + '/' + pad2(d.getUTCMonth() + 1) + ' ' +
    pad2(d.getUTCHours()) + ':' + pad2(d.getUTCMinutes());
}

// "Mon Tue Wed", "Every day", "Weekdays".
export function daysText(days) {
  if ((days & EVERY_DAY) === EVERY_DAY) return t('sched.everyDay');
  if (days === 0x3e) return t('sched.weekdays');
  if (days === 0x41) return t('sched.weekend');
  return WEEK_ORDER.filter((d) => days & (1 << d)).map(dayName).join(' ');
}

export function spanText(s, offset) {
  if (s.type === 'once') return onceText(s.on, offset) + ' → ' + onceText(s.off, offset);
  return fmtMinutes(s.on) + ' → ' + fmtMinutes(s.off) + (s.off < s.on ? ' ' + t('sched.nextDay') : '');
}

// What the schedules of a channel are doing: on until, or next start. Empty without any.
export function channelPlan(schedules, channel, clock) {
  const mine = schedules.filter((s) => s.channel === channel && s.enabled);
  if (!mine.length || !clock) return mine.length ? { text: t('sched.noClock'), kind: 'warn' } : null;
  const { minute, weekday } = localParts(clock.unix, clock.offset);
  const active = mine.find((s) => scheduleActive(s, clock.unix, minute, weekday));
  if (active) {
    const end = active.type === 'once' ? onceText(active.off, clock.offset) : fmtMinutes(active.off);
    return { text: t('sched.onUntil', { time: end }), kind: 'ok' };
  }
  let best = null;
  mine.forEach((s) => {
    const m = minutesToStart(s, clock.unix, minute, weekday);
    if (m !== null && (best === null || m < best.m)) best = { m, s };
  });
  if (!best) return null;
  const at = clock.unix + best.m * 60;
  const parts = localParts(at, clock.offset);
  const when = best.m < 1440 - minute ? fmtMinutes(parts.minute)
    : dayName(parts.weekday) + ' ' + fmtMinutes(parts.minute);
  return { text: t('sched.nextOn', { time: when }), kind: 'info' };
}

// ---------------------------------------------------------------- editor

function toLocalInput(unix, offset) {
  const d = new Date((unix + offset) * 1000);
  return d.getUTCFullYear() + '-' + pad2(d.getUTCMonth() + 1) + '-' + pad2(d.getUTCDate()) +
    'T' + pad2(d.getUTCHours()) + ':' + pad2(d.getUTCMinutes());
}

function fromLocalInput(text, offset) {
  const m = /^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2})/.exec(text || '');
  if (!m) return null;
  return Date.UTC(+m[1], +m[2] - 1, +m[3], +m[4], +m[5]) / 1000 - offset;
}

// A new schedule, or one to change. onSave(args) runs the command and returns its outcome.
export function ScheduleEditor({ channels, initial, channel = 0, clock, onSave, onClose }) {
  // Without the time of the device the moments are the ones of this browser.
  const offset = clock ? clock.offset : -new Date().getTimezoneOffset() * 60;
  const now = clock ? clock.unix : Math.floor(Date.now() / 1000);
  const editing = !!initial;
  const [type, setType] = useState(initial ? initial.type : 'weekly');
  const [ch, setCh] = useState(String(initial ? initial.channel : channel));
  const [days, setDays] = useState(initial && initial.type === 'weekly' ? initial.days : EVERY_DAY);
  const [onW, setOnW] = useState(initial && initial.type === 'weekly' ? fmtMinutes(initial.on) : '08:00');
  const [offW, setOffW] = useState(initial && initial.type === 'weekly' ? fmtMinutes(initial.off) : '18:00');
  const start = now - (now % 60) + 3600;
  const [onO, setOnO] = useState(toLocalInput(initial && initial.type === 'once' ? initial.on : start, offset));
  const [offO, setOffO] = useState(toLocalInput(initial && initial.type === 'once' ? initial.off : start + 3600, offset));
  const [enabled, setEnabled] = useState(initial ? initial.enabled : true);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState('');

  let on = null;
  let off = null;
  if (type === 'weekly') {
    on = parseMinutes(onW);
    off = parseMinutes(offW);
  } else {
    on = fromLocalInput(onO, offset);
    off = fromLocalInput(offO, offset);
  }
  let problem = '';
  if (on === null || off === null) problem = t('sched.badTime');
  else if (type === 'weekly' && on === off) problem = t('sched.sameTime');
  else if (type === 'weekly' && !(days & EVERY_DAY)) problem = t('sched.noDays');
  else if (type === 'once' && off <= on) problem = t('sched.endBefore');

  const save = async () => {
    if (problem) return;
    setBusy(true);
    const args = { on, off, enabled };
    if (type === 'weekly') args.days = days;
    if (!editing) {
      args.channel = +ch;
      args.type = type;
    }
    const r = await onSave(args);
    setBusy(false);
    if (r.ok) onClose();
    else setError(t('status.' + r.status) + (r.field ? ': ' + r.field : ''));
  };

  const toggleDay = (d) => setDays(days ^ (1 << d));

  return html`<${Modal} title=${editing ? t('sched.edit') : t('sched.new')} icon="calendar-clock"
      onClose=${onClose}
      footer=${html`<${Btn} onClick=${onClose}>${t('btn.cancel')}<//>
        <${Btn} kind="primary" icon="check" busy=${busy} disabled=${!!problem}
          onClick=${save}>${t('btn.save')}<//>`}>
    ${channels.length > 1 && html`<${Select} label=${t('sched.channel')} value=${ch}
      disabled=${editing} onChange=${setCh}
      options=${channels.map((c) => ({ id: String(c.i), label: c.name }))}/>`}
    <div class="field"><label>${t('sched.type')}</label>
      <${Segmented} value=${type} onChange=${setType} disabled=${editing} options=${[
        { id: 'weekly', label: t('sched.weekly'), icon: 'calendar-clock' },
        { id: 'once', label: t('sched.once'), icon: 'timer' }]}/>
    </div>
    ${type === 'weekly' ? html`
      <div class="field"><label>${t('sched.days')}</label>
        <div class="days">${WEEK_ORDER.map((d) => html`<button type="button" key=${d}
            class=${days & (1 << d) ? 'on' : ''} onClick=${() => toggleDay(d)}>${dayName(d)}</button>`)}
        </div>
        <div class="days-q">
          <button type="button" onClick=${() => setDays(EVERY_DAY)}>${t('sched.everyDay')}</button>
          <button type="button" onClick=${() => setDays(0x3e)}>${t('sched.weekdays')}</button>
          <button type="button" onClick=${() => setDays(0x41)}>${t('sched.weekend')}</button>
        </div>
      </div>
      <div class="row2">
        <${TextInput} label=${t('sched.on')} type="time" value=${onW} onInput=${setOnW}/>
        <${TextInput} label=${t('sched.off')} type="time" value=${offW} onInput=${setOffW}/>
      </div>
      ${on !== null && off !== null && off < on &&
        html`<${Notice}>${t('sched.acrossMidnight')}<//>`}` : html`
      <div class="row2">
        <${TextInput} label=${t('sched.from')} type="datetime-local" value=${onO} onInput=${setOnO}/>
        <${TextInput} label=${t('sched.to')} type="datetime-local" value=${offO} onInput=${setOffO}/>
      </div>`}
    <div class="field sw"><${Switch} on=${enabled} onChange=${setEnabled} label=${t('sched.enabled')}/></div>
    ${!clock && html`<${Notice} kind="warn">${t('sched.noClockEdit')}<//>`}
    ${(problem || error) && html`<${Notice} kind="err">${error || problem}<//>`}
  <//>`;
}
