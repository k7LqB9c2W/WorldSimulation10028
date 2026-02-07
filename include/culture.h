// culture.h
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include "country.h"

class Country;
class TechnologyManager;
class Map;
class News;

struct Civic {
    std::string name;
    int cost;
    int id;
    std::vector<int> requiredCivics;
    std::vector<int> requiredTechs; // Technologies required to unlock this civic

    // Phase 5B: institutions (adoption dynamics; no culture currency).
    double minUrbanization = 0.0;
    double minAdminCapacity = 0.0;
    double minAvgControl = 0.0;
    double stabilityHit = 0.0;
    double legitimacyHit = 0.0;
    double stabilityBonus = 0.0;
    double legitimacyBonus = 0.0;
    double debtAdd = 0.0;
    double adminCapBonus = 0.0;
    double fiscalCapBonus = 0.0;
    double logisticsBonus = 0.0;
    double educationShareBonus = 0.0;
    double healthShareBonus = 0.0;
};

class CultureManager {
public:
    CultureManager();
    void initializeCivics();
    // Legacy per-country entry point (kept for editor calls); prefer `tickYear`.
    void updateCountry(Country& country, const TechnologyManager& techManager);
    // Phase 5B: update traits + adopt institutions (world-aware, contact-based).
    void tickYear(std::vector<Country>& countries,
                  const Map& map,
                  const TechnologyManager& techManager,
                  const std::vector<float>* tradeIntensityMatrix,
                  int currentYear,
                  int dtYears,
                  News& news);
    bool canUnlockCivic(const Country& country, int civicId, const TechnologyManager& techManager) const;
    void unlockCivic(Country& country, int civicId);
    const std::unordered_map<int, Civic>& getCivics() const;
    const std::vector<int>& getUnlockedCivics(const Country& country) const;
    
    // 🔧 DEBUG CONTROL: Toggle civic unlock messages
    static void setDebugMode(bool enabled) { s_debugMode = enabled; }
    static bool getDebugMode() { return s_debugMode; }

private:
    std::unordered_map<int, Civic> m_civics;
    std::unordered_map<int, std::vector<int>> m_unlockedCivics; // Country index -> list of unlocked civic IDs
    static bool s_debugMode; // Debug mode flag
};
