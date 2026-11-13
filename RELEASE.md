---
lang: en-GB
---

# DX11 x64 v0.2.1.73

Recover additional damaged preset expressions inside the plugin without editing
the .milk files. This prerelease includes the expression fixes accumulated since
v69 and adds explicitly warned maths inference when a complete original formula
cannot be recovered reliably.

- Seventeen further expression-warning cases compile cleanly through guarded
  punctuation handling and exact statement completions corroborated by variants.
- Four malformed formulae now execute complete expression blocks through
  conservative assumptions, with an approximate-repair warning.
- Two empty assignments retain their previous value, preserving existing output
  without inventing animation. No statements are omitted in these six cases.
- Valid expressions use normal compilation first. Original preset files remain
  unchanged. The bounded partial-statement fallback remains available when a
  complete expression still cannot be made executable.

Validation: 222 native tests and 46,968 off-screen WARP frames. All 114 targeted
warning cases render 300 frames: 108 clean and six with inference warnings.
Previous 108 clean outputs and two partial outputs match under equivalent inputs;
protected thin/dim images also match. Four newly inferred formulae complete
paired 900-frame runs without compile-time shader fallback or non-finite UVs.

Inferred values are assumptions, not verified reconstructions of the author's
formulae. Visual quality and historical parity remain unverified. Full-library
hardware testing remains separate on frozen v66; this is not a full-library v73
GPU pass. The alternate NS_EEL2 backend is not tested. The inferred `sound()`
contract and earlier automatic composite-bypass limitation remain unchanged.

The asset contains the x64 component, shader data and licences only. Profiles,
presets, logs, screenshots, debug symbols and earlier repository history are
excluded. Existing settings and presets are retained during normal installation.
Close foobar2000 before replacing its loaded component. See TESTING.md for details.
