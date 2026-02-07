#include "simulation_context.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <type_traits>

#include <toml++/toml.hpp>

namespace {

template <typename T>
void readTomlValue(const toml::table& root,
                   std::string_view section,
                   std::string_view key,
                   T& target) {
    const toml::node_view<const toml::node> view = root[section][key];
    if constexpr (std::is_same_v<T, int>) {
        if (const auto v = view.value<std::int64_t>()) {
            target = static_cast<int>(*v);
        }
    } else if constexpr (std::is_same_v<T, double>) {
        if (const auto v = view.value<double>()) {
            target = *v;
        } else if (const auto vi = view.value<std::int64_t>()) {
            target = static_cast<double>(*vi);
        }
    } else if constexpr (std::is_same_v<T, bool>) {
        if (const auto v = view.value<bool>()) {
            target = *v;
        }
    } else if constexpr (std::is_same_v<T, std::string>) {
        if (const auto v = view.value<std::string>()) {
            target = *v;
        }
    }
}

std::string toLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string canonicalDetOverseasFallback(std::string value) {
    value = toLowerAscii(std::move(value));
    if (value == "on" || value == "enabled" || value == "true" || value == "1") {
        return "on";
    }
    if (value == "off" || value == "disabled" || value == "false" || value == "0") {
        return "off";
    }
    return "auto";
}

} // namespace

SimulationContext::SimulationContext(std::uint64_t seed, const std::string& runtimeConfigPath)
    : worldSeed(seed), worldRng(seed), config(), configPath(runtimeConfigPath), configHash("defaults") {
    if (!runtimeConfigPath.empty()) {
        std::string err;
        if (!loadConfig(runtimeConfigPath, &err)) {
            std::cerr << "[Config] " << err << " Using built-in defaults.\n";
        }
    }
}

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

bool SimulationContext::loadConfig(const std::string& path, std::string* errorMessage) {
    config = SimulationConfig{};
    configPath = path;
    configHash = "defaults";

    if (path.empty()) {
        return true;
    }

    try {
        toml::table root = toml::parse_file(path);

        readTomlValue(root, "world", "yearsPerTick", config.world.yearsPerTick);
        readTomlValue(root, "world", "startYear", config.world.startYear);
        readTomlValue(root, "world", "endYear", config.world.endYear);
        readTomlValue(root, "world", "rngSeedMode", config.world.rngSeedMode);
        readTomlValue(root, "world", "deterministicMode", config.world.deterministicMode);
        readTomlValue(root, "world", "deterministicOverseasFallback", config.world.deterministicOverseasFallback);
        config.world.deterministicOverseasFallback =
            canonicalDetOverseasFallback(config.world.deterministicOverseasFallback);

        readTomlValue(root, "food", "baseForaging", config.food.baseForaging);
        readTomlValue(root, "food", "baseFarming", config.food.baseFarming);
        readTomlValue(root, "food", "climateSensitivity", config.food.climateSensitivity);
        readTomlValue(root, "food", "riverlandFoodFloor", config.food.riverlandFoodFloor);
        readTomlValue(root, "food", "coastalBonus", config.food.coastalBonus);
        readTomlValue(root, "food", "spoilageBase", config.food.spoilageBase);
        readTomlValue(root, "food", "storageBase", config.food.storageBase);
        readTomlValue(root, "food", "clayMin", config.food.clayMin);
        readTomlValue(root, "food", "clayMax", config.food.clayMax);
        readTomlValue(root, "food", "clayHotspotChance", config.food.clayHotspotChance);
        readTomlValue(root, "food", "foragingNoAgriShare", config.food.foragingNoAgriShare);
        readTomlValue(root, "food", "foragingWithAgriShare", config.food.foragingWithAgriShare);
        readTomlValue(root, "food", "farmingWithAgriShare", config.food.farmingWithAgriShare);

        readTomlValue(root, "resources", "oreWeightIron", config.resources.oreWeightIron);
        readTomlValue(root, "resources", "oreWeightCopper", config.resources.oreWeightCopper);
        readTomlValue(root, "resources", "oreWeightTin", config.resources.oreWeightTin);
        readTomlValue(root, "resources", "energyBiomassBase", config.resources.energyBiomassBase);
        readTomlValue(root, "resources", "energyCoalWeight", config.resources.energyCoalWeight);
        readTomlValue(root, "resources", "constructionClayWeight", config.resources.constructionClayWeight);
        readTomlValue(root, "resources", "constructionStoneBase", config.resources.constructionStoneBase);
        readTomlValue(root, "resources", "oreNormalization", config.resources.oreNormalization);
        readTomlValue(root, "resources", "energyNormalization", config.resources.energyNormalization);
        readTomlValue(root, "resources", "constructionNormalization", config.resources.constructionNormalization);
        readTomlValue(root, "resources", "oreDepletionRate", config.resources.oreDepletionRate);
        readTomlValue(root, "resources", "coalDepletionRate", config.resources.coalDepletionRate);

        readTomlValue(root, "migration", "famineShockThreshold", config.migration.famineShockThreshold);
        readTomlValue(root, "migration", "epidemicShockThreshold", config.migration.epidemicShockThreshold);
        readTomlValue(root, "migration", "warShockThreshold", config.migration.warShockThreshold);
        readTomlValue(root, "migration", "famineShockMultiplier", config.migration.famineShockMultiplier);
        readTomlValue(root, "migration", "epidemicShockMultiplier", config.migration.epidemicShockMultiplier);
        readTomlValue(root, "migration", "warShockMultiplier", config.migration.warShockMultiplier);
        readTomlValue(root, "migration", "corridorCoastBonus", config.migration.corridorCoastBonus);
        readTomlValue(root, "migration", "corridorRiverlandBonus", config.migration.corridorRiverlandBonus);
        readTomlValue(root, "migration", "corridorSteppeBonus", config.migration.corridorSteppeBonus);
        readTomlValue(root, "migration", "corridorMountainPenalty", config.migration.corridorMountainPenalty);
        readTomlValue(root, "migration", "corridorDesertPenalty", config.migration.corridorDesertPenalty);
        readTomlValue(root, "migration", "refugeeHalfLifeYears", config.migration.refugeeHalfLifeYears);
        readTomlValue(root, "migration", "culturalPreference", config.migration.culturalPreference);

        readTomlValue(root, "disease", "initialInfectedShare", config.disease.initialInfectedShare);
        readTomlValue(root, "disease", "initialRecoveredShare", config.disease.initialRecoveredShare);
        readTomlValue(root, "disease", "tradeImportWeight", config.disease.tradeImportWeight);
        readTomlValue(root, "disease", "endemicBase", config.disease.endemicBase);
        readTomlValue(root, "disease", "endemicUrbanWeight", config.disease.endemicUrbanWeight);
        readTomlValue(root, "disease", "endemicHumidityWeight", config.disease.endemicHumidityWeight);
        readTomlValue(root, "disease", "endemicInstitutionMitigation", config.disease.endemicInstitutionMitigation);
        readTomlValue(root, "disease", "zoonoticBase", config.disease.zoonoticBase);
        readTomlValue(root, "disease", "zoonoticForagingWeight", config.disease.zoonoticForagingWeight);
        readTomlValue(root, "disease", "zoonoticFarmingWeight", config.disease.zoonoticFarmingWeight);
        readTomlValue(root, "disease", "spilloverShockChance", config.disease.spilloverShockChance);
        readTomlValue(root, "disease", "spilloverShockMin", config.disease.spilloverShockMin);
        readTomlValue(root, "disease", "spilloverShockMax", config.disease.spilloverShockMax);
        readTomlValue(root, "disease", "warAmplifier", config.disease.warAmplifier);
        readTomlValue(root, "disease", "famineAmplifier", config.disease.famineAmplifier);

        readTomlValue(root, "war", "supplyBase", config.war.supplyBase);
        readTomlValue(root, "war", "supplyLogisticsWeight", config.war.supplyLogisticsWeight);
        readTomlValue(root, "war", "supplyMarketWeight", config.war.supplyMarketWeight);
        readTomlValue(root, "war", "supplyControlWeight", config.war.supplyControlWeight);
        readTomlValue(root, "war", "supplyEnergyWeight", config.war.supplyEnergyWeight);
        readTomlValue(root, "war", "supplyFoodStockWeight", config.war.supplyFoodStockWeight);
        readTomlValue(root, "war", "overSupplyAttrition", config.war.overSupplyAttrition);
        readTomlValue(root, "war", "terrainDefenseWeight", config.war.terrainDefenseWeight);
        readTomlValue(root, "war", "exhaustionRise", config.war.exhaustionRise);
        readTomlValue(root, "war", "exhaustionPeaceThreshold", config.war.exhaustionPeaceThreshold);
        readTomlValue(root, "war", "objectiveRaidWeight", config.war.objectiveRaidWeight);
        readTomlValue(root, "war", "objectiveBorderWeight", config.war.objectiveBorderWeight);
        readTomlValue(root, "war", "objectiveTributeWeight", config.war.objectiveTributeWeight);
        readTomlValue(root, "war", "objectiveVassalWeight", config.war.objectiveVassalWeight);
        readTomlValue(root, "war", "objectiveRegimeWeight", config.war.objectiveRegimeWeight);
        readTomlValue(root, "war", "objectiveAnnihilationWeight", config.war.objectiveAnnihilationWeight);
        readTomlValue(root, "war", "cooldownMinYears", config.war.cooldownMinYears);
        readTomlValue(root, "war", "cooldownMaxYears", config.war.cooldownMaxYears);
        readTomlValue(root, "war", "peaceReparationsWeight", config.war.peaceReparationsWeight);
        readTomlValue(root, "war", "peaceTributeWeight", config.war.peaceTributeWeight);
        readTomlValue(root, "war", "peaceReconstructionDrag", config.war.peaceReconstructionDrag);
        readTomlValue(root, "war", "earlyAnnihilationBias", config.war.earlyAnnihilationBias);
        readTomlValue(root, "war", "highInstitutionAnnihilationDamp", config.war.highInstitutionAnnihilationDamp);

        readTomlValue(root, "polity", "regionCountMin", config.polity.regionCountMin);
        readTomlValue(root, "polity", "regionCountMax", config.polity.regionCountMax);
        readTomlValue(root, "polity", "successionIntervalMin", config.polity.successionIntervalMin);
        readTomlValue(root, "polity", "successionIntervalMax", config.polity.successionIntervalMax);
        readTomlValue(root, "polity", "eliteDefectionSensitivity", config.polity.eliteDefectionSensitivity);
        readTomlValue(root, "polity", "farRegionPenalty", config.polity.farRegionPenalty);
        readTomlValue(root, "polity", "yearlyWarStabilityHit", config.polity.yearlyWarStabilityHit);
        readTomlValue(root, "polity", "yearlyPlagueStabilityHit", config.polity.yearlyPlagueStabilityHit);
        readTomlValue(root, "polity", "yearlyStagnationStabilityHit", config.polity.yearlyStagnationStabilityHit);
        readTomlValue(root, "polity", "peaceRecoveryLowGrowth", config.polity.peaceRecoveryLowGrowth);
        readTomlValue(root, "polity", "peaceRecoveryHighGrowth", config.polity.peaceRecoveryHighGrowth);
        readTomlValue(root, "polity", "resilienceRecoveryStrength", config.polity.resilienceRecoveryStrength);
        readTomlValue(root, "polity", "demogShortageStabilityHit", config.polity.demogShortageStabilityHit);
        readTomlValue(root, "polity", "demogDiseaseStabilityHit", config.polity.demogDiseaseStabilityHit);
        readTomlValue(root, "polity", "demogShortageLegitimacyHit", config.polity.demogShortageLegitimacyHit);
        readTomlValue(root, "polity", "demogDiseaseLegitimacyHit", config.polity.demogDiseaseLegitimacyHit);
        readTomlValue(root, "polity", "legitimacyRecoveryStrength", config.polity.legitimacyRecoveryStrength);

        readTomlValue(root, "tech", "capabilityThresholdScale", config.tech.capabilityThresholdScale);
        readTomlValue(root, "tech", "diffusionBase", config.tech.diffusionBase);
        readTomlValue(root, "tech", "culturalFrictionStrength", config.tech.culturalFrictionStrength);
        readTomlValue(root, "tech", "resourceReqEnergy", config.tech.resourceReqEnergy);
        readTomlValue(root, "tech", "resourceReqOre", config.tech.resourceReqOre);
        readTomlValue(root, "tech", "resourceReqConstruction", config.tech.resourceReqConstruction);

        readTomlValue(root, "economy", "foodLaborElasticity", config.economy.foodLaborElasticity);
        readTomlValue(root, "economy", "goodsLaborElasticity", config.economy.goodsLaborElasticity);
        readTomlValue(root, "economy", "servicesLaborElasticity", config.economy.servicesLaborElasticity);
        readTomlValue(root, "economy", "energyIntensity", config.economy.energyIntensity);
        readTomlValue(root, "economy", "oreIntensity", config.economy.oreIntensity);
        readTomlValue(root, "economy", "goodsToMilitary", config.economy.goodsToMilitary);
        readTomlValue(root, "economy", "servicesScaling", config.economy.servicesScaling);
        readTomlValue(root, "economy", "tradeResourceMismatchDemandBoost", config.economy.tradeResourceMismatchDemandBoost);
        readTomlValue(root, "economy", "tradeScarcityCapacityBoost", config.economy.tradeScarcityCapacityBoost);
        readTomlValue(root, "economy", "tradeMaxPricePremium", config.economy.tradeMaxPricePremium);
        readTomlValue(root, "economy", "tradeIntensityScale", config.economy.tradeIntensityScale);
        readTomlValue(root, "economy", "tradeIntensityValueNormBase", config.economy.tradeIntensityValueNormBase);
        readTomlValue(root, "economy", "tradeIntensityMemory", config.economy.tradeIntensityMemory);
        readTomlValue(root, "economy", "useGPU", config.economy.useGPU);

        if (const toml::array* checkpoints = root["scoring"]["checkpointsYears"].as_array()) {
            std::vector<int> parsed;
            parsed.reserve(checkpoints->size());
            for (const auto& node : *checkpoints) {
                if (const auto v = node.value<std::int64_t>()) {
                    parsed.push_back(static_cast<int>(*v));
                }
            }
            if (!parsed.empty()) {
                config.scoring.checkpointsYears = std::move(parsed);
            }
        }
        readTomlValue(root, "scoring", "weightFoodSecurityStability", config.scoring.weightFoodSecurityStability);
        readTomlValue(root, "scoring", "weightInnovationUrbanization", config.scoring.weightInnovationUrbanization);
        readTomlValue(root, "scoring", "weightEmpireLogisticsConstraint", config.scoring.weightEmpireLogisticsConstraint);
        readTomlValue(root, "scoring", "weightDiseaseTransition", config.scoring.weightDiseaseTransition);
        readTomlValue(root, "scoring", "weightTradeResourceInequality", config.scoring.weightTradeResourceInequality);
        readTomlValue(root, "scoring", "weightVariancePenalty", config.scoring.weightVariancePenalty);
        readTomlValue(root, "scoring", "weightBrittlenessPenalty", config.scoring.weightBrittlenessPenalty);

        configHash = hashFileFNV1a(path);
        return true;
    } catch (const toml::parse_error& err) {
        if (errorMessage) {
            std::ostringstream oss;
            oss << "Failed to parse config '" << path << "': " << err.description();
            *errorMessage = oss.str();
        }
    } catch (const std::exception& err) {
        if (errorMessage) {
            std::ostringstream oss;
            oss << "Failed to load config '" << path << "': " << err.what();
            *errorMessage = oss.str();
        }
    }

    return false;
}

std::string SimulationContext::hashFileFNV1a(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return "missing";
    }
    std::uint64_t h = 1469598103934665603ull;
    constexpr std::uint64_t prime = 1099511628211ull;
    char buffer[4096];
    while (in.good()) {
        in.read(buffer, static_cast<std::streamsize>(sizeof(buffer)));
        const std::streamsize n = in.gcount();
        for (std::streamsize i = 0; i < n; ++i) {
            h ^= static_cast<std::uint8_t>(buffer[i]);
            h *= prime;
        }
    }
    std::ostringstream oss;
    oss << std::hex << h;
    return oss.str();
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
