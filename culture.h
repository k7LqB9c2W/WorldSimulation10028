// culture.h
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include "country.h"

class Country;
class TechnologyManager;

struct Civic {
    std::string name;
    int cost;
    int id;
    std::vector<int> requiredCivics;
    std::vector<int> requiredTechs; // Technologies required to unlock this civic
};

class CultureManager {
public:
    CultureManager();
    void initializeCivics();
    void updateCountry(Country& country, const TechnologyManager& techManager);
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
