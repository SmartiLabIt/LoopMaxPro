# LoopMaxPro - where the console of a firmware goes.
#
# ESP32 family: the console and the interfaces of the modules of the firmware go inside
# the program. Before every build they are built from web/ into $BUILD_DIR/lmp_web/ and written
# as an array in $BUILD_DIR/lmp_gen/lmp_web_bundle.h, which src/services/web/web_bundle.cpp
# includes. An update of the program brings its console; the filesystem keeps only the data, and
# its image (buildfs) is empty.
#
# ESP8266: the console goes into the filesystem image, as before. Before buildfs and uploadfs
# it is built from web/ into data/web/.
#
#   pio run -e esp32dev                  firmware with its console
#   pio run -e esp8266_nodemcu -t uploadfs
#
# On ESP8266 the image replaces every file of the board, the settings of the system too.
# To try a console on an ESP32 without a new firmware, upload it as files: the files
# uploaded take the place of those of the program.
import os
import re
import subprocess
import sys

Import("env")  # noqa: F821  (provided by PlatformIO)

FS_TARGETS = {"buildfs", "uploadfs", "uploadfsota"}
NO_BUILD = {"idedata", "envdump", "menuconfig", "clean", "cleanall"}

project = env.subst("$PROJECT_DIR")  # noqa: F821
build_script = os.path.join(project, "tools", "web", "build.py")
targets = set(COMMAND_LINE_TARGETS)  # noqa: F821


def module_names():
    """The interfaces of the modules of this firmware only, as tools/pio/modules.py reads them."""
    wanted = os.environ.get("LMP_MODULES") or env.GetProjectOption("custom_lmp_modules", "")  # noqa: F821
    return [item.partition(":")[0] for item in re.split(r"[\s,]+", wanted.strip()) if item]


def build_console(out):
    result = subprocess.run([sys.executable, build_script, "--out", out,
                             "--modules=" + ",".join(module_names())], cwd=project)
    if result.returncode != 0:
        sys.stderr.write("LoopMaxPro: the console did not build\n")
        env.Exit(1)  # noqa: F821


def bundle_header(folder):
    """The files of the console as C arrays: path, gzip bytes, size."""
    names = sorted(n for n in os.listdir(folder) if n.endswith(".gz"))
    lines = ["// Written by tools/pio/web.py: the console inside the program. Do not edit.",
             "#pragma once", "#include \"services/web/web_bundle.h\"", "",
             "namespace lmp::webgen {", ""]
    entries = []
    for i, name in enumerate(names):
        with open(os.path.join(folder, name), "rb") as f:
            data = f.read()
        rows = [",".join("0x%02x" % b for b in data[k:k + 24]) for k in range(0, len(data), 24)]
        lines.append("static const uint8_t kFile%d[%d] = {\n%s};" % (i, len(data), ",\n".join(rows)))
        entries.append("    {\"/%s\", kFile%d, %d}," % (name[:-3], i, len(data)))
    lines += ["", "static const WebAsset kWebAssets[] = {"] + entries + ["};",
              "static const size_t kWebAssetCount = %d;" % len(names), "",
              "}  // namespace lmp::webgen", ""]
    return "\n".join(lines)


def write_if_changed(path, text):
    """The same header is not written again: nothing is compiled again for it."""
    if os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            if f.read() == text:
                return
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


platform = env.get("PIOPLATFORM")  # noqa: F821
build_dir = env.subst("$BUILD_DIR")  # noqa: F821

if platform == "espressif32":
    web_out = os.path.join(build_dir, "lmp_web")
    gen_dir = os.path.join(build_dir, "lmp_gen")
    if not (targets & NO_BUILD):
        build_console(web_out)
        write_if_changed(os.path.join(gen_dir, "lmp_web_bundle.h"), bundle_header(web_out))
    env.Append(CPPPATH=[gen_dir], CPPDEFINES=["LMP_WEB_BUNDLE"])  # noqa: F821
    # The image of the filesystem is empty: only the data of the board will be there.
    empty = os.path.join(build_dir, "lmp_fs")
    os.makedirs(empty, exist_ok=True)
    env.Replace(PROJECT_DATA_DIR=empty)  # noqa: F821
elif targets & FS_TARGETS:
    build_console(os.path.join(project, "data", "web"))
