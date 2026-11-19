# sig-viz

Analyse, generate, transform, compare and decode signals: audio, and SDR-style I/Q radio recordings.

## Windows

| Window          | What it does |
|-----------------|--------------|
| **Signal Viewer** | Overview + zoomable scope, playback with sound, load/save (WAV, cf32, cs16, cs8, cu8, f32, SigMF). Big raw files can be loaded in parts. Shift + drag selects a range (looped by playback, analysed by the other windows), right-click places two cursors. |
| **Spectrum**    | FFT around the playhead (live while playing), of the selection or of the whole signal. Measures RMS, peak, DC, crest factor, strongest frequency, SNR and THD. Right-click puts a marker on a peak (exact level, distance to marker 1, and *Listen* to hear only that frequency). Max hold, and *Freeze trace* to compare before / after. |
| **Spectrogram** | Frequency over time as an image (computed in the background). Shift + drag marks a band: listen to it alone, or extract it as a new signal (moved to 0 Hz, filtered, at a fitting sample rate). |
| **Compare**     | Two signals on one time axis (stacked or on top of each other) plus A - B. Shift B by hand or let Auto-align find the best match (cross-correlation). |
| **Transform**   | Gain, normalise, remove DC, add noise, filters, frequency shift, resample, real <-> I/Q, AM/FM demodulation, trim, combine. A live preview shows the spectrum before and after; listen A/B. Every result remembers its *recipe* (all steps), which can be re-run on another signal, saved and loaded. *Step back* goes to the signal it was made from. |
| **Generator**   | Tones (AM, FM, sweep), noise and digital data (OOK, BPSK, QPSK, FSK; NRZ or Manchester), real or I/Q, with optional noise. |
| **Digital**     | Constellation, eye diagram and decoded bits. *What is this?* guesses the modulation, symbol rate and carrier and explains why. *Follow carrier and timing* decodes like a real receiver (carrier and clock recovery). Sync-word search shows packets as hex and text. |
| **Bursts**      | Every stretch that is clearly above the noise floor; click one to select it. |
| **Audio**       | Tuner (note and cents at the playhead, pitch over time), loudness (LUFS momentary / short-term / integrated, loudness range), histogram of sample values and clipping. |
| **Live**        | The microphone, or an RTL-SDR radio through `rtl_tcp`, as a live signal that every window analyses as it comes in. *Keep a copy* freezes it. |
| **Guide**       | New to signals? 13 short topics, each explained with an everyday comparison, with *Show me* buttons. Plus 5 puzzles: signals with something hidden in them (Morse code in static, a hum, a delay, a digital message, a tricky note). |

Hover any **(?)**, or a measurement's name, for a quick explanation. **Ctrl+P** finds any command by
typing a few letters. Files can be dropped on the window. **File > Save session** keeps all signals,
the playhead and the marks; the status bar shows what the mouse points at in any window. Plots have a
*Save PNG* button.

## Code

```
src/core/   plain C++23, no UI: easy to test and reuse
    signal.hpp         the Signal type, generate(formula)
    fft.hpp            FFT (pocketfft), windows, averaged spectra
    analysis.hpp       levels, SNR / THD, alignment, similarity
    audio_analysis.hpp pitch, notes, loudness (LUFS)
    detect.hpp         burst finder, "what is this?" identification
    transforms.hpp     filters (FFT-based for long ones), shifting, resampling, demodulation, band extraction
    recipe.hpp         transform steps as data: apply, describe, save / load as text
    generator.hpp      synthesised signals
    digital.hpp        symbols <-> bits, line coding, sync words, carrier and timing recovery
    signal_io.hpp      file formats, SigMF, partial loading
    signal_audio.hpp   signal -> audible samples
    peak_index.hpp     min/max summary for drawing long signals fast

src/app/    ImGui / ImPlot / raylib
    main.cpp           window, audio device, ImGui, main loop
    app.*              menus, status bar, command palette, docking layout, all windows
    workspace.hpp      what the windows share: signals, playhead, selection, cursors, hover, audition
    library.hpp        Track = immutable signal + peak index + recipe, shared via std::shared_ptr
    background.hpp     runs slow work on a worker thread
    session.*          save / load the workspace
    live_input.hpp     microphone.cpp (miniaudio, via raylib) and rtl_tcp.cpp (sockets)
    tips.hpp           the short (?) explanations, all in one place
    *_window.*         one class per window (guide_window.cpp holds the lessons and puzzles)

tests/core_tests.cpp   numeric checks of everything in src/core
```

The core headers start each concept with a plain-language comparison before the details: the FFT as a
prism, windows as a DJ crossfade, I/Q as the two shadows of a dot going round a clock face, carrier
recovery as clapping along to a beat, ...

## Building

CMake 3.21+ and a C++23 compiler (tested with MSVC 2022). All libraries are in `thirdparty/`:
raylib (window, audio, microphone), Dear ImGui (docking branch), rlImGui, ImPlot and pocketfft.

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build      # the core tests
```

For live radio: install the RTL-SDR drivers, plug in the stick, run `rtl_tcp`, and press Start in the
Live window.
