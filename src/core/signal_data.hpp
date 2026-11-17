#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <random>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sig {

	inline constexpr double tau = 2.0 * std::numbers::pi;

	enum class Layout {
		Real, // one channel
		IQ,   // two channels: I (in-phase) and Q (quadrature)
	};

	[[nodiscard]] constexpr std::string_view channel_label(Layout layout, std::size_t channel) {
		if (layout == Layout::Real) return "";
		return channel == 0 ? "I" : "Q";
	}

	struct Signal {
		std::string name;
		std::string description;
		float  sample_rate;          // Hz (complex sample rate for I/Q)
		float  default_view_seconds; // initial zoom of the scope
		Layout layout;

		// Planar storage, normalised to [-1, 1].
		// Real: { samples }      IQ: { I, Q } (same length)
		std::vector<std::vector<float>> channels;

		[[nodiscard]] std::size_t frames() const { return channels.empty() ? 0 : channels.front().size(); }
		[[nodiscard]] float duration() const { return static_cast<float>(frames()) / sample_rate; }

		// Largest absolute sample value over all channels.
		[[nodiscard]] float peak() const {
			float result = 0.0f;
			for (const auto& channel : channels)
				for (const float v : channel) result = std::max(result, std::abs(v));
			return result;
		}
	};

	// Builds a real signal by evaluating fn(t) at every sample time t (in seconds).
	template <std::invocable<double> Fn>
	[[nodiscard]] Signal make_signal(std::string name, std::string description,
		float sample_rate, float seconds,
		float view_seconds, Fn&& fn) {
		const auto count = static_cast<std::size_t>(sample_rate * seconds);

		std::vector<std::vector<float>> channels;
		channels.push_back(
			std::views::iota(std::size_t{ 0 }, count)
			| std::views::transform([&](std::size_t i) {
				return static_cast<float>(fn(static_cast<double>(i) / sample_rate));
				})
			| std::ranges::to<std::vector>());

		return Signal{
			.name = std::move(name),
			.description = std::move(description),
			.sample_rate = sample_rate,
			.default_view_seconds = view_seconds,
			.layout = Layout::Real,
			.channels = std::move(channels),
		};
	}

	// Builds an I/Q signal; fn(t) returns the complex baseband sample at time t.
	template <std::invocable<double> Fn>
		requires std::same_as<std::invoke_result_t<Fn, double>, std::complex<double>>
	[[nodiscard]] Signal make_iq_signal(std::string name, std::string description,
		float sample_rate, float seconds,
		float view_seconds, Fn&& fn) {
		const auto count = static_cast<std::size_t>(sample_rate * seconds);

		std::vector<float> i(count), q(count);
		for (std::size_t n = 0; n < count; ++n) {
			const std::complex<double> v = fn(static_cast<double>(n) / sample_rate);
			i[n] = static_cast<float>(v.real());
			q[n] = static_cast<float>(v.imag());
		}

		std::vector<std::vector<float>> channels;
		channels.push_back(std::move(i));
		channels.push_back(std::move(q));

		return Signal{
			.name = std::move(name),
			.description = std::move(description),
			.sample_rate = sample_rate,
			.default_view_seconds = view_seconds,
			.layout = Layout::IQ,
			.channels = std::move(channels),
		};
	}

	[[nodiscard]] inline std::vector<Signal> make_example_signals() {
		constexpr float audio_rate = 44'100.0f;
		constexpr float radio_rate = 48'000.0f;

		std::vector<Signal> out;

		out.push_back(make_signal(
			"Sine 440 Hz", "Pure A4 tone.",
			audio_rate, 5.0f, 0.01f,
			[](double t) { return 0.8 * std::sin(tau * 440.0 * t); }));

		out.push_back(make_signal(
			"Square 220 Hz", "Odd harmonics only: sharp edges, buzzy sound.",
			audio_rate, 5.0f, 0.02f,
			[](double t) { return std::sin(tau * 220.0 * t) >= 0.0 ? 0.6 : -0.6; }));

		out.push_back(make_signal(
			"Sawtooth 110 Hz", "All harmonics: bright, rich sound.",
			audio_rate, 5.0f, 0.04f,
			[](double t) { return 0.7 * (2.0 * std::fmod(110.0 * t, 1.0) - 1.0); }));

		out.push_back(make_signal(
			"Plucked A major chord", "Three sines with an exponential decay, re-struck every 2 s.",
			audio_rate, 6.0f, 0.03f,
			[](double t) {
				const double envelope = std::exp(-2.5 * std::fmod(t, 2.0));
				const double chord = std::sin(tau * 440.00 * t)
					+ std::sin(tau * 554.37 * t)
					+ std::sin(tau * 659.25 * t);
				return 0.3 * envelope * chord;
			}));

		out.push_back(make_signal(
			"Chirp 100 Hz -> 4 kHz", "Linear frequency sweep over 5 s.",
			audio_rate, 5.0f, 0.02f,
			[](double t) {
				constexpr double f0 = 100.0, f1 = 4000.0, duration = 5.0;
				constexpr double rate = (f1 - f0) / duration; // Hz per second
				return 0.7 * std::sin(tau * (f0 * t + 0.5 * rate * t * t));
			}));

		out.push_back(make_signal(
			"Noisy sine", "440 Hz tone buried in Gaussian noise.",
			audio_rate, 5.0f, 0.02f,
			[rng = std::mt19937{ 42 },
			noise = std::normal_distribution<double>{ 0.0, 0.2 }](double t) mutable {
				return std::clamp(0.7 * std::sin(tau * 440.0 * t) + noise(rng), -1.0, 1.0);
			}));

		out.push_back(make_signal(
			"White noise", "Uniform random samples.",
			audio_rate, 5.0f, 0.01f,
			[rng = std::mt19937{ 7 },
			dist = std::uniform_real_distribution<double>{ -0.8, 0.8 }](double) mutable {
				return dist(rng);
			}));

		out.push_back(make_signal(
			"AM radio", "4 kHz carrier, amplitude-modulated by a 100 Hz message (70 % depth).",
			radio_rate, 5.0f, 0.02f,
			[](double t) {
				constexpr double carrier = 4000.0, message = 100.0, depth = 0.7;
				const double envelope = (1.0 + depth * std::sin(tau * message * t)) / (1.0 + depth);
				return 0.9 * envelope * std::sin(tau * carrier * t);
			}));

		out.push_back(make_signal(
			"FM radio (slowed down)", "400 Hz carrier, frequency swings +/-300 Hz at 2 Hz.",
			radio_rate, 5.0f, 0.25f,
			[](double t) {
				constexpr double carrier = 400.0, message = 2.0, deviation = 300.0;
				constexpr double beta = deviation / message; // modulation index
				return 0.8 * std::sin(tau * carrier * t + beta * std::sin(tau * message * t));
			}));

		out.push_back(make_signal(
			"OOK burst", "On-off keying at 10 baud, repeating the bit pattern 10110010.",
			radio_rate, 5.0f, 0.8f,
			[](double t) {
				constexpr std::array<bool, 8> bits{ true, false, true, true, false, false, true, false };
				constexpr double baud = 10.0, carrier = 1000.0;
				const auto bit = static_cast<std::size_t>(t * baud) % bits.size();
				return bits[bit] ? 0.8 * std::sin(tau * carrier * t) : 0.0;
			}));

		// --- I/Q (complex baseband) examples ---------------------------------------------------

		out.push_back(make_iq_signal(
			"IQ tone +1 kHz", "Complex exponential: I = cos, Q = sin. Rotates counter-clockwise on the IQ plane.",
			radio_rate, 5.0f, 0.004f,
			[](double t) { return std::polar(0.8, tau * 1000.0 * t); }));

		out.push_back(make_iq_signal(
			"QPSK symbols", "Unfiltered QPSK at 1000 symbols/s: I and Q each jump between +/-0.57.",
			radio_rate, 5.0f, 0.02f,
			[](double t) {
				constexpr double symbol_rate = 1000.0;
				const auto index = static_cast<std::uint32_t>(t * symbol_rate);
				const std::uint32_t symbol = (index * 2654435761u) >> 30; // cheap deterministic "random" 0..3
				const double phase = std::numbers::pi / 4.0 + std::numbers::pi / 2.0 * static_cast<double>(symbol);
				return std::polar(0.8, phase);
			}));

		return out;
	}

} // namespace sig