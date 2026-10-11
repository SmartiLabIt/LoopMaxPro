#!/usr/bin/env python3
# LoopMaxPro - builds the console from web/ into data/web/.
#
#   python tools/web/build.py              data/web/: what goes in the filesystem of a board
#   python tools/web/build.py --sim        also into the folder of the simulator on this PC
#   python tools/web/build.py --modules zappy2,meteowifi
#                                          only the interfaces of these modules (tools/pio/web.py
#                                          passes the modules of the firmware)
#
# A module can bring an interface of its own: "ui" in its module.json names the file, next to
# its sources. The file becomes m.<module>.<fingerprint>.js, the same on the device and on the
# server; the console reads it when the module is opened. docs/web-console.md.
#
# The sources are ES modules, as an editor wants them. The browser gets one script and one
# style sheet, compressed, with the fingerprint of the content in the name: after the first
# visit nothing is downloaded again until something changes. No Node: Python only.
#
# What the build checks, and stops for:
#   - two files that give the same name to two different things;
#   - a name imported by a file and declared by no file of the list (a file left out of SCRIPTS);
#   - a script the JavaScript parser refuses, when Node is on the PC (an apostrophe not
#     escaped in a text, for one);
#   - a text key used by the console and missing in English;
#   - an interface of a module that imports something, or has no "export default";
#   - the size over the limits of docs/web-console.md.
#
# The notes of the newest version of LoopMaxPro (CHANGES.md in the root) go into the console: the
# overview shows them. So that file is part of the firmware. The
# history stays in CHANGES.md.
import argparse
import gzip
import hashlib
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WEB = os.path.join(ROOT, "web")
OUT = os.path.join(ROOT, "data", "web")
SIM = os.path.join(ROOT, ".lmp_native", "fs", "web")
# Where the modules are: public, private, and those of the tests (simulator only).
MODULE_ROOTS = [("src/modules", "public"), ("modules_private", "private"), ("test/modules", "testing")]
# The files a build writes, and removes when they are old.
OURS = re.compile(r"^(index\.html|lmp\.[0-9a-f]{8}\.(js|css)|m\.[a-z0-9_]+\.[0-9a-f]{8}\.js)(\.gz)?$")

# Order matters: a file comes after the ones it uses.
SCRIPTS = [
    "i18n/en.js", "i18n/it.js",
    "kit/i18n.js", "kit/store.js", "kit/format.js", "kit/tz.js", "kit/sha256.js",
    "kit/icons.js",
    "kit/ui.js",
    "kit/form.js",
    "console/transport.js", "console/live.js", "console/place.js", "console/schedules.js",
    "console/module-ui.js",
    "console/module-view.js",
    "console/sys-core.js", "console/sys-more.js", "console/devices.js", "console/shell.js",
    "console/app.js",
]
STYLES = ["styles/console.css"]
VENDOR = "vendor/preact-htm.js"

LIMIT_TOTAL = 150 * 1024   # console, compressed: docs/web-console.md

DECL = re.compile(r"^(?:export\s+)?(?:async\s+)?(?:function\*?|const|let|class)\s+([A-Za-z_$][\w$]*)", re.M)
IMPORT = re.compile(r"^import\s[^;]*;[ \t]*\n", re.M)
EXPORT = re.compile(r"^export\s+(?=(?:async\s+)?(?:function|const|let|class)\b)", re.M)


def fail(message):
    sys.stderr.write("console build: %s\n" % message)
    sys.exit(1)


def read(rel):
    with io.open(os.path.join(WEB, rel), encoding="utf-8") as f:
        return f.read()


def vendor_block():
    """The library inside a function of its own: its short names stay there."""
    src = read(VENDOR)
    m = re.search(r"export\s*\{([^}]*)\}\s*;?\s*$", src)
    if not m:
        fail("%s: no export list at the end" % VENDOR)
    pairs = []
    for item in m.group(1).split(","):
        inner, _, outer = item.strip().partition(" as ")
        pairs.append((inner.strip(), (outer or inner).strip()))
    body = src[:m.start()]
    names = ", ".join(outer for _, outer in pairs)
    ret = ", ".join("%s: %s" % (outer, inner) for inner, outer in pairs)
    return "const { %s } = (() => {\n%s\nreturn { %s };\n})();\n" % (names, body, ret)


def vendor_exports():
    m = re.search(r"export\s*\{([^}]*)\}\s*;?\s*$", read(VENDOR))
    return {(item.strip().partition(" as ")[2] or item.strip()).strip()
            for item in m.group(1).split(",")} if m else set()


# Names a file takes from the others: import { a, b as c } from './x.js'.
IMPORTED = re.compile(r"^import\s*\{([^}]*)\}\s*from", re.M)


def check_imports(seen, vendor):
    """Every name a file imports is declared by a file of SCRIPTS, or by the library."""
    for rel in SCRIPTS:
        for group in IMPORTED.findall(read(rel)):
            for item in group.split(","):
                name = item.strip().split(" as ")[0].strip()
                if name and name not in seen and name not in vendor:
                    fail("%s imports '%s', which no file of SCRIPTS declares" % (rel, name))


def check_syntax(script, what="the script"):
    """The JavaScript parser of Node, when there is one: a broken text stops the build here and
    not on the board."""
    node = shutil.which("node")
    if not node:
        print("  syntax not checked: Node is not on this PC")
        return
    with tempfile.NamedTemporaryFile("wb", suffix=".js", delete=False) as f:
        f.write(script)
        path = f.name
    try:
        r = subprocess.run([node, "--check", path], capture_output=True, text=True)
    finally:
        os.remove(path)
    if r.returncode != 0:
        lines = [l for l in (r.stderr or "").splitlines() if l.strip()]
        fail("%s does not parse: %s" % (what, " | ".join(lines[:4])))


def module_block(rel, seen):
    src = read(rel)
    if re.search(r"^export\s+(default|\{)", src, re.M):
        fail("%s: use 'export function|const|class', not export default or export {...}" % rel)
    for name in DECL.findall(src):
        if name in seen:
            fail("'%s' is declared in %s and in %s: names are shared by every file" % (name, seen[name], rel))
        seen[name] = rel
    src = IMPORT.sub("", src)
    src = EXPORT.sub("", src)
    # Whole-line comments go: the rest is left as it is, strings and templates included.
    src = re.sub(r"^[ \t]*//.*\n", "", src, flags=re.M)
    return "// ---- %s\n%s" % (rel, src)


def module_uis(names=None, testing=False):
    """name -> (file of the interface, module.json) of the modules that have one. names: only
    these modules; None: all of them. The modules of the tests only with testing."""
    found = {}
    for folder, kind in MODULE_ROOTS:
        if kind == "testing" and not testing:
            continue
        base = os.path.join(ROOT, folder)
        if not os.path.isdir(base):
            continue
        for name in sorted(os.listdir(base)):
            manifest = os.path.join(base, name, "module.json")
            if name in found or not os.path.isfile(manifest):
                continue
            if names is not None and name not in names:
                continue
            with io.open(manifest, encoding="utf-8") as f:
                meta = json.load(f)
            if not meta.get("ui"):
                continue
            path = os.path.join(base, name, meta["ui"])
            if not os.path.isfile(path):
                fail("%s/module.json names the interface %s, which is not there" % (name, meta["ui"]))
            found[name] = (path, meta)
    return found


def module_ui_script(name, path):
    """The interface of a module as the browser gets it: a script that hands its object to the
    console through window.LMP_UI. It imports nothing: everything comes from ctx."""
    rel = os.path.relpath(path, ROOT).replace(os.sep, "/")
    with io.open(path, encoding="utf-8") as f:
        src = f.read()
    if re.search(r"^\s*import[\s{*]", src, re.M):
        fail("%s: an interface of a module imports nothing, it gets everything from ctx" % rel)
    if len(re.findall(r"^export\s+default\s", src, re.M)) != 1 or \
            re.search(r"^export\s+(?!default\s)", src, re.M):
        fail("%s: one 'export default { uiApi: 1, ... }' and no other export" % rel)
    body = re.sub(r"^export\s+default\s", "const lmpModuleUi = ", src, flags=re.M)
    body = re.sub(r"^[ \t]*//.*\n", "", body, flags=re.M)
    script = ("/* LoopMaxPro: interface of the module %s */\n(() => {\n'use strict';\n%s\n"
              "(window.LMP_UI = window.LMP_UI || []).push([%s, lmpModuleUi]);\n})();\n"
              % (name, body.strip(), json.dumps(name))).encode("utf-8")
    check_syntax(script, rel)
    return script


def check_texts(bundle):
    """Every literal key used by t() must exist in English."""
    en = read("i18n/en.js")
    known = set(re.findall(r"^\s*'?([\w.]+)'?:\s*'", en, re.M))
    known |= set(re.findall(r"(?:^|[\s,{])'?([\w.]+)'?:\s*'", en))
    used = set(re.findall(r"\bt\('([\w.]+)'[,)]", bundle))
    missing = sorted(k for k in used if k not in known)
    if missing:
        fail("texts missing in web/i18n/en.js: %s" % ", ".join(missing))
    it = read("i18n/it.js")
    known_it = set(re.findall(r"(?:^|[\s,{])'?([\w.]+)'?:\s*'", it))
    return sorted(k for k in used if k not in known_it)


NOTES = os.path.join(ROOT, "CHANGES.md")
# Versions the console carries: the newest only. KBs of notes do not take the room of a board;
# the history is in CHANGES.md.
NOTES_KEEP = 1
NOTES_HEAD = re.compile(r"^##\s+(\d+\.\d+\.\d+\S*)[ \t]*(?:\((\d{4}-\d{2}-\d{2})\))?[ \t]*$", re.M)


def release_notes(path=NOTES):
    """The versions of LoopMaxPro, newest first, from CHANGES.md in the root: under
    "## 1.0.1 (2026-10-01)" the lines of text say what the version is about, the bullets what it
    fixes and adds. The console shows them in the overview, on the device and on the server."""
    if not os.path.isfile(path):
        return []
    with io.open(path, encoding="utf-8") as f:
        text = f.read()
    heads = list(NOTES_HEAD.finditer(text))
    found = []
    for i, m in enumerate(heads[:NOTES_KEEP]):
        body = text[m.end():heads[i + 1].start() if i + 1 < len(heads) else len(text)]
        lines = [line.strip() for line in body.splitlines() if line.strip()]
        notes = [line[1:].strip() for line in lines if line[0] in "-*" and line[1:].strip()]
        about = " ".join(line for line in lines if line[0] not in "-*")
        found.append({"v": m.group(1), "date": m.group(2) or "", "text": about, "notes": notes})
    return found


def minify_css(css):
    css = re.sub(r"/\*.*?\*/", "", css, flags=re.S)
    css = re.sub(r"\s+", " ", css)
    css = re.sub(r"\s*([{};,>])\s*", r"\1", css)
    css = re.sub(r";}", "}", css)
    return css.strip() + "\n"


def fingerprint(data):
    return hashlib.sha256(data).hexdigest()[:8]


def gz(data):
    return gzip.compress(data, compresslevel=9, mtime=0)


def build(uis):
    """The files of a console with the interfaces of these modules: file name -> content, and
    the names of the two main files."""
    files = {}
    ui_map = {}
    for name, (path, _meta) in sorted(uis.items()):
        script = module_ui_script(name, path)
        file_name = "m.%s.%s.js" % (name, fingerprint(script))
        files[file_name] = script
        ui_map[name] = file_name

    seen = {}
    parts = ["/* LoopMaxPro console. Preact + htm: Apache-2.0. Icons: Lucide, ISC. */",
             "(() => {", "'use strict';", vendor_block()]
    parts += [module_block(rel, seen) for rel in SCRIPTS]
    parts.append("})();\n")
    text = "\n".join(parts)
    marker = "const MODULE_UI = {};"
    if text.count(marker) != 1:
        fail("console/transport.js: '%s' is where the build writes the interfaces of the modules" % marker)
    text = text.replace(marker, "const MODULE_UI = %s;" % json.dumps(ui_map, sort_keys=True))
    marker = "const RELEASES = [];"
    if text.count(marker) != 1:
        fail("console/sys-core.js: '%s' is where the build writes the versions of LoopMaxPro" % marker)
    text = text.replace(marker, "const RELEASES = %s;" % json.dumps(release_notes(), ensure_ascii=False))
    script = text.encode("utf-8")
    check_imports(seen, vendor_exports())
    check_syntax(script)
    missing_it = check_texts(script.decode("utf-8"))

    style = minify_css("\n".join(read(rel) for rel in STYLES)).encode("utf-8")
    js_name = "lmp.%s.js" % fingerprint(script)
    css_name = "lmp.%s.css" % fingerprint(style)
    page = read("device/index.html").replace("{{JS}}", js_name).replace("{{CSS}}", css_name)
    files.update({js_name: script, css_name: style, "index.html": page.encode("utf-8")})

    total = sum(len(gz(v)) for v in files.values())
    # What a device carries: the console and the interfaces of its modules, at most two with a
    # file of their own, never two that do not go together on a device ("notWith" of
    # module.json: the free and the full version of a module). A package that holds them all
    # is not a device: the sum of all the interfaces together is not held to the limit of
    # one.
    core = sum(len(gz(files[k])) for k in (js_name, css_name, "index.html"))
    sizes = {name: len(gz(files[f])) for name, f in ui_map.items()}
    apart = set()
    for name, (_path, meta) in uis.items():
        for other in meta.get("notWith", []):
            apart.add(frozenset((name, other)))
    names = sorted(sizes, key=lambda n: -sizes[n])
    pair = sizes[names[0]] if names else 0
    for i, a in enumerate(names):
        for b in names[i + 1:]:
            if frozenset((a, b)) not in apart:
                pair = max(pair, sizes[a] + sizes[b])
    device = core + pair
    if device > LIMIT_TOTAL:
        fail("console of %d bytes compressed with its two largest modules, over the limit of %d"
             % (device, LIMIT_TOTAL))
    return {"files": files, "js": js_name, "css": css_name, "modules": ui_map,
            "missing_it": missing_it, "total": total}


def write(folder, files, compressed=True, raw=False):
    """Writes the files in the folder; the files of an older build go."""
    os.makedirs(folder, exist_ok=True)
    for name in os.listdir(folder):
        if OURS.match(name):
            os.remove(os.path.join(folder, name))
    for name, data in files.items():
        if compressed:
            with open(os.path.join(folder, name + ".gz"), "wb") as f:
                f.write(gz(data))
        if raw:
            with open(os.path.join(folder, name), "wb") as f:
                f.write(data)


def report(result, folder):
    files = result["files"]
    mods = ["%s %d B" % (f, len(gz(files[f]))) for f in sorted(result["modules"].values())]
    print("console: %s %d B (%d B raw), %s %d B, index.html %d B%s; total %d B compressed" % (
        result["js"], len(gz(files[result["js"]])), len(files[result["js"]]), result["css"],
        len(gz(files[result["css"]])), len(gz(files["index.html"])),
        "; modules: " + ", ".join(mods) if mods else "", result["total"]))
    print("  written in %s" % os.path.relpath(folder, ROOT))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sim", action="store_true", help="copy into the simulator folder too")
    parser.add_argument("--out", default=OUT, help="output folder")
    parser.add_argument("--modules", default=None,
                        help="the modules whose interface goes in, separated by commas; all when missing")
    args = parser.parse_args()
    names = None if args.modules is None else {n for n in re.split(r"[\s,]+", args.modules) if n}

    result = build(module_uis(names))
    write(args.out, result["files"])
    report(result, args.out)
    if args.sim:
        # The simulator runs the modules of the tests too, such as demo.
        sim = build(module_uis(None if names is None else names | {"demo"}, testing=True))
        write(SIM, sim["files"])
        report(sim, SIM)
    if result["missing_it"]:
        print("  texts in English only, missing in it.js: %s" % ", ".join(result["missing_it"]))


if __name__ == "__main__":
    main()
