// culture.cpp
#include "culture.h"
#include "country.h"
#include "technology.h"
#include "map.h"
#include "news.h"

#include <algorithm>
#include <cmath>
#include <iostream>

using namespace std;

bool CultureManager::s_debugMode = false;

namespace {
double clamp01(double v) { return std::max(0.0, std::min(1.0, v)); }

int modPositive(int v, int m) {
    const int r = v % m;
    return (r < 0) ? (r + m) : r;
}

bool intervalHitsStaggeredCadence(int startYear, int endYear, int index, int cadenceYears) {
    if (cadenceYears <= 0 || startYear > endYear) return false;
    const int targetResidue = modPositive(-index, cadenceYears); // y where (y + index) % cadence == 0
    const int startResidue = modPositive(startYear, cadenceYears);
    const int delta = modPositive(targetResidue - startResidue, cadenceYears);
    const long long firstHit = static_cast<long long>(startYear) + static_cast<long long>(delta);
    return firstHit <= static_cast<long long>(endYear);
}

enum TraitId {
    Religiosity = 0,
    Collectivism = 1,
    Militarism = 2,
    Mercantile = 3,
    Hierarchy = 4,
    Openness = 5,
    Legalism = 6
};

double urbanizationOf(const Country& c) {
    const double pop = static_cast<double>(std::max<long long>(0, c.getPopulation()));
    if (pop <= 1.0) return 0.0;
    return clamp01(c.getTotalCityPopulation() / pop);
}

} // namespace

CultureManager::CultureManager() {
    initializeCivics();
}

void CultureManager::initializeCivics() {
    m_civics.clear();

    auto add = [&](int id,
                   const std::string& name,
                   std::vector<int> reqCivics,
                   std::vector<int> reqTechs,
                   auto&& configure) {
        Civic c;
        c.name = name;
        c.cost = 0; // legacy field, not a currency
        c.id = id;
        c.requiredCivics = std::move(reqCivics);
        c.requiredTechs = std::move(reqTechs);
        configure(c);
        m_civics[id] = std::move(c);
    };

    // Institutions: adoption is cadence- and pressure-driven; effects modify state capacity.
    add(1, "Customary Law", {}, {}, [&](Civic& c) {
        c.minAdminCapacity = 0.02;
        c.minAvgControl = 0.15;
        c.stabilityHit = 0.01;
        c.adminCapBonus = 0.03;
        c.legitimacyBonus = 0.02;
    });

    add(2, "Tax Registers", {1}, {TechId::WRITING}, [&](Civic& c) { // Writing
        c.minAdminCapacity = 0.04;
        c.minAvgControl = 0.20;
        c.stabilityHit = 0.02;
        c.fiscalCapBonus = 0.06;
        c.legitimacyBonus = 0.01;
    });

    add(3, "Road Corps", {1}, {TechId::CONSTRUCTION}, [&](Civic& c) { // Construction
        c.minAdminCapacity = 0.05;
        c.logisticsBonus = 0.07;
        c.stabilityHit = 0.02;
    });

    add(4, "Merchant Guilds", {1}, {TechId::CURRENCY}, [&](Civic& c) { // Currency
        c.minUrbanization = 0.05;
        c.minAvgControl = 0.25;
        c.fiscalCapBonus = 0.03;
        c.logisticsBonus = 0.03;
        c.stabilityHit = 0.02;
    });

    add(5, "Professional Army", {1}, {3}, [&](Civic& c) { // Archery
        c.minAdminCapacity = 0.06;
        c.minAvgControl = 0.25;
        c.stabilityHit = 0.03;
        c.legitimacyHit = 0.01;
        c.logisticsBonus = 0.02;
        c.adminCapBonus = 0.01;
    });

    add(6, "Civil Service", {2}, {TechId::CIVIL_SERVICE}, [&](Civic& c) { // Civil Service tech
        c.minAdminCapacity = 0.08;
        c.minAvgControl = 0.30;
        c.stabilityHit = 0.03;
        c.adminCapBonus = 0.08;
        c.fiscalCapBonus = 0.04;
        c.legitimacyBonus = 0.02;
    });

    add(7, "Public Education", {6}, {TechId::EDUCATION}, [&](Civic& c) { // Education tech
        c.minUrbanization = 0.10;
        c.minAdminCapacity = 0.10;
        c.stabilityHit = 0.03;
        c.educationShareBonus = 0.08;
        c.legitimacyBonus = 0.01;
    });

    add(8, "Public Health", {6}, {TechId::SANITATION}, [&](Civic& c) { // Sanitation
        c.minUrbanization = 0.10;
        c.minAdminCapacity = 0.10;
        c.stabilityHit = 0.02;
        c.healthShareBonus = 0.06;
        c.legitimacyBonus = 0.01;
    });

    add(9, "Banking System", {2}, {TechId::BANKING}, [&](Civic& c) { // Banking
        c.minUrbanization = 0.08;
        c.minAvgControl = 0.35;
        c.stabilityHit = 0.03;
        c.fiscalCapBonus = 0.09;
        c.debtAdd = 15.0; // transitional fiscal stress
    });

    add(10, "Representative Councils", {1}, {22}, [&](Civic& c) { // Philosophy
        c.minUrbanization = 0.06;
        c.minAvgControl = 0.30;
        c.stabilityHit = 0.03;
        c.legitimacyHit = 0.01;
        c.legitimacyBonus = 0.05;
        c.adminCapBonus = 0.02;
    });

    add(11, "Standard Weights & Measures", {4}, {TechId::ECONOMICS}, [&](Civic& c) { // Economics
        c.minUrbanization = 0.10;
        c.minAvgControl = 0.40;
        c.stabilityHit = 0.02;
        c.fiscalCapBonus = 0.03;
        c.logisticsBonus = 0.03;
    });

    add(12, "Maritime Administration", {4}, {TechId::NAVIGATION}, [&](Civic& c) { // Navigation
        c.minUrbanization = 0.08;
        c.minAvgControl = 0.35;
        c.stabilityHit = 0.03;
        c.logisticsBonus = 0.06;
        c.fiscalCapBonus = 0.02;
    });
}

void CultureManager::updateCountry(Country& country, const TechnologyManager& techManager) {
    // Legacy hook: no per-country currency purchase. Institutions are adopted via `tickYear`.
    (void)country;
    (void)techManager;
}

bool CultureManager::canUnlockCivic(const Country& country, int civicId, const TechnologyManager& techManager) const {
    // Already adopted?
    auto itCountry = m_unlockedCivics.find(country.getCountryIndex());
    if (itCountry != m_unlockedCivics.end()) {
        const auto& unlocked = itCountry->second;
        if (std::find(unlocked.begin(), unlocked.end(), civicId) != unlocked.end()) {
            return false;
        }
    }

    auto itCivic = m_civics.find(civicId);
    if (itCivic == m_civics.end()) {
        return false;
    }

    const Civic& civic = itCivic->second;

    // Prereq institutions.
    for (int requiredCivicId : civic.requiredCivics) {
        bool found = false;
        if (itCountry != m_unlockedCivics.end()) {
            const auto& unlocked = itCountry->second;
            found = (std::find(unlocked.begin(), unlocked.end(), requiredCivicId) != unlocked.end());
        }
        if (!found) {
            return false;
        }
    }

    // Tech gates.
    for (int requiredTechId : civic.requiredTechs) {
        if (!TechnologyManager::hasTech(techManager, country, requiredTechId)) {
            return false;
        }
    }

    // Feasibility gates (state capacity / development).
    if (urbanizationOf(country) < civic.minUrbanization) return false;
    if (country.getAdminCapacity() < civic.minAdminCapacity) return false;
    if (country.getAvgControl() < civic.minAvgControl) return false;

    return true;
}

void CultureManager::unlockCivic(Country& country, int civicId) {
    auto itCivic = m_civics.find(civicId);
    if (itCivic == m_civics.end()) {
        return;
    }
    const Civic& civic = itCivic->second;
    auto& ldbg = country.getMacroEconomyMutable().legitimacyDebug;
    const double legitBefore = clamp01(country.getLegitimacy());

    m_unlockedCivics[country.getCountryIndex()].push_back(civicId);

    if (civic.stabilityHit != 0.0) country.setStability(country.getStability() - civic.stabilityHit);
    if (civic.legitimacyHit != 0.0) country.setLegitimacy(country.getLegitimacy() - civic.legitimacyHit);
    if (civic.debtAdd != 0.0) country.addDebt(civic.debtAdd);

    if (civic.adminCapBonus != 0.0) country.addAdminCapacity(civic.adminCapBonus);
    if (civic.fiscalCapBonus != 0.0) country.addFiscalCapacity(civic.fiscalCapBonus);
    if (civic.logisticsBonus != 0.0) country.addLogisticsReach(civic.logisticsBonus);
    if (civic.educationShareBonus != 0.0) country.addEducationSpendingShare(civic.educationShareBonus);
    if (civic.healthShareBonus != 0.0) country.addHealthSpendingShare(civic.healthShareBonus);

    if (civic.stabilityBonus != 0.0) country.setStability(country.getStability() + civic.stabilityBonus);
    if (civic.legitimacyBonus != 0.0) country.setLegitimacy(country.getLegitimacy() + civic.legitimacyBonus);
    const double legitAfter = clamp01(country.getLegitimacy());
    ldbg.dbg_legit_delta_culture += (legitAfter - legitBefore);
    ldbg.dbg_legit_after_culture = legitAfter;

    // Trait nudges (one-time, small; long-run drift handled in tickYear).
    auto& t = country.getTraitsMutable();
    if (civicId == 1) { // law
        t[Legalism] = clamp01(t[Legalism] + 0.05);
    } else if (civicId == 4) { // guilds
        t[Mercantile] = clamp01(t[Mercantile] + 0.04);
        t[Openness] = clamp01(t[Openness] + 0.03);
    } else if (civicId == 5) { // army
        t[Militarism] = clamp01(t[Militarism] + 0.05);
        t[Hierarchy] = clamp01(t[Hierarchy] + 0.03);
    } else if (civicId == 7) { // education
        t[Openness] = clamp01(t[Openness] + 0.03);
        t[Legalism] = clamp01(t[Legalism] + 0.02);
    } else if (civicId == 10) { // councils
        t[Hierarchy] = clamp01(t[Hierarchy] - 0.04);
        t[Legalism] = clamp01(t[Legalism] + 0.02);
    }

    if (s_debugMode) {
        cout << country.getName() << " adopted institution: " << civic.name << endl;
    }
}

void CultureManager::tickYear(std::vector<Country>& countries,
                              const Map& map,
                              const TechnologyManager& techManager,
                              const std::vector<float>* tradeIntensityMatrix,
                              int currentYear,
                              int dtYears,
                              News& news) {
    const int years = std::max(1, dtYears);
    const double yearsD = static_cast<double>(years);

    const int n = static_cast<int>(countries.size());
    if (n <= 0) return;

    std::vector<Country::TraitVec> delta(static_cast<size_t>(n), Country::TraitVec{});

    auto diffusePair = [&](int a, int b, double w, double rate) {
        if (a < 0 || b < 0 || a >= n || b >= n || a == b) return;
        if (w <= 0.0) return;
        const double r = rate * w * yearsD;
        if (r <= 0.0) return;
        const auto& ta = countries[static_cast<size_t>(a)].getTraits();
        const auto& tb = countries[static_cast<size_t>(b)].getTraits();
        for (int k = 0; k < Country::kTraits; ++k) {
            const double va = ta[static_cast<size_t>(k)];
            const double vb = tb[static_cast<size_t>(k)];
            if (vb > va) delta[static_cast<size_t>(a)][static_cast<size_t>(k)] += r * (vb - va);
            else if (va > vb) delta[static_cast<size_t>(b)][static_cast<size_t>(k)] += r * (va - vb);
        }
    };

    // Border diffusion (slow).
    for (int a = 0; a < n; ++a) {
        if (countries[static_cast<size_t>(a)].getPopulation() <= 0) continue;
        for (int b : map.getAdjacentCountryIndicesPublic(a)) {
            if (b <= a || b < 0 || b >= n) continue;
            if (countries[static_cast<size_t>(b)].getPopulation() <= 0) continue;
            const int contact = std::max(1, map.getBorderContactCount(a, b));
            const double w = clamp01(std::log1p(static_cast<double>(contact)) / 5.0);
            diffusePair(a, b, w, /*rate*/0.003);
        }
    }

    // Trade diffusion (faster than borders, still slow vs knowledge).
    if (tradeIntensityMatrix && !tradeIntensityMatrix->empty()) {
        const size_t need = static_cast<size_t>(n) * static_cast<size_t>(n);
        if (tradeIntensityMatrix->size() >= need) {
            for (int a = 0; a < n; ++a) {
                for (int b = a + 1; b < n; ++b) {
                    const float wab = (*tradeIntensityMatrix)[static_cast<size_t>(a) * static_cast<size_t>(n) + static_cast<size_t>(b)];
                    const float wba = (*tradeIntensityMatrix)[static_cast<size_t>(b) * static_cast<size_t>(n) + static_cast<size_t>(a)];
                    const double w = clamp01(static_cast<double>(std::max(wab, wba)));
                    if (w <= 0.001) continue;
                    diffusePair(a, b, w, /*rate*/0.010);
                }
            }
        }
    }

    // Pressure-driven trait dynamics (per-country).
    for (int i = 0; i < n; ++i) {
        Country& c = countries[static_cast<size_t>(i)];
        if (c.getPopulation() <= 0) continue;
        auto& t = c.getTraitsMutable();

        const auto& m = c.getMacroEconomy();
        const double famine = clamp01(1.0 - m.foodSecurity);
        const double war = c.isAtWar() ? 1.0 : 0.0;
        const double trade = clamp01(m.marketAccess);

        const double k = 0.020 * yearsD;
        t[Religiosity] = clamp01(t[Religiosity] + k * (0.80 * famine + 0.15 * war - 0.25 * trade));
        t[Collectivism] = clamp01(t[Collectivism] + k * (0.65 * famine + 0.10 * war - 0.20 * trade));
        t[Militarism] = clamp01(t[Militarism] + k * (0.85 * war - 0.15 * trade));
        t[Mercantile] = clamp01(t[Mercantile] + k * (0.80 * trade - 0.25 * war));
        t[Hierarchy] = clamp01(t[Hierarchy] + k * (0.55 * war + 0.25 * famine - 0.20 * trade));
        t[Openness] = clamp01(t[Openness] + k * (0.95 * trade - 0.55 * war - 0.30 * famine));
        t[Legalism] = clamp01(t[Legalism] + k * (0.30 * clamp01(c.getAdminCapacity()) + 0.20 * trade - 0.10 * war));

        // Weak drift back toward midpoints (prevents hard locks at 0/1).
        const double midK = 0.0025 * yearsD;
        for (int kTrait = 0; kTrait < Country::kTraits; ++kTrait) {
            t[static_cast<size_t>(kTrait)] = clamp01(t[static_cast<size_t>(kTrait)] + midK * (0.5 - t[static_cast<size_t>(kTrait)]));
        }
    }

    // Apply diffusion deltas.
    for (int i = 0; i < n; ++i) {
        auto& t = countries[static_cast<size_t>(i)].getTraitsMutable();
        for (int k = 0; k < Country::kTraits; ++k) {
            t[static_cast<size_t>(k)] = clamp01(t[static_cast<size_t>(k)] + delta[static_cast<size_t>(i)][static_cast<size_t>(k)]);
        }
    }

    // Institution adoption cadence (deterministic; at most one attempt per cadence).
    const int cadenceYears = 20;
    const int startYear = currentYear - years + 1;
    const int endYear = currentYear;
    for (int i = 0; i < n; ++i) {
        Country& c = countries[static_cast<size_t>(i)];
        if (c.getPopulation() <= 0) continue;

        // Stagger by country index to spread work and keep determinism.
        // dtYears-safe: if any simulated year in [startYear, endYear] hits the cadence, evaluate once.
        if (!intervalHitsStaggeredCadence(startYear, endYear, i, cadenceYears)) {
            continue;
        }

        const double pop = static_cast<double>(std::max<long long>(1, c.getPopulation()));
        const double urban = urbanizationOf(c);
        const double control = clamp01(c.getAvgControl());
        const double legitimacy = clamp01(c.getLegitimacy());
        const double debtStress = clamp01(c.getDebt() / (50.0 + pop * 0.002));
        const double legitStress = 1.0 - legitimacy;
        const double survivalStress = (c.isAtWar() ? 0.8 : 0.0) + (1.0 - control) * 0.6;

        struct Cand { int id; double score; };
        Cand best{ -1, 0.0 };

        for (const auto& kv : m_civics) {
            const int civicId = kv.first;
            const Civic& inst = kv.second;
            if (!canUnlockCivic(c, civicId, techManager)) continue;

            double s = 0.0;
            if (inst.fiscalCapBonus > 0.0) s += 0.8 * debtStress;
            if (inst.adminCapBonus > 0.0) s += 0.6 * (1.0 - control);
            if (inst.logisticsBonus > 0.0) s += 0.5 * (1.0 - clamp01(c.getMarketAccess()));
            if (inst.educationShareBonus > 0.0) s += 0.7 * urban;
            if (inst.healthShareBonus > 0.0) s += 0.6 * urban + 0.3 * clamp01(1.0 - c.getFoodSecurity());
            if (inst.legitimacyBonus > 0.0) s += 0.9 * legitStress;
            if (inst.name.find("Army") != std::string::npos) s += 0.9 * survivalStress;

            // Trait compatibility nudges: legalistic/mercantile societies adopt those institutions faster.
            const auto& t = c.getTraits();
            if (civicId == 1 || civicId == 2 || civicId == 6) s *= (0.70 + 0.60 * t[Legalism]);
            if (civicId == 4 || civicId == 11 || civicId == 12) s *= (0.70 + 0.60 * t[Mercantile]);

            if (s > best.score || (s == best.score && civicId < best.id)) {
                best = { civicId, s };
            }
        }

        if (best.id >= 0 && best.score >= 0.35) {
            const std::string name = m_civics[best.id].name;
            unlockCivic(c, best.id);
            news.addEvent("🏛️ Institution adopted: " + c.getName() + " implements " + name + ".");
        }
    }

    for (int i = 0; i < n; ++i) {
        Country& c = countries[static_cast<size_t>(i)];
        auto& ldbg = c.getMacroEconomyMutable().legitimacyDebug;
        ldbg.dbg_legit_after_culture = clamp01(c.getLegitimacy());
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
    static const std::vector<int> emptyVec;
    return emptyVec;
}
