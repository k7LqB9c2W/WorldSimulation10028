#include "technology.h"
#include "country.h"
#include <algorithm>
#include <iostream> // Make sure to include iostream if not already present

using namespace std;

TechnologyManager::TechnologyManager() {
    initializeTechnologies();
}

void TechnologyManager::initializeTechnologies() {
    m_technologies = {
        {1, {"Pottery", 25, 1, {}}},
        {2, {"Animal Husbandry", 25, 2, {}}},
        {3, {"Archery", 30, 3, {}}},
        {4, {"Mining", 35, 4, {}}},
        {5, {"Sailing", 40, 5, {}}},
        {6, {"Calendar", 45, 6, {1}}},
        {7, {"Wheel", 50, 7, {2}}},
        {8, {"Masonry", 55, 8, {4}}},
        {9, {"Bronze Working", 60, 9, {4}}},
        {10, {"Irrigation", 65, 10, {2}}},
        {11, {"Writing", 70, 11, {1, 6}}},
        {12, {"Shipbuilding", 75, 12, {5}}},
        {13, {"Iron Working", 80, 13, {9}}},
        {14, {"Mathematics", 85, 14, {11}}},
        {15, {"Currency", 90, 15, {14}}},
        {16, {"Construction", 95, 16, {8}}},
        {17, {"Roads", 100, 17, {7}}},
        {18, {"Horseback Riding", 110, 18, {2, 7}}},
        {19, {"Alphabet", 120, 19, {11}}},
        {20, {"Agriculture", 130, 20, {10}}},
        {21, {"Drama and Poetry", 140, 21, {19}}},
        {22, {"Philosophy", 150, 22, {19}}},
        {23, {"Engineering", 160, 23, {16, 17}}},
        {24, {"Optics", 170, 24, {14}}},
        {25, {"Metal Casting", 180, 25, {13}}},
        {26, {"Compass", 190, 26, {12, 14}}},
        {27, {"Democracy", 200, 27, {22}}},
        {28, {"Steel", 220, 28, {25}}},
        {29, {"Machinery", 240, 29, {23}}},
        {30, {"Education", 260, 30, {22}}},
        {31, {"Acoustics", 280, 31, {21, 24}}},
        {32, {"Civil Service", 300, 32, {15, 30}}},
        {33, {"Paper", 320, 33, {19}}},
        {34, {"Banking", 340, 34, {15, 32}}},
        {35, {"Printing", 360, 35, {33}}},
        {36, {"Gunpowder", 380, 36, {28}}},
        {37, {"Mechanical Clock", 400, 37, {29}}},
        {38, {"Universities", 420, 38, {30}}},
        {39, {"Astronomy", 440, 39, {24, 38}}},
        {40, {"Chemistry", 460, 40, {39}}},
        {41, {"Metallurgy", 480, 41, {28}}},
        {42, {"Navigation", 500, 42, {26, 39}}},
        {43, {"Architecture", 520, 43, {23, 31}}},
        {44, {"Economics", 540, 44, {34}}},
        {45, {"Printing Press", 560, 45, {35}}},
        {46, {"Firearms", 580, 46, {36, 41}}},
        {47, {"Physics", 600, 47, {39}}},
        {48, {"Scientific Method", 620, 48, {47}}},
        {49, {"Rifling", 640, 49, {46}}},
        {50, {"Steam Engine", 660, 50, {47}}},
        {51, {"Industrialization", 680, 51, {41, 50}}},
        {52, {"Sanitation", 700, 52, {40}}},
        {53, {"Vaccination", 720, 53, {40}}},
        {54, {"Electricity", 740, 54, {48}}},
        {55, {"Railroad", 760, 55, {50, 51}}},
        {56, {"Dynamite", 780, 56, {40}}},
        {57, {"Replaceable Parts", 800, 57, {51}}},
        {58, {"Telegraph", 820, 58, {54}}},
        {59, {"Telephone", 840, 59, {54}}},
        {60, {"Combustion", 860, 60, {50}}},
        {61, {"Flight", 880, 61, {60}}},
        {62, {"Radio", 900, 62, {58}}},
        {63, {"Mass Production", 920, 63, {57}}},
        {64, {"Electronics", 940, 64, {54}}},
        {65, {"Penicillin", 960, 65, {53}}},
        {66, {"Plastics", 980, 66, {40}}},
        {67, {"Rocketry", 1000, 67, {61}}},
        {68, {"Nuclear Fission", 1020, 68, {47}}},
        {69, {"Computers", 1040, 69, {64}}},
        {70, {"Transistors", 1060, 70, {64}}},
        {71, {"Refrigeration", 1080, 71, {52}}},
        {72, {"Ecology", 1100, 72, {52}}},
        {73, {"Satellites", 1120, 73, {67}}},
        {74, {"Lasers", 1140, 74, {64}}},
        {75, {"Robotics", 1160, 75, {69}}},
        {76, {"Integrated Circuit", 1180, 76, {70}}},
        {77, {"Advanced Ballistics", 1200, 77, {49}}},
        {78, {"Superconductors", 1220, 78, {74}}},
        {79, {"Internet", 1240, 79, {69, 73}}},
        {80, {"Personal Computers", 1260, 80, {76}}},
        {81, {"Genetic Engineering", 1280, 81, {65}}},
        {82, {"Fiber Optics", 1300, 82, {74}}},
        {83, {"Mobile Phones", 1320, 83, {76, 82}}},
        {84, {"Stealth Technology", 1340, 84, {61, 78}}},
        {85, {"Artificial Intelligence", 1360, 85, {75, 80}}},
        {86, {"Nanotechnology", 1380, 86, {78}}},
        {87, {"Renewable Energy", 1400, 87, {72}}},
        {88, {"3D Printing", 1420, 88, {80}}},
        {89, {"Social Media", 1440, 89, {79}}},
        {90, {"Biotechnology", 1460, 90, {81}}},
        {91, {"Quantum Computing", 1480, 91, {85}}},
        {92, {"Blockchain", 1500, 92, {79}}},
        {93, {"Machine Learning", 1520, 93, {85}}},
        {94, {"Augmented Reality", 1540, 94, {80}}},
        {95, {"Virtual Reality", 1560, 95, {80}}}
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

    // Add any immediate effects of the technology here (e.g., resource bonuses)
    // ... (Implementation for technology effects will be added later)
    cout << country.getName() << " unlocked technology: " << m_technologies.at(techId).name << endl; // Debug message
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