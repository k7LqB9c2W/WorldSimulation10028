#include "simulation_context.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
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
    } else if constexpr (std::is_same_v<T, std::int64_t>) {
        if (const auto v = view.value<std::int64_t>()) {
            target = *v;
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

SimulationConfig::WorldPopulationConfig::Mode parsePopulationMode(std::string value) {
    value = toLowerAscii(std::move(value));
    if (value == "fixed") {
        return SimulationConfig::WorldPopulationConfig::Mode::Fixed;
    }
    return SimulationConfig::WorldPopulationConfig::Mode::Range;
}

SimulationConfig::SpawnConfig::DuplicateColorMode parseDuplicateColorMode(std::string value) {
    value = toLowerAscii(std::move(value));
    if (value == "erroronduplicate" || value == "error" || value == "strict") {
        return SimulationConfig::SpawnConfig::DuplicateColorMode::ErrorOnDuplicate;
    }
    return SimulationConfig::SpawnConfig::DuplicateColorMode::SplitConnectedComponents;
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
        if (const auto mode = root["world"]["population"]["mode"].value<std::string>()) {
            config.world.population.mode = parsePopulationMode(*mode);
        }
        if (const auto v = root["world"]["population"]["fixedValue"].value<std::int64_t>()) {
            config.world.population.fixedValue = *v;
        }
        if (const auto v = root["world"]["population"]["minValue"].value<std::int64_t>()) {
            config.world.population.minValue = *v;
        }
        if (const auto v = root["world"]["population"]["maxValue"].value<std::int64_t>()) {
            config.world.population.maxValue = *v;
        }

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
        readTomlValue(root, "migration", "frontierClaimPopulationThreshold", config.migration.frontierClaimPopulationThreshold);
        readTomlValue(root, "migration", "frontierClaimControlThreshold", config.migration.frontierClaimControlThreshold);

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
        readTomlValue(root, "war", "maxConcurrentWars", config.war.maxConcurrentWars);
        readTomlValue(root, "war", "leaderAmbitionWarWeight", config.war.leaderAmbitionWarWeight);
        readTomlValue(root, "war", "weakStatePredationWeight", config.war.weakStatePredationWeight);
        readTomlValue(root, "war", "opportunisticWarThreshold", config.war.opportunisticWarThreshold);

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
        readTomlValue(root, "polity", "lowCapabilityFiscalThreshold", config.polity.lowCapabilityFiscalThreshold);
        readTomlValue(root, "polity", "lowCapabilityNearBalanceCap", config.polity.lowCapabilityNearBalanceCap);
        readTomlValue(root, "polity", "lowCapabilityBorrowingScale", config.polity.lowCapabilityBorrowingScale);
        readTomlValue(root, "polity", "lowCapabilityReserveMonthsTarget", config.polity.lowCapabilityReserveMonthsTarget);
        readTomlValue(root, "polity", "debtMarketAccessFloor", config.polity.debtMarketAccessFloor);
        readTomlValue(root, "polity", "debtMarketAccessSlope", config.polity.debtMarketAccessSlope);
        readTomlValue(root, "polity", "revenueTrendFastAlpha", config.polity.revenueTrendFastAlpha);
        readTomlValue(root, "polity", "revenueTrendSlowAlpha", config.polity.revenueTrendSlowAlpha);
        readTomlValue(root, "polity", "revenueTrendSpendSensitivity", config.polity.revenueTrendSpendSensitivity);
        readTomlValue(root, "polity", "debtServiceAusterityThreshold", config.polity.debtServiceAusterityThreshold);
        readTomlValue(root, "polity", "debtServiceAusterityStrength", config.polity.debtServiceAusterityStrength);
        readTomlValue(root, "polity", "subsistenceAdminFloorShare", config.polity.subsistenceAdminFloorShare);
        readTomlValue(root, "polity", "earlyLegitimacyProvisioningWeight", config.polity.earlyLegitimacyProvisioningWeight);
        readTomlValue(root, "polity", "earlyLegitimacyFiscalWeight", config.polity.earlyLegitimacyFiscalWeight);
        readTomlValue(root, "polity", "stateTurnoverBaseChance", config.polity.stateTurnoverBaseChance);
        readTomlValue(root, "polity", "stateTurnoverStressWeight", config.polity.stateTurnoverStressWeight);
        readTomlValue(root, "polity", "stateTurnoverAgeWeight", config.polity.stateTurnoverAgeWeight);
        readTomlValue(root, "polity", "successionCrisisSplitWeight", config.polity.successionCrisisSplitWeight);
        readTomlValue(root, "polity", "institutionalContinuityShield", config.polity.institutionalContinuityShield);
        readTomlValue(root, "polity", "stateTurnoverMinAgeYears", config.polity.stateTurnoverMinAgeYears);

        readTomlValue(root, "tech", "capabilityThresholdScale", config.tech.capabilityThresholdScale);
        readTomlValue(root, "tech", "diffusionBase", config.tech.diffusionBase);
        readTomlValue(root, "tech", "culturalFrictionStrength", config.tech.culturalFrictionStrength);
        readTomlValue(root, "tech", "resourceReqEnergy", config.tech.resourceReqEnergy);
        readTomlValue(root, "tech", "resourceReqOre", config.tech.resourceReqOre);
        readTomlValue(root, "tech", "resourceReqConstruction", config.tech.resourceReqConstruction);
        readTomlValue(root, "tech", "adoptionThreshold", config.tech.adoptionThreshold);
        readTomlValue(root, "tech", "forgetPracticeThreshold", config.tech.forgetPracticeThreshold);
        readTomlValue(root, "tech", "discoveryBase", config.tech.discoveryBase);
        readTomlValue(root, "tech", "discoveryDifficultyScale", config.tech.discoveryDifficultyScale);
        readTomlValue(root, "tech", "maxDiscoveriesPerYear", config.tech.maxDiscoveriesPerYear);
        readTomlValue(root, "tech", "discoverySeedAdoption", config.tech.discoverySeedAdoption);
        readTomlValue(root, "tech", "knownDiffusionBase", config.tech.knownDiffusionBase);
        readTomlValue(root, "tech", "knownDiffusionTopK", config.tech.knownDiffusionTopK);
        readTomlValue(root, "tech", "adoptionSeedFromNeighbors", config.tech.adoptionSeedFromNeighbors);
        readTomlValue(root, "tech", "adoptionBaseSpeed", config.tech.adoptionBaseSpeed);
        readTomlValue(root, "tech", "adoptionDecayBase", config.tech.adoptionDecayBase);
        readTomlValue(root, "tech", "collapseDecayMultiplier", config.tech.collapseDecayMultiplier);
        readTomlValue(root, "tech", "prereqAdoptionFraction", config.tech.prereqAdoptionFraction);
        readTomlValue(root, "tech", "rareForgetYears", config.tech.rareForgetYears);
        readTomlValue(root, "tech", "rareForgetChance", config.tech.rareForgetChance);
        readTomlValue(root, "tech", "innovationVolatility", config.tech.innovationVolatility);
        readTomlValue(root, "tech", "leadershipInnovationWeight", config.tech.leadershipInnovationWeight);
        readTomlValue(root, "tech", "institutionalInertiaPenalty", config.tech.institutionalInertiaPenalty);
        readTomlValue(root, "tech", "europeAdvantageStartYear", config.tech.europeAdvantageStartYear);
        readTomlValue(root, "tech", "europeAdvantagePeakYear", config.tech.europeAdvantagePeakYear);
        readTomlValue(root, "tech", "europeAdvantageFadeYear", config.tech.europeAdvantageFadeYear);
        readTomlValue(root, "tech", "europeInnovationBoost", config.tech.europeInnovationBoost);
        readTomlValue(root, "tech", "europeAdoptionBoost", config.tech.europeAdoptionBoost);
        readTomlValue(root, "tech", "europeReadinessThreshold", config.tech.europeReadinessThreshold);
        readTomlValue(root, "tech", "trajectoryVarianceStrength", config.tech.trajectoryVarianceStrength);
        readTomlValue(root, "tech", "trajectoryCycleYears", config.tech.trajectoryCycleYears);
        readTomlValue(root, "tech", "trajectoryCycleAmplitude", config.tech.trajectoryCycleAmplitude);

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
        readTomlValue(root, "economy", "marketClearingBaseIterations", config.economy.marketClearingBaseIterations);
        readTomlValue(root, "economy", "marketClearingMaxIterations", config.economy.marketClearingMaxIterations);
        readTomlValue(root, "economy", "priceAdjustmentSpeed", config.economy.priceAdjustmentSpeed);
        readTomlValue(root, "economy", "priceStickinessBase", config.economy.priceStickinessBase);
        readTomlValue(root, "economy", "tradeSearchFrictionBase", config.economy.tradeSearchFrictionBase);
        readTomlValue(root, "economy", "creditFrictionWeight", config.economy.creditFrictionWeight);
        readTomlValue(root, "economy", "informationFrictionWeight", config.economy.informationFrictionWeight);
        readTomlValue(root, "economy", "useGPU", config.economy.useGPU);

        readTomlValue(root, "settlements", "enabled", config.settlements.enabled);
        readTomlValue(root, "settlements", "initNodeMinPop", config.settlements.initNodeMinPop);
        readTomlValue(root, "settlements", "maxNodesGlobal", config.settlements.maxNodesGlobal);
        readTomlValue(root, "settlements", "maxNodesPerCountry", config.settlements.maxNodesPerCountry);
        readTomlValue(root, "settlements", "splitPopThreshold", config.settlements.splitPopThreshold);
        readTomlValue(root, "settlements", "splitAlphaMin", config.settlements.splitAlphaMin);
        readTomlValue(root, "settlements", "splitAlphaMax", config.settlements.splitAlphaMax);
        readTomlValue(root, "settlements", "splitCooldownYears", config.settlements.splitCooldownYears);
        readTomlValue(root, "settlements", "splitMinSpacingFields", config.settlements.splitMinSpacingFields);
        readTomlValue(root, "settlements", "growthRMin", config.settlements.growthRMin);
        readTomlValue(root, "settlements", "growthRMax", config.settlements.growthRMax);
        readTomlValue(root, "settlements", "cal0", config.settlements.cal0);
        readTomlValue(root, "settlements", "calSlope", config.settlements.calSlope);
        readTomlValue(root, "settlements", "kBasePerFoodUnit", config.settlements.kBasePerFoodUnit);
        readTomlValue(root, "settlements", "transportRebuildIntervalYears", config.settlements.transportRebuildIntervalYears);

        readTomlValue(root, "density_init", "enabled", config.densityInit.enabled);
        readTomlValue(root, "density_init", "requireLandOwnership", config.densityInit.requireLandOwnership);
        readTomlValue(root, "density_init", "weightPopulation", config.densityInit.weightPopulation);
        readTomlValue(root, "density_init", "weightFood", config.densityInit.weightFood);
        readTomlValue(root, "density_init", "weightCorridor", config.densityInit.weightCorridor);
        readTomlValue(root, "density_init", "weightClimate", config.densityInit.weightClimate);
        readTomlValue(root, "density_init", "scoreNoise", config.densityInit.scoreNoise);
        readTomlValue(root, "density_init", "minPopFactor", config.densityInit.minPopFactor);
        readTomlValue(root, "density_init", "priorPath", config.densityInit.priorPath);
        readTomlValue(root, "density_init", "priorWeight", config.densityInit.priorWeight);

        readTomlValue(root, "paleo_climate", "enabled", config.paleoClimate.enabled);
        readTomlValue(root, "paleo_climate", "startupDiagnostics", config.paleoClimate.startupDiagnostics);
        readTomlValue(root, "paleo_climate", "monthlyForcingCsv", config.paleoClimate.monthlyForcingCsv);
        readTomlValue(root, "paleo_climate", "requireDataFile", config.paleoClimate.requireDataFile);
        readTomlValue(root, "paleo_climate", "tempInfluence", config.paleoClimate.tempInfluence);
        readTomlValue(root, "paleo_climate", "precipInfluence", config.paleoClimate.precipInfluence);
        readTomlValue(root, "paleo_climate", "monsoonVarianceScale", config.paleoClimate.monsoonVarianceScale);
        readTomlValue(root, "paleo_climate", "droughtClusterScale", config.paleoClimate.droughtClusterScale);
        readTomlValue(root, "paleo_climate", "coolingPulseScale", config.paleoClimate.coolingPulseScale);

        readTomlValue(root, "subsistence", "mixAdaptRate", config.subsistence.mixAdaptRate);
        readTomlValue(root, "subsistence", "riskPenaltyWeight", config.subsistence.riskPenaltyWeight);
        readTomlValue(root, "subsistence", "craftFromMarketWeight", config.subsistence.craftFromMarketWeight);

        readTomlValue(root, "packages", "enabled", config.packages.enabled);
        readTomlValue(root, "packages", "adoptionBase", config.packages.adoptionBase);
        readTomlValue(root, "packages", "diffusionWeight", config.packages.diffusionWeight);
        readTomlValue(root, "packages", "environmentWeight", config.packages.environmentWeight);

        readTomlValue(root, "package_diffusion", "enabled", config.packageDiffusion.enabled);
        readTomlValue(root, "package_diffusion", "demicWeight", config.packageDiffusion.demicWeight);
        readTomlValue(root, "package_diffusion", "culturalWeight", config.packageDiffusion.culturalWeight);
        readTomlValue(root, "package_diffusion", "coerciveWeight", config.packageDiffusion.coerciveWeight);
        readTomlValue(root, "package_diffusion", "prestigeWeight", config.packageDiffusion.prestigeWeight);
        readTomlValue(root, "package_diffusion", "conformistThreshold", config.packageDiffusion.conformistThreshold);
        readTomlValue(root, "package_diffusion", "conformistStrength", config.packageDiffusion.conformistStrength);
        readTomlValue(root, "package_diffusion", "coerciveExtractionWeight", config.packageDiffusion.coerciveExtractionWeight);
        readTomlValue(root, "package_diffusion", "coercivePolitySwitchWeight", config.packageDiffusion.coercivePolitySwitchWeight);

        readTomlValue(root, "knowledge_dynamics", "enabled", config.knowledgeDynamics.enabled);
        readTomlValue(root, "knowledge_dynamics", "maintenanceSpecialistThreshold", config.knowledgeDynamics.maintenanceSpecialistThreshold);
        readTomlValue(root, "knowledge_dynamics", "maintenanceMarketThreshold", config.knowledgeDynamics.maintenanceMarketThreshold);
        readTomlValue(root, "knowledge_dynamics", "lossRate", config.knowledgeDynamics.lossRate);
        readTomlValue(root, "knowledge_dynamics", "collapseLossScale", config.knowledgeDynamics.collapseLossScale);
        readTomlValue(root, "knowledge_dynamics", "recoveryRate", config.knowledgeDynamics.recoveryRate);
        readTomlValue(root, "knowledge_dynamics", "relearnFromNeighbors", config.knowledgeDynamics.relearnFromNeighbors);

        readTomlValue(root, "salinity", "enabled", config.salinity.enabled);
        readTomlValue(root, "salinity", "irrigationAccumulation", config.salinity.irrigationAccumulation);
        readTomlValue(root, "salinity", "aridityAccumulation", config.salinity.aridityAccumulation);
        readTomlValue(root, "salinity", "drainageRecovery", config.salinity.drainageRecovery);
        readTomlValue(root, "salinity", "rainfallDrainageWeight", config.salinity.rainfallDrainageWeight);
        readTomlValue(root, "salinity", "corridorDrainageWeight", config.salinity.corridorDrainageWeight);
        readTomlValue(root, "salinity", "infraDrainageWeight", config.salinity.infraDrainageWeight);
        readTomlValue(root, "salinity", "yieldPenaltyMax", config.salinity.yieldPenaltyMax);

        readTomlValue(root, "transport", "kNearest", config.transport.kNearest);
        readTomlValue(root, "transport", "maxEdgeCost", config.transport.maxEdgeCost);
        readTomlValue(root, "transport", "landCostMult", config.transport.landCostMult);
        readTomlValue(root, "transport", "seaCostMult", config.transport.seaCostMult);
        readTomlValue(root, "transport", "borderFriction", config.transport.borderFriction);
        readTomlValue(root, "transport", "warRiskMult", config.transport.warRiskMult);
        readTomlValue(root, "transport", "migrationM0", config.transport.migrationM0);
        readTomlValue(root, "transport", "migrationDistDecay", config.transport.migrationDistDecay);
        readTomlValue(root, "transport", "gravityKappa", config.transport.gravityKappa);
        readTomlValue(root, "transport", "gravityAlpha", config.transport.gravityAlpha);
        readTomlValue(root, "transport", "gravityBeta", config.transport.gravityBeta);
        readTomlValue(root, "transport", "gravityGamma", config.transport.gravityGamma);
        readTomlValue(root, "transport", "specialistEta", config.transport.specialistEta);
        readTomlValue(root, "transport", "specialistLambda", config.transport.specialistLambda);
        readTomlValue(root, "transport", "tradeHintBlend", config.transport.tradeHintBlend);

        readTomlValue(root, "transport_regimes", "enabled", config.transportRegimes.enabled);
        readTomlValue(root, "transport_regimes", "landMinCostMult", config.transportRegimes.landMinCostMult);
        readTomlValue(root, "transport_regimes", "seaMinCostMult", config.transportRegimes.seaMinCostMult);
        readTomlValue(root, "transport_regimes", "capacityMaxMult", config.transportRegimes.capacityMaxMult);
        readTomlValue(root, "transport_regimes", "reliabilityMaxAdd", config.transportRegimes.reliabilityMaxAdd);
        readTomlValue(root, "transport_regimes", "roadInfraWeight", config.transportRegimes.roadInfraWeight);
        readTomlValue(root, "transport_regimes", "portInfraWeight", config.transportRegimes.portInfraWeight);
        readTomlValue(root, "transport_regimes", "logisticsWeight", config.transportRegimes.logisticsWeight);
        readTomlValue(root, "transport_regimes", "marketWeight", config.transportRegimes.marketWeight);
        readTomlValue(root, "transport_regimes", "packThreshold", config.transportRegimes.packThreshold);
        readTomlValue(root, "transport_regimes", "tractionThreshold", config.transportRegimes.tractionThreshold);
        readTomlValue(root, "transport_regimes", "wheelThreshold", config.transportRegimes.wheelThreshold);
        readTomlValue(root, "transport_regimes", "maritimeThreshold", config.transportRegimes.maritimeThreshold);

        readTomlValue(root, "exploration", "enabled", config.exploration.enabled);
        readTomlValue(root, "exploration", "baseMissionsPerCountry", config.exploration.baseMissionsPerCountry);
        readTomlValue(root, "exploration", "missionsPerPopMillion", config.exploration.missionsPerPopMillion);
        readTomlValue(root, "exploration", "uncertaintyDecayLocal", config.exploration.uncertaintyDecayLocal);
        readTomlValue(root, "exploration", "uncertaintyDecayNeighbor", config.exploration.uncertaintyDecayNeighbor);
        readTomlValue(root, "exploration", "explorationGain", config.exploration.explorationGain);
        readTomlValue(root, "exploration", "missionCostCeiling", config.exploration.missionCostCeiling);
        readTomlValue(root, "exploration", "edgeReliabilityBoost", config.exploration.edgeReliabilityBoost);
        readTomlValue(root, "exploration", "edgeCostReduction", config.exploration.edgeCostReduction);

        readTomlValue(root, "research_gpu", "enabled", config.researchGpu.enabled);
        readTomlValue(root, "research_gpu", "startupDiagnostics", config.researchGpu.startupDiagnostics);
        readTomlValue(root, "research_gpu", "accelerateTransport", config.researchGpu.accelerateTransport);
        readTomlValue(root, "research_gpu", "accelerateFlows", config.researchGpu.accelerateFlows);
        readTomlValue(root, "research_gpu", "settlementDisease", config.researchGpu.settlementDisease);
        readTomlValue(root, "research_gpu", "climateFertility", config.researchGpu.climateFertility);
        readTomlValue(root, "research_gpu", "adoptionKernel", config.researchGpu.adoptionKernel);
        readTomlValue(root, "research_gpu", "warfareLogistics", config.researchGpu.warfareLogistics);
        readTomlValue(root, "research_gpu", "joinStayUtility", config.researchGpu.joinStayUtility);

        readTomlValue(root, "research_settlement", "pastoralMobility", config.researchSettlement.pastoralMobility);
        readTomlValue(root, "research_settlement", "householdsExtraction", config.researchSettlement.householdsExtraction);
        readTomlValue(root, "research_settlement", "polityChoiceAssignment", config.researchSettlement.polityChoiceAssignment);
        readTomlValue(root, "research_settlement", "polityControlGraph", config.researchSettlement.polityControlGraph);
        readTomlValue(root, "research_settlement", "campaignLogistics", config.researchSettlement.campaignLogistics);
        readTomlValue(root, "research_settlement", "irrigationLoop", config.researchSettlement.irrigationLoop);
        readTomlValue(root, "research_settlement", "transportPathRebuild", config.researchSettlement.transportPathRebuild);

        readTomlValue(root, "research_settlement", "pastoralMoveShare", config.researchSettlement.pastoralMoveShare);
        readTomlValue(root, "research_settlement", "pastoralRouteRadius", config.researchSettlement.pastoralRouteRadius);

        readTomlValue(root, "research_settlement", "extractionBase", config.researchSettlement.extractionBase);
        readTomlValue(root, "research_settlement", "extractionAdminWeight", config.researchSettlement.extractionAdminWeight);
        readTomlValue(root, "research_settlement", "extractionLegitimacyWeight", config.researchSettlement.extractionLegitimacyWeight);
        readTomlValue(root, "research_settlement", "extractionStorageInvestShare", config.researchSettlement.extractionStorageInvestShare);
        readTomlValue(root, "research_settlement", "extractionIrrigationInvestShare", config.researchSettlement.extractionIrrigationInvestShare);
        readTomlValue(root, "research_settlement", "extractionRoadInvestShare", config.researchSettlement.extractionRoadInvestShare);

        readTomlValue(root, "research_settlement", "irrigationDepreciation", config.researchSettlement.irrigationDepreciation);
        readTomlValue(root, "research_settlement", "irrigationWaterBoost", config.researchSettlement.irrigationWaterBoost);
        readTomlValue(root, "research_settlement", "irrigationFertilityShield", config.researchSettlement.irrigationFertilityShield);

        readTomlValue(root, "research_settlement", "polityTravelTauBase", config.researchSettlement.polityTravelTauBase);
        readTomlValue(root, "research_settlement", "polityTravelTauAdminWeight", config.researchSettlement.polityTravelTauAdminWeight);
        readTomlValue(root, "research_settlement", "polityTravelTauLogisticsWeight", config.researchSettlement.polityTravelTauLogisticsWeight);
        readTomlValue(root, "research_settlement", "polityTravelMaxCost", config.researchSettlement.polityTravelMaxCost);
        readTomlValue(root, "research_settlement", "polityTopCountryCandidates", config.researchSettlement.polityTopCountryCandidates);
        readTomlValue(root, "research_settlement", "polityControlWeight", config.researchSettlement.polityControlWeight);
        readTomlValue(root, "research_settlement", "polityStrengthWeight", config.researchSettlement.polityStrengthWeight);
        readTomlValue(root, "research_settlement", "polityDistancePenaltyWeight", config.researchSettlement.polityDistancePenaltyWeight);
        readTomlValue(root, "research_settlement", "polityDefectionShockWeight", config.researchSettlement.polityDefectionShockWeight);
        readTomlValue(root, "research_settlement", "politySwitchThreshold", config.researchSettlement.politySwitchThreshold);
        readTomlValue(root, "research_settlement", "politySwitchMaxNodeShare", config.researchSettlement.politySwitchMaxNodeShare);

        readTomlValue(root, "research_settlement", "campaignDemandBase", config.researchSettlement.campaignDemandBase);
        readTomlValue(root, "research_settlement", "campaignDemandWarScale", config.researchSettlement.campaignDemandWarScale);
        readTomlValue(root, "research_settlement", "campaignAttritionRate", config.researchSettlement.campaignAttritionRate);
        readTomlValue(root, "research_settlement", "campaignNodeShockScale", config.researchSettlement.campaignNodeShockScale);

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

        readTomlValue(root, "spawn", "enabled", config.spawn.enabled);
        readTomlValue(root, "spawn", "maskPath", config.spawn.maskPath);
        readTomlValue(root, "spawn", "colorTolerance", config.spawn.colorTolerance);
        if (const auto mode = root["spawn"]["dupMode"].value<std::string>()) {
            config.spawn.dupMode = parseDuplicateColorMode(*mode);
        }
        if (const toml::array* regions = root["spawn"]["regions"].as_array()) {
            config.spawn.regions.clear();
            config.spawn.regions.reserve(regions->size());
            for (const auto& node : *regions) {
                const toml::table* t = node.as_table();
                if (!t) continue;
                SimulationConfig::SpawnRegionConfig r{};
                if (const auto v = (*t)["key"].value<std::string>()) r.key = *v;
                if (const auto v = (*t)["name"].value<std::string>()) r.name = *v;
                if (const auto v = (*t)["r"].value<std::int64_t>()) r.r = static_cast<int>(*v);
                if (const auto v = (*t)["g"].value<std::int64_t>()) r.g = static_cast<int>(*v);
                if (const auto v = (*t)["b"].value<std::int64_t>()) r.b = static_cast<int>(*v);
                if (const auto v = (*t)["worldShare"].value<double>()) {
                    r.worldShare = *v;
                } else if (const auto vi = (*t)["worldShare"].value<std::int64_t>()) {
                    r.worldShare = static_cast<double>(*vi);
                }
                if (const auto v = (*t)["groupId"].value<std::int64_t>()) r.groupId = static_cast<int>(*v);
                if (const auto v = (*t)["anchorX"].value<double>()) r.anchorX = *v;
                if (const auto v = (*t)["anchorY"].value<double>()) r.anchorY = *v;
                if (r.key.empty()) continue;
                if (r.name.empty()) r.name = r.key;
                config.spawn.regions.push_back(std::move(r));
            }
        }
        if (config.spawn.regions.empty()) {
            config.spawn.regions = SimulationConfig::defaultSpawnRegions();
        }

        readTomlValue(root, "startTech", "enabled", config.startTech.enabled);
        readTomlValue(root, "startTech", "triggerYear", config.startTech.triggerYear);
        readTomlValue(root, "startTech", "requireExactYear", config.startTech.requireExactYear);
        readTomlValue(root, "startTech", "autoGrantPrereqs", config.startTech.autoGrantPrereqs);
        if (const toml::array* presets = root["startTech"]["presets"].as_array()) {
            config.startTech.presets.clear();
            config.startTech.presets.reserve(presets->size());
            for (const auto& node : *presets) {
                const toml::table* t = node.as_table();
                if (!t) continue;
                SimulationConfig::RegionalStartTechPreset preset{};
                if (const auto v = (*t)["regionKey"].value<std::string>()) {
                    preset.regionKey = *v;
                }
                if (const toml::array* techIds = (*t)["techIds"].as_array()) {
                    for (const auto& techNode : *techIds) {
                        if (const auto techId = techNode.value<std::int64_t>()) {
                            preset.techIds.push_back(static_cast<int>(*techId));
                        }
                    }
                }
                if (!preset.regionKey.empty()) {
                    config.startTech.presets.push_back(std::move(preset));
                }
            }
        }
        if (config.startTech.presets.empty()) {
            config.startTech.presets = SimulationConfig::defaultRegionalStartTechPresets();
        }

        if (config.world.population.mode == SimulationConfig::WorldPopulationConfig::Mode::Fixed) {
            if (config.world.population.fixedValue <= 0) {
                config.world.population.fixedValue = 19'000'000;
            }
        } else {
            if (config.world.population.minValue <= 0) {
                config.world.population.minValue = 12'000'000;
            }
            if (config.world.population.maxValue < config.world.population.minValue) {
                std::swap(config.world.population.maxValue, config.world.population.minValue);
            }
            if (config.world.population.maxValue < 12'000'000) {
                config.world.population.maxValue = 30'000'000;
            }
        }
        config.spawn.colorTolerance = std::clamp(config.spawn.colorTolerance, 0, 255);
        double shareSum = 0.0;
        for (const auto& r : config.spawn.regions) {
            if (r.worldShare > 0.0) shareSum += r.worldShare;
        }
        if (shareSum <= 0.0) {
            config.spawn.regions = SimulationConfig::defaultSpawnRegions();
            shareSum = 1.0;
        } else if (std::abs(shareSum - 1.0) > 1.0e-6) {
            for (auto& r : config.spawn.regions) {
                if (r.worldShare < 0.0) r.worldShare = 0.0;
                r.worldShare /= shareSum;
            }
        }

        config.settlements.initNodeMinPop = std::max(1.0, config.settlements.initNodeMinPop);
        config.settlements.maxNodesGlobal = std::max(64, config.settlements.maxNodesGlobal);
        config.settlements.maxNodesPerCountry = std::max(1, config.settlements.maxNodesPerCountry);
        config.settlements.splitPopThreshold = std::max(1000.0, config.settlements.splitPopThreshold);
        config.settlements.splitAlphaMin = std::clamp(config.settlements.splitAlphaMin, 0.05, 0.90);
        config.settlements.splitAlphaMax = std::clamp(config.settlements.splitAlphaMax, 0.05, 0.90);
        if (config.settlements.splitAlphaMax < config.settlements.splitAlphaMin) {
            std::swap(config.settlements.splitAlphaMax, config.settlements.splitAlphaMin);
        }
        config.settlements.splitCooldownYears = std::max(1, config.settlements.splitCooldownYears);
        config.settlements.splitMinSpacingFields = std::max(1, config.settlements.splitMinSpacingFields);
        if (config.settlements.growthRMax < config.settlements.growthRMin) {
            std::swap(config.settlements.growthRMax, config.settlements.growthRMin);
        }
        config.settlements.cal0 = std::max(1.0e-9, config.settlements.cal0);
        config.settlements.calSlope = std::max(1.0e-9, config.settlements.calSlope);
        config.settlements.kBasePerFoodUnit = std::max(1.0, config.settlements.kBasePerFoodUnit);
        config.settlements.transportRebuildIntervalYears = std::max(1, config.settlements.transportRebuildIntervalYears);

        config.densityInit.weightPopulation = std::max(0.0, config.densityInit.weightPopulation);
        config.densityInit.weightFood = std::max(0.0, config.densityInit.weightFood);
        config.densityInit.weightCorridor = std::max(0.0, config.densityInit.weightCorridor);
        config.densityInit.weightClimate = std::max(0.0, config.densityInit.weightClimate);
        config.densityInit.scoreNoise = std::clamp(config.densityInit.scoreNoise, 0.0, 0.5);
        config.densityInit.minPopFactor = std::clamp(config.densityInit.minPopFactor, 0.0, 2.0);
        config.densityInit.priorWeight = std::max(0.0, config.densityInit.priorWeight);

        config.paleoClimate.tempInfluence = std::max(0.0, config.paleoClimate.tempInfluence);
        config.paleoClimate.precipInfluence = std::max(0.0, config.paleoClimate.precipInfluence);
        config.paleoClimate.monsoonVarianceScale = std::max(0.0, config.paleoClimate.monsoonVarianceScale);
        config.paleoClimate.droughtClusterScale = std::max(0.0, config.paleoClimate.droughtClusterScale);
        config.paleoClimate.coolingPulseScale = std::max(0.0, config.paleoClimate.coolingPulseScale);

        config.subsistence.mixAdaptRate = std::clamp(config.subsistence.mixAdaptRate, 0.0, 1.0);
        config.subsistence.riskPenaltyWeight = std::max(0.0, config.subsistence.riskPenaltyWeight);
        config.subsistence.craftFromMarketWeight = std::max(0.0, config.subsistence.craftFromMarketWeight);

        config.packages.adoptionBase = std::clamp(config.packages.adoptionBase, 0.0, 1.0);
        config.packages.diffusionWeight = std::max(0.0, config.packages.diffusionWeight);
        config.packages.environmentWeight = std::max(0.0, config.packages.environmentWeight);

        config.packageDiffusion.demicWeight = std::max(0.0, config.packageDiffusion.demicWeight);
        config.packageDiffusion.culturalWeight = std::max(0.0, config.packageDiffusion.culturalWeight);
        config.packageDiffusion.coerciveWeight = std::max(0.0, config.packageDiffusion.coerciveWeight);
        config.packageDiffusion.prestigeWeight = std::clamp(config.packageDiffusion.prestigeWeight, 0.0, 1.0);
        config.packageDiffusion.conformistThreshold = std::clamp(config.packageDiffusion.conformistThreshold, 0.0, 1.0);
        config.packageDiffusion.conformistStrength = std::max(0.0, config.packageDiffusion.conformistStrength);
        config.packageDiffusion.coerciveExtractionWeight = std::max(0.0, config.packageDiffusion.coerciveExtractionWeight);
        config.packageDiffusion.coercivePolitySwitchWeight = std::max(0.0, config.packageDiffusion.coercivePolitySwitchWeight);

        config.knowledgeDynamics.maintenanceSpecialistThreshold = std::max(1.0e-4, config.knowledgeDynamics.maintenanceSpecialistThreshold);
        config.knowledgeDynamics.maintenanceMarketThreshold = std::max(1.0e-4, config.knowledgeDynamics.maintenanceMarketThreshold);
        config.knowledgeDynamics.lossRate = std::clamp(config.knowledgeDynamics.lossRate, 0.0, 1.0);
        config.knowledgeDynamics.collapseLossScale = std::max(0.0, config.knowledgeDynamics.collapseLossScale);
        config.knowledgeDynamics.recoveryRate = std::clamp(config.knowledgeDynamics.recoveryRate, 0.0, 1.0);
        config.knowledgeDynamics.relearnFromNeighbors = std::clamp(config.knowledgeDynamics.relearnFromNeighbors, 0.0, 1.0);

        config.salinity.irrigationAccumulation = std::max(0.0, config.salinity.irrigationAccumulation);
        config.salinity.aridityAccumulation = std::max(0.0, config.salinity.aridityAccumulation);
        config.salinity.drainageRecovery = std::max(0.0, config.salinity.drainageRecovery);
        config.salinity.rainfallDrainageWeight = std::max(0.0, config.salinity.rainfallDrainageWeight);
        config.salinity.corridorDrainageWeight = std::max(0.0, config.salinity.corridorDrainageWeight);
        config.salinity.infraDrainageWeight = std::max(0.0, config.salinity.infraDrainageWeight);
        config.salinity.yieldPenaltyMax = std::clamp(config.salinity.yieldPenaltyMax, 0.0, 0.95);

        config.transport.kNearest = std::max(1, config.transport.kNearest);
        config.transport.maxEdgeCost = std::max(1.0, config.transport.maxEdgeCost);
        config.transport.landCostMult = std::max(0.01, config.transport.landCostMult);
        config.transport.seaCostMult = std::max(0.01, config.transport.seaCostMult);
        config.transport.borderFriction = std::max(0.01, config.transport.borderFriction);
        config.transport.warRiskMult = std::max(0.01, config.transport.warRiskMult);
        config.transport.migrationM0 = std::max(0.0, config.transport.migrationM0);
        config.transport.migrationDistDecay = std::max(0.0, config.transport.migrationDistDecay);
        config.transport.gravityKappa = std::max(0.0, config.transport.gravityKappa);
        config.transport.gravityAlpha = std::max(0.0, config.transport.gravityAlpha);
        config.transport.gravityBeta = std::max(0.0, config.transport.gravityBeta);
        config.transport.gravityGamma = std::max(0.0, config.transport.gravityGamma);
        config.transport.specialistEta = std::max(0.0, config.transport.specialistEta);
        config.transport.specialistLambda = std::max(0.0, config.transport.specialistLambda);
        config.transport.tradeHintBlend = std::clamp(config.transport.tradeHintBlend, 0.0, 1.0);

        config.transportRegimes.landMinCostMult = std::clamp(config.transportRegimes.landMinCostMult, 0.05, 1.0);
        config.transportRegimes.seaMinCostMult = std::clamp(config.transportRegimes.seaMinCostMult, 0.05, 1.0);
        config.transportRegimes.capacityMaxMult = std::max(1.0, config.transportRegimes.capacityMaxMult);
        config.transportRegimes.reliabilityMaxAdd = std::clamp(config.transportRegimes.reliabilityMaxAdd, 0.0, 1.0);
        config.transportRegimes.roadInfraWeight = std::max(0.0, config.transportRegimes.roadInfraWeight);
        config.transportRegimes.portInfraWeight = std::max(0.0, config.transportRegimes.portInfraWeight);
        config.transportRegimes.logisticsWeight = std::max(0.0, config.transportRegimes.logisticsWeight);
        config.transportRegimes.marketWeight = std::max(0.0, config.transportRegimes.marketWeight);
        config.transportRegimes.packThreshold = std::clamp(config.transportRegimes.packThreshold, 0.0, 1.0);
        config.transportRegimes.tractionThreshold = std::clamp(config.transportRegimes.tractionThreshold, 0.0, 1.0);
        config.transportRegimes.wheelThreshold = std::clamp(config.transportRegimes.wheelThreshold, 0.0, 1.0);
        config.transportRegimes.maritimeThreshold = std::clamp(config.transportRegimes.maritimeThreshold, 0.0, 1.0);

        config.exploration.baseMissionsPerCountry = std::clamp(config.exploration.baseMissionsPerCountry, 0, 12);
        config.exploration.missionsPerPopMillion = std::max(0.0, config.exploration.missionsPerPopMillion);
        config.exploration.uncertaintyDecayLocal = std::clamp(config.exploration.uncertaintyDecayLocal, 0.0, 1.0);
        config.exploration.uncertaintyDecayNeighbor = std::clamp(config.exploration.uncertaintyDecayNeighbor, 0.0, 1.0);
        config.exploration.explorationGain = std::clamp(config.exploration.explorationGain, 0.0, 1.0);
        config.exploration.missionCostCeiling = std::max(1.0, config.exploration.missionCostCeiling);
        config.exploration.edgeReliabilityBoost = std::clamp(config.exploration.edgeReliabilityBoost, 0.0, 1.0);
        config.exploration.edgeCostReduction = std::clamp(config.exploration.edgeCostReduction, 0.0, 0.9);

        config.researchSettlement.polityTravelTauBase = std::max(0.5, config.researchSettlement.polityTravelTauBase);
        config.researchSettlement.polityTravelTauAdminWeight = std::max(0.0, config.researchSettlement.polityTravelTauAdminWeight);
        config.researchSettlement.polityTravelTauLogisticsWeight = std::max(0.0, config.researchSettlement.polityTravelTauLogisticsWeight);
        config.researchSettlement.polityTravelMaxCost = std::max(1.0, config.researchSettlement.polityTravelMaxCost);
        config.researchSettlement.polityTopCountryCandidates = std::clamp(config.researchSettlement.polityTopCountryCandidates, 1, 8);
        config.researchSettlement.polityControlWeight = std::max(0.0, config.researchSettlement.polityControlWeight);
        config.researchSettlement.polityStrengthWeight = std::max(0.0, config.researchSettlement.polityStrengthWeight);
        config.researchSettlement.polityDistancePenaltyWeight = std::max(0.0, config.researchSettlement.polityDistancePenaltyWeight);
        config.researchSettlement.polityDefectionShockWeight = std::max(0.0, config.researchSettlement.polityDefectionShockWeight);

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

std::vector<SimulationConfig::SpawnRegionConfig> SimulationConfig::defaultSpawnRegions() {
    return {
        {"south_asia", "South Asia", 151, 255, 135, 0.145, 1, -1.0, -1.0},
        {"east_asia", "East Asia", 255, 145, 145, 0.145, 1, -1.0, -1.0},
        {"west_asia", "West Asia", 63, 210, 255, 0.110, 1, -1.0, -1.0},
        {"se_asia", "Southeast Asia", 253, 255, 173, 0.060, 1, -1.0, -1.0},
        {"cn_asia", "Central and North Asia", 187, 135, 255, 0.033, 1, -1.0, -1.0},
        {"nile_ne_africa", "Nile Valley and Northeast Africa", 255, 135, 229, 0.020, 2, -1.0, -1.0},
        {"north_africa", "North Africa", 173, 255, 255, 0.013, 2, -1.0, -1.0},
        {"west_africa", "West Africa", 7, 255, 127, 0.016, 2, -1.0, -1.0},
        {"east_africa", "East Africa", 33, 195, 255, 0.017, 2, -1.0, -1.0},
        {"cs_africa", "Central and Southern Africa", 255, 53, 241, 0.010, 2, -1.0, -1.0},
        {"se_europe", "Southeast Europe", 246, 255, 170, 0.020, 3, -1.0, -1.0},
        {"med_europe", "Mediterranean Europe", 255, 106, 0, 0.018, 3, 0.52, 0.35},
        {"central_europe", "Central Europe", 81, 124, 81, 0.020, 3, -1.0, -1.0},
        {"wnw_europe", "Western and Northwestern Europe", 130, 255, 150, 0.018, 3, -1.0, -1.0},
        {"north_europe", "Northern Europe", 255, 208, 147, 0.008, 3, -1.0, -1.0},
        {"mesoamerica", "Mesoamerica", 255, 130, 165, 0.100, 4, -1.0, -1.0},
        {"andes", "Andes", 60, 32, 124, 0.065, 4, -1.0, -1.0},
        {"e_na", "Eastern North America", 124, 39, 72, 0.070, 4, -1.0, -1.0},
        {"w_na", "Western North America", 255, 0, 220, 0.078, 4, -1.0, -1.0},
        {"caribbean", "Caribbean", 109, 121, 255, 0.013, 4, -1.0, -1.0},
        {"oceania", "Oceania", 255, 106, 0, 0.021, 5, 0.82, 0.76},
    };
}

std::vector<SimulationConfig::RegionalStartTechPreset> SimulationConfig::defaultRegionalStartTechPresets() {
    return {
        {"west_asia", {20, 1, 2, 3, 4, 6, 10, 115, 119}},
        {"nile_ne_africa", {20, 1, 2, 3, 6, 10, 115}},
        {"south_asia", {20, 1, 2, 3, 4, 6, 115}},
        {"east_asia", {20, 1, 2, 3, 4, 6, 115}},
        {"se_asia", {20, 1, 2, 3, 5, 6}},
        {"cn_asia", {2, 3, 4, 6}},
        {"se_europe", {20, 1, 2, 3, 4, 6, 115, 119}},
        {"med_europe", {20, 1, 2, 3, 4, 5, 6, 115}},
        {"central_europe", {20, 1, 2, 3, 4, 6}},
        {"wnw_europe", {20, 1, 3, 4, 6}},
        {"north_europe", {3, 4, 6}},
        {"north_africa", {20, 1, 2, 3, 4, 6, 115}},
        {"west_africa", {20, 1, 3, 4, 6}},
        {"east_africa", {20, 1, 3, 4, 6}},
        {"cs_africa", {1, 3, 4, 6}},
        {"mesoamerica", {20, 1, 3, 6}},
        {"andes", {20, 1, 3, 4, 6}},
        {"e_na", {3, 4, 6}},
        {"w_na", {3, 4, 6}},
        {"caribbean", {3, 5, 6}},
        {"oceania", {3, 5, 6}},
    };
}
