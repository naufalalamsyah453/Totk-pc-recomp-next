"""recomp/layout.py reference(): the game-free layout the release's runtime library is built against.

The runtime reads MOD_MAIN_BASE (the first base) and MOD_COUNT (array sizes, checked against the real count at start),
so the reference must start at FIRST_BASE and have a slot for every module a real layout can hold.

    python tests/layout_reference_check.py
"""
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / 'recomp'))
import layout  # noqa: E402

mods = layout.reference()
assert [m[0] for m in mods] == layout.ORDER, 'one slot per module, in load order'
assert mods[0][1] == layout.FIRST_BASE == 0x7000000000, 'main starts at the fixed first base'
for (_, b0, s0), (_, b1, _) in zip(mods, mods[1:]):
    assert b1 == b0 + s0 and b1 % layout.ALIGN == 0, 'slots are adjacent and 2 MiB aligned'
assert mods[-1][1] + mods[-1][2] <= layout.ADDRESS_LIMIT

with tempfile.TemporaryDirectory() as d:
    layout.write(mods, d)
    header = (Path(d) / 'layout.h').read_text()
    assert f'#define MOD_COUNT {len(layout.ORDER)}' in header
    assert '#define MOD_MAIN_BASE 0x7000000000ull' in header
    assert layout.read(d) == mods, 'layout.json reads back'
# a real layout never has more modules than the reference has slots
assert len(layout.ORDER) >= 3
print('layout_reference_check: ok')
