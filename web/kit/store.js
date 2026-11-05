// LoopMaxPro console - observable state.
// One object for the whole console; a view reads the part it needs and is drawn again when
// that part changes.
//
//   const store = createStore({ count: 0 });
//   store.set({ count: 1 });                       // or store.set(s => ({ count: s.count + 1 }))
//   const count = useStore(store, s => s.count);   // inside a component
import { useEffect, useRef, useState } from '../vendor/preact-htm.js';

export function createStore(initial) {
  let value = initial;
  const listeners = new Set();
  return {
    get: () => value,
    set(patch) {
      const next = typeof patch === 'function' ? patch(value) : patch;
      if (!next) return;
      value = Object.assign({}, value, next);
      listeners.forEach((fn) => fn(value));
    },
    subscribe(fn) {
      listeners.add(fn);
      return () => listeners.delete(fn);
    },
  };
}

// The part of the state chosen by pick. The component is drawn again only when it changes:
// compared by identity, so pick must return what is in the state, not a new object.
// pick may change between two draws (the tabs of another module): the last one is always used.
export function useStore(store, pick) {
  const pickNow = useRef(pick);
  pickNow.current = pick;
  const part = pick(store.get());
  const last = useRef(part);
  last.current = part;
  const [, redraw] = useState(0);
  useEffect(() => {
    const check = (s) => {
      const next = pickNow.current(s);
      if (next !== last.current) {
        last.current = next;
        redraw((n) => n + 1);
      }
    };
    const off = store.subscribe(check);
    // What changed between the draw and now: a transport that answers at once, as one with
    // the answers already in hand, changes the state before the component listens.
    check(store.get());
    return off;
  }, [store]);
  return part;
}

// Browser memory, for the conveniences of one viewer: theme, last tab. It can be missing.
export const prefs = {
  get(key, fallback) {
    try {
      const v = localStorage.getItem('lmp.' + key);
      return v === null ? fallback : JSON.parse(v);
    } catch (e) {
      return fallback;
    }
  },
  set(key, value) {
    try {
      localStorage.setItem('lmp.' + key, JSON.stringify(value));
    } catch (e) {
      // private window or blocked storage: the preference lives until the page closes
    }
  },
};
