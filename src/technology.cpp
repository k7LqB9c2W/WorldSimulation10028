#include "technology.h"

#include "country.h"
#include "map.h"
#include "simulation_context.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <iostream>
#include <unordered_set>

bool TechnologyManager::s_debugMode = false;

TechnologyManager::TechnologyManager() {
    initializeTechnologies();

    m_sortedIds.clear();
    m_sortedIds.reserve(m_technologies.size());
    for (const auto& kv : m_technologies) {
        m_sortedIds.push_back(kv.first);
    }
    std::sort(m_sortedIds.begin(), m_sortedIds.end(), [&](int a, int b) {
        const Technology& ta = m_technologies.at(a);
        const Technology& tb = m_technologies.at(b);
        if (ta.order != tb.order) return ta.order < tb.order;
        return ta.id < tb.id;
    });

    m_denseTechIds = m_sortedIds;
    m_techIdToDense.clear();
    m_techIdToDense.reserve(m_denseTechIds.size());
    for (int i = 0; i < static_cast<int>(m_denseTechIds.size()); ++i) {
        m_techIdToDense[m_denseTechIds[static_cast<size_t>(i)]] = i;
    }
}

namespace {

std::string toLowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return s;
}

int domainForTechName(const std::string& name) {
    const std::string n = toLowerCopy(name);
    auto has = [&](const char* kw) { return n.find(kw) != std::string::npos; };

    // 0 Agriculture, 1 Materials, 2 Construction, 3 Navigation, 4 Governance,
    // 5 Medicine, 6 Education, 7 Warfare/Industry.
    if (has("agriculture") || has("irrigation") || has("husbandry") || has("calendar") ||
        has("refrigeration") || has("cultivation") || has("sedentism") || has("domestication")) {
        return 0;
    }
    if (has("sanitation") || has("vaccination") || has("penicillin") || has("genetic") ||
        has("biotechnology") || has("medicine")) {
        return 5;
    }
    if (has("education") || has("university") || has("universities") || has("writing") ||
        has("alphabet") || has("paper") || has("printing") || has("computer") ||
        has("internet") || has("telephone") || has("telegraph") || has("radio") ||
        has("mobile") || has("fiber optics") || has("integrated circuit") ||
        has("tokens") || has("tallies")) {
        return 6;
    }
    if (has("sailing") || has("ship") || has("compass") || has("navigation") || has("flight") ||
        has("satellite") || has("rocketry") || has("watercraft") || has("exchange networks")) {
        return 3;
    }
    if (has("democracy") || has("currency") || has("civil service") || has("banking") ||
        has("markets") || has("economics") || has("blockchain")) {
        return 4;
    }
    if (has("masonry") || has("construction") || has("engineering") || has("architecture") ||
        has("road") || has("railroad") || has("storage") || has("enclosures")) {
        return 2;
    }
    if (has("archery") || has("gunpowder") || has("firearms") || has("rifling") ||
        has("ballistics") || has("stealth") || has("dog domestication")) {
        return 7;
    }
    return 1;
}

std::string capabilityTagFor(int domainId) {
    switch (domainId) {
        case 0: return "ecological-intensification";
        case 1: return "materials-energy-conversion";
        case 2: return "construction-systems";
        case 3: return "long-range-logistics";
        case 4: return "bureaucracy-exchange-protocols";
        case 5: return "public-health-biocontrol";
        case 6: return "information-inference-systems";
        case 7: return "organized-force-projection";
        default: return "general-capability";
    }
}

} // namespace

void TechnologyManager::initializeTechnologies() {
    m_technologies.clear();

    // Core Paleolithic / Mesolithic section for deep starts.
    m_technologies.emplace(100, Technology{"Cordage and Knots", 25, 100, {}});
    m_technologies.emplace(101, Technology{"Hide Working and Tailored Clothing", 30, 101, {100}});
    m_technologies.emplace(102, Technology{"Hafted Stone Tools", 35, 102, {100}});
    m_technologies.emplace(103, Technology{"Bone and Antler Tools", 35, 103, {102}});
    m_technologies.emplace(104, Technology{"Fishing Technology", 45, 104, {100, 102}});
    m_technologies.emplace(105, Technology{"Food Preservation", 50, 105, {100, 101}});
    m_technologies.emplace(106, Technology{"Storage Pits and Containers", 60, 106, {105}});
    m_technologies.emplace(107, Technology{"Seasonal Aggregation Camps", 70, 107, {106}});
    m_technologies.emplace(108, Technology{"Watercraft", 75, 108, {104}});
    m_technologies.emplace(109, Technology{"Long-distance Exchange Networks", 85, 109, {108, 107}});
    m_technologies.emplace(110, Technology{"Dog Domestication", 90, 110, {102}});
    m_technologies.emplace(111, Technology{"Grinding Stones", 100, 111, {102}});
    m_technologies.emplace(112, Technology{"Proto-cultivation", 120, 112, {111, 107}});
    m_technologies.emplace(113, Technology{"Sedentism", 140, 113, {106, 107}});
    m_technologies.emplace(114, Technology{"Enclosures and Herd Management", 150, 114, {113}});
    m_technologies.emplace(115, Technology{"Counting Tokens and Tallies", 170, 115, {113, 106}});
    m_technologies.emplace(116, Technology{"Charcoal Firing", 180, 116, {111, 106}});
    m_technologies.emplace(117, Technology{"Proto-writing and Administrative Notation", 220, 117, {115, 113}});
    m_technologies.emplace(118, Technology{"Numeracy and Measurement", 240, 118, {115, 117}});
    m_technologies.emplace(119, Technology{"Native Copper Working", 150, 119, {4, 116}});
    m_technologies.emplace(120, Technology{"Copper Smelting", 190, 120, {119, 116}});

    // Existing tree with prerequisite rewires for realistic deep-start pacing.
    m_technologies.emplace(1, Technology{"Pottery", 50, 1, {106, 113, 116}});
    m_technologies.emplace(2, Technology{"Animal Husbandry", 60, 2, {110, 113, 114}});
    m_technologies.emplace(3, Technology{"Archery", 70, 3, {102}});
    m_technologies.emplace(4, Technology{"Mining", 80, 4, {102, 111}});
    m_technologies.emplace(5, Technology{"Sailing", 90, 5, {108}});
    m_technologies.emplace(6, Technology{"Calendar", 100, 6, {113}});
    m_technologies.emplace(7, Technology{"Wheel", 120, 7, {113, 114}});
    m_technologies.emplace(8, Technology{"Masonry", 140, 8, {4}});
    m_technologies.emplace(9, Technology{"Bronze Alloying", 220, 9, {120}});
    m_technologies.emplace(10, Technology{"Irrigation", 180, 10, {20}});
    m_technologies.emplace(11, Technology{"Writing", 250, 11, {117}});
    m_technologies.emplace(12, Technology{"Shipbuilding", 220, 12, {5}});
    m_technologies.emplace(13, Technology{"Iron Working", 250, 13, {9}});
    m_technologies.emplace(14, Technology{"Formal Mathematics", 340, 14, {118, 11}});
    m_technologies.emplace(15, Technology{"Currency", 380, 15, {118, 115}});
    m_technologies.emplace(16, Technology{"Construction", 350, 16, {8}});
    m_technologies.emplace(17, Technology{"Roads", 380, 17, {7}});
    m_technologies.emplace(18, Technology{"Horseback Riding", 420, 18, {2, 7}});
    m_technologies.emplace(19, Technology{"Alphabet", 420, 19, {11}});
    m_technologies.emplace(20, Technology{"Agriculture", 500, 20, {112, 113}});
    m_technologies.emplace(21, Technology{"Drama and Poetry", 550, 21, {19}});
    m_technologies.emplace(22, Technology{"Philosophy", 540, 22, {19}});
    m_technologies.emplace(23, Technology{"Engineering", 700, 23, {16, 17}});
    m_technologies.emplace(24, Technology{"Optics", 750, 24, {14}});
    m_technologies.emplace(25, Technology{"Metal Casting", 800, 25, {13}});
    m_technologies.emplace(26, Technology{"Compass", 900, 26, {12, 14}});
    m_technologies.emplace(27, Technology{"Democracy", 1000, 27, {22}});
    m_technologies.emplace(28, Technology{"Steel", 1100, 28, {25}});
    m_technologies.emplace(29, Technology{"Machinery", 1200, 29, {23}});
    m_technologies.emplace(30, Technology{"Education", 1150, 30, {22}});
    m_technologies.emplace(31, Technology{"Acoustics", 1400, 31, {21, 24}});
    m_technologies.emplace(32, Technology{"Civil Service", 1500, 32, {15, 30}});
    m_technologies.emplace(33, Technology{"Paper", 1600, 33, {19}});
    m_technologies.emplace(34, Technology{"Banking", 1700, 34, {15, 32}});
    m_technologies.emplace(35, Technology{"Markets", 1750, 35, {15, 34}});
    m_technologies.emplace(36, Technology{"Printing", 1800, 36, {33}});
    m_technologies.emplace(37, Technology{"Gunpowder", 2000, 37, {28}});
    m_technologies.emplace(38, Technology{"Mechanical Clock", 2200, 38, {29}});
    m_technologies.emplace(39, Technology{"Universities", 2100, 39, {30}});
    m_technologies.emplace(40, Technology{"Astronomy", 2600, 40, {24, 39}});
    m_technologies.emplace(41, Technology{"Chemistry", 2800, 41, {40}});
    m_technologies.emplace(42, Technology{"Metallurgy", 3000, 42, {28, 4, 116}});
    m_technologies.emplace(43, Technology{"Navigation", 3200, 43, {26, 40}});
    m_technologies.emplace(44, Technology{"Architecture", 3400, 44, {23, 31}});
    m_technologies.emplace(45, Technology{"Economics", 3600, 45, {34}});
    m_technologies.emplace(46, Technology{"Printing Press", 3800, 46, {36}});
    m_technologies.emplace(47, Technology{"Firearms", 4000, 47, {37, 42}});
    m_technologies.emplace(48, Technology{"Physics", 4200, 48, {40}});
    m_technologies.emplace(49, Technology{"Scientific Method", 4500, 49, {48}});
    m_technologies.emplace(50, Technology{"Rifling", 4800, 50, {47}});
    m_technologies.emplace(51, Technology{"Steam Engine", 5000, 51, {48}});
    m_technologies.emplace(52, Technology{"Industrialization", 5500, 52, {42, 51}});
    m_technologies.emplace(53, Technology{"Vaccination", 6500, 53, {40}});
    m_technologies.emplace(54, Technology{"Electricity", 7000, 54, {48}});
    m_technologies.emplace(55, Technology{"Railroad", 7500, 55, {50, 51}});
    m_technologies.emplace(56, Technology{"Dynamite", 8000, 56, {40}});
    m_technologies.emplace(57, Technology{"Replaceable Parts", 8500, 57, {51}});
    m_technologies.emplace(58, Technology{"Telegraph", 9000, 58, {54}});
    m_technologies.emplace(59, Technology{"Telephone", 9500, 59, {54}});
    m_technologies.emplace(60, Technology{"Combustion", 10000, 60, {50}});
    m_technologies.emplace(61, Technology{"Flight", 11000, 61, {60}});
    m_technologies.emplace(62, Technology{"Radio", 12000, 62, {58}});
    m_technologies.emplace(63, Technology{"Mass Production", 13000, 63, {57}});
    m_technologies.emplace(64, Technology{"Electronics", 14000, 64, {54}});
    m_technologies.emplace(65, Technology{"Penicillin", 15000, 65, {53}});
    m_technologies.emplace(66, Technology{"Plastics", 16000, 66, {40}});
    m_technologies.emplace(67, Technology{"Rocketry", 17000, 67, {61}});
    m_technologies.emplace(68, Technology{"Nuclear Fission", 18000, 68, {47}});
    m_technologies.emplace(69, Technology{"Computers", 20000, 69, {64}});
    m_technologies.emplace(70, Technology{"Transistors", 22000, 70, {64}});
    m_technologies.emplace(71, Technology{"Refrigeration", 24000, 71, {52}});
    m_technologies.emplace(72, Technology{"Ecology", 26000, 72, {52}});
    m_technologies.emplace(73, Technology{"Satellites", 28000, 73, {67}});
    m_technologies.emplace(74, Technology{"Lasers", 30000, 74, {64}});
    m_technologies.emplace(75, Technology{"Robotics", 32000, 75, {69}});
    m_technologies.emplace(76, Technology{"Integrated Circuit", 35000, 76, {70}});
    m_technologies.emplace(77, Technology{"Advanced Ballistics", 38000, 77, {49}});
    m_technologies.emplace(78, Technology{"Superconductors", 40000, 78, {74}});
    m_technologies.emplace(79, Technology{"Internet", 45000, 79, {69, 73}});
    m_technologies.emplace(80, Technology{"Personal Computers", 50000, 80, {76}});
    m_technologies.emplace(81, Technology{"Genetic Engineering", 55000, 81, {65}});
    m_technologies.emplace(82, Technology{"Fiber Optics", 60000, 82, {74}});
    m_technologies.emplace(83, Technology{"Mobile Phones", 65000, 83, {76, 82}});
    m_technologies.emplace(84, Technology{"Stealth Technology", 70000, 84, {61, 78}});
    m_technologies.emplace(85, Technology{"Artificial Intelligence", 75000, 85, {75, 80}});
    m_technologies.emplace(86, Technology{"Nanotechnology", 80000, 86, {78}});
    m_technologies.emplace(87, Technology{"Renewable Energy", 85000, 87, {72}});
    m_technologies.emplace(88, Technology{"3D Printing", 90000, 88, {80}});
    m_technologies.emplace(89, Technology{"Social Media", 95000, 89, {79}});
    m_technologies.emplace(90, Technology{"Biotechnology", 100000, 90, {81}});
    m_technologies.emplace(91, Technology{"Quantum Computing", 110000, 91, {85}});
    m_technologies.emplace(92, Technology{"Blockchain", 120000, 92, {79}});
    m_technologies.emplace(93, Technology{"Machine Learning", 130000, 93, {85}});
    m_technologies.emplace(94, Technology{"Augmented Reality", 140000, 94, {80}});
    m_technologies.emplace(95, Technology{"Virtual Reality", 150000, 95, {80}});
    m_technologies.emplace(96, Technology{"Sanitation", 6000, 96, {40}});

    auto mark = [&](int id, int order, double difficulty, bool keyTransition) {
        auto it = m_technologies.find(id);
        if (it == m_technologies.end()) return;
        it->second.order = order;
        it->second.difficulty = difficulty;
        it->second.isKeyTransition = keyTransition;
    };

    // Early progression orders.
    mark(100, 10, 0.2, false);
    mark(101, 20, 0.25, false);
    mark(102, 30, 0.25, false);
    mark(103, 40, 0.3, false);
    mark(104, 50, 0.35, false);
    mark(105, 60, 0.35, false);
    mark(106, 70, 0.4, false);
    mark(107, 80, 0.45, true);
    mark(108, 85, 0.5, true);
    mark(109, 90, 0.55, true);
    mark(110, 95, 0.45, false);
    mark(111, 110, 0.6, false);
    mark(112, 130, 0.8, true);
    mark(113, 150, 0.9, true);
    mark(114, 155, 1.0, false);
    mark(115, 160, 1.0, true);
    mark(116, 165, 1.1, true);
    mark(117, 175, 1.2, true);
    mark(118, 190, 1.3, true);
    mark(119, 185, 1.25, true);
    mark(120, 205, 1.4, true);

    // Re-anchor core early historical transitions.
    mark(1, 180, 1.1, true);   // Pottery
    mark(2, 195, 1.2, true);   // Animal husbandry
    mark(4, 200, 1.2, true);   // Mining
    mark(20, 220, 1.35, true); // Agriculture
    mark(11, 275, 1.7, true);  // Writing
    mark(42, 560, 2.2, true);  // Metallurgy

    for (auto& kv : m_technologies) {
        Technology& t = kv.second;
        t.threshold = static_cast<double>(t.cost);
        t.domainId = domainForTechName(t.name);
        t.capabilityTag = capabilityTagFor(t.domainId);
        if (t.order == 0) {
            t.order = 300 + t.id * 10;
        }
        if (t.difficulty <= 0.0) {
            const double complexity = std::clamp(std::log10(1.0 + t.threshold) / 4.8, 0.10, 3.5);
            t.difficulty = complexity;
        }
    }

    // Feasibility gates.
    auto gate = [&](int id,
                    bool requiresCoast,
                    bool requiresRiverOrWet,
                    double minClimateFood,
                    double minFarm,
                    double minForage,
                    double minOre,
                    double minEnergy,
                    double minConstr,
                    double minInstitution,
                    double minSpec,
                    double minPlant,
                    double minHerd) {
        auto it = m_technologies.find(id);
        if (it == m_technologies.end()) return;
        Technology& t = it->second;
        t.requiresCoast = requiresCoast;
        t.requiresRiverOrWetland = requiresRiverOrWet;
        t.minClimateFoodMult = minClimateFood;
        t.minFarmingPotential = minFarm;
        t.minForagingPotential = minForage;
        t.minOreAvail = minOre;
        t.minEnergyAvail = minEnergy;
        t.minConstructionAvail = minConstr;
        t.minInstitution = minInstitution;
        t.minSpecialization = minSpec;
        t.minPlantDomestication = minPlant;
        t.minHerdDomestication = minHerd;
    };

    gate(104, false, true, 0.45, 0.0, 120.0, 0.0, 0.0, 0.0, 0.0, 0.00, 0.0, 0.0);
    gate(108, true, true, 0.40, 0.0, 100.0, 0.0, 0.0, 0.0, 0.0, 0.00, 0.0, 0.0);
    gate(112, false, true, 0.55, 220.0, 90.0, 0.0, 0.0, 0.0, 0.02, 0.01, 0.28, 0.0);
    gate(113, false, false, 0.52, 0.0, 180.0, 0.0, 0.0, 0.0, 0.03, 0.02, 0.18, 0.0);
    gate(20, false, false, 0.62, 340.0, 0.0, 0.0, 0.0, 0.0, 0.06, 0.05, 0.40, 0.0);
    gate(2, false, false, 0.48, 0.0, 140.0, 0.0, 0.0, 0.0, 0.04, 0.02, 0.0, 0.35);
    gate(1, false, false, 0.50, 0.0, 0.0, 0.0, 0.08, 0.08, 0.04, 0.02, 0.0, 0.0);
    gate(4, false, false, 0.0, 0.0, 0.0, 0.20, 0.08, 0.0, 0.02, 0.01, 0.0, 0.0);
    gate(9, false, false, 0.0, 0.0, 0.0, 0.30, 0.22, 0.12, 0.08, 0.05, 0.0, 0.0);
    gate(117, false, false, 0.50, 80.0, 0.0, 0.0, 0.0, 0.02, 0.07, 0.04, 0.0, 0.0);
    gate(118, false, false, 0.50, 100.0, 0.0, 0.0, 0.0, 0.03, 0.09, 0.05, 0.0, 0.0);
    gate(119, false, false, 0.0, 0.0, 0.0, 0.22, 0.12, 0.08, 0.04, 0.02, 0.0, 0.0);
    gate(120, false, false, 0.0, 0.0, 0.0, 0.26, 0.18, 0.10, 0.06, 0.03, 0.0, 0.0);
    gate(42, false, false, 0.0, 0.0, 0.0, 0.36, 0.34, 0.18, 0.12, 0.08, 0.0, 0.0);
    gate(11, false, false, 0.53, 120.0, 0.0, 0.0, 0.0, 0.05, 0.14, 0.07, 0.0, 0.0);
    gate(43, true, false, 0.0, 0.0, 0.0, 0.0, 0.12, 0.0, 0.20, 0.06, 0.0, 0.0);
}

double TechnologyManager::smooth01(double x) {
    const double t = std::clamp(x, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

std::uint64_t TechnologyManager::techEventKey(int countryIndex, int denseTech) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(countryIndex)) << 32u) |
           static_cast<std::uint32_t>(denseTech);
}

double TechnologyManager::deterministicUnit(std::uint64_t worldSeed,
                                            int currentYear,
                                            int countryIndex,
                                            int denseTech,
                                            std::uint64_t salt) const {
    const std::uint64_t c = static_cast<std::uint64_t>(std::max(0, countryIndex) + 1);
    const std::uint64_t y = static_cast<std::uint64_t>(static_cast<std::int64_t>(currentYear) + 20000);
    const std::uint64_t t = static_cast<std::uint64_t>(std::max(0, denseTech) + 1);
    const std::uint64_t mixed = SimulationContext::mix64(
        worldSeed ^
        (c * 0x9E3779B97F4A7C15ull) ^
        (y * 0xBF58476D1CE4E5B9ull) ^
        (t * 0x94D049BB133111EBull) ^
        salt);
    return SimulationContext::u01FromU64(mixed);
}

void TechnologyManager::ensureCountryState(Country& country) const {
    country.ensureTechStateSize(static_cast<int>(m_denseTechIds.size()));
}

int TechnologyManager::getTechDenseIndex(int techId) const {
    const auto it = m_techIdToDense.find(techId);
    if (it == m_techIdToDense.end()) return -1;
    return it->second;
}

int TechnologyManager::getTechIdFromDenseIndex(int denseIndex) const {
    if (denseIndex < 0 || denseIndex >= static_cast<int>(m_denseTechIds.size())) {
        return -1;
    }
    return m_denseTechIds[static_cast<size_t>(denseIndex)];
}

int TechnologyManager::getTechCount() const {
    return static_cast<int>(m_denseTechIds.size());
}

bool TechnologyManager::countryKnowsTech(const Country& country, int techId) const {
    const int dense = getTechDenseIndex(techId);
    if (dense < 0) return false;
    return country.knowsTechDense(dense);
}

float TechnologyManager::countryTechAdoption(const Country& country, int techId) const {
    const int dense = getTechDenseIndex(techId);
    if (dense < 0) return 0.0f;
    return country.adoptionDense(dense);
}

bool TechnologyManager::hasAdoptedTech(const Country& country, int techId) const {
    const int dense = getTechDenseIndex(techId);
    if (dense < 0) return false;
    return country.adoptionDense(dense) >= 0.65f;
}

bool TechnologyManager::prerequisitesKnown(const Country& country, const Technology& tech) const {
    for (int req : tech.requiredTechs) {
        const int d = getTechDenseIndex(req);
        if (d < 0 || !country.knowsTechDense(d)) {
            return false;
        }
    }
    return true;
}

bool TechnologyManager::prerequisitesAdopted(const Country& country,
                                             const Technology& tech,
                                             double thresholdScale) const {
    const double threshold = std::clamp(0.65 * thresholdScale, 0.15, 0.95);
    for (int req : tech.requiredTechs) {
        const int d = getTechDenseIndex(req);
        if (d < 0 || country.adoptionDense(d) < threshold) {
            return false;
        }
    }
    return true;
}

bool TechnologyManager::isFeasible(const Country&,
                                   const Technology& tech,
                                   const CountryTechSignals& s) const {
    if (tech.requiresCoast && s.coastAccessRatio <= 0.03) return false;
    if (tech.requiresRiverOrWetland && s.riverWetlandShare <= 0.06) return false;
    if (s.climateFoodMult < tech.minClimateFoodMult) return false;
    if (s.farmingPotential < tech.minFarmingPotential) return false;
    if (s.foragingPotential < tech.minForagingPotential) return false;
    if (s.oreAvail < tech.minOreAvail) return false;
    if (s.energyAvail < tech.minEnergyAvail) return false;
    if (s.constructionAvail < tech.minConstructionAvail) return false;
    if (s.institution < tech.minInstitution) return false;
    if (s.specialization < tech.minSpecialization) return false;
    if (s.plantDomesticationPotential < tech.minPlantDomestication) return false;
    if (s.herdDomesticationPotential < tech.minHerdDomestication) return false;
    return true;
}

void TechnologyManager::refreshUnlockedFromAdoption(Country& country, double adoptionThreshold) {
    std::vector<int> adopted;
    adopted.reserve(m_sortedIds.size());
    for (int id : m_sortedIds) {
        const int dense = getTechDenseIndex(id);
        if (dense < 0) continue;
        if (country.adoptionDense(dense) >= adoptionThreshold) {
            adopted.push_back(id);
        }
    }
    m_unlockedTechnologies[country.getCountryIndex()] = std::move(adopted);
}

void TechnologyManager::recomputeCountryTechEffects(Country& country, double adoptionThreshold) const {
    country.resetTechnologyBonuses();
    for (int id : m_sortedIds) {
        const int dense = getTechDenseIndex(id);
        if (dense < 0) continue;
        const double a = static_cast<double>(country.adoptionDense(dense));
        if (a <= 0.001) continue;
        const double scale = smooth01(a / std::max(0.05, adoptionThreshold));
        if (scale <= 0.0) continue;
        country.applyTechnologyBonus(id, scale);
    }
}

void TechnologyManager::maybeRecordMilestoneEvents(const Country& country,
                                                   const Technology& tech,
                                                   int denseTech,
                                                   bool becameKnown,
                                                   bool crossedAdoption,
                                                   int currentYear) {
    const std::uint64_t k = techEventKey(country.getCountryIndex(), denseTech);
    if (becameKnown) {
        m_firstKnownYear.emplace(k, currentYear);
        if (s_debugMode && tech.isKeyTransition && country.getCountryIndex() < 3) {
            std::cout << "[TechDiscovery] y=" << currentYear
                      << " country=" << country.getName()
                      << " tech=" << tech.name << "\n";
        }
    }
    if (crossedAdoption) {
        m_firstAdoptionYear.emplace(k, currentYear);
        if (s_debugMode && tech.isKeyTransition && country.getCountryIndex() < 3) {
            std::cout << "[TechAdoption] y=" << currentYear
                      << " country=" << country.getName()
                      << " tech=" << tech.name << "\n";
        }
    }
}

void TechnologyManager::updateCountry(Country& country, const Map& map) {
    (void)map;
    ensureCountryState(country);
    const double threshold = std::clamp(map.getConfig().tech.adoptionThreshold, 0.10, 0.95);
    refreshUnlockedFromAdoption(country, threshold);
    recomputeCountryTechEffects(country, threshold);
}

bool TechnologyManager::canUnlockTechnology(const Country& country, int techId) const {
    const auto it = m_technologies.find(techId);
    if (it == m_technologies.end()) {
        return false;
    }
    const int dense = getTechDenseIndex(techId);
    if (dense < 0) {
        return false;
    }
    if (country.adoptionDense(dense) >= 0.999f) {
        return false;
    }
    // Editor-facing strict causal prerequisite rule (no bypass).
    for (int req : it->second.requiredTechs) {
        const int rd = getTechDenseIndex(req);
        if (rd < 0 || country.adoptionDense(rd) < 0.65f) {
            return false;
        }
    }
    return true;
}

void TechnologyManager::unlockTechnology(Country& country, int techId) {
    const auto it = m_technologies.find(techId);
    if (it == m_technologies.end()) {
        return;
    }
    ensureCountryState(country);
    const int dense = getTechDenseIndex(techId);
    if (dense < 0) {
        return;
    }

    country.setKnownTechDense(dense, true);
    country.setAdoptionDense(dense, 1.0f);
    country.setLowAdoptionYearsDense(dense, 0);

    refreshUnlockedFromAdoption(country, 0.65);
    recomputeCountryTechEffects(country, 0.65);

    if (s_debugMode) {
        std::cout << country.getName() << " unlocked technology: " << it->second.name << "\n";
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
    static const std::vector<int> empty;
    return empty;
}

const std::vector<int>& TechnologyManager::getSortedTechnologyIds() const {
    return m_sortedIds;
}

void TechnologyManager::tickYear(std::vector<Country>& countries,
                                 const Map& map,
                                 const std::vector<float>* tradeIntensityMatrix,
                                 int currentYear,
                                 int dtYears) {
    const int years = std::max(1, dtYears);
    const double yearsD = static_cast<double>(years);

    const int n = static_cast<int>(countries.size());
    if (n <= 0) return;

    const SimulationConfig& cfg = map.getConfig();
    const double adoptionThreshold = std::clamp(cfg.tech.adoptionThreshold, 0.10, 0.95);
    const double prereqAdoptScale = std::clamp(cfg.tech.prereqAdoptionFraction, 0.25, 1.0);
    const std::uint64_t worldSeed = map.getWorldSeed();

    auto clamp01 = [](double v) { return std::clamp(v, 0.0, 1.0); };
    auto traitDistance = [](const Country& a, const Country& b) {
        double sumSq = 0.0;
        for (int k = 0; k < Country::kTraits; ++k) {
            const double da = a.getTraits()[static_cast<size_t>(k)];
            const double db = b.getTraits()[static_cast<size_t>(k)];
            const double d = da - db;
            sumSq += d * d;
        }
        return std::sqrt(sumSq / static_cast<double>(Country::kTraits));
    };

    // Ensure dense per-country tech state is allocated.
    for (Country& c : countries) {
        ensureCountryState(c);
        if (c.getPopulation() <= 0) {
            for (int d = 0; d < getTechCount(); ++d) {
                c.setAdoptionDense(d, 0.0f);
                c.setLowAdoptionYearsDense(d, 0);
            }
            refreshUnlockedFromAdoption(c, adoptionThreshold);
            recomputeCountryTechEffects(c, adoptionThreshold);
        }
    }

    // ---- Domain knowledge innovation/diffusion (existing backbone) ----
    std::vector<Country::KnowledgeVec> delta(static_cast<size_t>(n), Country::KnowledgeVec{});

    for (int i = 0; i < n; ++i) {
        Country& c = countries[static_cast<size_t>(i)];
        if (c.getPopulation() <= 0) {
            c.setInnovationRate(0.0);
            continue;
        }

        const double pop = static_cast<double>(std::max<long long>(1, c.getPopulation()));
        const double urban = clamp01(c.getTotalCityPopulation() / pop);
        const double stability = clamp01(c.getStability());
        const double legitimacy = clamp01(c.getLegitimacy());
        const double access = clamp01(c.getMarketAccess());
        const double eduShare = clamp01(c.getEducationSpendingShare());
        const double healthShare = clamp01(c.getHealthSpendingShare());
        const double admin = clamp01(c.getAdminCapacity());
        const double control = clamp01(c.getAvgControl());

        const auto& m = c.getMacroEconomy();
        const double orePot = std::max(0.0, map.getCountryOrePotential(i));
        const double energyPot = std::max(0.0, map.getCountryEnergyPotential(i));
        const double constructionPot = std::max(0.0, map.getCountryConstructionPotential(i));
        auto sat = [](double x, double s) {
            const double d = std::max(1e-9, s);
            const double v = std::max(0.0, x);
            return v / (v + d);
        };
        const double resourceScale = 50.0 + 0.0002 * pop;
        const double oreSat = sat(orePot, resourceScale * std::max(0.5, cfg.resources.oreNormalization / 120.0));
        const double energySat = sat(energyPot, resourceScale * std::max(0.5, cfg.resources.energyNormalization / 120.0));
        const double constructionSat = sat(constructionPot, resourceScale * std::max(0.5, cfg.resources.constructionNormalization / 120.0));
        const double resourceReqE = std::clamp(cfg.tech.resourceReqEnergy, 0.05, 2.0);
        const double resourceReqO = std::clamp(cfg.tech.resourceReqOre, 0.05, 2.0);
        const double resourceReqC = std::clamp(cfg.tech.resourceReqConstruction, 0.05, 2.0);
        const double resourceGate = clamp01(std::min({energySat / resourceReqE, oreSat / resourceReqO, constructionSat / resourceReqC}));
        const double popScale = std::min(2.2, 0.30 + 0.24 * std::log1p(pop / 50000.0));
        const double nonFoodSurplus = std::max(0.0, m.lastNonFoodOutput - m.lastNonFoodCons);
        const double surplusPc = nonFoodSurplus / pop;
        const double surplusFactor = clamp01(surplusPc / 0.00085);
        const double foodSecurity = clamp01(m.foodSecurity);
        const double open = clamp01(c.getTraits()[5]);

        const double craftBase = 1.1;
        const double craftPop = std::min(2.0, 0.35 + 0.25 * std::log1p(pop / 20000.0));
        const double contact = 0.60 + 0.40 * (0.5 * access + 0.5 * open);
        const double order = (0.35 + 0.65 * stability) * (0.40 + 0.60 * legitimacy);
        const double survival = 0.55 + 0.45 * foodSecurity;
        const double warPenalty = c.isAtWar() ? 0.90 : 1.0;
        const double baselineCraft = craftBase * craftPop * contact * order * survival * warPenalty;

        double adv = 12.0 * surplusFactor;
        adv += 1.0 * urban;

        {
            double infra = std::max(0.0, c.getKnowledgeInfra());

            const double eduTerm = 0.05 + 0.95 * eduShare;
            double inst = 1.0;
            if (TechnologyManager::hasTech(*this, c, TechId::WRITING)) inst += 0.25;
            if (TechnologyManager::hasTech(*this, c, TechId::EDUCATION)) inst += 0.35;
            if (TechnologyManager::hasTech(*this, c, TechId::UNIVERSITIES)) inst += 0.25;
            if (TechnologyManager::hasTech(*this, c, TechId::SCIENTIFIC_METHOD)) inst += 0.45;
            if (TechnologyManager::hasTech(*this, c, 54)) inst += 0.20;
            if (TechnologyManager::hasTech(*this, c, 69)) inst += 0.18;
            if (TechnologyManager::hasTech(*this, c, 79)) inst += 0.12;

            const double infraUp =
                18.0 * eduTerm *
                (0.35 + 0.65 * stability) *
                (0.35 + 0.65 * admin) *
                (0.25 + 0.75 * urban) *
                (0.25 + 0.75 * access) *
                inst;

            double chaos = 0.0;
            if (c.isAtWar()) chaos += 1.0;
            chaos += 1.0 * (1.0 - control);
            chaos += 1.4 * std::max(0.0, 0.92 - clamp01(m.foodSecurity));
            chaos += 0.8 * std::max(0.0, 0.55 - legitimacy);

            const double infraDecay = 5.0 * chaos;
            infra = std::max(0.0, infra + (infraUp - infraDecay) * yearsD);
            c.setKnowledgeInfra(infra);

            const double infraFactor = (1.0 + 0.16 * std::log1p(infra));
            adv *= infraFactor;
        }

        adv *= (0.22 + 0.78 * access);
        adv *= (0.35 + 0.65 * stability);
        adv *= (0.40 + 0.60 * legitimacy);
        adv *= (0.25 + 0.75 * urban);
        adv *= (0.75 + 0.70 * eduShare);
        adv *= popScale;
        if (foodSecurity < 0.95) {
            adv *= (0.80 + 0.20 * foodSecurity);
        }

        adv *= (0.25 + 0.75 * clamp01(m.humanCapital));
        adv *= (0.20 + 0.80 * clamp01(m.knowledgeStock));
        adv *= (0.20 + 0.80 * clamp01(m.connectivityIndex));
        adv *= (0.30 + 0.70 * clamp01(m.institutionCapacity));
        adv *= (1.0 - 0.45 * clamp01(m.inequality));
        adv *= (0.28 + 0.72 * resourceGate);

        const double totalInnovPerYear = std::max(0.0, baselineCraft + std::max(0.0, adv));
        c.setInnovationRate(totalInnovPerYear);

        double w[Country::kDomains] = {1, 1, 1, 1, 1, 1, 1, 1};
        if (m.foodSecurity < 0.90) w[0] += 1.6;
        if (!c.getPorts().empty()) w[3] += 0.7;
        if (c.isAtWar()) w[7] += 1.2;
        w[4] += 0.6 * clamp01(c.getAdminSpendingShare());
        w[6] += 1.8 * eduShare;
        w[5] += 1.2 * healthShare;

        double sumW = 0.0;
        for (double wd : w) sumW += wd;
        if (sumW <= 0.0) sumW = 1.0;

        Country::KnowledgeVec& k = c.getKnowledgeMutable();
        for (int d = 0; d < Country::kDomains; ++d) {
            k[static_cast<size_t>(d)] = std::max(0.0, k[static_cast<size_t>(d)] + (totalInnovPerYear * (w[d] / sumW) * yearsD));
        }
    }

    auto diffusePair = [&](int a, int b, double w, double rate) {
        if (a < 0 || b < 0 || a >= n || b >= n || a == b) return;
        if (w <= 0.0) return;
        const Country& ca = countries[static_cast<size_t>(a)];
        const Country& cb = countries[static_cast<size_t>(b)];
        const double dist = traitDistance(ca, cb);
        const double friction = std::exp(-std::max(0.0, cfg.tech.culturalFrictionStrength) * dist);
        const double base = std::max(0.0, cfg.tech.diffusionBase);
        const double contact = std::clamp(w, 0.0, 1.0);
        const double absorbA = 0.20 + 0.80 * clamp01(ca.getInstitutionCapacity());
        const double absorbB = 0.20 + 0.80 * clamp01(cb.getInstitutionCapacity());
        const double r = base * rate * contact * friction * yearsD;
        if (r <= 0.0) return;
        const Country::KnowledgeVec& ka = ca.getKnowledge();
        const Country::KnowledgeVec& kb = cb.getKnowledge();
        for (int d = 0; d < Country::kDomains; ++d) {
            const double va = ka[static_cast<size_t>(d)];
            const double vb = kb[static_cast<size_t>(d)];
            if (vb > va) {
                delta[static_cast<size_t>(a)][static_cast<size_t>(d)] += r * absorbA * (vb - va);
            } else if (va > vb) {
                delta[static_cast<size_t>(b)][static_cast<size_t>(d)] += r * absorbB * (va - vb);
            }
        }
    };

    for (int a = 0; a < n; ++a) {
        if (countries[static_cast<size_t>(a)].getPopulation() <= 0) continue;
        for (int b : map.getAdjacentCountryIndicesPublic(a)) {
            if (b <= a || b < 0 || b >= n) continue;
            if (countries[static_cast<size_t>(b)].getPopulation() <= 0) continue;
            const int contact = std::max(1, map.getBorderContactCount(a, b));
            const double w = clamp01(std::log1p(static_cast<double>(contact)) / 5.0);

            const Country& ca = countries[static_cast<size_t>(a)];
            const Country& cb = countries[static_cast<size_t>(b)];
            const double accessAvg = 0.5 * (clamp01(ca.getMarketAccess()) + clamp01(cb.getMarketAccess()));
            const double openAvg = 0.5 * (clamp01(ca.getTraits()[5]) + clamp01(cb.getTraits()[5]));
            const double connAvg = 0.5 * (clamp01(ca.getConnectivityIndex()) + clamp01(cb.getConnectivityIndex()));
            const double instAvg = 0.5 * (clamp01(ca.getInstitutionCapacity()) + clamp01(cb.getInstitutionCapacity()));
            const double controlAvg = 0.5 * (clamp01(ca.getAvgControl()) + clamp01(cb.getAvgControl()));
            const double legitAvg = 0.5 * (clamp01(ca.getLegitimacy()) + clamp01(cb.getLegitimacy()));
            const double ineqAvg = 0.5 * (clamp01(ca.getInequality()) + clamp01(cb.getInequality()));
            const double popA = static_cast<double>(std::max<long long>(1, ca.getPopulation()));
            const double popB = static_cast<double>(std::max<long long>(1, cb.getPopulation()));
            const double urbanA = clamp01(ca.getTotalCityPopulation() / popA);
            const double urbanB = clamp01(cb.getTotalCityPopulation() / popB);
            const double urbanAvg = 0.5 * (urbanA + urbanB);

            const double adoption =
                (0.20 + 0.80 * instAvg) *
                (0.20 + 0.80 * controlAvg) *
                (0.20 + 0.80 * connAvg) *
                (0.25 + 0.75 * legitAvg) *
                (1.0 - 0.55 * ineqAvg);
            const double rate = 0.32 * (0.25 + 0.75 * accessAvg) * (0.25 + 0.75 * openAvg) * (0.35 + 0.65 * urbanAvg) * adoption;
            diffusePair(a, b, w, rate);
        }
    }

    if (tradeIntensityMatrix && !tradeIntensityMatrix->empty()) {
        const size_t need = static_cast<size_t>(n) * static_cast<size_t>(n);
        if (tradeIntensityMatrix->size() >= need) {
            for (int a = 0; a < n; ++a) {
                for (int b = a + 1; b < n; ++b) {
                    const float wab = (*tradeIntensityMatrix)[static_cast<size_t>(a) * static_cast<size_t>(n) + static_cast<size_t>(b)];
                    const float wba = (*tradeIntensityMatrix)[static_cast<size_t>(b) * static_cast<size_t>(n) + static_cast<size_t>(a)];
                    const double w = clamp01(static_cast<double>(std::max(wab, wba)));
                    if (w <= 0.001) continue;

                    const Country& ca = countries[static_cast<size_t>(a)];
                    const Country& cb = countries[static_cast<size_t>(b)];
                    const double accessAvg = 0.5 * (clamp01(ca.getMarketAccess()) + clamp01(cb.getMarketAccess()));
                    const double openAvg = 0.5 * (clamp01(ca.getTraits()[5]) + clamp01(cb.getTraits()[5]));
                    const double connAvg = 0.5 * (clamp01(ca.getConnectivityIndex()) + clamp01(cb.getConnectivityIndex()));
                    const double instAvg = 0.5 * (clamp01(ca.getInstitutionCapacity()) + clamp01(cb.getInstitutionCapacity()));
                    const double controlAvg = 0.5 * (clamp01(ca.getAvgControl()) + clamp01(cb.getAvgControl()));
                    const double legitAvg = 0.5 * (clamp01(ca.getLegitimacy()) + clamp01(cb.getLegitimacy()));
                    const double ineqAvg = 0.5 * (clamp01(ca.getInequality()) + clamp01(cb.getInequality()));
                    const double adoption =
                        (0.20 + 0.80 * instAvg) *
                        (0.20 + 0.80 * controlAvg) *
                        (0.20 + 0.80 * connAvg) *
                        (0.25 + 0.75 * legitAvg) *
                        (1.0 - 0.55 * ineqAvg);
                    const double rate = 0.95 * (0.20 + 0.80 * accessAvg) * (0.25 + 0.75 * openAvg) * adoption;
                    diffusePair(a, b, w, rate);
                }
            }
        }
    }

    for (int a = 0; a < n; ++a) {
        Country& ca = countries[static_cast<size_t>(a)];
        if (!ca.isAtWar() || ca.getPopulation() <= 0) continue;
        const auto& enemies = ca.getEnemies();
        for (const Country* e : enemies) {
            if (!e) continue;
            const int b = e->getCountryIndex();
            if (b < 0 || b >= n || b == a) continue;
            const double r = 0.03 * 0.85 * yearsD;
            const double va = ca.getKnowledgeDomain(7);
            const double vb = countries[static_cast<size_t>(b)].getKnowledgeDomain(7);
            if (vb > va) delta[static_cast<size_t>(a)][7] += r * (vb - va);
            else if (va > vb) delta[static_cast<size_t>(b)][7] += r * (va - vb);
        }
    }

    for (int i = 0; i < n; ++i) {
        Country::KnowledgeVec& k = countries[static_cast<size_t>(i)].getKnowledgeMutable();
        for (int d = 0; d < Country::kDomains; ++d) {
            k[static_cast<size_t>(d)] = std::max(0.0, k[static_cast<size_t>(d)] + delta[static_cast<size_t>(i)][static_cast<size_t>(d)]);
        }
    }

    // ---- Derived per-country feasibility signals for discovery/adoption ----
    std::vector<CountryTechSignals> signals(static_cast<size_t>(n));
    std::vector<double> ownedFieldCells(static_cast<size_t>(n), 0.0);
    std::vector<double> coastFieldCells(static_cast<size_t>(n), 0.0);
    std::vector<double> plantCells(static_cast<size_t>(n), 0.0);
    std::vector<double> herdCells(static_cast<size_t>(n), 0.0);

    const int fW = map.getFieldWidth();
    const int fH = map.getFieldHeight();
    const auto& owner = map.getFieldOwnerId();
    const auto& biome = map.getFieldBiome();

    auto biomeIsPlantFriendly = [](uint8_t b) {
        return b == 3u || b == 4u || b == 7u || b == 8u || b == 6u;
    };
    auto biomeIsHerdFriendly = [](uint8_t b) {
        return b == 4u || b == 6u || b == 2u || b == 1u;
    };

    if (fW > 0 && fH > 0 && owner.size() >= static_cast<size_t>(fW) * static_cast<size_t>(fH)) {
        for (int y = 0; y < fH; ++y) {
            for (int x = 0; x < fW; ++x) {
                const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(fW) + static_cast<size_t>(x);
                const int o = owner[idx];
                if (o < 0 || o >= n) continue;
                const uint8_t b = (idx < biome.size()) ? biome[idx] : 255u;
                if (b == 255u) continue;

                ownedFieldCells[static_cast<size_t>(o)] += 1.0;
                if (biomeIsPlantFriendly(b)) {
                    plantCells[static_cast<size_t>(o)] += 1.0;
                }
                if (biomeIsHerdFriendly(b)) {
                    herdCells[static_cast<size_t>(o)] += 1.0;
                }

                bool coast = false;
                const int dx[4] = {1, -1, 0, 0};
                const int dy[4] = {0, 0, 1, -1};
                for (int k = 0; k < 4; ++k) {
                    const int nx = x + dx[k];
                    const int ny = y + dy[k];
                    if (nx < 0 || ny < 0 || nx >= fW || ny >= fH) {
                        coast = true;
                        break;
                    }
                    const size_t ni = static_cast<size_t>(ny) * static_cast<size_t>(fW) + static_cast<size_t>(nx);
                    const uint8_t nb = (ni < biome.size()) ? biome[ni] : 255u;
                    if (nb == 255u) {
                        coast = true;
                        break;
                    }
                }
                if (coast) {
                    coastFieldCells[static_cast<size_t>(o)] += 1.0;
                }
            }
        }
    }

    for (int i = 0; i < n; ++i) {
        Country& c = countries[static_cast<size_t>(i)];
        CountryTechSignals s{};
        s.pop = static_cast<double>(std::max<long long>(1, c.getPopulation()));
        s.urban = clamp01(c.getTotalCityPopulation() / s.pop);
        s.specialization = clamp01((c.getSpecialistPopulation() / s.pop) / 0.15);
        s.institution = clamp01(c.getInstitutionCapacity());
        s.stability = clamp01(c.getStability());
        s.legitimacy = clamp01(c.getLegitimacy());
        s.marketAccess = clamp01(c.getMarketAccess());
        s.connectivity = clamp01(c.getConnectivityIndex());
        s.openness = clamp01(c.getTraits()[5]);
        s.inequality = clamp01(c.getInequality());
        s.competitionFragmentation = clamp01(c.getCompetitionFragmentationIndex());
        s.ideaMarketIntegration = clamp01(c.getIdeaMarketIntegrationIndex());
        s.credibleCommitment = clamp01(c.getCredibleCommitmentIndex());
        s.relativeFactorPrice = clamp01(c.getRelativeFactorPriceIndex());
        s.mediaThroughput = clamp01(c.getMediaThroughputIndex());
        s.merchantPower = clamp01(c.getMerchantPowerIndex());
        s.foodSecurity = clamp01(c.getFoodSecurity());
        s.famineSeverity = clamp01(c.getMacroEconomy().famineSeverity);
        s.atWar = c.isAtWar();
        s.climateFoodMult = std::max(0.05, static_cast<double>(map.getCountryClimateFoodMultiplier(i)));
        s.farmingPotential = std::max(0.0, map.getCountryFarmingPotential(i));
        s.foragingPotential = std::max(0.0, map.getCountryForagingPotential(i));

        auto sat = [](double x, double s) {
            const double d = std::max(1e-9, s);
            const double v = std::max(0.0, x);
            return v / (v + d);
        };
        const double resourceScale = 40.0 + 0.0002 * s.pop;
        s.oreAvail = sat(map.getCountryOrePotential(i), resourceScale * std::max(0.5, cfg.resources.oreNormalization / 120.0));
        s.energyAvail = sat(map.getCountryEnergyPotential(i), resourceScale * std::max(0.5, cfg.resources.energyNormalization / 120.0));
        s.constructionAvail = sat(map.getCountryConstructionPotential(i), resourceScale * std::max(0.5, cfg.resources.constructionNormalization / 120.0));

        const double own = std::max(1.0, ownedFieldCells[static_cast<size_t>(i)]);
        s.coastAccessRatio = std::max(!c.getPorts().empty() ? 0.60 : 0.0, coastFieldCells[static_cast<size_t>(i)] / own);
        s.plantDomesticationPotential = clamp01(plantCells[static_cast<size_t>(i)] / own);
        s.herdDomesticationPotential = clamp01(herdCells[static_cast<size_t>(i)] / own);
        s.riverWetlandShare = clamp01((s.farmingPotential) / std::max(1.0, s.farmingPotential + s.foragingPotential));
        signals[static_cast<size_t>(i)] = s;
    }

    auto inferSearchBias = [&](const Technology& t) -> int {
        // 1=labor-saving, 2=energy-using, 3=materials-intensive, 4=information/media, 5=institutions/finance
        const std::string n = toLowerCopy(t.name);
        auto has = [&](const char* kw) { return n.find(kw) != std::string::npos; };
        if (t.domainId == 6 || has("printing") || has("paper") || has("writing") || has("alphabet") ||
            has("telegraph") || has("radio") || has("internet") || has("computer") || has("education")) {
            return 4;
        }
        if (t.domainId == 4 || has("bank") || has("banking") || has("currency") || has("civil service") ||
            has("economics") || has("law") || has("market")) {
            return 5;
        }
        if (has("steam") || has("engine") || has("industrial") || has("mechanized") || has("rail") ||
            has("coal") || has("electric")) {
            return 2;
        }
        if (has("metall") || has("mining") || has("smelting") || has("steel") || has("iron") || has("bronze")) {
            return 3;
        }
        if (has("automation") || has("machine") || has("mechan") || has("assembly") || has("textile")) {
            return 1;
        }
        return 0;
    };

    auto inducedInnovationBias = [&](const Technology& t, const CountryTechSignals& s) -> double {
        const int bias = inferSearchBias(t);
        double mult = 1.0;
        if (bias == 1) {
            mult *= 0.85 + 0.55 * s.relativeFactorPrice;
        } else if (bias == 2) {
            mult *= 0.80 + 0.45 * s.relativeFactorPrice + 0.35 * s.energyAvail;
        } else if (bias == 3) {
            mult *= 0.85 + 0.55 * s.oreAvail;
        } else if (bias == 4) {
            mult *= 0.80 + 0.55 * s.mediaThroughput + 0.35 * s.ideaMarketIntegration;
        } else if (bias == 5) {
            mult *= 0.82 + 0.50 * s.credibleCommitment + 0.35 * s.merchantPower;
        }
        mult *= (0.85 + 0.30 * s.competitionFragmentation);
        mult *= (0.80 + 0.35 * s.ideaMarketIntegration);
        return std::clamp(mult, 0.35, 2.20);
    };

    // ---- Discovery pass: new knowledge only (not instant full adoption) ----
    for (int i = 0; i < n; ++i) {
        Country& c = countries[static_cast<size_t>(i)];
        if (c.getPopulation() <= 0) continue;

        const CountryTechSignals& s = signals[static_cast<size_t>(i)];
        int discoveredThisYear = 0;
        int maxDiscoveries = std::max(1, std::min(3, cfg.tech.maxDiscoveriesPerYear + (s.specialization > 0.10 ? 1 : 0)));

        for (int id : m_sortedIds) {
            if (discoveredThisYear >= maxDiscoveries) break;
            const Technology& t = m_technologies.at(id);
            const int dense = getTechDenseIndex(id);
            if (dense < 0) continue;
            if (c.knowsTechDense(dense)) continue;
            if (!prerequisitesKnown(c, t)) continue;
            if (!isFeasible(c, t, s)) continue;

            const double domainK = c.getKnowledgeDomain(t.domainId);
            const double domainFactor = smooth01((domainK - 0.45 * t.threshold) / std::max(1.0, 0.90 * t.threshold));
            if (domainFactor <= 0.0) continue;

            const double popFactor = std::clamp(0.35 + 0.20 * std::log1p(s.pop / 25000.0), 0.20, 2.4);
            const double orgFactor =
                (0.35 + 0.65 * s.specialization) *
                (0.35 + 0.65 * s.institution) *
                (0.45 + 0.55 * s.stability) *
                (0.35 + 0.65 * s.legitimacy) *
                (0.35 + 0.65 * s.connectivity) *
                (0.25 + 0.75 * s.openness);
            const double difficultyDen = 1.0 + std::max(0.0, cfg.tech.discoveryDifficultyScale) * std::max(0.0, t.difficulty);
            const double mechanismBoost =
                (0.72 + 0.28 * s.competitionFragmentation) *
                (0.72 + 0.28 * s.ideaMarketIntegration) *
                (0.75 + 0.25 * s.credibleCommitment) *
                (0.80 + 0.20 * s.mediaThroughput);
            const double inducedBias = inducedInnovationBias(t, s);
            const double hazard =
                std::max(0.0, cfg.tech.discoveryBase) *
                popFactor * orgFactor * domainFactor *
                mechanismBoost * inducedBias / std::max(0.2, difficultyDen);
            const double p = 1.0 - std::exp(-hazard * yearsD);
            const double u = deterministicUnit(worldSeed, currentYear, i, dense, 0x444953434f564552ull);
            if (u >= p) continue;

            c.setKnownTechDense(dense, true);
            const float seed = static_cast<float>(std::clamp(cfg.tech.discoverySeedAdoption * (0.6 + 0.8 * domainFactor), 0.0, 0.35));
            c.setAdoptionDense(dense, std::max(c.adoptionDense(dense), seed));
            c.setLowAdoptionYearsDense(dense, 0);
            ++discoveredThisYear;
            maybeRecordMilestoneEvents(c, t, dense, true, false, currentYear);
        }
    }

    // ---- Explicit known-tech diffusion and slow adoption seeding ----
    std::vector<std::vector<int>> knownDense(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        auto& vec = knownDense[static_cast<size_t>(i)];
        vec.reserve(static_cast<size_t>(std::min(24, getTechCount())));
        for (int id : m_sortedIds) {
            const int dense = getTechDenseIndex(id);
            if (dense < 0) continue;
            if (countries[static_cast<size_t>(i)].knowsTechDense(dense)) {
                vec.push_back(dense);
            }
        }
    }

    auto pairUnit = [&](int a, int b, int dense, std::uint64_t salt) {
        const std::uint64_t aa = static_cast<std::uint64_t>(std::max(0, a) + 1);
        const std::uint64_t bb = static_cast<std::uint64_t>(std::max(0, b) + 1);
        const std::uint64_t yy = static_cast<std::uint64_t>(static_cast<std::int64_t>(currentYear) + 20000);
        const std::uint64_t tt = static_cast<std::uint64_t>(std::max(0, dense) + 1);
        const std::uint64_t mixed = SimulationContext::mix64(
            worldSeed ^
            (aa * 0x9E3779B97F4A7C15ull) ^
            (bb * 0xBF58476D1CE4E5B9ull) ^
            (yy * 0x94D049BB133111EBull) ^
            (tt * 0xD6E8FEB86659FD93ull) ^
            salt);
        return SimulationContext::u01FromU64(mixed);
    };

    auto diffuseKnownDirectional = [&](int from, int to, double contactW) {
        if (from < 0 || to < 0 || from >= n || to >= n || from == to) return;
        Country& source = countries[static_cast<size_t>(from)];
        Country& target = countries[static_cast<size_t>(to)];
        if (source.getPopulation() <= 0 || target.getPopulation() <= 0) return;
        if (contactW <= 0.0) return;

        const CountryTechSignals& sf = signals[static_cast<size_t>(from)];
        const CountryTechSignals& st = signals[static_cast<size_t>(to)];
        const double dist = traitDistance(source, target);
        const double friction = std::exp(-std::max(0.0, cfg.tech.culturalFrictionStrength) * dist);
        const double topK = std::max(2, std::min(16, cfg.tech.knownDiffusionTopK));

        const auto& known = knownDense[static_cast<size_t>(from)];
        const int limit = std::min<int>(static_cast<int>(known.size()), static_cast<int>(topK));
        for (int offset = 0; offset < limit; ++offset) {
            const int dense = known[known.size() - 1u - static_cast<size_t>(offset)];
            if (dense < 0 || dense >= getTechCount()) continue;
            if (target.knowsTechDense(dense)) continue;

            const Technology& t = m_technologies.at(getTechIdFromDenseIndex(dense));
            const double pLearn = std::clamp(
                std::max(0.0, cfg.tech.knownDiffusionBase) *
                contactW *
                friction *
                (0.30 + 0.70 * sf.ideaMarketIntegration) *
                (0.30 + 0.70 * sf.mediaThroughput) *
                (0.30 + 0.70 * st.ideaMarketIntegration) *
                (0.35 + 0.65 * sf.connectivity) *
                (0.25 + 0.75 * st.openness) *
                yearsD,
                0.0,
                0.85);
            if (pairUnit(from, to, dense, 0x4b4e4f574e444946ull) < pLearn) {
                target.setKnownTechDense(dense, true);
                target.setAdoptionDense(dense, std::max(target.adoptionDense(dense), 0.0f));
                maybeRecordMilestoneEvents(target, t, dense, true, false, currentYear);
            }

            const float sourceAdopt = source.adoptionDense(dense);
            const float targetAdopt = target.adoptionDense(dense);
            if (sourceAdopt > 0.80f && targetAdopt < 0.10f && isFeasible(target, t, st)) {
                const double pSeed = std::clamp(
                    0.12 * contactW * friction *
                    (0.30 + 0.70 * sf.ideaMarketIntegration) *
                    (0.30 + 0.70 * sf.mediaThroughput) *
                    (0.35 + 0.65 * st.institution) *
                    (0.30 + 0.70 * st.connectivity) *
                    yearsD,
                    0.0,
                    0.60);
                if (pairUnit(from, to, dense, 0x41444f5054534545ull) < pSeed) {
                    target.setKnownTechDense(dense, true);
                    const float seed = static_cast<float>(std::clamp(
                        cfg.tech.adoptionSeedFromNeighbors * (0.8 + 0.4 * pairUnit(from, to, dense, 0x5345454456414c31ull)),
                        0.02,
                        0.18));
                    target.setAdoptionDense(dense, std::max(targetAdopt, seed));
                }
            }
        }
    };

    for (int a = 0; a < n; ++a) {
        if (countries[static_cast<size_t>(a)].getPopulation() <= 0) continue;

        for (int b : map.getAdjacentCountryIndicesPublic(a)) {
            if (b < 0 || b >= n || b == a) continue;
            const int contact = std::max(1, map.getBorderContactCount(a, b));
            const double w = clamp01(std::log1p(static_cast<double>(contact)) / 5.0);
            diffuseKnownDirectional(a, b, w);
        }

        if (tradeIntensityMatrix && tradeIntensityMatrix->size() >= static_cast<size_t>(n) * static_cast<size_t>(n)) {
            for (int b = 0; b < n; ++b) {
                if (b == a) continue;
                const float w = (*tradeIntensityMatrix)[static_cast<size_t>(a) * static_cast<size_t>(n) + static_cast<size_t>(b)];
                if (w <= 0.002f) continue;
                diffuseKnownDirectional(a, b, clamp01(static_cast<double>(w)));
            }
        }
    }

    // ---- Adoption and loss dynamics ----
    for (int i = 0; i < n; ++i) {
        Country& c = countries[static_cast<size_t>(i)];
        if (c.getPopulation() <= 0) {
            continue;
        }
        const CountryTechSignals& s = signals[static_cast<size_t>(i)];

        for (int id : m_sortedIds) {
            const int dense = getTechDenseIndex(id);
            if (dense < 0) continue;
            if (!c.knowsTechDense(dense)) continue;

            const Technology& t = m_technologies.at(id);
            const double oldA = static_cast<double>(c.adoptionDense(dense));
            double A = oldA;

            const bool prereqOk = prerequisitesAdopted(c, t, prereqAdoptScale);
            const bool feasible = isFeasible(c, t, s);

            if (prereqOk && feasible) {
                double speed = std::max(0.0, cfg.tech.adoptionBaseSpeed);
                speed *= (0.25 + 0.75 * s.institution);
                speed *= (0.25 + 0.75 * s.stability);
                speed *= (0.30 + 0.70 * s.legitimacy);
                speed *= (0.25 + 0.75 * s.marketAccess);
                speed *= (0.25 + 0.75 * s.connectivity);
                speed *= (0.20 + 0.80 * s.specialization);
                speed *= (1.0 - 0.50 * s.inequality);
                speed *= (0.55 + 0.45 * s.foodSecurity);
                speed *= (0.70 + 0.30 * s.ideaMarketIntegration);
                speed *= (0.72 + 0.28 * s.credibleCommitment);
                speed *= (0.78 + 0.22 * s.mediaThroughput);
                speed *= (0.78 + 0.22 * s.competitionFragmentation);
                speed *= inducedInnovationBias(t, s);
                if (s.atWar) speed *= 0.88;
                speed *= (1.0 - 0.35 * s.famineSeverity);
                const double dA = speed * (1.0 - A) * yearsD;
                A += dA;
            } else {
                double decay = std::max(0.0, cfg.tech.adoptionDecayBase);
                double collapse = 1.0;
                collapse += s.atWar ? 0.45 : 0.0;
                collapse += 1.15 * s.famineSeverity;
                collapse += std::max(0.0, 0.55 - s.stability);
                collapse += std::max(0.0, 0.50 - s.legitimacy);
                collapse += std::max(0.0, 0.35 - s.connectivity);
                if (s.pop < 5000.0) {
                    collapse += 0.8;
                }
                decay *= collapse * std::max(1.0, cfg.tech.collapseDecayMultiplier);
                const double dA = decay * A * yearsD;
                A -= dA;
            }

            A = clamp01(A);
            c.setAdoptionDense(dense, static_cast<float>(A));

            if (A < 0.05) {
                c.setLowAdoptionYearsDense(dense, c.lowAdoptionYearsDense(dense) + years);
            } else {
                c.setLowAdoptionYearsDense(dense, 0);
            }

            if (A < std::clamp(cfg.tech.forgetPracticeThreshold, 0.01, 0.5)) {
                // Keep known=true but treat as not practiced (derived by adoption threshold).
            }

            // Rare deep-isolation forgetting.
            if (c.knowsTechDense(dense) &&
                A < 0.05 &&
                s.pop < 1500.0 &&
                s.connectivity < 0.12 &&
                c.lowAdoptionYearsDense(dense) >= std::max(25, cfg.tech.rareForgetYears) &&
                t.order <= 250) {
                const double pForget = std::clamp(cfg.tech.rareForgetChance * yearsD, 0.0, 0.20);
                if (deterministicUnit(worldSeed, currentYear, i, dense, 0x464f524745545241ull) < pForget) {
                    c.setKnownTechDense(dense, false);
                    c.setAdoptionDense(dense, 0.0f);
                }
            }

            const bool crossed = (oldA < adoptionThreshold && A >= adoptionThreshold);
            if (crossed) {
                maybeRecordMilestoneEvents(c, t, dense, false, true, currentYear);
            }
        }

        refreshUnlockedFromAdoption(c, adoptionThreshold);
        recomputeCountryTechEffects(c, adoptionThreshold);
    }
}

void TechnologyManager::setUnlockedTechnologiesForEditor(Country& country,
                                                         const std::vector<int>& techIds,
                                                         bool includePrerequisites) {
    ensureCountryState(country);

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
            const int id = stack.back();
            stack.pop_back();
            auto it = m_technologies.find(id);
            if (it == m_technologies.end()) continue;
            for (int req : it->second.requiredTechs) {
                if (m_technologies.find(req) == m_technologies.end()) continue;
                if (requested.insert(req).second) {
                    stack.push_back(req);
                }
            }
        }
    }

    country.clearTechStateDense();
    ensureCountryState(country);
    for (int id : requested) {
        const int dense = getTechDenseIndex(id);
        if (dense < 0) continue;
        country.setKnownTechDense(dense, true);
        country.setAdoptionDense(dense, 1.0f);
        country.setLowAdoptionYearsDense(dense, 0);
    }

    refreshUnlockedFromAdoption(country, 0.65);
    recomputeCountryTechEffects(country, 0.65);
}

void TechnologyManager::printMilestoneAdoptionSummary() const {
    auto findIdByName = [&](const char* techName) -> int {
        const std::string needle = toLowerCopy(std::string(techName));
        for (const auto& kv : m_technologies) {
            if (toLowerCopy(kv.second.name) == needle) {
                return kv.first;
            }
        }
        return -1;
    };

    const std::array<const char*, 7> milestones = {
        "Sedentism",
        "Proto-cultivation",
        "Agriculture",
        "Animal Husbandry",
        "Pottery",
        "Metallurgy",
        "Writing"
    };

    std::cout << "  Milestone median adoption years:\n";
    for (const char* name : milestones) {
        const int id = findIdByName(name);
        if (id < 0) {
            std::cout << "    - " << name << ": n/a\n";
            continue;
        }
        const int dense = getTechDenseIndex(id);
        if (dense < 0) {
            std::cout << "    - " << name << ": n/a\n";
            continue;
        }

        std::vector<int> years;
        years.reserve(64);
        for (const auto& kv : m_firstAdoptionYear) {
            const int keyDense = static_cast<int>(kv.first & 0xffffffffu);
            if (keyDense == dense) {
                years.push_back(kv.second);
            }
        }
        if (years.empty()) {
            std::cout << "    - " << name << ": never\n";
            continue;
        }
        std::sort(years.begin(), years.end());
        const int med = years[years.size() / 2u];
        std::cout << "    - " << name << ": " << med << " (n=" << years.size() << ")\n";
    }
}

// POPULATION SYSTEM HELPER IMPLEMENTATIONS

bool TechnologyManager::hasTech(const TechnologyManager& tm, const Country& c, int id) {
    const auto& v = tm.getUnlockedTechnologies(c);
    return std::find(v.begin(), v.end(), id) != v.end();
}

struct KBoost { int id; double mult; };

double TechnologyManager::techKMultiplier(const TechnologyManager& tm, const Country& c) {
    static const KBoost foodCluster[] = {
        {10, 1.06},
        {20, 1.10},
        {23, 1.05},
        {17, 1.03},
        {TechId::CIVIL_SERVICE, 1.03},
        {TechId::BANKING, 1.03},
        {TechId::ECONOMICS, 1.04},
        {41, 1.12},
        {55, 1.20},
        {51, 1.15},
        {63, 1.10},
        {57, 1.08},
        {71, 1.10},
        {65, 1.05},
        {81, 1.08},
        {90, 1.07}
    };

    double m = 1.0;
    for (const auto& kb : foodCluster) {
        if (hasTech(tm, c, kb.id)) {
            m *= kb.mult;
        }
    }

    if (hasTech(tm, c, TechId::NAVIGATION)) m *= 1.02;
    if (hasTech(tm, c, 58)) m *= 1.02;
    if (hasTech(tm, c, 59)) m *= 1.02;
    if (hasTech(tm, c, 79)) m *= 1.01;

    return m;
}

struct RAdd { int id; double add; };
struct RCap { int id; double mult; };

double TechnologyManager::techGrowthRateR(const TechnologyManager& tm, const Country& c) {
    double r = 0.0003;

    static const RAdd early[] = {
        {10, 0.00005},
        {20, 0.00008},
        {23, 0.00003},
        {32, 0.00002}
    };
    for (const auto& e : early) {
        if (hasTech(tm, c, e.id)) {
            r += e.add;
        }
    }

    static const RAdd industrial[] = {
        {51, 0.0006},
        {52, 0.0008},
        {55, 0.0004},
        {96, 0.0010},
        {53, 0.0010},
        {54, 0.0005},
        {63, 0.0005},
        {65, 0.0006}
    };
    for (const auto& e : industrial) {
        if (hasTech(tm, c, e.id)) {
            r += e.add;
        }
    }

    double fertilityMult = 1.0;
    static const RCap transition[] = {
        {TechId::EDUCATION, 0.92},
        {TechId::UNIVERSITIES, 0.95},
        {TechId::ECONOMICS, 0.96},
        {69, 0.92},
        {80, 0.95},
        {79, 0.95},
        {85, 0.97},
    };
    for (const auto& t : transition) {
        if (hasTech(tm, c, t.id)) {
            fertilityMult *= t.mult;
        }
    }

    r *= fertilityMult;
    r = std::max(0.00005, std::min(r, 0.0200));
    return r;
}
