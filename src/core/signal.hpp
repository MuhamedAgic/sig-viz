#pragma once

// The data type everything else works on, plus a way to synthesise one from a formula.

#include <complex>
#include <concepts>
#include <cstddef>
#include <numbers>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace sig {

	inline constexpr double tau = 2.0 * std::numbers::pi;

	// Real or I/Q ("complex").
	//
	// Picture a dot going round a clock face. A real signal only records its shadow on the floor: the
	// shadow moves back and forth, but you cannot tell which way the dot turns. An I/Q signal also records
	// the shadow on the wall (Q), so the direction is known. That is why, for I/Q, +1 kHz (counter-clockwise)
	// and -1 kHz (clockwise) are different frequencies, while a real signal's spectrum is always mirrored.
	enum class Layout {
		Real, // one channel
		IQ,   // two channels: I (in-phase, the floor shadow) and Q (quadrature, the wall shadow)
	};

	[[nodiscard]] constexpr std::string_view channel_label(Layout layout, std::size_t channel) {
		if (layout == Layout::Real) {
			return "";
		}
		return channel == 0 ? "I" : "Q";
	}

	struct Signal {
		std::string name;
		std::string description;
		float       sample_rate;          // Hz (complex sample rate for I/Q)
		float       default_view_seconds; // initial zoom of the scope
		Layout      layout;

		// Planar storage, normalised to [-1, 1].
		// Real: { samples }      IQ: { I, Q } (same length)
		std::vector<std::vector<float>> channels;

		// For radio recordings: the frequency the receiver was tuned to, so 0 Hz in the signal is this many
		// Hz on the dial. Frequency axes add it. 0 = not a radio recording (or not known).
		double center_hz = 0.0;

		[[nodiscard]] std::size_t frames() const {
			return channels.empty() ? 0 : channels.front().size();
		}
		[[nodiscard]] float duration() const {
			return static_cast<float>(frames()) / sample_rate;
		}
		[[nodiscard]] bool is_iq() const {
			return layout == Layout::IQ;
		}

		// Sample i as a complex number: (x, 0) for a real signal, (I, Q) for an I/Q one.
		// Lets algorithms treat both kinds the same way.
		[[nodiscard]] std::complex<float> complex_at(std::size_t i) const {
			return { channels[0][i], is_iq() ? channels[1][i] : 0.0f };
		}
	};

	// A formula fn(t) giving the sample at time t (seconds): either a real value or a complex
	// baseband value.
	template <typename Fn>
	concept RealFormula = std::invocable<Fn&, double> && std::convertible_to<std::invoke_result_t<Fn&, double>, double>;

	template <typename Fn>
	concept IqFormula =
		std::invocable<Fn&, double> && std::same_as<std::invoke_result_t<Fn&, double>, std::complex<double>>;

	// Builds a signal by evaluating fn(t) at every sample time. A formula that returns
	// std::complex<double> gives an I/Q signal, anything else a real one.
	template <typename Fn>
		requires RealFormula<Fn> || IqFormula<Fn>
	[[nodiscard]] Signal generate(
		std::string name,
		std::string description,
		float       sample_rate,
		float       seconds,
		float       view_seconds,
		Fn          fn
	) {
		constexpr bool iq = IqFormula<Fn>;
		const auto     count = static_cast<std::size_t>(sample_rate * seconds);

		std::vector<std::vector<float>> channels(iq ? 2 : 1, std::vector<float>(count));
		for (std::size_t n = 0; n < count; ++n) {
			const auto value = fn(static_cast<double>(n) / sample_rate);
			if constexpr (iq) {
				channels[0][n] = static_cast<float>(value.real());
				channels[1][n] = static_cast<float>(value.imag());
			}
			else {
				channels[0][n] = static_cast<float>(value);
			}
		}

		return Signal{
			.name = std::move(name),
			.description = std::move(description),
			.sample_rate = sample_rate,
			.default_view_seconds = view_seconds,
			.layout = iq ? Layout::IQ : Layout::Real,
			.channels = std::move(channels),
		};
	}

} // namespace sig
