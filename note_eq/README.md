# Note EQ

A parametric EQ whose band frequencies follow the **note being played**. Built for monophonic
sources such as bass guitar.

Five bell bands, each with **Harmonic** (multiple of the played note's fundamental), **Gain** and **Q**.
Play an A (55 Hz) and band 2 at harmonic `2x` sits at 110 Hz, band 3 at `3x` sits at 165 Hz, and so on.
Play a D and every band slides to that D's fundamental and harmonics.

## How it works
- `src/PitchDetector.h` - YIN pitch tracker running on a decimated copy of the signal (~5 ms updates).
- `src/NoteEQ.h` - tracker -> optional snap to nearest semitone -> glide -> 5 TPT-SVF bell filters
  (stable under modulation, zero latency). Pitch is held for `Hold` ms after the note stops so the
  tail of a note keeps its EQ. Bands above 0.45 x Fs are bypassed.
- `src/Plugin*.{h,cpp}` - JUCE wrapper (AU on macOS, VST3, Standalone).

## Controls
Per band: On, Harmonic (0.5x - 16x), Gain (+-18 dB), Q.
Global: Snap to note, Glide, Hold, Sensitivity, Tuning A, Lowest/Highest note (tracking range), Output.

## What Glide, Hold and Sensitivity do
- **Sensitivity** - how strict the pitch detector is. Low = only accept clean, clearly periodic notes
  (fewer wrong guesses, slower to react to plucky attacks). High = accept shakier pitch (faster, but can
  grab the wrong note on noisy attacks or dead notes). Start in the middle.
- **Hold** - how long the EQ stays on the last note after the detector loses the pitch (note decay,
  muted notes, noisy attack). Short = EQ lets go quickly between notes; long = smoother, but the old
  note's EQ lingers into the gap. Bands fade in/out smoothly, so short values no longer crackle.
- **Glide** - how fast the bands slide for *small* pitch moves (slides, bends, vibrato). A jump of more
  than a semitone (a new note) doesn't glide: the EQ fades out for ~4 ms, moves, and fades back in
  over ~8 ms, so it never sweeps a boosted band across your signal.

## Build
```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target NoteEQ_VST3 NoteEQ_Standalone
```
On macOS the same commands also build the **Audio Unit** (universal arm64 + Intel) and copy it to
`~/Library/Audio/Plug-Ins/Components/Note EQ.component` (VST3 goes to `~/Library/Audio/Plug-Ins/VST3`).
For Logic:
```
cmake --build build --target NoteEQ_AU
auval -v aufx NtEq Izsh        # validate; Logic only loads AUs that pass
```
Then in Logic: Settings > Plug-in Manager > Reset & Rescan Selection (if it doesn't show up), and find it under
Audio Units > izash > Note EQ. Needs Xcode command line tools and CMake (`brew install cmake`).
If Logic still refuses it after auval passes, ad-hoc sign it: `codesign --force --deep -s - "~/Library/Audio/Plug-Ins/Components/Note EQ.component"`.

JUCE 8.0.4 is fetched automatically. On Linux you need the usual JUCE dev packages
(libasound2-dev libfreetype-dev libfontconfig1-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libgl1-mesa-dev).

DSP test (no JUCE needed): `g++ -O2 -std=c++17 -Isrc tests/test_core.cpp -o t && ./t`

## Known limits / roadmap
- Monophonic only; chords will confuse the tracker. Stereo input is tracked per channel.
- Next: per-band compression, per-note skip/ignore mask.
