#include "simulation_context.h"

#include <algorithm>

SimulationContext::SimulationContext(std::uint64_t seed)
    : worldSeed(seed), worldRng(seed) {}

double SimulationContext::rand01() {
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    return dist(worldRng);
}

int SimulationContext::randInt(int a, int b) {
    if (a > b) {
        std::swap(a, b);
    }
    std::uniform_int_distribution<int> dist(a, b);
    return dist(worldRng);
}

double SimulationContext::randNormal(double mean, double stddev) {
    std::normal_distribution<double> dist(mean, stddev);
    return dist(worldRng);
}

std::uint64_t SimulationContext::seedForCountry(int countryIndex) const {
    const std::uint64_t idx = static_cast<std::uint64_t>(std::max(0, countryIndex));
    return mix64(worldSeed ^ (idx * 0x9E3779B97F4A7C15ull) ^ 0xC0C0C0C0C0C0C0C0ull);
}

std::mt19937_64 SimulationContext::makeRng(std::uint64_t salt) const {
    return std::mt19937_64(mix64(worldSeed ^ salt));
}

std::uint64_t SimulationContext::mix64(std::uint64_t x) {
    // SplitMix64 finalizer (fast, deterministic, good bit diffusion).
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

double SimulationContext::u01FromU64(std::uint64_t x) {
    // Convert 53 random bits to [0,1).
    const std::uint64_t mantissa = (x >> 11);
    return static_cast<double>(mantissa) * (1.0 / 9007199254740992.0);
}
