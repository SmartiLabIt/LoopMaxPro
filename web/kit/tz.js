// LoopMaxPro console - the rules of a time zone, as the C library of the device wants them.
// The browser knows every zone by name (Europe/Rome) but gives no rules: they are found by
// looking at the offsets of the zone along the year, with Intl. No table: every zone works.
//   posixOf('Europe/Rome')  ->  'CET-1CEST,M3.5.0,M10.5.0/3'

const HOUR = 3600000;

// The zones of the list, with their usual names. Any other zone gets its rules from posixOf.
export const ZONES = [
  ['Europe/Rome', 'CET-1CEST,M3.5.0,M10.5.0/3'],
  ['Europe/Paris', 'CET-1CEST,M3.5.0,M10.5.0/3'],
  ['Europe/Berlin', 'CET-1CEST,M3.5.0,M10.5.0/3'],
  ['Europe/Madrid', 'CET-1CEST,M3.5.0,M10.5.0/3'],
  ['Europe/London', 'GMT0BST,M3.5.0/1,M10.5.0'],
  ['Europe/Lisbon', 'WET0WEST,M3.5.0/1,M10.5.0'],
  ['Europe/Athens', 'EET-2EEST,M3.5.0/3,M10.5.0/4'],
  ['Europe/Moscow', 'MSK-3'],
  ['UTC', 'UTC0'],
  ['America/New_York', 'EST5EDT,M3.2.0,M11.1.0'],
  ['America/Chicago', 'CST6CDT,M3.2.0,M11.1.0'],
  ['America/Denver', 'MST7MDT,M3.2.0,M11.1.0'],
  ['America/Los_Angeles', 'PST8PDT,M3.2.0,M11.1.0'],
  ['America/Sao_Paulo', '<-03>3'],
  ['Asia/Dubai', '<+04>-4'],
  ['Asia/Kolkata', 'IST-5:30'],
  ['Asia/Shanghai', 'CST-8'],
  ['Asia/Tokyo', 'JST-9'],
  ['Australia/Sydney', 'AEST-10AEDT,M10.1.0,M4.1.0/3'],
];

// The rules of a zone: the ones of the list, or found from the browser. Null when unknown.
export function zoneRules(zone) {
  const known = ZONES.find((z) => z[0] === zone);
  return known ? known[1] : posixOf(zone);
}

// Minutes the zone is ahead of UTC at a moment.
function offsetAt(zone, ms) {
  const parts = new Intl.DateTimeFormat('en-US', {
    timeZone: zone, hourCycle: 'h23', year: 'numeric', month: 'numeric', day: 'numeric',
    hour: 'numeric', minute: 'numeric', second: 'numeric',
  }).formatToParts(new Date(ms));
  const v = {};
  parts.forEach((p) => { v[p.type] = Number(p.value); });
  const wall = Date.UTC(v.year, v.month - 1, v.day, v.hour, v.minute, v.second);
  return Math.round((wall - Math.floor(ms / 1000) * 1000) / 60000);
}

// A name for the part of the year: the letters the browser uses, or <+09> as POSIX allows.
function nameOf(zone, ms, offset) {
  const part = new Intl.DateTimeFormat('en-US', { timeZone: zone, timeZoneName: 'short' })
    .formatToParts(new Date(ms)).find((p) => p.type === 'timeZoneName');
  if (part && /^[A-Z]{3,5}$/.test(part.value)) return part.value;
  const sign = offset < 0 ? '-' : '+';
  const a = Math.abs(offset);
  const mm = a % 60 ? String(a % 60).padStart(2, '0') : '';
  return '<' + sign + String(Math.floor(a / 60)).padStart(2, '0') + mm + '>';
}

// POSIX writes the offset the other way: 1 hour ahead of UTC is -1.
function posixOffset(minutes) {
  const west = -minutes;
  const a = Math.abs(west);
  const text = String(Math.floor(a / 60)) + (a % 60 ? ':' + String(a % 60).padStart(2, '0') : '');
  return (west < 0 ? '-' : '') + text;
}

// The moment the offset changes, between two moments with different offsets.
function change(zone, from, to) {
  const before = offsetAt(zone, from);
  while (to - from > 60000) {
    const mid = from + Math.floor((to - from) / 2);
    if (offsetAt(zone, mid) === before) from = mid; else to = mid;
  }
  return to;
}

// Mm.w.d/time of a change: month, week of the month (5 = the last), weekday, and the local
// time before the change.
function rule(zone, at, offsetBefore) {
  const local = new Date(at + offsetBefore * 60000);
  const month = local.getUTCMonth() + 1;
  const day = local.getUTCDate();
  const last = new Date(Date.UTC(local.getUTCFullYear(), month, 0)).getUTCDate();
  const week = day + 7 > last ? 5 : Math.ceil(day / 7);
  const minutes = local.getUTCHours() * 60 + local.getUTCMinutes();
  let text = 'M' + month + '.' + week + '.' + local.getUTCDay();
  if (minutes !== 120) {
    text += '/' + Math.floor(minutes / 60) + (minutes % 60 ? ':' + String(minutes % 60).padStart(2, '0') : '');
  }
  return text;
}

// The rules of a zone for this year, or null when the browser does not know the zone.
export function posixOf(zone, year = new Date().getUTCFullYear()) {
  try {
    const start = Date.UTC(year, 0, 1);
    // One look a week along the year: every change of offset is found.
    const changes = [];
    let prevMs = start;
    let prev = offsetAt(zone, start);
    for (let ms = start + 7 * 24 * HOUR; ms <= Date.UTC(year + 1, 0, 1); ms += 7 * 24 * HOUR) {
      const now = offsetAt(zone, ms);
      if (now !== prev) {
        changes.push({ at: change(zone, prevMs, ms), from: prev, to: now });
        prev = now;
      }
      prevMs = ms;
    }
    const jan = offsetAt(zone, start);
    if (changes.length !== 2) {
      // No summer time, or rules POSIX cannot say: the offset of now, all year.
      const now = offsetAt(zone, Date.now());
      return nameOf(zone, Date.now(), now) + posixOffset(now);
    }
    // Standard time is the lower offset; summer time begins when the offset goes up.
    const on = changes.find((c) => c.to > c.from);
    const off = changes.find((c) => c.to < c.from);
    if (!on || !off) return nameOf(zone, start, jan) + posixOffset(jan);
    const std = off.to;
    const dst = on.to;
    return nameOf(zone, off.at + HOUR, std) + posixOffset(std) +
      nameOf(zone, on.at + HOUR, dst) + (dst - std === 60 ? '' : posixOffset(dst)) +
      ',' + rule(zone, on.at, on.from) + ',' + rule(zone, off.at, off.from);
  } catch (e) {
    return null;
  }
}

// The zone of this browser, by name. Empty when it does not say.
export function browserZone() {
  try {
    return Intl.DateTimeFormat().resolvedOptions().timeZone || '';
  } catch (e) {
    return '';
  }
}
