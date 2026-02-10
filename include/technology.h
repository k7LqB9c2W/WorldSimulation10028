#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

class Country;
class Map;

struct Technology {
    std::string name;
    int cost;
    int id;
    std::vector<int> requiredTechs;
    int domainId = 0;       // Phase 5A: knowledge domain index (0..Country::kDomains-1)
    double threshold = 0.0; // Phase 5A: knowledge threshold to unlock (no point spending)
    std::string capabilityTag;
    int order = 0;                // Stable progression ordering (independent from id).
    double difficulty = 0.0;      // Discovery hazard denominator (higher = harder).
    bool isKeyTransition = false; // Used by milestone/debug summaries.
    bool requiresCoast = false;
    bool requiresRiverOrWetland = false;
    double minClimateFoodMult = 0.0;
    double minFarmingPotential = 0.0;
    double minForagingPotential = 0.0;
    double minOreAvail = 0.0;
    double minEnergyAvail = 0.0;
    double minConstructionAvail = 0.0;
    double minInstitution = 0.0;
    double minSpecialization = 0.0;
    double minPlantDomestication = 0.0;
    double minHerdDomestication = 0.0;
};

// Authoritative technology ID constants.
// Keep these aligned with TechnologyManager::initializeTechnologies().
namespace TechId {
    constexpr int PROTO_WRITING = 117;
    constexpr int NUMERACY_MEASUREMENT = 118;
    constexpr int NATIVE_COPPER_WORKING = 119;
    constexpr int COPPER_SMELTING = 120;

    constexpr int WRITING = 11;
    constexpr int CONSTRUCTION = 16;
    constexpr int CURRENCY = 15;

    constexpr int EDUCATION = 30;
    constexpr int CIVIL_SERVICE = 32;
    constexpr int BANKING = 34;
    constexpr int ECONOMICS = 45;

    constexpr int UNIVERSITIES = 39;
    constexpr int ASTRONOMY = 40;
    constexpr int SCIENTIFIC_METHOD = 49;

    constexpr int METALLURGY = 42;
    constexpr int NAVIGATION = 43;

    constexpr int SANITATION = 96;
}

class TechnologyManager {
public:
    TechnologyManager();
    void initializeTechnologies(); // Call this in the constructor
    void updateCountry(Country& country, const Map& map);
    bool canUnlockTechnology(const Country& country, int techId) const;
    void unlockTechnology(Country& country, int techId);
    const std::unordered_map<int, Technology>& getTechnologies() const;
    const std::vector<int>& getUnlockedTechnologies(const Country& country) const;
    const std::vector<int>& getSortedTechnologyIds() const;
    int getTechDenseIndex(int techId) const;
    int getTechIdFromDenseIndex(int denseIndex) const;
    int getTechCount() const;
    bool countryKnowsTech(const Country& country, int techId) const;
    float countryTechAdoption(const Country& country, int techId) const;
    bool hasAdoptedTech(const Country& country, int techId) const;
    void setUnlockedTechnologiesForEditor(Country& country, const std::vector<int>& techIds, bool includePrerequisites);
    void printMilestoneAdoptionSummary() const;

    // Phase 5A+: update knowledge (innovation + diffusion), discovery, and adoption dynamics.
    void tickYear(std::vector<Country>& countries,
                  const Map& map,
                  const std::vector<float>* tradeIntensityMatrix,
                  int currentYear,
                  int dtYears);
    
    // POPULATION SYSTEM HELPERS
    static bool hasTech(const TechnologyManager& tm, const Country& c, int id);
    static double techKMultiplier(const TechnologyManager& tm, const Country& c);
    static double techGrowthRateR(const TechnologyManager& tm, const Country& c);
    
    // 🔧 DEBUG CONTROL: Toggle tech unlock messages
    static void setDebugMode(bool enabled) { s_debugMode = enabled; }
    static bool getDebugMode() { return s_debugMode; }

private:
    struct CountryTechSignals {
        double pop = 0.0;
        double urban = 0.0;
        double specialization = 0.0;
        double institution = 0.0;
        double stability = 0.0;
        double legitimacy = 0.0;
        double marketAccess = 0.0;
        double connectivity = 0.0;
        double openness = 0.0;
        double inequality = 0.0;
        double foodSecurity = 1.0;
        double famineSeverity = 0.0;
        double climateFoodMult = 1.0;
        double farmingPotential = 0.0;
        double foragingPotential = 0.0;
        double oreAvail = 0.0;
        double energyAvail = 0.0;
        double constructionAvail = 0.0;
        double plantDomesticationPotential = 0.0;
        double herdDomesticationPotential = 0.0;
        double coastAccessRatio = 0.0;
        double riverWetlandShare = 0.0;
        bool atWar = false;
    };

    static double smooth01(double x);
    static std::uint64_t techEventKey(int countryIndex, int denseTech);
    double deterministicUnit(std::uint64_t worldSeed,
                             int currentYear,
                             int countryIndex,
                             int denseTech,
                             std::uint64_t salt) const;
    bool prerequisitesKnown(const Country& country, const Technology& tech) const;
    bool prerequisitesAdopted(const Country& country, const Technology& tech, double thresholdScale = 1.0) const;
    bool isFeasible(const Country& country,
                    const Technology& tech,
                    const CountryTechSignals& s) const;
    void ensureCountryState(Country& country) const;
    void refreshUnlockedFromAdoption(Country& country, double adoptionThreshold);
    void recomputeCountryTechEffects(Country& country, double adoptionThreshold) const;
    void maybeRecordMilestoneEvents(const Country& country,
                                    const Technology& tech,
                                    int denseTech,
                                    bool becameKnown,
                                    bool crossedAdoption,
                                    int currentYear);

    std::unordered_map<int, Technology> m_technologies;
    std::unordered_map<int, std::vector<int>> m_unlockedTechnologies;
    std::vector<int> m_sortedIds; // Sorted by (order, id) for stable progression ordering.
    std::vector<int> m_denseTechIds;
    std::unordered_map<int, int> m_techIdToDense;
    std::unordered_map<std::uint64_t, int> m_firstKnownYear;
    std::unordered_map<std::uint64_t, int> m_firstAdoptionYear;
    static bool s_debugMode; // Debug mode flag
};
