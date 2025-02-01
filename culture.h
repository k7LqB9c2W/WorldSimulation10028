// culture.h
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include "country.h"

class Country;

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
    void updateCountry(Country& country);
    bool canUnlockCivic(const Country& country, int civicId) const;
    void unlockCivic(Country& country, int civicId);
    const std::unordered_map<int, Civic>& getCivics() const;
    const std::vector<int>& getUnlockedCivics(const Country& country) const;

private:
    std::unordered_map<int, Civic> m_civics;
    std::unordered_map<int, std::vector<int>> m_unlockedCivics; // Country index -> list of unlocked civic IDs
};