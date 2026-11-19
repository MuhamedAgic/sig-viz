#include "guide_window.hpp"

#include <array>
#include <string_view>
#include <utility>

#include <imgui.h>

#include "ui.hpp"
#include "core/generator.hpp"
#include "core/transforms.hpp"

namespace {

	// ---- demos: each puts a fitting signal in the viewer ------------------------------------------

	void select_example(Workspace& ws, std::string_view name) {
		for (const TrackPtr& track : ws.library.tracks()) {
			if (track->name() == name) {
				ws.select(track);
				return;
			}
		}
	}

	// view_seconds: how much the scope shows at first (0 = the generator's choice).
	void generate(Workspace& ws, const sig::generator::Settings& settings, const char* name, float view_seconds = 0.0f) {
		auto signal = sig::generator::synthesize(settings);
		if (!signal) return;
		signal->name = name;
		if (view_seconds > 0.0f) signal->default_view_seconds = view_seconds;
		ws.add_and_select(std::move(*signal));
	}

	void aliasing_demo(Workspace& ws) {
		sig::generator::Settings s;
		s.sample_rate = 8000.0f; // can hold up to 4 kHz...
		s.frequency = 7000.0f;   // ...so a 7 kHz tone does not fit
		generate(ws, s, "7 kHz tone sampled at 8 kHz", 0.004f); // 4 ms: four periods of the 1 kHz it turns into
	}

	void delay_demo(Workspace& ws) {
		select_example(ws, "White noise");
		auto later = sig::transform::trim(ws.signal(), 0.1, ws.signal().duration());
		if (!later) return;
		later->name = "White noise, starting 0.1 s later";
		ws.add_and_select(std::move(*later));
	}

	void qpsk_demo(Workspace& ws) {
		sig::generator::Settings s;
		s.source = sig::generator::Source::Digital;
		s.scheme = sig::digital::Scheme::QPSK;
		s.iq = true;
		s.frequency = 0.0f;
		s.add_noise = true;
		s.snr_db = 15.0f;
		generate(ws, s, "QPSK, 100 baud, SNR 15 dB");
	}

	// ---- the lessons ------------------------------------------------------------------------------

	struct Lesson {
		const char* title;
		const char* text;
		void (*demo)(Workspace&) = nullptr; // what "Show me" does (none: no button)
		const char* look = "";              // where to look afterwards
	};

	const std::array lessons{
		Lesson{
			"Signals and samples",
			"A signal is something that changes over time: the air pressure of a sound, the voltage on an antenna. "
			"A computer cannot store a smooth curve, so it measures it many times per second, like the frames of a film. "
			"Each measurement is a sample; the sample rate is how many there are per second (44100 for CD audio).",
			[](Workspace& ws) { select_example(ws, "Sine 440 Hz"); },
			"Signal Viewer: scroll the mouse wheel over the scope to zoom in until you see the separate points.",
		},
		Lesson{
			"Sample rate and aliasing",
			"A sample rate can only capture frequencies up to half of itself (the Nyquist frequency): 48 kHz audio "
			"holds up to 24 kHz. Anything faster is mistaken for something slower, like a wagon wheel in a film "
			"that seems to turn backwards. That is aliasing, and once it happened it cannot be undone. "
			"That is why Resample removes the high frequencies first when it lowers the rate.",
			aliasing_demo,
			"Spectrum: the 7 kHz tone shows up at 1 kHz (8 - 7). Play it: you hear 1 kHz.",
		},
		Lesson{
			"Frequency and the spectrum (FFT)",
			"Most signals are a mix of simple waves, the way a chord is a mix of notes. The FFT (Fast Fourier Transform) "
			"un-mixes them, like a prism splitting white light into colours. The result, the spectrum, shows how strong "
			"every frequency is: one peak per note.\n\n"
			"FFT size is a trade-off: a bigger FFT tells close notes apart better, but needs a longer piece of signal, "
			"so it reacts slower to changes.",
			[](Workspace& ws) { select_example(ws, "Plucked A major chord"); },
			"Spectrum: three peaks, at 440, 554 and 659 Hz (the notes A, C# and E). Try a smaller FFT size: they blur together.",
		},
		Lesson{
			"Decibels (dB)",
			"Levels span an enormous range (a whisper vs. a jet engine), so they are compared on a logarithmic scale: "
			"every -20 dB is ten times smaller in amplitude, every -6 dB about half.\n\n"
			"dBFS means 'compared to full scale': 0 dBFS is the loudest a sample can be, -20 dBFS a tenth of that, "
			"-120 dBFS a millionth. That is how one plot can show a loud tone and faint noise at the same time.",
		},
		Lesson{
			"Windows: Hann, Blackman, ...",
			"The FFT looks at a short piece of signal and pretends it repeats forever. Where the end of the piece meets "
			"its start there is usually a jump, and that jump smears energy over the whole spectrum ('leakage'). "
			"A window fades the piece in and out, like a DJ crossfading, so there is no jump.",
			[](Workspace& ws) { select_example(ws, "Sine 440 Hz"); },
			"Spectrum: switch the window between Rectangular and Blackman. Rectangular: a wide skirt around the peak "
			"(leakage). Blackman: a clean, narrow peak.",
		},
		Lesson{
			"Harmonics and THD",
			"A note of 220 Hz usually also contains 440, 660, 880 Hz... These overtones (harmonics) are why a violin and "
			"a flute playing the same note sound different. Sharp edges need many of them: a square wave has all the odd ones.\n\n"
			"THD (total harmonic distortion) measures how strong the harmonics are compared to the note itself. "
			"An amplifier that adds harmonics changes the sound.",
			[](Workspace& ws) { select_example(ws, "Square 220 Hz"); },
			"Spectrum: peaks at 220, 660, 1100, 1540 Hz..., each a bit lower. The THD is high.",
		},
		Lesson{
			"Noise and SNR",
			"Noise is the random hiss on top of every real signal. SNR (signal-to-noise ratio) says how much stronger "
			"the signal is than the noise, in dB. Think of following one voice at a party: 40 dB is a quiet room, "
			"0 dB means the voice is as loud as the crowd.\n\n"
			"In a spectrum, noise is the 'grass' along the bottom and a tone sticks out of it. A bigger FFT lowers the "
			"grass (it spreads over more bars) but not the tone, so tones can be found even deep in noise.",
			[](Workspace& ws) { select_example(ws, "Noisy sine"); },
			"Spectrum: one peak at 440 Hz above the grass; see the SNR below the plot. Try a bigger FFT size.",
		},
		Lesson{
			"Filters",
			"A filter lets some frequencies through and blocks others, like a sieve for frequencies; the bass and treble "
			"knobs on a stereo are filters. Low-pass keeps the lows, high-pass the highs, band-pass a range.\n\n"
			"The filters here mix every output sample from its neighbours (a weighted average). 'Taps' is how many "
			"neighbours: more taps give a sharper edge between kept and removed.",
			[](Workspace& ws) { select_example(ws, "Noisy sine"); },
			"Transform: Filter, Low-pass to 1000 Hz, Apply. The tone (440 Hz) stays, the hiss above 1 kHz is gone: listen, and look at the Spectrum.",
		},
		Lesson{
			"I/Q: complex signals",
			"A real signal has one number per sample. An I/Q signal has two, I and Q, which together are a point on a "
			"plane. Picture a dot going round a clock face: I is its shadow on the floor, Q its shadow on the wall. "
			"From one shadow you cannot tell which way it turns; with both you can.\n\n"
			"So I/Q can tell +1 kHz (counter-clockwise) from -1 kHz (clockwise), and a real signal cannot: its spectrum "
			"is always mirrored around 0 Hz. Radios use I/Q to look at a band of frequencies around their tuning.",
			[](Workspace& ws) { select_example(ws, "IQ tone +1 kHz"); },
			"Spectrum: a single peak at +1 kHz, nothing at -1 kHz. Viewer: Q (orange) is I (blue) a quarter turn later.",
		},
		Lesson{
			"Modulation: AM and FM",
			"To send a message (voice, music, data) by radio, it is put onto a carrier: a fast wave that travels well. "
			"AM changes how strong the carrier is, like a dimmer; FM changes how fast it vibrates, like a siren. "
			"Demodulating gets the message back out.",
			[](Workspace& ws) { select_example(ws, "AM radio"); },
			"Viewer: the 4 kHz carrier's outline (envelope) traces the 100 Hz message. Then Transform: AM demodulate.",
		},
		Lesson{
			"The spectrogram",
			"A spectrogram puts spectra side by side, like a piano roll: time runs to the right, frequency goes up, and "
			"colour shows how strong each frequency is. A rising tone becomes a slanted line, a chord a few flat lines, "
			"a click a vertical line.",
			[](Workspace& ws) { select_example(ws, "Chirp 100 Hz -> 4 kHz"); },
			"Spectrogram: a straight line climbing from 100 Hz to 4 kHz over 5 seconds.",
		},
		Lesson{
			"Comparing and correlation",
			"Correlation measures how alike two signals are in shape: 1 = identical (even at another volume), "
			"0 = unrelated, -1 = mirror images. Auto-align slides one signal along the other, like a transparency over "
			"a drawing, and keeps the position where they match best. That finds delays, such as an echo or the travel "
			"time of a radio signal.",
			delay_demo,
			"Compare: pick A = White noise and B = this copy, then press Auto-align: it finds the 0.1 s shift.",
		},
		Lesson{
			"Digital: symbols, constellation, eye",
			"Digital radio sends bits as symbols: short stretches of the carrier with a fixed strength and phase. "
			"BPSK has 2 different symbols (1 bit each), QPSK 4 (2 bits each).\n\n"
			"The constellation shows every received symbol as a dot, like darts on a dartboard: a clean signal gives "
			"tight clusters, noise spreads them, and when clusters touch, bits are read wrong. The eye diagram overlays "
			"all symbols like a long-exposure photo; the open 'eye' in the middle is where the receiver decides.",
			qpsk_demo,
			"Digital window: set Scheme to QPSK. Constellation: four fuzzy clusters. Bits: the decoded data.",
		},
	};

} // namespace

void GuideWindow::draw(Workspace& ws) {
	if (!open) return;
	ImGui::SetNextWindowSize({ 450.0f, 600.0f }, ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(title, &open)) {
		ImGui::End();
		return;
	}

	ui::hint("New to signals? Each topic explains one idea with an everyday comparison. "
		"\"Show me\" puts a fitting signal in the viewer and says where to look. "
		"Hover any (?) in the other windows for a quick explanation.");
	ImGui::Spacing();

	for (int i = 0; i < static_cast<int>(lessons.size()); ++i) {
		const Lesson& lesson = lessons[static_cast<std::size_t>(i)];
		ImGui::PushID(i);
		if (ImGui::CollapsingHeader(lesson.title)) {
			ImGui::Indent();
			ImGui::TextWrapped("%s", lesson.text);
			if (lesson.demo) {
				if (ImGui::Button("Show me")) {
					lesson.demo(ws);
					shown_lesson_ = i;
				}
				if (shown_lesson_ == i) {
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{ 0.5f, 0.9f, 0.5f, 1.0f });
					ImGui::TextWrapped("%s", lesson.look);
					ImGui::PopStyleColor();
				}
			}
			ImGui::Unindent();
			ImGui::Spacing();
		}
		ImGui::PopID();
	}

	ImGui::End();
}
