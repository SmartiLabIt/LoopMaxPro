# LoopMaxPro - the pins of a module on a board, as the build gives them.
# Used by the build (tools/pio/modules.py) and by any tool that lists the products:
# the two cannot disagree.
#
# In module.json:
#   "pinParams":   {"leaf": ["Leaf R", "Leaf G", "Leaf B"], "ring": ["Ring"]}   which parameters are pins
#   "boardPins":   {"esp32c3": {"leaf": [5, 6, 7], "ring": 10},                 by chip (build.mcu)
#                   "board:heltec_v3": {"ring": 4}}                              by board (LMP_BOARD), first
#   "pinOptional": ["sda", "scl"]    a pin that may stay -1: the module does without
# A pin at -1 in the build is taken from boardPins; one still at -1 is not known on that board.


def as_pins(value):
    """The pins of a parameter as a list of numbers: [4, 5], "4|5", 4."""
    if isinstance(value, list):
        return [int(v) for v in value]
    return [int(v) for v in str(value).split("|") if v != ""]


def board_pins(manifest, key, mcu, board):
    table = manifest.get("boardPins", {})
    for where in ("board:" + (board or ""), mcu or ""):
        if where in table and key in table[where]:
            return table[where][key]
    return None


def resolve(manifest, given, mcu, board):
    """The parameters of the module with the pins resolved: {name: value}.

    given: the parameters of the build, as text or values. The pins at -1 take the ones of the
    board, element by element; the other parameters are left as they are."""
    params = dict(manifest.get("params", {}))
    params.update(given)
    for key in manifest.get("pinParams", {}):
        if key not in params:
            continue
        default = manifest["params"].get(key)
        pins = as_pins(params[key])
        if any(p < 0 for p in pins):
            fallback = board_pins(manifest, key, mcu, board)
            if fallback is not None:
                wanted = as_pins(fallback)
                pins = [w if p < 0 else p for p, w in zip(pins, wanted)] if len(wanted) == len(pins) else pins
        params[key] = pins if isinstance(default, list) else pins[0]
    return params


def missing(manifest, params):
    """The pin parameters still at -1 that the module needs."""
    optional = set(manifest.get("pinOptional", []))
    out = []
    for key in manifest.get("pinParams", {}):
        if key in optional or key not in params:
            continue
        if any(p < 0 for p in as_pins(params[key])):
            out.append(key)
    return out
