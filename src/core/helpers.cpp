
#include "helpers.h"

bool get_random_chance(std::ranlux24_base& rng, float chance) {
	auto dice = get_random_number<float>(rng, 0.0, 1.0);
	return dice <= chance;
}