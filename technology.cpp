#include "technology.h"
#include "country.h"
#include <algorithm>
#include <iostream> // Make sure to include iostream if not already present

using namespace std;

// 🔧 STATIC DEBUG CONTROL: Default to OFF (no spam)
bool TechnologyManager::s_debugMode = false;

TechnologyManager::TechnologyManager() {
    initializeTechnologies();
}

void TechnologyManager::initializeTechnologies() {
    // ACCELERATION REBALANCE: Reduced costs for faster tech progression to reach modern era
    m_technologies = {
        {1, {"Pottery", 15, 1, {}}},          // Reduced for faster start
        {2, {"Animal Husbandry", 15, 2, {}}}, // Reduced for faster start
        {3, {"Archery", 20, 3, {}}},          // Reduced for faster start
        {4, {"Mining", 25, 4, {}}},           // Reduced for faster start
        {5, {"Sailing", 30, 5, {}}},          // Reduced for faster start
        {6, {"Calendar", 35, 6, {1}}},        // Reduced for faster start
        {7, {"Wheel", 40, 7, {2}}},           // Reduced for faster start
        {8, {"Masonry", 45, 8, {4}}},         // Reduced for faster start
        {9, {"Bronze Working", 50, 9, {4}}},  // Reduced for faster start
        {10, {"Irrigation", 55, 10, {2}}},    // Reduced for faster start
        {11, {"Writing", 60, 11, {1, 6}}},    // Reduced for faster start
        {12, {"Shipbuilding", 65, 12, {5}}},  // Reduced for faster start
        {13, {"Iron Working", 80, 13, {9}}},  // Reduced for faster start
        {14, {"Mathematics", 90, 14, {11}}},  // Reduced for faster start
        {15, {"Currency", 100, 15, {14}}},    // Reduced for faster start
        {16, {"Construction", 110, 16, {8}}}, // Reduced for faster start
        {17, {"Roads", 120, 17, {7}}},        // Reduced for faster start
        {18, {"Horseback Riding", 140, 18, {2, 7}}}, // Reduced for faster start
        {19, {"Alphabet", 150, 19, {11}}},    // Reduced for faster start
        {20, {"Agriculture", 160, 20, {10}}},
        {21, {"Drama and Poetry", 180, 21, {19}}},    // Massively reduced
        {22, {"Philosophy", 200, 22, {19}}},         // Massively reduced  
        {23, {"Engineering", 220, 23, {16, 17}}},    // Massively reduced
        {24, {"Optics", 240, 24, {14}}},             // Massively reduced
        {25, {"Metal Casting", 260, 25, {13}}},      // Massively reduced
        {26, {"Compass", 280, 26, {12, 14}}},        // Massively reduced
        {27, {"Democracy", 300, 27, {22}}},          // Massively reduced
        {28, {"Steel", 320, 28, {25}}},              // Massively reduced
        {29, {"Machinery", 340, 29, {23}}},          // Massively reduced
        {30, {"Education", 360, 30, {22}}},          // Massively reduced
        {31, {"Acoustics", 380, 31, {21, 24}}},      // Massively reduced
        {32, {"Civil Service", 400, 32, {15, 30}}},  // Massively reduced
        {33, {"Paper", 420, 33, {19}}},              // Massively reduced
        {34, {"Banking", 440, 34, {15, 32}}},        // Massively reduced
        {35, {"Printing", 460, 35, {33}}},           // Massively reduced
        {36, {"Gunpowder", 480, 36, {28}}},          // Massively reduced
        {37, {"Mechanical Clock", 500, 37, {29}}},   // Massively reduced
        {38, {"Universities", 520, 38, {30}}},       // Massively reduced
        {39, {"Astronomy", 540, 39, {24, 38}}},      // Massively reduced
        {40, {"Chemistry", 560, 40, {39}}},
        {41, {"Metallurgy", 580, 41, {28}}},         // Reduced for progression
        {42, {"Navigation", 600, 42, {26, 39}}},     // Reduced for progression
        {43, {"Architecture", 620, 43, {23, 31}}},   // Reduced for progression
        {44, {"Economics", 640, 44, {34}}},          // Reduced for progression
        {45, {"Printing Press", 660, 45, {35}}},     // Reduced for progression
        {46, {"Firearms", 680, 46, {36, 41}}},       // Reduced for progression
        {47, {"Physics", 700, 47, {39}}},            // Reduced for progression
        {48, {"Scientific Method", 720, 48, {47}}},  // Reduced for progression
        {49, {"Rifling", 740, 49, {46}}},            // Reduced for progression
        {50, {"Steam Engine", 760, 50, {47}}},       // Reduced for progression
        {51, {"Industrialization", 780, 51, {41, 50}}}, // Reduced for progression
        {52, {"Sanitation", 800, 52, {40}}},         // Reduced for progression
        {53, {"Vaccination", 820, 53, {40}}},        // Reduced for progression
        {54, {"Electricity", 840, 54, {48}}},        // Reduced for progression
        {55, {"Railroad", 860, 55, {50, 51}}},       // Reduced for progression
        {56, {"Dynamite", 880, 56, {40}}},           // Reduced for progression
        {57, {"Replaceable Parts", 900, 57, {51}}},  // Reduced for progression
        {58, {"Telegraph", 920, 58, {54}}},          // Reduced for progression
        {59, {"Telephone", 940, 59, {54}}},          // Reduced for progression
        {60, {"Combustion", 960, 60, {50}}},         // Reduced for progression
        {61, {"Flight", 980, 61, {60}}},             // Reduced for progression
        {62, {"Radio", 1000, 62, {58}}},             // Reduced for progression
        {63, {"Mass Production", 1020, 63, {57}}},   // Reduced for progression
        {64, {"Electronics", 1040, 64, {54}}},       // Smooth progression from industrial era
        {65, {"Penicillin", 1060, 65, {53}}},        // Smooth progression
        {66, {"Plastics", 1080, 66, {40}}},          // Smooth progression
        {67, {"Rocketry", 1100, 67, {61}}},          // Smooth progression
        {68, {"Nuclear Fission", 1120, 68, {47}}},   // Smooth progression
        {69, {"Computers", 1140, 69, {64}}},         // Computers unlock major bonus
        {70, {"Transistors", 1160, 70, {64}}},       // Smooth progression
        {71, {"Refrigeration", 1180, 71, {52}}},     // Smooth progression
        {72, {"Ecology", 1200, 72, {52}}},           // Smooth progression
        {73, {"Satellites", 1220, 73, {67}}},        // Smooth progression
        {74, {"Lasers", 1240, 74, {64}}},            // Smooth progression
        {75, {"Robotics", 1260, 75, {69}}},          // Smooth progression
        {76, {"Integrated Circuit", 1280, 76, {70}}}, // Required for mobile phones
        {77, {"Advanced Ballistics", 1300, 77, {49}}}, // Smooth progression
        {78, {"Superconductors", 1320, 78, {74}}},   // Smooth progression
        {79, {"Internet", 1340, 79, {69, 73}}},      // Internet unlocks major bonus  
        {80, {"Personal Computers", 1360, 80, {76}}}, // Smooth progression
        {81, {"Genetic Engineering", 1380, 81, {65}}}, // Smooth progression
        {82, {"Fiber Optics", 1400, 82, {74}}},      // Required for mobile phones
        {83, {"Mobile Phones", 1420, 83, {76, 82}}}, // Target: ~2000 CE
        {84, {"Stealth Technology", 1440, 84, {61, 78}}}, // Smooth progression
        {85, {"Artificial Intelligence", 1460, 85, {75, 80}}}, // Smooth progression
        {86, {"Nanotechnology", 1480, 86, {78}}},    // Smooth progression
        {87, {"Renewable Energy", 1500, 87, {72}}},  // Smooth progression
        {88, {"3D Printing", 1520, 88, {80}}},       // Smooth progression
        {89, {"Social Media", 1540, 89, {79}}},      // Smooth progression
        {90, {"Biotechnology", 1560, 90, {81}}},     // Smooth progression
        {91, {"Quantum Computing", 1580, 91, {85}}}, // Smooth progression
        {92, {"Blockchain", 1600, 92, {79}}},        // Smooth progression
        {93, {"Machine Learning", 1620, 93, {85}}},  // Smooth progression
        {94, {"Augmented Reality", 1640, 94, {80}}}, // Smooth progression
        {95, {"Virtual Reality", 1660, 95, {80}}}    // Target: ~2050 CE
    };
}

void TechnologyManager::updateCountry(Country& country) {
    // Check if the country can unlock any new technologies
    for (const auto& pair : m_technologies) {
        int techId = pair.first;
        const Technology& tech = pair.second;

        if (canUnlockTechnology(country, techId)) {
            if (country.getSciencePoints() >= tech.cost) {
                unlockTechnology(country, techId);
            }
        }
    }
}

bool TechnologyManager::canUnlockTechnology(const Country& country, int techId) const {
    // Check if the technology has already been unlocked
    auto itCountry = m_unlockedTechnologies.find(country.getCountryIndex());
    if (itCountry != m_unlockedTechnologies.end()) {
        const std::vector<int>& unlockedTechs = itCountry->second;
        if (std::find(unlockedTechs.begin(), unlockedTechs.end(), techId) != unlockedTechs.end()) {
            return false; // Already unlocked
        }
    }

    // Check if all required technologies are unlocked
    auto itTech = m_technologies.find(techId);
    if (itTech == m_technologies.end()) {
        return false; // Tech not found
    }

    const Technology& tech = itTech->second;
    for (int requiredTechId : tech.requiredTechs) {
        bool found = false;
        if (itCountry != m_unlockedTechnologies.end()) {
            const std::vector<int>& unlockedTechs = itCountry->second;
            if (std::find(unlockedTechs.begin(), unlockedTechs.end(), requiredTechId) != unlockedTechs.end()) {
                found = true;
            }
        }
        if (!found) {
            return false; // Required tech not unlocked
        }
    }

    return true;
}

void TechnologyManager::unlockTechnology(Country& country, int techId) {
    // Unlock the technology for the country
    m_unlockedTechnologies[country.getCountryIndex()].push_back(techId);

    // Deduct science points
    auto itTech = m_technologies.find(techId);
    if (itTech != m_technologies.end()) {
        const Technology& tech = itTech->second;
        country.setSciencePoints(country.getSciencePoints() - tech.cost);
        country.setSciencePoints(0); // Reset science points to 0 after unlocking
    }

    // 🧬 APPLY TECHNOLOGY EFFECTS
    country.applyTechnologyBonus(techId);
    
    // 🔧 DEBUG CONTROL: Only show message if debug mode is enabled
    if (s_debugMode) {
        cout << country.getName() << " unlocked technology: " << m_technologies.at(techId).name << endl;
    }
}

const std::unordered_map<int, Technology>& TechnologyManager::getTechnologies() const {
    return m_technologies;
}

const std::vector<int>& TechnologyManager::getUnlockedTechnologies(const Country& country) const {
    auto it = m_unlockedTechnologies.find(country.getCountryIndex());
    if (it != m_unlockedTechnologies.end()) {
        return it->second;
    }
    static const std::vector<int> emptyVec; // Return an empty vector if not found
    return emptyVec;
}