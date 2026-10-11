// LoopMaxPro - the interface of the demo module: one tab of its own, next to the standard ones.
// It is the example of docs/web-console.md. It imports nothing: Preact, the pieces of the
// console, the document and the commands come from ctx, on the device and on the server.

function Counters({ ctx }) {
  const { html, ui, t } = ctx;
  const doc = ctx.useDoc();
  if (!doc) return html`<${ui.Spinner} size=${28}/>`;
  const pins = doc.pins || [];
  return html`<div class="grid2">
    <${ui.Card} title=${t('mod.demo.counters')} icon="activity">
      <div class="stats">
        <${ui.Stat} icon="power" label=${t('mod.demo.presses')} value=${doc.presses}/>
        <${ui.Stat} icon="timer" label=${t('mod.demo.pulses')} value=${doc.pulses}/>
      </div>
    <//>
    <${ui.Card} title=${t('mod.demo.pulse')} icon="zap" sub=${t('mod.demo.pulseSub')}>
      ${pins.map((p) => html`<${ui.Btn} key=${p.i} icon="play"
          onClick=${() => ctx.act('pulse', { channel: p.i }, t('done'))}>${p.name}<//>`)}
    <//>
  </div>`;
}

export default {
  uiApi: 1,
  texts: {
    it: { 'mod.demo.counters': 'Contatori', 'mod.demo.presses': 'Pressioni del pulsante',
          'mod.demo.pulses': 'Impulsi', 'mod.demo.pulse': 'Impulso',
          'mod.demo.pulseSub': 'Accende il canale per il tempo delle impostazioni' },
    en: { 'mod.demo.counters': 'Counters', 'mod.demo.presses': 'Button presses',
          'mod.demo.pulses': 'Pulses', 'mod.demo.pulse': 'Pulse',
          'mod.demo.pulseSub': 'Turns the channel on for the time of the settings' },
  },
  tabs: [
    { id: 'counters', icon: 'activity', label: 'mod.demo.counters', view: Counters },
  ],
};
