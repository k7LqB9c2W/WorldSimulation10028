#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class SubsistenceMode : std::uint8_t {
    Foraging = 0,
    Farming = 1,
    Pastoral = 2,
    Fishing = 3,
    Craft = 4,
    Count = 5
};

struct DomesticPackageDefinition {
    int id = -1;
    std::string key;
    double foragingMul = 1.0;
    double farmingMul = 1.0;
    double pastoralMul = 1.0;
    double fishingMul = 1.0;
    double storageBonus = 0.0;

    // Suitability weights used by deterministic adoption scoring.
    double waterAffinity = 0.0;
    double aridAffinity = 0.0;
    double coldAffinity = 0.0;
    double marketAffinity = 0.0;
};

const std::vector<DomesticPackageDefinition>& getDefaultDomesticPackages();
