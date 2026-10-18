// LoopMaxPro - the interface of EspRadio: a radio of the web with a look of its own, on
// purpose not the look of the console. Three of them, chosen with a key: a car stereo of matte
// black with lights of the colour one likes, a stage amplifier, a neon sign.
//   The head unit: a display with the station on air and its lamps, two levels, the keys of a
//                  radio (before, play, next, scan, timer, favourite, mute), the volume knob,
//                  six memories
//   Equalizer:     the spectrum of what plays, ten bands that change the sound, presets
//   Full version:  two more tabs, the media player and the television of EspWebMedia, drawn as
//                  they are there and still, under a banner with the link to the shop
//   Stations:      one question to more than one directory at once (Radio Browser, SomaFM,
//                  laut.fm), and a reserve kept here, so that something always plays; favourites
//                  with categories, kept by the device; the last ones listened to
// The sound is of the browser: the device keeps the favourites and serves the page. The radio
// itself lives outside the components: it plays on while the console shows something else.
// The page counts its bytes: everything is drawn by hand, and a look is a handful of colours
// over the same shapes. It imports nothing: Preact and the pieces of the console come from ctx
// (docs/web-console.md).

const STYLE = `
.rk { --gn:#2dff7a; --rd:#ff2d43; --am:#ffb020; --k:hsl(var(--h) 100% 62%); --g:hsl(var(--h) 100% 55% / .42); --fc:#08090b;
  --pn:linear-gradient(#17191c,#0e0f11); --bd:#262a30; --ln:#2a2e35; --ink:#e8edf4; --mut:#7f8894; --dp:linear-gradient(#06080b,#000); --dn:#fff;
  --ds:hsl(var(--h) 100% 78%); --kb:linear-gradient(#22252a,#131417); --kc:#c9d1db; --lo:#050607; --rw:#101215;
  color:var(--ink); display:grid; gap:10px; min-width:0; padding:10px; border-radius:16px; border:1px solid var(--bd); background:var(--fc);
  box-shadow:inset 0 1px 0 rgba(255,255,255,.05),0 10px 26px rgba(0,0,0,.45); }
.rk-amp { --gn:#39d353; --rd:#ff3b30; --k:#ffb000; --g:rgba(255,176,0,.4); --fc:#0d0c0a repeating-linear-gradient(45deg,rgba(255,255,255,.028) 0 2px,transparent 2px 5px);
  --pn:linear-gradient(#2c2a26,#181715); --bd:#000; --ln:#000; --ink:#f3e9d2; --mut:#a3977f; --dp:#150b02; --dn:#ffb000; --ds:#c98e1a;
  --kb:linear-gradient(#3b3833,#1d1b18); --kc:#f3e9d2; --lo:#0f0d0b; --rw:#1b1917; box-shadow:inset 0 0 0 2px #26221c,inset 0 0 0 3px rgba(184,137,43,.45); }
.rk-neo { --k:#19e6ff; --g:rgba(25,230,255,.42); --bd:#ff2bd6; --ln:#5b2c80; --ink:#f4eaff; --mut:#a892c8; --dp:#07030d; --dn:#ff6be6; --ds:#19e6ff;
  --fc:radial-gradient(circle at 12% 0,rgba(255,43,214,.22),transparent 42%),radial-gradient(circle at 90% 100%,rgba(25,230,255,.16),transparent 42%),#0a0612;
  --pn:rgba(20,10,34,.8); --kb:rgba(25,230,255,.05); --kc:#19e6ff; --lo:#07030d; --rw:rgba(255,255,255,.035); }
.rk * { box-sizing:border-box; }
.rk-p { position:relative; min-width:0; padding:12px; border-radius:12px; border:1px solid var(--bd); background:var(--pn); box-shadow:inset 0 1px 0 rgba(255,255,255,.06); }
.rk-amp .rk-p { box-shadow:inset 0 1px 0 rgba(255,255,255,.09),0 6px 16px rgba(0,0,0,.5); }
.rk-amp .rk-p::before { content:""; position:absolute; inset:0; pointer-events:none; border-radius:10px;
  background:radial-gradient(circle at 7px 7px,#8a8478 0 1.5px,#111 2.5px,transparent 3.5px),
    radial-gradient(circle at calc(100% - 7px) 7px,#8a8478 0 1.5px,#111 2.5px,transparent 3.5px),
    radial-gradient(circle at 7px calc(100% - 7px),#8a8478 0 1.5px,#111 2.5px,transparent 3.5px),
    radial-gradient(circle at calc(100% - 7px) calc(100% - 7px),#8a8478 0 1.5px,#111 2.5px,transparent 3.5px); }
.rk-neo .rk-p { box-shadow:0 0 14px rgba(255,43,214,.32),inset 0 0 22px rgba(255,43,214,.07); }
.rk-h { display:flex; align-items:center; gap:8px; flex-wrap:wrap; margin:0 2px 9px; }
.rk-logo { font:800 .95rem/1 var(--font); letter-spacing:.2em; text-transform:uppercase; color:var(--dn); }
.rk-logo b { color:var(--k); text-shadow:0 0 10px var(--k); }
.rk-amp .rk-logo { font:italic 900 1.3rem/1 Georgia,serif; letter-spacing:0; text-transform:none; background:linear-gradient(#f8d98a,#b8892b);
  -webkit-background-clip:text; background-clip:text; color:transparent; padding-right:4px; }
.rk-amp .rk-logo b { color:inherit; text-shadow:none; }
.rk-tag { font:700 .58rem var(--mono); letter-spacing:.14em; text-transform:uppercase; color:var(--mut); }
.rk-sp { margin-left:auto; }
.rk-led { display:inline-block; width:8px; height:8px; border-radius:50%; background:var(--ln); flex:none; }
.rk-lp { font:700 .54rem var(--mono); letter-spacing:.12em; padding:2px 5px; border-radius:3px; color:var(--mut); border:1px solid var(--ln); opacity:.55; white-space:nowrap; }
.rk-led, .rk-lp { --c:var(--k); }
.rk-gn { --c:var(--gn); } .rk-rd { --c:var(--rd); } .rk-am { --c:var(--am); }
.rk-led.on, .rk-lp.on { opacity:1; color:#000; border-color:var(--c); background:var(--c); box-shadow:0 0 9px var(--c); }
.rk-lp.rk-rd.on { color:#fff; }
.rk-wait { animation:rk-b 1s ease-in-out infinite; }
.rk-amp .rk-lp:not(.on):not([data-rk]) { display:none; }
@keyframes rk-b { 50% { opacity:.35; } }
.rk-d { position:relative; overflow:hidden; display:grid; grid-template-columns:auto minmax(0,1fr) auto; gap:8px 12px; align-items:center; min-height:92px;
  padding:11px 12px; border-radius:10px; border:1px solid var(--ln); background:var(--dp);
  box-shadow:inset 0 0 calc(16px + var(--lv,0)*34px) hsl(var(--h) 100% 50% / calc(.12 + var(--lv,0)*.3)); }
.rk-d::before { content:""; position:absolute; left:6%; right:6%; bottom:0; height:2px; opacity:calc(.25 + var(--lv,0)*.75);
  background:linear-gradient(90deg,transparent,var(--k),#fff,var(--k),transparent); }
.rk-d::after { content:""; position:absolute; inset:0; pointer-events:none; background:linear-gradient(115deg,rgba(255,255,255,.07) 0 30%,transparent 30.4%); }
.rk-amp .rk-d { border:2px solid #000; box-shadow:inset 0 0 26px rgba(255,150,0,.16),inset 0 0 0 1px #4a3a1a; font-family:var(--mono); }
.rk-amp .rk-d::before, .rk-neo .rk-d::after { content:none; }
.rk-amp .rk-d::after { background:repeating-linear-gradient(transparent 0 2px,rgba(0,0,0,.2) 2px 3px); }
.rk-neo .rk-d { border-color:var(--k); box-shadow:0 0 12px var(--g),inset 0 0 calc(14px + var(--lv,0)*30px) rgba(255,43,214,calc(.14 + var(--lv,0)*.3)); }
.rk-art { width:58px; height:58px; border-radius:9px; border:1px solid var(--ln); background:var(--lo); object-fit:contain; display:flex; align-items:center;
  justify-content:center; color:var(--mut); }
.rk-n { font-size:1.22rem; font-weight:800; color:var(--dn); text-shadow:0 0 12px var(--k); white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }
.rk-neo .rk-n, .rk-neo .rk-logo { text-shadow:0 0 12px var(--dn); }
.rk-s { font:.76rem var(--mono); color:var(--ds); white-space:nowrap; overflow:hidden; text-overflow:ellipsis; min-height:1.1em; margin-top:2px; }
.rk-s.err { color:#ff8a94; white-space:normal; }
.rk-st { display:flex; gap:5px; flex-wrap:wrap; margin-top:6px; }
.rk-lv { grid-column:1 / -1; display:grid; grid-template-columns:auto minmax(0,1fr); gap:3px 7px; align-items:center; font:700 .5rem var(--mono); color:var(--mut); }
.rk-lv i { position:relative; height:6px; background:repeating-linear-gradient(90deg,var(--ln) 0 4px,transparent 4px 6px); }
.rk-lv b { position:absolute; inset:0; clip-path:inset(0 100% 0 0); background:linear-gradient(90deg,var(--k) 55%,var(--am) 78%,var(--rd) 93%);
  -webkit-mask:repeating-linear-gradient(90deg,#000 0 4px,transparent 4px 6px); mask:repeating-linear-gradient(90deg,#000 0 4px,transparent 4px 6px); }
.rk-vus { display:flex; gap:6px; }
.rk-vu { width:64px; height:40px; border-radius:5px; background:#f1dfae; border:1px solid #000; box-shadow:inset 0 0 8px rgba(0,0,0,.35); }
.rk-vu path { fill:none; stroke:#3a2a10; stroke-width:1.5; } .rk-vu path.r { stroke:#c62828; stroke-width:2.5; }
.rk-vu line { stroke:#1a1208; stroke-width:1.6; stroke-linecap:round; transform-origin:30px 36px; transform:rotate(-48deg); }
.rk-vu text { font:700 6px var(--mono); fill:#3a2a10; text-anchor:middle; }
.rk-c { display:flex; gap:8px; flex-wrap:wrap; align-items:center; justify-content:center; margin-top:11px; }
.rk-b { min-width:46px; height:46px; padding:0 10px; border-radius:10px; border:1px solid #000; cursor:pointer; color:var(--kc); background:var(--kb);
  box-shadow:inset 0 1px 0 rgba(255,255,255,.08),inset 0 -2px 0 var(--g),0 2px 5px rgba(0,0,0,.6); display:inline-flex; align-items:center;
  justify-content:center; gap:6px; font:700 .62rem var(--mono); letter-spacing:.1em; text-decoration:none; }
.rk a.rk-b { color:var(--kc); }
.rk-b:active { transform:translateY(1px); }
.rk-b:disabled { opacity:.4; cursor:default; }
.rk-b:focus-visible, .rk-in:focus, .rk-r:focus-visible, .rk-ch:focus-visible { outline:2px solid var(--k); outline-offset:1px; }
.rk-b.big { min-width:70px; height:54px; color:var(--k); }
.rk-b.lit { color:#fff; text-shadow:0 0 8px var(--k); box-shadow:inset 0 -2px 0 var(--k),inset 0 0 14px var(--g),0 0 12px var(--g); }
.rk-b.lit .rk-f { fill:currentColor; }
.rk-b.sm { min-width:36px; height:36px; padding:0 7px; border-radius:8px; }
.rk-amp .rk-b { box-shadow:inset 0 1px 0 rgba(255,255,255,.13),0 2px 0 #000; }
.rk-amp .rk-b.lit { color:#141005; text-shadow:none; background:linear-gradient(#ffcb52,#d69200); box-shadow:0 0 12px var(--g),0 2px 0 #000; }
.rk-neo .rk-b { border-color:var(--k); box-shadow:0 0 8px var(--g),inset 0 0 8px rgba(25,230,255,.12); }
.rk-neo .rk-b.lit { background:rgba(255,43,214,.28); border-color:var(--bd); box-shadow:0 0 14px var(--bd); text-shadow:0 0 8px var(--bd); }
.rk-k { position:relative; width:64px; height:64px; flex:none; border-radius:50%; filter:drop-shadow(0 0 6px var(--g));
  background:conic-gradient(from 225deg,var(--k) calc(var(--v)*270deg),var(--ln) 0 270deg,#0000 0); }
.rk-k i { position:absolute; inset:5px; border-radius:50%; border:1px solid #000; background:radial-gradient(circle at 35% 28%,#3a3d42,#0b0c0e 72%); }
.rk-k b { position:absolute; inset:0; display:flex; align-items:center; justify-content:center; font:700 1.05rem var(--mono); color:var(--dn); }
.rk-k input { position:absolute; inset:0; width:100%; height:100%; margin:0; opacity:0; cursor:ew-resize; }
.rk-amp .rk-k { background:none; filter:none; }
.rk-amp .rk-k i { transform:rotate(calc(var(--v)*270deg - 135deg)); background:radial-gradient(circle at 35% 30%,#5a554c,#1a1816 70%);
  box-shadow:0 3px 6px rgba(0,0,0,.6),inset 0 1px 0 rgba(255,255,255,.2); }
.rk-amp .rk-k i::after { content:""; position:absolute; left:50%; top:4px; width:3px; height:15px; margin-left:-1.5px; border-radius:2px; background:var(--k);
  box-shadow:0 0 6px var(--k); }
.rk-amp .rk-k b { inset:auto 0 -13px; display:block; text-align:center; font-size:.56rem; letter-spacing:.12em; color:var(--mut); }
.rk-pr { display:grid; grid-template-columns:repeat(6,minmax(0,1fr)); gap:6px; margin-top:14px; }
.rk-pr .rk-b { min-width:0; height:42px; padding:0 3px; flex-direction:column; gap:2px; letter-spacing:0; }
.rk-pr b { color:var(--k); font-size:.8rem; line-height:1; }
.rk-pr small { max-width:100%; font:.54rem var(--font); opacity:.75; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }
.rk-amp .rk-pr .lit b { color:inherit; }
.rk-cv { display:block; width:100%; height:80px; border-radius:8px; border:1px solid var(--ln); background:var(--dp); }
.rk-eqs { display:grid; grid-template-columns:repeat(10,minmax(0,1fr)); gap:2px; margin-top:9px; text-align:center; }
.rk-eqs input { writing-mode:vertical-lr; direction:rtl; width:100%; height:86px; margin:0; accent-color:var(--k); }
.rk-eqs small { display:block; font:.6rem var(--mono); color:var(--mut); }
.rk-off { opacity:.4; }
.rk-note { margin:8px 2px 0; font-size:.78rem; line-height:1.4; color:var(--mut); }
.rk-chips { display:flex; gap:6px; flex-wrap:wrap; align-items:center; }
.rk-ch { font:700 .68rem var(--mono); padding:7px 10px; border-radius:8px; border:1px solid var(--ln); background:var(--lo); color:var(--mut); cursor:pointer;
  display:inline-flex; gap:6px; align-items:center; }
.rk-ch.on { color:#fff; border-color:var(--k); box-shadow:inset 0 0 12px var(--g),0 0 8px var(--g); }
.rk-amp .rk-ch { background:#211f1c; }
.rk-amp .rk-ch.on { color:#141005; background:var(--k); box-shadow:none; }
.rk-bar { display:flex; gap:6px; margin:9px 0 6px; }
.rk-in { flex:1 1 0; min-width:0; height:42px; padding:0 11px; border-radius:10px; border:1px solid var(--ln); background:var(--lo); color:var(--ink);
  font:1rem var(--font); }
.rk select { flex:none; height:34px; max-width:150px; padding:0 4px; border-radius:8px; border:1px solid var(--ln); background:var(--lo); color:var(--ink);
  font:700 .68rem var(--mono); }
.rk-pk { display:grid; gap:8px; margin-top:6px; padding:8px; border-radius:10px; border:1px solid var(--k); background:var(--lo); scroll-margin-top:110px; }
.rk-pk .rk-chips { max-height:150px; overflow:auto; }
.rk-l { display:grid; gap:4px; margin-top:9px; }
.rk-r { display:grid; grid-template-columns:42px minmax(0,1fr) auto; gap:1px 10px; align-items:center; padding:7px 8px; cursor:pointer; border-radius:10px;
  border:1px solid var(--ln); background:var(--rw); }
.rk-r:hover { border-color:var(--k); }
.rk-r.now { border-color:var(--k); background:linear-gradient(90deg,var(--g),var(--rw) 75%); box-shadow:0 0 12px var(--g); }
.rk-r img, .rk-r .ph { width:42px; height:42px; border-radius:8px; background:var(--lo); object-fit:contain; grid-row:span 2; display:flex; align-items:center;
  justify-content:center; color:var(--mut); }
.rk-rn { font-weight:700; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }
.rk-r.now .rk-rn { color:var(--dn); text-shadow:0 0 10px var(--k); }
.rk-rs { font:.7rem var(--mono); color:var(--mut); white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }
.rk-ra { grid-row:span 2; display:flex; gap:5px; align-items:center; }
.rk-ra select { height:36px; max-width:104px; }
.rk-bars { display:inline-flex; gap:2px; align-items:flex-end; height:11px; margin-right:6px; }
.rk-bars i { width:3px; background:var(--k); box-shadow:0 0 5px var(--k); animation:rk-e .7s ease-in-out infinite alternate; }
.rk-bars i:nth-child(2) { animation-delay:.2s; } .rk-bars i:nth-child(3) { animation-delay:.45s; }
@keyframes rk-e { from { height:3px; } to { height:11px; } }
.rk-e { padding:18px 10px; text-align:center; color:var(--mut); font-size:.84rem; border-radius:10px; border:1px dashed var(--ln); }
.rk-buy { display:flex; gap:10px; align-items:center; flex-wrap:wrap; padding:10px 12px; border-radius:12px; border:1px solid #fb923c; background:var(--lo); font-size:.86rem; }
.rk-buy svg { color:#fb923c; flex:none; }
.rk-buy span { flex:1 1 180px; }
.rk-lock { display:grid; gap:10px; opacity:.55; pointer-events:none; filter:grayscale(.5); }
.rk-lock .rk-r { cursor:default; }
.rk-lock .rk-rs { white-space:normal; }
.rk-tt { width:76px; height:76px; border-radius:50%; background:repeating-radial-gradient(circle,#0b0b0d 0 2px,#1c1c20 2px 3px); display:grid; place-items:center; }
.rk-tt i { width:36%; height:36%; border-radius:50%; background:var(--k); }
.rk-tv { aspect-ratio:16/9; border-radius:12px; background:#000; color:#9aa4b2; display:grid; place-content:center; justify-items:center; gap:8px; padding:10px;
  font-size:.86rem; text-align:center; }
@media (prefers-reduced-motion:reduce) { .rk-bars i, .rk-wait { animation:none; } }
@media (max-width:560px) {
  .rk { padding:7px; } .rk-p { padding:10px 9px; } .rk-sub { display:none; }
  .rk-d { grid-template-columns:auto minmax(0,1fr); } .rk-vus { grid-column:1 / 3; justify-content:center; }
  .rk-art { width:50px; height:50px; } .rk-n { font-size:1.08rem; }
  .rk-cv { height:64px; } .rk-ra select { max-width:76px; }
}
`;

const FREQ = [31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000];
const PRESETS = { flat: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0], rock: [5, 4, 3, 1, -1, -1, 1, 3, 4, 5], pop: [-1, 1, 3, 4, 3, 1, 0, -1, -1, -1],
  jazz: [3, 2, 1, 2, -1, -1, 0, 1, 2, 3], classic: [4, 3, 2, 1, -1, -1, 0, 2, 3, 4], voice: [-3, -2, 0, 2, 4, 4, 3, 1, 0, -2],
  bass: [6, 5, 4, 2, 0, 0, 0, 0, 0, 0] };
// The looks, by the class of each: the car stereo has none. And the colours of its lights.
const LOOKS = ['', 'rk-amp', 'rk-neo'];
const HUES = [195, 215, 265, 320, 355, 30, 140];
// The genres offered before a word is written: names Radio Browser has, each with hundreds of
// stations. The others come from it while one writes.
const GENRES = ('pop,rock,dance,house,jazz,blues,reggae,soul,funk,disco,metal,punk,indie,alternative,hiphop,rap,rnb,latin,classical,electronic,techno,'
  + 'trance,ambient,chillout,lounge,country,folk,oldies,60s,70s,80s,90s,classic rock,hard rock,italian pop,musica italiana,hits,top 40,news,talk,sports,kids')
  .split(',');
// Words that say nothing of a station: left out when the whole text finds nothing.
const NOISE = /\b(musica|music|radio|stazion[ei]|stations?|genere|fm)\b/g;
// Stations that play when no directory answers: name, address, genre, country. They are
// all in https and all letting the page read their sound.
const RESERVE = [
  ['Rai Radio 1', 'https://icestreaming.rai.it/1.mp3', 'news', 'IT'], ['Rai Radio 2', 'https://icestreaming.rai.it/2.mp3', 'pop', 'IT'],
  ['Rai Radio 3', 'https://icestreaming.rai.it/3.mp3', 'classical', 'IT'], ['Virgin Radio', 'https://icy.unitedradio.it/Virgin.mp3', 'rock', 'IT'],
  ['Radio Freccia', 'https://streamingv2.shoutcast.com/radiofreccia', 'rock', 'IT'],
  ['Rock Antenne', 'https://stream.rockantenne.de/rockantenne/stream/mp3', 'rock', 'DE'],
  ['Radio Paradise', 'https://stream.radioparadise.com/mp3-128', 'eclectic', 'US'],
  ['Radio Paradise Rock', 'https://stream.radioparadise.com/rock-128', 'rock', 'US'],
  ['FIP', 'https://icecast.radiofrance.fr/fip-midfi.mp3', 'eclectic', 'FR'], ['FIP Rock', 'https://icecast.radiofrance.fr/fiprock-midfi.mp3', 'rock', 'FR'],
  ['Radio Swiss Jazz', 'https://stream.srg-ssr.ch/m/rsj/mp3_128', 'jazz', 'CH'], ['Radio Swiss Pop', 'https://stream.srg-ssr.ch/m/rsp/mp3_128', 'pop', 'CH'],
  ['Radio Swiss Classic', 'https://stream.srg-ssr.ch/m/rsc_de/mp3_128', 'classical', 'CH'],
  ['KEXP', 'https://kexp-mp3-128.streamguys1.com/kexp128.mp3', 'indie', 'US'],
  ['BBC World Service', 'https://stream.live.vc.bbcmedia.co.uk/bbc_world_service', 'news', 'GB'],
  ['NTS 1', 'https://stream-relay-geo.ntslive.net/stream', 'eclectic', 'GB'],
];

// What this browser keeps for itself: the volume, the equalizer, the look, the last stations.
// Speakers, ears and eyes are of who listens, not of the device.
function ls(key, value) {
  try {
    if (value === undefined) return JSON.parse(localStorage.getItem('rk.' + key));
    localStorage.setItem('rk.' + key, JSON.stringify(value));
  } catch (e) { /* a private window: nothing is kept */ }
  return null;
}
function hid(text) {
  let h = 2166136261;
  for (let i = 0; i < text.length; i++) h = Math.imul(h ^ text.charCodeAt(i), 16777619);
  return (h >>> 0).toString(36);
}
// A text in at most n bytes: the device counts bytes, and a name may be in any alphabet.
function cut(text, n) {
  let s = String(text || '').trim();
  while (new TextEncoder().encode(s).length > n) s = s.slice(0, -1);
  return s;
}
const low = (text) => String(text || '').toLowerCase();
const flag = (k) => (/^[a-z]{2}$/i.test(k || '') ? String.fromCodePoint(...[...k.toUpperCase()].map((c) => 127397 + c.charCodeAt(0))) : '');
// A station, as the page and the device keep it: i id, n name, u address, l logo, t info, c
// category; h its site, g its genres, uuid its name in Radio Browser.
function station(s) {
  s.n = cut(s.n, 43) || '?';
  s.t = cut([flag(s.k), (s.g || '').split(/[,|]/).map((x) => x.trim()).filter(Boolean).slice(0, 2).join(', '), s.q].filter(Boolean).join(' · '), 59);
  return s;
}
async function getJson(url) {
  const r = await fetch(url, AbortSignal.timeout ? { signal: AbortSignal.timeout(7000) } : {});
  if (!r.ok) throw new Error(r.status);
  return r.json();
}

// ---------------------------------------------------------------- the directories

// Radio Browser has more than one server: the two known here, one at random first, then the
// name that leads to whichever is alive. Found silent, it is left alone for half a minute.
const RB = ['de1', 'de2'].sort(() => Math.random() - 0.5).concat('all');
let rbDown = 0;
async function rb(path) {
  if (Date.now() < rbDown) throw new Error('rb');
  for (let i = 0; i < RB.length; i++) {
    try {
      return await getJson('https://' + RB[0] + '.api.radio-browser.info/json/' + path);
    } catch (e) {
      RB.push(RB.shift());
    }
  }
  rbDown = Date.now() + 30000;
  throw new Error('rb');
}
// The countries that have stations, most first: k code, n name in the language of the console,
// e name in English.
let lands = null;
async function landList(lang) {
  if (!lands) {
    const list = await rb('countries?order=stationcount&reverse=true');
    let names = null;
    try {
      names = new Intl.DisplayNames([lang], { type: 'region' });
    } catch (e) { /* an old browser: the names in English */ }
    lands = list.filter((x) => x.iso_3166_1).map((x) => {
      let n = x.name;
      try {
        n = names.of(x.iso_3166_1) || n;
      } catch (e) { /* a code it does not know */ }
      return { k: x.iso_3166_1, e: x.name, n };
    });
  }
  return lands;
}
let soma = null;
const has = (s, text) => !text || low(s.n + ' ' + (s.g || '')).includes(low(text));
// Each directory: find(q) gives the stations for a question (text, genre, country, order, off),
// forty a page where there are more: then the list has `more`. One that has stations of one
// country only is silent for another.
const SRC = {
  rb: {
    name: 'Radio Browser',
    async find(q) {
      const order = q.order === 'near' ? 'clickcount' : q.order;
      const base = { limit: 40, offset: q.off || 0, hidebroken: true, order, reverse: order !== 'name' };
      // In a page that came by https (the remote console) a browser plays https stations only:
      // the others are not asked for, rather than shown and then refused.
      if (location.protocol === 'https:') base.is_https = true;
      // The place of the device, to the tenth of a degree: enough for the stations around.
      if (q.order === 'near' && typeof q.lat === 'number') Object.assign(base, { geo_lat: q.lat.toFixed(1), geo_long: q.lon.toFixed(1), geo_distance: 90000 });
      else if (q.country) base.countrycode = q.country;
      if (q.genre) Object.assign(base, { tag: q.genre, tagExact: true });
      // The words are looked for in the names and, with no genre chosen, among the genres too.
      const asks = !q.text ? [{}] : q.genre ? [{ name: q.text }] : [{ name: q.text }, { tag: low(q.text) }];
      const got = await Promise.all(asks.map((x) => rb('stations/search?' + new URLSearchParams(Object.assign({}, base, x)))));
      const seen = new Set();
      const all = got.flat().filter((s) => !seen.has(s.stationuuid) && seen.add(s.stationuuid));
      if (got.length > 1) all.sort((x, y) => (order === 'name' ? x.name.localeCompare(y.name) : y[order] - x[order]));
      const list = all.map((s) => station({ i: 'r' + s.stationuuid.replace(/-/g, '').slice(0, 16), uuid: s.stationuuid, n: s.name,
        u: s.url_resolved || s.url, l: s.favicon, h: s.homepage, k: s.countrycode, g: s.tags,
        q: [s.codec !== 'UNKNOWN' && s.codec, s.bitrate || ''].filter(Boolean).join(' '), hls: s.hls }));
      list.more = got.some((l) => l.length > 39);
      return list;
    },
  },
  sf: {
    name: 'SomaFM',
    async find(q) {
      if (q.off || (q.country && q.country !== 'US')) return [];
      soma = soma || (await getJson('https://somafm.com/channels.json')).channels.map((c) => station({ i: 's' + c.id, n: c.title,
        u: 'https://ice1.somafm.com/' + c.id + '-128-mp3', l: c.image, h: 'https://somafm.com/' + c.id + '/', k: 'US', g: c.genre, q: 'MP3 128' }));
      return soma.filter((s) => has(s, q.text) && has(s, q.genre));
    },
  },
  lf: {
    name: 'laut.fm',
    async find(q) {
      if (q.country && q.country !== 'DE') return [];
      // It has no search by name: the words are tried as a genre, and its genres begin with a
      // capital. With an offset the answer is an object with the list inside, without it the list.
      const g = (q.genre || q.text).replace(/(^|[\s-])\S/g, (c) => c.toUpperCase());
      const d = await getJson('https://api.laut.fm/stations' + (g ? '/genre/' + encodeURIComponent(g) : '') + '?limit=40&offset=' + (q.off || 0));
      const list = (d.items || d).map((s) => station({ i: 'l' + s.name, n: s.display_name, u: s.stream_url, l: s.images && s.images.station, h: s.page_url,
        k: 'DE', g: (s.genres || []).join(','), q: 'MP3 128' })).filter((s) => !q.genre || has(s, q.text));
      list.more = (d.items || d).length > 39;
      return list;
    },
  },
  rs: {
    name: '',
    find: async (q) => (q.off ? [] : RESERVE.map((r) => station({ i: 'u' + hid(r[1]), n: r[0], u: r[1], g: r[2], k: r[3] }))
      .filter((s) => has(s, q.text) && has(s, q.genre) && (!q.country || s.k === q.country))),
  },
};
const ORDER = ['rb', 'sf', 'lf', 'rs'];

// ---------------------------------------------------------------- the radio

// One for the page. Two players: one through the equalizer, for the stations that let the page
// read their sound (CORS), one straight to the speakers for the others, which still play.
// ui: what frame() moves, the canvas of the spectrum, the two levels, the display.
const R = { st: null, state: 'stop', cors: false, err: '', song: '', list: [], zap: '', scan: false, scanSec: 8, sleep: 0, sleepAt: 0, mute: false,
  vol: 0.7, eq: PRESETS.flat.slice(), pre: 'flat', byp: false, lv: [0, 0], pk: [], skin: '', hue: HUES[0], subs: new Set(), ui: { m: [], cv: null, d: null } };
{
  const v = ls('vol');
  const e = ls('eq');
  if (typeof v === 'number') R.vol = v;
  if (e && e.length === 10) R.eq = e;
  R.pre = ls('pre') || (e ? '' : 'flat');
  R.byp = !!ls('byp');
  if (LOOKS.includes(ls('look'))) R.skin = ls('look');
  if (HUES.includes(ls('hue'))) R.hue = ls('hue');
}
const tell = () => R.subs.forEach((fn) => fn());

function bind(el) {
  el.preload = 'none';
  el.addEventListener('playing', () => {
    if (el !== R.el) return;
    clearTimeout(R.tm);
    R.state = 'play';
    tell();
    frame();
    if (R.scan) R.tz = setTimeout(() => next(1, true), R.scanSec * 1000);
  });
  el.addEventListener('waiting', () => {
    if (el !== R.el || R.state !== 'play') return;
    R.state = 'wait';
    tell();
  });
  el.addEventListener('error', () => {
    if (el === R.el && el.getAttribute('src')) fail('down');
  });
  return el;
}

// The equalizer: ten filters in a row, then the spectrum and a level for each channel.
function graph() {
  const AC = window.AudioContext || window.webkitAudioContext;
  if (R.ac || !AC) return;
  R.ac = new AC();
  R.a = bind(new Audio());
  R.a.crossOrigin = 'anonymous';
  let node = R.ac.createMediaElementSource(R.a);
  R.f = FREQ.map((hz, i) => {
    const f = R.ac.createBiquadFilter();
    f.type = i === 0 ? 'lowshelf' : i === 9 ? 'highshelf' : 'peaking';
    f.frequency.value = hz;
    f.Q.value = 1.1;
    f.gain.value = R.eq[i];
    node.connect(f);
    node = f;
    return f;
  });
  // Two channels also from a station in mono: a level each.
  const out = R.ac.createGain();
  out.channelCount = 2;
  out.channelCountMode = 'explicit';
  node.connect(out);
  out.connect(R.ac.destination);
  R.an = R.ac.createAnalyser();
  R.an.fftSize = 512;
  R.an.smoothingTimeConstant = 0.78;
  out.connect(R.an);
  const split = R.ac.createChannelSplitter(2);
  out.connect(split);
  R.vu = [0, 1].map((c) => {
    const a = R.ac.createAnalyser();
    a.fftSize = 256;
    split.connect(a, c);
    return a;
  });
}

// From a page in https a station in http is asked in https: many answer there too.
const fit = (url) => (location.protocol === 'https:' ? url.replace(/^http:/, 'https:') : url);

function begin(cors) {
  const el = cors && R.a ? R.a : R.b || (R.b = bind(new Audio()));
  R.el = el;
  R.cors = el === R.a;
  el.volume = R.vol;
  el.muted = R.mute;
  el.src = fit(R.st.u);
  el.play().catch(() => {});
  clearTimeout(R.tm);
  R.tm = setTimeout(() => fail('down'), cors ? 6000 : 9000);
}
function quiet(el) {
  if (!el || !el.getAttribute('src')) return;
  el.pause();
  el.removeAttribute('src');
  el.load();
}
function halt() {
  clearTimeout(R.tm);
  clearTimeout(R.tz);
  clearInterval(R.ts);
  R.el = null;
  quiet(R.a);
  quiet(R.b);
}
function fail(why) {
  // A station that does not let the page read its sound still plays, without the equalizer.
  if (R.cors) {
    R.el = null;
    quiet(R.a);
    begin(false);
    return;
  }
  halt();
  R.state = 'err';
  R.err = /^http:/.test(R.st.u) && location.protocol === 'https:' ? 'http' : why;
  tell();
  // While scanning a silent station is passed over.
  if (R.scan) R.tz = setTimeout(() => next(1, true), 1200);
}

function play(st) {
  halt();
  Object.assign(R, { st, err: '', song: '', state: 'wait' });
  const hls = st.hls || /\.m3u8(\?|$)/i.test(st.u);
  if (hls && !document.createElement('audio').canPlayType('application/vnd.apple.mpegurl')) {
    R.state = 'err';
    R.err = 'hls';
    tell();
    if (R.scan) R.tz = setTimeout(() => next(1, true), 1200);
    return;
  }
  if (!R.byp) graph();
  if (R.ac && R.ac.state === 'suspended') R.ac.resume();
  // On Safari and on every browser of an iPhone an HLS station through the equalizer is silent.
  begin(!R.byp && !(hls && /AppleWebKit/.test(navigator.userAgent) && !/Chrom|Android/.test(navigator.userAgent)));
  // The last ones listened to, the newest first.
  ls('rec', [st].concat((ls('rec') || []).filter((s) => s.i !== st.i)).slice(0, 20));
  // Radio Browser counts who listens: its lists of the most listened come from here.
  if (st.uuid) rb('url/' + st.uuid).catch(() => {});
  // What is on air, from the directories that tell it.
  const song = async () => {
    try {
      if (document.hidden || R.st !== st) return;
      const x = st.i[0] === 's' ? (await getJson('https://somafm.com/songs/' + st.i.slice(1) + '.json')).songs[0]
        : await getJson('https://api.laut.fm/station/' + st.i.slice(1) + '/current_song');
      const text = [x.artist && (x.artist.name || x.artist), x.title].filter(Boolean).join(' - ');
      if (R.st === st && text !== R.song) {
        R.song = text;
        session();
        tell();
      }
    } catch (e) { /* no title this time */ }
  };
  if (/^[sl]/.test(st.i)) {
    song();
    R.ts = setInterval(song, 20000);
  }
  session();
  tell();
}
function stop() {
  halt();
  R.state = 'stop';
  R.scan = false;
  tell();
}
// Before and after, in the list in sight: the stations found, the favourites, one category.
function next(d, auto) {
  if (!auto) R.scan = false;
  const l = R.list;
  if (!l.length) return;
  const i = l.findIndex((s) => R.st && s.i === R.st.i);
  play(l[(i + d + l.length) % l.length]);
}
// The buttons of the lock screen and of the headphones.
function session() {
  const ms = navigator.mediaSession;
  if (!ms || !window.MediaMetadata || !R.st) return;
  ms.metadata = new MediaMetadata({ title: R.st.n, artist: R.song || R.st.t || '', album: 'EspRadio', artwork: R.st.l ? [{ src: R.st.l }] : [] });
  [['play', () => play(R.st)], ['pause', stop], ['stop', stop], ['previoustrack', () => next(-1)], ['nexttrack', () => next(1)]]
    .forEach(([name, fn]) => {
      try {
        ms.setActionHandler(name, fn);
      } catch (e) { /* not on this browser */ }
    });
}
function setVol(v) {
  R.vol = v;
  ls('vol', v);
  if (R.el) R.el.volume = v;
  tell();
}
function setEq(gains, pre) {
  R.eq = gains;
  R.pre = pre;
  ls('eq', gains);
  ls('pre', pre);
  if (R.f) R.f.forEach((f, i) => { f.gain.value = gains[i]; });
  tell();
}
function setSleep() {
  const steps = [0, 15, 30, 60, 90];
  R.sleep = steps[(steps.indexOf(R.sleep) + 1) % steps.length];
  R.sleepAt = R.sleep ? Date.now() + R.sleep * 60000 : 0;
  clearTimeout(R.tl);
  if (R.sleep) {
    R.tl = setTimeout(() => {
      R.sleep = R.sleepAt = 0;
      stop();
    }, R.sleep * 60000);
  }
  tell();
}
// The next of a row, and kept: the look, the colour of the lights.
function turn(key, list, name) {
  R[key] = list[(list.indexOf(R[key]) + 1) % list.length];
  ls(name, R[key]);
  tell();
  frame();
}

// The spectrum: bars of segments in the colours of the look. The car stereo and the neon show
// the unlit ones too, and a cap stays a moment where a bar arrived.
function paint(cv, bins) {
  const g = cv.getContext('2d');
  const w = cv.width;
  const h = cv.height;
  const n = w < 500 ? 24 : 40;
  const bw = w / n;
  const sh = h / 12;
  const amp = R.skin === LOOKS[1];
  g.clearRect(0, 0, w, h);
  for (let b = 0; b < n; b++) {
    let v = 0;
    if (bins) {
      const lo = Math.floor(Math.pow(200, b / n));
      const hi = Math.max(lo + 1, Math.floor(Math.pow(200, (b + 1) / n)));
      for (let k = lo; k < hi; k++) v = Math.max(v, bins[k]);
    }
    const lit = Math.round((v / 255) * 12);
    R.pk[b] = Math.max(lit, (R.pk[b] || 0) - 0.15);
    for (let s = 0; s < (amp ? lit : 12); s++) {
      g.fillStyle = s >= lit ? (R.pk[b] > 0 && s === Math.floor(R.pk[b]) ? '#fff' : R.skin ? '#1c0f2c' : '#14171c')
        : amp ? (s > 9 ? '#ff3b30' : s > 7 ? '#ffd23a' : '#ffb000')
          : 'hsl(' + (R.skin ? 190 + (11 - s) * 10 : R.hue + (b * 150) / n) + ' 100% 58%)';
      g.fillRect(b * bw + 1, h - (s + 1) * sh + 1, bw - 2, sh - 2);
    }
  }
}
// The spectrum, the levels and the light of the display, at the pace of the screen, only while
// a station plays through the equalizer and its panel is in sight.
function rest() {
  const u = R.ui;
  R.pk = [];
  if (u.cv) paint(u.cv, null);
  u.m.forEach((m) => { if (m) m.style.cssText = ''; });
  if (u.d) u.d.style.cssText = '';
}
function frame() {
  if (R.raf) return;
  const bins = new Uint8Array(256);
  const wave = new Uint8Array(256);
  const loop = () => {
    R.raf = 0;
    const u = R.ui;
    if (!u.cv || R.state !== 'play' || !R.cors) {
      rest();
      return;
    }
    R.raf = requestAnimationFrame(loop);
    R.an.getByteFrequencyData(bins);
    paint(u.cv, bins);
    R.vu.forEach((a, c) => {
      a.getByteTimeDomainData(wave);
      let sum = 0;
      for (let k = 0; k < 256; k++) sum += ((wave[k] - 128) / 128) ** 2;
      const level = Math.max(0, Math.min(1, (10 * Math.log10(sum / 256 + 1e-9) + 42) / 38));
      R.lv[c] += (level - R.lv[c]) * 0.22;
      // A bar of light, or a needle for the amplifier.
      if (u.m[c]) u.m[c].style.cssText = 'clip-path:inset(0 ' + (100 - R.lv[c] * 100) + '% 0 0)';
      if (u.m[c + 2]) u.m[c + 2].style.cssText = 'transform:rotate(' + (-48 + R.lv[c] * 96) + 'deg)';
    });
    if (u.d) u.d.style.cssText = '--lv:' + ((R.lv[0] + R.lv[1]) / 2).toFixed(2);
  };
  loop();
}

// A component that follows the radio: drawn again when it tells.
function useRadio(ctx) {
  const [, set] = ctx.useState(0);
  ctx.useEffect(() => {
    const fn = () => set((n) => n + 1);
    R.subs.add(fn);
    return () => R.subs.delete(fn);
  }, []);
  return R;
}

// ---------------------------------------------------------------- the favourites, on the device

const F = { list: null, max: 0 };
async function favLoad(ctx) {
  let all = [];
  let from = 0;
  for (let page = 0; page < 80 && from !== undefined; page++) {
    const r = await ctx.read('favs', { from });
    if (!r.ok || !r.result) break;
    all = all.concat(r.result.favs || []);
    F.max = r.result.max;
    from = r.result.next;
  }
  F.list = all;
  tell();
}
const isFav = (s) => !!s && (F.list || []).some((f) => f.i === s.i);
// A command of the device carries little text: name and address first, then logo and info.
async function favAdd(ctx, s, cat, okText) {
  const r = await ctx.act('fav.set', { id: s.i, name: s.n, url: s.u, cat: cat || '' }, okText);
  if (!r.ok) return false;
  const more = { id: s.i, info: s.t || '' };
  if (s.l && new TextEncoder().encode(s.l).length < 120) more.logo = s.l;
  await ctx.command('fav.set', more);
  return true;
}
async function favToggle(ctx, s, T) {
  if (isFav(s)) await ctx.act('fav.remove', { id: s.i }, T('removed'));
  else await favAdd(ctx, s, '', T('saved'));
  favLoad(ctx);
}

// ---------------------------------------------------------------- the head unit

function Amp({ ctx, T }) {
  const { html, ui, useEffect, useState } = ctx;
  const r = useRadio(ctx);
  const st = r.st;
  const amp = r.skin === LOOKS[1];
  const [, beat] = useState(0);
  // The minutes left of the timer, told again now and then.
  useEffect(() => {
    if (!r.sleepAt) return undefined;
    const timer = setInterval(() => beat((n) => n + 1), 20000);
    return () => clearInterval(timer);
  }, [r.sleepAt]);
  useEffect(() => { frame(); }, []);
  const at = st ? r.list.findIndex((s) => s.i === st.i) : -1;
  const favs = F.list || [];
  // The volume as the look counts it: an amplifier goes to eleven.
  const top = amp ? 11 : 30;
  // The needles have places of their own: when the look changes, the bars that leave do not
  // take them away.
  const meter = (c) => (e) => { R.ui.m[amp ? c + 2 : c] = e; };
  const btn = (icon, title, fn, cls, text, data) => html`<button type="button" class=${'rk-b ' + (cls || '')} title=${title} aria-label=${title}
    onClick=${fn} data-rk=${data}><${ui.Icon} name=${icon} size=${cls && cls.includes('big') ? 24 : 18} cls=${icon === 'rk-star' ? 'rk-f' : ''}/>${text}</button>`;
  // A lamp of the display: its word always there, lit when it is so.
  const lamp = (text, on, cls, data) => html`<span class=${'rk-lp' + (on ? ' on ' + (cls || '') : '')} data-rk=${data}>${text}</span>`;
  const vu = (c) => html`<svg class="rk-vu" viewBox="0 0 60 40" aria-hidden="true"><path d="M8 30A24 24 0 0 1 40 14"/><path class="r" d="M40 14A24 24 0 0 1 52 30"/>
    <text x="30" y="37">${c ? 'R' : 'L'}</text><line ref=${meter(c)} x1="30" y1="36" x2="30" y2="11"/></svg>`;
  return html`<div class="rk-p" data-rk="amp">
    <div class="rk-h"><span class="rk-logo">Esp<b>Radio</b></span><span class="rk-tag rk-sub">${T('tag')}</span>
      <span class="rk-sp rk-tag">${r.zap}${at >= 0 ? ' ' + (at + 1) + '/' + r.list.length : ''}</span>
      ${!r.skin && btn('sun', T('hue'), () => turn('hue', HUES, 'hue'), 'sm', '', 'hue')}
      ${btn('layout-grid', T('look'), () => turn('skin', LOOKS, 'look'), 'sm', '', 'look')}</div>
    <div class="rk-d" ref=${(e) => { R.ui.d = e; }}>
      ${st && st.l ? html`<img class="rk-art" src=${st.l} alt="" key=${st.l} onError=${(e) => { e.target.style.visibility = 'hidden'; }}/>`
        : html`<span class="rk-art"><${ui.Icon} name="radio" size=${30}/></span>`}
      <div style="min-width:0">
        <div class="rk-n" data-rk="name">${st ? st.n : 'EspRadio'}</div>
        <div class=${'rk-s' + (r.state === 'err' ? ' err' : '')}>${r.state === 'err' ? T('e.' + r.err) : r.song || (st ? st.t : T('pick'))}</div>
        <div class="rk-st">
          ${lamp(T('st.' + r.state), r.state !== 'stop', r.state === 'wait' ? 'rk-am rk-wait' : 'rk-rd', 'state')}
          ${lamp('EQ', r.state === 'play' && !r.byp, r.cors ? 'rk-gn' : 'rk-rd')}
          ${lamp(T('b.scan'), r.scan)}
          ${lamp(T('b.sleep') + (r.sleepAt ? ' ' + Math.max(1, Math.round((r.sleepAt - Date.now()) / 60000)) + "'" : ''), r.sleepAt > 0)}
          ${lamp(T('b.mute'), r.mute, 'rk-rd')}
        </div>
      </div>
      ${amp ? html`<div class="rk-vus">${vu(0)}${vu(1)}</div>`
        : html`<div class="rk-lv"><span>L</span><i><b ref=${meter(0)}></b></i><span>R</span><i><b ref=${meter(1)}></b></i></div>`}
    </div>
    <div class="rk-c">
      ${btn('rk-prev', T('prev'), () => next(-1))}
      ${r.state === 'stop' || r.state === 'err' ? btn('play', T('play'), () => (st ? play(st) : next(1)), 'big', '', 'play')
        : btn('rk-stop', T('stop'), stop, 'big' + (r.state === 'wait' ? ' rk-wait' : ''), '', 'stop')}
      ${btn('rk-next', T('next'), () => next(1))}
      <label class="rk-k" title=${T('vol')} style=${'--v:' + r.vol}><i></i><b>${Math.round(r.vol * top)}</b>
        <input type="range" min="0" max=${top} step="1" value=${Math.round(r.vol * top)} aria-label=${T('vol')}
          onInput=${(e) => setVol(Number(e.target.value) / top)} data-rk="vol"/></label>
      ${btn('radio', T('scan', { n: r.scanSec }), () => { R.scan = !R.scan; if (R.scan) next(1, true); else { clearTimeout(R.tz); tell(); } },
        r.scan ? 'lit' : '', T('b.scan'), 'scan')}
      ${btn('timer', T('sleep'), setSleep, r.sleep ? 'lit' : '', r.sleep ? r.sleep + "'" : T('b.sleep'), 'sleep')}
      ${btn('rk-star', T('fav'), () => st && favToggle(ctx, st, T), isFav(st) ? 'lit' : '', '', 'fav')}
      ${btn(r.mute ? 'rk-mute' : 'rk-vol', T('mute'), () => { R.mute = !R.mute; if (R.el) R.el.muted = R.mute; tell(); }, r.mute ? 'lit' : '')}
    </div>
    <div class="rk-pr">${[0, 1, 2, 3, 4, 5].map((i) => {
      // The six memories: the first favourites. An empty one takes the station on air.
      const f = favs[i];
      return html`<button type="button" key=${i} class=${'rk-b' + (f && st && f.i === st.i && r.state !== 'stop' ? ' lit' : '')}
        title=${T('mem', { n: i + 1 })} disabled=${!f && (!st || isFav(st))} onClick=${() => (f ? play(f) : favToggle(ctx, st, T))}
        data-rk-mem=${i}><b>${i + 1}</b><small>${f ? f.n : '+'}</small></button>`;
    })}</div>
  </div>`;
}

function Eq({ ctx, T }) {
  const { html, useRef, useEffect } = ctx;
  const r = useRadio(ctx);
  const cv = useRef(null);
  useEffect(() => {
    const c = cv.current;
    const size = () => {
      c.width = c.clientWidth * Math.min(2, window.devicePixelRatio || 1);
      c.height = c.clientHeight * Math.min(2, window.devicePixelRatio || 1);
      frame();
    };
    R.ui.cv = c;
    size();
    const ro = new ResizeObserver(size);
    ro.observe(c);
    return () => {
      ro.disconnect();
      R.ui.cv = null;
    };
  }, []);
  // Lit while the sound passes through it; off, and said, for a station that does not allow it.
  const live = r.state === 'play' && r.cors;
  const no = r.state === 'play' && !r.cors;
  return html`<div class="rk-p" data-rk="eq">
    <div class="rk-h"><span class=${'rk-led' + (live ? ' on rk-gn' : no ? ' on rk-rd' : '')}></span><span class="rk-tag">${T('eq')}</span>
      <span class="rk-chips rk-sp">
        ${Object.keys(PRESETS).map((p) => html`<button type="button" key=${p} class=${'rk-ch' + (r.pre === p ? ' on' : '')}
          onClick=${() => setEq(PRESETS[p].slice(), p)} data-rk-pre=${p}>${T('p.' + p)}</button>`)}
        <button type="button" class=${'rk-ch' + (r.byp ? '' : ' on')} title=${T('byp')} data-rk="byp"
          onClick=${() => { R.byp = !R.byp; ls('byp', R.byp); if (R.st && R.state !== 'stop') play(R.st); else tell(); }}>EQ</button>
      </span></div>
    <div class=${no || r.byp ? 'rk-off' : ''}>
      <canvas class="rk-cv" ref=${cv} aria-hidden="true"></canvas>
      <div class="rk-eqs">${FREQ.map((hz, i) => html`<label key=${hz}><input type="range" min="-12" max="12" step="1" value=${r.eq[i]}
        aria-label=${hz + ' Hz'} onInput=${(e) => { const g = r.eq.slice(); g[i] = Number(e.target.value); setEq(g, ''); }}/>
        <small>${hz < 1000 ? hz : hz / 1000 + 'k'}</small></label>`)}</div>
    </div>
    ${(no || r.byp) && html`<p class="rk-note">${T(r.byp ? 'eqByp' : 'eqNo')}</p>`}
  </div>`;
}

// ---------------------------------------------------------------- the stations

// A question with one or two answers, in a window of the console.
function Ask({ ctx, title, fields, onClose, onDone }) {
  const { html, ui, t, useState } = ctx;
  const [v, setV] = useState(fields.map((f) => f.value || ''));
  const done = () => { if (v[0].trim()) onDone(v.map((x) => x.trim())); };
  return html`<${ui.Modal} title=${title} icon="radio" onClose=${onClose}>
    <div class=${'rk ' + R.skin} style=${'--h:' + R.hue + ';border:0;box-shadow:none;background:none;padding:0'}>
      ${fields.map((f, i) => html`<input key=${i} class="rk-in" style="flex:none;width:100%" value=${v[i]} placeholder=${f.label} aria-label=${f.label}
        maxLength=${f.max} onInput=${(e) => setV(v.map((x, k) => (k === i ? e.target.value : x)))}
        onKeyDown=${(e) => { if (e.key === 'Enter') done(); }}/>`)}
      <${ui.Btn} kind="primary" icon="check" onClick=${done}>${t('btn.save')}<//>
    </div>
  <//>`;
}

function Stations({ ctx, T, doc }) {
  const { html, ui, useState, useEffect, useRef } = ctx;
  const r = useRadio(ctx);
  const geo = ctx.useSysDoc('sys.geo', 0) || {};
  const [mode, setMode] = useState('st');
  const [src, setSrc] = useState('all');
  const [q, setQ] = useState({ text: '', genre: '', country: null, order: 'clickcount' });
  const [rows, setRows] = useState(null);
  const [more, setMore] = useState(false);
  const [note, setNote] = useState('');
  const [dead, setDead] = useState({});
  const [open, setOpen] = useState('');
  const [flt, setFlt] = useState('');
  const [tags, setTags] = useState([]);
  const [cat, setCat] = useState('');
  const [ask, setAsk] = useState(null);
  const run = useRef(0);
  const last = useRef(null);
  // The country the search starts from: the one of the settings, else the one of the browser.
  const home = (doc.country || (navigator.language || '').split('-')[1] || '').toUpperCase();
  const country = q.country === null ? home : q.country;
  const name = (id) => (id === 'all' ? T('all') : SRC[id].name || T('reserve'));
  const landName = (k) => ((lands || []).find((l) => l.k === k) || { n: k }).n;
  // The directories with stations of every country: the others have one country only.
  const wide = (id) => id === 'all' || id === 'rb';

  // One question to every directory at once, or to the one chosen; the reserve when the large
  // one is silent: there is always something to listen to. A country written as the text is the
  // country. When the words find nothing as they are, the last one that says something is
  // asked alone. A country with little to give is followed by the rest of the world.
  const find = async (from, ask2, page) => {
    const mine = ++run.current;
    const ids = from !== 'all' ? [from] : ask2.order === 'near' ? ['rb'] : ['rb', 'sf', 'lf'];
    const state = {};
    let full = false;
    const each = async (a) => (await Promise.all(ids.map((id) => SRC[id].find(a).then((l) => {
      state[id] = false;
      full = full || !!l.more;
      return l;
    }, () => {
      state[id] = true;
      return [];
    })))).flat();
    let a = last.current;
    let list;
    let told = '';
    let land = null;
    if (page) {
      a.off += 40;
      list = await each(a);
    } else {
      setNote('');
      setRows(null);
      a = Object.assign({ lat: geo.lat, lon: geo.lon }, ask2, { off: 0, text: ask2.text.trim(), country: wide(from) ? (ask2.country === null ? home : ask2.country) : '' });
      if (a.text && wide(from)) land = (await landList(ctx.lang()).catch(() => [])).find((l) => low(l.n) === low(a.text) || low(l.e) === low(a.text));
      if (land) Object.assign(a, { text: '', country: land.k });
      list = await each(a);
      const word = low(a.text).replace(NOISE, ' ').trim().split(/\s+/).pop();
      if (!list.length && word && word !== low(a.text)) {
        a.text = word;
        list = await each(a);
      }
      if (list.length < 12 && a.country && (a.text || a.genre) && a.order !== 'near') {
        told = T('wide', { name: landName(a.country) });
        a.country = '';
        full = false;
        list = list.concat(await each(a));
      }
      if (from === 'all' && state.rb) {
        // Without the directory of every country the others say all they have, whatever the
        // country, and the reserve follows: the whole of it when nothing else is there.
        told = T('rbDown');
        a.country = '';
        list = (await each(a)).concat(await SRC.rs.find(a));
        if (!list.length) list = await SRC.rs.find({});
      } else if (state[from]) {
        told = T('srcDown', { name: name(from) });
      }
      last.current = a;
    }
    if (mine !== run.current) return;
    if (land) setQ((x) => Object.assign({}, x, { text: '', country: land.k }));
    setDead((d) => Object.assign({}, d, state));
    setSrc(from);
    setMore(full);
    setNote(told);
    setRows((old) => {
      const seen = new Set();
      return (page && old ? old.concat(list) : list).filter((s) => !seen.has(s.u) && seen.add(s.u));
    });
  };
  const go = (change, from) => {
    const nq = Object.assign({}, q, change);
    setQ(nq);
    setOpen('');
    find(from || src, nq);
  };
  useEffect(() => {
    favLoad(ctx);
    find('all', q);
    landList(ctx.lang()).then(tell, () => {});
  }, []);
  useEffect(() => { R.scanSec = doc.scanSec || 8; }, [doc.scanSec]);

  // Genres and countries are very many: a text that filters them, the first ones as keys. The
  // genres written here first, then the ones Radio Browser has with those letters.
  const f = low(flt).trim();
  useEffect(() => {
    if (open !== 'g' || f.length < 2) return undefined;
    let on = true;
    const timer = setTimeout(() => rb('tags/' + encodeURIComponent(f) + '?order=stationcount&reverse=true&limit=30&hidebroken=true')
      .then((l) => on && setTags(l.map((x) => x.name)), () => {}), 350);
    return () => {
      on = false;
      clearTimeout(timer);
    };
  }, [open, f]);
  const opts = (open === 'g' ? [...new Set(GENRES.concat(tags))].filter((g) => g.includes(f)).map((g) => [g, g])
    : (lands || []).filter((l) => low(l.n + ' ' + l.e).includes(f)).map((l) => [l.k, flag(l.k) + ' ' + l.n])).slice(0, 40);
  const choose = (v) => {
    setFlt('');
    go(open === 'g' ? { genre: v } : { country: v });
  };

  // The categories: the ones of the settings in their order, then the ones only the favourites have.
  const favs = F.list || [];
  const cats = [...new Set((doc.cats || '').split(',').map((x) => x.trim()).filter(Boolean).concat(favs.map((x) => x.c).filter(Boolean)))];
  const saveCats = (list) => ctx.command('config.set', { cats: list.join(',') });
  const shown = mode === 'st' ? rows || [] : mode === 'rec' ? ls('rec') || [] : favs.filter((x) => !cat || (cat === '-' ? !x.c : x.c === cat));
  // Before and after follow the list in sight.
  useEffect(() => {
    R.list = shown;
    R.zap = mode === 'st' ? name(src) : mode === 'rec' ? T('m.rec') : T('m.fav') + (cat && cat !== '-' ? ' · ' + cat : '');
    tell();
  }, [mode, src, cat, rows, F.list]);

  const setFavCat = async (s, to) => {
    if (to === '+') {
      setAsk({ title: T('newCat'), fields: [{ label: T('catName'), max: 19 }], done: async ([n]) => {
        const c = cut(n.replace(/,/g, ' '), 19);
        await saveCats(cats.includes(c) ? cats : cats.concat(c));
        if (s) await ctx.command('fav.set', { id: s.i, cat: c });
        favLoad(ctx);
      } });
      return;
    }
    await ctx.command('fav.set', { id: s.i, cat: to });
    favLoad(ctx);
  };
  const renameCat = () => setAsk({ title: T('renameCat'), fields: [{ label: T('catName'), max: 19, value: cat }], done: async ([n]) => {
    const c = cut(n.replace(/,/g, ' '), 19);
    await ctx.command('cat.rename', { from: cat, to: c });
    await saveCats(cats.map((x) => (x === cat ? c : x)));
    setCat(c);
    favLoad(ctx);
  } });
  const dropCat = async () => {
    if (!(await ctx.confirm(T('dropCat') + ': ' + cat + '?'))) return;
    await ctx.command('cat.rename', { from: cat, to: '' });
    await saveCats(cats.filter((x) => x !== cat));
    setCat('');
    favLoad(ctx);
  };
  const custom = () => setAsk({ title: T('custom'), fields: [{ label: T('customName'), max: 43 }, { label: T('customUrl'), max: 159 }],
    done: async ([n, u]) => {
      if (await favAdd(ctx, station({ i: 'u' + hid(u), n, u }), cat !== '-' ? cat : '', T('saved'))) favLoad(ctx);
    } });
  const save = () => {
    const a = document.createElement('a');
    a.href = URL.createObjectURL(new Blob([JSON.stringify({ espradio: 1, cats, favs }, null, 1)], { type: 'application/json' }));
    a.download = 'espradio.json';
    a.click();
  };
  const load = async (e) => {
    try {
      const data = JSON.parse(await e.target.files[0].text());
      let n = 0;
      for (const x of data.favs || []) {
        if (x && x.i && x.n && x.u && !isFav(x) && await favAdd(ctx, x, x.c)) n++;
      }
      await saveCats([...new Set(cats.concat(data.cats || []))]);
      ctx.toast(T('imported', { n }));
    } catch (x) {
      ctx.toast(T('badFile'));
    }
    e.target.value = '';
    favLoad(ctx);
  };

  const row = (s) => {
    const now = r.st && r.st.i === s.i && r.state !== 'stop';
    const tap = () => (now ? stop() : play(s));
    return html`<div class=${'rk-r' + (now ? ' now' : '')} key=${s.i} role="listitem" tabindex="0" onClick=${tap}
        onKeyDown=${(e) => { if (e.key === 'Enter') tap(); }} data-rk-st=${s.i}>
      ${s.l ? html`<img src=${s.l} alt="" loading="lazy" onError=${(e) => { e.target.style.visibility = 'hidden'; }}/>`
        : html`<span class="ph"><${ui.Icon} name="radio" size=${20}/></span>`}
      <div class="rk-rn">${now && r.state === 'play' && html`<span class="rk-bars"><i></i><i></i><i></i></span>`}${s.n}</div>
      <div class="rk-ra" onClick=${(e) => e.stopPropagation()} onKeyDown=${(e) => e.stopPropagation()}>
        ${mode === 'fav' && html`<select value=${s.c || ''} aria-label=${T('cat')} onChange=${(e) => setFavCat(s, e.target.value)}>
          <option value="">${T('noCat')}</option>
          ${cats.map((c) => html`<option key=${c} value=${c}>${c}</option>`)}
          <option value="+">${T('newCat')}…</option></select>`}
        ${s.h && html`<a class="rk-b sm" href=${s.h} target="_blank" rel="noopener" title=${T('site')} aria-label=${T('site')}><${ui.Icon} name="globe" size=${16}/></a>`}
        <button type="button" class=${'rk-b sm' + (isFav(s) ? ' lit' : '')} title=${T('fav')} aria-label=${T('fav')}
          onClick=${() => favToggle(ctx, s, T)} data-rk-fav=${s.i}><${ui.Icon} name="rk-star" size=${16} cls="rk-f"/></button>
      </div>
      <div class="rk-rs">${s.t}</div>
    </div>`;
  };
  const chip = (on, text, fn, data, led) => html`<button type="button" class=${'rk-ch' + (on ? ' on' : '')} onClick=${fn} data-rk-chip=${data}>
    ${led}${text}</button>`;
  // A key that opens the choice of a genre or of a country, and says the one chosen.
  const key = (id, on, text) => chip(on, html`${text}<${ui.Icon} name="chevron-down" size=${12}/>`, () => { setOpen(open === id ? '' : id); setFlt(''); }, id);
  return html`<div class="rk-p" data-rk="list">
    <div class="rk-h rk-chips">
      ${chip(mode === 'st', T('m.st'), () => setMode('st'), 'st')}
      ${chip(mode === 'fav', T('m.fav') + ' ' + favs.length, () => setMode('fav'), 'fav')}
      ${chip(mode === 'rec', T('m.rec'), () => setMode('rec'), 'rec')}
    </div>
    ${mode === 'st' && html`<div class="rk-chips">
      ${['all'].concat(ORDER).map((id) => chip(src === id, name(id), () => { setOpen(''); find(id, q); }, id,
        id !== 'all' && html`<span class=${'rk-led' + (dead[id] ? ' on rk-rd' : dead[id] === false ? ' on rk-gn' : '')}></span>`))}
    </div>
    <form class="rk-bar" onSubmit=${(e) => { e.preventDefault(); go({}); }}>
      <input class="rk-in" value=${q.text} placeholder=${T('text')} aria-label=${T('text')} enterkeyhint="search"
        onInput=${(e) => setQ(Object.assign({}, q, { text: e.target.value }))} data-rk="text"/>
      <button class="rk-b" title=${T('search')} aria-label=${T('search')} data-rk="find"><${ui.Icon} name="search" size=${18}/></button>
    </form>
    <div class="rk-chips">
      ${key('g', q.genre, T('genre') + ': ' + (q.genre || T('any')))}
      ${wide(src) && key('c', country, country ? flag(country) + ' ' + landName(country) : T('world'))}
      ${wide(src) && html`<select value=${q.order} onChange=${(e) => go({ order: e.target.value })} aria-label=${T('order')} data-rk="order">
        ${['clickcount', 'votes', 'clicktrend', 'name', 'near'].map((o) => html`<option key=${o} value=${o}>${T('o.' + o)}</option>`)}</select>`}
    </div>
    ${open && html`<div class="rk-pk" data-rk="pick">
      <input class="rk-in" value=${flt} placeholder=${T('f' + open)} aria-label=${T('f' + open)} onInput=${(e) => setFlt(e.target.value)}
        onFocus=${(e) => e.target.parentNode.scrollIntoView({ block: 'start', behavior: 'smooth' })} data-rk="flt"
        onKeyDown=${(e) => { if (e.key === 'Enter' && (opts[0] || (open === 'g' && f))) choose(opts[0] ? opts[0][0] : f); }}/>
      <div class="rk-chips">
        ${chip(!(open === 'g' ? q.genre : country), T(open === 'g' ? 'any' : 'world'), () => choose(''), '')}
        ${opts.map(([v, text]) => html`<button type="button" key=${v} class=${'rk-ch' + ((open === 'g' ? q.genre : country) === v ? ' on' : '')}
          onClick=${() => choose(v)} data-rk-opt=${v}>${text}</button>`)}
      </div>
    </div>`}`}
    ${mode === 'fav' && html`<div class="rk-chips">
      ${chip(!cat, T('allFavs') + ' ' + favs.length, () => setCat(''), 'all')}
      ${cats.map((c) => chip(cat === c, c + ' ' + favs.filter((x) => x.c === c).length, () => setCat(c), c))}
      ${favs.some((x) => !x.c) && cats.length > 0 && chip(cat === '-', T('noCat'), () => setCat('-'), '-')}
      <span class="rk-sp rk-chips">
        ${cat && cat !== '-' && html`<button type="button" class="rk-b sm" title=${T('renameCat')} aria-label=${T('renameCat')} onClick=${renameCat}><${ui.Icon} name="pencil" size=${15}/></button>
          <button type="button" class="rk-b sm" title=${T('dropCat')} aria-label=${T('dropCat')} onClick=${dropCat}><${ui.Icon} name="trash-2" size=${15}/></button>`}
        <button type="button" class="rk-b sm" onClick=${() => setFavCat(null, '+')} title=${T('newCat')} aria-label=${T('newCat')} data-rk="newcat"><${ui.Icon} name="plus" size=${15}/></button>
        <button type="button" class="rk-b sm" onClick=${custom} title=${T('custom')} aria-label=${T('custom')} data-rk="custom"><${ui.Icon} name="link" size=${15}/></button>
        <button type="button" class="rk-b sm" onClick=${save} title=${T('export')} aria-label=${T('export')}><${ui.Icon} name="download" size=${15}/></button>
        <label class="rk-b sm" title=${T('import')}><${ui.Icon} name="upload" size=${15}/><input type="file" accept=".json,application/json" hidden onChange=${load} aria-label=${T('import')}/></label>
      </span>
    </div>`}
    ${mode === 'st' && note && html`<p class="rk-note">${note}</p>`}
    ${mode === 'st' && q.order === 'near' && wide(src) && typeof geo.lat !== 'number' && html`<p class="rk-note">${T('noPlace')}</p>`}
    <div class="rk-l" role="list">
      ${mode === 'st' && rows === null ? html`<div class="rk-e">${T('loading')}</div>`
        : shown.length ? shown.map(row) : html`<div class="rk-e">${T(mode === 'st' ? 'none' : mode === 'fav' ? 'noFavs' : 'noRec')}</div>`}
    </div>
    ${mode === 'st' && more && html`<div class="rk-c"><button type="button" class="rk-b" onClick=${() => find(src, q, 1)} data-rk="more">${T('more')}</button></div>`}
    ${mode === 'fav' && html`<p class="rk-note">${T('count', { n: favs.length, max: F.max || doc.max || 0 })}</p>`}
    ${ask && html`<${Ask} ctx=${ctx} title=${ask.title} fields=${ask.fields} onClose=${() => setAsk(null)}
      onDone=${(v) => { setAsk(null); ask.done(v); }}/>`}
  </div>`;
}

function Radio({ ctx }) {
  const { html, t } = ctx;
  const T = (key, args) => t('mod.espradio.' + key, args);
  const doc = ctx.useDoc() || {};
  const r = useRadio(ctx);
  return html`<style>${STYLE}</style><div class=${'rk ' + r.skin} style=${'--h:' + r.hue}>
    <${Amp} ctx=${ctx} T=${T}/>
    <${Eq} ctx=${ctx} T=${T}/>
    <${Stations} ctx=${ctx} T=${T} doc=${doc}/>
    <p class="rk-note" style="margin:0 4px">${T('help', { n: doc.scanSec || 8 })}</p>
  </div>`;
}

// ---------------------------------------------------------------- what the full version has

// The other two tabs of EspWebMedia, the full version, drawn here as they are there and still:
// nothing of them works. What works is where the banner sends, a device sold in the shop.
const SHOP = 'https://smartilab.it/shop/';

function Locked({ ctx, tv }) {
  const { html, ui, t } = ctx;
  const T = (key) => t('mod.espradio.' + key);
  const r = useRadio(ctx);
  const key = (icon, text, big) => html`<button type="button" class=${'rk-b' + (big ? ' big' : '')} disabled>
    ${icon && html`<${ui.Icon} name=${icon} size=${18}/>`}${text}</button>`;
  const rows = tv ? [['play', 'ch'], ['eye', 'cam'], ['server', 'vid']] : [['server', 'srv'], ['radio', 'pod'], ['link', 'adr'], ['rk-music', 'spo']];
  return html`<style>${STYLE}</style><div class=${'rk ' + r.skin} style=${'--h:' + r.hue}>
    <div class="rk-buy" data-rk="buy"><${ui.Icon} name="lock" size=${20}/><span>${T('pro')}</span>
      <a class="btn sm" href=${SHOP} target="_blank" rel="noopener" data-rk="shop">${T('shop')} \u2197</a></div>
    <div class="rk-lock" inert aria-hidden="true" data-rk="locked">
      <div class="rk-p">
        <div class="rk-h"><span class="rk-logo">Esp<b>WebMedia</b></span><span class="rk-tag">${tv ? 'TV' : T('ptag')}</span></div>
        ${tv ? html`<div class="rk-tv"><${ui.Icon} name="monitor-smartphone" size=${40}/>${T('tvpick')}</div>`
    : html`<div class="rk-d" style="grid-template-columns:auto minmax(0,1fr)"><div class="rk-tt"><i></i></div>
          <div style="min-width:0"><div class="rk-n">${T('ppick')}</div><div class="rk-s">0:00</div></div></div>`}
        <div class="rk-c">${tv ? [key('rk-prev', 'CH'), key('rk-stop', '', 1), key('rk-next', 'CH'), key('rk-vol')]
    : [key('rk-prev'), key('', '-15'), key('play', '', 1), key('', '+30'), key('rk-next'), key('rk-stop')]}</div>
      </div>
      <div class="rk-p">
        <div class="rk-h rk-chips">${rows.map(([, k], i) => html`<span class=${'rk-ch' + (i ? '' : ' on')} key=${k}>${T('s.' + k)}</span>`)}</div>
        <div class="rk-l">${rows.map(([icon, k]) => html`<div class="rk-r" key=${k}><span class="ph"><${ui.Icon} name=${icon} size=${20}/></span>
          <div class="rk-rn">${T('s.' + k)}</div><div class="rk-ra"></div><div class="rk-rs">${T('x.' + k)}</div></div>`)}</div>
      </div>
    </div>
  </div>`;
}
const Player = ({ ctx }) => ctx.html`<${Locked} ctx=${ctx}/>`;
const Tv = ({ ctx }) => ctx.html`<${Locked} ctx=${ctx} tv=${1}/>`;

export default {
  uiApi: 1,
  icons: {
    'rk-prev': '<polygon points="19 20 9 12 19 4 19 20"/><line x1="5" x2="5" y1="19" y2="5"/>',
    'rk-next': '<polygon points="5 4 15 12 5 20 5 4"/><line x1="19" x2="19" y1="5" y2="19"/>',
    'rk-stop': '<rect width="16" height="16" x="4" y="4" rx="2"/>',
    'rk-star': '<polygon points="12 2 15.09 8.26 22 9.27 17 14.14 18.18 21.02 12 17.77 5.82 21.02 7 14.14 2 9.27 8.91 8.26 12 2"/>',
    'rk-vol': '<polygon points="11 5 6 9 2 9 2 15 6 15 11 19 11 5"/><path d="M15.54 8.46a5 5 0 0 1 0 7.07"/><path d="M19.07 4.93a10 10 0 0 1 0 14.14"/>',
    'rk-mute': '<polygon points="11 5 6 9 2 9 2 15 6 15 11 19 11 5"/><line x1="22" x2="16" y1="9" y2="15"/><line x1="16" x2="22" y1="9" y2="15"/>',
    'rk-music': '<path d="M9 18V5l12-2v13"/><circle cx="6" cy="18" r="3"/><circle cx="18" cy="16" r="3"/>',
  },
  // The phrases of the console this page uses, with their keys (F31 of the server).
  same: {
    'mod.espradio.all': 'log.all', 'mod.espradio.customName': 'mod.name',
    'mod.espradio.s.adr': 'net.addresses', 'mod.espradio.s.ch': 'mod.channels',
    'mod.espradio.search': 'net.scan',
  },
  texts: {
    it: {
      'mod.espradio.home': 'EspRadio',
      'mod.espradio.scan': 'Scansione: {n} secondi per stazione, finché ti fermi',
      'mod.espradio.hue': 'Colore delle luci', 'mod.espradio.look': 'Cambia stile: autoradio, amplificatore, neon',
      'mod.espradio.mem': 'Memoria {n}: i primi sei preferiti. Una vuota prende la stazione che suona.',
      'mod.espradio.eqNo': 'Questa stazione non lascia leggere il suo suono alla pagina: suona, ma senza equalizzatore, spettro e livelli.',
      'mod.espradio.tvpick': 'Scegli un canale, una telecamera o un video',
      'mod.espradio.x.ch': 'Liste di canali, le tue e quelle pronte: i canali spenti si nascondono da soli',
      'mod.espradio.x.cam': 'Telecamere IP, foto che si aggiornano, YouTube, tutte insieme in un mosaico',
      'mod.espradio.x.vid': 'I film e i video del NAS e del PC di casa', 'mod.espradio.x.srv': 'La musica del NAS, del PC e del router, trovati da soli',
      'mod.espradio.x.pod': 'Cercati per nome, ripresi da dove li avevi lasciati', 'mod.espradio.x.adr': 'Cartelle web, playlist, flussi, SoundCloud',
      'mod.espradio.x.spo': 'Un telecomando per il tuo Spotify Premium',
      'mod.espradio.pro': 'TV e lettore multimediale sono di EspWebMedia, la versione completa di questa radio: qui li vedi fermi.',
      'mod.espradio.shop': 'Vai al negozio',
      'cmd.mod.espradio.favs': 'Le stazioni preferite, una pagina alla volta',
      'cmd.mod.espradio.fav.set': 'Una stazione preferita, nuova o cambiata: nome, indirizzo, categoria, logo, info',
      'cmd.mod.espradio.fav.remove': 'Toglie una stazione dai preferiti', 'cmd.mod.espradio.cat.rename': 'Dà un altro nome a una categoria dei preferiti, o la toglie',
    },
    en: {
      'mod.espradio.home': 'EspRadio',
      'mod.espradio.scan': 'Scan: {n} seconds a station, until you stop',
      'mod.espradio.hue': 'Colour of the lights', 'mod.espradio.look': 'Change the look: car stereo, amplifier, neon',
      'mod.espradio.mem': 'Memory {n}: the first six favourites. An empty one takes the station on air.',
      'mod.espradio.eqNo': 'This station does not let the page read its sound: it plays, without equalizer, spectrum and levels.',
      'mod.espradio.tvpick': 'Choose a channel, a camera or a video',
      'mod.espradio.x.ch': 'Lists of channels, yours and the ready ones: the channels off hide by themselves',
      'mod.espradio.x.cam': 'IP cameras, pictures that refresh, YouTube, all together in a mosaic',
      'mod.espradio.x.vid': 'The films and the videos of the NAS and of the PC of the house', 'mod.espradio.x.srv': 'The music of the NAS, of the PC and of the router, found by themselves',
      'mod.espradio.x.pod': 'Found by name, started again from where you left them', 'mod.espradio.x.adr': 'Web folders, playlists, streams, SoundCloud',
      'mod.espradio.x.spo': 'A remote control for your Spotify Premium',
      'mod.espradio.pro': 'TV and media player belong to EspWebMedia, the full version of this radio: here you see them still.',
      'mod.espradio.shop': 'Go to the shop',
    },
  },
  tabs: [
    { id: 'home', icon: 'radio', label: 'mod.espradio.home', view: Radio },
    { id: 'tv', icon: 'monitor-smartphone', label: 'mod.espradio.tvtab', view: Tv },
    { id: 'player', icon: 'rk-music', label: 'mod.espradio.player', view: Player },
  ],
};
