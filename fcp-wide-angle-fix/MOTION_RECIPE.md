# Wide Angle Fix – build it in Motion (no Xcode, no code)

Uses Motion's Displacement Map filter driven by a horizontal gradient. Gray 50% = no
movement; darker on the left / lighter on the right pushes the edges inward.

1. Motion ▸ New Project ▸ **Final Cut Effect** ▸ **Filter**. (Set size 1920x1080.)
2. Add a **Gradient** generator (Library ▸ Generators). Rename "Map". Set it to a *Linear*
   gradient, left→right, filling the frame, then edit color stops (Position = x in the frame):

   | Position | Gray |   | Position | Gray |
   |---|---|---|---|---|
   | 0.00 | 0% | | 0.55–0.65 | 50% |
   | 0.10 | 18% | | 0.75 | 55% |
   | 0.20 | 36% | | 0.85 | 73% |
   | 0.30 | 50% | | 0.90 | 82% |
   | 0.35–0.50 | 50% | | 1.00 | 100% |

   (Computed from the tested maths for 33% zones, 10% feather. Max shift ≈ 108 px at 1920 wide.)
3. Turn the Map's visibility off (uncheck) so it doesn't show.
4. On the Filter's drop zone/placeholder, add **Distortion ▸ Displacement Map**.
   Drag **Map** into its *Map Image* well. Map Channel = Luminance (or Red), Amount X ≈ 108,
   Amount Y = 0, enable "Repeat edges" / pixel-edge mirroring.
5. If sides get fatter instead of thinner, set Amount X negative.
6. **Publish** (Inspector ▸ click the dropdown by "Amount X" ▸ Publish) Amount X so it
   appears in Final Cut as "Squeeze". Also publish the Map's gradient stops if you want to
   move the zone (or publish the Map's X position/scale to slide it).
7. File ▸ Save ▸ name "Wide Angle Fix", category e.g. *Distortion*, theme *Wide Angle*.
   It appears in Final Cut's Effects browser.

Limits vs. the Xcode plugin: zone width/feather are baked into the gradient (edit it per
project/resolution), no Link/Show Zones, no per-pixel exact filtering.
