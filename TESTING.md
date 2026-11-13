---
lang: en-GB
---

# Testing the DX11 component

Build the x64 Release configuration and run `tools/Test-Native.ps1` as described
in BUILDING.md. Tests use synthetic fixtures and an isolated software rendering
device. Preserve original preset files and existing settings when testing.

## v0.2.1.73 validation

The candidate passed 222 native tests and 46,968 off-screen WARP frames.
Across 114 expression-warning test cases, 108 compile without warnings and six
use explicitly warned inferred repairs. The six compile complete expression
blocks without omitting statements: four use inferred maths and two hold the
previous value for a terminal empty assignment. The author's original formulae
and artistic appearance are not established by these checks.

The rules run after normal compilation and corroborated repairs fail. They
preserve supplied decimal digits, infer products from earlier scalar assignments,
remove unmatched closing parentheses, use zero for a missing additive sine/cosine
coefficient and supply a zero comparison bound in the observed malformed
conditional. Empty assignments retain the previous variable value. Original
source text is preserved. Input sizes, edit counts and fallback retries are
bounded; ambiguous opening groups and unknown functions retain diagnostics.

All previous 108 clean outputs and two partial outputs are retained. Of those
110 images, 105 match in the ordinary run and five match with shared noise bytes
and a fixed C++ random seed. Protected thin/dim fixtures retain identical final
images, and both earlier native shader repairs pass. Four newly inferred maths
cases also complete paired 900-frame comparisons at recovery stage zero and
neutral contrast, without compile-time shader fallback or non-finite mesh UVs.

The checks comprise 35,568 ordinary frames, 3,000 paired regression frames,
1,200 protected frames and 7,200 extended comparison frames. Original preset
hashes are checked before and after execution. Some final images are unchanged
despite the previously malformed maths executing; compilation success is not a
visual-quality verdict. The earlier automatic composite bypass is still needed
for an independently failing composite.

Initial full-library hardware screening checked 125,089 distinct contents on a
separate frozen v66 engine. Longer hardware follow-ups continue on that engine.
This does not establish full-library v73 GPU acceptance. The alternate NS_EEL2
component backend retains its earlier behaviour and is not tested here. The
undocumented `sound()` helper still uses an inferred FFT band-integral contract;
historical API parity remains unverified.

## Additional acceptance checks

Test menu animation, track changes, pause/stop waves, resizing and device loss
in foobar2000. Check known-good thin/dim effects alongside repaired cases. Keep
hardware and software rendering results distinct and record the engine version.
Do not publish profiles, original preset collections, screenshots, debug symbols
or runtime logs as release assets. See RELEASE.md for the release scope.
