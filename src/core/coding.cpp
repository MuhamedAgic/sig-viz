// The AFF3CT side of coding.hpp: polar encoders / decoders, CRCs, interleavers and the noisy channel.
// Only this file includes AFF3CT, so its (big) headers are compiled once.

#include "coding.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <format>
#include <memory>
#include <random>

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <Module/CRC/Polynomial/CRC_polynomial.hpp>
#include <Module/Channel/AWGN/Channel_AWGN_LLR.hpp>
#include <Module/Decoder/Decoder_SIHO.hpp>
#include <Module/Decoder/Polar/SC/Decoder_polar_SC_naive_sys.hpp>
#include <Module/Decoder/Polar/SCL/CRC/Decoder_polar_SCL_naive_CA_sys.hpp>
#include <Module/Decoder/Polar/SCL/Decoder_polar_SCL_naive_sys.hpp>
#include <Module/Encoder/Polar/Encoder_polar_sys.hpp>
#include <Module/Interleaver/Interleaver.hpp>
#include <Tools/Code/Polar/Frozenbits_generator/Frozenbits_generator_GA_Arikan.hpp>
#include <Tools/Interleaver/Random/Interleaver_core_random.hpp>
#include <Tools/Interleaver/Row_column/Interleaver_core_row_column.hpp>
#include <Tools/Noise/Sigma.hpp>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace sig::coding {

	namespace {

		using namespace aff3ct;
		using B = int;   // AFF3CT's bit type
		using R = float; // ... and its soft-bit type

		constexpr int         min_n_log2 = 3, max_n_log2 = 12; // N = 8 .. 4096
		constexpr std::size_t max_sent_bits = 1 << 22;         // ~4 million: keeps decoding interactive

		// Noise level (standard deviation, for +-1 symbols) of a channel at `eb_n0_db`, for a code of this rate.
		[[nodiscard]] double sigma_for(double eb_n0_db, double rate) {
			const double es_n0_db = eb_n0_db + 10.0 * std::log10(rate);
			return std::sqrt(1.0 / (2.0 * std::pow(10.0, es_n0_db / 10.0)));
		}

		[[nodiscard]] std::string crc_key(Crc crc) {
			return crc == Crc::Crc16 ? "16-IBM" : "8-DVB-S2";
		}

		// Everything one polar code needs, made from its settings.
		struct Polar {
			std::vector<bool>                             frozen; // per position: true = always 0, carries nothing
			std::unique_ptr<module::Encoder_polar_sys<B>> encoder;
			std::unique_ptr<module::CRC<B>>               crc; // null without CRC
			std::unique_ptr<module::Decoder_SIHO<B, R>>   decoder;

			explicit Polar(const CodeSpec& code) : frozen(static_cast<std::size_t>(code.n())) {
				const int n = code.n(), k = code.k, info = code.message_bits();

				// Which positions to freeze: the ones that come out least reliable through the decoder,
				// at the noise level the code is designed for (Arikan's Gaussian approximation).
				tools::Frozenbits_generator_GA_Arikan generator(k, n);
				const auto design_sigma = static_cast<R>(sigma_for(code.design_db, static_cast<double>(info) / n));
				const tools::Sigma<> noise(
					design_sigma,
					code.design_db,
					code.design_db + 10.0f * std::log10(static_cast<float>(info) / n)
				);
				generator.set_noise(noise);
				generator.generate(frozen);

				encoder = std::make_unique<module::Encoder_polar_sys<B>>(k, n, frozen);
				if (code.crc != Crc::None) {
					crc = std::make_unique<module::CRC_polynomial<B>>(info, crc_key(code.crc), crc_bits(code.crc));
				}

				switch (code.decoder) {
				case Decoder::SC:
					decoder = std::make_unique<module::Decoder_polar_SC_naive_sys<B, R>>(k, n, frozen);
					break;
				case Decoder::SCL:
					decoder = std::make_unique<module::Decoder_polar_SCL_naive_sys<B, R>>(k, n, code.list_size, frozen);
					break;
				case Decoder::CA_SCL:
					decoder = std::make_unique<module::Decoder_polar_SCL_naive_CA_sys<B, R>>(
						k,
						n,
						code.list_size,
						frozen,
						*crc
					);
					break;
				}
			}

			// info: message bits of one codeword -> its N code bits.
			void encode(const std::vector<B>& info, std::vector<B>& codeword) const {
				if (crc) {
					std::vector<B> with_crc(static_cast<std::size_t>(encoder->get_K()));
					crc->build(info, with_crc);
					encoder->encode(with_crc, codeword);
				}
				else {
					encoder->encode(info, codeword);
				}
			}

			struct Result {
				std::vector<B> info;
				bool           crc_ok = true;
			};

			[[nodiscard]] Result decode(const std::vector<R>& soft) const {
				std::vector<B> bits(static_cast<std::size_t>(decoder->get_K()));
				decoder->decode_siho(soft, bits);
				Result out;
				if (crc) {
					out.crc_ok = crc->check(bits);
					out.info.resize(static_cast<std::size_t>(crc->get_K()));
					crc->extract(bits, out.info);
				}
				else {
					out.info = std::move(bits);
				}
				return out;
			}
		};

		[[nodiscard]] std::string check(const CodeSpec& code) {
			if (code.n_log2 < min_n_log2 || code.n_log2 > max_n_log2) {
				return std::format("N must be between {} and {}", 1 << min_n_log2, 1 << max_n_log2);
			}
			if (code.k > code.n()) {
				return std::format("K ({}) can't be more than N ({})", code.k, code.n());
			}
			if (code.message_bits() < 1) {
				return std::format("K must be more than the CRC's {} bits", crc_bits(code.crc));
			}
			if (code.decoder == Decoder::CA_SCL && code.crc == Crc::None) {
				return "CA-SCL needs a CRC";
			}
			if (code.decoder != Decoder::SC && (code.list_size < 1 || code.list_size > 64)) {
				return "The list size must be between 1 and 64";
			}
			return {};
		}

		// The shuffle for `size` bits, or null for none.
		[[nodiscard]] std::unique_ptr<tools::Interleaver_core<>> make_core(
			const InterleaverSpec& spec,
			std::size_t            size,
			int                    rows
		) {
			std::unique_ptr<tools::Interleaver_core<>> core;
			const int                                  n = static_cast<int>(size);
			switch (spec.kind) {
			case Interleaver::None: return nullptr;
			case Interleaver::Block:
				// Written row by row (rows x columns), read column by column.
				core = std::make_unique<tools::Interleaver_core_row_column<>>(
					n,
					n / rows,
					tools::Interleaver_core_row_column<>::READ_ORDER::TOP_LEFT
				);
				break;
			case Interleaver::Random:
				core = std::make_unique<tools::Interleaver_core_random<>>(n, static_cast<int>(spec.seed));
				break;
			}
			return core; // the constructors build the shuffle table
		}

		[[nodiscard]] std::expected<int, std::string> block_rows(
			const InterleaverSpec& spec,
			std::size_t            frames,
			std::size_t            size
		) {
			if (spec.kind != Interleaver::Block) {
				return 1;
			}
			int rows = spec.rows > 0 ? spec.rows : frames > 1 ? static_cast<int>(frames) : 16;
			rows = std::min(rows, static_cast<int>(size));
			if (rows < 1 || size % static_cast<std::size_t>(rows) != 0) {
				return std::unexpected(
					std::format("The block interleaver's {} rows don't divide the {} bits evenly", rows, size)
				);
			}
			return rows;
		}

		template <typename T>
		[[nodiscard]] std::vector<T> shuffle(
			const tools::Interleaver_core<>* core,
			const std::vector<T>&            in,
			bool                             forward
		) {
			if (!core) {
				return in;
			}
			module::Interleaver<T> interleaver(*core);
			std::vector<T>         out(in.size());
			if (forward) {
				interleaver.interleave(in, out);
			}
			else {
				interleaver.deinterleave(in, out);
			}
			return out;
		}

		[[nodiscard]] std::uint8_t hard(float soft) {
			return soft < 0.0f ? 1 : 0;
		}

	} // namespace

	// ---- sending and receiving ------------------------------------------------------------------------------

	std::expected<std::size_t, std::string> codewords_for(const CodeSpec& code, std::size_t message_bits) {
		if (!code.enabled) {
			return 1;
		}
		if (const std::string error = check(code); !error.empty()) {
			return std::unexpected(error);
		}
		const auto        per_codeword = static_cast<std::size_t>(code.message_bits());
		const std::size_t frames = std::max<std::size_t>(1, (message_bits + per_codeword - 1) / per_codeword);
		if (frames * static_cast<std::size_t>(code.n()) > max_sent_bits) {
			return std::unexpected(std::format("That is {} codewords: too many, shorten the message", frames));
		}
		return frames;
	}

	std::expected<Transmission, std::string> transmit(
		Bits                   message,
		const CodeSpec&        code,
		const InterleaverSpec& interleaver
	) {
		if (message.empty()) {
			return std::unexpected("The message is empty");
		}
		if (!code.enabled && message.size() > max_sent_bits) {
			return std::unexpected("The message is too long");
		}
		const auto frames = codewords_for(code, message.size());
		if (!frames) {
			return std::unexpected(frames.error());
		}

		try {
			Transmission tx;
			tx.code = code;
			tx.interleaver = interleaver;
			tx.message = std::move(message);
			tx.frames = *frames;
			if (!code.enabled) {
				tx.frame_size = tx.message.size();
				tx.codewords = tx.message;
			}
			else {
				const Polar polar(code);
				const auto  n = static_cast<std::size_t>(code.n()),
							per_codeword = static_cast<std::size_t>(code.message_bits());
				tx.frame_size = n;
				tx.codewords.reserve(tx.frames * n);
				std::vector<B> info(per_codeword), codeword(n);
				for (std::size_t f = 0; f < tx.frames; ++f) {
					for (std::size_t i = 0; i < per_codeword; ++i) {
						const std::size_t at = f * per_codeword + i;
						info[i] = at < tx.message.size() ? tx.message[at] : 0; // the last codeword is padded with 0s
					}
					polar.encode(info, codeword);
					for (const B b : codeword) {
						tx.codewords.push_back(static_cast<std::uint8_t>(b));
					}
				}
			}

			const auto rows = block_rows(interleaver, tx.frames, tx.codewords.size());
			if (!rows) {
				return std::unexpected(rows.error());
			}
			tx.rows = *rows;
			const auto core = make_core(interleaver, tx.codewords.size(), tx.rows);
			tx.sent = shuffle(core.get(), tx.codewords, true);
			return tx;
		}
		catch (const std::exception& e) {
			return std::unexpected(std::format("AFF3CT: {}", e.what()));
		}
	}

	std::vector<float> perfect_soft(std::span<const std::uint8_t> bits) {
		std::vector<float> soft(bits.size());
		for (std::size_t i = 0; i < bits.size(); ++i) {
			soft[i] = bits[i] ? -1.0f : 1.0f;
		}
		return soft;
	}

	std::expected<Reception, std::string> receive(const Transmission& tx, std::vector<float> soft) {
		try {
			Reception rx;
			soft.resize(tx.sent.size(), 0.0f); // what never arrived is unknown
			rx.soft = std::move(soft);
			for (std::size_t i = 0; i < rx.soft.size(); ++i) {
				if (hard(rx.soft[i]) != tx.sent[i]) {
					++rx.channel_errors;
				}
				if (rx.soft[i] == 0.0f) {
					++rx.erased;
				}
			}

			const auto core = make_core(tx.interleaver, tx.sent.size(), tx.rows);
			rx.deinterleaved = shuffle(core.get(), rx.soft, false);
			rx.received.resize(rx.deinterleaved.size());
			std::ranges::transform(rx.deinterleaved, rx.received.begin(), hard);

			rx.frame_wrong.assign(tx.frames, false);
			if (!tx.code.enabled) {
				rx.decoded = rx.received;
			}
			else {
				const Polar polar(tx.code);
				const auto  n = tx.frame_size;
				if (polar.crc) {
					rx.crc_failed.assign(tx.frames, false);
				}
				std::vector<R> frame(n);
				for (std::size_t f = 0; f < tx.frames; ++f) {
					std::copy_n(rx.deinterleaved.begin() + static_cast<std::ptrdiff_t>(f * n), n, frame.begin());
					const auto result = polar.decode(frame);
					if (polar.crc) {
						rx.crc_failed[f] = !result.crc_ok;
					}
					for (const B b : result.info) {
						rx.decoded.push_back(static_cast<std::uint8_t>(b));
					}
				}
			}

			const std::size_t per_frame =
				tx.code.enabled ? static_cast<std::size_t>(tx.code.message_bits()) : tx.message.size();
			rx.decoded.resize(tx.message.size());
			for (std::size_t i = 0; i < rx.decoded.size(); ++i) {
				if (rx.decoded[i] != tx.message[i]) {
					++rx.decoded_errors;
					rx.frame_wrong[std::min(i / per_frame, tx.frames - 1)] = true;
				}
			}
			rx.frame_errors = static_cast<std::size_t>(std::ranges::count(rx.frame_wrong, true));
			return rx;
		}
		catch (const std::exception& e) {
			return std::unexpected(std::format("AFF3CT: {}", e.what()));
		}
	}

	// ---- damage --------------------------------------------------------------------------------------------

	std::string describe(const Damage& d) {
		std::string where;
		switch (d.pattern) {
		case Pattern::All:      where = "every bit"; break;
		case Pattern::Random:   where = std::format("{:g} % of the bits", d.probability * 100.0f); break;
		case Pattern::EveryNth: where = std::format("1 bit in {}", d.every); break;
		case Pattern::Burst:    where = std::format("{} bits from bit {}", d.burst_length, d.burst_start); break;
		}
		switch (d.effect) {
		case Effect::Flip:     return "flip " + where;
		case Effect::FlipHard: return "flip (hard) " + where;
		case Effect::Erase:    return "erase " + where;
		case Effect::Noise:    return std::format("noise (sigma {:g}) on {}", d.sigma, where);
		}
		return where;
	}

	std::vector<float> apply(std::vector<float> soft, std::span<const Damage> steps) {
		for (const Damage& d : steps) {
			std::mt19937                    rng{ d.seed };
			std::bernoulli_distribution     chosen{ std::clamp(static_cast<double>(d.probability), 0.0, 1.0) };
			std::normal_distribution<float> noise{ 0.0f, std::max(d.sigma, 0.0f) };
			const auto                      every = static_cast<std::size_t>(std::max(d.every, 1));
			const auto                      offset = static_cast<std::size_t>(std::max(d.offset, 0));
			const auto                      burst_begin = static_cast<std::size_t>(std::max(d.burst_start, 0));
			const auto burst_end = burst_begin + static_cast<std::size_t>(std::max(d.burst_length, 0));

			for (std::size_t i = 0; i < soft.size(); ++i) {
				bool hit = false;
				switch (d.pattern) {
				case Pattern::All:      hit = true; break;
				case Pattern::Random:   hit = chosen(rng); break;
				case Pattern::EveryNth: hit = i >= offset && (i - offset) % every == 0; break;
				case Pattern::Burst:    hit = i >= burst_begin && i < burst_end; break;
				}
				if (!hit) {
					continue;
				}
				float& v = soft[i];
				switch (d.effect) {
				case Effect::Flip:     v = -v; break;
				case Effect::FlipHard: v = v >= 0.0f ? -1.0f : 1.0f; break;
				case Effect::Erase:    v = 0.0f; break;
				case Effect::Noise:    v += noise(rng); break;
				}
			}
		}
		return soft;
	}

	// ---- performance curve -----------------------------------------------------------------------------------

	std::vector<CurvePoint> ber_curve(
		const CodeSpec& code,
		double          from_db,
		double          to_db,
		double          step_db,
		std::size_t     frames,
		std::stop_token stop
	) {
		std::vector<CurvePoint> curve;
		if (!code.enabled || !check(code).empty() || step_db <= 0.0) {
			return curve;
		}
		try {
			const Polar                 polar(code);
			const int                   n = code.n(), per_codeword = code.message_bits();
			const double                rate = code.rate();
			module::Channel_AWGN_LLR<R> channel(n, tools::Gaussian_noise_generator_implem::STD, 1);
			std::mt19937                rng{ 7 };
			std::vector<B> info(static_cast<std::size_t>(per_codeword)), codeword(static_cast<std::size_t>(n));
			std::vector<R> symbols(static_cast<std::size_t>(n)), received(static_cast<std::size_t>(n));

			for (double db = from_db; db <= to_db + 1e-9; db += step_db) {
				const auto  sigma = static_cast<float>(sigma_for(db, rate));
				const float llr_scale = 2.0f / (sigma * sigma);
				std::size_t bit_errors = 0, frame_errors = 0, sent = 0;
				for (; sent < frames && frame_errors < 200; ++sent) {
					if (stop.stop_requested()) {
						return {};
					}
					for (B& b : info) {
						b = static_cast<B>(rng() & 1);
					}
					polar.encode(info, codeword);
					for (int i = 0; i < n; ++i) {
						symbols[static_cast<std::size_t>(i)] =
							codeword[static_cast<std::size_t>(i)] ? -1.0f : 1.0f; // BPSK
					}
					channel.add_noise(&sigma, symbols.data(), received.data());
					for (R& v : received) {
						v *= llr_scale; // -> log-likelihood ratios
					}
					const auto  result = polar.decode(received);
					std::size_t wrong = 0;
					for (std::size_t i = 0; i < info.size(); ++i) {
						wrong += result.info[i] != info[i];
					}
					bit_errors += wrong;
					frame_errors += wrong > 0;
				}
				curve.push_back(
					{
						.eb_n0_db = db,
						.ber = static_cast<double>(bit_errors) /
				               static_cast<double>(sent * static_cast<std::size_t>(per_codeword)),
						.fer = static_cast<double>(frame_errors) / static_cast<double>(sent),
						.uncoded = 0.5 * std::erfc(std::sqrt(std::pow(10.0, db / 10.0))),
					}
				);
				if (bit_errors == 0) {
					break; // below what this many frames can measure
				}
			}
		}
		catch (const std::exception&) {
			return {};
		}
		return curve;
	}

	// ---- text <-> bits -----------------------------------------------------------------------------------------

	Bits text_to_bits(std::string_view text) {
		Bits bits;
		bits.reserve(text.size() * 8);
		for (const char c : text) {
			for (int b = 7; b >= 0; --b) {
				bits.push_back(static_cast<std::uint8_t>((static_cast<unsigned char>(c) >> b) & 1));
			}
		}
		return bits;
	}

	std::string bits_to_text(std::span<const std::uint8_t> bits) {
		std::string text;
		for (std::size_t i = 0; i + 8 <= bits.size(); i += 8) {
			unsigned value = 0;
			for (std::size_t b = 0; b < 8; ++b) {
				value = value << 1 | (bits[i + b] & 1u);
			}
			text += (value >= 32 && value < 127) || value == '\n' ? static_cast<char>(value) : '.';
		}
		return text;
	}

	Bits random_bits(std::size_t count, unsigned seed) {
		std::mt19937 rng{ seed };
		Bits         bits(count);
		for (auto& b : bits) {
			b = static_cast<std::uint8_t>(rng() & 1);
		}
		return bits;
	}

	std::string to_string(std::span<const std::uint8_t> bits) {
		std::string out(bits.size(), '0');
		for (std::size_t i = 0; i < bits.size(); ++i) {
			out[i] = bits[i] ? '1' : '0';
		}
		return out;
	}

} // namespace sig::coding
