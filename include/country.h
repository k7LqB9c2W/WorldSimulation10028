// country.h

#pragma once

#include <SFML/Graphics.hpp>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <array>
#include <mutex>
#include <random>
#include <cstdint>
#include <string>
#include <algorithm>
#include <limits>
#include "resource.h"
#include "news.h"

// Forward declaration of Map
class Map;
class CultureManager;
class TechnologyManager;
struct SimulationConfig;

namespace std {
    template <>
    struct hash<sf::Vector2i> {
        size_t operator()(const sf::Vector2i& v) const {
            return (std::hash<int>()(v.x) ^ (std::hash<int>()(v.y) << 1));
        }
    };
}

class City {
public:
    City(const sf::Vector2i& location) : m_location(location) {}

    sf::Vector2i getLocation() const { return m_location; }
    float getPopulation() const { return m_population; }
    void setPopulation(float pop) { m_population = std::max(0.0f, pop); }
    float getAdminContribution() const { return m_adminContribution; }
    void setAdminContribution(float v) { m_adminContribution = std::max(0.0f, v); }
    bool isMajorCity() const { return m_isMajorCity; }
    void setMajorCity(bool isMajor) { m_isMajorCity = isMajor; }

private:
    sf::Vector2i m_location;
    float m_population = 0.0f;
    float m_adminContribution = 0.0f;
    bool m_isMajorCity = false; // True if population reached 1 million (gold square)
};

class Country {
public:
    // Add the enum for country types
    enum class Type {
        Warmonger,
        Pacifist,
        Trader
    };
    
    enum class Ideology {
        Tribal,        // Starting ideology
        Chiefdom,      // Early organization
        Kingdom,       // Monarchical rule
        Empire,        // Large territorial state
        Republic,      // Republican government
        Democracy,     // Democratic government
        Dictatorship,  // Authoritarian rule
        Federation,    // Federal system
        Theocracy,     // Religious rule
        CityState      // Independent city-state
    };

    enum class WarGoal {
        Raid,
        BorderShift,
        Tribute,
        Vassalization,
        RegimeChange,
        Annihilation
    };
    // Getter for m_nextWarCheckYear
    int getNextWarCheckYear() const;

    // Setter for m_nextWarCheckYear
    void setNextWarCheckYear(int year);

    Country(int countryIndex,
            const sf::Color& color,
            const sf::Vector2i& startCell,
            long long initialPopulation,
            double growthRate,
            const std::string& name,
            Type type,
            std::uint64_t rngSeed,
            int foundingYear = -20000);
    void update(const std::vector<std::vector<bool>>& isLandGrid, std::vector<std::vector<int>>& countryGrid, std::mutex& gridMutex, int gridCellSize, int regionSize, std::unordered_set<int>& dirtyRegions, int currentYear, const std::vector<std::vector<std::unordered_map<Resource::Type, double>>>& resourceGrid, News& news, bool plagueActive, long long& plagueDeaths, Map& map, const class TechnologyManager& technologyManager, std::vector<Country>& allCountries);
    long long getPopulation() const;
    sf::Color getColor() const;
    int getCountryIndex() const;
    void foundCity(const sf::Vector2i& location, News& news);
    const std::vector<City>& getCities() const;
    std::vector<City>& getCitiesMutable();
    double getTotalCityPopulation() const { return m_totalCityPopulation; }
    void setTotalCityPopulation(double v) { m_totalCityPopulation = std::max(0.0, v); }
    double getGold() const;
    void addGold(double amount);
    void subtractGold(double amount);
    void setGold(double amount);

    // Continuous development stocks (rules-not-knobs city/institution systems).
    double getSpecialistPopulation() const { return m_specialistPopulation; } // people (implied specialists)
    void setSpecialistPopulation(double v) { m_specialistPopulation = std::max(0.0, v); }
    double getKnowledgeInfra() const { return m_knowledgeInfra; } // unitless stock
    void setKnowledgeInfra(double v) { m_knowledgeInfra = std::max(0.0, v); }

    // City-founding persistence memory (field-grid coordinates + streak).
    struct CityCandidate {
        int fx = -1;
        int fy = -1;
        int streak = 0;
    };
    const CityCandidate& getCityCandidate() const { return m_cityCandidate; }
    CityCandidate& getCityCandidateMutable() { return m_cityCandidate; }
    void resetCityCandidate() { m_cityCandidate = CityCandidate{}; }

    // Economy (GPU econ grid aggregation)
    double getWealth() const { return m_wealth; }
    double getGDP() const { return m_gdp; }
    double getExports() const { return m_exports; }
    void setWealth(double v) { m_wealth = v; }
    void setGDP(double v) { m_gdp = v; }
    void setExports(double v) { m_exports = v; }

    // Phase 4: lightweight macro economy state (CPU authoritative).
    struct MacroEconomyState {
        bool initialized = false;
        double foodStock = 0.0;
        double foodStockCap = 0.0;
        double spoilageRate = 0.12; // annual
        double nonFoodStock = 0.0;
        double capitalStock = 0.0;
        double infraStock = 0.0;
        double militarySupplyStock = 0.0;
        double servicesStock = 0.0;
        double cumulativeOreExtraction = 0.0;
        double cumulativeCoalExtraction = 0.0;
        double refugeePush = 0.0; // 0..1 transient migration shock pressure

        double lastFoodOutput = 0.0;
        double lastGoodsOutput = 0.0;
        double lastServicesOutput = 0.0;
        double lastMilitaryOutput = 0.0;
        double lastNonFoodOutput = 0.0;
        // Annualized food accounting terms used by realism anti-loophole metrics.
        double lastFoodAvailableBeforeLosses = 0.0;
        double lastFoodSpoilageLoss = 0.0;
        double lastFoodStorageLoss = 0.0;
        double lastFoodCons = 0.0;
        double lastNonFoodCons = 0.0;
        double lastInvestment = 0.0;
        double lastDepreciation = 0.0;
        double lastFoodShortage = 0.0;
        double lastNonFoodShortage = 0.0;
        double lastBirths = 0.0;
        double lastDeathsBase = 0.0;
        double lastDeathsFamine = 0.0;
        double lastDeathsEpi = 0.0;
        double lastAvgNutrition = 1.0;
        double lastLaborFoodShare = 0.0; // 0..1

        double foodSecurity = 1.0;   // 0..1
        double marketAccess = 0.2;   // 0..1
        double importsValue = 0.0;   // yearly
        double exportsValue = 0.0;   // yearly

        // Endogenous long-run development stocks (no player dials).
        double humanCapital = 0.02;        // 0..1
        double knowledgeStock = 0.01;      // 0..1
        double inequality = 0.20;          // 0..1
        double educationInvestment = 0.0;  // 0..1 budget share
        double rndInvestment = 0.0;        // 0..1 budget share
        double connectivityIndex = 0.0;    // 0..1 (trade + access)
        double institutionCapacity = 0.0;  // 0..1 (derived)

        // Scarcity prices / wage signals.
        double priceFood = 1.0;
        double priceGoods = 1.0;
        double priceServices = 1.0;
        double priceMilitarySupply = 1.0;
        double cpi = 1.0;
        double wage = 0.0;
        double realWage = 0.0;

        // State finance quality.
        double compliance = 0.5;      // 0..1
        double leakageRate = 0.15;    // 0..1
        double netRevenue = 0.0;      // yearly

        // Demography pressure signals.
        double nutritionBalance = 0.0;       // calories available - required
        double famineSeverity = 0.0;         // 0..1
        double migrationPressureOut = 0.0;   // 0..1
        double migrationAttractiveness = 0.0;// 0..1
        double diseaseBurden = 0.0;          // 0..1

        // Stability debug state (recomputed each simulated year).
        struct StabilityDebug {
            double dbg_pop_country_before_update = 0.0;
            double dbg_pop_grid_oldTotals = 0.0;
            double dbg_pop_mismatch_ratio = 1.0;

            double dbg_stab_start_year = 0.0;
            double dbg_stab_after_country_update = 0.0;
            double dbg_stab_after_budget = 0.0;
            double dbg_stab_after_demography = 0.0;

            double dbg_stab_delta_update = 0.0;
            double dbg_stab_delta_budget = 0.0;
            double dbg_stab_delta_demog = 0.0;
            double dbg_stab_delta_total = 0.0;

            double dbg_growthRatio_used = 0.0;
            int dbg_stagnationYears = 0;
            bool dbg_isAtWar = false;
            bool dbg_plagueAffected = false;

            double dbg_delta_war = 0.0;
            double dbg_delta_plague = 0.0;
            double dbg_delta_stagnation = 0.0;
            double dbg_delta_peace_recover = 0.0;

            double dbg_delta_debt_crisis = 0.0;
            double dbg_delta_control_decay = 0.0;
            double dbg_avgControl = 0.0;
            double dbg_gold = 0.0;
            double dbg_debt = 0.0;
            double dbg_incomeAnnual = 0.0;

            double dbg_shortageRatio = 0.0;
            double dbg_diseaseBurden = 0.0;
            double dbg_delta_demog_stress = 0.0;
        };
        StabilityDebug stabilityDebug{};

        // Legitimacy debug state (recomputed each simulated year).
        struct LegitimacyDebug {
            double dbg_legit_start = 0.0;
            double dbg_legit_after_economy = 0.0;
            double dbg_legit_after_budget = 0.0;
            double dbg_legit_after_demog = 0.0;
            double dbg_legit_after_culture = 0.0;
            double dbg_legit_end = 0.0;

            double dbg_legit_delta_economy = 0.0;
            double dbg_legit_delta_budget = 0.0;
            double dbg_legit_delta_demog = 0.0;
            double dbg_legit_delta_culture = 0.0;
            double dbg_legit_delta_events = 0.0;
            double dbg_legit_delta_total = 0.0;

            // Economy drift inputs/components.
            double dbg_legit_econ_instCap = 0.0;
            double dbg_legit_econ_wageGain = 0.0;
            double dbg_legit_econ_famineSeverity = 0.0;
            double dbg_legit_econ_ineq = 0.0;
            double dbg_legit_econ_disease = 0.0;
            double dbg_legit_econ_yearsD = 0.0;
            double dbg_legit_econ_up_inst = 0.0;
            double dbg_legit_econ_up_wage = 0.0;
            double dbg_legit_econ_down_famine = 0.0;
            double dbg_legit_econ_down_ineq = 0.0;
            double dbg_legit_econ_down_disease = 0.0;
            int dbg_legit_clamp_to_zero_economy = 0;

            // Budget inputs/checks.
            double dbg_legit_budget_incomeAnnual = 0.0;
            double dbg_legit_budget_incomeSafe = 1.0;
            double dbg_legit_budget_desiredBlock = 0.0;
            double dbg_legit_budget_actualSpending = 0.0;
            double dbg_legit_budget_shortfall = 0.0;
            double dbg_legit_budget_shortfallStress = 0.0;
            double dbg_legit_budget_debtStart = 0.0;
            double dbg_legit_budget_debtEnd = 0.0;
            double dbg_legit_budget_debtToIncome = 0.0;
            double dbg_legit_budget_debtToIncomeRaw = 0.0;
            double dbg_legit_budget_interestRate = 0.0;
            double dbg_legit_budget_debtServiceAnnual = 0.0;
            double dbg_legit_budget_serviceToIncome = 0.0;
            double dbg_legit_budget_serviceToIncomeRaw = 0.0;
            double dbg_legit_budget_taxRate = 0.0;
            double dbg_legit_budget_taxRateTarget = 0.0;
            double dbg_legit_budget_taxRateBefore = 0.0;
            double dbg_legit_budget_taxRateAfter = 0.0;
            int dbg_legit_budget_taxRateSource = 0; // 1=economy target
            double dbg_legit_budget_avgControl = 0.0;
            double dbg_legit_budget_stability = 0.0;
            bool dbg_legit_budget_borrowingEnabled = false;
            double dbg_legit_budget_debtLimit = 0.0;
            bool dbg_legit_budget_war = false;
            bool dbg_legit_budget_plagueAffected = false;
            double dbg_legit_budget_debtStress = 0.0;
            double dbg_legit_budget_serviceStress = 0.0;
            bool dbg_legit_budget_ratioOver5 = false;

            // Budget legitimacy delta components.
            double dbg_legit_budget_shortfall_direct = 0.0;
            double dbg_legit_budget_burden_penalty = 0.0;
            double dbg_legit_budget_drift_stability = 0.0;
            double dbg_legit_budget_drift_tax = 0.0;
            double dbg_legit_budget_drift_control = 0.0;
            double dbg_legit_budget_drift_debt = 0.0;
            double dbg_legit_budget_drift_service = 0.0;
            double dbg_legit_budget_drift_shortfall = 0.0;
            double dbg_legit_budget_drift_plague = 0.0;
            double dbg_legit_budget_drift_war = 0.0;
            double dbg_legit_budget_drift_total = 0.0;
            int dbg_legit_clamp_to_zero_budget = 0;

            // Demography stress.
            double dbg_legit_demog_shortageRatio = 0.0;
            double dbg_legit_demog_diseaseBurden = 0.0;
            double dbg_legit_delta_demog_stress = 0.0;
            int dbg_legit_clamp_to_zero_demog = 0;

            // Political/culture counters.
            int dbg_legit_event_splits = 0;
            int dbg_legit_event_tag_replacements = 0;
            int dbg_legit_clamp_to_zero_events = 0;
        };
        LegitimacyDebug legitimacyDebug{};
    };

    const MacroEconomyState& getMacroEconomy() const { return m_macro; }
    MacroEconomyState& getMacroEconomyMutable() { return m_macro; }
    double getFoodSecurity() const { return m_macro.foodSecurity; }
    double getMarketAccess() const { return m_macro.marketAccess; }
    double getInstitutionCapacity() const { return m_macro.institutionCapacity; }
    double getInequality() const { return m_macro.inequality; }
    double getRealWage() const { return m_macro.realWage; }
    double getConnectivityIndex() const { return m_macro.connectivityIndex; }

    // Phase 4/5A integration: last computed taxable base / tax take (annualized).
    double getLastTaxBase() const { return m_lastTaxBase; }
    double getLastTaxTake() const { return m_lastTaxTake; }
    void setLastTaxStats(double baseAnnual, double takeAnnual) {
        m_lastTaxBase = std::max(0.0, baseAnnual);
        m_lastTaxTake = std::max(0.0, takeAnnual);
    }
    void applyBudgetFromEconomy(double taxBaseAnnual,
                                double taxTakeAnnual,
                                int dtYears,
                                int techCount,
                                bool plagueAffected,
                                const SimulationConfig& simCfg);

    // Phase 5A: knowledge domains (innovation + diffusion, no point hoarding).
    static constexpr int kDomains = 8;
    using KnowledgeVec = std::array<double, kDomains>;
    const KnowledgeVec& getKnowledge() const { return m_knowledge; }
    KnowledgeVec& getKnowledgeMutable() { return m_knowledge; }
    double getKnowledgeDomain(int domainId) const { return (domainId >= 0 && domainId < kDomains) ? m_knowledge[static_cast<size_t>(domainId)] : 0.0; }
    double getInnovationRate() const { return m_innovationRate; }
    void setInnovationRate(double v) { m_innovationRate = std::max(0.0, v); }
    void ensureTechStateSize(int techCount);
    bool knowsTechDense(int idx) const;
    float adoptionDense(int idx) const;
    void setKnownTechDense(int idx, bool known);
    void setAdoptionDense(int idx, float adoption);
    int lowAdoptionYearsDense(int idx) const;
    void setLowAdoptionYearsDense(int idx, int years);
    void clearTechStateDense();
    bool hasAdoptedTechId(const TechnologyManager& technologyManager, int techId, float threshold = 0.65f) const;

    // Phase 5B: cultural traits (0..1, slow-moving; no culture currency).
    static constexpr int kTraits = 7;
    using TraitVec = std::array<double, kTraits>;
    const TraitVec& getTraits() const { return m_traits; }
    TraitVec& getTraitsMutable() { return m_traits; }

    // Phase 7: exploration + colonization state (lightweight).
    struct ExplorationState {
        int lastColonizationYear = -999999;
        float explorationDrive = 0.0f;    // 0..1 (smoothed)
        float colonialOverstretch = 0.0f; // 0..1 (smoothed)
        int overseasLowControlYears = 0;  // accumulated years below threshold
    };
    const ExplorationState& getExploration() const { return m_exploration; }
    ExplorationState& getExplorationMutable() { return m_exploration; }

    struct RegionalState {
        double popShare = 0.0;         // 0..1
        double localControl = 0.0;     // 0..1
        double grievance = 0.0;        // 0..1
        double elitePower = 0.0;       // 0..1
        double distancePenalty = 0.0;  // 0..1
    };
    const std::vector<RegionalState>& getRegions() const { return m_regions; }
    std::vector<RegionalState>& getRegionsMutable() { return m_regions; }

    bool canAttemptColonization(const class TechnologyManager& techManager, const CultureManager& cultureManager) const;
    float computeColonizationPressure(const CultureManager& cultureManager,
                                      double marketAccess,
                                      double landPressure) const;
    double computeNavalRangePx(const class TechnologyManager& techManager, const CultureManager& cultureManager) const;
    bool forceAddPort(const Map& map, const sf::Vector2i& pos);

    // Phase 1/2 polity observables (pressure/control systems)
    double getLegitimacy() const { return m_polity.legitimacy; }
    void setLegitimacy(double v);
    double getAdminCapacity() const { return m_polity.adminCapacity; }
    double getFiscalCapacity() const { return m_polity.fiscalCapacity; }
    double getLogisticsReach() const { return m_polity.logisticsReach; }
    double getTaxRate() const { return m_polity.taxRate; }
    double getDebt() const { return m_polity.debt; }
    double getMilitarySpendingShare() const { return m_polity.militarySpendingShare; }
    double getAdminSpendingShare() const { return m_polity.adminSpendingShare; }
    double getInfraSpendingShare() const { return m_polity.infraSpendingShare; }
    double getHealthSpendingShare() const { return m_polity.healthSpendingShare; }
    double getEducationSpendingShare() const { return m_polity.educationSpendingShare; }
    double getRndSpendingShare() const { return m_polity.rndSpendingShare; }
    double getAvgControl() const { return m_avgControl; }
    void setAvgControl(double v);
    void setTaxRate(double v);
    void setBudgetShares(double military,
                         double admin,
                         double infra,
                         double health,
                         double education,
                         double rnd);

    // Phase 5B: institution effects helpers.
    void addAdminCapacity(double dv);
    void addFiscalCapacity(double dv);
    void addLogisticsReach(double dv);
    void addDebt(double dv);
    void addEducationSpendingShare(double dv);
    void addHealthSpendingShare(double dv);
    void addRndSpendingShare(double dv);

    double getMilitaryStrength() const;
    // Add a member variable to store science points
    double getSciencePoints() const;
    void addSciencePoints(double points);
    void setSciencePoints(double points);
    void addBoundaryPixel(const sf::Vector2i& cell);
    const std::unordered_set<sf::Vector2i>& getBoundaryPixels() const;
    // Deterministic territory helpers (Phase 0-3 audit fix: never sample unordered containers).
    void addTerritoryCell(const sf::Vector2i& c);
    void removeTerritoryCell(const sf::Vector2i& c);
    sf::Vector2i randomTerritoryCell(std::mt19937_64& rng) const;
    sf::Vector2i deterministicTerritoryAnchor() const;
    void canonicalizeDeterministicContainers();
    void canonicalizeDeterministicScalars(double fineScale, double govScale);
    const std::vector<sf::Vector2i>& getTerritoryVec() const { return m_territoryVec; }
    const ResourceManager& getResourceManager() const;
    const std::string& getName() const;
    void setName(const std::string& name);
    const std::string& getSpawnRegionKey() const { return m_spawnRegionKey; }
    void setSpawnRegionKey(const std::string& key) { m_spawnRegionKey = key; }
    Type getType() const; // Add a getter for the country type
    bool canFoundCity() const;
    bool canFoundCity(const class TechnologyManager& technologyManager) const;
    void checkCityGrowth(int currentYear, News& news); // Check for city upgrades and new cities
    void buildRoads(std::vector<Country>& allCountries,
                    const class Map& map,
                    const std::vector<std::vector<bool>>& isLandGrid,
                    const class TechnologyManager& techManager,
                    int currentYear,
                    News& news); // Road building system
    void buildAirways(std::vector<Country>& allCountries,
                      const class Map& map,
                      const class TechnologyManager& techManager,
                      int currentYear,
                      News& news); // Airway connections (invisible roads)
	    void buildPorts(const std::vector<std::vector<bool>>& isLandGrid,
	                    const std::vector<std::vector<int>>& countryGrid,
	                    int currentYear,
	                    std::mt19937_64& gen,
	                    News& news); // Coastal port system (preps for boats)
    
    // Road system helper functions
    bool canBuildRoadTo(const Country& otherCountry, int currentYear) const;
    bool canBuildAirwayTo(const Country& otherCountry, int currentYear) const;
    sf::Vector2i getClosestCityTo(const Country& otherCountry) const;
    double calculateDistanceToCountry(const Country& otherCountry) const;
    std::vector<sf::Vector2i> createRoadPath(sf::Vector2i start, sf::Vector2i end, const class Map& map) const;
    const std::vector<sf::Vector2i>& getRoads() const { return m_roads; }
    const std::vector<sf::Vector2i>& getFactories() const { return m_factories; }
    const std::vector<sf::Vector2i>& getPorts() const { return m_ports; }
    const std::unordered_set<int>& getAirways() const { return m_airways; }
    bool canDeclareWar() const;
    void startWar(Country& target, News& news);
    void endWar(int currentYear = 0);
    bool isAtWar() const;
    WarGoal getActiveWarGoal() const { return m_activeWarGoal; }
    double getWarExhaustion() const { return m_warExhaustion; }
    double getWarSupplyCapacity() const { return m_warSupplyCapacity; }
    bool isNeighbor(const Country& other) const;
    int getWarDuration() const;
    void setWarDuration(int duration);
    void decrementWarDuration();
    bool isWarofAnnihilation() const;
    void setWarofAnnihilation(bool isannhilation);
    bool isWarofConquest() const;
    void setWarofConquest(bool isconquest);
    int getPeaceDuration() const;
    void setPeaceDuration(int duration);
    void decrementPeaceDuration();
    bool isAtPeace() const;
    void addConqueredCity(const City& city);
    const std::vector<Country*>& getEnemies() const;
    void addEnemy(Country* enemy);
    void removeEnemy(Country* enemy);
    void clearEnemies();
    void setPopulation(long long population);
    double getStability() const;
    int getStagnationYears() const { return m_stagnationYears; }
    int getYearsSinceWar() const;
    bool isFragmentationReady() const;
    int getFragmentationCooldown() const;
    void setStability(double stability);
    void setFragmentationCooldown(int years);
    void setYearsSinceWar(int years);
    void resetStagnation();
    sf::Vector2i getCapitalLocation() const;
    sf::Vector2i getStartingPixel() const;
    void setStartingPixel(const sf::Vector2i& cell);
    void setTerritory(const std::unordered_set<sf::Vector2i>& territory);
    void setCities(const std::vector<City>& cities);
    void setRoads(const std::vector<sf::Vector2i>& roads);
    void clearRoadNetwork();
    void setFactories(const std::vector<sf::Vector2i>& factories);
    void setPorts(const std::vector<sf::Vector2i>& ports);
    void clearPorts();
    void clearWarState();
    void resetTechnologyBonuses();
    
    // NEW LOGISTIC POPULATION SYSTEM
    double computeYearlyFood(const std::vector<std::vector<std::unordered_map<Resource::Type,double>>>& resourceGrid) const;
    long long stepLogistic(double r, const std::vector<std::vector<std::unordered_map<Resource::Type,double>>>& resourceGrid, double techKMultiplier, double climateKMultiplier);
    long long stepLogisticFromFoodSum(double r, double yearlyFoodSum, double techKMultiplier, double climateKMultiplier);
    double getPlagueMortalityMultiplier(const class TechnologyManager& tm) const;
    
    // Resets military strength to its base value (based on the country type).
    void resetMilitaryStrength();
    // Applies a bonus multiplier to the current military strength.
    void applyMilitaryBonus(double bonus);
    double getCulturePoints() const;
    void addCulturePoints(double points);
    void setCulturePoints(double points);
    double getCultureMultiplier() const { return m_cultureMultiplier; }
    void resetCultureMultiplier();
    void applyCultureMultiplier(double bonus);

    // (Optionally, if you want to apply a bonus for science too, add similar methods.)

    void resetScienceMultiplier();
    void applyScienceMultiplier(double bonus);
    double getScienceMultiplier() const { return m_scienceMultiplier; }
    
    // Fast Forward Mode methods
    void fastForwardGrowth(int yearIndex, int currentYear, const std::vector<std::vector<bool>>& isLandGrid, 
                          std::vector<std::vector<int>>& countryGrid, 
                          const std::vector<std::vector<std::unordered_map<Resource::Type, double>>>& resourceGrid,
                          News& news, Map& map, const class TechnologyManager& technologyManager,
	                          std::mt19937_64& gen, bool plagueAffected = false);
    void applyPlagueDeaths(long long deaths);
    
    // Technology effects
    void applyTechnologyBonus(int techId, double scale = 1.0);
    double getTotalPopulationGrowthRate() const;
    double getPlagueResistance() const;
    double getMilitaryStrengthMultiplier() const;
    double getTerritoryCaptureBonusRate() const;
    double getDefensiveBonus() const;
    double getWarDurationReduction() const;
    double getMaxSizeMultiplier() const;
    int getExpansionRateBonus() const;
    int getBurstExpansionRadius() const;
    int getBurstExpansionFrequency() const;
    bool canAnnihilateCountry(const Country& target) const;
    void absorbCountry(Country& target, Map& map, News& news);
    int getWarBurstConquestRadius() const;
    int getWarBurstConquestFrequency() const;
    
    // Ideology system
    Ideology getIdeology() const { return m_ideology; }
    void setIdeology(Ideology ideology) { m_ideology = ideology; }
    std::string getIdeologyString() const;
    void checkIdeologyChange(int currentYear, News& news, const class TechnologyManager& techManager);
    bool canChangeToIdeology(Ideology newIdeology) const;
    double getSciencePointsMultiplier() const;
    double calculateNeighborScienceBonus(const std::vector<Country>& allCountries, const class Map& map, const class TechnologyManager& techManager, int currentYear) const;
    double calculateScienceGeneration() const; // New scaler-based science calculation
    static void setScienceScaler(double scaler) { s_scienceScaler = scaler; }
    
    // TECHNOLOGY SHARING SYSTEM
    void initializeTechSharingTimer(int currentYear);
    void attemptTechnologySharing(int currentYear, std::vector<Country>& allCountries, const class TechnologyManager& techManager, const class Map& map, class News& news);
    bool canShareTechWith(const Country& target, int currentYear) const;
    void recordWarEnd(int enemyIndex, int currentYear);

    // Country-level demography and epidemiology.
    const std::array<double, 5>& getPopulationCohorts() const { return m_popCohorts; } // 0-4,5-14,15-49,50-64,65+
    std::array<double, 5>& getPopulationCohortsMutable() { return m_popCohorts; }
    struct EpidemicState {
        double s = 0.999;
        double i = 0.001;
        double r = 0.0;
    };
    const EpidemicState& getEpidemicState() const { return m_epi; }
    EpidemicState& getEpidemicStateMutable() { return m_epi; }
    double getAutonomyPressure() const { return m_autonomyPressure; }
    int getAutonomyOverThresholdYears() const { return m_autonomyOverThresholdYears; }
    void setAutonomyPressure(double v) { m_autonomyPressure = std::clamp(v, 0.0, 1.0); }
    void setAutonomyOverThresholdYears(int v) { m_autonomyOverThresholdYears = std::max(0, v); }
    double getEliteDefectionPressure() const { return m_eliteDefectionPressure; }
    int getNextSuccessionYear() const { return m_nextSuccessionYear; }
    void initializePopulationCohorts();
    void renormalizePopulationCohortsToTotal();
    double getWorkingAgeLaborSupply() const;

private:
	    void attemptFactoryConstruction(const TechnologyManager& techManager,
	                                    const std::vector<std::vector<bool>>& isLandGrid,
	                                    const std::vector<std::vector<int>>& countryGrid,
	                                    std::mt19937_64& gen,
	                                    News& news);
	    int m_countryIndex;
	    std::mt19937_64 m_rng;
	    sf::Color m_color;
	    long long m_population;
	    long long m_prevYearPopulation = -1;
	    std::unordered_set<sf::Vector2i> m_boundaryPixels;
        // Deterministic owned-cell list for repeatable RNG sampling (kept consistent via add/remove/setTerritory).
        std::vector<sf::Vector2i> m_territoryVec;
        std::unordered_map<sf::Vector2i, size_t> m_territoryIndex;
	    double m_populationGrowthRate;
    ResourceManager m_resourceManager;
    std::string m_name;
    std::string m_spawnRegionKey;
	    int m_nextWarCheckYear;
	    std::vector<City> m_cities;
	    double m_totalCityPopulation = 0.0;
        double m_specialistPopulation = 0.0;
        double m_knowledgeInfra = 0.0;
        CityCandidate m_cityCandidate{};
	    bool m_hasCity;
    double m_gold;
    double m_wealth = 0.0;  // national net worth proxy (aggregated from econ grid)
    double m_gdp = 0.0;     // yearly value-added proxy (estimated from capital formation)
    double m_exports = 0.0; // yearly exports proxy (border-flow proxy + TradeManager export value)
	    MacroEconomyState m_macro{};
        double m_lastTaxBase = 0.0; // annualized taxable value base
        double m_lastTaxTake = 0.0; // annualized tax revenue actually collected
	    KnowledgeVec m_knowledge{}; // domain knowledge stocks (unbounded, thresholds unlock tech)
    std::vector<uint8_t> m_knownTechDense; // dense known-tech bitset (0/1).
    std::vector<float> m_adoptionTechDense; // dense adoption level [0,1].
    std::vector<uint16_t> m_lowAdoptionYearsDense; // consecutive years with very low adoption.
    double m_innovationRate = 0.0; // yearly innovation rate proxy (for UI/debug)
	    TraitVec m_traits{}; // 0..1 cultural trait stocks
        ExplorationState m_exploration{};
    double m_militaryStrength;  // Add military strength member
    Type m_type; // Add a member variable to store the country type
    Ideology m_ideology;
    sf::Vector2i m_startingPixel; // Remember the original founding location for capital bonus
    double m_culturePoints; // For culture points
    double m_cultureMultiplier = 1.0;
    
    // TECHNOLOGY SHARING SYSTEM (for Trader countries)
    int m_nextTechSharingYear = 0;
    std::unordered_map<int, int> m_lastWarEndYear; // Track when wars ended with other countries
    
    // ROAD BUILDING SYSTEM
    std::vector<sf::Vector2i> m_roads; // All road pixels owned by this country
    std::vector<sf::Vector2i> m_factories; // Factory positions within national territory
    std::vector<sf::Vector2i> m_ports; // Port positions within national territory
    std::unordered_set<int> m_airways; // Airway connections (other country indices)
    std::unordered_map<int, std::vector<sf::Vector2i>> m_roadsToCountries; // Roads to specific countries
    int m_nextRoadCheckYear = std::numeric_limits<int>::min(); // When to next check for road building opportunities (initialize to start year)
	int m_nextPortCheckYear = std::numeric_limits<int>::min(); // When to next check for port building opportunities
	int m_nextAirwayCheckYear = std::numeric_limits<int>::min(); // When to next check for airway building opportunities
	bool m_hasCheckedMajorCityUpgrade = false; // Track if we've checked for major city upgrade this population milestone
	long long m_prePlaguePopulation;
    std::vector<Country*> m_enemies;
    bool m_isAtWar;
    int m_warDuration;
    bool m_isWarofAnnihilation;
    bool m_isWarofConquest;
    WarGoal m_activeWarGoal = WarGoal::BorderShift;
    WarGoal m_pendingWarGoal = WarGoal::BorderShift;
    double m_warExhaustion = 0.0;
    double m_warSupplyCapacity = 0.0;
    int m_peaceDuration;
    long long m_preWarPopulation;
    int m_warCheckCooldown;  // Cooldown period before checking for war again
    int m_warCheckDuration;  // Duration for actively seeking war
    bool m_isSeekingWar;    // Flag to indicate if currently seeking war
    double m_sciencePoints; // For science points
    double m_scienceMultiplier = 1.0;
    double m_researchMultiplier = 1.0; // Multiplicative technology research bonuses
    
    // Science generation scaler system
    static double s_scienceScaler; // Global scaler for balancing
    double m_traitScienceMultiplier = 1.0;
    double m_policyScienceMultiplier = 1.0;
    double m_situationScienceMultiplier = 1.0;
    double m_educationScienceBase = 0.0;
    double m_buildingScienceMultiplier = 1.0;
    
    // Technology bonuses
    double m_populationGrowthBonus = 0.0;
    double m_plagueResistanceBonus = 0.0;
    double m_militaryStrengthBonus = 0.0;
    double m_territoryCaptureBonusRate = 0.0;
    double m_defensiveBonus = 0.0;
    double m_warDurationReduction = 0.0;
    double m_maxSizeMultiplier = 1.0;
    int m_expansionRateBonus = 0;
    int m_flatMaxSizeBonus = 0; // Additional flat territory cap unlocked by breakthrough logistics
    int m_burstExpansionRadius = 1; // How many pixels outward to expand in bursts
    int m_burstExpansionFrequency = 0; // How often burst expansion occurs (0 = never)
    int m_warBurstConquestRadius = 1; // How many enemy pixels to capture in war bursts
    int m_warBurstConquestFrequency = 0; // How often war burst conquest occurs
    double m_sciencePointsBonus = 0.0; // Science research speed bonus
    
    // 🚀 PERFORMANCE: Cache neighbor science bonuses to avoid O(n²) calculations
    mutable double m_cachedNeighborScienceBonus = 0.0;
    mutable int m_neighborBonusLastUpdated = -999999; // Year when bonus was last calculated
    mutable std::vector<int> m_cachedNeighborIndices; // Cache of neighbor country indices
    mutable int m_neighborRecalculationInterval = 50; // Random interval between 20-80 years for this country
    
    // Expansion contentment system
    bool m_isContentWithSize = false; // Whether country wants to stop expanding
    int m_contentmentDuration = 0; // How many years to remain content
    int m_expansionStaggerOffset = 0; // Personal offset for burst expansion timing
	double m_stability = 1.0;
	int m_stagnationYears = 0;
	int m_fragmentationCooldown = 0;
	int m_yearsSinceWar = 0;

		// Phase 1: polity state (pressure-and-constraint driven).
		struct PolityState {
	    double legitimacy = 0.65;            // 0..1
	    double adminCapacity = 0.08;         // 0..1 (maps to a territory cap)
	    double fiscalCapacity = 0.10;        // 0..1
	    double logisticsReach = 0.10;        // 0..1
	    double taxRate = 0.08;               // 0..1
	    double treasurySpendRate = 1.05;     // multiplier on income (allows deficits)
	    double militarySpendingShare = 0.34; // budget share
	    double adminSpendingShare = 0.28;
	    double infraSpendingShare = 0.38;
	    double healthSpendingShare = 0.00;   // placeholder
	    double educationSpendingShare = 0.00; // placeholder
        double rndSpendingShare = 0.00;
	    double debt = 0.0;
	    int lastPolicyYear = std::numeric_limits<int>::min();
	};
		PolityState m_polity{};
		int m_expansionBudgetCells = 0; // desired claims per year (AI-controlled)
		int m_pendingWarTarget = -1;    // neighbor index (AI-controlled)

        // Phase 2: coarse territorial control (set by Map control grid).
        double m_avgControl = 1.0; // 0..1

        std::array<double, 5> m_popCohorts{}; // 0-4, 5-14, 15-49, 50-64, 65+
        EpidemicState m_epi{};
        double m_autonomyPressure = 0.0;
        int m_autonomyOverThresholdYears = 0;
        std::vector<RegionalState> m_regions;
        int m_nextSuccessionYear = std::numeric_limits<int>::min();
        double m_eliteDefectionPressure = 0.0;
	};
