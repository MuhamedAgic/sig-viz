#pragma once

// Digital modulation: bits <-> symbols, plus the data behind the constellation and eye diagram.
//
// Bits travel as symbols: short stretches of signal with a fixed level, phase or frequency, one after
// the other at a fixed rate, like flashes of a signal lamp. The receiver looks at the signal once per
// symbol and decides which symbol it most resembles.
//
// Decoding expects a baseband signal (carrier at 0 Hz) and a known symbol rate. A signal on a
// carrier can be brought down with transform::shift_frequency first. Real recordings are never exactly
// at 0 Hz and never sampled exactly mid-symbol; recover() follows both drifts, like a real receiver.

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <complex>
#include <cstddef>
#include <numbers>
#include <numeric>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fft.hpp"
#include "signal.hpp"
#include "transforms.hpp"

namespace sig::digital {

	// How bits are put on the signal:
	//   OOK:  on-off keying, the carrier is on for a 1 and off for a 0 (like Morse code with a lamp)
	//   BPSK: the carrier's phase flips: 0 -> +1, 1 -> -1 (the dot on the clock face jumps half a turn)
	//   QPSK: four phases, so every symbol carries 2 bits
	//   FSK:  the frequency changes: a 1 is a slightly higher tone, a 0 a slightly lower one (like a modem's whistle)
	enum class Scheme { OOK, BPSK, QPSK, FSK };
	inline constexpr std::array all_schemes{ Scheme::OOK, Scheme::BPSK, Scheme::QPSK, Scheme::FSK };

	[[nodiscard]] constexpr std::string_view name(Scheme scheme) {
		switch (scheme) {
		case Scheme::OOK:  return "OOK (on-off)";
		case Scheme::BPSK: return "BPSK";
		case Scheme::QPSK: return "QPSK";
		case Scheme::FSK:  return "2-FSK";
		}
		std::unreachable();
	}

	[[nodiscard]] constexpr std::size_t bits_per_symbol(Scheme scheme) { return scheme == Scheme::QPSK ? 2 : 1; }

	// Line coding: how data bits become the bits that are sent ("chips").
	//   NRZ:        as they are. Long runs of equal bits give the receiver no edges to keep time by.
	//   Manchester: every bit becomes a pair, 1 -> 10 and 0 -> 01, so there is an edge in every bit.
	//               Twice the symbols for the same data, but the timing can always be recovered.
	enum class LineCode { NRZ, Manchester };
	inline constexpr std::array all_line_codes{ LineCode::NRZ, LineCode::Manchester };

	[[nodiscard]] constexpr std::string_view name(LineCode code) {
		switch (code) {
		case LineCode::NRZ:        return "NRZ (plain)";
		case LineCode::Manchester: return "Manchester";
		}
		std::unreachable();
	}

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
	//   FSK:  0 -> -1, 1 -> +1: the direction the frequency moves (there is no fixed point in the I/Q plane)
	[[nodiscard]] inline std::complex<double> symbol(Scheme scheme, bool b0, bool b1 = false) {
		switch (scheme) {
		case Scheme::OOK:  return b0 ? 1.0 : 0.0;
		case Scheme::BPSK: return b0 ? -1.0 : 1.0;
		case Scheme::QPSK: return std::complex<double>{ b0 ? -1.0 : 1.0, b1 ? -1.0 : 1.0 } / std::numbers::sqrt2;
		case Scheme::FSK:  return b0 ? 1.0 : -1.0;
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

	// How fast the signal turns, in Hz, sample by sample: what FSK changes. Real signals are made I/Q
	// first. Smoothed over `smooth` samples, because the raw value is noisy.
	// (In the clock-face picture: how far the dot turned since the previous sample, times the sample rate.)
	[[nodiscard]] inline Signal instantaneous_frequency(const Signal& signal, std::size_t smooth = 1) {
		const auto z = transform::detail::as_iq(signal);
		std::vector<float> hz(z.size());
		const double scale = signal.sample_rate / tau;
		for (std::size_t n = 1; n < z.size(); ++n) hz[n] = static_cast<float>(std::arg(z[n] * std::conj(z[n - 1])) * scale);
		if (hz.size() > 1) hz[0] = hz[1];

		if (smooth > 1) { // moving average, centred
			std::vector<double> sum(hz.size() + 1, 0.0);
			for (std::size_t n = 0; n < hz.size(); ++n) sum[n + 1] = sum[n] + hz[n];
			const std::size_t half = smooth / 2;
			std::vector<float> out(hz.size());
			for (std::size_t n = 0; n < hz.size(); ++n) {
				const std::size_t lo = n > half ? n - half : 0, hi = std::min(hz.size(), n + half + 1);
				out[n] = static_cast<float>((sum[hi] - sum[lo]) / static_cast<double>(hi - lo));
			}
			hz = std::move(out);
		}
		Signal out = transform::detail::from_real(signal, std::move(hz));
		out.center_hz = 0.0;
		return out;
	}

	// The bits that the received symbols most likely carried, as '0' / '1' characters.
	// OOK decides on magnitude: anything above half of the strongest symbol is a 1.
	// FSK symbols are frequencies (from instantaneous_frequency): above 0 is a 1.
	//
	// Like scoring darts: every received symbol landed somewhere on the board (the constellation),
	// and counts for the nearest bullseye. For BPSK / QPSK that is simply: which side of the axis is it on?
	[[nodiscard]] inline std::string decode(Scheme scheme, std::span<const std::complex<float>> symbols) {
		float strongest = 0.0f;
		for (const auto& z : symbols) strongest = std::max(strongest, std::abs(z));

		std::string bits;
		for (const auto& z : symbols) {
			switch (scheme) {
			case Scheme::OOK:  bits += std::abs(z) > 0.5f * strongest ? '1' : '0'; break;
			case Scheme::BPSK: bits += z.real() < 0.0f ? '1' : '0'; break;
			case Scheme::FSK:  bits += z.real() > 0.0f ? '1' : '0'; break;
			case Scheme::QPSK:
				bits += z.real() < 0.0f ? '1' : '0';
				bits += z.imag() < 0.0f ? '1' : '0';
				break;
			}
		}
		return bits;
	}

	// What decode() needs to look at: the signal itself, or for FSK its frequency over time.
	[[nodiscard]] inline Signal decision_signal(const Signal& signal, Scheme scheme, double symbol_rate) {
		if (scheme != Scheme::FSK) return signal;
		// Average over about half a symbol: enough to calm the noise, short enough to keep the edges.
		const auto smooth = static_cast<std::size_t>(std::max(1.0, 0.5 * signal.sample_rate / symbol_rate));
		return instantaneous_frequency(signal, smooth);
	}

	// Samples and decodes [begin, end) seconds in one go.
	[[nodiscard]] inline std::string decode_signal(const Signal& signal, Scheme scheme, const Timing& timing, double begin, double end) {
		const Signal decided = decision_signal(signal, scheme, timing.symbol_rate);
		return decode(scheme, sample_symbols(decided, timing, begin, end));
	}

	// Eye diagram: the signal from one symbol before to one symbol after each symbol instant in
	// [begin, end) seconds, laid on top of each other. A wide-open "eye" in the middle means the
	// symbols are easy to tell apart.
	// Like a long-exposure photo of all symbols at once: noise blurs the lines and closes the eye.
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

	// ---- line coding, sync words, bytes -------------------------------------------------------------------

	[[nodiscard]] inline std::string manchester_encode(std::string_view bits) {
		std::string chips;
		for (const char b : bits) {
			if (b == '1') chips += "10";
			else if (b == '0') chips += "01";
		}
		return chips;
	}

	struct Decoded {
		std::string bits;   // '?' where a pair was 00 or 11 (not valid Manchester)
		std::size_t errors = 0;
	};

	// Pairs chips back into bits. Where the pairs start is not known, so both ways are tried and the one
	// with fewer invalid pairs wins.
	[[nodiscard]] inline Decoded manchester_decode(std::string_view chips) {
		Decoded best;
		best.errors = static_cast<std::size_t>(-1);
		for (std::size_t start = 0; start < 2; ++start) {
			Decoded d;
			for (std::size_t i = start; i + 1 < chips.size(); i += 2) {
				const std::string_view pair = chips.substr(i, 2);
				if (pair == "10") d.bits += '1';
				else if (pair == "01") d.bits += '0';
				else {
					d.bits += '?';
					++d.errors;
				}
			}
			if (d.errors < best.errors) best = std::move(d);
		}
		return best;
	}

	struct SyncMatch {
		std::size_t position; // index of the sync word's first bit
		bool        inverted; // found with all bits flipped (BPSK can lock upside down: 180 degrees off)
		std::size_t errors;   // bits that differ
	};

	// Where `sync` occurs in `bits`, allowing `max_errors` wrong bits. A sync word (or preamble) is a fixed
	// pattern a transmitter sends before every packet, so the receiver knows where the data starts.
	[[nodiscard]] inline std::vector<SyncMatch> find_sync(std::string_view bits, std::string_view sync, std::size_t max_errors = 0) {
		std::vector<SyncMatch> found;
		if (sync.empty() || sync.size() > bits.size()) return found;
		for (std::size_t i = 0; i + sync.size() <= bits.size(); ++i) {
			std::size_t same = 0, flipped = 0;
			for (std::size_t k = 0; k < sync.size(); ++k) {
				const char b = bits[i + k];
				if (b == sync[k]) ++flipped;
				else if (b == '0' || b == '1') ++same;
			}
			// `same` counts differences for the straight match, `flipped` for the inverted one.
			if (same <= max_errors) found.push_back({ i, false, same });
			else if (flipped <= max_errors) found.push_back({ i, true, flipped });
		}
		return found;
	}

	[[nodiscard]] inline std::string invert(std::string_view bits) {
		std::string out{ bits };
		for (char& b : out) b = b == '1' ? '0' : b == '0' ? '1' : b;
		return out;
	}

	// Groups of 8 bits as bytes (most significant bit first); a trailing partial byte is dropped.
	[[nodiscard]] inline std::vector<unsigned char> to_bytes(std::string_view bits) {
		std::vector<unsigned char> bytes;
		for (std::size_t i = 0; i + 8 <= bits.size(); i += 8) {
			unsigned value = 0;
			for (std::size_t k = 0; k < 8; ++k) value = value << 1 | (bits[i + k] == '1' ? 1u : 0u);
			bytes.push_back(static_cast<unsigned char>(value));
		}
		return bytes;
	}

	[[nodiscard]] inline std::string to_hex(std::string_view bits) {
		static constexpr char digits[] = "0123456789ABCDEF";
		std::string out;
		for (const unsigned char byte : to_bytes(bits)) {
			if (!out.empty()) out += ' ';
			out += digits[byte >> 4];
			out += digits[byte & 15];
		}
		return out;
	}

	// Printable ASCII; anything else shows as '.'.
	[[nodiscard]] inline std::string to_text(std::string_view bits) {
		std::string out;
		for (const unsigned char byte : to_bytes(bits)) out += (byte >= 32 && byte < 127) ? static_cast<char>(byte) : '.';
		return out;
	}

	// ---- receiver: carrier and timing recovery ----------------------------------------------------------

	struct Recovery {
		std::vector<std::complex<float>> symbols;      // one per symbol, carrier removed, mid-symbol
		double                           frequency_offset_hz = 0.0; // how far off 0 Hz the carrier was
		double                           timing_drift = 0.0;        // samples the clock moved, start to end
	};

	namespace detail {

		// The frequency of the strongest tone in z (an FFT peak), in Hz, refined between bins.
		[[nodiscard]] inline double strongest_tone(std::span<const std::complex<float>> z, double sample_rate) {
			const std::size_t size = std::min<std::size_t>(std::bit_ceil(std::max<std::size_t>(z.size(), 2)), std::size_t{ 1 } << 20);
			std::vector<fft::Complex> buffer(size);
			std::copy_n(z.begin(), std::min(z.size(), size), buffer.begin());
			fft::forward(buffer);
			std::size_t top = 0;
			for (std::size_t k = 1; k < size; ++k)
				if (std::norm(buffer[k]) > std::norm(buffer[top])) top = k;
			const auto power = [&](std::size_t k) { return std::log(std::norm(buffer[k % size]) + 1e-30); };
			const double a = power(top + size - 1), b = power(top), c = power(top + 1);
			const double shift = (a - 2.0 * b + c) < 0.0 ? 0.5 * (a - c) / (a - 2.0 * b + c) : 0.0;
			double bin = static_cast<double>(top) + shift;
			if (bin > static_cast<double>(size) / 2.0) bin -= static_cast<double>(size); // upper half = negative frequencies
			return bin * sample_rate / static_cast<double>(size);
		}

	} // namespace detail

	// Decodes like a real receiver, for signals that are not exactly at 0 Hz or not sampled exactly
	// mid-symbol (every real recording):
	//   1. Coarse frequency: for BPSK, squaring the signal removes the data (both +1 and -1 square to +1)
	//      and leaves a pure tone at twice the offset; for QPSK the 4th power does the same. An FFT finds it.
	//   2. Carrier tracking (a Costas loop): after every symbol, look how far it is turned from the
	//      nearest ideal point and turn the next ones back by a bit of that. Keeps the constellation still.
	//   3. Timing (the Gardner method): look at the sample halfway between two symbols. If the symbols
	//      differ, it should be right on the edge between them (0); if it leans towards the later one, we
	//      sample too late, so sample a bit earlier next time. Like clapping along and adjusting to the beat.
	// OOK and FSK need no carrier tracking; they only get the timing loop (on magnitude / frequency).
	[[nodiscard]] inline Recovery recover(const Signal& signal, Scheme scheme, double symbol_rate, double begin, double end) {
		Recovery out;
		const auto first = static_cast<std::size_t>(std::clamp(begin * signal.sample_rate, 0.0, static_cast<double>(signal.frames())));
		const auto last = static_cast<std::size_t>(std::clamp(end * signal.sample_rate, 0.0, static_cast<double>(signal.frames())));
		const double sps = signal.sample_rate / symbol_rate; // samples per symbol
		if (last <= first || sps < 2.0) return out;

		// The samples the loops work on.
		std::vector<std::complex<float>> z(last - first);
		const bool psk = scheme == Scheme::BPSK || scheme == Scheme::QPSK;
		if (scheme == Scheme::FSK) {
			const Signal f = decision_signal(signal, scheme, symbol_rate);
			for (std::size_t n = 0; n < z.size(); ++n) z[n] = f.complex_at(first + n);
		}
		else {
			for (std::size_t n = 0; n < z.size(); ++n) z[n] = signal.complex_at(first + n);
		}
		float ook_mean = 0.0f;
		if (scheme == Scheme::OOK) {
			for (auto& v : z) v = std::abs(v); // on/off is all that matters
			ook_mean = std::accumulate(z.begin(), z.end(), std::complex<float>{}).real() / static_cast<float>(z.size());
			for (auto& v : z) v -= ook_mean;   // centred, so edges cross 0 like the other schemes
		}

		// 1. Coarse frequency offset.
		if (psk) {
			const int power = scheme == Scheme::BPSK ? 2 : 4;
			std::vector<std::complex<float>> raised(z.size());
			for (std::size_t n = 0; n < z.size(); ++n) raised[n] = scheme == Scheme::BPSK ? z[n] * z[n] : (z[n] * z[n]) * (z[n] * z[n]);
			out.frequency_offset_hz = detail::strongest_tone(raised, signal.sample_rate) / power;
			for (std::size_t n = 0; n < z.size(); ++n)
				z[n] *= std::polar(1.0f, static_cast<float>(-tau * out.frequency_offset_hz * static_cast<double>(n) / signal.sample_rate));
		}

		// Automatic gain: loops behave the same whatever the level.
		double power_sum = 0.0;
		for (const auto& v : z) power_sum += std::norm(v);
		const auto gain = static_cast<float>(1.0 / std::sqrt(std::max(power_sum / static_cast<double>(z.size()), 1e-20)));
		for (auto& v : z) v *= gain;

		const auto at = [&](double t) { // linear interpolation
			const double floor = std::floor(t);
			const auto i = static_cast<std::size_t>(floor);
			const auto f = static_cast<float>(t - floor);
			return z[i] * (1.0f - f) + z[i + 1] * f;
		};
		const auto ideal = [&](std::complex<float> v) -> std::complex<float> {
			if (scheme == Scheme::QPSK) return std::complex<float>{ v.real() < 0 ? -1.0f : 1.0f, v.imag() < 0 ? -1.0f : 1.0f } / std::numbers::sqrt2_v<float>;
			return v.real() < 0 ? -1.0f : 1.0f;
		};

		// 2 + 3. Carrier and timing loops, symbol by symbol.
		constexpr float carrier_gain = 0.1f;                                    // how hard the phase is corrected
		constexpr float frequency_gain = carrier_gain * carrier_gain / 4.0f;    // ... and the frequency
		constexpr double timing_gain = 0.08;
		float phase = 0.0f, frequency = 0.0f; // radians, radians per symbol
		std::complex<float> previous{};
		double t = sps / 2.0, drift = 0.0;
		for (bool have_previous = false; t + 1.0 < static_cast<double>(z.size()); t += sps) {
			const auto rotate = std::polar(1.0f, -phase);
			const auto current = at(t) * rotate;
			if (have_previous && t - sps / 2.0 >= 0.0) {
				const auto middle = at(t - sps / 2.0) * std::polar(1.0f, -(phase - frequency / 2.0f));
				const double error = std::real(std::conj(middle) * (current - previous));
				const double adjust = std::clamp(timing_gain * error, -0.1, 0.1) * sps;
				t -= adjust;
				drift -= adjust;
			}
			if (psk) {
				const float error = std::arg(current * std::conj(ideal(current)));
				frequency += frequency_gain * error;
				phase += frequency + carrier_gain * error;
			}
			out.symbols.push_back(scheme == Scheme::OOK ? current + ook_mean * gain : current); // OOK: back to on / off levels
			previous = current;
			have_previous = true;
		}
		out.frequency_offset_hz += frequency * symbol_rate / tau;
		out.timing_drift = drift;
		return out;
	}

} // namespace sig::digital
