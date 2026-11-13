---
lang: en-GB
---

# Testing the MilkDrop 2 Visualization Component for foobar2000

In the examples shown below, foobar2000 is installed in the `$Env:LOCALAPPDATA\Programs\foobar2000_x64\` folder.

## Runtime testing and debugging

### In foobar2000

See [BUILDING](BUILDING.md) for a sample `foo_vis_milk2/foo_vis_milk2.vcxproj.user`.

## Unit Testing

The isolated 0.2.1.68 DX11 candidate passed 179 native tests. Separator recovery
runs only after the unmodified expression fails to compile, at clear top-level
assignment boundaries across source lines. Tests cover preset, wave and shape
initialisation/drawing, ambiguous syntax, preserved source buffers, host callback
lifetime, single left-to-right argument evaluation and live audio changes.

Before/after WARP tests against all 114 original expression-warning contents
recover 72 cases: 65 with `sound()` calls and seven with missing separators.
All 72 complete 300 frames without expression warnings, fallback shaders or
non-finite mesh coordinates. Another 42 retain expression errors. The retained
two shader recoveries and two protected thin/dim fixtures pass 300-frame
before/after checks; the protected final images remain identical. Including
the 12-frame comparison across all 114 warning cases, these checks cover
26,736 frames. Original-file hashes are checked before and after execution.

`sound(first,last)` uses an inferred compatibility contract: integrated left-FFT
band magnitude over 10-Hz coordinates, clipped to the available spectrum, with
a 44.1-kHz reference bin-energy scale. It does not change existing bass/mid/treble
or PRO shader FFT data. Twelve production-FFT tone checks cover 100, 500 and
3,000 Hz at 44.1, 48, 96 and 192 kHz, physical frequency mapping, linear amplitude
response and cleared silence. This does not establish historical `sound()`
parity or that recovered presets have their intended appearance; some rendered
outputs remain very dark or bright. The alternate NS_EEL2 backend is not tested.

The v68 build, intermediates, resource snapshot and off-screen worker are
isolated. The full-library hardware GPU scan retains its frozen v66 engine,
and the installed component remains v66 until a separately verified installation.
No desktop input/capture or original preset changes are used for these checks.

The isolated 0.2.1.67 candidate passed 168 native tests. Its compiler-side empty
result-selector repair recovers the native composites in the unchanged
`md-96067.milk` and `md-96069.milk` originals. Both compile on shader models
4 and 5. Off-screen WARP checks cover 300 frames before and after the repair
for each preset, plus two protected thin/dim fixtures: 2,400 frames total,
finite geometry, no candidate fallback, and identical protected final images.
This establishes shader recovery and regression checks, not live artistic parity.

The candidate uses separate output and intermediate directories. The running
full-library GPU scan retains its frozen 0.2.1.66 engine and input hashes;
staging this candidate does not update the installed component or mix engines
in that scan. The new candidate still needs live Foobar verification.

The 0.2.1.66 candidate passed all 166 native tests. It removes the temporary context-menu rendering pause while
retaining the 0.2.1.65 deadline fix and single-frame dispatch guard. The owned,
message-only window regression now verifies that three fresh frames advance
inside a nested modal pump while duplicate messages cannot re-enter an active
frame. This does not exercise a visible Foobar menu. The user reports that
0.2.1.65 resolved the right-click soft crashes; live menu animation with the
new candidate requires separate verification.

The 0.2.1.65 UI candidate passed 166 native tests. Its six new frame-pacing
checks use real Win32 events and the production deadline helper to exercise
early/repeated wake signals, elapsed and changed deadlines, stopping and invalid
handles. A separate extracted-loop reproduction produces early requests with
the old wait logic and none with the corrected helper. Wake signals now restart
the scheduler's checks rather than grant permission to post a frame.

The user still observed right-click freezes with 0.2.1.64 installed. A passive
live sample measured 3,598 requests for 1,260 frames over 10.765 seconds. The
subsequent 75-second menu/hang monitor did not capture a fresh stall. These
observations support fixing the scheduler defect, but do not establish that
the user's right-click freeze is resolved. No desktop input, capture or target
thread suspension was used; full-library tests remain stopped.

The 0.2.1.64 UI candidate passed all 160 native tests. Six artwork queue
regressions cover blocked providers, superseded tracks, failed dispatch,
custom/embedded artwork separation and destroyed views. Eight frame-dispatch
regressions cover delayed messages, stale tokens, concurrent producers and
modal pauses, including a real nested Win32 message pump in an owned,
message-only window. No desktop input or capture is used.

The 0.2.1.64 and 0.2.1.65 builds paused visual rendering while the context menu
was open and resumed on dismissal. Their renderer library is byte-identical to the accepted
0.2.1.62 library, so the renderer evidence below remains applicable. These
checks establish the queue and dispatch fixes; they do not establish that the
user's live track-change, random-preset or right-click freezes are resolved.
Installation and live verification are recorded separately in local receipts.
The full-library GPU run remains held during the hang investigation.

The 0.2.1.62 candidate passed 146 native tests and 46 scanner tests. The
native pixel tests cover legacy boolean writes and masks, constant square-root
initialisers, resource-alias macros, vector aspect binding and legacy DDS/TGA
conversion, including authored mip chains and malformed input.

The first complete short pass covered 125,089 distinct contents representing
144,599 original `.milk` paths. Of these, 123,269 are native presets; 1,819 are
MilkDrop 3 composition containers and one is empty. Another 533 `.milk2` paths
represent 157 further distinct compositions; these are recorded as unsupported.
Byte-identical presets share a screening result. No adjacent preset-folder
images were found in the supported texture formats; global textures are shared.

The final candidate completed 83,700 off-screen frames across 93 original
regression cases without non-finite geometry or new shader fallback. Two
existing warp failures remain: their composites now compile, but missing or
invalid warp expressions still require fallback. Dim/static output and
composite-bypass recovery remain separate from artistic acceptance. All seven
previously timed-out sources completed this longer check. Protected thin/dim
images matched the previous baseline exactly across another 3,600 frames.

The production texture loader loaded all 51 tested assets; 32 previously failed.
The legacy image files retained their original hashes. All 144,599 inventoried
preset paths were read back and matched their original SHA256 hashes.

The resumable full-library run is separate from those targeted regressions.
Its first pass uses 12 frames at 30 FPS and 64x48 WARP; flagged sources require
longer tests. Compilation, visible output and intended artistic quality are
separate acceptance levels. The local report records composition containers
separately, including those named `.milk`; their numbered source streams must
not be combined and reported as a working native preset. Originals are read
without modification, and no desktop input or capture is used.

### Using VSTest in CLI

```powershell
.\tools\Test-Native.ps1
```

The runner builds the x64 Release component, the foobar2000 shared runtime and
the native test assembly before invoking VSTest. Its generated files remain in
ignored build directories.

The native suite includes software Direct3D 11 (WARP) regressions for preserving
unchanged shader constants and shader initialisers, and for executing rotation
matrices in HLSL. It also checks that unavailable visual output cannot hide a
preset, and covers DirectWrite layout resizing and shared font changes. These
checks do not require foobar2000 to be running.

Audio-input tests exercise the production fixed-window reader at 8â€“192 kHz
and simulated render rates of 30â€“1,000 FPS. They cover repeated timestamps,
backward seeks, sample-rate changes, startup padding, mono/multichannel input,
unavailable or malformed chunks and non-finite samples. The production FFT
must return identical results at the same playback position regardless of the
preceding render interval. The 0.2.1.48 build passed all 58 native tests; live
checks separately cover buffer availability, pause/resume and backward seeking.

The PRO FFT and three-party review build (0.2.1.50) passed all 74 native tests.
New GPU checks read the processed spectrum through the production shader
binding, verify Hz lookup against actual FFT tones at 44.1â€“192 kHz, check both
texture rows and recreation, and preserve unrelated texture prefixes. Long
reflected resource names must not terminate the host. Sparse preset tests
preserve waveform colour/position defaults. Constant-buffer tests use VS b3
and PS b13 with pixel readback and stale-slot clearing. Window-free lifecycle
checks cover repeated unchanged sizes, invalid inputs and failed resizes;
successful real swapchain changes and device loss remain integration checks.

The 0.2.1.51 preset-recovery build passed all 79 native tests. A new non-zero
pixel regression first failed with the old shader-switching path, then passed
after the built-in transform buffer was rebound. It covers the same return
from a custom shader used by custom waves and the paused/stopped oscilloscope.
Independent feedback/final-image diagnostic readbacks are tested across deferred
command submission. Automatic recovery uses final uniform extreme output only;
unknown samples, sparse lines and dim visible effects must not trigger or
escalate it. Occupancy measurements are diagnostic only. The production renderer
regressions in the local tri-party review compare thin and dim effects with the
previous build.

The 0.2.1.52 tri-party review build passed all 98 native tests. Its local
production-renderer pass completed 26,521 frames across all 15 hidden presets,
thin/dim recovery regressions, shader-wrapping fixtures, the idle ring and an
oversized-shape fixture. Live installed checks covered hidden-tab scheduling,
play/pause/resume/stop and fullscreen return. Pop-out docking, independent
duplicate panels, actual suspend and device-loss fault paths remain integration
limits; the local report records those separately from confirmed passes.

The 0.2.1.53 four-preset recovery build passed all 103 native tests and 20,220
production-renderer frames. GPU checks cover gated zero-rotation repair,
original-coordinate recovery, valid rotations, contrast colour ratios and
reset/blend isolation. Full-render fixtures cover multiple dormant authored
waves, singular mesh equations and the idle ring. All four original presets
show changing late-frame image signatures. The thin line and independent dim
effect match their v52 PNGs exactly; contrast boosting leaves the two dark
presets' feedback buffers identical. The new menu was not driven live because
desktop control and capture were released at the user's request.

The 0.2.1.54 automatic-contrast build passed all 112 native tests and 33,780
accepted production frames, covering all 15 hidden originals. The two confirmed
dark presets automatically match the v53 manually corrected PNGs exactly.
Protected thin/dim images remain identical, renamed contents retain eligibility,
same-name unrelated contents stay neutral, and fresh processes honour stored
enable/disable choices. A detailed dark-to-bright fixture restores the original
final image exactly. Content identity, mixed imports, inverse tone measurement,
hysteresis, ramp uploads and asynchronous statistics have native regressions.
Nine-second short-cycle passes cover both confirmed presets. Contrast uses its
own shorter settling period; blank/source recovery keeps its existing guard.
Automation is content-scoped; these checks do not establish a universal detector
of incorrect artistic brightness. No desktop input or capture was used.

The 0.2.1.55 general contrast build passed 117 native tests and 58,200
production frames, including all 15 hidden originals and an unrecognised
real-file copy. Its display brightness increased with identical feedback
images. Static, flat, sparse, highlighted and grain controls stayed neutral;
bright/flat transitions ended pixel-identical to disabled baselines. Ten
normal/protected controls also remained neutral in an every-frame strength
audit. Fresh processes honoured off/on choices. General detection requires
persistent dark spatial detail, coherent tile variation and movement across
an observation window; it uses a gentler 0.70 exponent. Confirmed cases retain
their validated 0.35 correction. These checks do not establish artistic intent
or guarantee zero false positives for deliberately dark moving artwork.
Tests remain window-free WARP with synthetic audio; live Foobar acceptance,
full-resolution quality and percentage performance overhead are unverified.

The 0.2.1.56 selector-colour build passed 120 native tests. Hidden copies of
the compiled dialog resources cover all 27 selectors (one listbox and 26
comboboxes) in light/dark, selected/unselected, focused/unfocused, disabled
and empty states, including closed combo fields. Checks preserve multiple
selections, Unicode strings and item data, verify native font/row heights,
restore GDI state and detach destroyed-window hooks. Selected dark-mode
colour pairs exceed a 4.5:1 contrast ratio; high-contrast selection uses the
system colour pair without changing OS settings. Renderer sources and the
compiled library are unchanged. No desktop input or capture was used;
live Foobar acceptance and live monitor/DPI changes remain unverified.

The 0.2.1.57 tri-party review build passed 127 native tests and 26,400
production-renderer frames. Three independent reviewers accepted the UI,
host/audio and rendering fixes. New regressions cover long imported names,
post-attachment font changes, child-hook cleanup, contrast-only re-arming,
neutral bypass metadata and early/late extrema with full diagnostic occupancy.
Both CPU band paths retain the same dominant channels for 1,000/5,500/9,000 Hz
tones at 44.1/48/96/192 kHz; reference indices/custom sums, rounded shell means,
zero-rate fallback and low-rate empty bands are checked. A source-extracted
callback probe verifies false on caught exceptions and true on completion.
All 15 hidden originals ran; protected thin/dim images matched their earlier
images exactly. An intentional black output remained original after recovery
was disabled and contrast re-enabled. Evolving/noise/recovery images are not
universal pixel-parity fixtures: ten hidden final PNGs matched the earlier
engine, five differed, and a same-engine repeat also changed one noise case.
No original preset, manual hidden list or desktop input/capture was changed.
Live Foobar appearance and real monitor/DPI transitions remain unverified.

Extended-variable tests execute the production EEL state and transfer paths for
all 64 q variables, then read back pixels from all 16 shader banks. Reload tests
check that upper q values and custom contexts reset. Slot 15 is imported,
evaluated, exported and reloaded to cover the 16-slot boundary.

Mouse tests check both coordinate conventions, the outside sentinel, held and
released button states, the dotted EEL-name adapter and actual shader uploads.
Legacy shader tests use production source preparation and GPU pixel readback
to distinguish the global aspect vector and a non-zero computed global `ret1`
from an uninitialised or zero replacement. Source checks cover comments,
quoted text, nearer local bindings and uncertain preprocessor input.
Validated numeric constant macros do not block the legacy binding fix; macros
that can inject declarations or change type meaning remain excluded. The
single-precision legacy `double3` path also has a non-zero GPU colour check,
with separate source checks preserving shader-model-5 types.
The observed local `point` binding is checked through production compilation
and GPU readback. Scope, shadowing, generated-name collisions and included
token-pasting/stringisation macros have negative regressions.

An isolated x64 Release rebuild from the reviewed source export passed all 48
native tests and all 45 scanner tests. Fixed RGB controls matched the WARP
checks, upper q banks animated in live fixtures, and a slot-15 drawing fixture
was visible. Original md-14888 and md-47753 rendered in both the component and
PRO; original md-90167 was black in both. Common shader mouse coordinates were
checked live. The shipped PRO reported errors for the dotted CPU mouse
fixtures, so the component EEL adapter is a tested extension rather than proven
PRO execution parity. Held/release-pulse behaviour has native regression
coverage; a live release-pulse capture was not completed.

These results establish the stated data paths and scoped observations. They do
not establish synchronised audio/feedback pixel parity or complete MilkDrop
3PRO visual compatibility. Transitions, resizing and reloads need broader
acceptance coverage. Waveform modes 8â€“16, double-preset `.milk2` composition and
complete synchronised PRO FFT comparisons remain separate from the verified
shader-input and Hz-lookup paths.

Runtime recovery requires three consecutive nearly uniform black or white
final images, after an eight-second warm-up, with non-silent audio. Samples
retain audio eligibility across gaps of up to half a second between Foobar
audio chunks; sustained silence or stopped playback cancels the sequence. They
are copied before titles and user sprites and read on a later frame after a
fresh GPU completion event. Unknown samples, flashes, detailed/static output,
transitions and preset editing must not trigger it. A user selection lock does
not disable recovery. A second measured black-feedback path can restore a
dormant authored source behind a dim composite, provided no custom wave/shape
is enabled and the standard waveform is transparent. After a drawing-source
attempt, four seconds of settling and three failing samples permit bypassing
the composite while retaining warp/feedback and the authored drawing source.
If that also fails, the third stage provides a final fallback. That stage bypasses
both preset shaders and the per-pixel warp grid, fading feedback by 0.98 per
frame before drawing the independent audio wave. This must also recover a warp
that maps every pixel to the same texel; bypassing only its composite is not
sufficient. Check portrait/landscape and black/white feedback, then verify the
result in Foobar. No recovery action edits a preset or adds an exclusion.

The context menu can request the same three stages for visible but faulty
presets. "Restore Original Preset Rendering" clears overrides and disables
automatic recovery until that preset is reloaded or recovery is requested
again. Verify that a new preset, resize, device reset or cancelled sample does
not inherit an old GPU measurement. Authored noise is not automatically smoothed.
Explicit "Load Preset..." selection may load a hidden file for inspection;
automatic selection must continue respecting the unchanged hidden list.

Hidden-preset investigations must distinguish shader compilation and isolated
GPU rendering from complete preset playback. Synthetic textures, time and audio
constants can establish that a shader produces pixels; they do not exercise its
EEL expressions, feedback history or transitions in foobar2000. Preserve the
user's hidden list and runtime safety cache during these checks. A preset can
remain excluded by the runtime cache after removal from the hidden list.

Blur sampling checks must include different feedback-texture and window sizes.
The first pass must use the feedback texture's dimensions; subsequent passes
use the preceding blur texture's dimensions. A bounded WARP probe with BGRA8
input verified unchanged output at matching sizes and consistent sampling after
window-size changes. This does not establish that noise authored into a preset
is a rendering fault.

The test binary also requires `foo_vis_milk2.dll` and foobar2000's `shared.dll`
to be present beside `test.dll` at runtime.

- `test.vcxproj` now stages `foo_vis_milk2.dll` automatically.
- `shared.dll` must come from a foobar2000 installation or a local build of
  `external/foobar2000/shared/shared.vcxproj`.

## Using VSTest in GUI

Use Test Explorer via **Test > Test Explorer** (Ctrl+E, T).

## Coverage Collection

### At Runtime

As of Visual Studio 2022 version 17.2, the native DLL binary can be instrumented statically (on disk). Consequently, the Debug configurations link with the `/PROFILE` option. This enables coverage collection.

Enable native static instrumentation by enabling the preview feature "_Code Coverage experience improvements_" in **Tools > Options > Environment > Preview Features**. Alternatively, the [`coverage.runsettings`](test/coverage.runsettings) file sets:

```xml
<CodeCoverage>
  <EnableStaticNativeInstrumentation>True</EnableStaticNativeInstrumentation>
</CodeCoverage>
```

To instrument the component DLL (optional):

```powershell
Microsoft.CodeCoverage.Console.exe instrument "$Env:LOCALAPPDATA\Programs\foobar2000_x64\profile\user-components-x64\foo_vis_milk2\foo_vis_milk2.dll"
```

To run coverage collection while foobar2000 is running, launch it like so:

```powershell
Microsoft.CodeCoverage.Console.exe collect "$Env:LOCALAPPDATA\Programs\foobar2000_x64\foobar2000.exe" --settings "$(Get-Location)\test\coverage.runsettings" --output "$(Get-Location)\test\CodeCoverage\foo_vis_milk2.coverage"
```

Once foobar2000 is closed, open `foo_vis_milk2.coverage` in Visual Studio.

## DirectX "Device Lost" Testing

A "device lost" will not happen often. In Direct3D 9, a 'device lost' would routinely appear from `ALT + TAB` because the GPU used to be an 'exclusive' rather than 'shared' resource.

`DXGI_ERROR_DEVICE_RESET` occurs if the driver crashes or the video hardware hangs.
`DXGI_ERROR_DEVICE_REMOVED` occurs if a new driver is installed while the component is running or if you are running on a 'GPU is in the dock' style laptop and the laptop is undocked. This event can be triggered from the Developer Command Prompt for Visual Studio as 'administrator':

```pwsh
dxcap -forcetdr
```

> It will immediately cause all currently running Direct3D apps to get a `DXGI_ERROR_DEVICE_REMOVED` event.

#### References

- [Customize Code Coverage Analysis](https://learn.microsoft.com/en-us/visualstudio/test/customizing-code-coverage-analysis?view=vs-2022#static-and-dynamic-native-instrumentation)
