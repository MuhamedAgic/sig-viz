#pragma once

// Channel coding: forward error correction with polar codes, interleaving, and ways to damage bits on
// purpose to see what the code can repair. The encoders, decoders and interleavers come from the AFF3CT
// library (src/core/coding.cpp is the only file that includes it: its headers are big).
//
// The chain, like a parcel service:
//   message bits -> encode (add clever redundancy: the padding in the box)
//                -> interleave (shuffle the order, so damage in one spot is spread over many boxes)
//                -> send ................ damage happens here ................
//                -> deinterleave -> decode (repair what the redundancy allows) -> message bits
//
// Soft bits: what a receiver really knows about each bit is not just 0 or 1 but how sure it is. A soft
// bit is a number: positive = probably 0, negative = probably 1, and the further from 0, the more sure
// (AFF3CT's convention, the same as BPSK: a 0 is sent as +1, a 1 as -1). 0 itself means "no idea"
// (an erased bit). Decoders that use soft bits repair much more than ones that only see 0s and 1s.

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sig::coding {

	using Bits = std::vector<std::uint8_t>; // one bit per element, 0 or 1

	// ---- polar code -------------------------------------------------------------------------------------

	// How the polar decoder works its way through the bits:
	//   SC:     successive cancellation: decides bit after bit, never looks back. Fast, weakest.
	//   SCL:    keeps a list of the L most likely guesses so far and picks the best at the end.
	//   CA-SCL: the same, but picks the guess whose CRC checks out: much stronger. Needs a CRC.
	enum class Decoder { SC, SCL, CA_SCL };
	inline constexpr std::array all_decoders{ Decoder::SC, Decoder::SCL, Decoder::CA_SCL };

	[[nodiscard]] constexpr std::string_view name(Decoder decoder) {
		switch (decoder) {
		case Decoder::SC:     return "SC";
		case Decoder::SCL:    return "SCL (list)";
		case Decoder::CA_SCL: return "CA-SCL (list + CRC)";
		}
		std::unreachable();
	}

	// A checksum added to every codeword's message bits: tells the receiver whether decoding worked.
	enum class Crc { None, Crc8, Crc16 };
	inline constexpr std::array all_crcs{ Crc::None, Crc::Crc8, Crc::Crc16 };

	[[nodiscard]] constexpr std::string_view name(Crc crc) {
		switch (crc) {
		case Crc::None:  return "none";
		case Crc::Crc8:  return "8 bits";
		case Crc::Crc16: return "16 bits";
		}
		std::unreachable();
	}

	[[nodiscard]] constexpr int crc_bits(Crc crc) {
		return crc == Crc::Crc8 ? 8 : crc == Crc::Crc16 ? 16 : 0;
	}

	struct CodeSpec {
		bool    enabled = true; // false: send the message bits as they are (to compare)
		int     n_log2 = 8;     // codeword length N = 2^n_log2 (polar codes come in powers of two)
		int     k = 128;        // bits of each codeword that carry information (CRC included); the rest is redundancy
		Crc     crc = Crc::Crc8;
		Decoder decoder = Decoder::CA_SCL;
		int     list_size = 8;    // L, for the list decoders
		float   design_db = 2.0f; // the Eb/N0 the code is tuned for (picks which bit positions are frozen)

		[[nodiscard]] int n() const {
			return 1 << n_log2;
		}
		[[nodiscard]] int message_bits() const {
			return k - crc_bits(crc);
		} // per codeword
		[[nodiscard]] double rate() const {
			return enabled ? static_cast<double>(message_bits()) / n() : 1.0;
		}

		bool operator==(const CodeSpec&) const = default;
	};

	// A message of this many bits, in codewords (or 0 and why it does not work).
	[[nodiscard]] std::expected<std::size_t, std::string> codewords_for(const CodeSpec& code, std::size_t message_bits);

	// ---- interleaver ------------------------------------------------------------------------------------

	//   None:   bits go out in order.
	//   Block:  bits are written into a table row by row and read out column by column. A burst of
	//           damaged bits on the air lands in different rows: with rows = codewords, one bit per codeword.
	//   Random: a fixed shuffle (the receiver knows the seed, so it can undo it).
	enum class Interleaver { None, Block, Random };
	inline constexpr std::array all_interleavers{ Interleaver::None, Interleaver::Block, Interleaver::Random };

	[[nodiscard]] constexpr std::string_view name(Interleaver interleaver) {
		switch (interleaver) {
		case Interleaver::None:   return "None";
		case Interleaver::Block:  return "Block (rows -> columns)";
		case Interleaver::Random: return "Random";
		}
		std::unreachable();
	}

	struct InterleaverSpec {
		Interleaver kind = Interleaver::Block;
		int         rows = 0; // Block: 0 = one row per codeword (or 16 for a single codeword)
		unsigned    seed = 1; // Random

		bool operator==(const InterleaverSpec&) const = default;
	};

	// ---- sending and receiving ----------------------------------------------------------------------------

	struct Transmission {
		CodeSpec        code;
		InterleaverSpec interleaver;
		Bits            message;        // as given
		Bits            codewords;      // after encoding, before interleaving: codeword after codeword
		Bits            sent;           // after interleaving: what goes on the air
		std::size_t     frames = 0;     // codewords
		std::size_t     frame_size = 0; // N (or the message length, without coding)
		int             rows = 1;       // the block interleaver's table: rows x (sent.size() / rows)
	};

	[[nodiscard]] std::expected<Transmission, std::string> transmit(
		Bits                   message,
		const CodeSpec&        code,
		const InterleaverSpec& interleaver
	);

	// The soft bits a perfect channel would deliver: +1 for every 0, -1 for every 1.
	[[nodiscard]] std::vector<float> perfect_soft(std::span<const std::uint8_t> bits);

	struct Reception {
		std::vector<float> soft;               // as received (air order)
		std::vector<float> deinterleaved;      // codeword order
		Bits               received;           // hard decisions before decoding, codeword order
		Bits               decoded;            // the message bits, after decoding
		std::vector<bool>  frame_wrong;        // per codeword: decoded message bits differ from what was sent
		std::vector<bool>  crc_failed;         // per codeword: the CRC says decoding failed (empty without CRC)
		std::size_t        channel_errors = 0; // bits wrong before decoding
		std::size_t        erased = 0;         // soft bits that were exactly 0
		std::size_t        decoded_errors = 0; // message bits wrong after decoding
		std::size_t        frame_errors = 0;
	};

	// Decodes `soft` (air order, as many as tx.sent; missing ones count as erased).
	[[nodiscard]] std::expected<Reception, std::string> receive(const Transmission& tx, std::vector<float> soft);

	// ---- damage --------------------------------------------------------------------------------------------

	// Which bits are hit (in air order).
	//   All:      every bit.
	//   Random:   each bit with a probability.
	//   EveryNth: one bit every n bits.
	//   Burst:    a run of consecutive bits (like a lightning strike or a car driving past).
	enum class Pattern { All, Random, EveryNth, Burst };
	inline constexpr std::array all_patterns{ Pattern::All, Pattern::Random, Pattern::EveryNth, Pattern::Burst };

	[[nodiscard]] constexpr std::string_view name(Pattern pattern) {
		switch (pattern) {
		case Pattern::All:      return "every bit";
		case Pattern::Random:   return "random bits";
		case Pattern::EveryNth: return "one bit every n";
		case Pattern::Burst:    return "a burst";
		}
		std::unreachable();
	}

	// What happens to a hit bit.
	//   Flip:      the soft bit changes sign, keeping its confidence: a confident wrong bit.
	//   FlipHard:  becomes a fully confident wrong bit (+-1), the worst kind of error.
	//   Erase:     becomes 0: the receiver knows it does not know (much easier to repair than an error).
	//   Noise:     random (Gaussian) noise is added, like a radio channel; may or may not cross 0.
	enum class Effect { Flip, FlipHard, Erase, Noise };
	inline constexpr std::array all_effects{ Effect::Flip, Effect::FlipHard, Effect::Erase, Effect::Noise };

	[[nodiscard]] constexpr std::string_view name(Effect effect) {
		switch (effect) {
		case Effect::Flip:     return "flip (soft)";
		case Effect::FlipHard: return "flip (hard)";
		case Effect::Erase:    return "erase";
		case Effect::Noise:    return "add noise";
		}
		std::unreachable();
	}

	struct Damage {
		Pattern  pattern = Pattern::Random;
		Effect   effect = Effect::Flip;
		float    probability = 0.02f; // Random
		int      every = 10;          // EveryNth
		int      offset = 0;          // EveryNth: the first one hit
		int      burst_start = 100;   // Burst
		int      burst_length = 32;
		float    sigma = 0.5f; // Noise: standard deviation (the soft bits are +-1)
		unsigned seed = 1;

		bool operator==(const Damage&) const = default;
	};

	[[nodiscard]] std::string describe(const Damage& damage);

	// The steps one after the other.
	[[nodiscard]] std::vector<float> apply(std::vector<float> soft, std::span<const Damage> steps);

	// ---- performance curve -----------------------------------------------------------------------------------

	// Bit error rate over a noisy radio channel (BPSK with Gaussian noise), at increasing signal-to-noise
	// ratios: the classic plot every code is judged by. Eb/N0 is the SNR per message bit, so a code that
	// spends energy on redundancy is compared fairly with sending the bits as they are.
	struct CurvePoint {
		double eb_n0_db = 0.0;
		double ber = 0.0;     // message bits wrong / sent
		double fer = 0.0;     // codewords wrong / sent
		double uncoded = 0.0; // BPSK without any code, in theory
	};

	// Simulates `frames` codewords per point (fewer once enough errors are seen). Empty if stopped.
	[[nodiscard]] std::vector<CurvePoint> ber_curve(
		const CodeSpec& code,
		double          from_db,
		double          to_db,
		double          step_db,
		std::size_t     frames,
		std::stop_token stop
	);

	// ---- text <-> bits -----------------------------------------------------------------------------------------

	[[nodiscard]] Bits        text_to_bits(std::string_view text); // 8 bits per character, most significant first
	[[nodiscard]] std::string bits_to_text(std::span<const std::uint8_t> bits); // unprintable -> '.'
	[[nodiscard]] Bits        random_bits(std::size_t count, unsigned seed);
	[[nodiscard]] std::string to_string(std::span<const std::uint8_t> bits); // "0110..."

} // namespace sig::coding
