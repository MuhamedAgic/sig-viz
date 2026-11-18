#pragma once

// Builds signals from a handful of settings: tones (optionally AM / FM modulated or swept),
// noise, and digital data (OOK / BPSK / QPSK). Real or I/Q, with optional added noise.

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <expected>
#include <format>
#include <numbers>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "digital.hpp"
#include "signal.hpp"
#include "transforms.hpp"
#include "units.hpp"

namespace sig::generator {

	enum class Source { Tone, Noise, Digital };
	enum class Waveform { Sine, Square, Triangle, Sawtooth };
	enum class Modulation { None, AM, FM, Sweep };

	inline constexpr std::array all_sources{ Source::Tone, Source::Noise, Source::Digital };
	inline constexpr std::array all_waveforms{ Waveform::Sine, Waveform::Square, Waveform::Triangle, Waveform::Sawtooth };
	inline constexpr std::array all_modulations{ Modulation::None, Modulation::AM, Modulation::FM, Modulation::Sweep };

	[[nodiscard]] constexpr std::string_view name(Source source) {
		switch (source) {
		case Source::Tone:    return "Tone";
		case Source::Noise:   return "Noise";
		case Source::Digital: return "Digital data";
		}
		std::unreachable();
	}

	[[nodiscard]] constexpr std::string_view name(Waveform waveform) {
		switch (waveform) {
		case Waveform::Sine:     return "Sine";
		case Waveform::Square:   return "Square";
		case Waveform::Triangle: return "Triangle";
		case Waveform::Sawtooth: return "Sawtooth";
		}
		std::unreachable();
	}

	[[nodiscard]] constexpr std::string_view name(Modulation modulation) {
		switch (modulation) {
		case Modulation::None:  return "None";
		case Modulation::AM:    return "AM";
		case Modulation::FM:    return "FM";
		case Modulation::Sweep: return "Sweep (chirp)";
		}
		std::unreachable();
	}

	struct Settings {
		Source source = Source::Tone;
		bool   iq = false;
		float  sample_rate = 48'000.0f;
		float  seconds = 2.0f;
		float  amplitude = 0.8f;
		float  frequency = 440.0f; // tone, carrier or sweep start, in Hz (may be negative for I/Q)

		// Tone
		Waveform   waveform = Waveform::Sine;
		Modulation modulation = Modulation::None;
		float      message_hz = 5.0f;      // AM / FM: how fast the modulation goes
		float      am_depth = 0.5f;        // AM: 0 = none, 1 = the envelope touches zero
		float      fm_deviation = 100.0f;  // FM: how far the frequency swings, +- Hz
		float      sweep_to = 4000.0f;     // Sweep: frequency at the end

		// Digital data
		digital::Scheme scheme = digital::Scheme::BPSK;
		float           symbol_rate = 100.0f;
		std::string     bits = "1011001110001011"; // repeated for as long as the signal lasts

		// Noise on top
		bool     add_noise = false;
		float    snr_db = 20.0f;
		unsigned seed = 1;
	};

	// One period of the waveform: phase in radians -> value in [-1, 1].
	[[nodiscard]] inline double wave(Waveform waveform, double phase) {
		switch (waveform) {
		case Waveform::Sine:     return std::sin(phase);
		case Waveform::Square:   return std::sin(phase) >= 0.0 ? 1.0 : -1.0;
		case Waveform::Triangle: return 2.0 / std::numbers::pi * std::asin(std::sin(phase));
		case Waveform::Sawtooth: {
			const double cycles = phase / tau;
			return 2.0 * (cycles - std::floor(cycles)) - 1.0;
		}
		}
		std::unreachable();
	}

	// A short name for what the settings make, e.g. "Sine 440 Hz, FM".
	[[nodiscard]] inline std::string describe(const Settings& s) {
		std::string text;
		switch (s.source) {
		case Source::Tone:
			text = std::format("{} {}", name(s.waveform), format_hz(s.frequency));
			if (s.modulation == Modulation::Sweep) text += " -> " + format_hz(s.sweep_to);
			else if (s.modulation != Modulation::None) text += std::format(", {} {}", name(s.modulation), format_hz(s.message_hz));
			break;
		case Source::Noise:
			text = "Noise";
			break;
		case Source::Digital:
			text = std::format("{} {:g} baud", name(s.scheme), s.symbol_rate);
			if (s.frequency != 0.0f) text += " on " + format_hz(s.frequency);
			break;
		}
		if (s.iq) text += " (I/Q)";
		if (s.add_noise) text += std::format(", SNR {:g} dB", s.snr_db);
		return text;
	}

	[[nodiscard]] inline std::expected<Signal, std::string> synthesize(const Settings& s) {
		if (!(s.sample_rate > 0.0f) || !(s.seconds > 0.0f)) return std::unexpected("Sample rate and duration must be greater than 0");
		const double frames = static_cast<double>(s.sample_rate) * s.seconds;
		if (frames > 200e6) return std::unexpected("That would be over 200 million samples; shorten it or lower the sample rate");

		const std::vector<bool> bits = digital::parse_bits(s.bits);
		if (s.source == Source::Digital && bits.empty()) return std::unexpected("Type a bit pattern of 0s and 1s");
		if (s.source == Source::Digital && !(s.symbol_rate > 0.0f)) return std::unexpected("The symbol rate must be greater than 0");

		const auto count = static_cast<std::size_t>(frames);
		const double rate = s.sample_rate;
		std::vector<std::vector<float>> channels(s.iq ? 2 : 1, std::vector<float>(count));
		std::mt19937 rng{ s.seed };
		std::normal_distribution<double> noise{ 0.0, s.amplitude / 3.0 }; // 99.7 % of samples within +-amplitude
		double phase = 0.0;

		for (std::size_t n = 0; n < count; ++n) {
			const double t = static_cast<double>(n) / rate;
			std::complex<double> z; // the I/Q value; a real signal keeps the real part (I)

			switch (s.source) {
			case Source::Tone: {
				double amplitude = s.amplitude;
				double frequency = s.frequency;
				const double message = std::sin(tau * s.message_hz * t);
				switch (s.modulation) {
				case Modulation::None:  break;
				case Modulation::AM:    amplitude *= (1.0 + s.am_depth * message) / (1.0 + s.am_depth); break;
				case Modulation::FM:    frequency += s.fm_deviation * message; break;
				case Modulation::Sweep: frequency += (s.sweep_to - s.frequency) * t / s.seconds; break;
				}
				// I/Q: Q lags I by a quarter period, so a positive frequency turns counter-clockwise.
				z = amplitude * std::complex<double>{ wave(s.waveform, phase), wave(s.waveform, phase - std::numbers::pi / 2.0) };
				phase = std::fmod(phase + tau * frequency / rate, tau); // accumulate, so frequency changes stay smooth
				break;
			}
			case Source::Noise:
				z = { noise(rng), noise(rng) };
				break;
			case Source::Digital: {
				const std::size_t per_symbol = digital::bits_per_symbol(s.scheme);
				const auto first_bit = static_cast<std::size_t>(t * s.symbol_rate) * per_symbol;
				const std::complex<double> symbol = digital::symbol(s.scheme,
					bits[first_bit % bits.size()], bits[(first_bit + 1) % bits.size()]);
				z = static_cast<double>(s.amplitude) * symbol * std::polar(1.0, tau * s.frequency * t); // on a carrier, if frequency != 0
				break;
			}
			}

			channels[0][n] = static_cast<float>(z.real());
			if (s.iq) channels[1][n] = static_cast<float>(z.imag());
		}

		Signal signal{
			.name = describe(s),
			.description = std::format("Generated: {}, {} sample rate, {:g} s.", describe(s), format_hz(rate), s.seconds),
			.sample_rate = s.sample_rate,
			.default_view_seconds = 0.02f,
			.layout = s.iq ? Layout::IQ : Layout::Real,
			.channels = std::move(channels),
		};
		// Show a few periods / symbols at first.
		if (s.source == Source::Digital) signal.default_view_seconds = 8.0f / s.symbol_rate;
		else if (s.source == Source::Tone && s.frequency != 0.0f) signal.default_view_seconds = 4.0f / std::abs(s.frequency);
		signal.default_view_seconds = std::clamp(signal.default_view_seconds, 1e-5f, std::min(10.0f, s.seconds));

		if (s.add_noise) return transform::add_noise(signal, s.snr_db, s.seed + 1);
		return signal;
	}

} // namespace sig::generator
