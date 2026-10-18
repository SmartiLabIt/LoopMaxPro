// LoopMaxPro - the interface of ExpNet, the free edition of Networks Explorer: the bench
// of a network, with the look of a monitor of its own, dark, made of metrics and graphs, whatever
// the theme of the console.
//   ExpNet:     the metrics of the network, who is in it, what happened; a device opens its
//               card with watch, wake, a name of yours
//   Wi-Fi:      the networks around and how crowded each channel is
//   Tools, Speed, MQTT, Endpoints: there with their parameters, to explore; starting them
//               is of the full version, and a window says where it is
//
// The device does the work of the network; here everything is drawn, by hand: a library of
// charts weighs more than the whole file. It imports nothing: Preact and the pieces of the
// console come from ctx (docs/web-console.md). The pieces of the console take the colours of
// ExpNet inside it: the variables of the console are given again on .dbx.

// The free edition shows what is of ExpNetPro and says where it is; the full one does it.
const FREE = true;

const STYLE = `
.dbx { --bg:#070b13; --panel:#0d1523; --panel2:#0a111d; --line:#1b2a40; --text:#dbe6f5; --muted:#7489a8;
  --accent:#22d3ee; --accent2:#a78bfa; --accent-ink:#03131a; --ok:#34d399; --warn:#fbbf24; --err:#f87171;
  --glow:0 0 0 1px rgba(34,211,238,.4),0 0 18px rgba(34,211,238,.3);
  color:var(--text); border-radius:16px; padding:12px; display:grid; gap:12px; min-width:0;
  background:radial-gradient(900px 320px at 0% -10%,rgba(34,211,238,.10),transparent 60%),
    radial-gradient(700px 320px at 100% 0%,rgba(167,139,250,.10),transparent 60%),#070b13; }
.dbx * { box-sizing:border-box; }
.dbx-p { min-width:0; background:linear-gradient(180deg,rgba(255,255,255,.03),rgba(255,255,255,0) 40%),#0d1523;
  border:1px solid #1b2a40; border-radius:12px; padding:12px 14px; box-shadow:0 10px 28px rgba(0,0,0,.35),inset 0 1px 0 rgba(255,255,255,.04); }
.dbx-ph { display:flex; align-items:center; gap:8px; margin:-2px 0 10px; min-height:28px; flex-wrap:wrap; }
.dbx-pt { font-size:.7rem; font-weight:700; letter-spacing:.14em; text-transform:uppercase; color:#93a8c6; }
.dbx-pt i { display:inline-block; width:7px; height:7px; border-radius:50%; background:var(--tone,var(--accent));
  box-shadow:0 0 10px var(--tone,var(--accent)); margin-right:9px; vertical-align:1px; }
.dbx-pr { margin-left:auto; display:flex; gap:6px; align-items:center; color:var(--muted); font-size:.76rem; font-family:var(--mono); flex-wrap:wrap; }
.dbx-two { display:grid; gap:12px; grid-template-columns:repeat(auto-fit,minmax(min(100%,420px),1fr)); }
.dbx-tiles { display:grid; gap:10px; grid-template-columns:repeat(auto-fit,minmax(150px,1fr)); }
.dbx-tile { position:relative; overflow:hidden; background:#0d1523; border:1px solid #1b2a40; border-radius:12px;
  padding:10px 12px 34px; min-height:96px; }
.dbx-tile::before { content:""; position:absolute; left:0; top:0; right:0; height:2px; background:var(--tone,var(--accent));
  box-shadow:0 0 14px var(--tone,var(--accent)); }
.dbx-tl { font-size:.64rem; font-weight:700; letter-spacing:.14em; text-transform:uppercase; color:#93a8c6; }
.dbx-tv { font-family:var(--mono); font-size:1.65rem; font-weight:700; line-height:1.15; margin-top:4px; color:#eef5ff;
  white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }
.dbx-tv small { font-size:.75rem; color:var(--muted); font-weight:500; margin-left:4px; }
.dbx-ts { font-size:.72rem; color:var(--muted); font-family:var(--mono); white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }
.dbx-tile svg { position:absolute; left:0; right:0; bottom:0; width:100%; height:32px; }
.dbx-strip { display:flex; flex-wrap:wrap; gap:6px 18px; font-family:var(--mono); font-size:.8rem; }
.dbx-strip b { color:#93a8c6; font-weight:500; margin-right:6px; text-transform:uppercase; font-size:.68rem; letter-spacing:.1em; }
.dbx-pill { display:inline-flex; align-items:center; gap:6px; padding:2px 9px; border-radius:999px; font-size:.72rem;
  font-family:var(--mono); border:1px solid #1b2a40; background:#0a111d; color:var(--muted); white-space:nowrap; }
.dbx-led { display:inline-block; width:8px; height:8px; border-radius:50%; background:#3a4a63; flex:none; }
.dbx-led.ok { background:var(--ok); box-shadow:0 0 8px var(--ok); } .dbx-led.bad { background:var(--err); box-shadow:0 0 8px var(--err); }
.dbx-led.mid { background:var(--warn); box-shadow:0 0 8px var(--warn); } .dbx-led.in { background:var(--accent); box-shadow:0 0 8px var(--accent); }
.dbx-led.blink { animation:dbx-b 1.2s ease-in-out infinite; }
@keyframes dbx-b { 50% { opacity:.35; } }
@media (prefers-reduced-motion:reduce) { .dbx-led.blink { animation:none; } }
.dbx-term { font-family:var(--mono); font-size:.8rem; line-height:1.5; background:#050910; color:#cfe9d8;
  border:1px solid #16233a; border-radius:10px; padding:10px 12px; overflow-x:auto; }
.dbx .k { color:#6f84a3; } .dbx .a { color:#22d3ee; } .dbx .g { color:#34d399; } .dbx .y { color:#fbbf24; }
.dbx .r { color:#f87171; } .dbx .v { color:#b4a6ff; }
.dbx-tbl { display:grid; }
.dbx-tr { display:grid; grid-template-columns:18px 116px minmax(84px,.8fr) minmax(76px,.9fr) minmax(110px,1.4fr) minmax(60px,.7fr) 56px 28px;
  grid-template-areas:"gl ip ty mk nm sv ms go"; gap:0 10px; align-items:baseline; padding:5px 8px; border-radius:7px;
  cursor:pointer; outline:none; }
.dbx-tr > .c1 { grid-area:gl; display:flex; align-self:center; } .dbx-tr > .c6 { grid-area:ty; } .dbx-tr > .c2 { grid-area:ip; } .dbx-tr > .c3 { grid-area:mk; } .dbx-tr > .c4 { grid-area:nm; }
.dbx-tr > .c5 { grid-area:sv; } .dbx-tr > .ms { grid-area:ms; } .dbx-tr > .go { grid-area:go; align-self:center; display:flex; }
.dbx-tr .sv, .dbx-tr .u, .dbx-tr .tyn { display:none; }
.dbx-chip svg { vertical-align:-3px; margin-right:6px; } .dbx-chip b { margin-left:7px; }
.dbx-tr:hover, .dbx-tr:focus-visible { background:rgba(34,211,238,.07); box-shadow:inset 2px 0 0 var(--accent); }
.dbx-tr.th { cursor:default; color:#6f84a3; font-size:.66rem; text-transform:uppercase; letter-spacing:.12em; }
.dbx-tr.th:hover { background:none; box-shadow:none; }
.dbx-tr.off { opacity:.42; } .dbx-tr .ms { text-align:right; } .dbx-tr small { color:#6f84a3; }
.dbx-cell { overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }
.dbx-go { display:inline-flex; align-items:center; justify-content:center; width:28px; height:20px; border-radius:6px;
  border:1px solid #1f3150; background:#081019; color:var(--accent); text-decoration:none; }
.dbx-go:hover, .dbx-go:focus-visible { border-color:var(--accent); box-shadow:0 0 0 3px rgba(34,211,238,.15); outline:none; }
.dbx-cp { display:inline-flex; vertical-align:-2px; margin-left:6px; padding:2px; border:0; border-radius:5px;
  background:none; color:#5f7393; cursor:pointer; }
.dbx-cp:hover, .dbx-cp:focus-visible { color:var(--accent); background:rgba(34,211,238,.1); outline:none; }
.dbx-log { display:grid; gap:1px; max-height:280px; overflow-y:auto; }
.dbx-log div { white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }
.dbx-bar { display:flex; gap:8px; flex-wrap:wrap; align-items:center; }
.dbx-in { flex:1 1 180px; min-width:0; font-family:var(--mono); font-size:.88rem; padding:9px 11px; height:38px;
  border-radius:10px; border:1px solid #1f3150; background:#081019; color:var(--text); }
.dbx-in:focus { outline:none; border-color:var(--accent); box-shadow:0 0 0 3px rgba(34,211,238,.15); }
.dbx-ta { width:100%; min-height:72px; font-family:var(--mono); font-size:.82rem; padding:8px 11px; border-radius:10px;
  border:1px solid #1f3150; background:#081019; color:var(--text); resize:vertical; }
.dbx-form { display:grid; gap:10px; }
.dbx-pair { display:grid; grid-template-columns:1fr 1fr; gap:10px; }
.dbx-help { color:var(--muted); font-size:.8rem; margin:8px 0 0; line-height:1.45; }
.dbx-note { border-radius:10px; padding:10px 12px; font-size:.86rem; border:1px solid; line-height:1.45; }
.dbx-note.info { border-color:rgba(34,211,238,.35); background:rgba(34,211,238,.07); }
.dbx-note.warn { border-color:rgba(251,191,36,.4); background:rgba(251,191,36,.08); }
.dbx-note.err { border-color:rgba(248,113,113,.45); background:rgba(248,113,113,.1); }
.dbx-gauges { display:grid; gap:10px; grid-template-columns:repeat(auto-fit,minmax(150px,1fr)); }
.dbx-off { opacity:.35; filter:grayscale(1); pointer-events:none; }
.dbx-gauge { text-align:center; min-width:0; }
.dbx-gauge svg { width:100%; max-width:220px; display:block; margin:0 auto; overflow:visible; }
.dbx-g-track { fill:none; stroke:#16243a; stroke-width:13; stroke-linecap:round; }
.dbx-g-val { fill:none; stroke-width:13; stroke-linecap:round; stroke:var(--accent); filter:drop-shadow(0 0 5px rgba(34,211,238,.55)); }
.dbx-g-val.ok { stroke:var(--ok); filter:drop-shadow(0 0 5px rgba(52,211,153,.55)); }
.dbx-g-val.mid { stroke:var(--warn); filter:drop-shadow(0 0 5px rgba(251,191,36,.55)); }
.dbx-g-val.bad { stroke:var(--err); filter:drop-shadow(0 0 5px rgba(248,113,113,.55)); }
.dbx-g-tick { stroke:#2a3d5c; stroke-width:1.5; } .dbx-g-tl { fill:#6f84a3; font-size:9px; text-anchor:middle; font-family:var(--mono); }
.dbx-g-needle { will-change:transform; }
.dbx-g-needle line { stroke:#eef5ff; stroke-width:3; stroke-linecap:round; } .dbx-g-needle circle { fill:#eef5ff; }
.dbx-g-hub { fill:#0d1523; stroke:#2a3d5c; stroke-width:2; }
.dbx-g-num { fill:#eef5ff; font-size:21px; font-weight:700; text-anchor:middle; font-family:var(--mono); }
.dbx-g-label { font-size:.66rem; font-weight:700; letter-spacing:.14em; text-transform:uppercase; color:#93a8c6; margin-top:-2px; }
.dbx-g-label small { color:var(--muted); font-weight:500; letter-spacing:0; text-transform:none; font-size:.72rem; }
.dbx-g-sub { color:var(--muted); font-size:.74rem; font-family:var(--mono); margin-top:2px; }
.dbx-graph { width:100%; display:block; }
.dbx-gw { position:relative; margin:6px 0 0 36px; }
.dbx-gw svg, .dbx-wchart svg { position:absolute; inset:0; width:100%; height:100%; overflow:visible; }
.dbx-yl { position:absolute; left:-38px; width:32px; text-align:right; transform:translateY(-50%); font-family:var(--mono);
  font-size:.66rem; color:#5f7393; }
.dbx .gl { stroke:#16243a; stroke-width:1; vector-effect:non-scaling-stroke; }
.dbx .ax { stroke:#22344f; stroke-width:1; vector-effect:non-scaling-stroke; }
.dbx-graph path.line, .dbx-gw path.line { fill:none; stroke-width:1.8; vector-effect:non-scaling-stroke; stroke-linejoin:round; }
.dbx-wchart { position:relative; height:220px; margin:30px 0 28px 36px; }
.dbx-wchart path { stroke-width:1.6; vector-effect:non-scaling-stroke; }
.dbx-wchart .net { fill-opacity:.14; } .dbx-wchart .mine { fill-opacity:.3; stroke-width:2.4; }
.dbx-ch { position:absolute; top:calc(100% + 6px); transform:translateX(-50%); font-family:var(--mono); font-size:.7rem; color:#6f84a3; }
.dbx-ch.best { color:var(--ok); font-weight:700; }
.dbx-ssid { position:absolute; transform:translate(-50%,-100%); font-family:var(--mono); font-size:.68rem; white-space:nowrap;
  pointer-events:none; text-shadow:0 1px 3px #000; }
.dbx-legend { display:flex; flex-wrap:wrap; gap:4px 14px; font-family:var(--mono); font-size:.74rem; color:var(--muted); margin-top:4px; }
.dbx-legend i { display:inline-block; width:10px; height:3px; border-radius:2px; margin-right:6px; vertical-align:middle; }
.dbx-list { display:grid; gap:8px; }
.dbx-row { display:flex; align-items:center; gap:10px; flex-wrap:wrap; padding:9px 11px; border:1px solid #1b2a40; border-radius:10px; background:#0a111d; }
.dbx-row .dbx-main { flex:1; min-width:150px; display:grid; gap:2px; }
.dbx-row .dbx-main small { color:var(--muted); overflow-wrap:anywhere; font-family:var(--mono); font-size:.74rem; }
.dbx-kv { display:grid; grid-template-columns:max-content 1fr; gap:5px 14px; font-size:.86rem; margin:0; }
.dbx-kv dt { color:var(--muted); } .dbx-kv dd { margin:0; font-family:var(--mono); overflow-wrap:anywhere; }
.dbx-acts { display:flex; gap:6px; flex-wrap:wrap; margin:12px 0 4px; }
.dbx-prog { height:6px; border-radius:3px; background:#16243a; overflow:hidden; margin:0 0 10px; }
.dbx-prog i { display:block; height:100%; background:linear-gradient(90deg,var(--accent),var(--accent2)); transition:width .3s; box-shadow:0 0 10px var(--accent); }
.dbx-pre { white-space:pre-wrap; word-break:break-word; max-height:320px; overflow:auto; margin:6px 0 0; }
.dbx-topic { cursor:pointer; } .dbx-topic:hover .a { text-decoration:underline; }
.dbx a.btn { text-decoration:none; }
.dbx-chips { display:flex; gap:6px; flex-wrap:wrap; margin-top:6px; }
.dbx-chip { font-family:var(--mono); font-size:.78rem; padding:6px 11px; border-radius:8px; border:1px solid #1f3150;
  background:#081019; color:var(--muted); cursor:pointer; }
.dbx-chip.on { color:var(--accent-ink); background:var(--accent); border-color:var(--accent); font-weight:700; }
.dbx-eps { display:grid; gap:6px; }
.dbx-ep { display:grid; gap:6px; align-items:center; padding:6px; border:1px solid #16233a; border-radius:9px; background:#0a111d;
  grid-template-columns:minmax(90px,1.2fr) 66px 96px 118px minmax(90px,1.4fr) 34px; }
.dbx-ep .dbx-in { height:34px; padding:6px 8px; font-size:.82rem; min-width:0; width:100%; }
.dbx-ep .dbx-in.bad { border-color:var(--err); }
.dbx-req { display:flex; align-items:center; gap:6px; font-size:.78rem; color:var(--muted); white-space:nowrap; }
.dbx-ep-m { font-family:var(--mono); font-size:.7rem; font-weight:700; color:var(--accent); border:1px solid #1f3150;
  border-radius:6px; padding:2px 6px; white-space:nowrap; }
.dbx-pro { display:flex; align-items:center; gap:10px; flex-wrap:wrap; padding:10px 12px; border-radius:12px; font-size:.86rem;
  border:1px solid rgba(251,146,60,.45); background:linear-gradient(90deg,rgba(251,146,60,.14),rgba(167,139,250,.08)); }
.dbx-pro b { font-family:var(--mono); font-size:.7rem; letter-spacing:.14em; color:#1a0d02; background:#fb923c; border-radius:6px;
  padding:3px 7px; box-shadow:0 0 12px rgba(251,146,60,.5); }
.dbx-pro span { flex:1 1 200px; }
.dbx-proc ul { margin:8px 0; padding-left:20px; display:grid; gap:4px; }
@media (min-width:561px) and (max-width:900px) {
  .dbx-tr { grid-template-columns:18px 116px minmax(146px,1fr) minmax(76px,.8fr) minmax(110px,1.3fr) 56px 28px;
    grid-template-areas:"gl ip ty mk nm ms go"; }
  .dbx-tr > .c5 { display:none; }
  .dbx-tr .sv { display:inline; }
}
@media (max-width:560px) {
  .dbx { padding:8px; gap:10px; border-radius:12px; }
  .dbx-p { padding:10px 11px; }
  .dbx-tiles, .dbx-gauges { grid-template-columns:1fr 1fr; }
  .dbx-tv { font-size:1.3rem; }
  .dbx-tr { grid-template-columns:14px minmax(0,1fr) auto 36px; grid-template-areas:"gl ip ms go" ". nm nm go" ". mk mk mk";
    gap:2px 8px; padding:8px 2px; border-radius:0; }
  .dbx-tr:not(.th) ~ .dbx-tr { border-top:1px dashed #1b2a40; }
  .dbx-tr.th, .dbx-tr > .c5, .dbx-tr > .c6 { display:none; }
  .dbx-tr .sv, .dbx-tr .u, .dbx-tr .tyn { display:inline; }
  .dbx-go { width:36px; height:36px; border-radius:9px; }
  .dbx-cp { padding:6px; margin:-6px 0 -6px 2px; }
  .dbx-log { gap:3px; }
  .dbx-log div { white-space:pre-wrap; overflow-wrap:anywhere; padding-left:2ch; text-indent:-2ch; }
  .dbx-pair { grid-template-columns:1fr; }
  .dbx-bar .btn { flex:1 1 auto; }
  .dbx-ep { grid-template-columns:64px 1fr 1fr 34px; }
  .dbx-ep > :nth-child(1) { grid-column:1 / 4; grid-row:1; }
  .dbx-ep > :nth-child(6) { grid-column:4; grid-row:1; }
  .dbx-ep > :nth-child(2) { grid-column:1; grid-row:2; }
  .dbx-ep > :nth-child(3) { grid-column:2; grid-row:2; }
  .dbx-ep > :nth-child(4) { grid-column:3 / 5; grid-row:2; }
  .dbx-ep > :nth-child(5) { grid-column:1 / 5; grid-row:3; }
}
`;

// The makers of the network cards seen at home and in small offices, by the first half of the
// MAC, from the IEEE registry, with the type of device each one makes most. Written by
// tools/oui.py: the prefixes of a maker sorted, as the distances between them, 5 bits a
// character. The whole registry is megabytes. Phones and tablets show a random address, with
// the bit of the local addresses on.
const OUI = {
  Hikvision: 'val1vpn6xzw1qdp6mY6uhJutAngg7ffd7mkrGww5sxZNYh6nj2ti5WcQvrf7-kp8c-uEsqaGdca9nb2cYv6smBspNvmq7hvf7vwGmrMjYEywx7fwf7qyDc_l6odJkxu7mvfHnna7cmqNzcYOZ-CjoG_tDhrd7dl1v_m8van6sZn1ap-F-gg7xs4ziGmi4wlZGhfb6hc5ts8WaMfhCjxuFYn4yzi8rZf7slXFxbDhfFgk4wi3yrb7bh1gu5Zjm7oxz8ndx6WZrHbzdEopo1ZYw7ly3tc5upz7Yxo7aaY8hv7WhXNXmq8',
  Dahua: 'jlxH-ikN_sgV_bte1YYj8XX3pneUxwuX1lWYGewdEubhHhaem1rdc6ZvzHtqc6wbg9-uZd1anvFxtX8ghy8gYcVm_wNji8X_q7sgZX1dihFqwL',
  Uniview: 'eqkf1x-he3xuXe2Xcjs1csqu3',
  Ezviz: 'iofPmhbe1XvYe1Wyg8mzut1Ykk7-ZbNkrp1bzid1ku6bpgu1pWik1wz-W4uXsFgWjF',
  Reolink: 'xkyuE',
  Axis: 'iaGYWZvAX-rNssW_2',
  Hanwha: 'ZXjf4__--9',
  Wyze: 'kqgv2anp_4wxp7xtz_4ahYX2',
  Ring: 'ZZj1bvol1kYhNksaW1Zkfv1oz-7bwvEbuqGfmht2djhOmjgW1g_km1kYuE',
  Arlo: 'apum4-tht5svYn5',
  Zyxel: 'fw4Yq1jn2jcVnpW7tXd1gvv6yuoGyn2hmIxhx6vKsja1fYhFmYjNwexFyyf1gokOcrq6_Zb8Wg1anY9vei7_n9ore7fYUXue7vbY8xas7ol1iWU_m4idl6n-g9XnyEvm-7x--Gvii7rty8rrmFruH-iv6ljn8cYK_ldEaxrHnmpVgoeGzx5Wna7ko6-nDmut6o-w8Zgc7tc8yX6dcy7ZilFYv8tvq8xzMcygMlud8vvX8Zg1Wxm8cmn7jYm7hgb8sXV-lL',
  Tenda: 'clb9jug7sWn8-qwNkYel3-x6hvh1tv_FxWXu1qlmNagvMpyFvjYW2jga9wxh6jsg8kqi8_ipNity7pdXOW-iW1',
  'AVM Fritz!Box': 'kW1-d4Zg1xFrN_j1WWa9vku7Wy7zWl7xt-W1YzzEilrFkrLvWeFWWKb_Nivh6znf8zuR-XgFesZ8XeYHZwuMqWx8odZd1kclHeYf6sX-m1wi3Xod8Ypge1rpY8ccYEZ-d8kewFgWm9jbn6oma8hdyFdz8ctp7uW8ZujHabqF',
  Ubiquiti: 'jh5rj4qjy7-fqHrm_NYxe6mqNnXyFmlIoWp7lhz8gXzl1uX_d1pWpEgWZGrWHfgP-YZ7fwm8Xat7tdApZJz7lvo6ecHeaeFWjp8-WkFxhl1n-q6zbw7dxsFfvyHjxe7zvx7xXyGrlkk1WXx7e-v8ljg8ot3vht7__m7fbr7_vvOzty7dWKqmYG',
  MikroTik: 'YY3w-v9reu6ZYgX1drod1_owMWdtW1mn6Yaj7ewym1tmwEzjaGmdp8y-p_3WwmOibWFnXl9ozb6gbo1XmuE-euGhcaV',
  Netgear: 'xg2wo1tb1XVYNxNf8iOpJWIWWQ_oa8zfu6xXDwpX1evpEq8acp1zx_6euiNtjGhrb1-wcFvx4jqCbjw6gXW1znb7xmq8uX_7vqe7ylf8wxj8oZ4mxkFmiw7ry7whd7nggn1ee7hnzEpqZ9_cvEffb1Ysp6lYeGmsk1eleEzr8ywQnwbFyWZ1Yi2pdi6tg7ft7nt8x_n7gkZOZpHnkEdywNzlc7ccm1XYX6ojEepq7qmZ1jqe7vnhX1nyr6q2duIxYCspi8f_X8btzU',
  Mercusys: 'u-w1vudFWga7ip-d2nite6Ylrm6',
  Cudy: 'g-hX8Xltc5',
  'TP-Link': 'ht2ji2eh1lPuW1cLvDpCvp2rkB__h7_ck1pjCvhf6lyDqYz7us9tn3bGjbRWdk6WrCuYf1Zrz6Ye4kiHoRoc2Wq3oc1li_6ojFm-Bbx6dko6cX2xi7qbc1fdg6rs4XkD_dStZ1ppp6lqAfu3gwm7euMgg2kgNixY7rsx7hWJa9faAzm7nav6vhDqr6kxDYq3lXZ7YxW7nXGf-2kn4vh1g4Zk3r-2rna7jdAfmLxa6msFmi-6zaJ_zs6ex4qbFcn7poHqgp6ydh8v6vvKyxAWp2uf3ji9nu-6khCqa2uZ6gq2ZZ5pgd6ncFotb1vpY7q9xk9rb7t4hoG-Xh6yz4ci2h-1hhIy_7q6mpFwsy6ob4a_1hBYvX8ic2dkNak8Xgp6ql3erLxuDatv6koj7a_C_de1oz3qa5Zie6ib9-w6wYBnY5qw7mxo7nkAhf3urBcqo6peJcm1tv7pzEodh6tie1hy7uhp6p_SayX7pkPmg2or5rb-6srUn-4tXIxmb6cgp1zi5-Xb6yj8zs8bzJpq8bZ8iW1wqm6um9Wz2o-JXPijp8vpf6sh4ziw7x_4q4bc6jb5sv5-uf7avRhv8mfy6vwBij1lwHPhn4zr5xgd7pYL_a4W-j8mPgxY6yxF-sY1os1wph6cv7iZ5hz3uu5hcQgo6olY7rPmXUtxdEglAaAjaDdmKqcx6qz3yj9cl6vcKjjk6XbRncQtci6Xh1s_1yxBfLwm5Y_a1fwZ6YmO_yc1jzX6bo8uwg1jZy6xwVeW4oy9qlp6-XJnfIaog6as3xy3fjEva5Zy4YbCy-p6and1bWJdwj6orG-q5W6hzPrn1-Kygf6fgLnZ7YdF-e1mNYnX8',
  Honor: 'due1kpr7cyY8cvMexr7wFXXgFk_vGXxk6vkg9d-u6XpCjmx8bybFqYFgnt7o_xGmwk7Xhh8dY1fqXFgpe9kkwN-y6pYwEquJXeIemjFc2hfj7qsq7wuj1wcnEupr7fYe1_mtOhti6nXn1pyf7tY8hqn6mfv1zs_6uaCthOZqwNxtc7owj8rWr6Yif1vnp6kpNwho7vuBfanGpon7rac1qriNufz7WdXFXXhGce4_enGmpsF',
  OnePlus: 'l_fv2kya8eonk1joSy-s7X_nVgvYHzelFcjZPfvW8zzyUhaWGlg1pwd7p-XHeopN-YOmk5Zdce1XqsUczzd1',
  Realme: 'ZYaPvnLxYd7kvSYusNdZ_7lyz-1giW8vhHcqz6wgc1lr_Npuw7d2Zqc8owf8YsW8aitMnoD-mqOZpIzvh6tmZ1_vBuoW8qZn7sgv7o_o8fwg7fne8Zql7dguGgIyYe8omo6kaa1pZf7ruCw-Z7ldX1tcs7esd7ufb8oWd9Xzj6zdX1sd2j_WFZoCXeFvqu7ufNZYzMZnd8-n8maAtghNgiz8dnX7vuJfrs6vp8yzf1fx1',
  Nothing: 'zns8kbvm23_lyV',
  Nokia: 'imNyua9xctc1_gx7ql_7cXse2whsFvbQjdXe1vp-7asY8lwo7jz6pgnFownd1hlqGwvo8kokMmur1Xil7maW7XpZ1cuY7rsgVdpZ8vmZPgZWd1pn2YecOertNwbZ1',
  ASUS: 'kZ3uHfKf5yF-G_9YE_CgBaBi9w9lI_9u9iCWmk1Ymu6iWK-qGnf1vpX7uFogNwkt7ojt7z8dtGpX1kxb8WMsWr6jhr8WWCaWc8kc_6sgWGei2bsf1vat6lxAm_Fzoq7qYb7zdg1utf7_nc7ulc1ZsgFYsa9YZs6vif1mbc6gzc1ocy6fbjGXqNuvAtl_UooF-Yn8aar6wWx7jvnPasZGqkzMkns7ZtTcbiFc-m7iWVebq7ZX5Z_gOfZ4jwBip3vuuMqw7sgg7uux7Yjn8Yyy7ZpxFbWZ1tcqFlwaFblIhzy7Wln7pjW8yiZ1',
  MSI: 'sZ3cZ1tNem1tTpVyKbhlW4',
  Gigabyte: 'Xh3fKxa1XJdRmPpIz8mPguxW1vbRjZmFWss6uYLgrvd1jwjW1j_rW1WuYu1plvEvlV_trs1iwi9vWq-1wXie2hqzGsbrt1',
  ASRock: 'Yf9aorf5yoYt1-fvt2vojOxqld1bbyd1',
  Acer: 'a9rwbXC',
  Framework: 'julv9',
  Intel: 'mt4IUk5Qu8NuA7q31pFp71lAs2eA1W91Z412q61WH1D1lC1P1XE1lF1d22eFWg6sbCmr7vCzm4im5cx1uk4lMoZ1jj1Wtk6sw5Zv8qh5lpEZaAbZ1fe5zCYKx2xTebe6WoIlt3hs1We7ax3-s5ih7kEru5qdw6Wm9el4ri1gb2z_4nw6ee2oq2nif6qd2to8yu3Xh3gq1cr3anCfd2Zp2yQiyHbm1vre6_l9ec4nk9aX4co9puIouf6_Y2vw5-c6ly1ofDtcEttCcbd6xjCd_Irz9jjr6l-1xz6rsFbWH-Z1n__6thDpWTaSsjd6ug1be1eaLwa5nn4fPYw2_Upp3ru4j3hr2Xu3yjh6nj9fpEa6xe3nm4sgJr_3fmc6jv7on5ZY1ZrImnMg1hw4tyY6Yh4ls6cr4vaFdwIvr1an3pOWi1jd3nLbv1rrW6ZY6hXKxh4dk3ZvCrz8hl7aef6cvDbl4ehCbl3ZWGwm1nY2yi1bqd6_f2-uJitBhNba8tl2cx6a1lpb6yjDWb4uxExr7jx2llD-zs6hY1ta3Ebq5zi8hq6ef2szClX4mka6l_2uo3ot5p-3ne1am5xY6ZDi2vs1XcAst1utEeel6ug1vFquHWk1ZX2p_Mit2Yig6-f8wrDlZ8pyLhWk6jvJYAeZ9yKoDcm2sW2nl9wgBhkr6um3yW2pf6ct1nsAnGsg4dy1fi1Xl3bRprq6no3gl4eW4cg1yhEizDzi1tv3rW1nXl6rs5W8vDay2ik1wg4mu2fr1_zEYa6WEp-1zwf6hAqW4ud7bm2_i2ra3fd2Xf7eYJvm9gY3cai6hl1Yh7ri7zi7oY3wa3wkLwx1yjY6hl3zb1lHmf5wrQwRXq1dGrj6bq2yHwc2_4lXh6vt3gu9fmDkxIp-6jc3k7_rf6kq7yeIwn7iZ7lYKple6dd2eo9_Z3gr2vf2wg1yY1nnDxDfNow5pwi6hu4-m1_e6er1Yd3irFfJkX5rv6fk2ya9Wye6pW3gv1qs7er8hw2vu4ku5ZZ5-s7xW3tg7vr3zgr6bg9-aEzu9ib7_pe6zn1qzDmu6kc7wY7yt3Zm3jx8ts3wea6up1bk7cj6uHqz5sUzr4W_DrAfx6xk5qYn6Wm4nt6zi4jZKaJlkAxb1Knjn6ps3hZ2q_2jX1_g1dk1bmB_m8xm3-bGuvg6Wc9rz5hFbwBrn6kdAgr8zjX6zq9zg3pb6vDbY2vu9-b1YQh2nW7eW1mt1yRmo4yk3Lny4kzc6nW9lm3Yh3s_2bi7hmHWg3ld1a5_aDiXu6oy2vb2Zb1za3zsX7yX3xc1mLWl4zo4aX2je5XZ3iv2eHmt1cr1ruQoZ4n-j6zv1ju2_a6yc7yt9laElli6af6_eGxMdCkk5Ww4b_7dw7egh6gk1fcUec2g-3ql7bx1o_DZ_Y6Zk1fo1qY2Wd8ty6pr6rY2lhCpd9pa1vs7i3eto6liQeHft3gCbvo6Wx2uY5wj6qn2zoBzw3zcE-t4ab2om9lmY6cz2gW3sv4bX9Dfl1_w3oa7qv2jc7Ze6ds5kc5-ld6Wo6nm3gXd1uX8xnm6yw63wl3zb6WjBYp6qwt6vh5di6Yd6yv9ur5nt9Xm1tf4Zj2oao6ZyW1eyCvj1mW1kud6dj1KjY8zSY7uf3sn7oi6Xd1nz1tj4tkKXbi6YH_a3izDsd7xe1gc2pf9ks1pu1_W1a4r3xd3ZYe6xq2_q4ns8wX2tw4WFXb4vzClPux2lY5rfCXJeIbjh6q-Ddl8WXBt9yj3f1eY2_-Aaas6hw5WrAmfMzg1un1Zkh6ze8gi5zW8dq9lZ5bRtl7gDgbi6mW8_g5_o2ib2yo7gk3dg4al2Xo3iq3fi5vi1bNign6jl7mf9lf1_a1ZiCok2nk3ed2bm6ni5xf3WY3ann6rdAzl2_s9tg5IodH',
  Vestel: '_k2y-um4WhyOWe-Vva_MZ-mW2uYyk1otof2-qw_1',
  LG: 'YZ7pGs7u3uGk5wEYBz7ul3ke2vk6uiIte2XnDysk6pyKghv8xWz6wzd8pjY1zx1azp7lnpMZp2gviOxb1kfj7tzPfoFpXIieq6uth1gWj6twMeii8xz3vlX7khNlXgFsv1m-BudnFjnwFsrLhb3ytMoty6ZpFelZ7-ZEWoRshg6uZVo-2Wr9os1dk8jywFuo1gqh6kme9yo6wf9muz6wrc1g_w6lrZ1ftv6dghFquBnxa1pey6Zx6cilOkd9zn7WuuEki5alGfgXFig3knRYo9jbr6rkJdWd1-am6yt-8_fCgm3dk1fmf6YWs9gYp6viW8ppLdtx6qdRyfVppk6qZp1zlc6Ykk1vlWFyxk7dXqOfwDlhYFXjvF_v2sxA-gNfW5',
  TCL: 'pzmHlewVmxie1kgyEscr7esi8wqhOnhm7lixVccs_2amwe3fzfOean7ivhF',
  Hisense: 'rq6vbpGnuKuo5tawErdz7fkW8cfk8nl4nym8zze8nZdGzWZErow9cXsFddil1_ij7nt-GnieW1X_vd1usv7Ydd7Wxa1lciUlrObudGeY9sbf7mtWOYna8sWgFfX9_pNdx_6ea3hx-7rYYX1igeFgsJj-sGZbyU',
  'Philips TV': 'rsZn1lYem5',
  PlayStation: '_W1st3iLaW1eQwKcd1agj1mo2twa8hc-EYmZ1tzZs1XtCZft8aoBt_ud2Ysj8_fdFgn4cmaN-jlG__DuogGskdOnduMctiHzil6ZXhPgtfMrZ5y-aG-hBqZj7bijGng6a_aPbpzc1ooX8cyxFgmu7mqY1wvl6wok1',
  Amlogic: 'mWfv1',
  Amazon: 'dgSwc5veDdXEgf2wmbEsgHgt4gX4g9aZ2uZ3k4ve5ex6rkW7mUecb1em4Wxc6waZ1j-5de4xq3Zcl6ppX1Zl-6ZjFmxAYqd7srE-oHdpQcqa6Wca1dg7-j5giAZtp6mgW1sqr6jvJhuU-tW7Wvs7_as7rlY1zr8aid7fwX1div6zxX1bif7re6qt1-t2llGjiY6heFjb1iu8oZ2ck3jhEwe8nmx6mw6nf6b-EXvu6vbFmb3puZ1ZWc6tia1YuEha2pd6Ygj6hpt7tba1gr3WzEZtf7cqlGnUZxi6ks4s_8qe7giHjg1zr5ah9co6_ob6teIc7pl4peCjur7fYw7rY7YrCyo6zk4qGr-2cr2o-_6xzCacGZZW7_CXpSWZ6pvd6uZOhcEzmW7dX8moZ1ltq6cXIjxf7pf4sX9wsm7ck4am5ZsRiWKZpm6aq7dFoxn7bqg1s_-7plk6Wmp1rEcWi6gg1spm9gz6fws6qo8-fQWrf7iW6ZXHv_9fY1jZb6YXRcrEoeBaw-6-td1_tWEyzY1eZ1sW1nv_7_C-ir7rhNXds6lWVXg4owHwkcFsgh8nhFXes6edFnu3jog8hWBYfrEqXZ1chj6dm7qh1bhMqp5rlX7nZ2--7tlCob8icCpj7cqa6kiq8fXg1rqa6tjEnb7bzEad1YdClq4',
  Sonos: 'yX1ym2aWye3lzm7jbvW1ZxWNYdWHZvo8WYrd1gpt6eneGiYre1vloe2-vpMiolv2ZhvE',
  Bose: 'ga3znn8uYZ9cqi-1y_h8yZut1Wgv8rZfe1jonGbgkUjWbf3faoVfvyNvitt1',
  Espressif: 'ouIrd9ZwY1-zX7_vBcYY7b3WyLba5pm1re2w-6d_9Wcr6rdFWaBfZ9zze6Xl1-q6XfCX2iyFrm1-a4Y-2ci2bpo6dv8jX2tXCimNlho7cvSmsg6yWGap8pUhp3fl9fX6td2wam6Zo7bZ3at7ubBvmEcm4yte6dlGn-2ln2zZ3unC-h4je9uc1dy3xgc6giCWu7soBZpj7pkW1txDdcl6ylHZb3iuApY6hH-Z9bjy6pk9bAtl3cX1xu4zc2eRsk5xpo6cn9jp1lYo7rFZmKwb4ep4ul1soKva3gkk6-d2qd8daHl-Oppe6ZZ1kBikNcoDtW3row6rk2Zn3_f5uWIYs4mo6cd7b-3Wsc6txOyt3XxFqYAxrb6ilTend7_w6kj4qt5awGpcZ7s8kbOarCqY4iw5spc6aop1rmi6bZLxui8qlBhfCqee6sa7egGjrDXd6dy3rYAY_3dxl6-pTg_4mk6lie6-3jzH_vOjl2dcb7gxErua7eg1ua8nge1_s2pmd6eW8cw8h_8sX5lt5dqDzo2me2wbt6rp3la4g9Xz5qWEaZFeHqjc6wo4p-f1jd1Zw9uOZl1vsb6dtGx3qnc1ZYr6yh1iVffFsb2sk5rf5ap3xtBzzb6cu8laBk-2ix9ax6vv9_v2rur6se5fNhcLb-2fX1eeBevr6ovIsn6XZ8rn2_t9ptd6tvW1gg1lIpiBbbr6ejLry3mk7oz4dm9sd5xfY7o_JZSgp5pq2xua6vzFnScKjWj1qtZ6ze2so4aYGurEwa1mhAox6j_q6njGWa9k9gt2yj6jpx6b-JWY7_i6ao9oek6kWNa-Ffu6ll6vY2ace6yan1hi8Yro6XY1vw3jYSzgW7ha3WsGtoEYa1hCqbp6YKfvLwf1evFca4eY4wOpObYu6a_5rq4ZMZy3rdBme8eul7aZDZZ8ymb6ghX1jAnaIwe5vy3zic6mbAokInp6xW7eSxiCweb6qViZ5jYCpg1y9evNpf9lWW7ca6zo6eu2WtIosW7hrIWMjxq6zvAq_2cvAZOZyCtWFcq6sYx6fJYWCqfDyW3WX1vd2ey5rmZ6ig3ys3bjU_uLzfd6ls6YwDtqQiYz6jv5yaMrhp6xvi1vYDae3',
  AltoBeam: 'ZqzGgpem1jyiOaZ3hgv7pos7vZl7om8azHppwMkiInoi8_uA_wt6ZZ2ycAXlZ9kof6-ai9dhY7WWY8piX1ZWb7mXs7chs7v_W1evoOZjk7bwx6cop1joa7dkh7s-i8udr7onp9xaY6qzVzY8kWz6pmh9hcf8kcZW1hkiFnfW7XyMump7vkpOb_aFrb1knw7ei-7lgi1fisExun1oyfMshb1XcCqucG',
  Tuya: 'wxCzqfW1gwUtZbFuZTlri7pbu8_rmNiWb7murFug3adUmiv6WkdX1jdu7pDqxol1foiGmdxOsmx6cs_8csht1joxFdhNyrc8riZMnuh8rvm9kqY7-kYGenf7XWK3cxe8Xc-6wdg1yo3gn9ooiNfYwc1gjf8eoA',
  Beken: '-yXX7WovVkjsl4',
  Shelly: 'idWe8',
  'Philips Hue': 'ey5kmaeCaZZn2oayU',
  IKEA: 'gaxn6',
  Yeelight: 'XYfmF',
  Meross: 'flun4bkXu7',
  BroadLink: 'tkqv1mkZGYthVdfjt6yncHgZk-3ymMyvc7',
  Nanoleaf: 'ttYX8',
  Netatmo: 'moxX7',
  Bouffalo: 'bekn1cwl7tjZGoq3vwf8mYvd2trsFZasd1nfjOwkjFgsUedi7qajF_fo9iluFzmsEjluFpea8lXy8gdKcrjEnWj1Yh9vjr6YmcPhbk7hpLZbm_1wq8WW-MWjqF',
  Aqara: 'ynmn1eehu3',
  Epson: 'e2Zp9dzyl3WwW1objOugXNznhFvdmPwb9eow6_dse3fZgGxu5lkgEiZq9wXuN_zjl1qvWGmhIlpx7pjwk1ssg1',
  Brother: 'fz6kcPbdX_2uifOauin5sqdt1ohf7',
  Canon: 'b4gm7ofd1hdqk1moaf1_ZWGahsOmtYUhqtOnXi7nlgOyoj1_knEfuX1jwmMkvj9ycp7tzgd1nglHmesc3wi-8mpll1-YAf-u6jeb1',
  Kyocera: 'fcyfD',
  Ricoh: 'q3_l9cmam5',
  Xerox: '0111111111X5arav9-qkt4',
  Lexmark: 'WW1WW7tDsvOgyXm7',
  'Konica Minolta': 'hZ8_XCy-hF',
  Synology: 'of4-a-_8',
  QNAP: 'xa2Znre2udvdC',
  WD: 'W6kX5xzU',
  'Raspberry Pi': 'Xkpn2cjW8tvqt5szsW1tig-1otaW2rww8lckF',
  Supermicro: 'mi9ur2oXbPrZgW3chr_3pzbd1Zhnt1hrj9',
  Google: 'nm6lWt1koY6YYo1uum7Wo4fr5YxZ8lib8vao6pud1hraFXrd1Xbh6kY5mXh1xc4qdb6iz8x_QW_JwZs7xreGqd7--k6psZ1Zl5hfg7YZ1qed7de2WgAipCgvb8Xsu6bf2hug1wyc8cng7kutFWw1Wdl7fvf1fXw6ndl8YuHgkv7bwYNko4xhk8zifFwc5_qg1espFWeb7xt5bvRaioEonl1wsq6iy9WYKiiLptp6ysNuiEic_6zlu8tkn7oWX9ixm6pcAtvVeym6-_FnpCfgEws3loh6-Z4oXs8axNih6qrw7oel6ppZGad_8mYJxmnExuk1yuz6sW8jsNrm-7aij6yvm8dna1upt6npX1iwXFpp5yaVib2osY6hqUfYUGnmc6on2_iTigi7dYK',
  Apple: 'oo7Yewt3lpoX4tabNjwcVWoWPhYkGaWcVaghe1-ub_1',
  Samsung: 'iveu1nofW4_Xzu1sgkd1emkW4',
  Huawei: 'ydu1iWpl4',
  Xiaomi: 'dWxm2fYWn2mfdc1yYYe1lWk8',
  HP: 'qxdW1tjkv2khj_5',
  Realtek: 'iYu1',
  Technicolor: 'yfjXA',
  ZTE: 'algv9YYol3',
  VMware: 'sYK',
};
// What a maker makes most: the page guesses the type of a device from it, when nothing better says.
const MAKER_TYPE = {
  Hikvision: 'camera', Dahua: 'camera', Uniview: 'camera', Ezviz: 'camera', Reolink: 'camera',
  Axis: 'camera', Hanwha: 'camera', Wyze: 'camera', Ring: 'camera', Arlo: 'camera', Zyxel: 'net',
  Tenda: 'net', 'AVM Fritz!Box': 'net', Ubiquiti: 'net', MikroTik: 'net', Netgear: 'net', Mercusys: 'net',
  Cudy: 'net', Honor: 'phone', OnePlus: 'phone', Realme: 'phone', Nothing: 'phone', Nokia: 'phone',
  ASUS: 'pc', MSI: 'pc', Gigabyte: 'pc', ASRock: 'pc', Acer: 'pc', Framework: 'pc', Intel: 'pc',
  Vestel: 'tv', LG: 'tv', TCL: 'tv', Hisense: 'tv', 'Philips TV': 'tv', PlayStation: 'tv', Amlogic: 'tv',
  Amazon: 'speaker', Sonos: 'speaker', Bose: 'speaker', Espressif: 'iot', AltoBeam: 'iot', Tuya: 'iot',
  Beken: 'iot', Shelly: 'iot', 'Philips Hue': 'iot', IKEA: 'iot', Yeelight: 'iot', Meross: 'iot',
  BroadLink: 'iot', Nanoleaf: 'iot', Netatmo: 'iot', Bouffalo: 'iot', Aqara: 'iot', Epson: 'printer',
  Brother: 'printer', Canon: 'printer', Kyocera: 'printer', Ricoh: 'printer', Xerox: 'printer',
  Lexmark: 'printer', 'Konica Minolta': 'printer', Synology: 'nas', QNAP: 'nas', WD: 'nas',
  'Raspberry Pi': 'server', Supermicro: 'server',
};
const MAKERS = {};
// Adds the prefixes of a maker, packed as the tables write them: this one, and the extended
// one the device gives on the boards with room (command makers).
function addPacked(name, packed) {
  const digits = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-_';
  let at = 0;
  let n = 0;
  let shift = 0;
  for (const ch of packed) {
    const c = digits.indexOf(ch);
    n |= (c & 31) << shift;
    shift += 5;
    if (c & 32) continue;
    at += n;
    MAKERS[at.toString(16).toUpperCase().padStart(6, '0')] = name;
    n = 0;
    shift = 0;
  }
}
Object.keys(OUI).forEach((name) => addPacked(name, OUI[name]));

function isRandom(mac) {
  if (!mac) return false;
  const hex = mac.replace(/[^0-9A-Fa-f]/g, '');
  return hex.length >= 2 && (parseInt(hex[1], 16) & 2) !== 0;
}
function maker(mac) {
  if (!mac || isRandom(mac)) return '';
  return MAKERS[mac.replace(/[^0-9A-Fa-f]/g, '').toUpperCase().slice(0, 6)] || '';
}


const C = { cy: '#22d3ee', vi: '#a78bfa', gr: '#34d399', ye: '#fbbf24', re: '#f87171', pk: '#f472b6', bl: '#60a5fa', or: '#fb923c' };

function since(sec, t) {
  if (sec === undefined || sec === null) return '';
  if (sec < 90) return t('mod.expnet.sec', { n: sec });
  const min = Math.round(sec / 60);
  if (min < 90) return t('mod.expnet.min', { n: min });
  const h = Math.round(min / 60);
  if (h < 48) return t('mod.expnet.hours', { n: h });
  return t('mod.expnet.days', { n: Math.round(h / 24) });
}
function clock(unix, lang) {
  if (!unix) return '--:--';
  const d = new Date(unix * 1000);
  const hm = d.toLocaleTimeString(lang, { hour: '2-digit', minute: '2-digit' });
  return d.toDateString() === new Date().toDateString() ? hm : d.toLocaleDateString(lang, { day: '2-digit', month: '2-digit' }) + ' ' + hm;
}
function pct(perMille, lang) {
  if (perMille === undefined || perMille === null || perMille < 0) return '–';
  const v = (perMille / 10).toFixed(perMille >= 1000 || perMille % 10 === 0 ? 0 : 1);
  return (lang === 'it' ? v.replace('.', ',') : v) + '%';
}
function num(v, lang, digits = 0) {
  if (v === null || v === undefined || !isFinite(v)) return '–';
  return Number(v).toLocaleString(lang, { maximumFractionDigits: digits, minimumFractionDigits: digits });
}
const isNum = (v) => typeof v === 'number' && isFinite(v) && v >= 0;
function stats(values) {
  const got = values.filter(isNum);
  const all = values.filter((v) => typeof v === 'number');
  if (!all.length) return null;
  let jitter = 0;
  for (let i = 1; i < got.length; ++i) jitter += Math.abs(got[i] - got[i - 1]);
  return { min: got.length ? Math.min(...got) : null, max: got.length ? Math.max(...got) : null,
           avg: got.length ? got.reduce((a, b) => a + b, 0) / got.length : null,
           jitter: got.length > 1 ? jitter / (got.length - 1) : null, lost: all.length - got.length, n: all.length,
           last: all[all.length - 1] };
}
// A top for a scale: 1, 2 or 5 times a power of ten.
function niceMax(v) {
  const p = Math.pow(10, Math.floor(Math.log10(Math.max(v, 1e-9))));
  const f = v / p;
  return (f <= 1 ? 1 : f <= 2 ? 2 : f <= 5 ? 5 : 10) * p;
}
function stateOf(x) {
  if (!x.known) return 'mid';
  return x.up ? 'ok' : 'bad';
}

// What a device seems to be, as a mark of the terminal.
function glyph(h, gateway) {
  if (h.self || h.loopmax) return '◆';
  if (h.ip === gateway) return '⌂';
  const s = h.services || [];
  if (s.includes('printer')) return '⎙';
  if (s.includes('cast') || s.includes('airplay')) return '▣';
  if (s.includes('files')) return '▤';
  if (s.includes('homeassistant') || s.includes('esphome')) return '⌂';
  if (isRandom(h.mac)) return '▯';
  return '•';
}
function nameOf(h) {
  return h.alias || (h.loopmax && h.loopmax.name) || h.name || serverName(h.server) || '';
}
// The first word of what a device says to SSDP, when it is a name: "Sonos", not "1.0", "POSIX",
// "Linux" or "Unspecified".
function serverName(server) {
  const word = (server || '').split(/[ ,/]/)[0];
  return /^(\d[\d.]*|posix|linux|unspecified|upnp)?$/i.test(word) ? '' : word;
}
// The web page of a device, when the search found one: the console of a LoopMaxPro, else the port
// mDNS gave for its web page or its Home Assistant. Not the device itself: its page is this one.
function webOf(h) {
  const s = h.services || [];
  const port = h.loopmax ? h.loopmax.port || 80 : h.webPort || (s.includes('web') ? 80 : s.includes('homeassistant') ? 8123 : 0);
  if (h.self || !port) return '';
  return (port === 443 ? 'https://' : 'http://') + h.ip + (port === 80 || port === 443 ? '' : ':' + port) + '/';
}

// The types of device, as the list shows them: their group (the buttons of the filter), icon and
// colour. The owner can give a device one of OWN_TYPES, which the device keeps (kTypes of the
// module); the others the page finds by itself.
const TYPES = {
  router: { g: 'net', icon: 'router', tone: '#a78bfa' },
  ap: { g: 'net', icon: 'wifi', tone: '#a78bfa' },
  pc: { g: 'pc', icon: 'db-pc', tone: '#60a5fa' },
  phone: { g: 'phone', icon: 'db-phone', tone: '#34d399' },
  camera: { g: 'cam', icon: 'db-cam', tone: '#fb7185' },
  nvr: { g: 'cam', icon: 'db-nvr', tone: '#fb7185' },
  tv: { g: 'media', icon: 'db-tv', tone: '#e879f9' },
  speaker: { g: 'media', icon: 'db-speaker', tone: '#e879f9' },
  printer: { g: 'printer', icon: 'db-printer', tone: '#94a3b8' },
  nas: { g: 'server', icon: 'hard-drive', tone: '#2dd4bf' },
  server: { g: 'server', icon: 'server', tone: '#2dd4bf' },
  iot: { g: 'iot', icon: 'db-plug', tone: '#a3e635' },
  loopmax: { g: 'loopmax', icon: 'cpu', tone: '#22d3ee' },
  loopmaxpro: { g: 'loopmax', icon: 'cpu', tone: '#fb923c' },
  loopmax1: { g: 'loopmax', icon: 'cpu', tone: '#5eb8c9' },
  other: { g: 'other', icon: 'circle-help', tone: '#6f84a3' },
  '': { g: 'other', icon: 'circle-help', tone: '#6f84a3' },
};
const GROUPS = [
  { id: 'net', icon: 'router', tone: '#a78bfa' }, { id: 'pc', icon: 'db-pc', tone: '#60a5fa' },
  { id: 'phone', icon: 'db-phone', tone: '#34d399' }, { id: 'cam', icon: 'db-cam', tone: '#fb7185' },
  { id: 'media', icon: 'db-tv', tone: '#e879f9' }, { id: 'printer', icon: 'db-printer', tone: '#94a3b8' },
  { id: 'server', icon: 'server', tone: '#2dd4bf' }, { id: 'loopmax', icon: 'cpu', tone: '#22d3ee' },
  { id: 'iot', icon: 'db-plug', tone: '#a3e635' }, { id: 'other', icon: 'circle-help', tone: '#6f84a3' },
];
// The platform of a camera or a recorder, from its typical ports: many brands are made on these,
// Xiongmai (its app is XMEye), Dahua and Hikvision.
function platformOf(h) {
  const p = h.ports || [];
  if (p.includes(34567)) return 'XMEye';
  if (p.includes(37777)) return 'Dahua';
  if (p.includes(8000) && p.includes(554)) return 'Hikvision';
  return '';
}

// The maker shown for a device: SmartiLab for the LoopMaxPro devices and the LoopMax of v1 (the
// MAC says Espressif, the maker of their chip), else the one of its MAC.
function makerOf(h) {
  const k = h.kind || '';
  return k === 'loopmax' || k === 'loopmaxpro' || k === 'loopmax1' ? 'SmartiLab' : maker(h.mac);
}

// The extended table of the makers, from the device on the boards with room (not the C3): asked a
// page at a time, once for the page. How many pieces it added; 0 none.
let makersAdded = null;
async function loadMakers(ctx) {
  if (makersAdded !== null) return makersAdded;
  makersAdded = 0;
  let from = 0;
  for (let page = 0; page < 60 && from !== undefined; ++page) {
    const r = await ctx.read('makers', { from });
    if (!r.ok || !r.result) break;
    (r.result.makers || []).forEach(([name, type, packed]) => {
      addPacked(name, packed);
      if (type && !MAKER_TYPE[name]) MAKER_TYPE[name] = type;
      makersAdded++;
    });
    from = r.result.next;
  }
  return makersAdded;
}

const OWN_TYPES = ['router', 'ap', 'pc', 'phone', 'camera', 'nvr', 'tv', 'speaker', 'printer', 'nas', 'server', 'iot', 'other'];

// What a device is: the word of its owner, then what the device says of itself (LoopMaxPro,
// services, SSDP), its name, its maker, a random MAC. '' when nothing says it.
function typeOf(h, gateway) {
  if (h.type && TYPES[h.type]) return h.type;
  if (h.self) return FREE ? 'loopmax' : 'loopmaxpro';
  if (h.loopmax) return h.loopmax.pro ? 'loopmaxpro' : 'loopmax';
  if (h.system) return h.system === 'pro' ? 'loopmaxpro' : 'loopmax';
  const s = h.services || [];
  const u = h.upnp || [];
  const words = [h.alias, h.name, h.server].filter(Boolean).join(' ');
  const by = MAKER_TYPE[maker(h.mac)];
  const cast = s.includes('cast') || s.includes('airplay') || u.includes('dial');
  if (/^loopmax-[0-9a-f]{4}$/i.test(h.name || '')) return 'loopmax1';
  if (h.ip === gateway) return 'router';
  if (s.includes('printer')) return 'printer';
  // Cameras and recorders: their names, their typical ports (the video, the port of the
  // platform; 9010 and 9020 of a Hikvision recorder), their maker.
  const p = h.ports || [];
  const video = !s.includes('files') && (p.includes(554) || p.includes(37777) || p.includes(34567) || by === 'camera');
  if (/\b(i?DS-[789]\d|[NDX]VR)/i.test(words) || (video && (p.includes(9010) || p.includes(9020)))) return 'nvr';
  if (/\b(DS-2C|DS-2D|IPC)/i.test(words) || video) return 'camera';
  if (s.includes('homeassistant') || s.includes('mqtt')) return 'server';
  if (by === 'speaker' && !cast) return 'speaker';
  if (cast || u.includes('media') || by === 'tv' || /chromecast|roku|bravia|webos|tizen|android ?tv|fire ?tv|apple ?tv/i.test(words)) {
    return 'tv';
  }
  if (s.includes('files') || by === 'nas') return 'nas';
  // A router that is not the gateway: an access point, a node of a mesh, a router inside.
  if (u.includes('igd') || u.includes('wps') || by === 'net') return 'ap';
  if (h.pc || /^(desktop|laptop)-|microsoft-windows/i.test(words) || by === 'pc') return 'pc';
  if (s.includes('esphome') || s.includes('homekit') || by === 'iot') return 'iot';
  if (by === 'phone' || isRandom(h.mac) || /iphone|ipad|android|galaxy/i.test(words)) return 'phone';
  if (by === 'printer') return 'printer';
  if (by === 'server' || s.includes('ssh')) return 'server';
  return '';
}

// A refresh while the component lives: now, then every ms. Out of sight (a tab behind, the
// screen off, another app) it asks nothing, and back in sight it asks at once: a page nobody
// looks at does not keep the board and the network busy.
function usePoll(ctx, fn, ms, deps) {
  const { useEffect, useRef } = ctx;
  const keep = useRef(fn);
  keep.current = fn;
  useEffect(() => {
    let live = true;
    const run = () => { if (live && !document.hidden) keep.current(); };
    run();
    const timer = ms ? setInterval(run, ms) : null;
    document.addEventListener('visibilitychange', run);
    return () => {
      live = false;
      if (timer) clearInterval(timer);
      document.removeEventListener('visibilitychange', run);
    };
  }, deps);
}

// A tool asked from the card of a device or from the bar of the home: the Tools tab starts it.
let pendingTool = null;
function openTool(kind, target) {
  pendingTool = { kind, target };
  location.hash = '#/m/expnet/tools';
}

let gid = 0;

// One clock of frames for the whole page: who draws by itself joins it, and leaves when done.
const frames = new Set();
let framesOn = false;
function frameLoop(now) {
  frames.forEach((fn) => fn(now));
  if (frames.size) requestAnimationFrame(frameLoop);
  else framesOn = false;
}
function onFrame(fn) {
  frames.add(fn);
  if (!framesOn) {
    framesOn = true;
    requestAnimationFrame(frameLoop);
  }
  return () => frames.delete(fn);
}

// ---------------------------------------------------------------- the data apart from the drawing

// Two engines, apart (on one engine the graphs danced when the numbers
// changed). The answers of the board for what goes on go into a feed, and nothing is drawn again
// for them: the graphs take them at their frame, a tile or a needle when its own number changes.
// Drawing the whole page again at every answer costs frames on a phone, and the graphs jerk right
// when the numbers change.
function useFeed(ctx, cmd, paceOf) {
  const { useRef, useEffect } = ctx;
  const keep = useRef(null);
  if (!keep.current) keep.current = { data: null, version: 0, at: 0, subs: new Set() };
  const feed = keep.current;
  const pace = useRef(paceOf);
  pace.current = paceOf;
  useEffect(() => {
    let on = true;
    let timer = null;
    let asking = false;
    const ask = async () => {
      timer = null;
      // Out of sight nothing is asked: the board stops its pings and its questions to the router
      // 15 seconds later. Back in sight, at once.
      if (!on || asking || document.hidden) return;
      asking = true;
      const asked = performance.now();
      const r = await ctx.read(cmd, {});
      asking = false;
      if (!on) return;
      if (r.ok && r.result) {
        feed.data = r.result;
        feed.version += 1;
        feed.at = performance.now();
        feed.subs.forEach((fn) => fn(feed.data));
      }
      timer = setTimeout(ask, Math.max(50, pace.current(feed.data) - (performance.now() - asked)));
    };
    const back = () => { if (!timer) ask(); };
    document.addEventListener('visibilitychange', back);
    ask();
    return () => {
      on = false;
      if (timer) clearTimeout(timer);
      document.removeEventListener('visibilitychange', back);
    };
  }, [cmd]);
  return feed;
}

// A part of the feed in a component: drawn again only when that part changes.
function useFeedValue(ctx, feed, pick) {
  const { useState, useEffect, useRef } = ctx;
  const keep = useRef(pick);
  keep.current = pick;
  const [value, setValue] = useState(() => (feed && feed.data ? pick(feed.data) : undefined));
  useEffect(() => {
    if (!feed) return undefined;
    const take = (d) => {
      const v = keep.current(d);
      setValue((old) => (JSON.stringify(old) === JSON.stringify(v) ? old : v));
    };
    feed.subs.add(take);
    if (feed.data) take(feed.data);
    return () => { feed.subs.delete(take); };
  }, [feed]);
  return value;
}

// The moments of the samples of a series that goes on, on the clock of the browser: from the
// number of the newest (seq), the pace of the board (every), how long ago the newest was due (age)
// and when the answer arrived (at). As a needle follows its value, the anchor and the delay of the
// drawing reach where they should be a little at every frame, never faster than 40% of the time
// that passes: the graph may slow down for a moment, it never stops and never goes back. The delay
// is how late a sample is known here, plus two steps: the last piece of the curve, the one the
// next sample changes, stays out of sight.
function newTimeline() {
  return { s0: null, t0: 0, goal: 0, last: null, late: 0, delay: 0, delayGoal: 0, at: 0 };
}

function timelineTake(st, seq, every, age, at) {
  if (typeof seq !== 'number' || !every || seq === st.last) return;
  const a = typeof age === 'number' && isFinite(age) ? Math.max(0, age) : every;
  const due = at - a;
  // The oldest of the samples that arrive together waited the most.
  const missed = st.last === null || seq < st.last ? 0 : Math.max(0, seq - st.last - 1);
  st.late = Math.max(a + missed * every, st.late * 0.99);
  st.delayGoal = st.late + 2 * every + 150;
  const goal = st.s0 === null ? due : due - (seq - st.s0) * every;
  if (st.s0 === null || seq < st.last || Math.abs(goal - st.t0) > every * 8 + 3000) {
    st.s0 = seq;
    st.t0 = st.goal = due;
    st.delay = st.delayGoal;
  } else {
    st.goal = goal;
  }
  st.last = seq;
}

function timelineFrame(st, now) {
  const dt = st.at ? Math.min(100, now - st.at) : 16;
  st.at = now;
  const k = 1 - Math.exp(-dt * 0.0012);
  const most = dt * 0.4;
  const toward = (from, to) => from + Math.max(-most, Math.min(most, (to - from) * k));
  st.t0 = toward(st.t0, st.goal);
  st.delay = toward(st.delay, st.delayGoal);
}

// A bound of a scale that does not breathe: it goes out as far as the values push it, gliding,
// and comes back only when they stayed inside for 10 seconds. top: the upper bound.
function newBound() {
  return { goal: null, v: null, since: 0 };
}

function boundFrame(b, want, top, now, dt) {
  if (b.goal === null || (top ? want > b.goal : want < b.goal)) {
    b.goal = want;
    b.since = 0;
  } else if (want !== b.goal) {
    if (!b.since) {
      b.since = now;
    } else if (now - b.since > 10000) {
      b.goal = want;
      b.since = 0;
    }
  } else {
    b.since = 0;
  }
  b.v = b.v === null ? b.goal : b.v + (b.goal - b.v) * (1 - Math.exp(-dt * 2.5));
  return b.v;
}

function lastOf(vals) {
  for (let i = (vals || []).length - 1; i >= 0; --i) if (isNum(vals[i])) return vals[i];
  return null;
}

// A number that glides to its value instead of changing at once; negative too (the signal). Its
// text is written only here, never by Preact: a text of Preact in the same element comes back
// next to it when the page is drawn again, and two numbers show for a frame.
const finite = (v) => typeof v === 'number' && isFinite(v);
function Num({ ctx, value, digits = 0 }) {
  const { html, useRef, useEffect } = ctx;
  const st = useRef(null);
  if (!st.current) {
    const s = { x: value, target: value, stop: null, last: 0, el: null };
    s.show = () => {
      if (!s.el) return;
      const p = Math.pow(10, s.digits);
      s.el.textContent = finite(s.x) ? num(Math.round(s.x * p) / p || 0, s.lang, s.digits) : '–';
    };
    s.bind = (e) => { s.el = e; s.show(); };
    st.current = s;
  }
  const s = st.current;
  s.lang = ctx.lang();
  s.digits = digits;
  useEffect(() => {
    s.target = value;
    if (!finite(value) || !finite(s.x)) {
      if (s.stop) s.stop();
      s.stop = null;
      s.x = value;
      s.show();
      return;
    }
    if (s.stop) return;
    s.last = 0;
    s.stop = onFrame((now) => {
      const dt = s.last ? Math.min(0.1, (now - s.last) / 1000) : 0.016;
      s.last = now;
      s.x += (s.target - s.x) * (1 - Math.exp(-dt * 6));
      const done = Math.abs(s.target - s.x) < Math.pow(10, -s.digits) / 2;
      if (done) s.x = s.target;
      s.show();
      if (done) {
        s.stop();
        s.stop = null;
      }
    });
  }, [value]);
  useEffect(() => () => { if (s.stop) s.stop(); }, []);
  return html`<span ref=${s.bind}></span>`;
}

// ---------------------------------------------------------------- the pieces of the monitor

function Panel({ ctx, title, tone, right, children, data }) {
  const { html } = ctx;
  return html`<section class="dbx-p" data-db-panel=${data || ''}>
    ${title && html`<div class="dbx-ph"><span class="dbx-pt" style=${tone ? `--tone:${tone}` : ''}><i></i>${title}</span>
      ${right && html`<span class="dbx-pr">${right}</span>`}</div>`}
    ${children}
  </section>`;
}

function Note({ ctx, kind = 'info', children }) {
  return ctx.html`<div class=${'dbx-note ' + kind}>${children}</div>`;
}

// A small area under a tile: the last values, the newest on the right. With src (a feed, and
// what to take from it: values, seq, every, age, slots) it goes on with the clock of its samples:
// the line is made once for every answer, in the units of the data, and the frames only move and
// stretch it, 60 times a second; nothing else is drawn again for it. Without src it is drawn once.
function Mini({ ctx, values, color = C.cy, src }) {
  const { html, useMemo, useRef, useEffect } = ctx;
  const id = useMemo(() => 'dbm' + (++gid), []);
  const line = useRef(null);
  const area = useRef(null);
  const moving = useRef(null);
  const keep = useRef(src);
  keep.current = src;
  const live = !!src;
  const shape = (pts, bottom) => {
    if (pts.length < 2) return ['', ''];
    const l = pts.map((p, i) => (i ? 'L' : 'M') + p[0].toFixed(1) + ' ' + p[1].toFixed(2)).join(' ');
    return [l, `${l} L${pts[pts.length - 1][0].toFixed(1)} ${bottom} L${pts[0][0].toFixed(1)} ${bottom} Z`];
  };
  useEffect(() => {
    if (!live) return undefined;
    const st = { tl: newTimeline(), version: -1, vals: [], seq: 0, every: 0, slots: 0, drawn: true,
      lo: newBound(), hi: newBound(), wantLo: 0, wantHi: 1, last: 0 };
    return onFrame((now) => {
      const s = keep.current;
      if (!s || !moving.current) return;
      const f = s.feed;
      if (f.data && f.version !== st.version) {
        st.version = f.version;
        const p = s.pick(f.data);
        st.vals = p && p.values ? p.values : [];
        if (p) {
          st.seq = p.seq;
          st.every = p.every;
          st.slots = p.slots || 0;
          timelineTake(st.tl, p.seq, p.every, p.age, f.at);
        }
        st.drawn = false;
      }
      timelineFrame(st.tl, now);
      const dt = st.last ? Math.min(0.1, (now - st.last) / 1000) : 0.016;
      st.last = now;
      const n = st.vals.length;
      if (n < 2 || !st.every) {
        line.current.setAttribute('d', '');
        area.current.setAttribute('d', '');
        return;
      }
      // The window of all the places, not of the samples there are: it does not shrink while the
      // board fills it.
      const win = (Math.max(st.slots, n, 11) - 1) * st.every;
      const left = now - st.tl.delay - win;
      if (!st.drawn) {
        // Once for every answer: x in ms from the anchor, y the values.
        st.drawn = true;
        const pts = [];
        let lo = Infinity;
        let hi = -Infinity;
        for (let i = 0; i < n; ++i) {
          const v = st.vals[i];
          if (!isNum(v)) continue;
          const x = (st.seq - (n - 1 - i) - st.tl.s0) * st.every;
          pts.push([x, v]);
          const at = st.tl.t0 + x;
          if (at >= left && at <= left + win) {
            lo = Math.min(lo, v);
            hi = Math.max(hi, v);
          }
        }
        if (hi !== -Infinity) {
          st.wantLo = lo;
          st.wantHi = hi;
        }
        const span = (st.wantHi - st.wantLo) || st.wantHi || 1;
        const [l, a] = shape(pts, (st.wantLo - span).toFixed(2));
        line.current.setAttribute('d', l);
        area.current.setAttribute('d', a);
      }
      const b0 = boundFrame(st.lo, st.wantLo, false, now, dt);
      const b1 = boundFrame(st.hi, st.wantHi, true, now, dt);
      const span = b1 - b0 || b1 || 1;
      const sx = 100 / win;
      const sy = -24 / span;
      moving.current.setAttribute('transform',
        `matrix(${sx.toFixed(7)} 0 0 ${sy.toFixed(5)} ${((st.tl.t0 - left) * sx).toFixed(3)} ${(27 - sy * b0).toFixed(3)})`);
    });
  }, [live]);
  if (live) {
    // Upside down in the units of the data: the light of the area goes from the line down.
    return html`<svg viewBox="0 0 100 32" preserveAspectRatio="none" aria-hidden="true">
      <defs><linearGradient id=${id} x1="0" y1="1" x2="0" y2="0"><stop offset="0" stop-color=${color} stop-opacity=".35"/>
        <stop offset="1" stop-color=${color} stop-opacity="0"/></linearGradient></defs>
      <g ref=${moving}>
        <path ref=${area} fill=${`url(#${id})`}/>
        <path ref=${line} fill="none" stroke=${color} stroke-width="1.4" vector-effect="non-scaling-stroke"/>
      </g>
    </svg>`;
  }
  const vals = (values || []).map((v) => (isNum(v) ? v : null));
  const got = vals.filter((v) => v !== null);
  if (got.length < 2) return null;
  const max = Math.max(...got) || 1;
  const min = Math.min(...got);
  const span = max - min || max || 1;
  const n = vals.length;
  const pts = [];
  vals.forEach((v, i) => { if (v !== null) pts.push([(i / (n - 1)) * 100, 30 - ((v - min) / span) * 24 - 3]); });
  const [l, a] = shape(pts, 32);
  return html`<svg viewBox="0 0 100 32" preserveAspectRatio="none" aria-hidden="true">
    <defs><linearGradient id=${id} x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color=${color} stop-opacity=".35"/>
      <stop offset="1" stop-color=${color} stop-opacity="0"/></linearGradient></defs>
    <path d=${a} fill=${`url(#${id})`}/>
    <path d=${l} fill="none" stroke=${color} stroke-width="1.4" vector-effect="non-scaling-stroke"/>
  </svg>`;
}

// A metric: its name, its value (a number glides to it), a line under, a small graph: still
// with spark, going on with src.
function Tile({ ctx, label, value, digits = 0, unit, sub, tone = C.cy, spark, src, data }) {
  const { html } = ctx;
  return html`<div class="dbx-tile" style=${`--tone:${tone}`} data-db-tile=${data || ''}>
    <div class="dbx-tl">${label}</div>
    <div class="dbx-tv">${typeof value === 'number' ? html`<${Num} ctx=${ctx} value=${value} digits=${digits}/>` : value}${unit && html`<small>${unit}</small>`}</div>
    ${sub && html`<div class="dbx-ts">${sub}</div>`}
    ${(spark || src) && html`<${Mini} ctx=${ctx} values=${spark} color=${tone} src=${src}/>`}
  </div>`;
}

// A tile that follows the feed: drawn again only when its number, unit, line or colour change
// (show); its small graph takes its samples by itself (spark).
function LiveTile({ ctx, feed, show, spark, label, digits = 0, data }) {
  const v = useFeedValue(ctx, feed, show) || {};
  const keepSpark = ctx.useRef(spark);
  keepSpark.current = spark;
  const src = ctx.useMemo(() => ({ feed, pick: (d) => (keepSpark.current ? keepSpark.current(d) : null) }), [feed]);
  return ctx.html`<${Tile} ctx=${ctx} label=${label} value=${v.value === undefined ? '–' : v.value} digits=${digits}
    unit=${v.unit} sub=${v.sub} tone=${v.tone || C.cy} src=${spark ? src : null} data=${data}/>`;
}

// A needle that follows the feed: drawn again only when its value or its line change.
function LiveGauge({ ctx, feed, show, ...rest }) {
  const v = useFeedValue(ctx, feed, show) || {};
  return ctx.html`<${Gauge} ctx=${ctx} ...${rest} value=${v.value === undefined ? null : v.value} sub=${v.sub || ''}/>`;
}

// Through every point, without corners: a curve of Catmull-Rom, as cubic pieces.
function curvePath(run) {
  const p = (q) => q[0].toFixed(2) + ' ' + Math.max(-2, Math.min(102, q[1])).toFixed(2);
  let d = 'M' + p(run[0]);
  for (let i = 0; i < run.length - 1; ++i) {
    const a = run[i - 1] || run[i];
    const b = run[i];
    const c = run[i + 1];
    const e = run[i + 2] || c;
    d += ' C' + p([b[0] + (c[0] - a[0]) / 6, b[1] + (c[1] - a[1]) / 6]) + ' ' +
      p([c[0] - (e[0] - b[0]) / 6, c[1] - (e[1] - b[1]) / 6]) + ' ' + p(c);
  }
  return d;
}

// The same curve in the units of the data, x in ms and y the values, as the graphs that go on
// make it: a curve of Catmull-Rom stays the same under the transform that moves it.
function curveData(run) {
  const p = (q) => q[0].toFixed(1) + ' ' + q[1].toFixed(3);
  let d = 'M' + p(run[0]);
  for (let i = 0; i < run.length - 1; ++i) {
    const a = run[i - 1] || run[i];
    const b = run[i];
    const c = run[i + 1];
    const e = run[i + 2] || c;
    d += ' C' + p([b[0] + (c[0] - a[0]) / 6, b[1] + (c[1] - a[1]) / 6]) + ' ' +
      p([c[0] - (e[0] - b[0]) / 6, c[1] - (e[1] - b[1]) / 6]) + ' ' + p(c);
  }
  return d;
}

// A graph of a panel: curves over a grid, the first one filled; a value not there yet does not
// break the line, a negative one is a gap. Its height is fixed and its lines stretch with the
// width; the labels are HTML, of one size. With src (a feed, and what to take from it: the values
// of each series, seq, every, age, slots) it goes on with the clock of its samples: each one has
// its moment, the frames draw them 60 times a second a little behind the clock, the scale does not
// breathe, and the page is not drawn again for it. Without src it is drawn once, from the values
// of the series.
function Graph({ ctx, series, height = 130, unit = '', fixedMax, slots, src, digits }) {
  const { html, useMemo, useRef, useEffect } = ctx;
  const lang = ctx.lang();
  const id = useMemo(() => 'dbg' + (++gid), []);
  const live = !!src;
  const keep = useRef(src);
  keep.current = src;
  const lines = useRef([]);
  const fill = useRef(null);
  const labels = useRef([]);
  const grids = useRef([]);
  const lasts = useRef([]);
  const moving = useRef(null);
  const places = digits !== undefined ? digits : unit === 'Mbit/s' ? 1 : 0;
  const grid = [0, 1 / 3, 2 / 3, 1];
  const yOf = (v, max) => 100 - (Math.min(v, max) / max) * 94;
  useEffect(() => {
    if (!live) return undefined;
    const st = { tl: newTimeline(), version: -1, sets: [], seq: 0, every: 0, slots: 0, drawn: true, top: 0,
      bound: newBound(), last: 0 };
    const clear = () => {
      lines.current.forEach((el) => el && el.setAttribute('d', 'M0 0'));
      if (fill.current) fill.current.setAttribute('d', 'M0 0');
    };
    return onFrame((now) => {
      const s = keep.current;
      if (!s || !moving.current) return;
      const f = s.feed;
      if (f.data && f.version !== st.version) {
        st.version = f.version;
        const p = s.pick(f.data);
        st.sets = p && p.values ? p.values : [];
        if (p) {
          st.seq = p.seq;
          st.every = p.every;
          st.slots = p.slots || 0;
          timelineTake(st.tl, p.seq, p.every, p.age, f.at);
        }
        // The numbers of the legend change only when the samples do.
        st.sets.forEach((vals, k) => {
          const el = lasts.current[k];
          if (el) el.textContent = num(lastOf(vals), lang, places);
        });
        st.drawn = false;
      }
      timelineFrame(st.tl, now);
      const dt = st.last ? Math.min(0.1, (now - st.last) / 1000) : 0.016;
      st.last = now;
      if (!st.every || !st.sets.length) {
        clear();
        return;
      }
      const n = Math.max(st.slots, ...st.sets.map((v) => v.length), 2);
      const win = (n - 1) * st.every;
      const left = now - st.tl.delay - win;
      if (!st.drawn) {
        // Once for every answer: the curves in the units of the data, x in ms from the anchor.
        st.drawn = true;
        let top = 0;
        const half = win * 0.004;
        st.sets.forEach((values, k) => {
          const len = values.length;
          const runs = [];
          let run = [];
          for (let i = 0; i < len; ++i) {
            const v = values[i];
            // Not answered yet: the line goes on. A ping lost (-1) is a gap.
            if (v === null || v === undefined) continue;
            if (!isNum(v)) {
              if (run.length) runs.push(run);
              run = [];
              continue;
            }
            const x = (st.seq - (len - 1 - i) - st.tl.s0) * st.every;
            run.push([x, v]);
            const at = st.tl.t0 + x;
            if (at >= left && at <= left + win) top = Math.max(top, v);
          }
          if (run.length) runs.push(run);
          const el = lines.current[k];
          if (el) {
            el.setAttribute('d', runs.map((r) => (r.length > 1 ? curveData(r)
              : `M${(r[0][0] - half).toFixed(1)} ${r[0][1]} L${(r[0][0] + half).toFixed(1)} ${r[0][1]}`)).join(' ') || 'M0 0');
          }
          if (k === 0 && fill.current) {
            fill.current.setAttribute('d', runs.filter((r) => r.length > 1).map((r) =>
              `${curveData(r)} L${r[r.length - 1][0].toFixed(1)} 0 L${r[0][0].toFixed(1)} 0 Z`).join(' ') || 'M0 0');
          }
        });
        st.top = top;
      }
      // The scale: up at once when a value goes over it, gliding; down only after 10 seconds.
      const max = boundFrame(st.bound, fixedMax || niceMax(Math.max(st.top, 1)), true, now, dt);
      const sx = 100 / win;
      const sy = -94 / max;
      moving.current.setAttribute('transform',
        `matrix(${sx.toFixed(7)} 0 0 ${sy.toFixed(5)} ${((st.tl.t0 - left) * sx).toFixed(3)} 100)`);
      grid.forEach((f2, i) => {
        const label = labels.current[i];
        if (!label) return;
        const text = num(max * f2, lang, max < 10 ? 1 : 0);
        if (label.textContent !== text) label.textContent = text;
      });
    });
  }, [live]);
  // Drawn once, when the series does not go on with a clock.
  let still = null;
  const max = fixedMax || (live ? 1 : niceMax(Math.max(1, ...series.flatMap((s) => (s.values || []).filter(isNum)))));
  if (!live) {
    const n = Math.max(slots || 0, ...series.map((s) => s.values.length), 2);
    still = series.map((s) => {
      const out = [];
      let run = [];
      s.values.forEach((v, i) => {
        if (!isNum(v)) { if (run.length) out.push(run); run = []; return; }
        run.push([((n - s.values.length + i) / (n - 1)) * 100, yOf(v, max)]);
      });
      if (run.length) out.push(run);
      return out;
    });
  }
  return html`<div>
    <div class="dbx-gw" style=${`height:${height}px`}>
      <svg viewBox="0 0 100 100" preserveAspectRatio="none" aria-hidden="true">
        <defs>
          <linearGradient id=${id} x1="0" y1=${live ? 1 : 0} x2="0" y2=${live ? 0 : 1}><stop offset="0" stop-color=${series[0] ? series[0].color : C.cy} stop-opacity=".34"/>
            <stop offset="1" stop-color=${series[0] ? series[0].color : C.cy} stop-opacity="0"/></linearGradient>
          <clipPath id=${id + 'c'}><rect x="0" y="-5" width="100" height="110"/></clipPath>
        </defs>
        ${grid.map((f, i) => html`<line key=${'g' + f} ref=${(e) => { grids.current[i] = e; }} class="gl" x1="0" x2="100"
          y1=${yOf(f, 1)} y2=${yOf(f, 1)}/>`)}
        <g clip-path=${`url(#${id}c)`}>
          ${live && html`<g ref=${moving}>
            <path ref=${fill} fill=${`url(#${id})`}/>
            ${series.map((s, k) => html`<path key=${'s' + k} ref=${(e) => { lines.current[k] = e; }} class="line" stroke=${s.color}/>`)}
          </g>`}
          ${!live && still.map((runs, k) => runs.map((run, i) => html`<g key=${k + '-' + i}>
            ${k === 0 && run.length > 1 && html`<path d=${`${curvePath(run)} L${run[run.length - 1][0].toFixed(2)} 100 L${run[0][0].toFixed(2)} 100 Z`} fill=${`url(#${id})`}/>`}
            <path class="line" d=${run.length > 1 ? curvePath(run) : `M${Math.max(0, run[0][0] - 0.5)} ${run[0][1]} L${run[0][0] + 0.5} ${run[0][1]}`} stroke=${series[k].color}/>
          </g>`))}
        </g>
      </svg>
      ${grid.map((f, i) => (live
        // With the clock the frames write the labels: no text of Preact in them (see Num).
        ? html`<span key=${'L' + f} class="dbx-yl" style=${`top:${yOf(f, 1)}%`} ref=${(e) => { labels.current[i] = e; }}></span>`
        : html`<span key=${'l' + f} class="dbx-yl" style=${`top:${yOf(max * f, max)}%`}>${num(max * f, lang, max < 10 ? 1 : 0)}</span>`))}
    </div>
    <div class="dbx-legend">${series.map((s, k) => html`<span key=${s.label}><i style=${`background:${s.color}`}></i>${s.label}
      ${' '}${live
        ? html`<b style="color:var(--text)" ref=${(e) => { lasts.current[k] = e; }}></b>`
        : html`<b style="color:var(--text)">${num(lastOf(s.values), lang, places)}</b>`} ${unit}</span>`)}</div>
  </div>`;
}

function UpBars({ ctx, values }) {
  const { html } = ctx;
  return html`<svg class="dbx-graph" style="height:18px" viewBox=${`0 0 ${values.length} 10`} preserveAspectRatio="none" aria-hidden="true">
    ${values.map((v, i) => html`<rect key=${i} x=${i + 0.08} y="0" width="0.84" height="10"
      fill=${v === null ? '#16243a' : v >= 990 ? C.gr : v >= 900 ? C.ye : C.re}/>`)}
  </svg>`;
}

// A needle on a half circle, 0 to max; the value under it. warn and bad colour the arc; with
// reverse the low values are the bad ones. The needle follows its value as a spring, 60 frames a
// second, a little beyond it and back, as an old instrument does; each needle has a pace of its
// own, from its name, so that they never move together. The frames draw the needle, the arc and
// the number themselves: the page is not drawn again 60 times a second.
function Gauge({ ctx, value, max, label, unit, digits = 0, warn, bad, sub, reverse = false, data }) {
  const { html, useRef, useEffect, useMemo } = ctx;
  const lang = ctx.lang();
  const v = typeof value === 'number' && isFinite(value) ? value : null;
  const top = max > 0 ? max : 1;
  const needle = useRef(null);
  const arcEl = useRef(null);
  const numEl = useRef(null);
  const anim = useRef({ x: 0, vel: 0, target: 0, raf: 0, last: 0, shown: false });
  const stiff = useMemo(() => {
    let h = 7;
    for (const c of String(label)) h = (h * 31 + c.charCodeAt(0)) >>> 0;
    return 15 + (h % 11);
  }, [label]);
  const point = (x, r) => {
    const a = Math.PI * (1 - x);
    return [100 + r * Math.cos(a), 100 - r * Math.sin(a)];
  };
  const arc = (to) => {
    const [x1, y1] = point(0, 80);
    const [x2, y2] = point(to, 80);
    return `M${x1.toFixed(1)} ${y1.toFixed(1)} A80 80 0 0 1 ${x2.toFixed(1)} ${y2.toFixed(1)}`;
  };
  const level = v === null ? '' : reverse
    ? (bad !== undefined && v <= bad ? 'bad' : warn !== undefined && v <= warn ? 'mid' : 'ok')
    : (bad !== undefined && v >= bad ? 'bad' : warn !== undefined && v >= warn ? 'mid' : warn !== undefined ? 'ok' : '');
  const draw = (x) => {
    const f = Math.max(0, Math.min(1.04, x / top));
    if (needle.current) needle.current.setAttribute('transform', `rotate(${(-90 + f * 180).toFixed(2)} 100 100)`);
    if (arcEl.current) arcEl.current.setAttribute('d', f > 0.004 ? arc(Math.min(1, f)) : 'M0 0');
    if (numEl.current) numEl.current.textContent = v === null ? '–' : num(Math.max(0, x), lang, digits);
  };
  useEffect(() => {
    const a = anim.current;
    a.target = v === null ? 0 : v;
    const damp = 2 * 0.62 * Math.sqrt(stiff);   // a little beyond the value, and back
    const frame = (now) => {
      const dt = Math.min(0.05, a.last ? (now - a.last) / 1000 : 0.016);
      a.last = now;
      a.vel += (stiff * (a.target - a.x) - damp * a.vel) * dt;
      a.x += a.vel * dt;
      draw(a.x);
      if (Math.abs(a.target - a.x) < top * 0.0004 && Math.abs(a.vel) < top * 0.0004) {
        a.x = a.target;
        a.vel = 0;
        a.raf = 0;
        a.last = 0;
        draw(a.x);
        return;
      }
      a.raf = requestAnimationFrame(frame);
    };
    if (!a.raf) a.raf = requestAnimationFrame(frame);
    if (!a.shown) {
      a.shown = true;
      draw(a.x);
    }
  }, [v, top, stiff]);
  useEffect(() => () => { if (anim.current.raf) cancelAnimationFrame(anim.current.raf); }, []);
  const ticks = [];
  for (let i = 0; i <= 10; ++i) {
    const [a, b] = point(i / 10, 64);
    const [c, d] = point(i / 10, i % 5 ? 68 : 71);
    ticks.push(html`<line key=${'t' + i} class="dbx-g-tick" x1=${a.toFixed(1)} y1=${b.toFixed(1)} x2=${c.toFixed(1)} y2=${d.toFixed(1)}/>`);
  }
  const [lx, ly] = point(0, 54);
  const [rx, ry] = point(1, 54);
  const [mx, my] = point(0.5, 54);
  const short = (x) => (x >= 1000 ? num(x / 1000, lang, x % 1000 ? 1 : 0) + 'k' : num(x, lang));
  return html`<div class="dbx-gauge" data-db-gauge=${data || ''}>
    <svg viewBox="0 0 200 136" role="img" aria-label=${label + ' ' + (v === null ? '' : num(v, lang, digits)) + ' ' + unit}>
      <path class="dbx-g-track" d=${arc(1)}/>
      <path ref=${arcEl} class=${'dbx-g-val ' + level}/>
      ${ticks}
      <text class="dbx-g-tl" x=${lx.toFixed(1)} y=${(ly + 12).toFixed(1)}>0</text>
      <text class="dbx-g-tl" x=${mx.toFixed(1)} y=${(my + 4).toFixed(1)}>${short(top / 2)}</text>
      <text class="dbx-g-tl" x=${rx.toFixed(1)} y=${(ry + 12).toFixed(1)}>${short(top)}</text>
      <g ref=${needle} class="dbx-g-needle" transform="rotate(-90 100 100)">
        <line x1="100" y1="100" x2="100" y2="36"/><circle cx="100" cy="100" r="7"/>
      </g>
      <circle class="dbx-g-hub" cx="100" cy="100" r="3"/>
      <text ref=${numEl} class="dbx-g-num" x="100" y="131"></text>
    </svg>
    <div class="dbx-g-label">${label} ${unit && html`<small>${unit}</small>`}</div>
    ${sub && html`<div class="dbx-g-sub">${sub}</div>`}
  </div>`;
}

// ---------------------------------------------------------------- ExpNetPro

// What starts is of ExpNetPro: a device of SmartiLab, ready, sold in the shop.
const SHOP = 'https://smartilab.it/shop/';

function ProGate({ ctx, onClose }) {
  const { html, ui, t } = ctx;
  return html`<${ui.Modal} title=${t('mod.expnet.pro.title')} icon="lock" onClose=${onClose}
    footer=${html`<${ui.Btn} onClick=${onClose}>${t('mod.expnet.pro.close')}<//>
      <a class="btn primary" href=${SHOP} target="_blank" rel="noopener" data-db="shop">${t('mod.expnet.pro.shop')} ↗</a>`}>
    <div class="dbx-proc" data-db="pro-gate">
      <p>${t('mod.expnet.pro.text')}</p>
      <ul>${['tools', 'speed', 'mqtt', 'ep', 'bot'].map((k) => html`<li key=${k}>${t('mod.expnet.pro.li.' + k)}</li>`)}</ul>
      <p class="dbx-help">${t('mod.expnet.pro.text2')}</p>
    </div>
  <//>`;
}

function ProBanner({ ctx }) {
  const { html, t } = ctx;
  return html`<div class="dbx-pro" data-db="pro-banner"><b>PRO</b><span>${t('mod.expnet.pro.banner')}</span>
    <a class="btn sm" href=${SHOP} target="_blank" rel="noopener">${t('mod.expnet.pro.shop')} ↗</a></div>`;
}

// ---------------------------------------------------------------- home

function Home({ ctx }) {
  const { html, ui, t, useState, useMemo } = ctx;
  const lang = ctx.lang();
  const doc = ctx.useDoc() || {};
  const net = ctx.useSysDoc('sys.net', 20000);
  const [hosts, setHosts] = useState(null);
  const [nowUnix, setNow] = useState(0);
  const [events, setEvents] = useState([]);
  const [open, setOpen] = useState(null);
  const [filter, setFilter] = useState('');
  const [target, setTarget] = useState('');
  const [group, setGroup] = useState('');
  const [makersAt, setMakersAt] = useState(0);
  ctx.useEffect(() => { loadMakers(ctx).then((n) => { if (n) setMakersAt(n); }); }, []);
  const scan = doc.scan || {};
  const sta = (net && net.sta) || {};
  const gateway = sta.gateway || '';
  const load = async () => {
    let all = [];
    let from = 0;
    for (let page = 0; page < 40 && from !== undefined; ++page) {
      const r = await ctx.read('hosts', { from });
      if (!r.ok || !r.result) break;
      all = all.concat(r.result.hosts || []);
      if (r.result.nowUnix) setNow(r.result.nowUnix);
      from = r.result.next;
    }
    setHosts(all);
  };
  usePoll(ctx, load, 60000, [scan.lastUnix, scan.state]);
  usePoll(ctx, async () => {
    const r = await ctx.read('events', {});
    if (r.ok && r.result) setEvents(r.result.events || []);
  }, 0, [doc.events, doc.lastEventUnix]);
  // What goes on, apart: the tiles take it, the page is not drawn again for it.
  const feed = useFeed(ctx, 'live', () => 2000);
  // What each device is, once for each list, and how many of each group.
  const typed = useMemo(() => (hosts || []).map((h) => Object.assign({}, h, { kind: typeOf(h, gateway) })), [hosts, gateway, makersAt]);
  const counts = useMemo(() => typed.reduce((n, h) => { n[TYPES[h.kind].g] = (n[TYPES[h.kind].g] || 0) + 1; return n; }, {}), [typed]);
  const typeName = (k) => t('mod.expnet.type.' + (k || 'unknown'));
  const shown = useMemo(() => {
    const f = filter.trim().toLowerCase();
    return typed.filter((h) => (!group || TYPES[h.kind].g === group) && (!f || [h.ip, h.mac, nameOf(h), makerOf(h),
      (h.services || []).join(' '), typeName(h.kind)].join(' ').toLowerCase().includes(f)))
      .sort((a, b) => (b.online ? 1 : 0) - (a.online ? 1 : 0));
  }, [typed, filter, group]);
  const services = doc.services || [];
  const pcs = doc.pcs || [];
  const down = services.filter((s) => s.known && !s.up);
  const bits = (m) => (m ? m.split('.').reduce((n, p) => n + (Number(p) >>> 0).toString(2).split('1').length - 1, 0) : '');
  const tool = (kind) => { const v = target.trim(); if (v) openTool(kind, v); };
  const tone = (ms, warn, bad) => (!isNum(ms) ? C.re : ms >= bad ? C.re : ms >= warn ? C.ye : C.gr);
  const line = (e) => {
    const w = html`<span class="k">${clock(e.unix, lang)}</span> `;
    switch (e.kind) {
      case 'down': return html`${w}<span class="r">✖ ${t('mod.expnet.ev.down')}</span> ${e.name}`;
      case 'up': return html`${w}<span class="g">✔ ${t('mod.expnet.ev.up')}</span> ${e.name}${e.minutes ? ' · ' + t('mod.expnet.ev.after', { n: e.minutes }) : ''}`;
      case 'new': return html`${w}<span class="y">▲ ${t('mod.expnet.ev.new')}</span> <span class="a">${e.ip}</span> ${e.mac ? '· ' + (isRandom(e.mac) ? t('mod.expnet.randomShort') : maker(e.mac) || e.mac) : ''}${e.name ? ' · ' + e.name : ''}`;
      case 'pcOn': return html`${w}<span class="g">⏻ ${t('mod.expnet.ev.pcOn')}</span> ${e.name}`;
      case 'pcOff': return html`${w}<span class="k">⏻ ${t('mod.expnet.ev.pcOff')}</span> ${e.name}`;
      case 'netDown': return html`${w}<span class="r">✖ ${t('mod.expnet.ev.netDown')}</span>`;
      case 'netUp': return html`${w}<span class="g">✔ ${t('mod.expnet.ev.netUp', { n: e.minutes || 0 })}</span>`;
      default: return html`${w}${e.kind}`;
    }
  };
  const notify = doc.notify || {};
  const running = scan.state === 'running';
  return html`<style>${STYLE}</style><div class="dbx">
    ${doc.online === false && html`<${Note} ctx=${ctx} kind="warn">${t('mod.expnet.offline')}<//>`}
    <${Panel} ctx=${ctx} title=${sta.ssid || t('mod.expnet.h.net')} tone=${net && net.internet ? C.gr : C.re}
      right=${html`<span class="dbx-pill"><span class=${'dbx-led ' + (running ? 'in blink' : 'ok')}></span>${running
        ? t('mod.expnet.h.scanning', { step: scan.step || '', at: scan.at || 0, of: scan.of || 0 })
        : scan.lastUnix ? t('mod.expnet.h.lastScan', { when: clock(scan.lastUnix, lang) }) : t('mod.expnet.h.noScan')}</span>`}>
      <div class="dbx-strip">
        <span><b>ip</b>${sta.ip || '–'}${sta.mask ? '/' + bits(sta.mask) : ''}</span>
        <span><b>gw</b>${gateway || '–'}</span>
        <span><b>dns</b>${sta.dns || '–'}</span>
        <span><b>ch</b>${sta.channel || '–'}</span>
        <span><b>mac</b>${sta.mac || '–'}</span>
      </div>
    <//>
    <div class="dbx-tiles">
      <${Tile} ctx=${ctx} data="online" label=${t('mod.expnet.t.online')} value=${scan.found || 0} tone=${C.cy}
        sub=${t('mod.expnet.t.known', { n: scan.known || 0 })}/>
      <${Tile} ctx=${ctx} data="new" label=${t('mod.expnet.t.new')} value=${scan.new || 0} tone=${scan.new ? C.ye : C.gr}
        sub=${t('mod.expnet.t.last24')}/>
      <${LiveTile} ctx=${ctx} feed=${feed} data="internet" label="Internet"
        show=${(d) => {
          const s = stats((d.netMs || []).slice(-10));
          const ok = s && isNum(s.avg);
          return { value: ok ? s.avg : net && net.internet === false ? 'off' : '–', unit: ok ? 'ms' : '',
            tone: net && net.internet ? tone(s && s.avg, 60, 120) : C.re, sub: s ? t('mod.expnet.t.lost', { n: s.lost, of: s.n }) : '' };
        }}
        spark=${(d) => ({ values: d.netMs, seq: d.seq, every: d.everyMs, age: d.ageMs, slots: d.slots })}/>
      <${LiveTile} ctx=${ctx} feed=${feed} data="router" label=${t('mod.expnet.router')}
        show=${(d) => {
          const s = stats((d.gwMs || []).slice(-10));
          return { value: s && isNum(s.avg) ? s.avg : '–', unit: 'ms', tone: tone(s && s.avg, 20, 40), sub: gateway };
        }}
        spark=${(d) => ({ values: d.gwMs, seq: d.seq, every: d.everyMs, age: d.ageMs, slots: d.slots })}/>
      <${LiveTile} ctx=${ctx} feed=${feed} data="signal" label=${t('mod.expnet.signal')}
        show=${(d) => {
          // The signal of the reading of every 2 seconds; the document of the network is older.
          const rssi = finite(d.rssi) && d.rssi < 0 ? d.rssi : sta.rssi;
          return { value: finite(rssi) ? rssi : '–', unit: 'dBm', tone: rssi >= -67 ? C.gr : rssi >= -75 ? C.ye : C.re,
            sub: 'ch ' + (d.channel || sta.channel || '–') };
        }}
        spark=${(d) => ({ values: (d.rssiHist || []).map((x) => x + 100), seq: d.seq, every: d.everyMs, age: d.ageMs, slots: d.slots })}/>
      <${LiveTile} ctx=${ctx} feed=${feed} data="traffic" label=${t('mod.expnet.t.traffic')} digits=${1}
        show=${(d) => {
          const r = d.router || {};
          const down = r.down || [];
          return { value: r.state === 'ok' && down.length ? down[down.length - 1] / 1000 : '–', unit: r.state === 'ok' ? 'Mbit/s' : '',
            tone: r.state === 'ok' ? C.vi : r.state === 'none' ? C.re : C.ye,
            sub: r.state === 'ok' ? '↓ ' + t('mod.expnet.download') : '● ' + (r.state === 'none' ? t('mod.expnet.t.noUpnp') : t('mod.expnet.routerLooking')) };
        }}
        spark=${(d) => {
          const r = d.router || {};
          return r.state === 'ok' ? { values: r.down, seq: r.seq, every: r.everyMs, age: r.ageMs, slots: r.slots } : null;
        }}/>
      <${Tile} ctx=${ctx} data="watched" label=${t('mod.expnet.t.watched')} value=${services.length - down.length + '/' + services.length}
        tone=${down.length ? C.re : C.gr} sub=${down.length ? down.map((s) => s.name).join(', ') : t('mod.expnet.t.allUp')}/>
    </div>
    <${Panel} ctx=${ctx} title=${t('mod.expnet.devicesTitle')} data="hosts"
      right=${html`<${ui.Btn} small kind="primary" icon="radar" busy=${running} onClick=${() => ctx.read('scan', {})} data-db="scan">${t('mod.expnet.search')}<//>`}>
      <div class="dbx-bar" style="margin-bottom:10px">
        <input class="dbx-in" value=${target} placeholder=${t('mod.expnet.hostOrName')} onInput=${(e) => setTarget(e.target.value)}
          onKeyDown=${(e) => { if (e.key === 'Enter') tool('ping'); }} aria-label=${t('mod.expnet.hostOrName')} data-db="target"/>
        <${ui.Btn} small icon="activity" onClick=${() => tool('ping')} data-db="go-ping">Ping<//>
        <${ui.Btn} small icon="db-route" onClick=${() => tool('trace')}>${t('mod.expnet.tool.trace')}<//>
        <${ui.Btn} small icon="search" onClick=${() => tool('ports')}>${t('mod.expnet.tool.ports')}<//>
        <${ui.Btn} small icon="globe" onClick=${() => tool('dns')}>DNS<//>
      </div>
      <div class="dbx-bar" style="margin-bottom:8px">
        <input class="dbx-in" value=${filter} placeholder=${t('mod.expnet.filter')} onInput=${(e) => setFilter(e.target.value)} aria-label=${t('mod.expnet.filter')}/>
      </div>
      ${typed.length > 0 && html`<div class="dbx-chips" style="margin:0 0 10px" role="group" aria-label=${t('mod.expnet.byType')}>
        <button type="button" class=${'dbx-chip' + (group ? '' : ' on')} onClick=${() => setGroup('')} data-db-group="all">
          ${t('mod.expnet.group.all')}<b>${typed.length}</b></button>
        ${GROUPS.filter((g) => counts[g.id] || group === g.id).map((g) => html`<button type="button" key=${g.id}
            class=${'dbx-chip' + (group === g.id ? ' on' : '')} onClick=${() => setGroup(group === g.id ? '' : g.id)} data-db-group=${g.id}>
          <span style=${group === g.id ? '' : 'color:' + g.tone}><${ui.Icon} name=${g.icon} size=${14}/></span>${t('mod.expnet.group.' + g.id)}<b>${counts[g.id] || 0}</b>
        </button>`)}
      </div>`}
      <div class="dbx-term">
        ${hosts === null && html`<div class="k">${t('mod.expnet.loading')}</div>`}
        ${hosts && !hosts.length && html`<div class="k">${running ? t('mod.expnet.looking') : t('mod.expnet.notYet')}</div>`}
        ${hosts && hosts.length > 0 && html`<div class="dbx-tbl" role="list">
          <div class="dbx-tr th"><span class="c1"></span><span class="c2">IP</span><span class="c6">${t('mod.expnet.typeCol')}</span>
            <span class="c3">${t('mod.expnet.maker')}</span>
            <span class="c4">${t('mod.expnet.name')}</span><span class="c5">${t('mod.expnet.services')}</span>
            <span class="ms">ms</span><span class="go"></span></div>
          ${shown.map((h) => {
            const who = isRandom(h.mac) ? t('mod.expnet.randomShort') : makerOf(h);
            const name = nameOf(h);
            const tag = h.self ? t('mod.expnet.thisDevice') : h.loopmax ? [h.loopmax.title, h.loopmax.fw].filter(Boolean).join(' ') : '';
            const svc = (h.services || []).join(',');
            const web = webOf(h);
            const look = TYPES[h.kind];
            const alarm = h.new ? C.ye : h.watch && h.watch.up === false ? C.re : '';
            return html`<div class=${'dbx-tr' + (h.online ? '' : ' off')} key=${h.ip + (h.mac || '')} role="listitem" tabindex="0"
                onClick=${() => setOpen(h)} onKeyDown=${(e) => { if (e.key === 'Enter') setOpen(h); }} data-db-host=${h.ip}>
              <span class="c1" style=${'color:' + (alarm || look.tone)} title=${typeName(h.kind)}><${ui.Icon} name=${look.icon} size=${15}/></span>
              <span class=${'dbx-cell c2 ' + (h.loopmax || h.self ? 'a' : '')}>${h.ip}<button type="button" class="dbx-cp"
                title=${t('mod.expnet.copyIp')} aria-label=${t('mod.expnet.copyIp')} data-db-copy=${h.ip}
                onClick=${(e) => { e.stopPropagation(); epCopy(ctx, h.ip); }} onKeyDown=${(e) => e.stopPropagation()}>
                <${ui.Icon} name="db-copy" size=${13}/></button>${h.new ? html` <span class="y">${t('mod.expnet.newTag')}</span>` : ''}</span>
              <span class="dbx-cell c6" style=${'color:' + look.tone}>${typeName(h.kind)}</span>
              <span class="dbx-cell c3"><span class="tyn" style=${'color:' + look.tone}>${typeName(h.kind)} · </span><span class="k">${who || platformOf(h) || (h.mac ? h.mac.slice(0, 8) : '–')}</span>${svc ? html`<small class="sv"> · ${svc}</small>` : ''}</span>
              <span class="dbx-cell c4">${name ? html`<span class=${h.loopmax || h.self ? 'a' : 'v'}>${name}</span>` : ''}${tag ? html` <small>${tag}</small>` : ''}${h.pc ? html` <span class=${h.pc.on ? 'g' : 'k'}>⏻</span>` : ''}</span>
              <span class="dbx-cell c5">${svc ? html`<small>${svc}</small>` : ''}</span>
              <span class="ms">${h.online ? (h.pingMs !== undefined ? html`${h.pingMs}<small class="u"> ms</small>` : '·') : html`<small>${h.lastUnix && nowUnix ? since(nowUnix - h.lastUnix, t) : ''}</small>`}</span>
              <span class="go">${web && html`<a class="dbx-go" href=${web} target="_blank" rel="noopener" title=${t('mod.expnet.openWeb') + ': ' + web}
                aria-label=${t('mod.expnet.openWeb')} onClick=${(e) => e.stopPropagation()} onKeyDown=${(e) => e.stopPropagation()}
                data-db-web=${h.ip}><${ui.Icon} name="db-open" size=${15}/></a>`}</span>
            </div>`;
          })}
        </div>`}
      </div>
      <p class="dbx-help">${t('mod.expnet.searchHelp', { n: scan.everyMin || 0 })}</p>
    <//>
    <div class="dbx-two">
      <${Panel} ctx=${ctx} title=${t('mod.expnet.events')} tone=${C.ye} data="log">
        <div class="dbx-term dbx-log">
          ${!events.length && html`<div class="k">${t('mod.expnet.noEvents')}</div>`}
          ${events.map((e, i) => html`<div key=${i}>${line(e)}</div>`)}
        </div>
      <//>
      <${Watched} ctx=${ctx} services=${services} pcs=${pcs}/>
    </div>
    <${Panel} ctx=${ctx} title="Telegram" tone=${notify.telegram ? C.gr : '#3a4a63'} data="telegram"
      right=${html`<${ui.Btn} small icon="db-send" disabled=${!notify.telegram} data-db="tg-test"
        onClick=${() => ctx.act('notify.test', {}, t('mod.expnet.tg.sent'))}>${t('mod.expnet.tg.test')}<//>
        <a class="btn sm" href="#/m/expnet/settings">${t('mod.expnet.tg.settings')}</a>`}>
      <div class="dbx-row">
        <span class=${'dbx-led ' + (notify.telegram ? (notify.bot === 'error' || notify.bot === 'token' ? 'bad' : 'ok') : '')}></span>
        <span class="dbx-main"><b>${notify.telegram ? t('mod.expnet.tg.on') : t('mod.expnet.tg.off')}</b>
          <small>${notify.telegram ? [t('mod.expnet.tg.pro'),
            notify.last && t('mod.expnet.tg.last', { r: notify.last }), notify.quiet && t('mod.expnet.tg.quiet')].filter(Boolean).join(' · ')
            : t('mod.expnet.tg.how')}</small></span>
      </div>
    <//>
    ${open && html`<${DeviceCard} ctx=${ctx} h=${open} gateway=${gateway} onClose=${() => setOpen(null)} onReload=${load}/>`}
  </div>`;
}

// The card of a device: what is known of it, its tools, watch, wake, a name of yours.
function DeviceCard({ ctx, h, gateway, onClose, onReload }) {
  const { html, ui, t, useState } = ctx;
  const lang = ctx.lang();
  const doc = ctx.useDoc() || {};
  const [alias, setAlias] = useState(h.alias || '');
  const [type, setType] = useState(h.type || '');
  const auto = typeOf(Object.assign({}, h, { type: '' }), gateway);
  const typeName = (k) => t('mod.expnet.type.' + (k || 'unknown'));
  const saveType = async (v) => {
    setType(v);
    const r = await ctx.act('host.alias', { ip: h.ip, mac: h.mac || '', type: v }, t('saved'));
    if (r.ok) onReload();
  };
  const [form, setForm] = useState(null);
  const who = makerOf(h);
  const pc = (doc.pcs || []).find((p) => h.mac && p.mac === h.mac);
  const watch = (doc.services || []).filter((s) => s.host === h.ip);
  const web = (h.services || []).includes('web') || (h.services || []).includes('homeassistant');
  const saveAlias = async () => {
    const r = await ctx.act('host.alias', { ip: h.ip, mac: h.mac || '', alias: alias.trim() }, t('saved'));
    if (r.ok) onReload();
  };
  const wake = () => (pc ? ctx.act('pc.wake', { id: pc.id }, t('mod.expnet.sent')) : setForm({ kind: 'pc' }));
  const rows = [
    ['IP', h.ip], [t('mod.expnet.typeCol'), typeName(type || auto) + (type ? ' · ' + t('mod.expnet.typeOwn') : auto ? ' · ' + t('mod.expnet.typeAuto') : '')],
    ['MAC', h.mac || '–'],
    [t('mod.expnet.maker'), isRandom(h.mac) ? t('mod.expnet.random') : who || '–'],
    h.ports && [t('mod.expnet.videoPorts'), h.ports.join(', ') + (platformOf(h) ? ' · ' + platformOf(h) : '')],
    h.name && [t('mod.expnet.mdnsName'), h.name], h.server && ['UPnP', h.server],
    h.services && [t('mod.expnet.services'), h.services.map((s) => t('mod.expnet.kind.' + s)).join(', ')],
    [t('mod.expnet.ping'), h.pingMs !== undefined ? h.pingMs + ' ms' : t('mod.expnet.noPing')],
    h.firstUnix && [t('mod.expnet.firstSeen'), clock(h.firstUnix, lang)],
    h.lastUnix && [t('mod.expnet.lastSeen'), h.online ? t('mod.expnet.nowHere') : clock(h.lastUnix, lang)],
    h.loopmax && ['LoopMaxPro', [h.loopmax.title, h.loopmax.fw && 'v' + h.loopmax.fw, h.loopmax.board].filter(Boolean).join(' · ')],
  ].filter(Boolean);
  const title = nameOf(h) || who || h.ip;
  const go = (kind, target) => { onClose(); openTool(kind, target); };
  return html`<${ui.Modal} title=${glyph(h, gateway) + ' ' + title} icon="network" onClose=${onClose} wide>
    <dl class="dbx-kv">${rows.map((r) => html`<dt key=${r[0]}>${r[0]}</dt><dd key=${r[0] + 'v'}>${r[1]}</dd>`)}</dl>
    ${watch.length > 0 && html`<p class="dbx-help">${watch.map((s) => html`<span key=${s.id} style="margin-right:12px">
      <span class=${'dbx-led ' + stateOf(s)}></span> ${s.name} (${t('mod.expnet.check.' + s.check)})</span>`)}</p>`}
    <div class="dbx-acts">
      <${ui.Btn} small icon="activity" onClick=${() => go('ping', h.ip)} data-db="card-ping">Ping<//>
      <${ui.Btn} small icon="db-route" onClick=${() => go('trace', h.ip)}>${t('mod.expnet.tool.trace')}<//>
      <${ui.Btn} small icon="search" onClick=${() => go('ports', h.ip)}>${t('mod.expnet.tool.ports')}<//>
      ${web && html`<${ui.Btn} small icon="globe" onClick=${() => go('http', webOf(h) || 'http://' + h.ip + '/')}>HTTP<//>`}
      ${webOf(h) && html`<a class="btn sm" href=${webOf(h)} target="_blank" rel="noopener" data-db="card-web">
        ${t(h.loopmax ? 'mod.expnet.openConsole' : 'mod.expnet.openWeb')}</a>`}
      <${ui.Btn} small icon="eye" onClick=${() => setForm({ kind: 'watch' })} data-db="card-watch">${t('mod.expnet.watch')}<//>
      ${h.mac && !h.self && html`<${ui.Btn} small kind="primary" icon="power" onClick=${wake} data-db="card-wake">${t('mod.expnet.wakeUp')}<//>`}
    </div>
    <div class="dbx-bar">
      <input class="dbx-in" value=${alias} maxLength=${19} placeholder=${t('mod.expnet.aliasHint')}
        onInput=${(e) => setAlias(e.target.value)} aria-label=${t('mod.expnet.alias')}/>
      <${ui.Btn} small icon="check" onClick=${saveAlias}>${t('mod.expnet.rename')}<//>
    </div>
    <div class="dbx-bar" style="margin-top:8px">
      <select class="dbx-in" value=${type} onChange=${(e) => saveType(e.target.value)} aria-label=${t('mod.expnet.typeCol')} data-db="card-type">
        <option value="">${t('mod.expnet.typeAutoIs', { type: typeName(auto) })}</option>
        ${OWN_TYPES.map((k) => html`<option key=${k} value=${k}>${typeName(k)}</option>`)}
      </select>
    </div>
    ${form && form.kind === 'watch' && html`<${ServiceForm} ctx=${ctx}
      start=${{ name: (title || h.ip).slice(0, 23), host: h.ip, check: web ? 'http' : 'ping' }} onClose=${() => setForm(null)}/>`}
    ${form && form.kind === 'pc' && html`<${PcForm} ctx=${ctx} wakeAfter
      start=${{ name: (title || h.ip).slice(0, 23), mac: h.mac, host: h.ip }} onClose=${() => setForm(null)}/>`}
  <//>`;
}

// The watched services and the saved PCs: their history, their changes, the PCs to wake.
function Watched({ ctx, services, pcs }) {
  const { html, ui, t, useState } = ctx;
  const lang = ctx.lang();
  const [history, setHistory] = useState(null);
  const [edit, setEdit] = useState(null);
  return html`<${Panel} ctx=${ctx} title=${t('mod.expnet.watched')} tone=${C.gr} data="watched">
    ${!services.length && !pcs.length && html`<p class="dbx-help" style="margin:0">${t('mod.expnet.noWatched')}</p>`}
    <div class="dbx-list">
      ${services.map((s) => html`<div class="dbx-row" key=${'s' + s.id} data-db-service=${s.id}>
        <span class=${'dbx-led ' + stateOf(s)}></span>
        <span class="dbx-main"><b>${s.name}</b>
          <small>${t('mod.expnet.check.' + s.check)} ${s.host}${s.port ? ':' + s.port : ''} · ${!s.known ? t('mod.expnet.checking')
            : t(s.up ? 'mod.expnet.upFor' : 'mod.expnet.downFor', { when: since(s.forSec, t) })}${s.up && s.ms !== undefined ? ' · ' + s.ms + ' ms' : ''} · 24h ${pct(s.up24, lang)}</small></span>
        <${ui.Btn} small icon="activity" title=${t('mod.expnet.history')} onClick=${() => setHistory(s)}/>
        <${ui.Btn} small icon="pencil" title=${t('btn.edit')} onClick=${() => setEdit({ kind: 's', v: s })}/>
        <${ui.Btn} small icon="trash-2" title=${t('btn.remove')} onClick=${() => ctx.act('service.remove', { id: s.id }, t('mod.expnet.removed'))}/>
      </div>`)}
      ${pcs.map((p) => html`<${PcRow} key=${'p' + p.id} ctx=${ctx} p=${p} onEdit=${(x) => setEdit({ kind: 'p', v: x })}/>`)}
    </div>
    ${history && html`<${History} ctx=${ctx} s=${history} onClose=${() => setHistory(null)}/>`}
    ${edit && edit.kind === 's' && html`<${ServiceForm} ctx=${ctx} start=${edit.v} onClose=${() => setEdit(null)}/>`}
    ${edit && edit.kind === 'p' && html`<${PcForm} ctx=${ctx} start=${edit.v} onClose=${() => setEdit(null)}/>`}
  <//>`;
}

const CHECKS = ['ping', 'tcp', 'http'];

function ServiceForm({ ctx, start, onClose }) {
  const { html, ui, t, useState } = ctx;
  const editing = !!(start && start.id);
  const [v, setV] = useState(() => Object.assign({ name: '', check: 'ping', host: '', port: '', path: '/', everySec: '' }, start || {}));
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState('');
  const set = (k) => (x) => setV(Object.assign({}, v, { [k]: x }));
  const save = async (e) => {
    if (e) e.preventDefault();
    const args = { name: v.name.trim(), host: v.host.trim() };
    if (!editing) args.check = v.check;
    if (v.check !== 'ping' && String(v.port).trim()) args.port = Number(v.port);
    if (v.check === 'http' && v.path.trim()) args.path = v.path.trim();
    if (String(v.everySec).trim()) args.everySec = Number(v.everySec);
    if (editing) args.id = v.id;
    setBusy(true);
    const r = await ctx.act(editing ? 'service.update' : 'service.add', args, t('saved'));
    setBusy(false);
    if (r.ok) onClose();
    else setError(r.field || r.status || '');
  };
  return html`<${ui.Modal} title=${t(editing ? 'mod.expnet.editService' : 'mod.expnet.addService')} icon="eye"
      onClose=${onClose} footer=${html`<${ui.Btn} kind="primary" icon="check" busy=${busy} onClick=${save}
        disabled=${!v.name.trim() || !v.host.trim() || (v.check === 'tcp' && !String(v.port).trim())}>${t('btn.save')}<//>`}>
    <form class="dbx-form" onSubmit=${save}>
      <${ui.TextInput} label=${t('mod.expnet.name')} value=${v.name} onInput=${set('name')} maxLength=${23} placeholder="NAS" autoFocus/>
      ${!editing && html`<${ui.Segmented} value=${v.check} onChange=${set('check')}
        options=${CHECKS.map((c) => ({ id: c, label: t('mod.expnet.check.' + c) }))}/>`}
      <${ui.TextInput} label=${t(v.check === 'http' ? 'mod.expnet.hostName' : 'mod.expnet.host')} value=${v.host}
        onInput=${set('host')} maxLength=${39} placeholder="192.168.1.20" error=${error === 'host' ? t('mod.expnet.badHost') : ''}/>
      ${v.check !== 'ping' && html`<div class="dbx-pair">
        <${ui.TextInput} label=${t('mod.expnet.port')} type="number" min=${1} max=${65535} value=${v.port}
          onInput=${set('port')} placeholder=${v.check === 'http' ? '80' : '445'}/>
        ${v.check === 'http' && html`<${ui.TextInput} label=${t('mod.expnet.path')} value=${v.path} onInput=${set('path')} maxLength=${39} placeholder="/"/>`}
      </div>`}
      <${ui.TextInput} label=${t('mod.expnet.every')} type="number" min=${10} max=${3600} value=${v.everySec}
        onInput=${set('everySec')} placeholder="60" hint=${t('mod.expnet.everyHint')}/>
      <p class="dbx-help">${t('mod.expnet.checkHelp.' + v.check)}</p>
    </form>
  <//>`;
}

function History({ ctx, s, onClose }) {
  const { html, ui, t, useState, useEffect } = ctx;
  const [range, setRange] = useState('day');
  const [data, setData] = useState(null);
  const [failed, setFailed] = useState(false);
  useEffect(() => {
    let live = true;
    setData(null);
    setFailed(false);
    ctx.read('history', { id: s.id, range }).then((r) => {
      if (!live) return;
      if (r.ok && r.result) setData(r.result);
      else setFailed(true);
    });
    return () => { live = false; };
  }, [range]);
  return html`<${ui.Modal} title=${s.name} icon="activity" onClose=${onClose} wide>
    <${ui.Segmented} value=${range} onChange=${setRange}
      options=${[{ id: 'day', label: t('mod.expnet.day') }, { id: 'week', label: t('mod.expnet.week') }]}/>
    ${failed && html`<p class="dbx-help">${t('mod.expnet.noHistory')}</p>`}
    ${!failed && !data && html`<p><${ui.Spinner}/></p>`}
    ${data && html`<div>
      <p class="dbx-help">${t('mod.expnet.latency')}</p>
      <${Graph} ctx=${ctx} height=${90} unit="ms" series=${[{ label: 'ms', color: C.cy, values: (data.ms || []).map((v) => (v === null ? -1 : v)) }]}/>
      <p class="dbx-help">${t('mod.expnet.answered')}</p>
      <${UpBars} ctx=${ctx} values=${data.up || []}/>
      <p class="dbx-help">${t(range === 'day' ? 'mod.expnet.dayNote' : 'mod.expnet.weekNote')}</p>
    </div>`}
  <//>`;
}

function PcForm({ ctx, start, onClose, wakeAfter = false }) {
  const { html, ui, t, useState } = ctx;
  const editing = !!(start && start.id);
  const [v, setV] = useState(() => Object.assign({ name: '', mac: '', host: '', broadcast: '', port: '', offUrl: '' }, start || {}));
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState('');
  const set = (k) => (x) => setV(Object.assign({}, v, { [k]: x }));
  const save = async (e) => {
    if (e) e.preventDefault();
    const args = { name: v.name.trim(), host: (v.host || '').trim(), broadcast: (v.broadcast || '').trim(),
                   port: Number(v.port) || 0, offUrl: (v.offUrl || '').trim() };
    if (editing) args.id = v.id;
    else args.mac = v.mac.trim();
    if (!editing && !args.host) delete args.host;
    if (!editing && !args.broadcast) delete args.broadcast;
    setBusy(true);
    const r = await ctx.act(editing ? 'pc.update' : 'pc.add', args, t('saved'));
    if (r.ok && wakeAfter && r.result && r.result.id) await ctx.act('pc.wake', { id: r.result.id }, t('mod.expnet.sent'));
    setBusy(false);
    if (r.ok) onClose();
    else setError(r.field || r.status || '');
  };
  return html`<${ui.Modal} title=${t(editing ? 'mod.expnet.editPc' : 'mod.expnet.addPc')} icon="power"
      onClose=${onClose} footer=${html`<${ui.Btn} kind="primary" icon=${wakeAfter ? 'power' : 'check'} busy=${busy} onClick=${save}
        disabled=${!v.name.trim() || (!editing && v.mac.replace(/[^0-9a-fA-F]/g, '').length !== 12)}>${t(wakeAfter ? 'mod.expnet.saveWake' : 'btn.save')}<//>`}>
    <form class="dbx-form" onSubmit=${save}>
      <${ui.TextInput} label=${t('mod.expnet.name')} value=${v.name} onInput=${set('name')} maxLength=${23} placeholder="PC Gaming" autoFocus/>
      ${!editing && html`<${ui.TextInput} label="MAC" value=${v.mac} onInput=${set('mac')} maxLength=${17}
        placeholder="3C:7C:3F:01:02:03" error=${error === 'mac' ? t('mod.expnet.badMac') : ''}/>`}
      <div class="dbx-pair">
        <${ui.TextInput} label=${t('mod.expnet.pcHost')} value=${v.host} onInput=${set('host')} maxLength=${15}
          placeholder="192.168.1.30" error=${error === 'host' ? t('mod.expnet.badHost') : ''}/>
        <${ui.TextInput} label=${t('mod.expnet.pcPort')} type="number" min=${0} max=${65535} value=${v.port}
          onInput=${set('port')} placeholder="3389" hint=${t('mod.expnet.pcPortHint')}/>
      </div>
      <${ui.TextInput} label=${t('mod.expnet.broadcast')} value=${v.broadcast} onInput=${set('broadcast')}
        maxLength=${15} placeholder="192.168.1.255" hint=${t('mod.expnet.broadcastHint')}/>
      <${ui.TextInput} label=${t('mod.expnet.offUrl')} value=${v.offUrl} onInput=${set('offUrl')} maxLength=${95}
        placeholder="http://192.168.1.10:8123/api/webhook/pc_off" hint=${t('mod.expnet.offHint')}
        error=${error === 'offUrl' ? t('mod.expnet.badUrl') : ''}/>
      <p class="dbx-help">${t('mod.expnet.wakeHelp')}</p>
    </form>
  <//>`;
}

function PcRow({ ctx, p, onEdit }) {
  const { html, ui, t } = ctx;
  const waking = p.wakingSec !== undefined;
  const led = waking ? 'mid blink' : !p.host ? '' : !p.known ? 'mid' : p.on ? 'ok' : 'bad';
  const line = waking ? t('mod.expnet.waking', { n: p.wakingSec }) : !p.host ? t('mod.expnet.noState')
    : !p.known ? t('mod.expnet.checking') : t(p.on ? 'mod.expnet.onFor' : 'mod.expnet.off', { when: since(p.forSec, t) });
  return html`<div class="dbx-row" data-db-pc=${p.id}>
    <span class=${'dbx-led ' + led}></span>
    <span class="dbx-main"><b>⏻ ${p.name}</b>
      <small>${line}${p.wokeSec !== undefined && !waking ? ' · ' + t('mod.expnet.woke', { n: p.wokeSec }) : ''} · ${p.mac}${p.host ? ' · ' + p.host : ''}</small></span>
    <${ui.Btn} small kind="primary" icon="power" disabled=${waking || (p.known && p.on)}
      onClick=${() => ctx.act('pc.wake', { id: p.id }, t('mod.expnet.sent'))}>${t('mod.expnet.wakeUp')}<//>
    ${p.canOff && html`<${ui.Btn} small icon="power" disabled=${p.known && !p.on}
      onClick=${() => ctx.act('pc.off', { id: p.id }, t('mod.expnet.offSent'))}>${t('mod.expnet.turnOff')}<//>`}
    <${ui.Btn} small icon="pencil" title=${t('btn.edit')} onClick=${() => onEdit(p)}/>
    <${ui.Btn} small icon="trash-2" title=${t('btn.remove')} onClick=${() => ctx.act('pc.remove', { id: p.id }, t('mod.expnet.removed'))}/>
  </div>`;
}

// ---------------------------------------------------------------- tools

const TOOLS = ['ping', 'trace', 'ports', 'dns', 'http'];
const DNS_TYPES = ['A', 'AAAA', 'CNAME', 'MX', 'TXT', 'NS', 'PTR'];
const METHODS = ['GET', 'POST', 'PUT', 'DELETE', 'OPTIONS'];

function Tools({ ctx }) {
  const { html, ui, t, useState, useRef } = ctx;
  const first = useRef(pendingTool);
  pendingTool = null;
  const [kind, setKind] = useState(first.current ? first.current.kind : 'ping');
  const [v, setV] = useState(() => {
    const f = first.current || {};
    return { target: f.kind && f.kind !== 'http' ? f.target : '', count: '10', mode: 'common', from: '1', to: '1024',
      type: 'A', server: '', method: 'GET', url: f.kind === 'http' ? f.target : 'http://', headers: '', body: '' };
  });
  const [pro, setPro] = useState(false);
  const set = (k) => (x) => setV((o) => Object.assign({}, o, { [k]: x && x.target ? x.target.value : x }));
  const ready = kind === 'http' ? v.url.trim().length > 8 : v.target.trim().length > 0;
  return html`<style>${STYLE}</style><div class="dbx">
    <${ProBanner} ctx=${ctx}/>
    <${Panel} ctx=${ctx} title=${t('mod.expnet.tools')} data="tools">
      <${ui.Segmented} value=${kind} onChange=${setKind}
        options=${TOOLS.map((x) => ({ id: x, label: t('mod.expnet.tool.' + x) }))}/>
      <div class="dbx-form" style="margin-top:12px">
        ${kind !== 'http' && html`<${ui.TextInput} label=${t(kind === 'dns' ? 'mod.expnet.dnsName' : 'mod.expnet.hostOrName')}
          value=${v.target} onInput=${set('target')} maxLength=${63} placeholder=${kind === 'dns' ? 'example.com' : '192.168.1.1'}/>`}
        ${kind === 'ping' && html`<${ui.TextInput} label=${t('mod.expnet.count')} type="number" min=${1} max=${1000} value=${v.count} onInput=${set('count')}/>`}
        ${kind === 'ports' && html`<${ui.Segmented} value=${v.mode} onChange=${set('mode')}
          options=${[{ id: 'common', label: t('mod.expnet.portsCommon') }, { id: 'range', label: t('mod.expnet.portsRange') }]}/>`}
        ${kind === 'ports' && v.mode === 'range' && html`<div class="dbx-pair">
          <${ui.TextInput} label=${t('mod.expnet.from')} type="number" min=${1} max=${65535} value=${v.from} onInput=${set('from')}/>
          <${ui.TextInput} label=${t('mod.expnet.to')} type="number" min=${1} max=${65535} value=${v.to} onInput=${set('to')}/>
        </div>`}
        ${kind === 'dns' && html`<div class="dbx-pair">
          <${ui.Select} label=${t('mod.expnet.dnsType')} value=${v.type} onChange=${set('type')} options=${DNS_TYPES.map((x) => ({ id: x, label: x }))}/>
          <${ui.TextInput} label=${t('mod.expnet.dnsServer')} value=${v.server} onInput=${set('server')} maxLength=${15} placeholder=${t('mod.expnet.dnsServerHint')}/>
        </div>`}
        ${kind === 'http' && html`<div class="dbx-pair">
          <${ui.Select} label=${t('mod.expnet.method')} value=${v.method} onChange=${set('method')} options=${METHODS.map((x) => ({ id: x, label: x }))}/>
          <${ui.TextInput} label="URL" value=${v.url} onInput=${set('url')} maxLength=${200} placeholder="http://192.168.1.10:8123/api/"/>
        </div>
        <span class="dbx-help">${t('mod.expnet.headers')}</span>
        <textarea class="dbx-ta" value=${v.headers} onInput=${set('headers')} maxLength=${400} placeholder="Authorization: Bearer …"></textarea>
        ${v.method !== 'GET' && html`<span class="dbx-help">${t('mod.expnet.body')}</span>
          <textarea class="dbx-ta" value=${v.body} onInput=${set('body')} maxLength=${1024} placeholder='{"on": true}'></textarea>`}`}
        <div class="dbx-bar">
          <${ui.Btn} kind="primary" icon="play" disabled=${!ready} onClick=${() => setPro(true)} data-db="run">${t('mod.expnet.run')}<//>
        </div>
      </div>
      <p class="dbx-help">${t('mod.expnet.toolHelp.' + kind, { ping: 1000, range: 65535, kb: 64, records: 32 })}</p>
    <//>
    ${pro && html`<${ProGate} ctx=${ctx} onClose=${() => setPro(false)}/>`}
  </div>`;
}

// ---------------------------------------------------------------- speed and live

// What ExpNetPro shows here, at rest: the needles, the graphs and what they would measure.
function Speed({ ctx }) {
  const { html, ui, t, useState } = ctx;
  const [pro, setPro] = useState(false);
  const start = () => setPro(true);
  return html`<style>${STYLE}</style><div class="dbx">
    <${ProBanner} ctx=${ctx}/>
    <${Panel} ctx=${ctx} title=${t('mod.expnet.liveTitle')} tone=${C.cy} data="live"
      right=${html`<${ui.Btn} kind="primary" small icon="play" onClick=${start} data-db="live-start">${t('mod.expnet.liveStart')}<//>`}>
      <div class="dbx-gauges">
        <${Gauge} ctx=${ctx} data="gw" value=${null} max=${50} warn=${20} bad=${40} label=${t('mod.expnet.router')} unit="ms"/>
        <${Gauge} ctx=${ctx} data="net" value=${null} max=${200} warn=${60} bad=${120} label="Internet" unit="ms"/>
        <${Gauge} ctx=${ctx} data="loss" value=${null} max=${20} warn=${1} bad=${5} label=${t('mod.expnet.loss')} unit="%" digits=${1}/>
        <${Gauge} ctx=${ctx} data="rssi" value=${null} max=${70} reverse warn=${30} bad=${20} label=${t('mod.expnet.signal')} unit=""/>
      </div>
      <${Graph} ctx=${ctx} unit="ms" series=${[{ label: 'Internet', color: C.cy, values: [] },
        { label: t('mod.expnet.router'), color: C.gr, values: [] }]}/>
      <p class="dbx-help">${t('mod.expnet.liveHelp', { ms: 250 })}</p>
    <//>
    <${Panel} ctx=${ctx} title=${t('mod.expnet.traffic')} tone=${C.vi} data="traffic">
      <div class="dbx-gauges">
        <${Gauge} ctx=${ctx} data="down" value=${null} max=${100} label=${t('mod.expnet.download')} unit="Mbit/s" digits=${1}/>
        <${Gauge} ctx=${ctx} data="up" value=${null} max=${20} label=${t('mod.expnet.upload')} unit="Mbit/s" digits=${1}/>
      </div>
      <p class="dbx-help">${t('mod.expnet.trafficHelp', { s: 1 })}</p>
    <//>
    <${Panel} ctx=${ctx} title=${t('mod.expnet.speedTest')} tone=${C.ye} data="speedtest"
      right=${html`<${ui.Btn} kind="primary" small icon="play" onClick=${start} data-db="speed">${t('mod.expnet.startTest')}<//>`}>
      <div class="dbx-gauges">
        <${Gauge} ctx=${ctx} value=${null} max=${100} label=${t('mod.expnet.download')} unit="Mbit/s" digits=${1}/>
        <${Gauge} ctx=${ctx} value=${null} max=${20} label=${t('mod.expnet.upload')} unit="Mbit/s" digits=${1}/>
        <${Gauge} ctx=${ctx} value=${null} max=${100} warn=${40} bad=${80} label="Ping" unit="ms"/>
      </div>
      <p class="dbx-help">${t('mod.expnet.speedHelp')}</p>
    <//>
    ${pro && html`<${ProGate} ctx=${ctx} onClose=${() => setPro(false)}/>`}
  </div>`;
}

// ---------------------------------------------------------------- Wi-Fi

function Wifi({ ctx }) {
  const { html, ui, t, useState } = ctx;
  const [asked, setAsked] = useState(0);
  const net = ctx.useSysDoc('sys.net', asked ? 1500 : 0);
  const scan = (net && net.scan) || {};
  const sta = (net && net.sta) || {};
  const nets = (scan.networks || []).filter((n) => n.channel >= 1 && n.channel <= 14).sort((a, b) => b.rssi - a.rssi);
  const running = scan.state === 'running' || scan.state === 'scanning';
  const ask = async () => { setAsked(Date.now()); await ctx.sysRead('sys.net', 'scan', {}); };
  // Channels -1 to 15 across, so that the networks of 1 and 13 are whole; -95 to -30 dBm up.
  const x = (ch) => ((ch + 1.5) / 17) * 100;
  const y = (rssi) => 100 - Math.max(0, Math.min(1, (rssi + 95) / 65)) * 85;
  const colors = [C.vi, C.pk, C.ye, C.gr, C.or, C.bl, '#e879f9', '#2dd4bf'];
  // The least crowded of 1, 6 and 11: who is near, and how loud, weighs on a channel.
  const load = (ch) => nets.filter((n) => n.ssid !== sta.ssid)
    .reduce((s, n) => s + Math.max(0, 1 - Math.abs(n.channel - ch) / 5) * (n.rssi + 100), 0);
  const best = nets.length ? [1, 6, 11].sort((a, b) => load(a) - load(b))[0] : 0;
  return html`<style>${STYLE}</style><div class="dbx">
    <div class="dbx-tiles">
      <${Tile} ctx=${ctx} label=${t('mod.expnet.h.net')} value=${sta.ssid || '–'} tone=${C.cy} sub=${sta.channel ? 'ch ' + sta.channel : ''}/>
      <${Tile} ctx=${ctx} label=${t('mod.expnet.signal')} value=${sta.rssi !== undefined ? sta.rssi : '–'} unit="dBm"
        tone=${sta.rssi >= -67 ? C.gr : sta.rssi >= -75 ? C.ye : C.re}/>
      <${Tile} ctx=${ctx} label=${t('mod.expnet.t.around')} value=${nets.length} tone=${C.vi}/>
      <${Tile} ctx=${ctx} label=${t('mod.expnet.bestChannel')} value=${best || '–'} tone=${best && best === sta.channel ? C.gr : C.ye}
        sub=${best && sta.channel && best !== sta.channel ? t('mod.expnet.t.move', { n: best }) : best ? t('mod.expnet.t.fine') : ''}/>
    </div>
    <${Panel} ctx=${ctx} title=${t('mod.expnet.channels')} tone=${C.vi} data="wifi"
      right=${html`<${ui.Btn} kind="primary" small icon="radar" busy=${running} onClick=${ask} data-db="wifi-scan">${t('mod.expnet.wifiScan')}<//>`}>
      ${nets.length > 0 && html`<div class="dbx-wchart" role="img" aria-label=${t('mod.expnet.channels')}>
        <svg viewBox="0 0 100 100" preserveAspectRatio="none" aria-hidden="true">
          ${[-40, -60, -80].map((r) => html`<line key=${r} class="gl" x1="0" x2="100" y1=${y(r)} y2=${y(r)}/>`)}
          <line class="ax" x1="0" x2="100" y1="100" y2="100"/>
          ${nets.slice().reverse().map((n, i) => {
            const mine = n.ssid === sta.ssid && n.channel === sta.channel;
            const col = mine ? C.cy : colors[i % colors.length];
            const top = y(n.rssi);
            return html`<path key=${'n' + i} class=${'net' + (mine ? ' mine' : '')} fill=${col} stroke=${col}
              d=${`M${x(n.channel - 2)} 100 C ${x(n.channel - 1)} ${top - 12}, ${x(n.channel + 1)} ${top - 12}, ${x(n.channel + 2)} 100 Z`}/>`;
          })}
        </svg>
        ${[-40, -60, -80].map((r) => html`<span key=${'r' + r} class="dbx-yl" style=${`top:${y(r)}%`}>${r}</span>`)}
        ${[1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13].map((c) => html`<span key=${'c' + c} class=${'dbx-ch' + (c === best ? ' best' : '')}
          style=${`left:${x(c)}%`}>${c}</span>`)}
        ${(() => {
          const stack = {};
          return nets.slice(0, 8).map((n, i) => {
            const mine = n.ssid === sta.ssid && n.channel === sta.channel;
            const col = mine ? C.cy : colors[(nets.length - 1 - i) % colors.length];
            const k = stack[n.channel] = (stack[n.channel] || 0) + 1;
            return html`<span key=${'s' + i} class="dbx-ssid" style=${`left:${x(n.channel)}%;top:calc(${y(n.rssi) - 9}% - ${(k - 1) * 14}px);color:${col}`}>${(n.ssid || '?').slice(0, 16)}</span>`;
          });
        })()}
      </div>`}
      <div class="dbx-term" style="margin-top:10px">
        ${!nets.length && html`<div class="k">${running ? t('mod.expnet.looking') : t('mod.expnet.wifiNone')}</div>`}
        ${nets.map((n, i) => html`<div key=${i} style="display:flex;gap:10px">
          <span style="width:46px;text-align:right" class=${n.rssi >= -67 ? 'g' : n.rssi >= -80 ? 'y' : 'r'}>${n.rssi}</span>
          <span style="width:34px" class="k">ch${n.channel}</span>
          <span style="width:16px" class="k">${n.secured ? '\u{1F512}' : '○'}</span>
          <span class=${n.ssid === sta.ssid ? 'a' : n.device ? 'v' : ''} style="flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap">${n.ssid || t('mod.expnet.hidden')}${n.device ? ' ◆' : ''}</span>
        </div>`)}
      </div>
      <p class="dbx-help">${t('mod.expnet.wifiHelp')}</p>
    <//>
  </div>`;
}

// ---------------------------------------------------------------- MQTT

// The broker and what to listen to; listening and sending are of ExpNetPro.
function Mqtt({ ctx }) {
  const { html, ui, t, useState } = ctx;
  const sysMqtt = ctx.useSysDoc('sys.mqtt', 0);
  const host = sysMqtt && (sysMqtt.host || (sysMqtt.broker && sysMqtt.broker.host));
  const guess = host ? 'ws://' + host + ':9001' : 'ws://192.168.1.10:9001';
  const [v, setV] = useState({ url: '', user: '', pass: '', filter: '#' });
  const [pro, setPro] = useState(false);
  const set = (k) => (x) => setV((o) => Object.assign({}, o, { [k]: x && x.target ? x.target.value : x }));
  return html`<style>${STYLE}</style><div class="dbx">
    <${ProBanner} ctx=${ctx}/>
    <${Panel} ctx=${ctx} title=${t('mod.expnet.mqttBroker')} tone="#3a4a63" data="mqtt"
      right=${html`<span class="dbx-pill"><span class="dbx-led"></span>${t('mod.expnet.mqttState.closed')}</span>`}>
      <div class="dbx-form">
        <div class="dbx-pair">
          <${ui.TextInput} label=${t('mod.expnet.mqttUrl')} value=${v.url} onInput=${set('url')} placeholder=${guess} maxLength=${120}/>
          <${ui.TextInput} label=${t('mod.expnet.mqttFilter')} value=${v.filter} onInput=${set('filter')} placeholder="#" maxLength=${120}/>
        </div>
        <div class="dbx-pair">
          <${ui.TextInput} label=${t('mod.expnet.mqttUser')} value=${v.user} onInput=${set('user')} maxLength=${64}/>
          <${ui.TextInput} label=${t('mod.expnet.mqttPass')} type="password" value=${v.pass} onInput=${set('pass')} maxLength=${64}/>
        </div>
        <div class="dbx-bar"><${ui.Btn} kind="primary" icon="play" onClick=${() => setPro(true)} data-db="mqtt-connect">${t('mod.expnet.mqttOpen')}<//></div>
      </div>
      <p class="dbx-help">${t('mod.expnet.mqttHelp')}</p>
    <//>
    ${pro && html`<${ProGate} ctx=${ctx} onClose=${() => setPro(false)}/>`}
  </div>`;
}

// ---------------------------------------------------------------- test endpoints

// Endpoints of a test server, for who writes a program: a path under /ep/ of the device, the
// methods it takes, the code it answers with, the parameters in (checked as a server checks
// them) and out (the members of the answer: fixed, random, counted, of the clock, given back).
// The page shows the address, the request as curl and the answer before they exist. ExpNetPro
// keeps them and answers; in ExpNet saving is of ExpNetPro.
const EP_TYPES = ['text', 'int', 'float', 'bool'];
const EP_MODES = ['fixed', 'random', 'counter', 'time', 'echo'];
const EP_METHODS = ['GET', 'POST', 'PUT', 'DELETE'];
const EP_CODES = [200, 201, 202, 204, 400, 401, 403, 404, 409, 422, 500, 503];

// A new endpoint starts as an example that already works.
function epFresh() {
  return { id: 0, name: '', path: '', methods: ['GET'], code: 200, params: [
    { name: 'id', dir: 'in', type: 'int', required: false, mode: 'fixed', value: '1' },
    { name: 'valore', dir: 'out', type: 'float', mode: 'random', value: '10..30' },
  ] };
}

// "Meteo di casa" -> "meteo-di-casa": the path the name suggests.
function epSlug(text) {
  return String(text || '').normalize('NFD').replace(/[\u0300-\u036f]/g, '').toLowerCase()
    .replace(/[^a-z0-9._~/-]+/g, '-').replace(/-+/g, '-').replace(/^[-/]+|[-/]+$/g, '').slice(0, 47);
}
const epPathGood = (p) => /^[A-Za-z0-9._~/-]+$/.test(p) && !p.includes('..') && !p.includes('//') && !p.endsWith('/');
const epNameGood = (n) => /^[A-Za-z0-9._-]+$/.test(n);

function epTyped(type, text) {
  const v = text === undefined || text === null ? '' : String(text);
  if (type === 'int') return parseInt(v, 10) || 0;
  if (type === 'float') return Number(v) || 0;
  if (type === 'bool') return v === 'true' || v === '1';
  return v;
}
const epExample = (p) => (p.value ? p.value : p.type === 'int' ? '1' : p.type === 'float' ? '1.5' : p.type === 'bool' ? 'true' : 'abc');

// The clock of the device, as the answer writes it: 2026-10-06T19:05:00+02:00.
function epStamp(d) {
  const two = (n) => String(n).padStart(2, '0');
  const off = -d.getTimezoneOffset();
  return `${d.getFullYear()}-${two(d.getMonth() + 1)}-${two(d.getDate())}T${two(d.getHours())}:${two(d.getMinutes())}:${two(d.getSeconds())}`
    + `${off < 0 ? '-' : '+'}${two(Math.floor(Math.abs(off) / 60))}:${two(Math.abs(off) % 60)}`;
}

// An answer as the device writes it, with the middle value for the random ones.
function epSample(ep) {
  const named = ep.params.filter((p) => p.name);
  const ins = named.filter((p) => p.dir === 'in');
  const outs = named.filter((p) => p.dir === 'out');
  if (!outs.length) {
    const got = {};
    ins.forEach((p) => { got[p.name] = epTyped(p.type, epExample(p)); });
    return { ok: ep.code < 400, endpoint: ep.name, received: got };
  }
  const out = {};
  outs.forEach((p) => {
    const v = p.value || '';
    if (p.mode === 'random') {
      if (p.type === 'bool') out[p.name] = true;
      else if (p.type === 'text') out[p.name] = v.split('|')[0];
      else {
        const [a, b] = v.includes('..') ? v.split('..').map(Number) : [0, v ? Number(v) : 100];
        const mid = ((isFinite(a) ? a : 0) + (isFinite(b) ? b : 0)) / 2;
        out[p.name] = p.type === 'int' ? Math.round(mid) : Number(mid.toFixed(2));
      }
    } else if (p.mode === 'counter') {
      const n = parseInt(v, 10);
      out[p.name] = p.type === 'text' ? String(isFinite(n) ? n : 1) : isFinite(n) ? n : 1;
    } else if (p.mode === 'time') {
      out[p.name] = p.type === 'text' ? epStamp(new Date()) : Math.floor(Date.now() / 1000);
    } else if (p.mode === 'echo') {
      const from = ins.find((q) => q.name === (v || p.name));
      out[p.name] = from ? epTyped(p.type, epExample(from)) : null;
    } else {
      out[p.name] = epTyped(p.type, v);
    }
  });
  return out;
}

// The request of a program, as curl: the parameters in the query, or in a JSON body.
function epCurl(base, ep) {
  const ins = ep.params.filter((p) => p.dir === 'in' && p.name);
  const method = ep.methods.length ? ep.methods[0] : 'GET';
  const url = base + ep.path;
  if (method === 'GET' || method === 'DELETE') {
    const q = ins.map((p) => encodeURIComponent(p.name) + '=' + encodeURIComponent(epExample(p))).join('&');
    return `curl ${method === 'DELETE' ? '-X DELETE ' : ''}"${url}${q ? '?' + q : ''}"`;
  }
  const body = {};
  ins.forEach((p) => { body[p.name] = epTyped(p.type, epExample(p)); });
  return `curl -X ${method} "${url}" -H "Content-Type: application/json" -d '${JSON.stringify(body)}'`;
}

// Into the clipboard; on the device the page is not https, and the old way is the one that works.
function epCopy(ctx, text) {
  const done = () => ctx.toast(ctx.t('mod.expnet.ep.copied'), 'ok');
  if (navigator.clipboard && window.isSecureContext) {
    navigator.clipboard.writeText(text).then(done, () => {});
    return;
  }
  const ta = document.createElement('textarea');
  ta.value = text;
  ta.style.position = 'fixed';
  ta.style.opacity = '0';
  document.body.appendChild(ta);
  ta.select();
  try { if (document.execCommand('copy')) done(); } catch (e) { /* not allowed */ }
  ta.remove();
}

function Endpoints({ ctx }) {
  const { html, ui, t, useState } = ctx;
  const lang = ctx.lang();
  const doc = ctx.useDoc() || {};
  const info = doc.endpoints || {};
  const limits = doc.limits || {};
  const net = ctx.useSysDoc('sys.net', 20000);
  const [list, setList] = useState(FREE ? [] : null);
  const [calls, setCalls] = useState([]);
  const [edit, setEdit] = useState(null);
  const [pro, setPro] = useState(false);
  const sta = (net && net.sta) || {};
  const base = info.base || `http://${sta.ip || location.host}/ep/`;
  const most = limits.endpoints || 8;
  const load = async () => {
    if (FREE) return;
    const r = await ctx.read('ep.list', {});
    if (r.ok && r.result) setList(r.result.endpoints || []);
  };
  usePoll(ctx, load, 0, [info.count]);
  usePoll(ctx, async () => {
    if (FREE) return;
    const r = await ctx.read('ep.calls', {});
    if (r.ok && r.result) setCalls(r.result.calls || []);
  }, 0, [info.calls]);
  const open = async (e) => {
    if (!e || FREE) {
      setEdit(e || epFresh());
      return;
    }
    const r = await ctx.read('ep.get', { id: e.id });
    if (!r.ok || !r.result) return;
    const x = r.result;
    setEdit({ id: x.id, name: x.name, path: x.path, methods: x.methods || [], code: x.code,
      params: (x.params || []).map((p) => ({ name: p.name, dir: p.dir, type: p.type, required: !!p.required, mode: p.mode || 'fixed', value: p.value || '' })) });
  };
  return html`<style>${STYLE}</style><div class="dbx">
    ${FREE && html`<${ProBanner} ctx=${ctx}/>`}
    <${Panel} ctx=${ctx} title=${t('mod.expnet.ep.title')} tone=${C.or} data="endpoints"
      right=${html`<${ui.Btn} small kind="primary" icon="plus" disabled=${!FREE && list && list.length >= most}
        onClick=${() => open(null)} data-db="ep-new">${t('mod.expnet.ep.new')}<//>`}>
      <div class="dbx-strip" style="margin-bottom:10px"><span><b>base</b>${base}</span></div>
      ${list === null && html`<div class="dbx-term k">${t('mod.expnet.loading')}</div>`}
      ${list && !list.length && html`<div class="dbx-term k">${t('mod.expnet.ep.none')}</div>`}
      ${list && list.length > 0 && html`<div class="dbx-list">${list.map((e) => html`<div class="dbx-row" key=${e.id} data-db-ep=${e.path}>
        <span class="dbx-ep-m">${e.methods && e.methods.length ? e.methods.join(' ') : 'ANY'}</span>
        <span class="dbx-main"><b>${e.name}</b><small>/ep/${e.path} → ${e.code}${(e.in || []).length ? ' · in: ' + e.in.join(', ') : ''}${(e.out || []).length ? ' · out: ' + e.out.join(', ') : ''}</small></span>
        <span class="dbx-pill">${t('mod.expnet.ep.hits', { n: e.hits || 0 })}${e.lastUnix ? ' · ' + clock(e.lastUnix, lang) : ''}</span>
        <${ui.Btn} small icon="db-copy" onClick=${() => epCopy(ctx, base + e.path)}>URL<//>
        <${ui.Btn} small icon="pencil" onClick=${() => open(e)} data-db="ep-edit">${t('mod.expnet.ep.edit')}<//>
      </div>`)}</div>`}
      <p class="dbx-help">${t('mod.expnet.ep.help', { n: most })}</p>
    <//>
    ${!FREE && html`<${Panel} ctx=${ctx} title=${t('mod.expnet.ep.calls')} tone=${C.cy} data="ep-calls"
      right=${calls.length ? html`<${ui.Btn} small icon="trash-2" onClick=${async () => { await ctx.read('ep.clear', {}); setCalls([]); }}>${t('mod.expnet.ep.clear')}<//>` : ''}>
      <div class="dbx-term dbx-log">
        ${!calls.length && html`<div class="k">${t('mod.expnet.ep.noCalls')}</div>`}
        ${calls.map((c) => html`<div key=${c.seq}><span class="k">${c.unix ? clock(c.unix, lang) : ''}</span>
          <span class=${c.code < 300 ? 'g' : c.code < 500 ? 'y' : 'r'}> ${c.code}</span> <span class="a">${c.method}</span> /ep/${c.path}
          <span class="k"> ${c.ip}</span>${c.params ? html` <span class="v">${c.params}</span>` : ''}${c.body ? html` <span>${c.body.slice(0, 160)}</span>` : ''}</div>`)}
      </div>
    <//>`}
    ${edit && html`<${EpEditor} ctx=${ctx} start=${edit} base=${base} limits=${limits}
      taken=${(list || []).filter((e) => e.id !== edit.id).map((e) => e.path)}
      onClose=${() => setEdit(null)} onSaved=${() => { setEdit(null); load(); }} onPro=${() => setPro(true)}/>`}
    ${pro && html`<${ProGate} ctx=${ctx} onClose=${() => setPro(false)}/>`}
  </div>`;
}

function EpEditor({ ctx, start, base, limits, taken, onClose, onSaved, onPro }) {
  const { html, ui, t, useState } = ctx;
  const [ep, setEp] = useState(() => JSON.parse(JSON.stringify(start)));
  const [byHand, setByHand] = useState(!!start.id);   // the path was written, not made from the name
  const [busy, setBusy] = useState(false);
  const most = limits.epParams || 12;
  const change = (k, v) => setEp((o) => {
    const n = Object.assign({}, o, { [k]: v });
    if (k === 'name' && !byHand) n.path = epSlug(v);
    return n;
  });
  const setParam = (i, k, v) => setEp((o) => Object.assign({}, o, {
    params: o.params.map((p, j) => (j === i ? Object.assign({}, p, { [k]: v }) : p)) }));
  const addParam = () => setEp((o) => Object.assign({}, o, {
    params: o.params.concat([{ name: '', dir: 'out', type: 'text', required: false, mode: 'fixed', value: '' }]) }));
  const dropParam = (i) => setEp((o) => Object.assign({}, o, { params: o.params.filter((_, j) => j !== i) }));
  const flip = (m) => change('methods', ep.methods.includes(m) ? ep.methods.filter((x) => x !== m)
    : EP_METHODS.filter((x) => x === m || ep.methods.includes(x)));
  const badPath = !ep.path || !epPathGood(ep.path) || taken.includes(ep.path);
  const badName = ep.params.some((p) => p.name && !epNameGood(p.name));
  const ready = ep.name.trim() && !badPath && !badName;
  const save = async () => {
    if (FREE) { onPro(); return; }
    setBusy(true);
    const fail = (r) => { setBusy(false); ctx.toast(t('mod.expnet.ep.notSaved', { why: r.field || r.status }), 'err'); };
    const r = await ctx.command('ep.set', { id: ep.id, name: ep.name.trim(), path: ep.path, methods: ep.methods.join(','), code: Number(ep.code) });
    if (!r.ok || !r.result) { fail(r); return; }
    const id = r.result.id;
    let at = 0;
    for (const p of ep.params.filter((x) => x.name)) {
      const how = p.dir === 'out' ? `out:${p.type}:${p.mode}` : `in:${p.type}${p.required ? ':required' : ''}`;
      const s = await ctx.command('ep.param', { id, at, name: p.name, how, value: p.dir === 'out' && p.mode === 'time' ? '' : p.value || '' });
      if (!s.ok) { fail(s); return; }
      at += 1;
    }
    const c = await ctx.command('ep.params', { id, count: at });
    setBusy(false);
    if (!c.ok) { fail(c); return; }
    ctx.toast(t('mod.expnet.ep.saved'), 'ok');
    onSaved();
  };
  const remove = async () => {
    if (FREE) { onPro(); return; }
    const r = await ctx.act('ep.remove', { id: ep.id }, t('mod.expnet.ep.removed'));
    if (r.ok) onSaved();
  };
  const curl = epCurl(base, ep);
  const sample = ep.code === 204 ? '' : JSON.stringify(epSample(ep), null, 2);
  return html`<${ui.Modal} title=${ep.id ? ep.name : t('mod.expnet.ep.new')} icon="db-api" onClose=${onClose} wide
    footer=${html`${ep.id ? html`<${ui.Btn} kind="danger" icon="trash-2" onClick=${remove}>${t('mod.expnet.ep.remove')}<//>` : ''}
      <${ui.Btn} onClick=${onClose}>${t('btn.cancel')}<//>
      <${ui.Btn} kind="primary" icon="check" busy=${busy} disabled=${!ready} onClick=${save} data-db="ep-save">${t('mod.expnet.ep.save')}<//>`}>
    <div class="dbx-form">
      <div class="dbx-pair">
        <${ui.TextInput} label=${t('mod.expnet.ep.name')} value=${ep.name} maxLength=${31} onInput=${(v) => change('name', v)}/>
        <${ui.TextInput} label=${t('mod.expnet.ep.path')} value=${ep.path} maxLength=${47} hint=${'/ep/' + (ep.path || '…')}
          error=${ep.path && badPath ? t('mod.expnet.ep.badPath') : ''}
          onInput=${(v) => { setByHand(true); change('path', v.replace(/^\/+/, '')); }}/>
      </div>
      <div class="dbx-pair">
        <div><span class="dbx-help">${t('mod.expnet.ep.methods')}</span>
          <div class="dbx-chips">${EP_METHODS.map((m) => html`<button type="button" key=${m} class=${'dbx-chip' + (ep.methods.includes(m) ? ' on' : '')}
            onClick=${() => flip(m)}>${m}</button>`)}</div></div>
        <${ui.Select} label=${t('mod.expnet.ep.code')} value=${String(ep.code)} onChange=${(v) => change('code', Number(v))}
          options=${EP_CODES.map((c) => ({ id: String(c), label: String(c) }))}/>
      </div>
      <div class="dbx-ph" style="margin:6px 0 0"><span class="dbx-pt" style=${`--tone:${C.or}`}><i></i>${t('mod.expnet.ep.params')}</span>
        <span class="dbx-pr"><${ui.Btn} small icon="plus" disabled=${ep.params.length >= most} onClick=${addParam} data-db="ep-add">${t('mod.expnet.ep.addParam')}<//></span></div>
      <div class="dbx-eps">
        ${ep.params.map((p, i) => html`<div class="dbx-ep" key=${i}>
          <input class=${'dbx-in' + (p.name && !epNameGood(p.name) ? ' bad' : '')} value=${p.name} maxLength=${23}
            placeholder=${t('mod.expnet.ep.pName')} aria-label=${t('mod.expnet.ep.pName')} onInput=${(e) => setParam(i, 'name', e.target.value)}/>
          <select class="dbx-in" value=${p.dir} onChange=${(e) => setParam(i, 'dir', e.target.value)} aria-label="in/out">
            <option value="in">in</option><option value="out">out</option></select>
          <select class="dbx-in" value=${p.type} onChange=${(e) => setParam(i, 'type', e.target.value)} aria-label=${t('mod.expnet.ep.pType')}>
            ${EP_TYPES.map((x) => html`<option key=${x} value=${x}>${t('mod.expnet.ep.type.' + x)}</option>`)}</select>
          ${p.dir === 'out'
            ? html`<select class="dbx-in" value=${p.mode} onChange=${(e) => setParam(i, 'mode', e.target.value)} aria-label=${t('mod.expnet.ep.pMode')}>
                ${EP_MODES.map((x) => html`<option key=${x} value=${x}>${t('mod.expnet.ep.mode.' + x)}</option>`)}</select>`
            : html`<label class="dbx-req"><input type="checkbox" checked=${!!p.required}
                onChange=${(e) => setParam(i, 'required', e.target.checked)}/> ${t('mod.expnet.ep.required')}</label>`}
          <input class="dbx-in" value=${p.dir === 'out' && p.mode === 'time' ? '' : p.value} maxLength=${47}
            disabled=${p.dir === 'out' && p.mode === 'time'} aria-label=${t('mod.expnet.ep.pValue')}
            placeholder=${t(p.dir === 'in' ? 'mod.expnet.ep.hint.example' : 'mod.expnet.ep.hint.' + p.mode)} onInput=${(e) => setParam(i, 'value', e.target.value)}/>
          <${ui.Btn} small icon="x" title=${t('mod.expnet.ep.dropParam')} onClick=${() => dropParam(i)}/>
        </div>`)}
      </div>
      <div class="dbx-two">
        <div><div class="dbx-ph" style="margin:4px 0 6px"><span class="dbx-pt"><i></i>${t('mod.expnet.ep.curl')}</span>
          <span class="dbx-pr"><${ui.Btn} small icon="db-copy" onClick=${() => epCopy(ctx, curl)}>${t('mod.expnet.ep.copy')}<//></span></div>
          <pre class="dbx-term dbx-pre" data-db="ep-curl">${curl}</pre></div>
        <div><div class="dbx-ph" style="margin:4px 0 6px"><span class="dbx-pt" style=${`--tone:${C.gr}`}><i></i>${t('mod.expnet.ep.answer', { code: ep.code })}</span></div>
          <pre class="dbx-term dbx-pre" data-db="ep-answer">${sample}</pre></div>
      </div>
      <p class="dbx-help">${t('mod.expnet.ep.editHelp')}</p>
    </div>
  <//>`;
}

export default {
  uiApi: 1,
  icons: {
    'db-terminal': '<polyline points="4 17 10 11 4 5"/><line x1="12" x2="20" y1="19" y2="19"/>',
    'db-route': '<circle cx="6" cy="19" r="3"/><path d="M9 19h8.5a3.5 3.5 0 0 0 0-7h-11a3.5 3.5 0 0 1 0-7H15"/><circle cx="18" cy="5" r="3"/>',
    'db-send': '<path d="M14.536 21.686a.5.5 0 0 0 .937-.024l6.5-19a.496.496 0 0 0-.635-.635l-19 6.5a.5.5 0 0 0-.024.937l7.93 3.18a2 2 0 0 1 1.112 1.11z"/><path d="m21.854 2.147-10.94 10.939"/>',
    'db-mqtt': '<path d="M4.9 19.1C1 15.2 1 8.8 4.9 4.9"/><path d="M7.8 16.2c-2.3-2.3-2.3-6.1 0-8.5"/><circle cx="12" cy="12" r="2"/><path d="M16.2 7.8c2.3 2.3 2.3 6.1 0 8.5"/><path d="M19.1 4.9C23 8.8 23 15.1 19.1 19"/>',
    'db-copy': '<rect width="14" height="14" x="8" y="8" rx="2" ry="2"/><path d="M4 16c-1.1 0-2-.9-2-2V4c0-1.1.9-2 2-2h10c1.1 0 2 .9 2 2"/>',
    'db-api': '<path d="M8 3H7a2 2 0 0 0-2 2v5a2 2 0 0 1-2 2 2 2 0 0 1 2 2v5c0 1.1.9 2 2 2h1"/><path d="M16 21h1a2 2 0 0 0 2-2v-5c0-1.1.9-2 2-2a2 2 0 0 1-2-2V5a2 2 0 0 0-2-2h-1"/>',
    'db-open': '<path d="M15 3h6v6"/><path d="M10 14 21 3"/><path d="M18 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h6"/>',
    'db-pc': '<rect width="20" height="14" x="2" y="3" rx="2"/><path d="M8 21h8"/><path d="M12 17v4"/>',
    'db-phone': '<rect width="14" height="20" x="5" y="2" rx="2"/><path d="M12 18h.01"/>',
    'db-cam': '<path d="m16 13 5.2 3.5a.5.5 0 0 0 .8-.4V7.9a.5.5 0 0 0-.8-.4L16 10.5"/><rect x="2" y="6" width="14" height="12" rx="2"/>',
    'db-nvr': '<rect width="20" height="10" x="2" y="7" rx="2"/><circle cx="7" cy="12" r="1.5"/><path d="M12 12h6"/>',
    'db-tv': '<rect width="20" height="15" x="2" y="7" rx="2"/><path d="m17 2-5 5-5-5"/>',
    'db-speaker': '<rect width="16" height="20" x="4" y="2" rx="2"/><circle cx="12" cy="14" r="4"/><path d="M12 6h.01"/>',
    'db-printer': '<path d="M6 9V2h12v7"/><path d="M6 18H4a2 2 0 0 1-2-2v-5a2 2 0 0 1 2-2h16a2 2 0 0 1 2 2v5a2 2 0 0 1-2 2h-2"/><rect width="12" height="8" x="6" y="14"/>',
    'db-plug': '<path d="M12 22v-5"/><path d="M9 8V2"/><path d="M15 8V2"/><path d="M18 8v5a4 4 0 0 1-4 4h-4a4 4 0 0 1-4-4V8Z"/>',
  },
  // The phrases of the console this page uses, with their keys (F31 of the server).
  same: {
    'mod.expnet.broadcast': 'field.broadcast', 'mod.expnet.editService': 'btn.edit',
    'mod.expnet.ep.edit': 'btn.edit', 'mod.expnet.ep.name': 'mod.name',
    'mod.expnet.ep.save': 'btn.save', 'mod.expnet.ev.pcOff': 'net.apDown',
    'mod.expnet.ev.pcOn': 'net.apUp', 'mod.expnet.group.loopmax': 'sys.title',
    'mod.expnet.group.net': 'sys.network', 'mod.expnet.h.net': 'sys.network',
    'mod.expnet.kind.mqtt': 'sys.mqtt', 'mod.expnet.mqttBroker': 'mqtt.state',
    'mod.expnet.mqttPass': 'login.password',
    'mod.expnet.mqttState.connected': 'net.state.connected', 'mod.expnet.mqttUser': 'login.user',
    'mod.expnet.name': 'mod.name', 'mod.expnet.off': 'net.apDown', 'mod.expnet.port': 'mqtt.port',
    'mod.expnet.pro.close': 'btn.close', 'mod.expnet.services': 'sys.services',
    'mod.expnet.t.online': 'kind.online', 'mod.expnet.tg.settings': 'sys.settings',
    'mod.expnet.tool.dns': 'net.dns', 'mod.expnet.type.ap': 'mode.AP',
    'mod.expnet.type.server': 'cloud.state', 'mod.expnet.why.timeout': 'net.result.timeout',
  },
  texts: {
    it: {
      'mod.expnet.home': 'ExpNet',
      'mod.expnet.thisDevice': 'questo ExpNet',
      'mod.expnet.tg.how': 'Crea un bot con @BotFather, poi scrivi token e chat nelle impostazioni: arrivano gli avvisi e il riepilogo del mattino. I comandi dal bot sono di ExpNetPro.',
      'mod.expnet.tg.pro': 'comandi: in ExpNetPro', 'mod.expnet.botCommands.locked': 'Solo in ExpNetPro',
      'mod.expnet.liveStart': 'Avvia', 'mod.expnet.pro.title': 'Questo è di ExpNetPro',
      'mod.expnet.pro.text': 'ExpNet è la versione free di Networks Explorer: la rete e chi c\'è, i nuovi, i servizi sorvegliati, i PC da accendere, il Wi-Fi e gli avvisi su Telegram. Queste sono di ExpNetPro, la versione completa:',
      'mod.expnet.pro.li.tools': 'ping, traccia, porte, DNS e richieste HTTP fatti dalla scheda',
      'mod.expnet.pro.li.speed': 'le lancette dal vivo, il traffico del router e il test di velocità',
      'mod.expnet.pro.li.mqtt': 'MQTT: i messaggi di un broker, dal vivo, e l\'invio a mano',
      'mod.expnet.pro.li.ep': 'gli endpoint di prova, per chi scrive un programma',
      'mod.expnet.pro.li.bot': 'i comandi dal bot Telegram: /stato, /ping, /accendi…',
      'mod.expnet.pro.text2': 'ExpNetPro è un dispositivo SmartiLab pronto, su una scheda più potente, gestito anche dal server SmartiLab.',
      'mod.expnet.pro.shop': 'Vai al negozio',
      'mod.expnet.pro.banner': 'Per utilizzare la suite completa passa a Networks Explorer PRO.',
      'mod.expnet.trafficHelp': 'Quanto passa adesso tra casa e Internet, letto dal router ogni {s} s. La scala è la velocità della linea. Se il router non conta i byte ricevuti, il download è stimato dai pacchetti.',
      'cmd.mod.expnet.scan': 'Chi c\'è nella rete: indirizzi, MAC, nomi, servizi',
      'cmd.mod.expnet.hosts': 'I dispositivi della rete, una pagina alla volta',
      'cmd.mod.expnet.host.alias': 'Dà un nome tuo a un dispositivo della rete',
      'cmd.mod.expnet.history': 'Le ultime 24 ore di un servizio, o la sua settimana',
      'cmd.mod.expnet.events': 'Cosa è successo nella rete, i più recenti prima',
      'cmd.mod.expnet.live': 'I tempi del router e di Internet, il segnale, il traffico del router',
      'cmd.mod.expnet.notify.test': 'Manda un messaggio di prova su Telegram',
      'mod.expnet.ep.help': 'Endpoint finti per chi scrive un programma: ExpNetPro li espone nella rete locale all\'indirizzo base, senza password. Il programma li chiama come il servizio vero: i parametri in arrivo sono controllati, la risposta è quella scritta qui. Qui li prepari e vedi richiesta e risposta; salvarli è di ExpNetPro.',
    },
    en: {
      'mod.expnet.home': 'ExpNet',
      'mod.expnet.thisDevice': 'this ExpNet',
      'mod.expnet.tg.how': 'Make a bot with @BotFather, then write token and chat in the settings: the warnings and the morning summary arrive. The commands from the bot are of ExpNetPro.',
      'mod.expnet.tg.pro': 'commands: in ExpNetPro', 'mod.expnet.botCommands.locked': 'In ExpNetPro only',
      'mod.expnet.liveStart': 'Start', 'mod.expnet.pro.title': 'Part of ExpNetPro',
      'mod.expnet.pro.text': 'ExpNet is the free version of Networks Explorer: the network and who is in it, the new ones, the watched services, the PCs to wake, Wi-Fi and the warnings on Telegram. These are of ExpNetPro, the full version:',
      'mod.expnet.pro.li.tools': 'ping, trace, ports, DNS and HTTP requests made by the device',
      'mod.expnet.pro.li.speed': 'the live needles, the traffic of the router and the speed test',
      'mod.expnet.pro.li.mqtt': 'MQTT: the messages of a broker, live, and sending by hand',
      'mod.expnet.pro.li.ep': 'the test endpoints, for who writes a program',
      'mod.expnet.pro.li.bot': 'the commands from the Telegram bot: /status, /ping, /wake…',
      'mod.expnet.pro.text2': 'ExpNetPro is a ready SmartiLab device, on a more powerful board, managed from the SmartiLab server too.',
      'mod.expnet.pro.shop': 'Go to the shop',
      'mod.expnet.pro.banner': 'To use the full suite, switch to Networks Explorer PRO.',
      'mod.expnet.trafficHelp': 'What passes now between the house and Internet, read from the router every {s} s. The scale is the speed of the line. When the router does not count the bytes received, the download is estimated from the packets.',
      'mod.expnet.ep.help': 'Fake endpoints for who writes a program: ExpNetPro serves them in the local network at the base address, without a password. The program calls them as the real service: the parameters that arrive are checked, the answer is the one written here. Here you prepare them and see request and answer; saving them is of ExpNetPro.',
    },
  },
  tabs: [
    { id: 'home', icon: 'house', label: 'mod.expnet.home', view: Home },
    { id: 'wifi', icon: 'wifi', label: 'mod.expnet.wifi', view: Wifi },
    { id: 'tools', icon: 'db-terminal', label: 'mod.expnet.tools', view: Tools },
    { id: 'speed', icon: 'gauge', label: 'mod.expnet.speed', view: Speed },
    { id: 'mqtt', icon: 'db-mqtt', label: 'MQTT', view: Mqtt },
    { id: 'endpoints', icon: 'db-api', label: 'mod.expnet.endpoints', view: Endpoints },
  ],
};
