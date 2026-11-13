---
lang: en-GB
---

# DX11 x64 v0.2.1.69

Follow-up to v68: recover 19 additional expression-error contents through plugin
compatibility, without changing preset files. Of the 114 warning contents tested,
91 now execute without expression errors; 23 still contain unresolved errors.

- Correct INI parsing of whitespace before `=`, including custom-shape code.
- Keep intrinsic calls separate from same-named variables, and lower extended
  variable identifiers for the projectM expression backend.
- Retry failed compilation after removing empty top-level statements, isolated
  dots before signed numeric operands and recognised numeric suffixes.
- Preserve errors where repairing truncated or ambiguous maths would require
  choosing missing operands or operators.

Validation: 185 native tests and 31,068 off-screen WARP frames. All 19 new
recoveries complete 300 frames without expression warnings, fallback shaders or
non-finite mesh coordinates. All 72 previous recoveries retain identical final
image hashes in the same ordered test; protected thin/dim fixtures also match.

The full-library hardware GPU scan remains separate, incomplete and based on
frozen v66. This release does not claim a full-library v69 GPU pass. Live artistic
appearance is unverified. The v68 `sound()` extension still uses an inferred
10-Hz band-integral interpretation; historical API parity is unverified.
The alternate NS_EEL2 backend has not been tested.

This prerelease contains the x64 component, shaders and licences only. Existing
profiles and presets are retained during normal installation. Close foobar2000
before replacing its loaded component. Local profiles, test logs, preset
collections, debug symbols and the earlier repository history are excluded.

The software source retains upstream licensing and attribution and includes
the exact downstream expression-engine patch against the pinned dependency.
