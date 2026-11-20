#pragma once

// Bits -> a signal you can see and hear -> soft bits again: the radio part of the error-correction chain.
// The transmitter is the generator's digital mode; the receiver brings the carrier down to 0 Hz, filters
// away what is left of it, and reads every symbol (optionally following carrier and timing drift, like
// the Digital window does for real recordings).

#include <algorithm>
#include <cmath>
#include <expected>
#include <format>
#include <span>
#include <string>
#include <vector>

#include "digital.hpp"
#include "generator.hpp"
#include "signal.hpp"
#include "transforms.hpp"

namespace sig::link {

	struct Settings {
		digital::Scheme scheme = digital::Scheme::BPSK;
		float           symbol_rate = 500.0f; // baud
		float           carrier_hz = 2000.0f; // 0 = baseband
		float           sample_rate = 48'000.0f;
		bool            follow = false; // carrier and timing recovery (for signals that drifted)

		bool operator==(const Settings&) const = default;
	};

	[[nodiscard]] inline std::string describe(const Settings& s) {
		std::string text = std::format("{} {:g} baud", digital::name(s.scheme), s.symbol_rate);
		if (s.carrier_hz != 0.0f) {
			text += " on " + format_hz(s.carrier_hz);
		}
		return text;
	}

	// The bits (0 / 1 per element) as a signal, each sent once.
	[[nodiscard]] inline std::expected<Signal, std::string> modulate(
		std::span<const std::uint8_t> bits,
		const Settings&               s
	) {
		if (bits.empty()) {
			return std::unexpected("Nothing to send");
		}
		if (!(s.symbol_rate > 0.0f) || s.sample_rate < 2.0f * s.symbol_rate) {
			return std::unexpected("Needs at least 2 samples per symbol");
		}
		generator::Settings g;
		g.source = generator::Source::Digital;
		g.scheme = s.scheme;
		g.iq = s.scheme == digital::Scheme::QPSK || (s.carrier_hz == 0.0f && s.scheme == digital::Scheme::FSK);
		g.symbol_rate = s.symbol_rate;
		g.frequency = s.carrier_hz;
		g.fsk_deviation = s.symbol_rate; // tones 2 x symbol rate apart: easy to tell apart
		g.sample_rate = s.sample_rate;
		g.amplitude = 0.8f;
		g.bits.resize(bits.size());
		for (std::size_t i = 0; i < bits.size(); ++i) {
			g.bits[i] = bits[i] ? '1' : '0';
		}
		const double symbols =
			std::ceil(static_cast<double>(bits.size()) / static_cast<double>(digital::bits_per_symbol(s.scheme)));
		g.seconds = static_cast<float>(
			(symbols + 0.5) / s.symbol_rate
		); // half a symbol extra: the last one is read in its middle
		return generator::synthesize(g);
	}

	struct Received {
		std::vector<float> soft;             // one per bit, + = 0
		bool               inverted = false; // the receiver locked upside down (flipped back)
	};

	// Reads `count` soft bits from `signal`. `expected` (the bits that were sent) is only used to notice a
	// receiver that locked 180 degrees off: real systems use a known sync word for that.
	[[nodiscard]] inline std::expected<Received, std::string> demodulate(
		const Signal&                 signal,
		const Settings&               s,
		std::size_t                   count,
		std::span<const std::uint8_t> expected = {}
	) {
		if (signal.frames() < 2) {
			return std::unexpected("The signal is empty");
		}
		if (!(s.symbol_rate > 0.0f) || signal.sample_rate < 2.0f * s.symbol_rate) {
			return std::unexpected("The signal has fewer than 2 samples per symbol");
		}

		Signal baseband = signal;
		if (s.carrier_hz != 0.0f) {
			auto shifted = transform::shift_frequency(signal, -s.carrier_hz);
			if (!shifted) {
				return std::unexpected(shifted.error());
			}
			// What is left at -2 x carrier (from a real signal) and everything outside the data's own band goes.
			const float cutoff = std::min(
				1.5f * s.symbol_rate + (s.scheme == digital::Scheme::FSK ? s.symbol_rate : 0.0f),
				0.45f * signal.sample_rate
			);
			auto filtered = transform::filter(
				*shifted,
				{ .type = transform::FilterType::LowPass,
			      .high_hz = cutoff,
			      .taps = transform::taps_for(0.5 * s.symbol_rate, signal.sample_rate) }
			);
			if (!filtered) {
				return std::unexpected(filtered.error());
			}
			baseband = std::move(*filtered);
		}

		const double                     duration = baseband.duration();
		std::vector<std::complex<float>> symbols;
		if (s.follow) {
			symbols = digital::recover(baseband, s.scheme, s.symbol_rate, 0.0, duration).symbols;
		}
		else {
			const Signal decided = digital::decision_signal(baseband, s.scheme, s.symbol_rate);
			symbols = digital::sample_symbols(
				decided,
				{ .symbol_rate = s.symbol_rate, .offset = 0.5 },
				0.0,
				duration,
				count + 16
			);
		}

		Received out{ .soft = digital::soft_decisions(s.scheme, symbols) };
		out.soft.resize(count, 0.0f); // missing at the end: unknown

		if (!expected.empty()) {
			std::size_t       wrong = 0;
			const std::size_t n = std::min(expected.size(), out.soft.size());
			for (std::size_t i = 0; i < n; ++i) {
				wrong += (out.soft[i] < 0.0f) != (expected[i] != 0);
			}
			if (2 * wrong > n) {
				out.inverted = true;
				for (float& v : out.soft) {
					v = -v;
				}
			}
		}
		return out;
	}

} // namespace sig::link
