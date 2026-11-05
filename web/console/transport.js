// LoopMaxPro console - the way to the device.
// This is the only file that knows addresses and HTTP. The console on the server gets the
// same methods from another transport, which passes through the server and the bridge.
//
// Every answer is JSON. An error is thrown as ApiError, with the HTTP status, the code of
// the device ("invalid", "auth", ...) and the field that was refused.

export class ApiError extends Error {
  constructor(status, body) {
    super((body && (body.msg || body.error || body.status)) || 'HTTP ' + status);
    this.status = status;
    this.code = (body && (body.error || body.status)) || 'http';
    this.field = (body && body.field) || '';
    this.body = body || null;
  }
}

// The interfaces of the modules in the console of the device: module -> file. Written by
// tools/web/build.py; empty in the sources.
export const MODULE_UI = {};

const TOKEN_KEY = 'lmp.token';

function readToken() {
  try {
    return sessionStorage.getItem(TOKEN_KEY) || '';
  } catch (e) {
    return '';
  }
}

function writeToken(token) {
  try {
    if (token) sessionStorage.setItem(TOKEN_KEY, token);
    else sessionStorage.removeItem(TOKEN_KEY);
  } catch (e) {
    // without storage the session lasts as long as the page
  }
}

export class LocalTransport {
  // base: where the device answers. Empty: the page comes from the device itself.
  constructor(base = '') {
    this.kind = 'local';
    this.base = base.replace(/\/$/, '');
    this.token = readToken();
    this.caps = { live: true, setup: true, upload: true, logs: true };
    this.onAuthLost = null;
    this.source = null;
    // How many requests leave together: the device says it in its state ("lanes") when it has
    // no memory for the dozen a page asks at once as it opens. 0: all of them.
    this.lanes = 0;
    this.flying = 0;
    this.waiting = [];
  }

  // Through the lanes of the device: fn leaves when one is free, and gives it to the next.
  async lane(fn) {
    if (this.lanes && this.flying >= this.lanes) await new Promise((go) => this.waiting.push(go));
    else this.flying++;
    try {
      return await fn();
    } finally {
      const go = this.waiting.shift();
      if (go) go();
      else this.flying--;
    }
  }

  headers(extra) {
    const h = Object.assign({}, extra);
    if (this.token) h.Authorization = 'Bearer ' + this.token;
    return h;
  }

  request(method, path, body, contentType, timeoutMs) {
    return this.lane(() => this.send(method, path, body, contentType, timeoutMs));
  }

  // The time a request has starts when it leaves, not while it waits for a lane.
  async send(method, path, body, contentType, timeoutMs = 15000) {
    const ctl = typeof AbortController !== 'undefined' ? new AbortController() : null;
    const timer = ctl ? setTimeout(() => ctl.abort(), timeoutMs) : 0;
    let res;
    try {
      res = await fetch(this.base + path, {
        method,
        headers: this.headers(contentType ? { 'Content-Type': contentType } : {}),
        body,
        cache: 'no-store',
        signal: ctl ? ctl.signal : undefined,
      });
    } catch (e) {
      throw new ApiError(0, { error: 'offline', msg: 'offline' });
    } finally {
      if (timer) clearTimeout(timer);
    }
    let data = null;
    const text = await res.text();
    if (text) {
      try {
        data = JSON.parse(text);
      } catch (e) {
        data = { error: 'format', msg: text.slice(0, 120) };
      }
    }
    if (res.status === 401 && data && data.error === 'auth') {
      this.setToken('');
      if (this.onAuthLost) this.onAuthLost();
    }
    if (!res.ok) throw new ApiError(res.status, data);
    return data;
  }

  get(path, timeoutMs) {
    return this.request('GET', '/api/' + path, undefined, undefined, timeoutMs);
  }

  // Runs a command. The answer is the one of every interface:
  // {"result":{..},"ok":true,"status":"ok"} with "ref" when it ends later.
  command(target, name, args) {
    const body = args && Object.keys(args).length ? JSON.stringify(args) : '';
    return this.request('POST', `/api/cmd/${target}/${name}`, body, 'application/json', 30000)
      .catch((e) => {
        // A refused command still has the answer of the device: the caller reads it.
        if (e.body && e.body.status) return e.body;
        throw e;
      });
  }

  setToken(token) {
    this.token = token || '';
    writeToken(this.token);
  }

  async login(password) {
    const data = await this.request('POST', '/api/auth/login', JSON.stringify({ password }),
                                    'application/json');
    this.setToken(data.token);
    return data;
  }

  async logout() {
    try {
      await this.request('POST', '/api/auth/logout', '', 'application/json');
    } catch (e) {
      // the session may already be gone
    }
    this.setToken('');
  }

  // A file sent whole as the body of the request, with its progress.
  upload(path, file, onProgress) {
    return new Promise((resolve, reject) => {
      const xhr = new XMLHttpRequest();
      xhr.open('POST', this.base + path);
      xhr.setRequestHeader('Content-Type', 'application/octet-stream');
      if (this.token) xhr.setRequestHeader('Authorization', 'Bearer ' + this.token);
      xhr.upload.onprogress = (e) => {
        if (e.lengthComputable && onProgress) onProgress(e.loaded, e.total);
      };
      xhr.onload = () => {
        let data = null;
        try {
          data = JSON.parse(xhr.responseText);
        } catch (e) {
          data = null;
        }
        if (xhr.status >= 200 && xhr.status < 300) resolve(data);
        else reject(new ApiError(xhr.status, data));
      };
      xhr.onerror = () => reject(new ApiError(0, { error: 'offline' }));
      xhr.send(file);
    });
  }

  // The live stream of the device. handlers: { event, log, open, error }.
  listen(handlers) {
    this.close();
    if (typeof EventSource === 'undefined') return;
    const url = this.base + '/api/events' + (this.token ? '?token=' + this.token : '');
    const es = new EventSource(url);
    this.source = es;
    es.addEventListener('event', (e) => handlers.event && handlers.event(JSON.parse(e.data)));
    es.addEventListener('log', (e) => handlers.log && handlers.log(JSON.parse(e.data)));
    es.onopen = () => handlers.open && handlers.open();
    es.onerror = () => handlers.error && handlers.error();
  }

  close() {
    if (this.source) this.source.close();
    this.source = null;
  }

  // Where the interface of a module is, when the console of the device has one. Null: the
  // standard view.
  moduleUi(name) {
    const file = MODULE_UI[name];
    return file ? (this.base ? this.base + '/' : '') + file : null;
  }
}
