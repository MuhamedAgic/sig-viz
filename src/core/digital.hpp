#pragma once

// Digital modulation: bits <-> symbols, plus the data behind the constellation and eye diagram.
//
// Decoding expects a baseband signal (carrier at 0 Hz) and a known symbol rate. A signal on a
// carrier can be brought down with transform::shift_frequency first.

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "signal.hpp"

namespace sig::digital {

	enum class Scheme { OOK, BPSK, QPSK };
	inline constexpr std::array all_schemes{ Scheme::OOK, Scheme::BPSK, Scheme::QPSK };

	[[nodiscard]] constexpr std::string_view name(Scheme scheme) {
		switch (scheme) {
		case Scheme::OOK:  return "OOK (on-off)";
		case Scheme::BPSK: return "BPSK";
		case Scheme::QPSK: return "QPSK";
		}
		std::unreachable();
	}

	[[nodiscard]] constexpr std::size_t bits_per_symbol(Scheme scheme) { return scheme == Scheme::QPSK ? 2 : 1; }

	// The '0' and '1' characters of `text`; anything else (spaces, commas) is ignored.
	[[nodiscard]] inline std::vector<bool> parse_bits(std::string_view text) {
		std::vector<bool> bits;
		for (const char c : text)
			if (c == '0' || c == '1') bits.push_back(c == '1');
		return bits;
	}

	// The ideal symbol for some bits (b1 is only used by QPSK):
	//   OOK:  0 -> 0,  1 -> 1
	//   BPSK: 0 -> +1, 1 -> -1
	//   QPSK: b0 picks the sign of I, b1 the sign of Q (0 -> +, 1 -> -), scaled so |symbol| = 1
	[[nodiscard]] inline std::complex<double> symbol(Scheme scheme, bool b0, bool b1 = false) {
		switch (scheme) {
		case Scheme::OOK:  return b0 ? 1.0 : 0.0;
		case Scheme::BPSK: return b0 ? -1.0 : 1.0;
		case Scheme::QPSK: return std::complex<double>{ b0 ? -1.0 : 1.0, b1 ? -1.0 : 1.0 } / std::numbers::sqrt2;
		}
		std::unreachable();
	}

	// The signal at fractional frame position `frame`, linearly interpolated (0 outside the signal).
	[[nodiscard]] inline std::complex<float> value_at(const Signal& signal, double frame) {
		const double floor = std::floor(frame);
		if (floor < 0.0 || floor + 1.0 >= static_cast<double>(signal.frames())) return {};
		const auto i = static_cast<std::size_t>(floor);
		const auto fraction = static_cast<float>(frame - floor);
		return signal.complex_at(i) * (1.0f - fraction) + signal.complex_at(i + 1) * fraction;
	}

	// Symbol k is read at time (k + offset) / symbol_rate; `offset` is a fraction of a symbol.
	struct Timing {
		double symbol_rate = 100.0; // symbols per second
		double offset = 0.5;        // 0.5 = the middle of each symbol
	};

	// The signal at every symbol instant inside [begin, end) seconds (at most `max_symbols` of them).
	[[nodiscard]] inline std::vector<std::complex<float>> sample_symbols(const Signal& signal, const Timing& timing,
		double begin, double end, std::size_t max_symbols = 4096) {
		std::vector<std::complex<float>> out;
		for (double k = std::ceil(begin * timing.symbol_rate - timing.offset); out.size() < max_symbols; k += 1.0) {
			const double t = (k + timing.offset) / timing.symbol_rate;
			if (t >= end) break;
			out.push_back(value_at(signal, t * signal.sample_rate));
		}
		return out;
	}

	// The bits that the received symbols most likely carried, as '0' / '1' characters.
	// OOK decides on magnitude: anything above half of the strongest symbol is a 1.
	[[nodiscard]] inline std::string decode(Scheme scheme, std::span<const std::complex<float>> symbols) {
		float strongest = 0.0f;
		for (const auto& z : symbols) strongest = std::max(strongest, std::abs(z));

		std::string bits;
		for (const auto& z : symbols) {
			switch (scheme) {
			case Scheme::OOK:  bits += std::abs(z) > 0.5f * strongest ? '1' : '0'; break;
			case Scheme::BPSK: bits += z.real() < 0.0f ? '1' : '0'; break;
			case Scheme::QPSK:
				bits += z.real() < 0.0f ? '1' : '0';
				bits += z.imag() < 0.0f ? '1' : '0';
				break;
			}
		}
		return bits;
	}

	// Eye diagram: the signal from one symbol before to one symbol after each symbol instant in
	// [begin, end) seconds, laid on top of each other. A wide-open "eye" in the middle means the
	// symbols are easy to tell apart.
	// Returns `points` values per trace (channel 0 = real / I, 1 = Q), traces one after the other.
	[[nodiscard]] inline std::vector<float> eye_traces(const Signal& signal, std::size_t channel, const Timing& timing,
		double begin, double end, std::size_t max_traces, std::size_t points) {
		std::vector<float> out;
		const double samples_per_symbol = signal.sample_rate / timing.symbol_rate;
		std::size_t traces = 0;

		for (double k = std::ceil(begin * timing.symbol_rate - timing.offset); traces < max_traces; k += 1.0, ++traces) {
			const double centre = (k + timing.offset) * samples_per_symbol; // in frames
			if (centre / signal.sample_rate >= end) break;
			for (std::size_t p = 0; p < points; ++p) {
				const double x = -1.0 + 2.0 * static_cast<double>(p) / static_cast<double>(points - 1); // -1 .. 1 symbols
				const auto z = value_at(signal, centre + x * samples_per_symbol);
				out.push_back(channel == 0 ? z.real() : z.imag());
			}
		}
		return out;
	}

} // namespace sig::digital
