# LoopMaxPro - the tunings of a module on a board, as the build gives them.
# Values of LoopMaxPro that a module needs other than the ones every module has, only on the
# boards where it needs them: a tuning for one module must never change the others.
#
# In module.json:
#   "tune": {"esp32c3": {"LMP_WIFI_DYNAMIC_RX_BUFFERS": 16, "LMP_GEO_HTTP": 1},   by chip (build.mcu)
#            "board:heltec_v3": {"LMP_WIFI_DYNAMIC_RX_BUFFERS": 24}}            by board (LMP_BOARD), over it
#
# Only the parameters of include/lmp_config.h that a build may set (#ifndef there), not those that
# say what the board is, and whole numbers. The order: the value of LoopMaxPro, then the one of
# the module, then the one of the product (its build flags, products.ini): a product decides for
# its own firmware. Two modules of one firmware that want different values stop the build.
#
# A module tunes its own parameters the same way: the ones whose name begins with its own, an
# #ifndef in its sources, MYMODULE_MEM of mymodule, say: what it keeps and asks on a board whose
# memory is not what its class says (the first ESP32). Nobody else has that name.
import os
import re

# What the board is, not a tuning.
FIXED = {"LMP_BOARD", "LMP_TIER", "LMP_MODULE_MEM", "LMP_LIGHT"}


def tunables(project):
    """The parameters a module may tune: the #ifndef of include/lmp_config.h, less FIXED."""
    with open(os.path.join(project, "include", "lmp_config.h"), "r", encoding="utf-8") as f:
        return set(re.findall(r"^#ifndef\s+(LMP_[A-Z0-9_]+)", f.read(), re.M)) - FIXED


def of_module(manifest, mcu, board):
    """{parameter: value} the module asks on this board: its chip, then its board over it."""
    table = manifest.get("tune", {})
    out = dict(table.get(mcu or "", {}))
    out.update(table.get("board:" + (board or ""), {}))
    return out


def resolve(wanted, mcu, board, product_flags, allowed):
    """wanted: [(module, manifest)] of a firmware. ([(parameter, value)], [what was done]);
    ValueError when a tuning is not allowed or two modules disagree."""
    chosen, by = {}, {}
    for name, manifest in wanted:
        own = re.sub(r"[^A-Z0-9]", "_", name.upper()) + "_"
        for key, value in sorted(of_module(manifest, mcu, board).items()):
            if key not in allowed and not (key.startswith(own) and re.fullmatch(r"[A-Z0-9_]+", key)):
                raise ValueError("module '%s': %s is not a parameter of LoopMaxPro a module can tune "
                                 "(include/lmp_config.h), nor one of its own (%s...)" % (name, key, own))
            if isinstance(value, bool) or not isinstance(value, int):
                raise ValueError("module '%s': %s must be a whole number" % (name, key))
            if key in chosen and chosen[key] != value:
                raise ValueError("modules '%s' and '%s' want %s at %d and %d: the product decides, in "
                                 "its build_flags" % (by[key], name, key, chosen[key], value))
            chosen[key], by[key] = value, name
    defines, told = [], []
    for key in sorted(chosen):
        if re.search(r"(^|\s)-D\s*%s(=|\s|$)" % key, product_flags or ""):
            told.append("%s of the product, not of %s" % (key, by[key]))
            continue
        defines.append((key, chosen[key]))
        told.append("%s=%d for %s" % (key, chosen[key], by[key]))
    return defines, told
