#pragma once

#include <cstdint>
#include <random>
#include <string>
#include <vector>

struct SimulationConfig {
    struct SpawnRegionConfig {
        std::string key;
        std::string name;
        int r = 0;
        int g = 0;
        int b = 0;
        double worldShare = 0.0;
        int groupId = 0;
        double anchorX = -1.0;
        double anchorY = -1.0;
    };

    struct RegionalStartTechPreset {
        std::string regionKey;
        std::vector<int> techIds;
    };

    struct WorldPopulationConfig {
        enum class Mode {
            Range,
            Fixed
        };
        Mode mode = Mode::Range;
        std::int64_t fixedValue = 0;
        std::int64_t minValue = 12000000;
        std::int64_t maxValue = 30000000;
    };

    struct SpawnConfig {
        enum class DuplicateColorMode {
            SplitConnectedComponents,
            ErrorOnDuplicate
        };
        bool enabled = true;
        std::string maskPath = "assets/images/spawn.png";
        std::vector<SpawnRegionConfig> regions;
        int colorTolerance = 15;
        DuplicateColorMode dupMode = DuplicateColorMode::SplitConnectedComponents;
    };

    struct StartTechConfig {
        bool enabled = true;
        int triggerYear = -5000;
        bool requireExactYear = true;
        bool autoGrantPrereqs = true;
        std::vector<RegionalStartTechPreset> presets;
    };

    struct World {
        int yearsPerTick = 1;
        int startYear = -5000;
        int endYear = 2025;
        std::string rngSeedMode = "provided";
        bool deterministicMode = true;
        // Expert override for Phase-7 overseas deterministic fallback: auto|on|off.
        std::string deterministicOverseasFallback = "auto";
        WorldPopulationConfig population{};
    } world{};

    struct Food {
        double baseForaging = 28.0;
        double baseFarming = 52.0;
        double climateSensitivity = 0.70;
        double riverlandFoodFloor = 150.0;
        double coastalBonus = 1.35;
        double spoilageBase = 0.12;
        double storageBase = 0.55;
        double clayMin = 0.8;
        double clayMax = 3.0;
        double clayHotspotChance = 0.08;
        double foragingNoAgriShare = 0.90;
        double foragingWithAgriShare = 0.35;
        double farmingWithAgriShare = 0.85;
    } food{};

    struct Resources {
        double oreWeightIron = 1.00;
        double oreWeightCopper = 0.85;
        double oreWeightTin = 1.20;
        double energyBiomassBase = 0.60;
        double energyCoalWeight = 1.65;
        double constructionClayWeight = 1.00;
        double constructionStoneBase = 0.55;
        double oreNormalization = 140.0;
        double energyNormalization = 120.0;
        double constructionNormalization = 110.0;
        double oreDepletionRate = 0.035;
        double coalDepletionRate = 0.030;
    } resources{};

    struct Migration {
        double famineShockThreshold = 0.22;
        double epidemicShockThreshold = 0.16;
        double warShockThreshold = 0.24;
        double famineShockMultiplier = 1.40;
        double epidemicShockMultiplier = 1.10;
        double warShockMultiplier = 1.25;
        double corridorCoastBonus = 0.25;
        double corridorRiverlandBonus = 0.35;
        double corridorSteppeBonus = 0.15;
        double corridorMountainPenalty = 0.45;
        double corridorDesertPenalty = 0.25;
        double refugeeHalfLifeYears = 10.0;
        double culturalPreference = 0.20;
        double frontierClaimPopulationThreshold = 350.0;
        double frontierClaimControlThreshold = 0.30;
    } migration{};

    struct Disease {
        double initialInfectedShare = 0.0010;
        double initialRecoveredShare = 0.0;
        double tradeImportWeight = 0.12;
        double endemicBase = 0.0012;
        double endemicUrbanWeight = 0.70;
        double endemicHumidityWeight = 0.55;
        double endemicInstitutionMitigation = 0.55;
        double zoonoticBase = 0.0010;
        double zoonoticForagingWeight = 0.80;
        double zoonoticFarmingWeight = 0.25;
        double spilloverShockChance = 0.015;
        double spilloverShockMin = 0.002;
        double spilloverShockMax = 0.012;
        double warAmplifier = 0.20;
        double famineAmplifier = 0.30;
    } disease{};

    struct War {
        double supplyBase = 0.25;
        double supplyLogisticsWeight = 0.35;
        double supplyMarketWeight = 0.20;
        double supplyControlWeight = 0.20;
        double supplyEnergyWeight = 0.10;
        double supplyFoodStockWeight = 0.15;
        double overSupplyAttrition = 0.06;
        double terrainDefenseWeight = 0.35;
        double exhaustionRise = 0.08;
        double exhaustionPeaceThreshold = 0.75;
        double objectiveRaidWeight = 0.30;
        double objectiveBorderWeight = 0.20;
        double objectiveTributeWeight = 0.15;
        double objectiveVassalWeight = 0.12;
        double objectiveRegimeWeight = 0.10;
        double objectiveAnnihilationWeight = 0.08;
        int cooldownMinYears = 6;
        int cooldownMaxYears = 40;
        double peaceReparationsWeight = 0.20;
        double peaceTributeWeight = 0.25;
        double peaceReconstructionDrag = 0.15;
        double earlyAnnihilationBias = 0.15;
        double highInstitutionAnnihilationDamp = 0.65;
        int maxConcurrentWars = 3;
        double leaderAmbitionWarWeight = 0.45;
        double weakStatePredationWeight = 0.55;
        double opportunisticWarThreshold = 0.60;
    } war{};

    struct Polity {
        int regionCountMin = 3;
        int regionCountMax = 8;
        int successionIntervalMin = 18;
        int successionIntervalMax = 45;
        double eliteDefectionSensitivity = 0.65;
        double farRegionPenalty = 0.40;
        double yearlyWarStabilityHit = 0.030;
        double yearlyPlagueStabilityHit = 0.048;
        double yearlyStagnationStabilityHit = 0.010;
        double peaceRecoveryLowGrowth = 0.006;
        double peaceRecoveryHighGrowth = 0.015;
        double resilienceRecoveryStrength = 0.012;
        double demogShortageStabilityHit = 0.018;
        double demogDiseaseStabilityHit = 0.012;
        double demogShortageLegitimacyHit = 0.014;
        double demogDiseaseLegitimacyHit = 0.009;
        double legitimacyRecoveryStrength = 0.010;
        double lowCapabilityFiscalThreshold = 0.50;
        double lowCapabilityNearBalanceCap = 1.02;
        double lowCapabilityBorrowingScale = 0.08;
        double lowCapabilityReserveMonthsTarget = 1.50;
        double debtMarketAccessFloor = 0.30;
        double debtMarketAccessSlope = 0.35;
        double revenueTrendFastAlpha = 0.55;
        double revenueTrendSlowAlpha = 0.18;
        double revenueTrendSpendSensitivity = 0.40;
        double debtServiceAusterityThreshold = 0.30;
        double debtServiceAusterityStrength = 0.85;
        double subsistenceAdminFloorShare = 0.62;
        double earlyLegitimacyProvisioningWeight = 0.65;
        double earlyLegitimacyFiscalWeight = 0.35;
        double stateTurnoverBaseChance = 0.0018;
        double stateTurnoverStressWeight = 0.60;
        double stateTurnoverAgeWeight = 0.35;
        double successionCrisisSplitWeight = 0.50;
        double institutionalContinuityShield = 0.55;
        int stateTurnoverMinAgeYears = 160;
    } polity{};

    struct Tech {
        double capabilityThresholdScale = 1.0;
        double diffusionBase = 0.010;
        double culturalFrictionStrength = 1.10;
        double resourceReqEnergy = 0.40;
        double resourceReqOre = 0.35;
        double resourceReqConstruction = 0.25;
        double adoptionThreshold = 0.65;
        double forgetPracticeThreshold = 0.15;
        double discoveryBase = 0.020;
        double discoveryDifficultyScale = 0.90;
        int maxDiscoveriesPerYear = 2;
        double discoverySeedAdoption = 0.02;
        double knownDiffusionBase = 0.020;
        int knownDiffusionTopK = 6;
        double adoptionSeedFromNeighbors = 0.08;
        double adoptionBaseSpeed = 0.08;
        double adoptionDecayBase = 0.05;
        double collapseDecayMultiplier = 1.0;
        double prereqAdoptionFraction = 0.70;
        int rareForgetYears = 220;
        double rareForgetChance = 0.0015;
        double innovationVolatility = 0.30;
        double leadershipInnovationWeight = 0.35;
        double institutionalInertiaPenalty = 0.28;
        int europeAdvantageStartYear = 1100;
        int europeAdvantagePeakYear = 1750;
        int europeAdvantageFadeYear = 1980;
        double europeInnovationBoost = 0.22;
        double europeAdoptionBoost = 0.16;
        double europeReadinessThreshold = 0.42;
        double trajectoryVarianceStrength = 0.65;
        int trajectoryCycleYears = 90;
        double trajectoryCycleAmplitude = 0.28;
    } tech{};

    struct Economy {
        double foodLaborElasticity = 0.95;
        double goodsLaborElasticity = 0.70;
        double servicesLaborElasticity = 0.78;
        double energyIntensity = 0.80;
        double oreIntensity = 0.90;
        double goodsToMilitary = 0.55;
        double servicesScaling = 1.00;
        double tradeResourceMismatchDemandBoost = 0.55;
        double tradeScarcityCapacityBoost = 0.65;
        double tradeMaxPricePremium = 1.30;
        double tradeIntensityScale = 5.0;
        double tradeIntensityValueNormBase = 2000.0;
        double tradeIntensityMemory = 0.35;
        bool useGPU = true;
    } economy{};

    struct Scoring {
        std::vector<int> checkpointsYears = {-5000, -3000, -1000, 0, 1000, 1500, 2025};
        double weightFoodSecurityStability = 1.0;
        double weightInnovationUrbanization = 1.0;
        double weightEmpireLogisticsConstraint = 1.0;
        double weightDiseaseTransition = 1.0;
        double weightTradeResourceInequality = 1.0;
        double weightVariancePenalty = 1.0;
        double weightBrittlenessPenalty = 1.0;
    } scoring{};

    SpawnConfig spawn{};
    StartTechConfig startTech{};

    static std::vector<SpawnRegionConfig> defaultSpawnRegions();
    static std::vector<RegionalStartTechPreset> defaultRegionalStartTechPresets();
};

struct SimulationContext {
    std::uint64_t worldSeed = 0;
    std::mt19937_64 worldRng;
    SimulationConfig config;
    std::string configPath;
    std::string configHash;

    explicit SimulationContext(std::uint64_t seed, const std::string& runtimeConfigPath = "data/sim_config.toml");

    double rand01();
    int randInt(int a, int b); // inclusive
    double randNormal(double mean = 0.0, double stddev = 1.0);

    bool loadConfig(const std::string& path, std::string* errorMessage = nullptr);
    static std::string hashFileFNV1a(const std::string& path);

    std::uint64_t seedForCountry(int countryIndex) const;
    std::mt19937_64 makeRng(std::uint64_t salt) const;

    static std::uint64_t mix64(std::uint64_t x);
    static double u01FromU64(std::uint64_t x);
};
