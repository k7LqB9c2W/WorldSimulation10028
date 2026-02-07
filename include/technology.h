#pragma once

#include <string>
#include <vector>
#include <unordered_map>

class Country;
class Map;

struct Technology {
    std::string name;
    int cost;
    int id;
    std::vector<int> requiredTechs;
    int domainId = 0;       // Phase 5A: knowledge domain index (0..Country::kDomains-1)
    double threshold = 0.0; // Phase 5A: knowledge threshold to unlock (no point spending)
};

// Authoritative technology ID constants.
// Keep these aligned with TechnologyManager::initializeTechnologies().
namespace TechId {
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
    void updateCountry(Country& country);
    bool canUnlockTechnology(const Country& country, int techId) const;
    void unlockTechnology(Country& country, int techId);
    const std::unordered_map<int, Technology>& getTechnologies() const;
    const std::vector<int>& getUnlockedTechnologies(const Country& country) const;
    const std::vector<int>& getSortedTechnologyIds() const;
    void setUnlockedTechnologiesForEditor(Country& country, const std::vector<int>& techIds, bool includePrerequisites);

    // Phase 5A: update knowledge (innovation + diffusion) and then unlock technologies by thresholds.
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
    std::unordered_map<int, Technology> m_technologies;
    std::unordered_map<int, std::vector<int>> m_unlockedTechnologies;
    std::vector<int> m_sortedIds; // Sorted technology IDs for stable unlock ordering
    static bool s_debugMode; // Debug mode flag
};
