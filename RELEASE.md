---
lang: en-GB
---

# DX11 x64 v0.2.1.68

This release includes the frame-pacing and context-menu stability fixes,
asynchronous artwork handling, readable list selections, texture/shader
compatibility improvements and bounded EEL recovery accumulated since v46.

Expression recovery restores 72 of the 114 tested expression-warning contents:
65 using `sound()` and seven with clear missing statement separators. Normal
compilation is attempted first; original preset files are never rewritten.
Forty-two expression-error contents remain unresolved in this version.

Validation: 179 native tests, 26,736 off-screen before/after WARP frames and
12 production-FFT tone cases. Protected thin/dim fixture images are unchanged.
The full-library hardware GPU scan is separate, incomplete and uses frozen v66.
This release does not claim a completed full-library v68 GPU pass.

The undocumented `sound(first,last)` compatibility function uses an inferred
integral of FFT band magnitude in 10-Hz coordinates. Historical API and artistic
parity are unverified. Compilation and finite rendering do not guarantee the
intended appearance of every recovered preset. The alternate NS_EEL2 backend
has not been tested.

The release asset contains only the x64 component, shader includes and licences.
Debug symbols, local settings, preset collections and diagnostic reports are
excluded. Existing profiles and presets are retained by normal component
installation. Close foobar2000 before replacing an installed DLL.

The source tree retains upstream attribution and licences. Build with Visual
Studio C++ v145, the pinned foobar2000 SDK and projectM dependency used by the
workflow, and the NuGet versions in packages.config. Apply external/pmeel.patch
to the pinned projectM revision before building. Use tools/Test-Native.ps1 for
native checks. Do not distribute local test output or debug symbols.
