#pragma once

#include <string>
#include <vector>
#include <unordered_map>

class Country;

struct Technology {
    std::string name;
    int cost;
    int id;
    std::vector<int> requiredTechs;
};

class TechnologyManager {
public:
    TechnologyManager();
    void initializeTechnologies(); // Call this in the constructor
    void updateCountry(Country& country);
    bool canUnlockTechnology(const Country& country, int techId) const;
    void unlockTechnology(Country& country, int techId);
    const std::unordered_map<int, Technology>& getTechnologies() const;
    const std::vector<int>& getUnlockedTechnologies(const Country& country) const;
    
    // 🔧 DEBUG CONTROL: Toggle tech unlock messages
    static void setDebugMode(bool enabled) { s_debugMode = enabled; }
    static bool getDebugMode() { return s_debugMode; }

private:
    std::unordered_map<int, Technology> m_technologies;
    std::unordered_map<int, std::vector<int>> m_unlockedTechnologies;
    static bool s_debugMode; // Debug mode flag
};