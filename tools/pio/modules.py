# LoopMaxPro - the modules of a firmware.
# Reads custom_lmp_modules of the environment, checks each module against its module.json
# and turns names and parameters into build flags:
#
#   custom_lmp_modules = rgb:pins=2|3|4
#
#   -DLMP_MODULE_1=rgb  -DLMP_MOD_RGB=1
#   -DLMP_RGB_PINS={2,3,4}
#
# A pin at -1 is taken from "boardPins" of module.json, by board and by chip
# (tools/pio/lmp_pins.py): the pins of every board are in one place, the manifest.
#
# Several modules are separated by spaces, commas or new lines; the parameters of one module
# by semicolons; the values of a list by a vertical bar.
# The same text in the variable LMP_MODULES of the shell wins over the option.
# The sources of every module are always compiled: the linker keeps the ones named here.
import glob
import json
import os
import re
import sys

Import("env")  # noqa: F821  (provided by PlatformIO)

sys.path.insert(0, os.path.join(env.subst("$PROJECT_DIR"), "tools", "pio"))  # noqa: F821
import lmp_pins  # noqa: E402
import lmp_tune  # noqa: E402

PROJECT = env.subst("$PROJECT_DIR")  # noqa: F821
ROOTS = [os.path.join(PROJECT, "src", "modules"), os.path.join(PROJECT, "modules_private")]
# The modules that exist only for the tests on the PC and the simulator, such as demo.
TESTING = env.subst("$PIOENV") == "native"  # noqa: F821
if TESTING:
    ROOTS.append(os.path.join(PROJECT, "test", "modules"))
MAX_MODULES = 16
KINDS = ("hardware", "bridge", "online", "ui", "headless")
NAME = re.compile(r"^[a-z][a-z0-9_]{0,23}$")


def stop(message):
    sys.stderr.write("LoopMaxPro modules: %s\n" % message)
    env.Exit(1)  # noqa: F821


def split_modules(text):
    """'a:x=1;y=2, b' -> [('a', {'x': '1', 'y': '2'}), ('b', {})]"""
    found = []
    for item in re.split(r"[\s,]+", text.strip()):
        if not item:
            continue
        name, _, tail = item.partition(":")
        given = {}
        for pair in tail.split(";"):
            if not pair:
                continue
            key, equal, value = pair.partition("=")
            if not equal or not key:
                stop("'%s': a parameter is written name=value" % item)
            given[key] = value
        found.append((name, given))
    return found


def find_module(name):
    for root in ROOTS:
        path = os.path.join(root, name)
        if os.path.isfile(os.path.join(path, "module.json")):
            return path
    return None


def as_flag(value, default):
    """The text of a parameter, as the C++ code will read it. The default decides the type."""
    if isinstance(default, list):
        items = [v for v in str(value).split("|") if v != ""] if not isinstance(value, list) else value
        for v in items:
            if not re.match(r"^-?\d+$", str(v)):
                stop("'%s' is not a list of numbers" % value)
        return "{%s}" % ",".join(str(v) for v in items)
    if isinstance(default, bool):
        text = str(value).lower()
        if text in ("1", "true", "yes", "on"):
            return "1"
        if text in ("0", "false", "no", "off"):
            return "0"
        stop("'%s' is not true or false" % value)
    if isinstance(default, (int, float)):
        if not re.match(r"^-?\d+(\.\d+)?$", str(value)):
            stop("'%s' is not a number" % value)
        return str(value)
    if not re.match(r"^[A-Za-z0-9_.\- ]*$", str(value)):
        stop("'%s': letters, digits, space, dot, hyphen and underscore only" % value)
    return '\\"%s\\"' % value


# LMP_MODULES in the environment of the shell takes the place of the option: a build with
# other modules without touching platformio.ini.
wanted = split_modules(os.environ.get("LMP_MODULES") or
                       env.GetProjectOption("custom_lmp_modules", ""))  # noqa: F821


def build_flags():
    """The build flags of the environment, the board and the product included, as one text."""
    flags = env.GetProjectOption("build_flags", "")  # noqa: F821
    return " ".join(flags) if isinstance(flags, list) else flags


def board_and_chip():
    """LMP_BOARD of the build flags, and the chip of the board: "native" on the PC."""
    # The PC has no board: asking its configuration would stop the build.
    mcu = env.BoardConfig().get("build.mcu", "") if env.subst("$BOARD") else "native"  # noqa: F821
    m = re.search(r'LMP_BOARD=\\?"([^"\\]+)', build_flags())
    return (m.group(1) if m else ""), mcu


BOARD, MCU = board_and_chip()
if len(wanted) > MAX_MODULES:
    stop("%d modules, at most %d" % (len(wanted), MAX_MODULES))

defines = []
seen = set()
device_code = None
needs = set()
manifests = []
for index, (name, given) in enumerate(wanted, start=1):
    if not NAME.match(name):
        stop("'%s' is not the name of a module: lower case letters, digits, underscore" % name)
    if name in seen:
        stop("module '%s' is listed twice" % name)
    seen.add(name)
    path = find_module(name)
    if not path:
        stop("module '%s' not found in src/modules or modules_private%s"
             % (name, " or test/modules" if TESTING else ""))
    try:
        with open(os.path.join(path, "module.json"), "r", encoding="utf-8") as f:
            manifest = json.load(f)
    except (OSError, ValueError) as error:
        stop("%s/module.json: %s" % (name, error))

    if manifest.get("name") != name:
        stop("%s/module.json says its name is '%s'" % (name, manifest.get("name")))
    if manifest.get("kind") not in KINDS:
        stop("%s/module.json: kind is one of %s" % (name, ", ".join(KINDS)))

    params = dict(manifest.get("params", {}))
    for key in given:
        if key not in params:
            stop("module '%s' has no parameter '%s'. It has: %s"
                 % (name, key, ", ".join(sorted(params)) or "none"))

    # Two modules that are never on one device: the free and the full version of a product.
    for other in manifest.get("notWith", []):
        if other in [n for n, _ in wanted]:
            stop("modules '%s' and '%s' do not go together on a device" % (name, other))

    needs.update(manifest.get("requires", []))
    manifests.append((name, manifest))
    upper = name.upper()
    defines.append(("LMP_MODULE_%d" % index, name))
    defines.append(("LMP_MOD_%s" % upper, 1))
    # The pins at -1 are the ones of the board, from the manifest.
    resolved = lmp_pins.resolve(manifest, given, MCU, BOARD)
    for key in lmp_pins.missing(manifest, resolved):
        print("LoopMaxPro modules: %s: no pin '%s' for %s (%s): give it in the build, %s:%s=..."
              % (name, key, BOARD or "this board", MCU, name, key))
    for key, default in sorted(params.items()):
        value = resolved.get(key, default)
        if key == "code":
            # The kind of device the server knows: one for the whole firmware.
            if value and device_code is None:
                device_code = str(value)
            continue
        defines.append(("LMP_%s_%s" % (upper, re.sub(r"[^A-Za-z0-9]", "_", key).upper()),
                        as_flag(value, default)))

# The tunings the modules ask on this board (tools/pio/lmp_tune.py): values of LoopMaxPro
# for this firmware only, never for the other modules.
try:
    tuned, told = lmp_tune.resolve(manifests, MCU, BOARD, build_flags(), lmp_tune.tunables(PROJECT))
except ValueError as error:
    stop(str(error))
defines += tuned
if told:
    print("LoopMaxPro tunings: %s" % ", ".join(told))

# The tests on the PC use every module, also those not in the list: their pins too come from
# the manifest, as they would on a board.
if TESTING:
    for root in ROOTS:
        for folder in sorted(glob.glob(os.path.join(root, "*", "module.json"))):
            with open(folder, "r", encoding="utf-8") as f:
                other = json.load(f)
            if other.get("name") in seen:
                continue
            upper = other.get("name", "").upper()
            resolved = lmp_pins.resolve(other, {}, MCU, BOARD)
            for key in other.get("pinParams", {}):
                defines.append(("LMP_%s_%s" % (upper, re.sub(r"[^A-Za-z0-9]", "_", key).upper()),
                                as_flag(resolved[key], other["params"][key])))

# The light edition leaves the bus out unless a module asks for it.
if "bus" in needs:
    defines.append(("LMP_NEEDS_BUS", 1))
# The smallest boards leave the client of web services out unless a module asks for it.
if "httpclient" in needs:
    defines.append(("LMP_NEEDS_HTTPCLIENT", 1))

if device_code is not None:
    if not re.match(r"^[A-Za-z0-9_.\-]+$", device_code):
        stop("code '%s': letters, digits, dot, hyphen and underscore only" % device_code)
    defines.append(("LMP_DEVICE_CODE", '\\"%s\\"' % device_code))

if defines:
    env.Append(CPPDEFINES=defines)  # noqa: F821
    print("LoopMaxPro modules: %s" % ", ".join(name for name, _ in wanted))

# Modules kept outside the sources of the project.
private = os.path.join(PROJECT, "modules_private")
if os.path.isdir(private) and env.get("PIOFRAMEWORK", [""])[0:1] != ["espidf"]:  # noqa: F821
    # They are built outside src/: they see the headers of the project as the modules there.
    env.Append(CPPPATH=[os.path.join(PROJECT, "include"), os.path.join(PROJECT, "src")])  # noqa: F821
    for name in sorted(os.listdir(private)):
        sources = os.path.join(private, name, "src")
        if os.path.isdir(sources):
            env.BuildSources(os.path.join("$BUILD_DIR", "lmp_private", name), sources)  # noqa: F821

# Modules of the tests: built for the PC only.
testing = os.path.join(PROJECT, "test", "modules")
if TESTING and os.path.isdir(testing):
    env.Append(CPPPATH=[os.path.join(PROJECT, "include"), os.path.join(PROJECT, "src")])  # noqa: F821
    for name in sorted(os.listdir(testing)):
        sources = os.path.join(testing, name, "src")
        if os.path.isdir(sources):
            env.BuildSources(os.path.join("$BUILD_DIR", "lmp_testing", name), sources)  # noqa: F821
