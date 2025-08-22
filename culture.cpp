// culture.cpp
#include "culture.h"
#include "country.h"
#include "technology.h"
#include <algorithm>
#include <iostream>

using namespace std;

// 🔧 STATIC DEBUG CONTROL: Default to OFF (no spam)
bool CultureManager::s_debugMode = false;

CultureManager::CultureManager() {
    initializeCivics();
}

void CultureManager::initializeCivics() {
    m_civics = {
        {1, {"Code of Laws", 900, 1, {}, {}}}, // No requirements
        {2, {"Craftsmanship", 80, 2, {1}, {}}},
        {3, {"Foreign Trade", 80, 3, {1}, {}}},
        {4, {"State Workforce", 150, 4, {2}, {}}},
        {5, {"Early Empire", 180, 5, {3}, {}}},
        {6, {"Mysticism", 200, 6, {1}, {}}},
        {7, {"Military Tradition", 220, 7, {2}, {}}},
        {8, {"Games and Recreation", 250, 8, {4}, {}}},
        {9, {"Political Philosophy", 280, 9, {4, 5}, {}}},
        {10, {"Ancient Republic", 300, 10, {9}, {}}}, // Government
        {11, {"Drama and Poetry", 350, 11, {6}, {}}},
        {12, {"Feudalism", 400, 12, {4}, {2}}}, // Requires Animal Husbandry tech
        {13, {"Naval Tradition", 420, 13, {3, 7}, {5}}}, // Requires Sailing tech
        {14, {"Imperialism", 450, 14, {5}, {}}},
        {15, {"Theology", 480, 15, {6}, {6}}}, // Requires Calendar tech
        {16, {"Medieval Republic", 500, 16, {10}, {}}}, // Government
        {17, {"Guilds", 550, 17, {12}, {14}}}, // Requires Mathematics tech
        {18, {"Mercenaries", 580, 18, {7, 12}, {}}},
        {19, {"Humanism", 620, 19, {11}, {22}}}, // Requires Philosophy tech
        {20, {"Diplomatic Service", 650, 20, {14}, {}}},
        {21, {"Divine Right", 680, 21, {15}, {}}},
        {22, {"Renaissance Republic", 720, 22, {16}, {}}}, // Government
        {23, {"Mercantilism", 780, 23, {17}, {15}}}, // Requires Currency tech
        {24, {"Professional Army", 820, 24, {18}, {28}}}, // Requires Steel tech
        {25, {"Enlightenment", 880, 25, {19}, {30}}}, // Requires Education tech
        {26, {"Colonialism", 920, 26, {20, 23}, {}}},
        {27, {"Civil Engineering", 950, 27, {8}, {23}}}, // Requires Engineering tech
        {28, {"Nationalism", 1000, 28, {25}, {}}},
        {29, {"Opera and Ballet", 1050, 29, {19}, {31}}}, // Requires Acoustics tech
        {30, {"Modern Republic", 1100, 30, {22}, {}}}, // Government
        {31, {"Capitalism", 1150, 31, {23}, {44}}}, // Requires Economics tech
        {32, {"Mass Production", 1200, 32, {24}, {57}}}, // Requires Replaceable Parts tech
        {33, {"Urbanization", 1250, 33, {27}, {52}}}, // Requires Sanitation tech
        {34, {"Social Contract", 1300, 34, {25}, {}}},
        {35, {"Free Market", 1350, 35, {31}, {}}},
        {36, {"Suffrage", 1400, 36, {28}, {}}},
        {37, {"Totalitarianism", 1450, 37, {28}, {}}}, // Government
        {38, {"Class Struggle", 1500, 38, {32}, {}}},
        {39, {"Public Works", 1550, 39, {33}, {55}}}, // Requires Railroad tech
        {40, {"Propaganda", 1600, 40, {29}, {62}}}, // Requires Radio tech
        {41, {"Modern Democracy", 1650, 41, {30}, {}}}, // Government
        {42, {"Communism", 1700, 42, {38}, {63}}}, // Requires Mass Production tech
        {43, {"Environmentalism", 1750, 43, {39}, {72}}}, // Requires Ecology tech
        {44, {"Mass Media", 1800, 44, {40}, {}}},
        {45, {"Social Media", 1850, 45, {44}, {89}}}, // Requires Social Media tech
        {46, {"Globalization", 1900, 46, {35, 42}, {79}}}, // Requires Internet tech
        {47, {"Cyber Security", 1950, 47, {45}, {92}}}, // Requires Blockchain tech
        {48, {"Human Rights", 2000, 48, {46}, {}}}
    };
    }

        void CultureManager::updateCountry(Country& country) {
        // Check if the country can unlock any new civics
        for (const auto& pair : m_civics) {
            int civicId = pair.first;
            const Civic& civic = pair.second;

            if (canUnlockCivic(country, civicId)) {
                if (country.getCulturePoints() >= civic.cost) {
                    unlockCivic(country, civicId);
                }
            }
        }
    }

    bool CultureManager::canUnlockCivic(const Country& country, int civicId) const {
        // Check if the civic has already been unlocked
        auto itCountry = m_unlockedCivics.find(country.getCountryIndex());
        if (itCountry != m_unlockedCivics.end()) {
            const std::vector<int>& unlockedCivics = itCountry->second;
            if (std::find(unlockedCivics.begin(), unlockedCivics.end(), civicId) != unlockedCivics.end()) {
                return false; // Already unlocked
            }
        }

        // Check if all required civics are unlocked
        auto itCivic = m_civics.find(civicId);
        if (itCivic == m_civics.end()) {
            return false; // Civic not found
        }

        const Civic& civic = itCivic->second;
        for (int requiredCivicId : civic.requiredCivics) {
            bool found = false;
            if (itCountry != m_unlockedCivics.end()) {
                const std::vector<int>& unlockedCivics = itCountry->second;
                if (std::find(unlockedCivics.begin(), unlockedCivics.end(), requiredCivicId) != unlockedCivics.end()) {
                    found = true;
                }
            }
            if (!found) {
                return false; // Required civic not unlocked
            }
        }

        // Check if all required technologies are unlocked
        for (int requiredTechId : civic.requiredTechs) {
            bool found = false;
            // Assuming you have a way to get the TechnologyManager and check unlocked techs
            // Replace the following line with your actual logic to check for unlocked technologies
            // Example: if (technologyManager.getUnlockedTechnologies(country).contains(requiredTechId)) {
            if (false) { // Replace this with your actual technology check
                found = true;
            }
            if (!found) {
                return false; // Required technology not unlocked
            }
        }

        return true;
    }

    void CultureManager::unlockCivic(Country& country, int civicId) {
        // Unlock the civic for the country
        m_unlockedCivics[country.getCountryIndex()].push_back(civicId);

        // Deduct culture points
        auto itCivic = m_civics.find(civicId);
        if (itCivic != m_civics.end()) {
            const Civic& civic = itCivic->second;
            country.setCulturePoints(country.getCulturePoints() - civic.cost);
            country.setCulturePoints(0); // Reset culture points to 0 after unlocking
        }

        // Add any immediate effects of the civic here (e.g., government type changes)
        // ... (Implementation for civic effects will be added later)
        
        // 🔧 DEBUG CONTROL: Only show message if debug mode is enabled
        if (s_debugMode) {
            cout << country.getName() << " unlocked civic: " << m_civics.at(civicId).name << endl;
        }
    }

    const std::unordered_map<int, Civic>& CultureManager::getCivics() const {
        return m_civics;
    }

    const std::vector<int>& CultureManager::getUnlockedCivics(const Country& country) const {
        auto it = m_unlockedCivics.find(country.getCountryIndex());
        if (it != m_unlockedCivics.end()) {
            return it->second;
        }
        static const std::vector<int> emptyVec; // Return an empty vector if not found
        return emptyVec;
    }