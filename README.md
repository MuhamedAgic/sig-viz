# sig-viz

Analyse, generate, transform and compare signals: audio and SDR-style I/Q.

## Windows

| Window          | What it does |
|-----------------|--------------|
| **Signal Viewer** | Overview + zoomable scope, playback with sound, load/save (WAV, cf32, cs16, cs8, cu8, f32). Shift + drag selects a range (looped by playback, analysed by the other windows), right-click places two cursors (time and frequency between them). |
| **Spectrum**    | FFT around the playhead (live while playing), of the selection or of the whole signal. Measures RMS, peak, DC, crest factor, strongest frequency, SNR and THD. |
| **Spectrogram** | Frequency over time as an image. Drag the yellow line to move the playhead. |
| **Compare**     | Two signals on one time axis (stacked or on top of each other) plus A - B. Shift B by hand or let Auto-align find the best match (cross-correlation). |
| **Transform**   | Gain, normalise, remove DC, add noise, filters (with response plot), frequency shift, resample, real <-> I/Q, AM/FM demodulation, trim, combine. Results become new signals, so steps can be chained and compared. |
| **Generator**   | Tones (AM, FM, sweep), noise and digital data (OOK, BPSK, QPSK), real or I/Q, with optional noise. |
| **Digital**     | Constellation, eye diagram and decoded bits of a baseband digital signal. |

The **Windows** menu reopens closed windows and resets the layout; **Help** lists the mouse controls.

## Code

```
src/core/   plain C++23, no UI: easy to test and reuse
    signal.hpp       the Signal type, generate(formula)
    fft.hpp          FFT (pocketfft), windows, averaged spectra
    analysis.hpp     levels, SNR / THD, alignment, similarity
    transforms.hpp   everything the Transform window can do
    generator.hpp    synthesised signals
    digital.hpp      symbols <-> bits, eye diagram data
    signal_io.hpp    file formats
    signal_audio.hpp signal -> audible samples
    peak_index.hpp   min/max summary for drawing long signals fast

src/app/    ImGui / ImPlot / raylib
    main.cpp         window, audio device, ImGui, main loop
    app.*            menu bar, docking layout, all windows
    workspace.hpp    what the windows share: signals, playhead, selection, cursors
    library.hpp      Track = immutable signal + peak index, shared via std::shared_ptr
    *_window.*       one class per window
```

## Building

CMake 3.21+ and a C++23 compiler (tested with MSVC 2022). All libraries are in `thirdparty/`:
raylib (window, audio), Dear ImGui (docking branch), rlImGui, ImPlot and pocketfft.
