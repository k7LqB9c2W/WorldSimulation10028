#pragma once

#include <cstdint>
#include <random>

struct SimulationContext {
    std::uint64_t worldSeed = 0;
    std::mt19937_64 worldRng;

    explicit SimulationContext(std::uint64_t seed);

    double rand01();
    int randInt(int a, int b); // inclusive
    double randNormal(double mean = 0.0, double stddev = 1.0);

    std::uint64_t seedForCountry(int countryIndex) const;
    std::mt19937_64 makeRng(std::uint64_t salt) const;

    static std::uint64_t mix64(std::uint64_t x);
    static double u01FromU64(std::uint64_t x);
};

