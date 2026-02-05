#include "technology.h"
#include "country.h"
#include "map.h"
#include <algorithm>
#include <iostream> // Make sure to include iostream if not already present
#include <cmath> // For std::llround
#include <unordered_set>
#include <cctype>

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
        {96, {"Sanitation", 6000, 96, {40}}},
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

    // Phase 5A: map old "cost" to a knowledge threshold, and assign a knowledge domain.
    auto toLower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    };

    auto domainFor = [&](int id, const std::string& name) -> int {
        (void)id;
        const std::string n = toLower(name);
        auto has = [&](const char* kw) { return n.find(kw) != std::string::npos; };

        // 0 Agriculture, 1 Materials, 2 Construction, 3 Navigation, 4 Governance, 5 Medicine, 6 Education, 7 Warfare/Industry
        if (has("agriculture") || has("irrigation") || has("husbandry") || has("calendar") || has("refrigeration")) return 0;
        if (has("sanitation") || has("vaccination") || has("penicillin") || has("genetic") || has("biotechnology")) return 5;
        if (has("education") || has("university") || has("universities") || has("writing") || has("alphabet") || has("paper") || has("printing") ||
            has("computer") || has("computers") || has("internet") || has("telephone") || has("telegraph") || has("radio") || has("mobile") ||
            has("fiber optics") || has("integrated circuit") || has("personal computers") || has("social media")) return 6;
        if (has("sailing") || has("ship") || has("compass") || has("navigation") || has("flight") || has("satellite") || has("satellites") ||
            has("rocketry") || has("rocket") || has("augmented reality") || has("virtual reality")) return 3;
        if (has("democracy") || has("currency") || has("civil service") || has("banking") || has("markets") || has("economics") || has("blockchain")) return 4;
        if (has("masonry") || has("construction") || has("engineering") || has("architecture") || has("road") || has("railroad") || has("rail")) return 2;
        if (has("archery") || has("gunpowder") || has("firearms") || has("rifling") || has("ballistics") || has("stealth")) return 7;
        if (has("mining") || has("metal") || has("iron") || has("steel") || has("electric") || has("electronics") || has("transistor") ||
            has("laser") || has("robot") || has("nanotech") || has("superconduct") || has("industrial") || has("mass production") || has("plastics")) return 1;
        return 1;
    };

    for (auto& kv : m_technologies) {
        Technology& t = kv.second;
        t.threshold = static_cast<double>(t.cost);
        t.domainId = domainFor(t.id, t.name);
    }

#ifndef NDEBUG
    // Debug-only sanity checks: ensure IDs are consistent and expected constants match names.
    {
        std::unordered_map<int, std::string> idToName;
        idToName.reserve(m_technologies.size());

        for (const auto& kv : m_technologies) {
            const int keyId = kv.first;
            const Technology& t = kv.second;
            if (t.id != keyId) {
                std::cerr << "[TechSanity] key/id mismatch: key=" << keyId << " tech.id=" << t.id
                          << " name=" << t.name << "\n";
            }
            auto ins = idToName.emplace(t.id, t.name);
            if (!ins.second && ins.first->second != t.name) {
                std::cerr << "[TechSanity] duplicate tech id " << t.id << " names: "
                          << ins.first->second << " vs " << t.name << "\n";
            }
        }

        auto expectNameContains = [&](int id, const char* needle) {
            auto it = idToName.find(id);
            if (it == idToName.end()) {
                std::cerr << "[TechSanity] missing expected id " << id << " (" << needle << ")\n";
                return;
            }
            const std::string& name = it->second;
            if (name.find(needle) == std::string::npos) {
                std::cerr << "[TechSanity] expected id " << id << " to contain '" << needle
                          << "' but got '" << name << "'\n";
            }
        };

        expectNameContains(TechId::METALLURGY, "Metallurgy");
        expectNameContains(TechId::NAVIGATION, "Navigation");
        expectNameContains(TechId::UNIVERSITIES, "Universities");
        expectNameContains(TechId::ASTRONOMY, "Astronomy");
        expectNameContains(TechId::SCIENTIFIC_METHOD, "Scientific Method");
        expectNameContains(TechId::SANITATION, "Sanitation");
        expectNameContains(TechId::EDUCATION, "Education");
        expectNameContains(TechId::CIVIL_SERVICE, "Civil Service");
        expectNameContains(TechId::BANKING, "Banking");
        expectNameContains(TechId::ECONOMICS, "Economics");
        expectNameContains(TechId::WRITING, "Writing");
        expectNameContains(TechId::CONSTRUCTION, "Construction");
        expectNameContains(TechId::CURRENCY, "Currency");
    }
#endif
}

void TechnologyManager::updateCountry(Country& country) {
    // Unlock technologies by knowledge thresholds, but cap per-tick unlock count so a large
    // overshoot doesn't instantly grant an entire era in a single year.
    const double pop = static_cast<double>(std::max<long long>(1, country.getPopulation()));
    const double urban = std::clamp(country.getTotalCityPopulation() / pop, 0.0, 1.0);

    int maxUnlocks = 1;
    if (urban > 0.06) maxUnlocks += 1;
    if (TechnologyManager::hasTech(*this, country, TechId::WRITING)) maxUnlocks += 1;
    if (TechnologyManager::hasTech(*this, country, TechId::UNIVERSITIES)) maxUnlocks += 1;
    if (country.getInnovationRate() > 12.0) maxUnlocks += 1;
    if (country.getInnovationRate() > 30.0) maxUnlocks += 1;
    maxUnlocks = std::max(1, std::min(6, maxUnlocks));

    int unlockedThisTick = 0;
    bool progress = true;
    while (progress && unlockedThisTick < maxUnlocks) {
        progress = false;
        for (int techId : m_sortedIds) {
            const Technology& tech = m_technologies.at(techId);
            if (canUnlockTechnology(country, techId) && country.getKnowledgeDomain(tech.domainId) >= tech.threshold) {
                unlockTechnology(country, techId);
                unlockedThisTick++;
                progress = true;
                break; // restart because prereqs/unlocks changed
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

    // Phase 5A: no science point spending; unlocks are gated by knowledge thresholds.

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

void TechnologyManager::tickYear(std::vector<Country>& countries,
                                 const Map& map,
                                 const std::vector<float>* tradeIntensityMatrix,
                                 int currentYear,
                                 int dtYears) {
    (void)currentYear;
    const int years = std::max(1, dtYears);
    const double yearsD = static_cast<double>(years);

    const int n = static_cast<int>(countries.size());
    if (n <= 0) {
        return;
    }

    auto clamp01 = [](double v) { return std::max(0.0, std::min(1.0, v)); };

    std::vector<Country::KnowledgeVec> delta(static_cast<size_t>(n), Country::KnowledgeVec{});

    // Innovation (rate, not hoarded points): driven by surplus, urbanization, institutions, stability, access.
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

        const auto& m = c.getMacroEconomy();
        const double nonFoodSurplus = std::max(0.0, m.lastNonFoodOutput - m.lastNonFoodCons);
        const double surplusPc = nonFoodSurplus / pop;
        const double surplusFactor = clamp01(surplusPc / 0.00085);

        // Key fix for realism: early societies without cities/education must not accumulate
        // thousands of "knowledge" within a few centuries. Base innovation is small, then
        // accelerates when institutions/urbanization/education appear.
        double innov = 5.0 + 12.0 * surplusFactor;
        innov *= (0.22 + 0.78 * access);
        innov *= (0.35 + 0.65 * stability);
        innov *= (0.40 + 0.60 * legitimacy);
        innov *= (0.25 + 0.75 * urban);
        innov *= (0.75 + 0.70 * eduShare);
        if (m.foodSecurity < 0.95) {
            innov *= (0.80 + 0.20 * clamp01(m.foodSecurity));
        }

        // Tech/institution era multipliers (no point currencies): stepwise acceleration.
        double era = 1.0;
        if (TechnologyManager::hasTech(*this, c, TechId::WRITING)) era *= 1.55;
        if (TechnologyManager::hasTech(*this, c, TechId::EDUCATION)) era *= 1.70;
        if (TechnologyManager::hasTech(*this, c, TechId::UNIVERSITIES)) era *= 1.55;
        if (TechnologyManager::hasTech(*this, c, TechId::SCIENTIFIC_METHOD)) era *= 2.10;
        if (TechnologyManager::hasTech(*this, c, 54)) era *= 1.50; // Electricity
        if (TechnologyManager::hasTech(*this, c, 69)) era *= 1.45; // Computers
        if (TechnologyManager::hasTech(*this, c, 79)) era *= 1.25; // Internet
        innov *= era;

        // Legacy country flavor types remain as mild innovation multipliers (no point hoarding).
        if (c.getScienceType() == Country::ScienceType::MS) {
            innov *= 1.22;
        } else if (c.getScienceType() == Country::ScienceType::LS) {
            innov *= 0.70;
        }
        c.setInnovationRate(innov);

        double w[Country::kDomains] = { 1, 1, 1, 1, 1, 1, 1, 1 };
        // Domain weights respond to pressures and institutions/spending.
        if (m.foodSecurity < 0.90) w[0] += 1.6; // Agriculture under famine stress
        if (c.getPorts().size() > 0) w[3] += 0.7; // Navigation with ports
        if (c.isAtWar()) w[7] += 1.2; // Warfare/Industry during war
        w[4] += 0.6 * clamp01(c.getAdminSpendingShare()); // Governance responds to admin focus
        w[6] += 1.8 * eduShare; // Education responds strongly to education share
        w[5] += 1.2 * healthShare; // Medicine responds to health focus

        double sumW = 0.0;
        for (int d = 0; d < Country::kDomains; ++d) sumW += w[d];
        if (sumW <= 0.0) sumW = 1.0;

        Country::KnowledgeVec& k = c.getKnowledgeMutable();
        for (int d = 0; d < Country::kDomains; ++d) {
            k[static_cast<size_t>(d)] = std::max(0.0, k[static_cast<size_t>(d)] + (innov * (w[d] / sumW) * yearsD));
        }
    }

    auto diffusePair = [&](int a, int b, double w, double rate) {
        if (a < 0 || b < 0 || a >= n || b >= n || a == b) return;
        if (w <= 0.0) return;
        const double r = rate * w * yearsD;
        if (r <= 0.0) return;
        const Country::KnowledgeVec& ka = countries[static_cast<size_t>(a)].getKnowledge();
        const Country::KnowledgeVec& kb = countries[static_cast<size_t>(b)].getKnowledge();
        for (int d = 0; d < Country::kDomains; ++d) {
            const double va = ka[static_cast<size_t>(d)];
            const double vb = kb[static_cast<size_t>(d)];
            if (vb > va) {
                delta[static_cast<size_t>(a)][static_cast<size_t>(d)] += r * (vb - va);
            } else if (va > vb) {
                delta[static_cast<size_t>(b)][static_cast<size_t>(d)] += r * (va - vb);
            }
        }
    };

    // Border diffusion (contact-based gradient). Scale strongly with access/openness so
    // early tribal worlds don't instantly converge.
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
            const double openAvg = 0.5 * (clamp01(ca.getTraits()[5]) + clamp01(cb.getTraits()[5])); // Openness
            const double popA = static_cast<double>(std::max<long long>(1, ca.getPopulation()));
            const double popB = static_cast<double>(std::max<long long>(1, cb.getPopulation()));
            const double urbanA = clamp01(ca.getTotalCityPopulation() / popA);
            const double urbanB = clamp01(cb.getTotalCityPopulation() / popB);
            const double urbanAvg = 0.5 * (urbanA + urbanB);

            const double rate = 0.0035 * (0.25 + 0.75 * accessAvg) * (0.25 + 0.75 * openAvg) * (0.35 + 0.65 * urbanAvg);
            diffusePair(a, b, w, rate);
        }
    }

    // Trade diffusion (uses executed trade intensity; faster convergence along trade links).
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
                    const double openAvg = 0.5 * (clamp01(ca.getTraits()[5]) + clamp01(cb.getTraits()[5])); // Openness
                    const double rate = 0.014 * (0.20 + 0.80 * accessAvg) * (0.25 + 0.75 * openAvg);
                    diffusePair(a, b, w, rate);
                }
            }
        }
    }

    // War contact: warfare/industry domain converges faster between enemies (spoils, espionage, adaptation).
    for (int a = 0; a < n; ++a) {
        Country& ca = countries[static_cast<size_t>(a)];
        if (!ca.isAtWar() || ca.getPopulation() <= 0) continue;
        const auto& enemies = ca.getEnemies();
        for (const Country* e : enemies) {
            if (!e) continue;
            const int b = e->getCountryIndex();
            if (b < 0 || b >= n || b == a) continue;
            const double w = 0.85; // strong contact when at war
            const double r = 0.03 * w * yearsD;
            const double va = ca.getKnowledgeDomain(7);
            const double vb = countries[static_cast<size_t>(b)].getKnowledgeDomain(7);
            if (vb > va) delta[static_cast<size_t>(a)][7] += r * (vb - va);
            else if (va > vb) delta[static_cast<size_t>(b)][7] += r * (va - vb);
        }
    }

    // Apply diffusion deltas.
    for (int i = 0; i < n; ++i) {
        Country& c = countries[static_cast<size_t>(i)];
        Country::KnowledgeVec& k = c.getKnowledgeMutable();
        for (int d = 0; d < Country::kDomains; ++d) {
            k[static_cast<size_t>(d)] = std::max(0.0, k[static_cast<size_t>(d)] + delta[static_cast<size_t>(i)][static_cast<size_t>(d)]);
        }
    }

    // Unlock technologies by knowledge thresholds (no spending).
    for (int i = 0; i < n; ++i) {
        if (countries[static_cast<size_t>(i)].getPopulation() <= 0) continue;
        updateCountry(countries[static_cast<size_t>(i)]);
    }
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
        {TechId::CIVIL_SERVICE, 1.03}, // Civil Service
        {TechId::BANKING, 1.03},       // Banking
        {TechId::ECONOMICS, 1.04},     // Economics
        {41, 1.12},   // Chemistry as fertilizer proxy
        {55, 1.20},   // Railroad
        {51, 1.15},   // Steam Engine
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
    if (hasTech(tm, c, TechId::NAVIGATION)) m *= 1.02; // Navigation
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
        {51, 0.0006},   // Steam Engine
        {52, 0.0008},   // Industrialization
        {55, 0.0004},   // Railroad
        {96, 0.0010},   // Sanitation
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
        {TechId::EDUCATION, 0.92},    // Education
        {TechId::UNIVERSITIES, 0.95}, // Universities
        {TechId::ECONOMICS, 0.96},    // Economics
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
