// LoopMaxPro console - numbers, sizes and times as people read them.
import { t } from './i18n.js';

export function fmtBytes(n) {
  if (n === undefined || n === null || isNaN(n)) return '–';
  if (n < 1024) return n + ' B';
  if (n < 1024 * 1024) return (n / 1024).toFixed(n < 10240 ? 1 : 0) + ' KB';
  return (n / 1048576).toFixed(2) + ' MB';
}

// Seconds of activity: "3 d 4 h", "12 min 5 s".
export function fmtDuration(sec) {
  if (sec === undefined || sec === null || isNaN(sec)) return '–';
  sec = Math.max(0, Math.floor(sec));
  const d = Math.floor(sec / 86400);
  const h = Math.floor((sec % 86400) / 3600);
  const m = Math.floor((sec % 3600) / 60);
  const s = sec % 60;
  if (d) return `${d} ${t('u.d')} ${h} ${t('u.h')}`;
  if (h) return `${h} ${t('u.h')} ${m} min`;
  if (m) return `${m} min ${s} s`;
  return `${s} s`;
}

export function pad2(n) {
  return String(n).padStart(2, '0');
}

// Minutes of the day, 0..1439, as "08:30".
export function fmtMinutes(min) {
  return pad2(Math.floor(min / 60)) + ':' + pad2(min % 60);
}

export function parseMinutes(text) {
  const m = /^(\d{1,2}):(\d{2})$/.exec(String(text).trim());
  if (!m) return null;
  const h = +m[1];
  const mm = +m[2];
  return h < 24 && mm < 60 ? h * 60 + mm : null;
}

// A moment of the device: unix seconds plus the offset of its time zone, shown as it is there.
export function fmtDeviceTime(unix, offset, withSeconds = true) {
  if (!unix) return '–';
  const d = new Date((unix + (offset || 0)) * 1000);
  const time = pad2(d.getUTCHours()) + ':' + pad2(d.getUTCMinutes()) +
    (withSeconds ? ':' + pad2(d.getUTCSeconds()) : '');
  return time;
}

export function fmtDeviceDate(unix, offset) {
  if (!unix) return '–';
  const d = new Date((unix + (offset || 0)) * 1000);
  return pad2(d.getUTCDate()) + '/' + pad2(d.getUTCMonth() + 1) + '/' + d.getUTCFullYear();
}

// Milliseconds from the start, for the log: "00:12.345", "1:02:03.004".
export function fmtUptimeMs(ms) {
  const total = Math.floor(ms / 1000);
  const h = Math.floor(total / 3600);
  const m = Math.floor((total % 3600) / 60);
  const s = total % 60;
  const milli = String(ms % 1000).padStart(3, '0');
  return (h ? h + ':' + pad2(m) : pad2(m)) + ':' + pad2(s) + '.' + milli;
}

// Signal of a radio, as bars from 0 to 4.
export function rssiBars(rssi) {
  if (rssi === undefined || rssi === null || rssi === 0) return 0;
  if (rssi >= -55) return 4;
  if (rssi >= -67) return 3;
  if (rssi >= -78) return 2;
  if (rssi >= -88) return 1;
  return 0;
}

export function percent(part, whole) {
  if (!whole) return 0;
  return Math.max(0, Math.min(100, Math.round((part * 100) / whole)));
}
