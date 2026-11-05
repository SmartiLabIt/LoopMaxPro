# Demo module

<p align="center">
<a href="../../../README.md">Home</a> &nbsp;·&nbsp;
<a href="../../../docs/README.md">Documentation</a>
</p>

---

The demo module has two outputs with names and schedules, a button and a few settings. It is
the module of the simulator on the PC and of the tests. It is not a product: it lives in
`test/modules` and is built only for the `native` environment.

It is there for two things:

- to show what a module written with the SDK looks like: [src/demo.cpp](src/demo.cpp), about a
  hundred lines, and [ui/demo.js](ui/demo.js), an interface of its own that adds the tab
  "Counters" beside the standard ones;
- to give the console a real module to work on, without a board.

How a module is written is told in [Writing a module](../../../docs/writing-a-module.md).

## What it has

| | |
|---|---|
| Target | `mod.demo` |
| Tabs in the console | Device, Counters, Schedules, Settings, Info |
| Build parameters | `pins`: the two outputs, 22 and 23 when missing; `button`: the pin of the button, -1 for none |
| Its own command | `pulse` (`channel`, 0 or 1): turns a channel on for the time of the settings |
| Commands of the SDK | `set`, `toggle`, `all`, `flip`, `name`, `invert`, `restore`, `sched.add`, `sched.update`, `sched.remove`, `sched.clear`, `config.set`, `config.reset` |
| Settings | `label`: a text, `Demo`; `pulseMs`: 50 to 60000, 1000; `buttonToggles`: the button turns channel 0 into its opposite, true |
| Counters | `presses` of the button, `pulses` |

The `native` environment of [platformio.ini](../../../platformio.ini) chooses it so:

```ini
custom_lmp_modules = demo:pins=40|41;button=42
```

## Running the simulator

The simulator is LoopMaxPro built for the PC, with a simulated network and a true HTTP server.
It needs a C++ compiler for the PC (`g++`).

```
pio run -e native
python tools/web/build.py --sim
.pio/build/native/program
```

The first command builds the simulator, the second builds the console and puts it in the
folder of the simulator, the third starts it; on Windows it is
`.pio\build\native\program.exe`. Start it from the root of the project: it keeps its files,
the console and the settings, in the folder `.lmp_native`. Then open `http://localhost:8088`.

- It answers this computer only.
- Another port: set the variable `LMP_SIM_PORT` before starting it.
- The lines written in its window reach the command line of the system, as on a serial
  console: `cmd mod.demo pulse channel=0`.
- A number after the name of the program is how many milliseconds it runs before it ends by
  itself.

## Running the tests

```
pio test -e native
pio test -e native -f test_modules
```

The first runs every test, the second one of them. The tests write their program in the place
of the simulator: after them, build the simulator again.
