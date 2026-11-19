// Numeric checks for src/core: every measurement, transform and decoder against a known answer.
// Run with `ctest` (or the core-tests executable). Prints one line per check; exits with the failure count.

#include <cmath>
#include <cstdio>
#include <format>
#include <string>
#include <string_view>

#include "core/analysis.hpp"
#include "core/audio_analysis.hpp"
#include "core/detect.hpp"
#include "core/digital.hpp"
#include "core/examples.hpp"
#include "core/generator.hpp"
#include "core/recipe.hpp"
#include "core/transforms.hpp"

using namespace sig;

namespace {

	int failures = 0;

	void check(bool ok, const std::string& what, double value = 0.0) {
		std::printf("%s %-56s %g\n", ok ? "ok  " : "FAIL", what.c_str(), value);
		failures += ok ? 0 : 1;
	}

	bool near(double value, double expected, double tolerance) { return std::abs(value - expected) <= tolerance; }

	Signal tone(double hz, float rate = 48'000.0f, float seconds = 1.0f, bool iq = false, float amplitude = 1.0f) {
		generator::Settings s;
		s.frequency = static_cast<float>(hz);
		s.sample_rate = rate;
		s.seconds = seconds;
		s.iq = iq;
		s.amplitude = amplitude;
		return *generator::synthesize(s);
	}

	fft::Spectrum spectrum_of(const Signal& s, std::size_t size, fft::Window window, std::size_t begin = 0, std::size_t end = 0) {
		return fft::Analyzer(size, window).average(s, begin, end ? end : s.frames());
	}

	double rms(const Signal& s, std::size_t margin = 1000) { return analysis::levels(s, margin, s.frames() - margin).rms; }

	// ---- spectra and measurements ---------------------------------------------------------------------

	void spectra() {
		{
			const auto peak = analysis::strongest(spectrum_of(tone(1000), 4096, fft::Window::FlatTop), 4);
			check(near(peak->hz, 1000, 2), "real tone: peak frequency", peak->hz);
			check(near(peak->db, 0, 0.1), "real tone: full scale reads 0 dBFS (flat top)", peak->db);
		}
		{
			const auto peak = analysis::strongest(spectrum_of(tone(-2000, 48'000, 1, true, 0.5f), 4096, fft::Window::FlatTop));
			check(near(peak->hz, -2000, 2), "I/Q tone at -2 kHz", peak->hz);
			check(near(peak->db, -6.02, 0.1), "I/Q tone, amplitude 0.5: -6 dBFS", peak->db);
		}
		{
			generator::Settings g;
			g.frequency = 1000;
			g.amplitude = 1;
			g.add_noise = true;
			g.snr_db = 30;
			const auto q = analysis::tone_quality(spectrum_of(*generator::synthesize(g), 4096, fft::Window::Hann), false, fft::lobe_half_width(fft::Window::Hann));
			check(near(q->snr_db, 30, 1), "SNR of a tone with 30 dB noise", q->snr_db);
		}
		{
			generator::Settings g;
			g.frequency = 1000;
			g.waveform = generator::Waveform::Square;
			const auto q = analysis::tone_quality(spectrum_of(*generator::synthesize(g), 8192, fft::Window::Blackman), false, 3);
			check(q->thd_db && near(*q->thd_db, -7.36, 0.3), "square wave THD (harmonics 3..9) ~ -7.4 dB", q->thd_db.value_or(0));
		}
		for (const auto window : { fft::Window::Hann, fft::Window::Blackman, fft::Window::FlatTop }) {
			const double limit = window == fft::Window::Hann ? 50 : window == fft::Window::Blackman ? 70 : 85;
			const auto q = analysis::tone_quality(spectrum_of(tone(440, 44'100, 2, false, 0.8f), 4096, window, 0, 4096), false, fft::lobe_half_width(window));
			check(q->snr_db > limit, std::format("clean sine has a high SNR ({})", fft::name(window)), q->snr_db);
		}
		{
			const auto s = tone(1000);
			const auto l = analysis::levels(s, 0, s.frames());
			check(near(l.rms, 0.7071, 0.001), "sine RMS = 0.707", l.rms);
			check(near(l.crest_db(), 3.01, 0.05), "sine crest factor = 3 dB", l.crest_db());
		}
	}

	// ---- alignment ---------------------------------------------------------------------------------------

	void alignment() {
		generator::Settings g;
		g.source = generator::Source::Noise;
		g.seconds = 0.5;
		const Signal a = *generator::synthesize(g);
		Signal delayed = a;
		for (auto& channel : delayed.channels) channel.insert(channel.begin(), 123, 0.0f);

		check(analysis::best_lag(delayed, a) == 123, "best_lag finds +123 samples", static_cast<double>(analysis::best_lag(delayed, a)));
		check(analysis::best_lag(a, delayed) == -123, "best_lag finds -123 samples", static_cast<double>(analysis::best_lag(a, delayed)));
		const auto same = analysis::similarity(delayed, a, 123);
		check(same.correlation > 0.9999 && same.rms_difference < 1e-6, "aligned copies: correlation 1", same.correlation);

		Signal inverted = a;
		for (auto& channel : inverted.channels)
			for (float& v : channel) v = -v;
		check(analysis::similarity(a, inverted, 0).correlation < -0.9999, "inverted copy: correlation -1", analysis::similarity(a, inverted, 0).correlation);
	}

	// ---- transforms --------------------------------------------------------------------------------------

	void filters() {
		const transform::FilterSpec low{ transform::FilterType::LowPass, 0, 1500, 101 };
		check(near(rms(*transform::filter(tone(500), low)), 0.7071, 0.01), "low-pass 1.5 kHz keeps 500 Hz", rms(*transform::filter(tone(500), low)));
		check(rms(*transform::filter(tone(5000), low)) < 0.001, "low-pass 1.5 kHz removes 5 kHz", rms(*transform::filter(tone(5000), low)));

		const transform::FilterSpec band{ transform::FilterType::BandPass, 500, 2000, 301 };
		check(near(rms(*transform::filter(tone(1000), band)), 0.7071, 0.02), "band-pass 0.5-2 kHz keeps 1 kHz", rms(*transform::filter(tone(1000), band)));

		const transform::FilterSpec high{ transform::FilterType::HighPass, 2000, 0, 101 };
		check(rms(*transform::filter(tone(500), high)) < 0.001, "high-pass 2 kHz removes 500 Hz", rms(*transform::filter(tone(500), high)));

		// Long filters run through the FFT; they must give the same answer as the direct sum.
		const Signal noise = *transform::add_noise(tone(300, 48'000, 0.3f), 0, 7);
		const transform::FilterSpec sharp{ transform::FilterType::BandPass, 1000, 3000, 801 };
		const Signal fast = *transform::filter(noise, sharp);
		const Signal slow = *transform::filter_direct(noise, sharp);
		float worst = 0.0f;
		for (std::size_t i = 0; i < fast.frames(); ++i) worst = std::max(worst, std::abs(fast.channels[0][i] - slow.channels[0][i]));
		check(worst < 1e-4f, "FFT filtering = direct filtering", worst);

		// I/Q band-pass around +5 kHz keeps +5 kHz and removes -5 kHz.
		const Signal mix = *transform::combine(tone(5000, 48'000, 1, true), tone(-5000, 48'000, 1, true), transform::Combine::Add);
		const Signal kept = *transform::band_pass(mix, 4000, 6000);
		const auto sp = spectrum_of(kept, 4096, fft::Window::Blackman);
		const auto at = [&](double hz) { return to_db(sp.power[static_cast<std::size_t>(std::lround((hz - sp.first_hz) / sp.bin_hz))]); };
		check(at(5000) > -1 && at(-5000) < -60, "I/Q band-pass keeps +5 kHz, removes -5 kHz", at(-5000));
	}

	void rate_and_frequency() {
		{
			const Signal down = *transform::resample(tone(1000), 8000);
			const auto peak = analysis::strongest(spectrum_of(down, 1024, fft::Window::FlatTop), 4);
			check(down.frames() == 8000 && near(peak->hz, 1000, 1) && near(peak->db, 0, 0.2), "resample 48k -> 8k keeps a 1 kHz tone", peak->db);
			const Signal aliased = *transform::resample(tone(6000), 8000);
			check(rms(aliased, 100) < 0.01, "resample 48k -> 8k removes 6 kHz (no aliasing)", rms(aliased, 100));
			const Signal up = *transform::resample(tone(1000, 8000), 48'000);
			const auto peak_up = analysis::strongest(spectrum_of(up, 4096, fft::Window::FlatTop), 4);
			check(near(peak_up->hz, 1000, 2) && near(peak_up->db, 0, 0.2), "resample 8k -> 48k keeps 1 kHz", peak_up->db);
		}
		{
			const Signal z = *transform::to_iq(tone(1000));
			const auto sp = spectrum_of(z, 4096, fft::Window::Blackman);
			const double negative = to_db(sp.power[static_cast<std::size_t>(std::lround((-1000 - sp.first_hz) / sp.bin_hz))]);
			check(z.is_iq() && near(analysis::strongest(sp)->hz, 1000, 1) && negative < -80, "Hilbert: only +1 kHz remains", negative);
		}
		{
			const Signal shifted = *transform::shift_frequency(tone(1000, 48'000, 1, true), 500);
			check(near(analysis::strongest(spectrum_of(shifted, 4096, fft::Window::Blackman))->hz, 1500, 1), "shift +1 kHz by +500 Hz", 0);
			check(shifted.center_hz == -500.0, "shifting moves the centre frequency the other way", shifted.center_hz);
		}
		{
			// A band of a real signal, extracted: 3 kHz tone in 2.5-3.5 kHz comes out near 0 Hz at a low rate.
			const Signal mix = *transform::combine(tone(3000), tone(9000), transform::Combine::Add);
			const Signal band = *transform::extract_band(mix, 0.0, 1.0, 2500, 3500);
			const auto peak = analysis::strongest(spectrum_of(band, 1024, fft::Window::FlatTop));
			check(band.is_iq() && band.sample_rate < 5000 && near(peak->hz, 0, 5), "extract_band: 3 kHz moves to 0 Hz", peak->hz);
			check(band.center_hz == 3000.0, "extract_band: centre frequency = 3 kHz", band.center_hz);
		}
	}

	void demodulation() {
		generator::Settings g;
		g.frequency = 2000;
		g.message_hz = 20;
		g.modulation = generator::Modulation::AM;
		const Signal am = *transform::am_demodulate(*generator::synthesize(g));
		check(near(analysis::strongest(spectrum_of(am, 16384, fft::Window::Hann), 1)->hz, 20, 1), "AM demodulation recovers the 20 Hz message", 0);

		g.modulation = generator::Modulation::FM;
		g.fm_deviation = 300;
		const Signal fm = *transform::fm_demodulate(*generator::synthesize(g));
		check(near(analysis::strongest(spectrum_of(fm, 16384, fft::Window::Hann), 1)->hz, 20, 1), "FM demodulation recovers the 20 Hz message", 0);
	}

	void editing() {
		check(transform::trim(tone(1000), 0.25, 0.75)->frames() == 24000, "trim keeps 0.5 s");
		const Signal zero = *transform::combine(tone(1000), tone(1000), transform::Combine::Subtract);
		check(analysis::levels(zero, 0, zero.frames()).peak == 0, "A - A = 0");
		check(!transform::combine(tone(1000), tone(1000, 8000), transform::Combine::Add).has_value(), "combine refuses different rates");
		const Signal normal = *transform::normalize(*transform::gain(tone(100), -20));
		check(near(analysis::levels(normal, 0, normal.frames()).peak, 1, 1e-4), "normalise to peak 1");

		generator::Settings g;
		g.modulation = generator::Modulation::Sweep;
		g.frequency = 100;
		g.sweep_to = 4000;
		g.seconds = 5;
		const Signal sweep = *generator::synthesize(g);
		const auto peak = analysis::strongest(spectrum_of(sweep, 4096, fft::Window::Hann, 235'200, 237'600), 4);
		check(near(peak->hz, 3941, 15), "sweep reaches ~3.9 kHz at 4.92 s", peak->hz);
		check(make_example_signals().size() >= 12, "example signals");
	}

	void recipes() {
		std::vector<recipe::Step> steps(3);
		steps[0].op = recipe::Op::Filter;
		steps[0].filter = { transform::FilterType::BandPass, 300, 3400, 201 };
		steps[1].op = recipe::Op::Gain;
		steps[1].gain_db = -3.5f;
		steps[2].op = recipe::Op::Normalize;
		const auto back = recipe::from_text(recipe::to_text(steps));
		check(back && *back == steps, "recipe: text and back gives the same steps");
		check(!recipe::from_text("filter sideways 1 2 3").has_value(), "recipe: a broken line is reported");
		const Signal made = *recipe::run(tone(1000), steps);
		check(near(analysis::levels(made, 0, made.frames()).peak, 1, 1e-4) && made.name.ends_with("| normalised"), "recipe: run applies every step");
	}

	// ---- digital -----------------------------------------------------------------------------------------

	// What the generator sends: the pattern repeated to `count` bits.
	std::string repeated(const std::string& pattern, std::size_t count) {
		std::string out;
		while (out.size() < count) out += pattern;
		out.resize(count);
		return out;
	}

	void digital_round_trip() {
		for (const auto scheme : digital::all_schemes) {
			generator::Settings g;
			g.source = generator::Source::Digital;
			g.scheme = scheme;
			g.frequency = 0;
			g.iq = true;
			g.symbol_rate = 100;
			g.seconds = 0.32f;
			const Signal s = *generator::synthesize(g);
			const std::string bits = digital::decode_signal(s, scheme, { 100, 0.5 }, 0, s.duration());
			check(!bits.empty() && bits == repeated(g.bits, bits.size()), std::format("decode {} ({} bits)", digital::name(scheme), bits.size()), static_cast<double>(bits.size()));
		}

		generator::Settings g;
		g.source = generator::Source::Digital;
		g.frequency = 0;
		const Signal s = *generator::synthesize(g);
		check(digital::eye_traces(s, 0, { 100, 0.5 }, 0, 1, 50, 64).size() == 50 * 64, "eye diagram: 50 traces of 64 points");
	}

	void line_coding() {
		const std::string data = "1011001110001011";
		const std::string chips = digital::manchester_encode(data);
		check(chips.size() == 32 && chips.substr(0, 4) == "1001", "Manchester: 1 -> 10, 0 -> 01");
		check(digital::manchester_decode(chips).bits == data, "Manchester decodes back");
		check(digital::manchester_decode("0" + chips).bits.starts_with(data.substr(0, 15)), "Manchester finds the pair boundary");

		const auto found = digital::find_sync("0000111010110011100", "10110011");
		check(found.size() == 1 && found[0].position == 8 && !found[0].inverted, "sync word found at bit 8");
		const auto inverted = digital::find_sync("0000111001001100011", "10110011");
		check(inverted.size() == 1 && inverted[0].inverted, "inverted sync word found (180 degree ambiguity)");
		check(digital::to_hex("0100100001101001") == "48 69" && digital::to_text("0100100001101001") == "Hi", "bits -> hex / text");
	}

	void recovery() {
		// QPSK with a 37 Hz carrier offset and a symbol rate the receiver doesn't sample exactly in the middle.
		generator::Settings g;
		g.source = generator::Source::Digital;
		g.scheme = digital::Scheme::QPSK;
		g.iq = true;
		g.frequency = 37;
		g.symbol_rate = 1000;
		g.sample_rate = 48'000;
		g.seconds = 1;
		g.bits = "0100100001101001"; // "Hi"
		const Signal s = *generator::synthesize(g);

		const auto r = digital::recover(s, digital::Scheme::QPSK, 1000, 0.0, s.duration());
		check(near(r.frequency_offset_hz, 37, 3), "carrier recovery measures the 37 Hz offset", r.frequency_offset_hz);
		std::size_t tight = 0;
		for (std::size_t i = r.symbols.size() / 2; i < r.symbols.size(); ++i) {
			const auto z = r.symbols[i];
			tight += near(std::abs(z.real()), std::abs(z.imag()), 0.15 * std::abs(z)) ? 1 : 0; // on a diagonal
		}
		check(tight > r.symbols.size() * 0.45, "after locking, QPSK symbols sit on the diagonals", static_cast<double>(tight));

		// FSK round trip.
		g.scheme = digital::Scheme::FSK;
		g.frequency = 0;
		g.symbol_rate = 100;
		g.fsk_deviation = 500;
		const Signal fsk = *generator::synthesize(g);
		const std::string bits = digital::decode_signal(fsk, digital::Scheme::FSK, { 100, 0.5 }, 0, fsk.duration());
		check(bits.size() > 90 && bits.substr(2, 80) == repeated(g.bits + g.bits, 82).substr(2, 80), "FSK decodes", static_cast<double>(bits.size()));
	}

	void detection() {
		// Two bursts of tone in silence.
		Signal s = tone(1000, 48'000, 2);
		for (std::size_t i = 0; i < s.frames(); ++i) {
			const double t = static_cast<double>(i) / 48'000;
			if (!((t > 0.2 && t < 0.5) || (t > 1.2 && t < 1.3))) s.channels[0][i] *= 0.001f;
		}
		const auto bursts = analysis::find_bursts(s, 20.0);
		check(bursts.size() == 2 && near(bursts[0].begin, 0.2, 0.01) && near(bursts[1].end, 1.3, 0.01), "burst finder: 2 bursts at the right times", static_cast<double>(bursts.size()));

		generator::Settings g;
		g.source = generator::Source::Digital;
		g.iq = true;
		g.frequency = 0;
		g.symbol_rate = 1200;
		g.seconds = 1;
		g.bits = "1011001110001011000101";
		for (const auto scheme : { digital::Scheme::OOK, digital::Scheme::BPSK, digital::Scheme::QPSK, digital::Scheme::FSK }) {
			g.scheme = scheme;
			const auto guess = analysis::identify(*generator::synthesize(g), 0, 48'000);
			check(guess.scheme == scheme && near(guess.symbol_rate, 1200, 25),
				std::format("identify {}: {:.0f} baud", digital::name(scheme), guess.symbol_rate), guess.symbol_rate);
		}

		// On a carrier, in noise: the carrier is found too.
		g.scheme = digital::Scheme::BPSK;
		g.frequency = 1200;
		g.symbol_rate = 300;
		g.sample_rate = 24'000;
		g.add_noise = true;
		g.snr_db = 14;
		const auto guess = analysis::identify(*generator::synthesize(g), 0, 24'000);
		check(guess.scheme == digital::Scheme::BPSK && near(guess.symbol_rate, 300, 10) && near(guess.center_hz, 1200, 5),
			"identify BPSK in noise on a 1200 Hz carrier", guess.center_hz);
	}

	// The Guide's "message from space" puzzle, solved the way a user would: identify, shift to 0 Hz,
	// receive with carrier and timing recovery, find the sync word, read the bytes.
	void message_puzzle() {
		std::string bits = "10101010101010101010101011001011";
		for (const char c : std::string_view("RADIO"))
			for (int b = 7; b >= 0; --b) bits += (c >> b) & 1 ? '1' : '0';
		bits += "0000000000000000";
		generator::Settings g;
		g.source = generator::Source::Digital;
		g.scheme = digital::Scheme::BPSK;
		g.iq = true;
		g.frequency = 1200;
		g.symbol_rate = 300;
		g.sample_rate = 24'000;
		g.seconds = 3;
		g.bits = bits;
		g.add_noise = true;
		g.snr_db = 14;
		const Signal s = *generator::synthesize(g);

		const auto id = analysis::identify(s, 0, 24'000);
		const Signal baseband = *transform::shift_frequency(s, -id.center_hz);
		const auto r = digital::recover(baseband, id.scheme, std::round(id.symbol_rate), 0, baseband.duration());
		const std::string decoded = digital::decode(id.scheme, r.symbols);
		bool found = false;
		for (const auto& m : digital::find_sync(decoded, "11001011")) {
			std::string payload = decoded.substr(m.position + 8, 40);
			if (m.inverted) payload = digital::invert(payload);
			found = found || digital::to_text(payload) == "RADIO";
		}
		check(found, "puzzle: the message from space decodes to RADIO");
	}

	// ---- audio -------------------------------------------------------------------------------------------

	void audio() {
		const auto p = analysis::pitch(tone(440, 44'100), 0, 4096);
		check(p && near(p->hz, 440, 1), "pitch of a 440 Hz sine", p ? p->hz : 0);
		const auto n = analysis::nearest_note(445.0);
		check(n.name == "A4" && near(n.cents, 19.56, 0.1), "445 Hz = A4 + 19.6 cents", n.cents);

		generator::Settings g;
		g.waveform = generator::Waveform::Square;
		g.frequency = 220;
		const auto sq = analysis::pitch(*generator::synthesize(g), 0, 4096);
		check(sq && near(sq->hz, 220, 1), "pitch of a 220 Hz square (not a harmonic)", sq ? sq->hz : 0);

		// A full-scale 1 kHz sine reads -3.0 LUFS (EBU Tech 3341 calibration).
		const auto l = analysis::loudness(tone(1000, 48'000, 5), 0, 5 * 48'000);
		check(l.integrated_lufs && near(*l.integrated_lufs, -3.01, 0.1), "loudness of a full-scale 1 kHz sine = -3 LUFS", l.integrated_lufs.value_or(0));
		Signal clipped = tone(100);
		for (float& v : clipped.channels[0]) v = std::clamp(v * 2.0f, -1.0f, 1.0f);
		check(analysis::levels(clipped, 0, clipped.frames()).clipped > 1000, "clipping is counted", static_cast<double>(analysis::levels(clipped, 0, clipped.frames()).clipped));
	}

} // namespace

int main() {
	spectra();
	alignment();
	filters();
	rate_and_frequency();
	demodulation();
	editing();
	recipes();
	digital_round_trip();
	line_coding();
	recovery();
	detection();
	message_puzzle();
	audio();
	std::printf("\n%d failure(s)\n", failures);
	return failures;
}
