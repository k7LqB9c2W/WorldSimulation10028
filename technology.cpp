#include "technology.h"
#include "country.h"
#include <algorithm>
#include <iostream> // Make sure to include iostream if not already present
#include <cmath> // For std::llround
#include <unordered_set>

using namespace std;

// 🔧 STATIC DEBUG CONTROL: Default to OFF (no spam)
bool TechnologyManager::s_debugMode = false;

TechnologyManager::TechnologyManager() {
    initializeTechnologies();
    // Build sorted IDs vector for stable unlock ordering
    m_sortedIds.reserve(m_technologies.size());
    for (auto& kv : m_technologies) {
        m_sortedIds.push_back(kv.first);
    }
    std::sort(m_sortedIds.begin(), m_sortedIds.end());
}

void TechnologyManager::initializeTechnologies() {
    // BALANCED COSTS: Realistic progression from 5000 BCE to 2050 CE
    m_technologies = {
        {1, {"Pottery", 50, 1, {}}},
        {2, {"Animal Husbandry", 60, 2, {}}},
        {3, {"Archery", 70, 3, {}}},
        {4, {"Mining", 80, 4, {}}},
        {5, {"Sailing", 90, 5, {}}},
        {6, {"Calendar", 100, 6, {1}}},
        {7, {"Wheel", 120, 7, {2}}},
        {8, {"Masonry", 140, 8, {4}}},
        {9, {"Bronze Working", 160, 9, {4}}},
        {10, {"Irrigation", 180, 10, {2}}},
        {11, {"Writing", 200, 11, {1, 6}}},
        {12, {"Shipbuilding", 220, 12, {5}}},
        {13, {"Iron Working", 250, 13, {9}}},
        {14, {"Mathematics", 280, 14, {11}}},
        {15, {"Currency", 320, 15, {14}}},
        {16, {"Construction", 350, 16, {8}}},
        {17, {"Roads", 380, 17, {7}}},
        {18, {"Horseback Riding", 420, 18, {2, 7}}},
        {19, {"Alphabet", 450, 19, {11}}},
        {20, {"Agriculture", 500, 20, {10}}},
        {21, {"Drama and Poetry", 550, 21, {19}}},
        {22, {"Philosophy", 600, 22, {19}}},
        {23, {"Engineering", 700, 23, {16, 17}}},
        {24, {"Optics", 750, 24, {14}}},
        {25, {"Metal Casting", 800, 25, {13}}},
        {26, {"Compass", 900, 26, {12, 14}}},
        {27, {"Democracy", 1000, 27, {22}}},
        {28, {"Steel", 1100, 28, {25}}},
        {29, {"Machinery", 1200, 29, {23}}},
        {30, {"Education", 1300, 30, {22}}},
        {31, {"Acoustics", 1400, 31, {21, 24}}},
        {32, {"Civil Service", 1500, 32, {15, 30}}},
        {33, {"Paper", 1600, 33, {19}}},
        {34, {"Banking", 1700, 34, {15, 32}}},
        {35, {"Markets", 1750, 35, {15, 34}}}, // Trade markets - requires Currency and Banking
        {36, {"Printing", 1800, 36, {33}}},
        {37, {"Gunpowder", 2000, 37, {28}}},
        {38, {"Mechanical Clock", 2200, 38, {29}}},
        {39, {"Universities", 2400, 39, {30}}},
        {40, {"Astronomy", 2600, 40, {24, 39}}},
        {41, {"Chemistry", 2800, 41, {40}}},
        {42, {"Metallurgy", 3000, 42, {28}}},
        {43, {"Navigation", 3200, 43, {26, 40}}},
        {44, {"Architecture", 3400, 44, {23, 31}}},
        {45, {"Economics", 3600, 45, {34}}},
        {46, {"Printing Press", 3800, 46, {36}}},
        {47, {"Firearms", 4000, 47, {37, 42}}},
        {48, {"Physics", 4200, 48, {40}}},
        {49, {"Scientific Method", 4500, 49, {48}}},
        {50, {"Rifling", 4800, 50, {47}}},
        {51, {"Steam Engine", 5000, 51, {48}}},
        {52, {"Industrialization", 5500, 52, {42, 51}}},
        {52, {"Sanitation", 6000, 52, {40}}},
        {53, {"Vaccination", 6500, 53, {40}}},
        {54, {"Electricity", 7000, 54, {48}}},
        {55, {"Railroad", 7500, 55, {50, 51}}},
        {56, {"Dynamite", 8000, 56, {40}}},
        {57, {"Replaceable Parts", 8500, 57, {51}}},
        {58, {"Telegraph", 9000, 58, {54}}},
        {59, {"Telephone", 9500, 59, {54}}},
        {60, {"Combustion", 10000, 60, {50}}},
        {61, {"Flight", 11000, 61, {60}}},
        {62, {"Radio", 12000, 62, {58}}},
        {63, {"Mass Production", 13000, 63, {57}}},
        {64, {"Electronics", 14000, 64, {54}}},
        {65, {"Penicillin", 15000, 65, {53}}},
        {66, {"Plastics", 16000, 66, {40}}},
        {67, {"Rocketry", 17000, 67, {61}}},
        {68, {"Nuclear Fission", 18000, 68, {47}}},
        {69, {"Computers", 20000, 69, {64}}},
        {70, {"Transistors", 22000, 70, {64}}},
        {71, {"Refrigeration", 24000, 71, {52}}},
        {72, {"Ecology", 26000, 72, {52}}},
        {73, {"Satellites", 28000, 73, {67}}},
        {74, {"Lasers", 30000, 74, {64}}},
        {75, {"Robotics", 32000, 75, {69}}},
        {76, {"Integrated Circuit", 35000, 76, {70}}},
        {77, {"Advanced Ballistics", 38000, 77, {49}}},
        {78, {"Superconductors", 40000, 78, {74}}},
        {79, {"Internet", 45000, 79, {69, 73}}},
        {80, {"Personal Computers", 50000, 80, {76}}},
        {81, {"Genetic Engineering", 55000, 81, {65}}},
        {82, {"Fiber Optics", 60000, 82, {74}}},
        {83, {"Mobile Phones", 65000, 83, {76, 82}}},
        {84, {"Stealth Technology", 70000, 84, {61, 78}}},
        {85, {"Artificial Intelligence", 75000, 85, {75, 80}}},
        {86, {"Nanotechnology", 80000, 86, {78}}},
        {87, {"Renewable Energy", 85000, 87, {72}}},
        {88, {"3D Printing", 90000, 88, {80}}},
        {89, {"Social Media", 95000, 89, {79}}},
        {90, {"Biotechnology", 100000, 90, {81}}},
        {91, {"Quantum Computing", 110000, 91, {85}}},
        {92, {"Blockchain", 120000, 92, {79}}},
        {93, {"Machine Learning", 130000, 93, {85}}},
        {94, {"Augmented Reality", 140000, 94, {80}}},
        {95, {"Virtual Reality", 150000, 95, {80}}}
    };
}

void TechnologyManager::updateCountry(Country& country) {
    // Greedy loop: unlock all affordable technologies until no more progress
    bool progress = true;
    while (progress) {
        progress = false;
        for (int techId : m_sortedIds) {
            const Technology& tech = m_technologies.at(techId);
            if (canUnlockTechnology(country, techId) && country.getSciencePoints() >= tech.cost) {
                unlockTechnology(country, techId);
                progress = true;
                break; // restart because prereqs and science points changed
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

    // Deduct science points (keep overflow for multiple unlocks)
    auto itTech = m_technologies.find(techId);
    if (itTech != m_technologies.end()) {
        const Technology& tech = itTech->second;
        country.setSciencePoints(country.getSciencePoints() - tech.cost);
        // Removed: country.setSciencePoints(0); // Allow overflow research
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

const std::vector<int>& TechnologyManager::getSortedTechnologyIds() const {
    return m_sortedIds;
}

void TechnologyManager::setUnlockedTechnologiesForEditor(Country& country, const std::vector<int>& techIds, bool includePrerequisites) {
    std::unordered_set<int> requested;
    requested.reserve(techIds.size());
    for (int id : techIds) {
        if (m_technologies.find(id) != m_technologies.end()) {
            requested.insert(id);
        }
    }

    if (includePrerequisites) {
        std::vector<int> stack;
        stack.reserve(requested.size());
        for (int id : requested) {
            stack.push_back(id);
        }

        while (!stack.empty()) {
            int id = stack.back();
            stack.pop_back();
            auto it = m_technologies.find(id);
            if (it == m_technologies.end()) {
                continue;
            }
            for (int req : it->second.requiredTechs) {
                if (m_technologies.find(req) == m_technologies.end()) {
                    continue;
                }
                if (requested.insert(req).second) {
                    stack.push_back(req);
                }
            }
        }
    }

    std::vector<int> normalized;
    normalized.reserve(requested.size());
    for (int id : m_sortedIds) {
        if (requested.count(id)) {
            normalized.push_back(id);
        }
    }

    m_unlockedTechnologies[country.getCountryIndex()] = normalized;
    country.resetTechnologyBonuses();
    for (int id : normalized) {
        country.applyTechnologyBonus(id);
    }
}

// POPULATION SYSTEM HELPER IMPLEMENTATIONS

bool TechnologyManager::hasTech(const TechnologyManager& tm, const Country& c, int id) {
    const auto& v = tm.getUnlockedTechnologies(c);
    return std::find(v.begin(), v.end(), id) != v.end();
}

struct KBoost { int id; double mult; };

double TechnologyManager::techKMultiplier(const TechnologyManager& tm, const Country& c) {
    // Agriculture and land productivity technologies
    static const KBoost foodCluster[] = {
        {10, 1.06},   // Irrigation
        {20, 1.10},   // Agriculture
        {23, 1.05},   // Engineering
        {17, 1.03},   // Roads
        {32, 1.03},   // Civil Service
        {34, 1.03},   // Banking
        {44, 1.04},   // Economics
        {40, 1.12},   // Chemistry as fertilizer proxy
        {55, 1.20},   // Railroad
        {50, 1.15},   // Steam Engine
        {63, 1.10},   // Mass Production
        {57, 1.08},   // Replaceable Parts
        {71, 1.10},   // Refrigeration
        {65, 1.05},   // Penicillin improves survival and usable K
        {81, 1.08},   // Genetic Engineering
        {90, 1.07}    // Biotechnology
    };

    double m = 1.0;
    for (const auto& kb : foodCluster) {
        if (hasTech(tm, c, kb.id)) {
            m *= kb.mult;
        }
    }

    // Small extras for transport and trade
    if (hasTech(tm, c, 43)) m *= 1.02; // Navigation
    if (hasTech(tm, c, 58)) m *= 1.02; // Telegraph
    if (hasTech(tm, c, 59)) m *= 1.02; // Telephone
    if (hasTech(tm, c, 79)) m *= 1.01; // Internet

    return m;
}

struct RAdd { int id; double add; };     // raises r
struct RCap { int id; double mult; };    // reduces r as fertility falls

double TechnologyManager::techGrowthRateR(const TechnologyManager& tm, const Country& c) {
    // Baseline for early human populations
    double r = 0.0003; // 0.03% per year base rate

    // Modest bumps for early improvements to survival and granaries
    static const RAdd early[] = {
        {10, 0.00005},  // Irrigation
        {20, 0.00008},  // Agriculture
        {23, 0.00003},  // Engineering
        {32, 0.00002}   // Civil Service
    };
    for (auto e : early) {
        if (hasTech(tm, c, e.id)) {
            r += e.add;
        }
    }

    // Industrial and public health lift r materially
    static const RAdd industrial[] = {
        {50, 0.0006},   // Steam Engine
        {51, 0.0008},   // Industrialization
        {55, 0.0004},   // Railroad
        {52, 0.0010},   // Sanitation
        {53, 0.0010},   // Vaccination
        {54, 0.0005},   // Electricity
        {63, 0.0005},   // Mass Production
        {65, 0.0006}    // Penicillin
    };
    for (auto e : industrial) {
        if (hasTech(tm, c, e.id)) {
            r += e.add;
        }
    }

    // Fertility transition lowers r as education and modernity rise
    // Multiply down, do not subtract below zero
    double fertilityMult = 1.0;
    static const RCap transition[] = {
        {30, 0.92},   // Education
        {38, 0.95},   // Universities
        {44, 0.96},   // Economics
        {69, 0.92},   // Computers
        {80, 0.95},   // Personal Computers
        {79, 0.95},   // Internet
        {85, 0.97},   // Artificial Intelligence
    };
    for (auto t : transition) {
        if (hasTech(tm, c, t.id)) {
            fertilityMult *= t.mult;
        }
    }

    r *= fertilityMult;

    // Keep r in a sane envelope
    r = std::max(0.00005, std::min(r, 0.0200)); // 0.005% to 2.0% per year
    return r;
}
