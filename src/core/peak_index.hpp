#pragma once

// Min/max summary of one channel, so a zoomed-out view can find the extremes of millions of samples
// without reading them all.
//
// Like a book with a summary per chapter: to find the most exciting moment in chapters 3 to 50, read
// the summaries of the chapters in between, and only the actual pages at the two ragged ends.
//
// The samples are split into blocks of `block_size`, and the min/max of every block is stored up front
// (about 3 % extra memory). A range query then reads only the samples at its two ragged ends plus
// one stored pair per whole block in between, so it costs at most ~2 * block_size + range / block_size reads
// instead of `range`.
//
// The index refers to the samples, it does not copy them: rebuild it whenever they change or move.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <vector>

namespace sig {

	struct MinMax {
		float min = std::numeric_limits<float>::infinity(); // an empty range has min > max
		float max = -std::numeric_limits<float>::infinity();

		void add(float v) {
			min = std::min(min, v);
			max = std::max(max, v);
		}
		void add(const MinMax& other) {
			min = std::min(min, other.min);
			max = std::max(max, other.max);
		}

		[[nodiscard]] bool empty() const {
			return min > max;
		}
		[[nodiscard]] float peak() const {
			return empty() ? 0.0f : std::max(std::abs(min), std::abs(max));
		}
	};

	class PeakIndex {
	public:
		static constexpr std::size_t block_size = 256;

		PeakIndex() = default;

		explicit PeakIndex(std::span<const float> samples) : samples_{ samples } {
			blocks_.reserve(samples.size() / block_size + 1);
			for (std::size_t begin = 0; begin < samples.size(); begin += block_size) {
				blocks_.push_back(scan(begin, std::min(begin + block_size, samples.size())));
			}
		}

		[[nodiscard]] std::span<const float> samples() const {
			return samples_;
		}

		// Extremes of samples [begin, end). `end` is clamped to the number of samples.
		[[nodiscard]] MinMax range(std::size_t begin, std::size_t end) const {
			end = std::min(end, samples_.size());
			if (begin >= end) {
				return {};
			}

			const std::size_t first_block = (begin + block_size - 1) / block_size; // first block entirely inside
			const std::size_t last_block = end / block_size;                       // one past the last one
			if (first_block >= last_block) {
				return scan(begin, end); // too short to contain a block
			}

			MinMax result = scan(begin, first_block * block_size);
			for (std::size_t b = first_block; b < last_block; ++b) {
				result.add(blocks_[b]);
			}
			result.add(scan(last_block * block_size, end));
			return result;
		}

		[[nodiscard]] MinMax all() const {
			MinMax result;
			for (const MinMax& block : blocks_) {
				result.add(block);
			}
			return result;
		}

	private:
		[[nodiscard]] MinMax scan(std::size_t begin, std::size_t end) const {
			MinMax result;
			for (const float v : samples_.subspan(begin, end - begin)) {
				result.add(v);
			}
			return result;
		}

		std::span<const float> samples_;
		std::vector<MinMax>    blocks_;
	};

} // namespace sig
