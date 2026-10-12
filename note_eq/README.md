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
- `src/Plugin*.{h,cpp}` - JUCE wrapper (VST3 / Standalone, add AU on macOS).

## Controls
Per band: On, Harmonic (0.5x - 16x), Gain (+-18 dB), Q.
Global: Snap to note, Glide, Hold, Sensitivity, Tuning A, Lowest/Highest note (tracking range), Output.

## Build
```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target NoteEQ_VST3 NoteEQ_Standalone
```
JUCE 8.0.4 is fetched automatically. On Linux you need the usual JUCE dev packages
(libasound2-dev libfreetype-dev libfontconfig1-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libgl1-mesa-dev).

DSP test (no JUCE needed): `g++ -O2 -std=c++17 -Isrc tests/test_core.cpp -o t && ./t`

## Known limits / roadmap
- Monophonic only; chords will confuse the tracker. Stereo input is tracked per channel.
- Next: per-band compression, per-note skip/ignore mask.
