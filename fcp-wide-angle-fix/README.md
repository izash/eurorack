# Wide Angle Fix – Final Cut Pro effect (FxPlug 4)

Un-stretches the left and right sides of wide-angle shots so people at the
edges of a two/three-shot stop looking fat. The centre is left alone.

## Controls
| Control | What it does |
|---|---|
| Link Left & Right | Editing a Left control copies it to Right (untick for asymmetric framing) |
| Center Line % | Where the untouched centre sits (move it if your subject is off-centre) |
| Zone Width % | How much of the frame, measured from each edge, is corrected. 33 = outer third |
| Feather % | Smooth ramp from "no correction" to "full correction" so there's no visible seam |
| Squeeze % | + slims the side, − widens it (e.g. for anamorphic/ultra-wide cases) |
| Fill Frame | On: every source pixel stays in frame, centre zooms in a touch. Off: centre stays exactly 1:1, edges mirror-fill |
| Show Zones | Red/blue overlay + yellow centre line while you dial it in |
| Mix % | Blend with the original |

All sliders are keyframeable (rack the squeeze during a push-in or pan).

## Build (needs a Mac)
1. Xcode ▸ New Project ▸ **FxPlug** template (FxPlug 4, Swift, "Filter"). Install the
   FxPlug SDK from Apple Developer first.
2. Replace the template's plug-in class with `Sources/WideAnglePlugIn.swift`, add
   `Sources/ShaderTypes.h` (and include it in the bridging header), add `Shaders/WideAngle.metal`.
3. Keep the template's `MetalDeviceCache.swift`; point its pipeline creation at
   `wideAngleVertex` / `wideAngleFragment`.
4. Set the plug-in class/name/UUID in the template's Info.plist, build, run the host app once
   — the effect appears in FCP under *Effects ▸ Distortion* (restart FCP).

## Status / honesty
- `reference/warp_reference.py` (numpy) is the tested maths: edges stay pinned, mapping is
  monotonic, centre is 1:1 when Fill is off. Run it with `python3`.
- The Swift/Metal code was written without Xcode or the FxPlug SDK available, so it has **not
  been compiled**. Expect to fix small API-name/signature differences against your SDK version
  (parameter-API versions, `MetalDeviceCache` helper names, texture Y-origin on tiled renders).

## Things you may want next
- Vertical counter-correction (edge stretch also tilts verticals); lens-profile presets (GoPro/iPhone/16mm).
- Face-tracking to auto-place the zones.
- On-screen drag handles for zone edges (FxPlug custom UI).
