<p align="center"><a href="../README.md"><img src="assets/img/logo.webp" alt="LoopMaxPro" height="40"></a>&nbsp;&nbsp;&nbsp;<img src="assets/t/web-console.svg" alt="Web console" height="46"></p>

<p align="center">
<a href="../README.md">Home</a> &nbsp;·&nbsp;
<a href="README.md">Documentation</a> &nbsp;·&nbsp;
<a href="writing-a-module.md">← Writing a module</a> &nbsp;·&nbsp;
<a href="http-api.md">HTTP API →</a>
</p>

---

The web console is the user interface of every LoopMaxPro device: one web application, served by
the device itself, that shows the module of the firmware and manages the whole system. This page
explains how it is made, what a module gets from it without writing any JavaScript, how a module
brings a page of its own, and how the console is built and tried on a PC. Read it if you write a
module or want to change the console. To talk to a device from your own program, go to the
[HTTP API](http-api.md).

- [What it is](#what-it-is)
- [The fixed part and the part of the module](#the-fixed-part-and-the-part-of-the-module)
- [The LoopMaxPro space](#the-loopmaxpro-space)
- [The standard view of a module](#the-standard-view-of-a-module)
- [A page of its own for a module](#a-page-of-its-own-for-a-module)
- [The UI kit](#the-ui-kit)
- [Translations](#translations)
- [How the console talks to the device](#how-the-console-talks-to-the-device)
- [Sources and build](#sources-and-build)
- [Where the console lives on a board](#where-the-console-lives-on-a-board)
- [Working with the PC simulator](#working-with-the-pc-simulator)

## What it is

| | |
|---|---|
| Served by | the device, at `http://<address of the device>/`. No cloud, no account and no Internet are needed: it works on the access point of a board just flashed |
| Made with | Preact and htm, in the single file [web/vendor/preact-htm.js](../web/vendor/preact-htm.js). No Node and no bundler: the build is one Python script |
| Files | `index.html`, one script `lmp.<fingerprint>.js`, one style sheet `lmp.<fingerprint>.css`, and one more script for each module with a page of its own. All compressed with gzip |
| Languages | English and Italian, both inside the script. The language is a setting of the device |
| Screens | designed for the phone first; the same views widen on tablets and PCs |
| Themes | dark and light. It follows the system until one is chosen; the choice stays in the browser |
| Isolation | the console is the custom element `<lmp-console>` and draws inside a shadow root: no style enters or leaves. Its colours are CSS variables |
| Data | everything it shows is a document and everything it does is a command of the [HTTP API](http-api.md). There is no polling: one report at the start, then the events of the device |

The page the device serves is all here ([web/device/index.html](../web/device/index.html)); the
build fills in the two names:

```html
<lmp-console css="lmp.4b07b721.css" hash></lmp-console>
<script src="lmp.13759944.js" defer></script>
```

`css` is the style sheet, loaded inside the element. `hash` makes the address follow the view,
so that a view can be bookmarked: `#/m/zappy2/schedules`, `#/sys/log`.

## The fixed part and the part of the module

```
+--------------------------------------------------------------------+
| LoopMaxPro | device name | module v      network, time, live dot  * |  fixed
+--------------------------------------------------------------------+
| [Device] [Schedules] [Settings] [Info]                              |  tabs of the module
+--------------------------------------------------------------------+
| banners: connection lost, fallback, network to set up, time zone    |  fixed
|  +----------+ +----------+                                          |
|  | Line 1   | | Line 2   |                                          |  part of the module
|  |   ON     | |   OFF    |                                          |
|  +----------+ +----------+                                          |
+--------------------------------------------------------------------+
  * opens the LoopMaxPro space: the tabs of the system
```

| Part | What it holds | Who writes it | Where |
|------|---------------|---------------|-------|
| Fixed | header and status bar, login, banners, the screens for a restart and for a lost connection, the choice of the module, the LoopMaxPro space | the project: the same on every device | [web/console/](../web/console/), [web/kit/](../web/kit/) |
| Module, standard view | the tabs Device, Schedules, Settings and Info, drawn from what the firmware of the module publishes | nobody: the console works it out from the document and the manifest of the module | [web/console/module-view.js](../web/console/module-view.js) |
| Module, page of its own | more tabs, or a tab in place of a standard one, with the texts and icons of the module | who writes the module | one file in the folder of the module |

**Navigation.** The console opens on the module, tab Device: the module is what gives the device
its purpose. With more than one module the title in the header is a menu, and the console
remembers the last one chosen. The gear button opens the LoopMaxPro space and "Back to the
device" closes it; a firmware without modules opens there. On a phone the tabs of the module
move to a bar at the bottom of the screen, under the thumb.

**Status bar.** Always in sight: network and signal (or the access point), Internet when the
device checks it, MQTT when it is on, the cloud when the firmware has a bridge, the time of the
device, and a dot that is green while the live updates arrive.

**Whole-page situations.**

| Situation | What happens |
|-----------|--------------|
| Start | logo and spinner; the console reads `/api/state`, then the report |
| Login needed | the login screen. A wrong password and too many attempts have their own message |
| Session over | at the first `401` the login screen comes back |
| Connection lost | a red banner; the browser reconnects by itself, then the console reads the report again and the log entries it missed |
| Restart, update, factory reset | a waiting screen; the console asks `/api/state` every 2 seconds and reloads when the device answers. After 2 minutes it says the device may be elsewhere |
| Access point, no network chosen yet | a banner that leads to the Network tab |
| Device on UTC, browser in another zone | a banner that offers the time zone of the browser |
| The device does not answer | a screen with "Try again" |

A device that has not taken the time from the network takes it from the browser that opens its
console. The session token lives in the storage of the browser tab: closing the tab ends it.

## The LoopMaxPro space

The tabs of the system. A tab is there only when its service is in the firmware: a build without
MQTT has no MQTT tab.

| Tab | Content |
|-----|---------|
| Overview | name, board, chip, version, mode; uptime, free memory and its lowest point, network, time; bars for memory and storage; the key of the device; the state of every service; the notes of the installed version |
| Network | client and access point with addresses and signal; AP or LAN mode; networks around; test of the credentials and change of network, with automatic return when it fails; name and password of the access point, with a warning while the password is the factory one; the addresses of the console |
| Find devices | the other LoopMaxPro devices on the same network, each one opening its own console, and the ones switched on but not yet on a network, known by their access point |
| Modules | the modules with state, kind and version; open, restart |
| Log | entries as they are written; filters by level, source and text; pause, download, clear; the log level of the device |
| Update | installed firmware, trial and confirmation, return to the previous one; firmware from a file, with its SHA-256 computed in the browser and a progress bar; firmware from an address; the files of the console |
| Time & place | time of the device, synchronization, time from the browser; time zone, among the common ones and the one of the browser; time server; position |
| MQTT | state of the link and counters; broker, TLS, user, password, topic base; how the device presents itself to Home Assistant |
| Cloud | the state of the cloud bridge, when the firmware has one: see [Cloud bridge](cloud.md) |
| Hardware | pins in use with owner, mode and level; I2C buses, opening one and searching it; the LoRa radio where there is one |
| Services | every service with state, reason and timings; opened: its dependencies, its document and **every command with a form** |
| Settings | name, host name, language; password of the console and sessions; theme; restart and factory reset |

The Services tab gives access to every command of the system: nothing the
[serial console](serial-console.md) can do is missing from the web console.

A module of the kind `bridge` can add tabs of its own to this space, and a notice above every
page: see [Adding to the LoopMaxPro space](#adding-to-the-loopmaxpro-space).

## The standard view of a module

A module does not need to write an interface. The console draws it from what the firmware
publishes, without knowing the module: [Zappy 2](../src/modules/zappy2/README.md) has no line of
JavaScript.

| Tab | Shown when | Drawn from | What it shows |
|-----|-----------|------------|---------------|
| Device | always | `now` in the document | one block with a large icon, the main value with its unit, a label, and the small values of `items` |
| | | `days` in the document | one box for each day: the name of the day in the language of the console ("Today" for the first), icon, `hi` and `lo` as degrees, the small values of `items`. On a narrow screen the row scrolls sideways |
| | | `pins` in the document | one tile for each channel: a large switch (a badge for an input), name, GPIO, what the schedules will do next, a button for each command that takes only `channel`. All on, all off, and whether the channels come back as they were at the next start. A channel held by a schedule has its switch locked |
| | | plain members of the document | a card "Values", with names and known values translated |
| | | the other commands of the manifest | a card "Actions", each command with the form of its arguments |
| Schedules | the document has `schedules` | `schedules` | for each channel: list, enable and disable, edit, remove; a new weekly or one-time schedule; schedules that overlap are pointed out |
| Settings | the manifest has `config` | `config` in the manifest | a form made from the schema: types, limits, defaults; secrets never shown; only what changed is saved; back to the defaults |
| Info | always | manifest and `sys.modules` | version, kind, state and reason; every command with its form; the whole document; restart of the module |

`now` and `days` are two shapes a module can give its document for values it reads rather than
drives. They are not tied to the weather:

```json
"now":  { "icon": "moon", "value": 17.8, "unit": "°C", "label": "Milan, IT",
          "items": [ { "key": "humidity", "value": 80, "unit": "%", "icon": "droplets" } ] },
"days": [ { "date": "2026-03-14", "icon": "cloud", "hi": 25, "lo": 16,
            "items": [ { "key": "rain", "value": 0, "unit": "%", "icon": "umbrella" } ] } ]
```

Icons are names of the [UI kit](#the-ui-kit). A setting the schema marks `locked` is shown
switched off with a lock and a note; one marked `readOnly` is not shown.

What the firmware side publishes, and how, is in [Modules and SDK](modules-sdk.md) and
[Data model and report](data-model.md). [MeteoWifi](../src/modules/meteowifi/README.md) is a
full example of `now` and `days`.

## A page of its own for a module

When the standard view is not enough (a chart, a colour picker, a search) the module brings
**one file** with its interface. The file sees no address and no HTTP: it receives everything
from the console in one object, `ctx`. For this reason the same file runs unchanged wherever the
console runs, outside the device as well.

### Where it is

```
src/modules/<name>/
├── module.json        "ui": "ui/<name>.js"
├── src/<name>.cpp     the code of the device
└── ui/<name>.js       the interface
```

`ui` in `module.json` is the path of the file from the folder of the module. Two modules can
name the same file: RGB Controller and GanjaLeaf share
[src/sdk/ui/lights.js](../src/sdk/ui/lights.js).

### What it contains

```js
function Counters({ ctx }) {
  const { html, ui, t } = ctx;
  const doc = ctx.useDoc();                 // the document of the module, kept fresh by the events
  if (!doc) return html`<${ui.Spinner} size=${28}/>`;
  return html`<${ui.Card} title=${t('mod.demo.counters')} icon="activity">
    <${ui.Stat} icon="timer" label=${t('mod.demo.pulses')} value=${doc.pulses}/>
    <${ui.Btn} icon="play" onClick=${() => ctx.act('pulse', { channel: 0 }, t('done'))}>Pulse<//>
  <//>`;
}

export default {
  uiApi: 1,
  texts: { en: { 'mod.demo.counters': 'Counters', 'mod.demo.pulses': 'Pulses' },
           it: { /* the same keys, in Italian */ } },
  icons: { 'my-icon': '<path d="..."/>' },  // optional: more drawings, in the format of Lucide
  tabs: [
    { id: 'counters', icon: 'activity', label: 'mod.demo.counters', view: Counters },
  ],
};
```

| Member | |
|--------|---|
| `uiApi` | the version of the contract: `1`. With another number the console draws the standard view and says so |
| `texts` | texts for each language, added to those of the console. They never replace a text of the console |
| `icons` | more drawings; those of the console stay |
| `tabs` | the tabs of the module. An `id` equal to a standard one (`home`, `schedules`, `settings`, `info`) **takes its place**; any other `id` is one more tab, after Device. `label` is the key of a text; `icon` a name of the kit |
| `view` | a Preact component, drawn with `ctx.html`. It receives `{ ctx }` |
| `mount(el, ctx)` | in place of `view`: draws into `el` as it likes, even without Preact. What it returns (a function, or an object with `destroy`) is called when the tab closes |

### What it receives: `ctx`

| Member | |
|--------|---|
| `name`, `target` | `demo`, `mod.demo` |
| `html`, `h`, `render`, `useState`, `useEffect`, `useRef`, `useMemo`, `useCallback` | Preact and htm, the ones of the console |
| `ui` | the pieces of the [UI kit](#the-ui-kit), with the look of the console |
| `t(key, values)`, `lang()`, `label(key)` | a text in the language of the device; the language; the translated name of a field of the module |
| `doc()`, `useDoc()`, `onDoc(fn)` | the document of the module: now, inside a component, at every change (`onDoc` returns the function that stops it) |
| `manifest()` | the commands and the schema of the settings |
| `clock()`, `useClock()` | the time of the device, moved on in the browser: `{ unix, offset, tz }`, or null while the device does not know it |
| `command(name, args)` | a command of the module. It resolves with `{ ok, status, result, field }` and never throws for a refusal |
| `act(name, args, okText)` | the same, and it tells the user how it went with a notice |
| `read(name, args)` | a command that **only reads**, such as the pages of a long list: it does not block the console, and several can travel together |
| `stream(name, args, key)` | for a value that follows a finger, such as a slider: never two travelling for the same `key`, and the last value always arrives |
| `useBusy()` | true while a command is with the device, to draw something of your own |
| `useSysDoc(target, everyMs)`, `sysRead(target, name, args)` | for a module that shows the device itself: the document of a `sys.` target inside a component, read again every `everyMs` (0: once), and a command of it that does not block. `sys.` targets only |
| `hasService(name)` | true when the firmware has that service, `mqtt` for example |
| `config()`, `saveConfig(values)` | the settings without the secrets; `saveConfig` sends the values it is given with `config.set`: give it only what changed |
| `toast(text, kind)`, `confirm({ title, text, okLabel, danger })` | the notices and the confirmation window of the console. `confirm` resolves true or false |
| `askPlace(what)`, `placed()`, `mode()` | the window that asks where something is (the position of who looks, or coordinates typed in); whether the device has a position of its own; `AP` or `LAN` |
| `where()` | `local` in the console of the device. For a text only, never for a different path of the code |

### Rules

| Rule | Why |
|------|-----|
| **No `import`**: everything comes from `ctx` | the build wraps the file and gives it the name of the module. It stops when it finds an `import` |
| One `export default`, an object, and no other export | the build stops otherwise |
| The device is reached through `ctx` only: no address, no call to `/api` | it is what lets the same file run wherever the console runs |
| Commands of its own module | what is needed of the system is in the fixed part, or read with `useSysDoc` |
| Texts in both languages, with keys `mod.<name>.*` | the console is in English and in Italian |
| Styles of its own in a `<style>` element of the view, built on the variables of the console (`--text`, `--muted`, `--line`, `--panel2`, `--accent`, ...) | the page follows the theme, dark or light |
| Try it at phone width, on a tablet and on a PC | the console is used from the phone most of all |

An error inside a tab stays in the tab: the console shows it with "Try again" and everything
else goes on. A file that does not arrive leaves the standard view, with a note.

### How it travels

1. [tools/web/build.py](../tools/web/build.py) reads `ui` in `module.json`, checks the file and
   writes it as `m.<name>.<fingerprint>.js`.
2. The name of that file goes into the console, in a map from module to file.
3. A board gets only the interfaces of the modules of its firmware.
4. When a module is opened for the first time the console loads its file. With the fingerprint
   in the name, the browser keeps it for a year.

Examples to start from: [test/modules/demo/ui/demo.js](../test/modules/demo/ui/demo.js), one
more tab, in under forty lines; [src/modules/meteowifi/ui/meteowifi.js](../src/modules/meteowifi/ui/meteowifi.js),
a tab with a search and a style of its own; [src/sdk/ui/lights.js](../src/sdk/ui/lights.js), a
home that takes the place of the standard one, with a colour picker and sliders that use
`stream`.

### Adding to the LoopMaxPro space

The tabs of a module live in the space of the module. A module of the kind `bridge` can also
add to the LoopMaxPro space, for what belongs to the whole device and comes with the bridge: an
account on a server, the state of a
[service of the product](kernel-and-services.md#a-service-of-the-product). Its file says it with
one more member, `system`:

```js
export default {
  uiApi: 1,
  texts: { en: { 'sys.account': 'Account' }, it: { /* the same keys, in Italian */ } },
  system: {
    tabs: [{ id: 'account', icon: 'user', label: 'sys.account', need: 'cloud', view: Account }],
    Banner,
    start(ctx) { /* once, when the console opens on a device */ },
    stopped: { guard: 'mod.stoppedGuard' },
    reasons: { guard: 'mod.byGuard' },
  },
};
```

| Member | |
|--------|---|
| `tabs` | tabs of the LoopMaxPro space, placed after Cloud. `view` is a component that receives `{ ctx }`. `need` is optional: the name of a service, and the tab is there only when the firmware has it |
| `Banner` | a component drawn above every page, with the other notices of the console. It receives `{ ctx, go }`; `go(id)` opens a tab of the LoopMaxPro space. It returns nothing when it has nothing to say |
| `start(ctx)` | called once, when the console has read the report of the device |
| `stopped` | for each reason a module can be stopped for, the key of the text shown in place of the page of that module. With such a reason the Modules tab does not offer the restart |
| `reasons` | the same reasons in two words, for the list of the modules |

The words of `stopped` and `reasons` are the ones the firmware writes as the `reason` of a
module: for a service of the product, what its `reason()` returns.

The file of a bridge is read when the console starts, not when the module is opened. Everything
else is as for any other file: the same `ctx`, the same rules, the same build. The `system`
member of a module of another kind is not looked at.

## The UI kit

The pieces every view of the console is made of, in [web/kit/ui.js](../web/kit/ui.js) and
[web/kit/form.js](../web/kit/form.js). A module gets them as `ctx.ui` and looks like the rest of
the console without a line of style.

| Piece | Main properties | |
|-------|-----------------|---|
| `Card` | `title`, `icon`, `sub`, `actions` | a box with a title; `actions` are the buttons beside it |
| `Btn` | `kind` (`primary`, `ghost`, `danger`), `icon`, `small`, `busy`, `disabled`, `onClick` | a button |
| `Switch` | `on`, `onChange`, `label`, `big`, `disabled` | an on/off switch |
| `Segmented` | `value`, `options`, `onChange` | a choice among few, as buttons side by side |
| `Select`, `TextInput` | `label`, `value`, `hint`, `onChange` or `onInput` | fields of a form |
| `Field` | `label`, `hint`, `error` | the frame of a field, for a control of your own |
| `Badge` | `kind` (`ok`, `warn`, `err`, `info`, `off`), `icon` | a coloured label for a state |
| `Notice` | `kind`, `icon`, `action` | something the user should know before going on |
| `KV` | `rows`: `[[label, value], ...]` | a list of names and values; a null value is left out |
| `Stat` | `icon`, `label`, `value`, `sub` | a value with its name, large |
| `Meter` | `value`, `max`, `label`, `detail` | a bar that fills |
| `Modal` | `title`, `icon`, `onClose`, `footer` | a window over the page |
| `Spinner`, `Empty`, `Icon` | `size`; `icon`; `name`, `size` | waiting, nothing to show, a drawing |
| `SchemaForm` | `schema`, `values`, `onSave`, `onReset` | the form of the settings, from the schema of the manifest |
| `CommandForm` | `command`, `onRun` | the form of the arguments of one command of the manifest |

A `Btn`, a `Switch` or a `Segmented` whose handler returns a promise stays busy, and takes no
other press, until the promise ends: `onClick=${() => ctx.act(...)}` is all it needs.

**Icons.** Seventy drawings from [Lucide](https://lucide.dev), drawn inline; the names are in
[web/kit/icons.js](../web/kit/icons.js). An unknown name draws a question mark.

**Look.** System font, cards, large switches, coloured labels for the states, bars for memory
and progress, modal windows, notices at the bottom. Grids become one column on a narrow screen.
Who asked the system for less motion sees no animation.

## Translations

The firmware speaks English. The language of the console is the setting `lang` of the device
(`sys.config`): changing it changes it for everybody who opens the console. The texts are in
[web/i18n/en.js](../web/i18n/en.js) and [web/i18n/it.js](../web/i18n/it.js); a text missing in
Italian is taken from English.

The console translates the names of the firmware it knows, and makes the others readable:

| What | Key looked up | Otherwise |
|------|---------------|-----------|
| A field of a module (a setting, a value, an argument) | `mod.<name>.<key>`, then `field.<key>` | the name made readable: `pulseMs` becomes "Pulse Ms" |
| What a command does | `cmd.<target>.<command>`; for a module also `cmd.<command>` | the help text of the firmware |
| A value that is a word | `val.<value>` | the word as it is |

A module brings its translations in the `texts` of its own file: nothing of the console has to
be touched.

## How the console talks to the device

All the HTTP of the console is in one file, [web/console/transport.js](../web/console/transport.js);
what the console knows of the device and how it is kept fresh is in
[web/console/live.js](../web/console/live.js).

| Rule | Effect |
|------|--------|
| One report at the start, then the events | an event says what changed, and only that document is read again |
| Events close together become one request | a document is read some tens or hundreds of milliseconds after its event |
| Two views asking for the same document together | one request |
| Manifests are read once | commands and schemas do not change while the firmware runs |
| Commands that end later | the console waits for the event with the same `ref`, up to 60 seconds |
| Memory and uptime | read again every 10 seconds, only with the Overview open and the page in sight |
| Time of the device | read once, then moved on in the browser |
| An answer too long for a small board (`507`) | the report is asked in two parts, a manifest without its help texts |

**One command at a time.** A board is slow, and a button pressed again and again would pile
commands up on it. So every command, of the console and of the page of any module, passes
through one place:

- from the moment a command of the user leaves to the moment the device has done it, the
  console takes no other press. A bar runs at the top of the page; the button pressed shows a
  spinner. The block lasts at least 450 ms, and for a module until its document has been read
  again (1.5 seconds at most);
- a second command that arrives all the same is **not sent**: it resolves with
  `{ ok: false, status: 'wait' }`, and nothing is said;
- `ctx.stream` and `ctx.read` do not block and are not blocked.

Who writes a view has nothing to do to get this.

## Sources and build

```
web/
├── vendor/preact-htm.js   Preact with its hooks and htm, in one file
├── kit/                   reusable pieces: ui, form, store, i18n, format, tz, sha256, qr, icons
├── console/               transport, live, place, schedules, module-ui, module-view, sys-core,
│                          sys-more, devices, shell, app
├── i18n/                  en.js, it.js
├── styles/console.css
└── device/index.html      the page of the device
```

The sources are JavaScript modules, as an editor wants them. The browser gets one script and one
style sheet. [tools/web/build.py](../tools/web/build.py) makes them, with Python alone:

1. it puts the library in a function of its own, so its short names stay there;
2. it joins the other files into one script, in the order written in the script itself;
3. it **stops** when two files give the same name to two different things, or a file imports a
   name that no file declares;
4. it **stops** when the script does not pass the syntax check of Node, when Node is installed;
5. it **stops** when a text the console uses is missing in English, and lists the ones missing
   in Italian;
6. it prepares the interfaces of the modules and writes their map into the console;
7. it writes the notes of the newest version, from `CHANGES.md`, for the Overview;
8. it shrinks the style sheet, computes the fingerprints and writes them in the file names and
   in `index.html`;
9. it compresses everything, and **stops** when the console with its two largest module
   interfaces is over 150 KB: the most a board carries.

| Command | What it does |
|---------|--------------|
| `python tools/web/build.py` | builds the console with the interface of every module into `data/web/` |
| `python tools/web/build.py --modules zappy2,meteowifi` | only the interfaces of these modules |
| `python tools/web/build.py --out <folder>` | writes there |
| `python tools/web/build.py --sim` | also into the folder of the PC simulator, with the modules of the tests |

You rarely run it by hand for a board: PlatformIO does, through
[tools/pio/web.py](../tools/pio/web.py).

## Where the console lives on a board

| Board | Where | How it gets there |
|-------|-------|-------------------|
| ESP32 family | **inside the program**. Before every build the console is built into the build folder and written as a table of bytes that [web_bundle.cpp](../src/services/web/web_bundle.cpp) includes | with the firmware: `pio run -e <env> -t upload`, or any firmware update. The filesystem keeps only the data, and its image is empty |
| ESP8266 | in the filesystem, folder `/web` | `pio run -e <env> -t uploadfs`. The image **replaces every file of the board**, the settings too |
| PC simulator | in the filesystem of the simulator | `python tools/web/build.py --sim` |

A board gets the interfaces of its own modules only: the build passes the modules of the
environment (`custom_lmp_modules`, or the variable `LMP_MODULES`).

On the ESP32 family a file of the console can also be uploaded to the filesystem of a running
board, from the Update tab or with `POST /api/upload/file`: from then on the uploaded files
come before those of the program. It is the way to try a console without a new firmware. The
command `sys.web useProgramConsole` removes them and goes back to the console of the program.
`/api/state` tells which one is in use, in `console.inProgram` and `console.uploaded`.

A firmware without the files of its console still answers: a small page says the console is not
installed, and the API works.

## Working with the PC simulator

The same firmware runs on the PC, with a real HTTP server: the fastest way to work on the
console or on the page of a module.

```
pio run -e native
python tools/web/build.py --sim
.pio/build/native/program
```

(`program.exe` on Windows.) Then open `http://localhost:8088/`.

| | |
|---|---|
| Port | 8088. Another one: set the variable `LMP_SIM_PORT` before starting it |
| Reachable from | this PC only |
| Login | none while the simulated access point is on: the browser of the PC counts as one of its clients. After that, the password is the factory one of the access point, `loopmaxap` |
| Module | `demo`, in [test/modules/demo](../test/modules/demo/): two outputs with schedules, a button, a few settings, and a page of its own with one tab |
| Networks | a few simulated Wi-Fi networks to search and join, declared at the start of `main()` in [src/main.cpp](../src/main.cpp) |
| Command line | the lines typed in the terminal reach the system, as from the serial port |
| Files of the console | `.lmp_native/fs/web`, written by `build.py --sim` |
| Settings | kept between two runs, in `.lmp_native/` |

After a change to a file of `web/`, or to the `ui` file of a module, run
`python tools/web/build.py --sim` again and reload the page: the simulator does not need to be
restarted.

---

<p align="center">
<a href="../README.md">Home</a> &nbsp;·&nbsp;
<a href="README.md">Documentation</a> &nbsp;·&nbsp;
<a href="writing-a-module.md">← Writing a module</a> &nbsp;·&nbsp;
<a href="http-api.md">HTTP API →</a>
</p>
