#include "guide_window.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <format>
#include <map>
#include <random>
#include <string>
#include <vector>
#include <string_view>
#include <utility>

#include <imgui.h>
#include <imgui_stdlib.h>

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
	void generate(
		Workspace&                      ws,
		const sig::generator::Settings& settings,
		const char*                     name,
		float                           view_seconds = 0.0f
	) {
		auto signal = sig::generator::synthesize(settings);
		if (!signal) {
			return;
		}
		signal->name = name;
		if (view_seconds > 0.0f) {
			signal->default_view_seconds = view_seconds;
		}
		ws.add_and_select(std::move(*signal));
	}

	void aliasing_demo(Workspace& ws) {
		sig::generator::Settings s;
		s.sample_rate = 8000.0f;                                // can hold up to 4 kHz...
		s.frequency = 7000.0f;                                  // ...so a 7 kHz tone does not fit
		generate(ws, s, "7 kHz tone sampled at 8 kHz", 0.004f); // 4 ms: four periods of the 1 kHz it turns into
	}

	void delay_demo(Workspace& ws) {
		select_example(ws, "White noise");
		auto later = sig::transform::trim(ws.signal(), 0.1, ws.signal().duration());
		if (!later) {
			return;
		}
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
		Lesson{
			"Error correction and interleaving",
			"Noise always breaks a few bits. Error correction adds redundancy in a clever way, so the receiver can repair "
			"them without asking again, like spelling 'M as in Mike' on a bad phone line. Polar codes (used in 5G) do this "
			"with codewords of N bits, of which K carry the message.\n\n"
			"A receiver also knows how sure it is of every bit ('soft bits'), and a good decoder suspects the doubtful "
			"ones first. Errors that come in a burst (a click, a fade) are the hardest: too many in one codeword. An "
			"interleaver shuffles the bits before sending and back after receiving, so a burst becomes a few scattered "
			"errors spread over many codewords, which each can repair.\n\n"
			"Open the Error correction window (Windows menu). Its default damage is a burst of 32 wrong bits: switch the "
			"interleaver between None and Block and watch the error map and the decoded message. 'Make a signal from the "
			"sent bits' turns it into a real signal you can hear, add noise to (Transform) and decode again.",
		},
	};

	// ---- puzzles: a signal with something hidden in it ----------------------------------------------

	// Morse code: a tone switched on and off, hidden in noise so that only the spectrogram shows it.
	sig::Signal morse_signal(std::string_view word) {
		static const std::map<char, std::string_view> code{ { 'E', "." },   { 'H', "...." }, { 'L', ".-.." },
			                                                { 'O', "---" }, { 'S', "..." },  { 'W', ".--" },
			                                                { 'A', ".-" },  { 'V', "...-" } };
		constexpr double                              dot = 0.08;
		std::vector<std::pair<double, double>>        on; // seconds
		double                                        t = 0.3;
		for (const char letter : word) {
			for (const char mark : code.at(letter)) {
				const double length = mark == '.' ? dot : 3 * dot;
				on.emplace_back(t, t + length);
				t += length + dot;
			}
			t += 2 * dot; // 3 dots between letters
		}
		const double seconds = t + 0.3;
		sig::Signal  s = sig::generate(
			"Puzzle: listen to the static",
			"A noisy recording. Is something hiding in it?",
			8000.0f,
			static_cast<float>(seconds),
			0.02f,
			[&](double time) {
				const bool keyed =
					std::ranges::any_of(on, [&](const auto& p) { return time >= p.first && time < p.second; });
				return keyed ? 0.12 * std::sin(sig::tau * 1250.0 * time) : 0.0;
			}
		);
		// Noise at a fixed level, four times stronger than the tone: you hear static, the spectrogram sees more.
		std::mt19937                    rng{ 7 };
		std::normal_distribution<float> noise{ 0.0f, 0.25f };
		for (float& v : s.channels[0]) {
			v += noise(rng);
		}
		return s;
	}

	sig::Signal hum_signal() {
		std::mt19937                     rng{ 3 };
		std::normal_distribution<double> noise{ 0.0, 0.02 };
		return sig::generate(
			"Puzzle: the humming recording",
			"A melody, and something else.",
			8000.0f,
			4.0f,
			0.05f,
			[&](double t) {
				const double melody = 0.3 * std::sin(sig::tau * (t < 2.0 ? 523.25 : 659.25) * t);
				const double hum = 0.08 * std::sin(sig::tau * 60.0 * t) + 0.04 * std::sin(sig::tau * 180.0 * t);
				return melody + hum + noise(rng);
			}
		);
	}

	// The same sound arriving at two microphones, the second one 23 ms later (7.9 m further away).
	std::pair<sig::Signal, sig::Signal> microphones() {
		std::mt19937                     rng{ 5 };
		std::normal_distribution<double> noise{ 0.0, 0.2 };
		std::vector<double>              sound(16000);
		for (std::size_t n = 0; n < sound.size(); ++n) {
			const double t = static_cast<double>(n) / 16000.0;
			sound[n] = (t > 0.2 && t < 0.4) ? noise(rng) * std::sin(sig::tau * 3.0 * t) : 0.01 * noise(rng);
		}
		const auto at = [&](double t, double delay) {
			const auto n = static_cast<std::ptrdiff_t>(std::lround((t - delay) * 16000.0));
			return n >= 0 && n < static_cast<std::ptrdiff_t>(sound.size()) ? sound[static_cast<std::size_t>(n)] : 0.0;
		};
		return {
			sig::generate(
				"Puzzle: microphone 1",
				"A clap, heard by microphone 1.",
				16000.0f,
				1.0f,
				0.05f,
				[&](double t) { return at(t, 0.0); }
			),
			sig::generate(
				"Puzzle: microphone 2",
				"The same clap, heard by microphone 2.",
				16000.0f,
				1.0f,
				0.05f,
				[&](double t) { return 0.6 * at(t, 0.023); }
			),
		};
	}

	sig::Signal message_signal() {
		std::string bits = "10101010101010101010101011001011"; // preamble, then the sync word 11001011
		for (const char c : std::string_view("RADIO")) {
			for (int b = 7; b >= 0; --b) {
				bits += (c >> b) & 1 ? '1' : '0';
			}
		}
		bits += "0000000000000000"; // a pause before it repeats
		sig::generator::Settings s;
		s.source = sig::generator::Source::Digital;
		s.scheme = sig::digital::Scheme::BPSK;
		s.iq = true;
		s.frequency = 1200.0f; // not at 0 Hz: part of the puzzle
		s.symbol_rate = 300.0f;
		s.sample_rate = 24000.0f;
		s.seconds = 3.0f;
		s.bits = bits;
		s.add_noise = true;
		s.snr_db = 14.0f;
		auto signal = sig::generator::synthesize(s);
		signal->name = "Puzzle: a message from space";
		signal->description = "Somebody is sending something digital. What does it say?";
		return std::move(*signal);
	}

	// A plucked string whose second harmonic is louder than the note itself.
	sig::Signal note_signal() {
		return sig::generate(
			"Puzzle: which note?",
			"A plucked string. Careful: the loudest frequency is not the note.",
			44100.0f,
			2.0f,
			0.02f,
			[](double t) {
				const double f = 196.0; // G3
				const double decay = std::exp(-2.0 * t);
				return decay * (0.25 * std::sin(sig::tau * f * t) + 0.5 * std::sin(sig::tau * 2 * f * t) +
			                    0.2 * std::sin(sig::tau * 3 * f * t));
			}
		);
	}

	struct Puzzle {
		const char* title;
		const char* question;
		const char* hint;
		void (*make)(Workspace&);
		bool (*check)(const std::string& answer); // answer in upper case, trimmed
		const char* solution;
	};

	double number_in(const std::string& text) {
		try {
			return std::stod(text);
		}
		catch (...) {
			return std::nan("");
		}
	}

	const std::array puzzle_list{
		Puzzle{
			"The static",
			"This recording sounds like static. But someone is tapping a word in Morse code. Which word?",
			"Look at the Spectrogram (try FFT size 512 and dB range -80 to -20): one frequency switches on and off. "
			"Short = dot, long = dash. H = ...., E = ., L = .-.., O = ---",
			[](Workspace& ws) { ws.add_and_select(morse_signal("HELLO")); },
			[](const std::string& a) { return a == "HELLO"; },
			"HELLO",
		},
		Puzzle{
			"The hum",
			"A melody was recorded next to a noisy power supply. At which frequency (Hz) does it hum?",
			"Spectrum: switch on Log frequency, look below the melody's notes. Right-click a peak to put a marker on it.",
			[](Workspace& ws) { ws.add_and_select(hum_signal()); },
			[](const std::string& a) { return std::abs(number_in(a) - 60.0) < 1.5; },
			"60 Hz (the mains in America; 50 Hz in Europe), with a harmonic at 180 Hz",
		},
		Puzzle{
			"Two microphones",
			"Two microphones heard the same clap. How many milliseconds later did it reach microphone 2?",
			"Compare: A = microphone 1, B = microphone 2, then Auto-align. Or place cursors (right-click) on the same spot in both.",
			[](Workspace& ws) {
				auto [one, two] = microphones();
				ws.library.add(std::move(one));
				ws.add_and_select(std::move(two));
			},
			[](const std::string& a) { return std::abs(number_in(a) - 23.0) < 2.0; },
			"23 ms: sound travels 343 m/s, so microphone 2 was about 7.9 m further away",
		},
		Puzzle{
			"A message from space",
			"Something digital is being sent. What word does it carry?",
			"Digital window: What is this? -> Shift it down to 0 Hz -> What is this? again -> Use these settings. "
			"Then Bits: sync word 11001011, Bytes 5.",
			[](Workspace& ws) { ws.add_and_select(message_signal()); },
			[](const std::string& a) { return a == "RADIO"; },
			"RADIO (BPSK at 300 baud on a 1200 Hz carrier)",
		},
		Puzzle{
			"Which note?",
			"Which note does the string play? (like A4 or C#3)",
			"The Spectrum's strongest peak is an overtone. The Audio window's tuner finds the note itself.",
			[](Workspace& ws) { ws.add_and_select(note_signal()); },
			[](const std::string& a) { return a == "G3"; },
			"G3 (196 Hz); the loudest peak is its second harmonic at 392 Hz (G4)",
		},
	};

} // namespace

std::vector<std::string> GuideWindow::topics() {
	std::vector<std::string> out;
	for (const Lesson& lesson : lessons) {
		out.emplace_back(lesson.title);
	}
	return out;
}

std::vector<std::string> GuideWindow::puzzles() {
	std::vector<std::string> out;
	for (const Puzzle& puzzle : puzzle_list) {
		out.emplace_back(puzzle.title);
	}
	return out;
}

void GuideWindow::show_topic(int index) {
	open = true;
	open_topic_ = index;
	ImGui::SetWindowFocus(title);
}

void GuideWindow::show_puzzle(int index) {
	open = true;
	open_puzzle_ = index;
	ImGui::SetWindowFocus(title);
}

void GuideWindow::draw_puzzles(Workspace& ws) {
	ImGui::SeparatorText("Puzzles: find what is hidden");
	ui::hint("Each puzzle adds a signal with something hidden in it. Use the app's windows to find it.");
	for (int i = 0; i < static_cast<int>(puzzle_list.size()); ++i) {
		const Puzzle& puzzle = puzzle_list[static_cast<std::size_t>(i)];
		Attempt&      attempt = attempts_[static_cast<std::size_t>(i)];
		ImGui::PushID(1000 + i);
		if (open_puzzle_ == i) {
			ImGui::SetNextItemOpen(true);
			ImGui::SetScrollHereY(0.0f);
			open_puzzle_ = -1;
		}
		const std::string header = std::format("{}{}", attempt.solved ? "(solved) " : "", puzzle.title);
		if (ImGui::CollapsingHeader(header.c_str())) {
			ImGui::Indent();
			ImGui::TextWrapped("%s", puzzle.question);
			if (ImGui::Button("Get the signal")) {
				puzzle.make(ws);
			}
			ImGui::SetNextItemWidth(ui::em(10.0f));
			const bool entered = ImGui::InputTextWithHint(
				"##answer",
				"your answer",
				&attempt.answer,
				ImGuiInputTextFlags_EnterReturnsTrue
			);
			ImGui::SameLine();
			if (ImGui::Button("Check") || entered) {
				std::string clean = attempt.answer;
				std::erase_if(clean, [](unsigned char c) { return std::isspace(c); });
				std::ranges::transform(clean, clean.begin(), [](unsigned char c) {
					return static_cast<char>(std::toupper(c));
				});
				attempt.solved = puzzle.check(clean);
				++attempt.tries;
			}
			if (attempt.tries > 0) {
				if (attempt.solved) {
					ImGui::TextColored({ 0.4f, 1.0f, 0.5f, 1.0f }, "Correct! %s", puzzle.solution);
				}
				else {
					ImGui::TextColored(
						{ 1.0f, 0.6f, 0.4f, 1.0f },
						"Not quite. Try again%s",
						attempt.hint ? "." : ", or ask for a hint."
					);
				}
			}
			if (!attempt.solved) {
				if (!attempt.hint && ImGui::SmallButton("Hint")) {
					attempt.hint = true;
				}
				if (attempt.hint) {
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{ 0.5f, 0.9f, 0.5f, 1.0f });
					ImGui::TextWrapped("%s", puzzle.hint);
					ImGui::PopStyleColor();
				}
				if (attempt.tries >= 3) {
					ImGui::SameLine();
					if (ImGui::SmallButton("Show the answer")) {
						attempt.answer = "";
						attempt.solved = true;
					}
				}
			}
			ImGui::Unindent();
			ImGui::Spacing();
		}
		ImGui::PopID();
	}
}

void GuideWindow::draw(Workspace& ws) {
	if (!open) {
		return;
	}
	ImGui::SetNextWindowSize({ 450.0f, 600.0f }, ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(title, &open)) {
		ImGui::End();
		return;
	}

	ui::hint(
		"New to signals? Each topic explains one idea with an everyday comparison. "
		"\"Show me\" puts a fitting signal in the viewer and says where to look. "
		"Hover any (?) in the other windows for a quick explanation."
	);
	ImGui::Spacing();

	for (int i = 0; i < static_cast<int>(lessons.size()); ++i) {
		const Lesson& lesson = lessons[static_cast<std::size_t>(i)];
		ImGui::PushID(i);
		if (open_topic_ == i) {
			ImGui::SetNextItemOpen(true);
			ImGui::SetScrollHereY(0.0f);
			open_topic_ = -1;
		}
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

	draw_puzzles(ws);
	ImGui::End();
}
