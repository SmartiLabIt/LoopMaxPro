# LoopMaxPro - what goes in the flash of a board, and where, as the platform knows it.
#
#   pio run -e rgb_c3 -t flashinfo      writes $BUILD_DIR/lmp_flash.json
#
# The images the upload writes (bootloader, partition table, boot data), the firmware and its
# address, the filesystem image and its address, the parameters of the flash, esptool.
# A tool that makes a package to flash a board reads it from there:
# the addresses are the ones of the upload, never written again by hand.
import json
import os
import re

Import("env")  # noqa: F821  (provided by PlatformIO)


def esp8266_fs_offset(env):
    """Address of the filesystem in the flash, from the linker script, as the platform does."""
    with open(env.GetActualLDScript()) as f:
        text = f.read()
    m = re.search(r"PROVIDE\s*\(\s*_FS_start\s*=\s*(0x[\da-f]+)\s*\)", text, re.I)
    if not m:
        return None
    value = int(m.group(1), 16)
    if value < 0x40300000:
        return value & 0xFFFFF
    if value < 0x411FB000:
        return (value & 0xFFFFFF) - 0x200000
    return None


def write_info(target, source, env):  # noqa: ARG001
    board = env.BoardConfig()
    platform = env.PioPlatform()
    esptool = os.path.join(platform.get_package_dir("tool-esptoolpy") or "", "esptool.py")
    esp8266 = env.get("PIOPLATFORM") == "espressif8266"
    info = {
        "mcu": board.get("build.mcu", ""),
        "flash_size": board.get("upload.flash_size", "4MB"),
        "images": [],
        "fs": env.subst(os.path.join("$BUILD_DIR", "littlefs.bin" if esp8266 else "${ESP32_FS_IMAGE_NAME}.bin")),
        "python": env.subst("$PYTHONEXE"),
        "esptool": esptool,
    }
    if esp8266:
        # One image from 0, the boot loader inside; the filesystem where the linker script says.
        info.update({"app": {"offset": "0x0", "path": env.subst(os.path.join("$BUILD_DIR", "${PROGNAME}.bin"))},
                     "fs_offset": esp8266_fs_offset(env)})
    else:
        info.update({
            "flash_mode": env.subst("${__get_board_flash_mode(__env__)}"),
            "flash_freq": env.subst("${__get_board_f_image(__env__)}"),
            "images": [{"offset": env.subst(str(offset)), "path": env.subst(path)}
                       for offset, path in env.get("FLASH_EXTRA_IMAGES", [])],
            "app": {"offset": env.subst("$ESP32_APP_OFFSET"),
                    "path": env.subst(os.path.join("$BUILD_DIR", "${PROGNAME}.bin"))},
            "partitions": env.subst("$PARTITIONS_TABLE_CSV"),
        })
    out = env.subst(os.path.join("$BUILD_DIR", "lmp_flash.json"))
    with open(out, "w", encoding="utf-8") as f:
        json.dump(info, f, indent=2)
    print("LoopMaxPro: flash images in %s" % out)


if env.get("PIOPLATFORM") in ("espressif32", "espressif8266"):  # noqa: F821
    env.AddCustomTarget(  # noqa: F821
        name="flashinfo",
        dependencies=None,
        actions=[write_info],
        title="Flash images",
        description="Writes what goes in the flash and where, for the package of the site")
