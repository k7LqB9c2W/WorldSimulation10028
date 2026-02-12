// country.cpp

#include "country.h"
#include "map.h"
#include "technology.h"
#include "culture.h"
#include "simulation_context.h"
#include <random>
#include <algorithm>
#include <iostream>
#include <limits>
#include <mutex>
#include <unordered_set>
#include <queue>
#include <numeric>
#include <cmath> // For std::llround
#include <cctype>

// Initialize static science scaler (tuned for realistic science progression)
double Country::s_scienceScaler = 0.1;

namespace {
double clamp01d(double v) {
    return std::max(0.0, std::min(1.0, v));
}

std::string lowerAscii(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

struct RegionIdentity {
    int languageFamily = 0;
    int cultureFamily = 0;
    const char* language = "Proto-Local";
    const char* culture = "Local Culture";
};

RegionIdentity regionIdentityFromKey(const std::string& regionKey) {
    const std::string key = lowerAscii(regionKey);
    if (key.find("south_asia") != std::string::npos) return {10, 10, "Proto-Indic", "Indic Riverine"};
    if (key.find("east_asia") != std::string::npos) return {11, 11, "Proto-Sinitic", "East Riverine"};
    if (key.find("west_asia") != std::string::npos) return {12, 12, "Proto-Mesopotamian", "Fertile Crescent"};
    if (key.find("se_asia") != std::string::npos) return {13, 13, "Proto-Austroasiatic", "Monsoon Coastal"};
    if (key.find("cn_asia") != std::string::npos) return {14, 14, "Proto-Steppe", "Steppe Nomadic"};
    if (key.find("nile_ne_africa") != std::string::npos) return {15, 15, "Proto-Nile", "Nile Floodplain"};
    if (key.find("north_africa") != std::string::npos) return {16, 16, "Proto-Berberic", "North Saharan"};
    if (key.find("west_africa") != std::string::npos) return {17, 17, "Proto-Sahelian", "West Sahel"};
    if (key.find("east_africa") != std::string::npos) return {18, 18, "Proto-Cushitic", "East Horn"};
    if (key.find("cs_africa") != std::string::npos) return {19, 19, "Proto-Bantu", "Central Forest"};
    if (key.find("se_europe") != std::string::npos) return {20, 20, "Proto-Balkan", "Mediterranean Highland"};
    if (key.find("med_europe") != std::string::npos) return {21, 21, "Proto-Italic", "Mediterranean Urban"};
    if (key.find("central_europe") != std::string::npos) return {22, 22, "Proto-Continental", "Central Plain"};
    if (key.find("wnw_europe") != std::string::npos) return {23, 23, "Proto-Atlantic", "Atlantic Fringe"};
    if (key.find("north_europe") != std::string::npos) return {24, 24, "Proto-Nordic", "Northern Maritime"};
    if (key.find("mesoamerica") != std::string::npos) return {30, 30, "Proto-Meso", "Mesoamerican"};
    if (key.find("andes") != std::string::npos) return {31, 31, "Proto-Andean", "Andean Highland"};
    if (key.find("e_na") != std::string::npos) return {32, 32, "Proto-Woodland", "Eastern Woodland"};
    if (key.find("w_na") != std::string::npos) return {33, 33, "Proto-Plains", "Western Plains"};
    if (key.find("caribbean") != std::string::npos) return {34, 34, "Proto-Carib", "Caribbean Seafaring"};
    if (key.find("oceania") != std::string::npos) return {35, 35, "Proto-Oceanic", "Oceanic Navigators"};
    return {};
}

std::string evolveLanguageLabel(const std::string& current) {
    if (current.rfind("Proto-", 0) == 0) {
        return "Old " + current.substr(6);
    }
    if (current.rfind("Old ", 0) == 0) {
        return "Middle " + current.substr(4);
    }
    if (current.rfind("Middle ", 0) == 0) {
        return "Modern " + current.substr(7);
    }
    return current + " II";
}

std::string randomLeaderRootForRegion(std::mt19937_64& rng, const std::string& regionKey) {
    const std::string key = lowerAscii(regionKey);
    auto pick = [&](const std::vector<std::string>& pool) {
        std::uniform_int_distribution<int> idx(0, static_cast<int>(pool.size()) - 1);
        return pool[static_cast<size_t>(idx(rng))];
    };
    if (key.find("south_asia") != std::string::npos) return pick({"Asha", "Ravi", "Mitra", "Vasu", "Indra", "Nira"});
    if (key.find("east_asia") != std::string::npos) return pick({"Wei", "Han", "Lin", "Zhao", "Qin", "Ren"});
    if (key.find("west_asia") != std::string::npos) return pick({"Aru", "Nabu", "Tamar", "Eshar", "Belu", "Sena"});
    if (key.find("se_asia") != std::string::npos) return pick({"Suri", "Khai", "Lem", "Panna", "Rin", "Mali"});
    if (key.find("africa") != std::string::npos) return pick({"Kofi", "Amin", "Sefu", "Nala", "Zuri", "Tano"});
    if (key.find("europe") != std::string::npos) return pick({"Alden", "Bran", "Rhea", "Tarin", "Luka", "Mira"});
    if (key.find("mesoamerica") != std::string::npos) return pick({"Itza", "Yohu", "Tecu", "Nemi", "Cali", "Olin"});
    if (key.find("andes") != std::string::npos) return pick({"Inti", "Kusi", "Ayni", "Rumi", "Suma", "Tupa"});
    if (key.find("na") != std::string::npos) return pick({"Aponi", "Nodin", "Takoda", "Elan", "Kai", "Maka"});
    if (key.find("oceania") != std::string::npos) return pick({"Tane", "Maui", "Rangi", "Moana", "Kiri", "Hina"});
    return pick({"Arin", "Belan", "Cora", "Daren", "Elia", "Farin"});
}
} // namespace

// Constructor
Country::Country(int countryIndex,
                 const sf::Color& color,
                 const sf::Vector2i& startCell,
                 long long initialPopulation,
                 double growthRate,
                 const std::string& name,
                 Type type,
                 std::uint64_t rngSeed,
                 int foundingYear) :
    m_countryIndex(countryIndex),
	    m_rng(rngSeed),
	    m_color(color),
        m_foundingYear(foundingYear),
	    m_population(initialPopulation),
	    m_prevYearPopulation(initialPopulation),
	    m_populationGrowthRate(growthRate),
    m_culturePoints(0.0), // Initialize culture points to zero
    m_name(name),
    m_type(type),
    m_ideology(Ideology::Tribal), // All countries start as Tribal
    m_startingPixel(startCell), // Remember the founding location
    m_hasCity(false),
    m_gold(0.0),
    m_militaryStrength(0.0), // Initialize m_militaryStrength here
    m_isAtWar(false),
    m_warDuration(0),
    m_isWarofConquest(false),
    m_isWarofAnnihilation(false),
    m_peaceDuration(0),
    m_preWarPopulation(initialPopulation),
    m_prePlaguePopulation(initialPopulation),
    m_warCheckCooldown(0),
    m_warCheckDuration(0),
    m_isSeekingWar(false),
    m_sciencePoints(0.0),
    m_stability(1.0),
    m_stagnationYears(0),
    m_fragmentationCooldown(0),
    m_yearsSinceWar(0)
{
    addTerritoryCell(startCell);
    m_traits.fill(0.5);
    //intiialize war check

    // Set initial military strength based on type
    if (m_type == Type::Pacifist) {
        m_militaryStrength = 0.3;
    }
    else if (m_type == Type::Trader) {
        m_militaryStrength = 0.6;
        m_traitScienceMultiplier = 1.25; // Traders get bonus from trade knowledge
    }
    else if (m_type == Type::Warmonger) {
        m_militaryStrength = 1.3;
    }
    
    // Initialize education policy multiplier (could be modified by policies later)
    m_policyScienceMultiplier = 1.10; // Base education policy bonus
    
    // Initialize technology sharing timer for trader countries
    if (m_type == Type::Trader) {
        initializeTechSharingTimer(foundingYear);
    }

    // 🚀 STAGGERED OPTIMIZATION: Each country gets a random neighbor recalculation interval (20-80 years)
    std::uniform_int_distribution<> intervalDist(20, 80);
    m_neighborRecalculationInterval = intervalDist(m_rng);
    
    // Also add a random offset so countries don't all update in the same year
    std::uniform_int_distribution<> offsetDist(0, m_neighborRecalculationInterval - 1);
    m_neighborBonusLastUpdated = -999999 + offsetDist(m_rng);

    // Stagger initial war check year for Warmongers
    if (m_type == Type::Warmonger) {
        std::uniform_int_distribution<> staggerDist(foundingYear + 50, foundingYear + 550);
        m_nextWarCheckYear = staggerDist(m_rng);
    }

    // Stagger initial road-building check year to offset load
    {
        std::uniform_int_distribution<> initialRoadOffset(0, 120);
        m_nextRoadCheckYear = foundingYear + initialRoadOffset(m_rng);
    }

    // Stagger initial port-building check year to offset load
    {
        std::uniform_int_distribution<> initialPortOffset(0, 160);
        m_nextPortCheckYear = foundingYear + initialPortOffset(m_rng);
    }

    // Stagger initial airway-building check year to offset load
    {
        std::uniform_int_distribution<> initialAirwayOffset(0, 220);
        m_nextAirwayCheckYear = foundingYear + initialAirwayOffset(m_rng);
    }
    
    // 🎯 INITIALIZE EXPANSION CONTENTMENT SYSTEM - reuse existing gen
    
    // Stagger burst expansion timing to prevent lag spikes
    std::uniform_int_distribution<> staggerDist(0, 20);
    m_expansionStaggerOffset = staggerDist(m_rng);
    
    // Set initial expansion contentment based on country type
    std::uniform_int_distribution<> contentmentChance(1, 100);
    int roll = contentmentChance(m_rng);
    
    if (m_type == Type::Pacifist) {
        // Pacifists: 60% chance to be content, 5% chance permanent
        if (roll <= 5) {
            m_isContentWithSize = true;
            m_contentmentDuration = 999999; // Permanent contentment
        } else if (roll <= 60) {
            m_isContentWithSize = true;
            std::uniform_int_distribution<> durationDist(50, 300);
            m_contentmentDuration = durationDist(m_rng);
        }
    } else if (m_type == Type::Trader) {
        // Traders: 40% chance to be content, 2% chance permanent
        if (roll <= 2) {
            m_isContentWithSize = true;
            m_contentmentDuration = 999999; // Permanent contentment
        } else if (roll <= 40) {
            m_isContentWithSize = true;
            std::uniform_int_distribution<> durationDist(30, 200);
            m_contentmentDuration = durationDist(m_rng);
        }
	    } else { // Warmonger
	        // Warmongers: 15% chance to be content, 0.5% chance permanent
	        if (roll <= 1) { // 0.5% chance (rounded to 1%)
	            m_isContentWithSize = true;
	            m_contentmentDuration = 999999; // Permanent contentment (rare peaceful warmonger)
	        } else if (roll <= 15) {
	            m_isContentWithSize = true;
	            std::uniform_int_distribution<> durationDist(10, 100);
	            m_contentmentDuration = durationDist(m_rng);
	        }
	    }

	    // Phase 1 polity initialization: low-capability starts vary strongly by era and local path.
	    auto eraCapability = [](int year) {
	        if (year <= -20000) return 0.0;
	        if (year <= -5000) {
	            return 0.10 + 0.30 * static_cast<double>(year + 20000) / 15000.0;
	        }
	        if (year <= 0) {
	            return 0.40 + 0.25 * static_cast<double>(year + 5000) / 5000.0;
	        }
	        if (year >= 2025) return 1.0;
	        return 0.65 + 0.35 * static_cast<double>(year) / 2025.0;
	    };
	    std::uniform_real_distribution<double> jitter(-0.06, 0.06);
	    const double era = std::clamp(eraCapability(foundingYear), 0.0, 1.0);
	    const double jLegit = jitter(m_rng);
	    const double jAdmin = jitter(m_rng);
	    const double jFiscal = jitter(m_rng);
	    const double jLog = jitter(m_rng);
	    const double jTax = jitter(m_rng);
	    m_polity.legitimacy = std::clamp(0.50 + 0.26 * era + jLegit, 0.20, 0.95);
	    m_polity.adminCapacity = std::clamp(0.03 + 0.10 * era + jAdmin, 0.01, 0.65);
	    m_polity.fiscalCapacity = std::clamp(0.03 + 0.12 * era + jFiscal, 0.01, 0.75);
	    m_polity.logisticsReach = std::clamp(0.03 + 0.12 * era + jLog, 0.01, 0.75);
	    m_polity.taxRate = std::clamp(0.04 + 0.08 * era + 0.015 * jTax, 0.02, 0.30);
	    m_polity.treasurySpendRate = std::clamp(0.90 + 0.20 * era + 0.08 * jitter(m_rng), 0.55, 1.40);
	    m_polity.debt = 0.0;
	    if (m_type == Type::Warmonger) {
	        m_polity.militarySpendingShare = std::max(0.05, 0.40 + 0.04 * jitter(m_rng));
	        m_polity.adminSpendingShare = std::max(0.05, 0.31 + 0.04 * jitter(m_rng));
	        m_polity.infraSpendingShare = std::max(0.05, 0.29 + 0.04 * jitter(m_rng));
	    } else if (m_type == Type::Trader) {
	        m_polity.militarySpendingShare = std::max(0.05, 0.22 + 0.04 * jitter(m_rng));
	        m_polity.adminSpendingShare = std::max(0.05, 0.33 + 0.04 * jitter(m_rng));
	        m_polity.infraSpendingShare = std::max(0.05, 0.45 + 0.04 * jitter(m_rng));
	    } else {
	        m_polity.militarySpendingShare = std::max(0.05, 0.20 + 0.04 * jitter(m_rng));
	        m_polity.adminSpendingShare = std::max(0.05, 0.36 + 0.04 * jitter(m_rng));
	        m_polity.infraSpendingShare = std::max(0.05, 0.44 + 0.04 * jitter(m_rng));
	    }
	    const double shareSum =
	        m_polity.militarySpendingShare +
	        m_polity.adminSpendingShare +
	        m_polity.infraSpendingShare;
	    if (shareSum > 1e-9) {
	        m_polity.militarySpendingShare /= shareSum;
	        m_polity.adminSpendingShare /= shareSum;
	        m_polity.infraSpendingShare /= shareSum;
	    }
	    std::uniform_int_distribution<int> policyOffset(0, 4);
	    m_polity.lastPolicyYear = foundingYear + policyOffset(m_rng);
        {
            std::uniform_int_distribution<int> successionInterval(18, 45);
            m_nextSuccessionYear = foundingYear + successionInterval(m_rng);
        }

        initializeLeaderForEra(foundingYear);
        resetEliteBlocsForEra(foundingYear);
        m_lastNameChangeYear = foundingYear;
        initializePopulationCohorts();
        scheduleNextElection(foundingYear);
	}

void Country::initializeLeaderForEra(int foundingYear) {
    const double era =
        (foundingYear <= -5000) ? 0.15 :
        (foundingYear <= 0) ? 0.45 :
        0.75;
    std::uniform_real_distribution<double> jitter(-0.12, 0.12);
    std::uniform_int_distribution<int> ageDist(
        foundingYear <= -5000 ? 22 : 28,
        foundingYear <= -5000 ? 44 : 60);

    m_leader.name = randomLeaderRootForRegion(m_rng, m_spawnRegionKey);
    if (m_ideology == Ideology::Tribal || m_ideology == Ideology::Chiefdom) {
        m_leader.name = "Chief " + m_leader.name;
    }
    m_leader.age = ageDist(m_rng);
    m_leader.yearsInPower = 0;
    m_leader.competence = std::clamp(0.35 + 0.40 * era + jitter(m_rng), 0.10, 0.95);
    m_leader.coercion = std::clamp(0.50 + 0.20 * (m_type == Type::Warmonger ? 1.0 : 0.0) + jitter(m_rng), 0.05, 0.98);
    m_leader.diplomacy = std::clamp(0.38 + 0.28 * (m_type == Type::Trader ? 1.0 : 0.0) + jitter(m_rng), 0.05, 0.95);
    m_leader.reformism = std::clamp(0.30 + 0.45 * era + jitter(m_rng), 0.05, 0.95);
    m_leader.eliteAffinity = std::clamp(0.45 + 0.15 * (m_type == Type::Warmonger ? 1.0 : 0.0) + jitter(m_rng), 0.05, 0.95);
    m_leader.commonerAffinity = std::clamp(0.45 + 0.15 * (m_type == Type::Pacifist ? 1.0 : 0.0) + jitter(m_rng), 0.05, 0.95);
    m_leader.ambition = std::clamp(0.45 + 0.20 * (m_type == Type::Warmonger ? 1.0 : 0.0) + jitter(m_rng), 0.05, 0.95);

    // Archetypal variation to avoid near-identical leadership across seeds.
    std::uniform_real_distribution<double> u01(0.0, 1.0);
    const double draw = u01(m_rng);
    if (draw < 0.12) { // conquering founder
        m_leader.ambition = std::clamp(m_leader.ambition + 0.22 + 0.10 * u01(m_rng), 0.05, 0.98);
        m_leader.coercion = std::clamp(m_leader.coercion + 0.08 + 0.08 * u01(m_rng), 0.05, 0.98);
        m_leader.diplomacy = std::clamp(m_leader.diplomacy - 0.04, 0.05, 0.95);
    } else if (draw < 0.24) { // reform administrator
        m_leader.competence = std::clamp(m_leader.competence + 0.12 + 0.08 * u01(m_rng), 0.10, 0.98);
        m_leader.reformism = std::clamp(m_leader.reformism + 0.12 + 0.08 * u01(m_rng), 0.05, 0.98);
        m_leader.coercion = std::clamp(m_leader.coercion - 0.06, 0.05, 0.98);
    } else if (draw < 0.34) { // court-balancer
        m_leader.diplomacy = std::clamp(m_leader.diplomacy + 0.10 + 0.10 * u01(m_rng), 0.05, 0.98);
        m_leader.eliteAffinity = std::clamp(m_leader.eliteAffinity + 0.08, 0.05, 0.98);
        m_leader.ambition = std::clamp(m_leader.ambition - 0.05, 0.05, 0.98);
    }
}

void Country::resetEliteBlocsForEra(int foundingYear) {
    (void)foundingYear;
    m_eliteBlocs[0] = EliteBlocState{"Landed Clans", 0.34, 0.62, 0.20, 0.55};
    m_eliteBlocs[1] = EliteBlocState{"Warrior Houses", 0.28, 0.60, 0.22, 0.62};
    m_eliteBlocs[2] = EliteBlocState{"Ritual Authorities", 0.22, 0.64, 0.18, 0.48};
    m_eliteBlocs[3] = EliteBlocState{"Merchant Networks", 0.16, 0.58, 0.24, 0.42};
    m_socialClasses.shares = {0.82, 0.18, 0.0, 0.0, 0.0, 0.0};
    m_socialClasses.complexityLevel = 2;
    for (size_t i = 0; i < m_classAgents.size(); ++i) {
        ClassAgentState& a = m_classAgents[i];
        a.sentiment = 0.54;
        a.influence = std::max(0.0, m_socialClasses.shares[i]);
        a.tradePreference = 0.45;
        a.innovationPreference = 0.42;
        a.redistributionPreference = 0.58;
        a.externalNetwork = 0.0;
    }
    // Structured class priors (no scripted events, only persistent preferences).
    m_classAgents[static_cast<size_t>(SocialClass::Subsistence)].tradePreference = 0.22;
    m_classAgents[static_cast<size_t>(SocialClass::Subsistence)].innovationPreference = 0.28;
    m_classAgents[static_cast<size_t>(SocialClass::Subsistence)].redistributionPreference = 0.80;
    m_classAgents[static_cast<size_t>(SocialClass::Laborers)].tradePreference = 0.35;
    m_classAgents[static_cast<size_t>(SocialClass::Laborers)].innovationPreference = 0.45;
    m_classAgents[static_cast<size_t>(SocialClass::Laborers)].redistributionPreference = 0.68;
    m_classAgents[static_cast<size_t>(SocialClass::Artisans)].tradePreference = 0.58;
    m_classAgents[static_cast<size_t>(SocialClass::Artisans)].innovationPreference = 0.65;
    m_classAgents[static_cast<size_t>(SocialClass::Artisans)].redistributionPreference = 0.42;
    m_classAgents[static_cast<size_t>(SocialClass::Merchants)].tradePreference = 0.74;
    m_classAgents[static_cast<size_t>(SocialClass::Merchants)].innovationPreference = 0.62;
    m_classAgents[static_cast<size_t>(SocialClass::Merchants)].redistributionPreference = 0.30;
    m_classAgents[static_cast<size_t>(SocialClass::Bureaucrats)].tradePreference = 0.46;
    m_classAgents[static_cast<size_t>(SocialClass::Bureaucrats)].innovationPreference = 0.57;
    m_classAgents[static_cast<size_t>(SocialClass::Bureaucrats)].redistributionPreference = 0.50;
    m_classAgents[static_cast<size_t>(SocialClass::Elite)].tradePreference = 0.50;
    m_classAgents[static_cast<size_t>(SocialClass::Elite)].innovationPreference = 0.45;
    m_classAgents[static_cast<size_t>(SocialClass::Elite)].redistributionPreference = 0.20;
    m_eliteBargainingPressure = 0.0;
    m_commonerPressure = 0.0;
}

void Country::assignRegionalIdentityFromSpawnKey() {
    const RegionIdentity rid = regionIdentityFromKey(m_spawnRegionKey);
    m_languageFamilyId = rid.languageFamily;
    m_cultureFamilyId = rid.cultureFamily;
    m_languageName = rid.language;
    m_cultureIdentityName = rid.culture;

    if (m_leader.name == "Nameless Chief" || m_leader.name.empty()) {
        initializeLeaderForEra(-5000);
    }
    if (m_leader.name.rfind("Chief ", 0) == 0 || m_leader.name.rfind("Leader ", 0) == 0) {
        const std::string baseName = randomLeaderRootForRegion(m_rng, m_spawnRegionKey);
        if (m_ideology == Ideology::Tribal || m_ideology == Ideology::Chiefdom) {
            m_leader.name = "Chief " + baseName;
        } else if (m_ideology == Ideology::Kingdom || m_ideology == Ideology::Empire) {
            m_leader.name = "Ruler " + baseName;
        } else {
            m_leader.name = "Leader " + baseName;
        }
    }
}

void Country::transitionLeader(int currentYear, bool crisis, News& news) {
    m_lastLeaderTransitionYear = currentYear;
    const std::string oldName = m_leader.name;
    initializeLeaderForEra(currentYear);
    if (crisis) {
        m_leader.competence = std::max(0.10, m_leader.competence - 0.08);
        m_leader.coercion = std::min(0.98, m_leader.coercion + 0.08);
    }
    if (m_ideology == Ideology::Tribal || m_ideology == Ideology::Chiefdom) {
        if (m_leader.name.rfind("Chief ", 0) != 0) m_leader.name = "Chief " + m_leader.name;
    } else if (m_ideology == Ideology::Kingdom || m_ideology == Ideology::Empire || m_ideology == Ideology::Theocracy) {
        if (m_leader.name.rfind("Ruler ", 0) != 0) m_leader.name = "Ruler " + m_leader.name;
    } else {
        if (m_leader.name.rfind("Leader ", 0) != 0) m_leader.name = "Leader " + m_leader.name;
    }
    if (!oldName.empty() && oldName != m_leader.name) {
        news.addEvent(m_name + " installs a new leadership figure: " + m_leader.name + ".");
    }
}

void Country::scheduleNextElection(int currentYear) {
    if (m_ideology == Ideology::Democracy) {
        std::uniform_int_distribution<int> term(4, 6);
        m_nextElectionYear = currentYear + term(m_rng);
    } else if (m_ideology == Ideology::Republic || m_ideology == Ideology::Federation) {
        std::uniform_int_distribution<int> term(5, 8);
        m_nextElectionYear = currentYear + term(m_rng);
    } else {
        m_nextElectionYear = std::numeric_limits<int>::min();
    }
}

void Country::maybeRunElection(int currentYear, News& news) {
    auto clamp01 = [](double v) { return std::max(0.0, std::min(1.0, v)); };
    const bool electoralRegime =
        (m_ideology == Ideology::Republic) ||
        (m_ideology == Ideology::Democracy) ||
        (m_ideology == Ideology::Federation);
    if (!electoralRegime) {
        m_nextElectionYear = std::numeric_limits<int>::min();
        return;
    }
    if (m_nextElectionYear == std::numeric_limits<int>::min()) {
        scheduleNextElection(currentYear);
        return;
    }
    if (currentYear < m_nextElectionYear) {
        return;
    }

    const double economySignal = clamp01(
        0.45 * clamp01(m_macro.foodSecurity) +
        0.30 * clamp01(m_macro.realWage / 2.0) +
        0.25 * clamp01(m_macro.marketAccess));
    const double governanceSignal = clamp01(
        0.35 * clamp01(m_polity.legitimacy) +
        0.30 * clamp01(m_stability) +
        0.20 * clamp01(m_avgControl) +
        0.15 * clamp01(m_polity.adminCapacity));
    const double incumbencyStrength = clamp01(
        0.35 * m_leader.competence +
        0.20 * m_leader.diplomacy +
        0.20 * m_leader.commonerAffinity +
        0.10 * (1.0 - m_leader.coercion) +
        0.15 * (1.0 - m_commonerPressure));
    const double warPenalty = isAtWar() ? 0.22 : 0.0;
    const double retainProb = clamp01(
        0.22 +
        0.34 * economySignal +
        0.28 * governanceSignal +
        0.16 * incumbencyStrength -
        warPenalty);

    std::uniform_real_distribution<double> u01(0.0, 1.0);
    const bool incumbentWins = (u01(m_rng) < retainProb);
    m_lastElectionYear = currentYear;

    if (incumbentWins) {
        m_polity.legitimacy = clamp01(m_polity.legitimacy + 0.01 + 0.02 * retainProb);
        m_stability = clamp01(m_stability + 0.004 + 0.010 * retainProb);
        news.addEvent("Election in " + m_name + ": incumbent leadership is returned to office.");
    } else {
        transitionLeader(currentYear, false, news);
        m_polity.legitimacy = clamp01(m_polity.legitimacy + 0.02);
        m_stability = clamp01(m_stability - 0.01 + 0.03 * governanceSignal);
        m_polity.taxRate = std::clamp(m_polity.taxRate - 0.01 * (0.4 + 0.6 * m_leader.commonerAffinity), 0.02, 0.45);
        news.addEvent("Election in " + m_name + ": opposition leadership wins and forms a new government.");
    }

    scheduleNextElection(currentYear);
}

void Country::tickAgenticSociety(int currentYear,
                                 int techCount,
                                 const SimulationConfig& simCfg,
                                 News& news) {
    auto clamp01 = [](double v) { return std::max(0.0, std::min(1.0, v)); };
    const double pop = std::max(1.0, static_cast<double>(std::max<long long>(1, m_population)));
    const double urbanShare = clamp01(m_totalCityPopulation / pop);
    const double institution = clamp01(m_macro.institutionCapacity);
    const double marketAccess = clamp01(m_macro.marketAccess);
    const double connectivity = clamp01(m_macro.connectivityIndex);
    const double ideaMarket = clamp01(m_macro.ideaMarketIntegrationIndex);
    const double merchantPower = clamp01(m_macro.merchantPowerIndex);
    const double mediaThroughput = clamp01(m_macro.mediaThroughputIndex);
    const double humanCapital = clamp01(m_macro.humanCapital);
    const double knowledgeStock = clamp01(m_macro.knowledgeStock);
    const double credibility = clamp01(m_macro.credibleCommitmentIndex);
    const double ineq = clamp01(m_macro.inequality);
    const double famine = clamp01(m_macro.famineSeverity + std::max(0.0, 0.92 - m_macro.foodSecurity));
    const double warPressure = m_isAtWar ? 1.0 : 0.0;
    const double debtStress = clamp01(m_polity.debt / std::max(1.0, m_lastTaxTake * 8.0 + 1.0));
    const double creditStress = clamp01(
        std::max(0.0, simCfg.economy.creditFrictionWeight) * debtStress +
        (1.0 - std::max(0.0, simCfg.economy.creditFrictionWeight)) * clamp01(m_macro.leakageRate));
    const double infoFriction = clamp01(
        std::max(0.0, simCfg.economy.informationFrictionWeight) *
            (1.0 - clamp01(0.60 * connectivity + 0.40 * mediaThroughput)));
    const double capability = clamp01(
        0.34 * clamp01(m_polity.adminCapacity) +
        0.24 * clamp01(m_avgControl) +
        0.18 * institution +
        0.14 * marketAccess +
        0.10 * urbanShare);
    const double scienceDepth = clamp01(static_cast<double>(std::max(0, techCount)) / 55.0);
    const double stateCapacity = clamp01(0.45 * capability + 0.30 * institution + 0.25 * clamp01(m_avgControl));
    const double commercialDepth = clamp01(
        0.33 * marketAccess +
        0.23 * connectivity +
        0.18 * ideaMarket +
        0.16 * merchantPower +
        0.10 * mediaThroughput);
    const double bourgeoisEmergence = clamp01(
        0.26 * urbanShare +
        0.20 * commercialDepth +
        0.16 * scienceDepth +
        0.14 * institution +
        0.12 * humanCapital +
        0.12 * knowledgeStock -
        0.18 * famine -
        0.12 * warPressure -
        0.08 * creditStress -
        0.08 * infoFriction);

    int targetComplexity = 2;
    if (capability > 0.14 || techCount >= 8 || bourgeoisEmergence > 0.18) targetComplexity = 3;
    if (capability > 0.24 || techCount >= 15 || bourgeoisEmergence > 0.30) targetComplexity = 4;
    if (capability > 0.38 || techCount >= 24 || bourgeoisEmergence > 0.44) targetComplexity = 5;
    if (capability > 0.54 || techCount >= 36 || bourgeoisEmergence > 0.58) targetComplexity = 6;
    targetComplexity = std::max(2, std::min(6, targetComplexity));
    if (targetComplexity > m_socialClasses.complexityLevel && (currentYear % 15 == 0)) {
        m_socialClasses.complexityLevel = targetComplexity;
        news.addEvent(m_name + " develops more complex social strata and institutions.");
    }

    std::array<double, 6> targetShares{};
    targetShares[0] = std::clamp(0.84 - 0.44 * capability - 0.20 * urbanShare + 0.16 * famine, 0.06, 0.93);
    targetShares[1] = std::clamp(0.12 + 0.14 * urbanShare + 0.10 * capability - 0.05 * famine, 0.04, 0.54);
    targetShares[2] = (m_socialClasses.complexityLevel >= 3)
        ? std::clamp(0.02 + 0.10 * capability + 0.11 * urbanShare + 0.13 * bourgeoisEmergence + 0.08 * scienceDepth, 0.0, 0.28) : 0.0;
    targetShares[3] = (m_socialClasses.complexityLevel >= 4)
        ? std::clamp(0.01 + 0.16 * commercialDepth + 0.14 * bourgeoisEmergence + 0.05 * credibility - 0.04 * famine, 0.0, 0.26) : 0.0;
    targetShares[4] = (m_socialClasses.complexityLevel >= 5)
        ? std::clamp(0.01 + 0.14 * stateCapacity + 0.08 * institution + 0.08 * scienceDepth, 0.0, 0.22) : 0.0;
    targetShares[5] = (m_socialClasses.complexityLevel >= 6)
        ? std::clamp(0.03 + 0.09 * ineq + 0.05 * stateCapacity + 0.04 * debtStress, 0.02, 0.16)
        : std::clamp(0.02 + 0.06 * ineq + 0.03 * debtStress, 0.02, 0.10);

    double sumT = 0.0;
    for (double v : targetShares) sumT += std::max(0.0, v);
    if (sumT <= 1e-9) {
        targetShares = {0.82, 0.18, 0.0, 0.0, 0.0, 0.0};
        sumT = 1.0;
    }
    for (double& v : targetShares) v = std::max(0.0, v / sumT);

    const double classAdjust = std::clamp(0.08 + 0.08 * bourgeoisEmergence + 0.06 * famine, 0.06, 0.24);
    for (size_t i = 0; i < m_socialClasses.shares.size(); ++i) {
        m_socialClasses.shares[i] =
            (1.0 - classAdjust) * m_socialClasses.shares[i] + classAdjust * targetShares[i];
        if (static_cast<int>(i) >= m_socialClasses.complexityLevel) {
            m_socialClasses.shares[i] *= 0.85;
        }
    }
    double classSum = 0.0;
    for (double v : m_socialClasses.shares) classSum += std::max(0.0, v);
    if (classSum > 1e-9) {
        for (double& v : m_socialClasses.shares) v = std::max(0.0, v / classSum);
    }

    // Class-level agents: low-dimensional political economy actors.
    for (size_t ci = 0; ci < m_classAgents.size(); ++ci) {
        ClassAgentState& agent = m_classAgents[ci];
        const double share = clamp01(m_socialClasses.shares[ci]);
        const bool active = static_cast<int>(ci) < m_socialClasses.complexityLevel;
        const double activeMult = active ? 1.0 : 0.45;
        const double orgDepth =
            clamp01(0.28 + 0.30 * stateCapacity + 0.24 * commercialDepth + 0.18 * urbanShare) * activeMult;
        const double influenceTarget = share * (0.45 + 0.55 * orgDepth);
        agent.influence = clamp01(0.86 * agent.influence + 0.14 * influenceTarget);

        const double preferenceFit = clamp01(
            0.34 * (agent.tradePreference * commercialDepth +
                    (1.0 - agent.tradePreference) * (1.0 - famine)) +
            0.34 * (agent.innovationPreference * (0.55 * scienceDepth + 0.45 * bourgeoisEmergence) +
                    (1.0 - agent.innovationPreference) * (0.65 + 0.35 * stateCapacity)) +
            0.32 * (agent.redistributionPreference * (1.0 - ineq) +
                    (1.0 - agent.redistributionPreference) * (0.65 * credibility + 0.35 * merchantPower)));

        double hardship = 0.0;
        if (ci == static_cast<size_t>(SocialClass::Subsistence)) {
            hardship = clamp01(0.58 * famine + 0.18 * warPressure + 0.14 * ineq + 0.10 * creditStress);
        } else if (ci == static_cast<size_t>(SocialClass::Laborers)) {
            hardship = clamp01(0.35 * famine + 0.25 * ineq + 0.18 * warPressure + 0.22 * clamp01(1.0 - m_macro.realWage / 1.2));
        } else if (ci == static_cast<size_t>(SocialClass::Artisans)) {
            hardship = clamp01(0.30 * creditStress + 0.26 * infoFriction + 0.22 * warPressure + 0.22 * clamp01(1.0 - commercialDepth));
        } else if (ci == static_cast<size_t>(SocialClass::Merchants)) {
            hardship = clamp01(0.38 * clamp01(1.0 - credibility) + 0.24 * warPressure + 0.20 * creditStress + 0.18 * infoFriction);
        } else if (ci == static_cast<size_t>(SocialClass::Bureaucrats)) {
            hardship = clamp01(0.32 * clamp01(1.0 - stateCapacity) + 0.28 * clamp01(1.0 - institution) + 0.20 * warPressure + 0.20 * ineq);
        } else {
            hardship = clamp01(0.34 * debtStress + 0.28 * clamp01(m_polity.taxRate / 0.45) + 0.22 * warPressure + 0.16 * clamp01(1.0 - credibility));
        }
        const double sentimentTarget = clamp01(
            0.14 +
            0.40 * preferenceFit +
            0.18 * agent.externalNetwork +
            0.10 * stateCapacity +
            0.08 * m_leader.competence -
            0.32 * hardship);
        agent.sentiment = clamp01(0.88 * agent.sentiment + 0.12 * sentimentTarget);
    }
    double agentInfluenceSum = 0.0;
    for (const ClassAgentState& a : m_classAgents) {
        agentInfluenceSum += std::max(0.0, a.influence);
    }
    if (agentInfluenceSum > 1e-9) {
        for (ClassAgentState& a : m_classAgents) {
            a.influence = std::max(0.0, a.influence / agentInfluenceSum);
        }
    }

    const double militarism = clamp01(m_traits[2]);
    const double religiosity = clamp01(m_traits[0]);
    const double hierarchy = clamp01(m_traits[4]);
    const double mercantile = clamp01(m_traits[3]);
    std::array<double, 4> blocInfluenceTarget{
        std::clamp(0.42 * m_socialClasses.shares[0] + 0.22 * m_socialClasses.shares[1] + 0.10 * hierarchy, 0.05, 0.55), // landed
        std::clamp(0.20 + 0.22 * militarism + 0.12 * (m_isAtWar ? 1.0 : 0.0), 0.08, 0.50),                               // military
        std::clamp(0.14 + 0.20 * religiosity + 0.06 * hierarchy, 0.06, 0.45),                                              // ritual
        std::clamp(0.08 + 0.26 * m_socialClasses.shares[3] + 0.12 * mercantile + 0.18 * getBourgeoisInfluence(), 0.04, 0.50) // merchant
    };
    double inflSum = 0.0;
    for (double v : blocInfluenceTarget) inflSum += std::max(0.0, v);
    if (inflSum > 1e-9) {
        for (double& v : blocInfluenceTarget) v /= inflSum;
    }

    const double commonerSentiment = clamp01(
        0.58 * m_classAgents[static_cast<size_t>(SocialClass::Subsistence)].sentiment +
        0.42 * m_classAgents[static_cast<size_t>(SocialClass::Laborers)].sentiment);
    m_commonerPressure = clamp01(
        0.32 * famine +
        0.20 * ineq +
        0.20 * clamp01(m_polity.taxRate / 0.45) +
        0.12 * (1.0 - clamp01(m_avgControl)) +
        0.08 * warPressure +
        0.08 * (1.0 - commonerSentiment));
    const double bourgeoisSentiment = clamp01(
        0.52 * m_classAgents[static_cast<size_t>(SocialClass::Artisans)].sentiment +
        0.48 * m_classAgents[static_cast<size_t>(SocialClass::Merchants)].sentiment);
    const double bureaucratSentiment = m_classAgents[static_cast<size_t>(SocialClass::Bureaucrats)].sentiment;
    const double bourgeoisPressure =
        clamp01(getBourgeoisInfluence() * (1.0 - bourgeoisSentiment) * (0.70 + 0.30 * commercialDepth));
    const double bureaucratPressure =
        clamp01(m_classAgents[static_cast<size_t>(SocialClass::Bureaucrats)].influence * (1.0 - bureaucratSentiment));

    m_eliteBargainingPressure = 0.0;
    for (size_t i = 0; i < m_eliteBlocs.size(); ++i) {
        EliteBlocState& bloc = m_eliteBlocs[i];
        bloc.influence = 0.88 * bloc.influence + 0.12 * blocInfluenceTarget[i];
        const double extraction = clamp01(m_polity.taxRate / std::max(0.15, bloc.extractionTolerance));
        double eliteStress = clamp01(
            0.30 * clamp01(m_polity.debt / std::max(1.0, m_lastTaxTake * 5.0)) +
            0.25 * extraction +
            0.20 * (1.0 - clamp01(m_polity.legitimacy)) +
            0.15 * m_commonerPressure +
            0.10 * warPressure);
        if (i == 3) {
            eliteStress = clamp01(eliteStress + 0.30 * bourgeoisPressure);
        }
        bloc.grievance = clamp01(0.82 * bloc.grievance + 0.18 * eliteStress);
        const double alignment = clamp01(0.5 + 0.5 * (m_leader.eliteAffinity - 0.5));
        bloc.loyalty = clamp01(
            bloc.loyalty +
            0.018 * (alignment - 0.50) +
            0.014 * (clamp01(m_polity.legitimacy) - 0.50) -
            0.030 * bloc.grievance);
        m_eliteBargainingPressure += bloc.influence * clamp01(0.6 * bloc.grievance + 0.4 * (0.55 - bloc.loyalty));
    }
    m_eliteBargainingPressure = clamp01(m_eliteBargainingPressure);

    const double combinedPressure = clamp01(
        0.42 * m_eliteBargainingPressure +
        0.33 * m_commonerPressure +
        0.19 * bourgeoisPressure +
        0.06 * bureaucratPressure);
    if (combinedPressure > 0.35) {
        m_polity.treasurySpendRate = std::clamp(
            m_polity.treasurySpendRate - 0.05 * combinedPressure + 0.03 * (m_leader.ambition - 0.5),
            0.40, 1.45);
        m_polity.taxRate = std::clamp(
            m_polity.taxRate +
                0.010 * combinedPressure * (0.35 + 0.65 * m_leader.coercion) -
                0.004 * bourgeoisPressure * (0.55 + 0.45 * m_leader.reformism),
            0.02, 0.45);
        m_polity.adminSpendingShare = std::max(0.03, m_polity.adminSpendingShare + 0.014 * combinedPressure + 0.008 * bureaucratPressure);
        m_polity.infraSpendingShare = std::max(0.03, m_polity.infraSpendingShare + 0.009 * combinedPressure + 0.010 * bourgeoisPressure);
        m_polity.militarySpendingShare = std::max(0.03, m_polity.militarySpendingShare + 0.010 * m_eliteBargainingPressure);
        m_polity.educationSpendingShare = std::max(0.0, m_polity.educationSpendingShare + 0.008 * (bourgeoisPressure + bureaucratPressure));
        m_polity.rndSpendingShare = std::max(0.0, m_polity.rndSpendingShare + 0.010 * bourgeoisPressure * (0.55 + 0.45 * m_leader.reformism));
    } else if (m_commonerPressure > 0.28) {
        m_polity.taxRate = std::max(0.02, m_polity.taxRate - 0.006 * m_commonerPressure * (0.45 + 0.55 * m_leader.commonerAffinity));
        m_polity.infraSpendingShare = std::max(0.03, m_polity.infraSpendingShare + 0.010 * m_commonerPressure);
    } else if (bourgeoisPressure > 0.16) {
        m_polity.taxRate = std::max(0.02, m_polity.taxRate - 0.005 * bourgeoisPressure * (0.45 + 0.55 * m_leader.reformism));
        m_polity.infraSpendingShare = std::max(0.03, m_polity.infraSpendingShare + 0.012 * bourgeoisPressure);
        m_polity.educationSpendingShare = std::max(0.0, m_polity.educationSpendingShare + 0.010 * bourgeoisPressure);
        m_polity.rndSpendingShare = std::max(0.0, m_polity.rndSpendingShare + 0.012 * bourgeoisPressure);
    }

    if (bourgeoisEmergence > 0.60 &&
        m_socialClasses.complexityLevel >= 4 &&
        m_socialClasses.shares[static_cast<size_t>(SocialClass::Merchants)] > 0.10 &&
        m_socialClasses.shares[static_cast<size_t>(SocialClass::Artisans)] > 0.08 &&
        (currentYear % 37 == 0)) {
        news.addEvent(m_name + " sees autonomous urban commercial classes gain political leverage.");
    }

    m_leader.age = std::min(95, m_leader.age + 1);
    m_leader.yearsInPower = std::max(0, m_leader.yearsInPower + 1);
    m_leader.reformism = clamp01(
        m_leader.reformism +
        0.010 * bourgeoisPressure +
        0.006 * bureaucratPressure -
        0.006 * m_eliteBargainingPressure);
    m_leader.coercion = clamp01(
        m_leader.coercion +
        0.008 * combinedPressure +
        0.006 * m_eliteBargainingPressure -
        0.007 * bourgeoisPressure);
    m_leader.ambition = clamp01(
        m_leader.ambition +
        0.006 * m_eliteBargainingPressure +
        0.004 * (1.0 - commonerSentiment) -
        0.004 * warPressure);

    const double leaderLegitDelta =
        +0.008 * (m_leader.competence - 0.5) +
        +0.006 * (m_leader.commonerAffinity - 0.5) * (1.0 - m_commonerPressure) -
        0.010 * m_commonerPressure * (0.60 + 0.40 * m_leader.coercion) -
        0.008 * m_eliteBargainingPressure * (0.60 + 0.40 * (1.0 - m_leader.eliteAffinity)) +
        0.006 * bourgeoisPressure * (0.45 + 0.55 * m_leader.reformism);
    m_polity.legitimacy = clamp01(m_polity.legitimacy + leaderLegitDelta);

    const double leaderStabilityDelta =
        +0.010 * (m_leader.coercion - 0.5) * (0.40 + 0.60 * clamp01(m_polity.adminCapacity)) +
        +0.007 * (m_leader.competence - 0.5) -
        0.010 * combinedPressure -
        0.004 * bourgeoisPressure * (0.65 + 0.35 * m_leader.diplomacy);
    m_stability = clamp01(m_stability + leaderStabilityDelta);

    maybeRunElection(currentYear, news);

    const double driftRate =
        0.0010 * (1.0 - clamp01(m_macro.connectivityIndex)) +
        0.0006 * m_commonerPressure +
        0.0005 * m_eliteBargainingPressure;
    m_culturalDrift += driftRate;
    if (m_culturalDrift > 1.0 && (currentYear - m_lastLeaderTransitionYear) > 25) {
        const std::string oldLang = m_languageName;
        m_languageName = evolveLanguageLabel(m_languageName);
        m_culturalDrift *= 0.45;
        if (oldLang != m_languageName) {
            news.addEvent(m_name + " language shifts from " + oldLang + " to " + m_languageName + ".");
            if ((currentYear - m_lastNameChangeYear) >= 220) {
                std::uniform_real_distribution<double> u01(0.0, 1.0);
                if (u01(m_rng) < 0.22) {
                    std::string nextName = generate_country_name(m_rng, m_spawnRegionKey);
                    if (m_ideology == Ideology::Empire) {
                        nextName = "Empire of " + nextName;
                    } else if (m_ideology == Ideology::Kingdom || m_ideology == Ideology::Theocracy) {
                        nextName = "Kingdom of " + nextName;
                    } else if (m_ideology == Ideology::Republic || m_ideology == Ideology::Democracy) {
                        nextName = "Republic of " + nextName;
                    }
                    if (!nextName.empty() && nextName != m_name) {
                        const std::string oldName = m_name;
                        m_name = nextName;
                        m_lastNameChangeYear = currentYear;
                        news.addEvent(oldName + " adopts a new endonym: " + m_name + ".");
                    }
                }
            }
        }
    }

    if (m_leader.age > 74) {
        const double mortalityPressure = std::clamp((static_cast<double>(m_leader.age) - 74.0) / 20.0, 0.0, 1.0);
        std::uniform_real_distribution<double> u01(0.0, 1.0);
        if (u01(m_rng) < 0.08 * mortalityPressure) {
            m_nextSuccessionYear = std::min(m_nextSuccessionYear, currentYear);
        }
    }
    if (scienceDepth > 0.45) {
        m_leader.reformism = std::clamp(m_leader.reformism + 0.002 * (scienceDepth - 0.45), 0.05, 0.98);
    }
}

double Country::getBourgeoisInfluence() const {
    const auto art = static_cast<size_t>(SocialClass::Artisans);
    const auto mer = static_cast<size_t>(SocialClass::Merchants);
    const double mix = 0.48 * m_classAgents[art].influence + 0.52 * m_classAgents[mer].influence;
    return std::clamp(mix, 0.0, 1.0);
}

void Country::applyClassNetworkSignals(double artisanSignal,
                                       double merchantSignal,
                                       double bureaucratSignal,
                                       int dtYears) {
    const double yearsD = std::max(1.0, static_cast<double>(std::max(1, dtYears)));
    auto clamp01 = [](double v) { return std::max(0.0, std::min(1.0, v)); };
    const double alpha = std::clamp(0.16 * yearsD, 0.04, 0.42);
    auto applyOne = [&](SocialClass cls, double signal) {
        ClassAgentState& a = m_classAgents[static_cast<size_t>(cls)];
        const double s = clamp01(signal);
        a.externalNetwork = clamp01((1.0 - alpha) * a.externalNetwork + alpha * s);
        // Transnational class networks are weak but persistent amplifiers.
        a.sentiment = clamp01(a.sentiment + 0.05 * (s - 0.50));
    };
    applyOne(SocialClass::Artisans, artisanSignal);
    applyOne(SocialClass::Merchants, merchantSignal);
    applyOne(SocialClass::Bureaucrats, bureaucratSignal);
}

void Country::ensureTechStateSize(int techCount) {
    const int n = std::max(0, techCount);
    if (static_cast<int>(m_knownTechDense.size()) < n) {
        m_knownTechDense.resize(static_cast<size_t>(n), 0u);
    }
    if (static_cast<int>(m_adoptionTechDense.size()) < n) {
        m_adoptionTechDense.resize(static_cast<size_t>(n), 0.0f);
    }
    if (static_cast<int>(m_lowAdoptionYearsDense.size()) < n) {
        m_lowAdoptionYearsDense.resize(static_cast<size_t>(n), 0u);
    }
}

bool Country::knowsTechDense(int idx) const {
    if (idx < 0 || idx >= static_cast<int>(m_knownTechDense.size())) {
        return false;
    }
    return m_knownTechDense[static_cast<size_t>(idx)] != 0u;
}

float Country::adoptionDense(int idx) const {
    if (idx < 0 || idx >= static_cast<int>(m_adoptionTechDense.size())) {
        return 0.0f;
    }
    return std::clamp(m_adoptionTechDense[static_cast<size_t>(idx)], 0.0f, 1.0f);
}

void Country::setKnownTechDense(int idx, bool known) {
    if (idx < 0) return;
    ensureTechStateSize(idx + 1);
    m_knownTechDense[static_cast<size_t>(idx)] = known ? 1u : 0u;
    if (!known) {
        m_adoptionTechDense[static_cast<size_t>(idx)] = 0.0f;
        m_lowAdoptionYearsDense[static_cast<size_t>(idx)] = 0u;
    }
}

void Country::setAdoptionDense(int idx, float adoption) {
    if (idx < 0) return;
    ensureTechStateSize(idx + 1);
    m_adoptionTechDense[static_cast<size_t>(idx)] = std::clamp(adoption, 0.0f, 1.0f);
}

int Country::lowAdoptionYearsDense(int idx) const {
    if (idx < 0 || idx >= static_cast<int>(m_lowAdoptionYearsDense.size())) {
        return 0;
    }
    return static_cast<int>(m_lowAdoptionYearsDense[static_cast<size_t>(idx)]);
}

void Country::setLowAdoptionYearsDense(int idx, int years) {
    if (idx < 0) return;
    ensureTechStateSize(idx + 1);
    m_lowAdoptionYearsDense[static_cast<size_t>(idx)] =
        static_cast<uint16_t>(std::clamp(years, 0, 65535));
}

void Country::clearTechStateDense() {
    m_knownTechDense.clear();
    m_adoptionTechDense.clear();
    m_lowAdoptionYearsDense.clear();
}

bool Country::hasAdoptedTechId(const TechnologyManager& technologyManager, int techId, float threshold) const {
    const int dense = technologyManager.getTechDenseIndex(techId);
    if (dense < 0) return false;
    return adoptionDense(dense) >= std::clamp(threshold, 0.0f, 1.0f);
}

// Check if the country can declare war
bool Country::canDeclareWar() const {
    if (m_population <= 0) return false;
    if (m_peaceDuration > 0) return false;
    if (m_stability < 0.18) return false;
    if (m_polity.legitimacy < 0.12) return false;
    return m_enemies.size() < 5u;
}

// Start a war with a target country
void Country::startWar(Country& target, News& news) {
    if (&target == this) return;
    if (target.getPopulation() <= 0 || m_population <= 0) return;

    if (std::find(m_enemies.begin(), m_enemies.end(), &target) != m_enemies.end()) {
        return;
    }

    m_isAtWar = true;
    m_warExhaustion = 0.0;
    m_peaceDuration = 0;
    m_preWarPopulation = m_population;

    const double ourPower = getMilitaryStrength() * std::sqrt(std::max(1.0, static_cast<double>(m_population) / 10000.0));
    const double theirPower = target.getMilitaryStrength() * std::sqrt(std::max(1.0, static_cast<double>(target.getPopulation()) / 10000.0));
    const double ratio = (theirPower > 1e-6) ? (ourPower / theirPower) : 2.0;
    const double logistic = std::clamp(0.5 * getLogisticsReach() + 0.5 * getMarketAccess(), 0.0, 1.0);
    const int baseWarDuration = std::clamp(
        8 + static_cast<int>(std::round(10.0 / std::max(0.6, ratio))) +
        static_cast<int>(std::round(8.0 * (1.0 - logistic))),
        6, 36);
    const double durationReduction = getWarDurationReduction();
    m_warDuration = std::max(4, static_cast<int>(std::round(static_cast<double>(baseWarDuration) * (1.0 - durationReduction))));

    m_activeWarGoal = m_pendingWarGoal;
    m_isWarofAnnihilation = (m_activeWarGoal == WarGoal::Annihilation);
    m_isWarofConquest = (m_activeWarGoal == WarGoal::BorderShift || m_activeWarGoal == WarGoal::Vassalization);

    addEnemy(&target);

    // Symmetric war state so both polities actually fight and wars can persist across years.
    target.m_isAtWar = true;
    target.m_warExhaustion = std::min(target.m_warExhaustion, 0.2);
    target.m_peaceDuration = 0;
    target.m_preWarPopulation = target.m_population;
    if (target.m_warDuration <= 0) {
        const double backRatio = (ourPower > 1e-6) ? (theirPower / ourPower) : 0.5;
        const double backLogistics = std::clamp(0.5 * target.getLogisticsReach() + 0.5 * target.getMarketAccess(), 0.0, 1.0);
        const int backDuration = std::clamp(
            8 + static_cast<int>(std::round(10.0 / std::max(0.6, backRatio))) +
            static_cast<int>(std::round(8.0 * (1.0 - backLogistics))),
            6, 36);
        target.m_warDuration = std::max(4, backDuration);
    }
    if (target.m_activeWarGoal == WarGoal::BorderShift) {
        const double fragility = std::clamp((1.0 - target.getStability()) * 0.6 + (1.0 - target.getLegitimacy()) * 0.4, 0.0, 1.0);
        target.m_activeWarGoal = (fragility > 0.60) ? WarGoal::Raid : WarGoal::BorderShift;
    }
    target.addEnemy(this);

    auto warGoalLabel = [&](WarGoal goal) -> const char* {
        switch (goal) {
            case WarGoal::Raid: return "raid";
            case WarGoal::BorderShift: return "border";
            case WarGoal::Tribute: return "tribute";
            case WarGoal::Vassalization: return "vassalization";
            case WarGoal::RegimeChange: return "regime-change";
            case WarGoal::Annihilation: return "annihilation";
            default: return "war";
        }
    };
    news.addEvent(m_name + " has declared war on " + target.getName() + " (" + warGoalLabel(m_activeWarGoal) + ").");
}

// End the current war
void Country::endWar(int currentYear) {
    const int durationBefore = std::max(0, m_warDuration);
    const double exhaustionBefore = std::clamp(m_warExhaustion, 0.0, 1.0);
    const std::vector<Country*> enemies = m_enemies;

    m_isAtWar = false;
    m_warDuration = 0;
    m_isWarofAnnihilation = false;
    m_isWarofConquest = false;
    m_activeWarGoal = WarGoal::BorderShift;
    m_warExhaustion = 0.0;
    m_warSupplyCapacity = 0.0;
    m_peaceDuration = std::clamp(10 + static_cast<int>(std::round(30.0 * (1.0 - m_stability))), 8, 40);

    // Record war end time and clear bilateral enemy links.
    for (Country* enemy : enemies) {
        if (!enemy) continue;
        recordWarEnd(enemy->getCountryIndex(), currentYear);
        enemy->recordWarEnd(m_countryIndex, currentYear);
        enemy->removeEnemy(this);
        if (enemy->getEnemies().empty()) {
            enemy->m_isAtWar = false;
            enemy->m_warDuration = 0;
            enemy->m_isWarofAnnihilation = false;
            enemy->m_isWarofConquest = false;
            enemy->m_activeWarGoal = WarGoal::BorderShift;
            enemy->m_warSupplyCapacity = 0.0;
            enemy->m_warExhaustion = std::max(0.0, enemy->m_warExhaustion * 0.35);
        }
        enemy->m_peaceDuration = std::max(enemy->m_peaceDuration, std::clamp(6 + static_cast<int>(std::round(24.0 * (1.0 - enemy->m_stability))), 6, 36));
    }

    clearEnemies();

    // War deaths scale with duration and exhaustion, instead of a flat 10% cut.
    if (m_population > 0) {
        const double deathFrac = std::clamp(0.01 + 0.0015 * static_cast<double>(durationBefore) + 0.08 * exhaustionBefore, 0.0, 0.20);
        const long long deaths = static_cast<long long>(std::llround(static_cast<double>(m_population) * deathFrac));
        m_population = std::max(0LL, m_population - deaths);
    }
    m_conquestMomentum *= 0.55;
}

void Country::clearWarState() {
    m_isAtWar = false;
    m_warDuration = 0;
    m_isWarofAnnihilation = false;
    m_isWarofConquest = false;
    m_activeWarGoal = WarGoal::BorderShift;
    m_warExhaustion = 0.0;
    m_warSupplyCapacity = 0.0;
    m_peaceDuration = 0;
    m_conquestMomentum = 0.0;
    clearEnemies();
}

// Check if the country is currently at war
bool Country::isAtWar() const {
    return m_isAtWar;
}

// Get the remaining war duration
int Country::getWarDuration() const {
    return m_warDuration;
}

// Set the war duration
void Country::setWarDuration(int duration) {
    m_warDuration = duration;
}

// Decrement the war duration by one year
void Country::decrementWarDuration() {
    if (m_warDuration > 0) {
        m_warDuration--;
    }
}

// Check if the current war is a War of Annihilation
bool Country::isWarofAnnihilation() const {
    return m_isWarofAnnihilation;
}

// Set the war type to Annihilation
void Country::setWarofAnnihilation(bool isannihilation) {
    m_isWarofAnnihilation = isannihilation;
}

// Check if the current war is a War of Conquest
bool Country::isWarofConquest() const {
    return m_isWarofConquest;
}

// Set the war type to Conquest
void Country::setWarofConquest(bool isconquest) {
    m_isWarofConquest = isconquest;
}

// Get the remaining peace duration
int Country::getPeaceDuration() const {
    return m_peaceDuration;
}

// Set the peace duration
void Country::setPeaceDuration(int duration) {
    m_peaceDuration = duration;
}

// Decrement the peace duration by one year
void Country::decrementPeaceDuration() {
    if (m_peaceDuration > 0) {
        m_peaceDuration--;
    }
}

// Check if the country is at peace
bool Country::isAtPeace() const {
    return m_peaceDuration == 0;
}

// Add a conquered city to the country's list
void Country::addConqueredCity(const City& city) {
    m_cities.push_back(city);
}

// Get the list of enemy countries
const std::vector<Country*>& Country::getEnemies() const {
    return m_enemies;
}

// Add an enemy to the country's enemy list
void Country::addEnemy(Country* enemy) {
    if (std::find(m_enemies.begin(), m_enemies.end(), enemy) == m_enemies.end()) {
        m_enemies.push_back(enemy);
    }
}

// Remove an enemy from the country's enemy list
void Country::removeEnemy(Country* enemy) {
    auto it = std::find(m_enemies.begin(), m_enemies.end(), enemy);
    if (it != m_enemies.end()) {
        m_enemies.erase(it);
    }
}

// Clear all enemies from the country's enemy list
void Country::clearEnemies() {
    m_enemies.clear();
}

// Set the country's population
void Country::setPopulation(long long population)
{
    m_population = population;
}

void Country::initializePopulationCohorts() {
    const double pop = static_cast<double>(std::max<long long>(0, m_population));
    // Pre-modern baseline age pyramid.
    m_popCohorts = {
        pop * 0.14, // 0-4
        pop * 0.24, // 5-14
        pop * 0.46, // 15-49
        pop * 0.10, // 50-64
        pop * 0.06  // 65+
    };
    renormalizePopulationCohortsToTotal();
}

void Country::renormalizePopulationCohortsToTotal() {
    const double target = static_cast<double>(std::max<long long>(0, m_population));
    double sum = 0.0;
    for (double v : m_popCohorts) {
        sum += std::max(0.0, v);
    }
    if (target <= 0.0) {
        m_popCohorts.fill(0.0);
        return;
    }
    if (sum <= 1e-9) {
        initializePopulationCohorts();
        return;
    }
    const double s = target / sum;
    for (double& v : m_popCohorts) {
        v = std::max(0.0, v * s);
    }
}

double Country::getWorkingAgeLaborSupply() const {
    // Most labor from 15-49, with lower participation in 50-64.
    return std::max(0.0, m_popCohorts[2] + 0.45 * m_popCohorts[3]);
}

double Country::getStability() const {
    return m_stability;
}

int Country::getYearsSinceWar() const {
    return m_yearsSinceWar;
}

bool Country::isFragmentationReady() const {
    return m_stability < 0.2 && m_fragmentationCooldown <= 0;
}

int Country::getFragmentationCooldown() const {
    return m_fragmentationCooldown;
}

void Country::setStability(double stability) {
    m_stability = std::max(0.0, std::min(1.0, stability));
}

void Country::setAvgControl(double v) {
    m_avgControl = std::max(0.0, std::min(1.0, v));
}

void Country::setTaxRate(double v) {
    m_polity.taxRate = std::clamp(v, 0.0, 0.8);
}

void Country::setBudgetShares(double military,
                              double admin,
                              double infra,
                              double health,
                              double education,
                              double rnd) {
    military = std::max(0.0, military);
    admin = std::max(0.0, admin);
    infra = std::max(0.0, infra);
    health = std::max(0.0, health);
    education = std::max(0.0, education);
    rnd = std::max(0.0, rnd);
    double sum = military + admin + infra + health + education + rnd;
    if (sum <= 1e-12) {
        military = 0.34;
        admin = 0.28;
        infra = 0.28;
        health = 0.05;
        education = 0.04;
        rnd = 0.01;
        sum = 1.0;
    }
    m_polity.militarySpendingShare = military / sum;
    m_polity.adminSpendingShare = admin / sum;
    m_polity.infraSpendingShare = infra / sum;
    m_polity.healthSpendingShare = health / sum;
    m_polity.educationSpendingShare = education / sum;
    m_polity.rndSpendingShare = rnd / sum;
}

void Country::setLegitimacy(double v) {
    m_polity.legitimacy = std::max(0.0, std::min(1.0, v));
}

void Country::addAdminCapacity(double dv) {
    m_polity.adminCapacity = std::max(0.0, std::min(1.0, m_polity.adminCapacity + dv));
}

void Country::addFiscalCapacity(double dv) {
    m_polity.fiscalCapacity = std::max(0.0, std::min(1.0, m_polity.fiscalCapacity + dv));
}

void Country::addLogisticsReach(double dv) {
    m_polity.logisticsReach = std::max(0.0, std::min(1.0, m_polity.logisticsReach + dv));
}

void Country::addDebt(double dv) {
    m_polity.debt = std::max(0.0, m_polity.debt + dv);
}

void Country::addEducationSpendingShare(double dv) {
    m_polity.educationSpendingShare = std::max(0.0, m_polity.educationSpendingShare + dv);
}

void Country::addHealthSpendingShare(double dv) {
    m_polity.healthSpendingShare = std::max(0.0, m_polity.healthSpendingShare + dv);
}

void Country::addRndSpendingShare(double dv) {
    m_polity.rndSpendingShare = std::max(0.0, m_polity.rndSpendingShare + dv);
}

void Country::applyBudgetFromEconomy(double taxBaseAnnual,
                                    double taxTakeAnnual,
                                    int dtYears,
                                    int techCount,
                                    bool plagueAffected,
                                    const SimulationConfig& simCfg) {
    auto clamp01 = [](double v) { return std::max(0.0, std::min(1.0, v)); };
    auto& sdbg = m_macro.stabilityDebug;
    auto& ldbg = m_macro.legitimacyDebug;

    const int years = std::max(1, dtYears);
    const double yearsD = static_cast<double>(years);

    setLastTaxStats(taxBaseAnnual, taxTakeAnnual);

    const double incomeAnnual = std::max(0.0, taxTakeAnnual);
    const double incomeSafe = std::max(1.0, incomeAnnual);
    sdbg.dbg_incomeAnnual = incomeAnnual;
    sdbg.dbg_avgControl = clamp01(m_avgControl);
    sdbg.dbg_delta_debt_crisis = 0.0;
    sdbg.dbg_delta_control_decay = 0.0;
    ldbg.dbg_legit_budget_incomeAnnual = incomeAnnual;
    ldbg.dbg_legit_budget_incomeSafe = incomeSafe;

    const double fastAlpha = std::clamp(simCfg.polity.revenueTrendFastAlpha, 0.05, 0.95);
    const double slowAlpha = std::clamp(simCfg.polity.revenueTrendSlowAlpha, 0.01, 0.50);
    if (!(m_revenueTrendFast >= 0.0) || !std::isfinite(m_revenueTrendFast)) {
        m_revenueTrendFast = incomeAnnual;
    }
    if (!(m_revenueTrendSlow >= 0.0) || !std::isfinite(m_revenueTrendSlow)) {
        m_revenueTrendSlow = incomeAnnual;
    }
    m_revenueTrendFast = fastAlpha * incomeAnnual + (1.0 - fastAlpha) * m_revenueTrendFast;
    m_revenueTrendSlow = slowAlpha * incomeAnnual + (1.0 - slowAlpha) * m_revenueTrendSlow;
    const double revenueTrendRatio = std::clamp(m_revenueTrendFast / std::max(1.0, m_revenueTrendSlow), 0.4, 1.6);
    const double trendDownPressure = clamp01((1.0 - revenueTrendRatio) / 0.35);
    const double trendUpSupport = clamp01((revenueTrendRatio - 1.0) / 0.45);

    // Desired spending is pressure-driven, then capped by what can be financed.
    const double institutionCapacity = clamp01(m_macro.institutionCapacity);
    const double connectivity = clamp01(m_macro.connectivityIndex);
    const double financeLevel = clamp01(0.5 * institutionCapacity + 0.5 * connectivity);
    const double marketAccess = clamp01(m_macro.marketAccess);

    const double control = clamp01(m_avgControl);
    const double lowControlPressure = clamp01((0.65 - control) / 0.65);
    const double faminePressure = clamp01(m_macro.famineSeverity + std::max(0.0, 0.92 - m_macro.foodSecurity));
    const double warPressure = m_isAtWar ? 1.0 : 0.0;
    const double opportunityPressure = clamp01(0.5 * clamp01(m_macro.marketAccess) + 0.5 * connectivity);
    const double pop = static_cast<double>(std::max<long long>(1, m_population));
    const double urbanShare = clamp01(m_totalCityPopulation / pop);
    const double capabilityIndex = clamp01(
        0.34 * clamp01(m_polity.adminCapacity) +
        0.24 * control +
        0.18 * institutionCapacity +
        0.14 * marketAccess +
        0.10 * urbanShare);
    const double lowCapabilityThreshold = std::clamp(simCfg.polity.lowCapabilityFiscalThreshold, 0.10, 0.90);
    const double lowCapabilityWeight = clamp01((lowCapabilityThreshold - capabilityIndex) / std::max(0.05, lowCapabilityThreshold));
    const double t = clamp01((capabilityIndex - 0.22) / 0.36);
    const double fiscalCoupling = t * t * (3.0 - 2.0 * t);
    const double fiscalPressure = 0.30 + 0.70 * fiscalCoupling;

    double desiredSpendFactor = std::clamp(m_polity.treasurySpendRate, 0.35, 2.20);
    desiredSpendFactor +=
        0.22 * warPressure +
        0.18 * lowControlPressure +
        0.18 * faminePressure +
        0.08 * opportunityPressure;
    const double trendSensitivity = std::clamp(simCfg.polity.revenueTrendSpendSensitivity, 0.0, 1.0);
    desiredSpendFactor *= 1.0 - trendSensitivity * trendDownPressure + 0.08 * (1.0 - lowCapabilityWeight) * trendUpSupport;
    desiredSpendFactor = std::clamp(desiredSpendFactor, 0.35, 2.20);

    if (lowCapabilityWeight > 0.0) {
        const double nearBalanceCap = std::clamp(simCfg.polity.lowCapabilityNearBalanceCap, 0.85, 1.20);
        const double emergencyHeadroom =
            0.08 * faminePressure +
            0.06 * warPressure +
            0.04 * lowControlPressure;
        const double cappedSpend = nearBalanceCap + emergencyHeadroom * (0.50 + 0.50 * (1.0 - lowCapabilityWeight));
        desiredSpendFactor = std::min(desiredSpendFactor, cappedSpend);
        m_polity.treasurySpendRate = std::min(m_polity.treasurySpendRate, cappedSpend + 0.05);
    }

    // Endogenous fiscal correction under debt-service pressure.
    const double debtStart = std::max(0.0, m_polity.debt);
    const double debtToIncomeStartRaw = debtStart / incomeSafe;
    const double debtToIncomeStart = std::clamp(debtToIncomeStartRaw, 0.0, 10.0);
    const double debtThresholdStart = 0.55 + 2.75 * financeLevel;
    const double stressAboveDebtThreshold = clamp01((debtToIncomeStart - debtThresholdStart) / 3.0);
    const double baselineInterest = 0.28 + (0.03 - 0.28) * financeLevel;
    const double serviceToIncomeStartRaw = (debtStart * baselineInterest) / incomeSafe;
    const double serviceToIncomeStart = std::clamp(serviceToIncomeStartRaw, 0.0, 10.0);
    const double serviceThreshold = std::clamp(simCfg.polity.debtServiceAusterityThreshold, 0.08, 0.65);
    const double serviceStressStart = clamp01((serviceToIncomeStart - serviceThreshold) / std::max(0.10, 1.0 - serviceThreshold));
    if (serviceToIncomeStart > serviceThreshold || debtToIncomeStart > debtThresholdStart) {
        const double correction = yearsD * (0.03 + 0.05 * serviceStressStart + 0.04 * stressAboveDebtThreshold);
        m_polity.treasurySpendRate = std::max(0.55, m_polity.treasurySpendRate - correction);

        const double fiscalHeadroom = clamp01((m_polity.fiscalCapacity - 0.20) / 0.80);
        const double taxEffort = yearsD * 0.010 * fiscalHeadroom * (0.35 + 0.65 * std::max(serviceStressStart, stressAboveDebtThreshold));
        m_polity.taxRate = std::clamp(m_polity.taxRate + taxEffort, 0.02, 0.45);
        ldbg.dbg_legit_budget_taxRateSource = 2; // budget debt-service adjustment

        desiredSpendFactor = std::max(0.52, desiredSpendFactor - (0.20 * serviceStressStart + 0.15 * stressAboveDebtThreshold));
    }

    if (serviceStressStart > 0.0) {
        const double austerityStrength = std::clamp(simCfg.polity.debtServiceAusterityStrength, 0.0, 1.0);
        const double austerityCap = std::clamp(
            1.0 - austerityStrength * (0.12 + 0.68 * serviceStressStart),
            0.50,
            1.02);
        desiredSpendFactor = std::min(desiredSpendFactor, austerityCap);
        m_polity.treasurySpendRate = std::min(m_polity.treasurySpendRate, austerityCap + 0.03);
    }

    const double desiredAnnual = std::max(0.0, incomeAnnual * desiredSpendFactor);
    const double desiredBlock = desiredAnnual * yearsD;

    double reserveMonthsTarget = std::clamp(
        0.55 +
        0.85 * (1.0 - financeLevel) +
        0.35 * lowControlPressure +
        0.20 * faminePressure,
        0.40,
        2.20);
    if (lowCapabilityWeight > 0.0) {
        reserveMonthsTarget = std::max(
            reserveMonthsTarget,
            std::max(0.25, simCfg.polity.lowCapabilityReserveMonthsTarget));
    }
    const double reserveTarget = incomeAnnual * reserveMonthsTarget;
    const double emergencyReserveRelease = reserveTarget * clamp01(0.20 * warPressure + 0.30 * faminePressure);
    const double maxDrawFromReserves = std::max(0.0, m_gold - std::max(0.0, reserveTarget - emergencyReserveRelease));

    const double stateDepth = clamp01(
        0.45 * clamp01(m_polity.adminCapacity) +
        0.35 * clamp01(m_polity.fiscalCapacity) +
        0.20 * institutionCapacity);
    const double networkDepth = clamp01(0.55 * connectivity + 0.45 * marketAccess);
    const double debtMarketSignal = clamp01(0.65 * stateDepth + 0.35 * networkDepth);
    const double debtAccessFloor = std::clamp(simCfg.polity.debtMarketAccessFloor, 0.0, 0.9);
    const double debtAccessSlope = std::clamp(simCfg.polity.debtMarketAccessSlope, 0.05, 1.0);
    double debtMarketAccess = clamp01((debtMarketSignal - debtAccessFloor) / debtAccessSlope);
    const double lowCapBorrowScale = std::clamp(simCfg.polity.lowCapabilityBorrowingScale, 0.0, 1.0);
    debtMarketAccess *= lowCapBorrowScale + (1.0 - lowCapBorrowScale) * (1.0 - lowCapabilityWeight);

    const bool borrowingEnabled = (debtMarketAccess >= 0.03);
    const double debtLimit =
        incomeAnnual *
        (0.05 + 3.20 * debtMarketAccess) *
        (0.20 + 0.80 * institutionCapacity) *
        (0.25 + 0.75 * stateDepth);
    const double maxNewBorrowing = borrowingEnabled ? std::max(0.0, debtLimit - debtStart) : 0.0;

    const double interestRate = 0.28 + (0.03 - 0.28) * clamp01(0.60 * debtMarketAccess + 0.40 * financeLevel);
    const double debtServiceAnnual = debtStart * interestRate;
    const double debtServiceBlock = debtServiceAnnual * yearsD;

    const double incomeBlock = incomeAnnual * yearsD;
    const double nonBorrowCapacity = incomeBlock + maxDrawFromReserves;
    const double debtServicePaid = std::min(debtServiceBlock, nonBorrowCapacity);
    const double debtServiceUnpaid = std::max(0.0, debtServiceBlock - debtServicePaid);

    const double financeable =
        std::max(0.0, nonBorrowCapacity - debtServicePaid) +
        maxNewBorrowing;
    const double actualSpending = std::min(desiredBlock, financeable);
    const double shortfall = std::max(0.0, desiredBlock - actualSpending);
    const double coreFloorShare = std::clamp(simCfg.polity.subsistenceAdminFloorShare, 0.25, 0.90);
    const double coreNeedShare = std::clamp(
        coreFloorShare +
        0.16 * faminePressure +
        0.08 * lowControlPressure +
        0.07 * warPressure -
        0.12 * capabilityIndex,
        0.35,
        0.92);
    const double coreNeedBlock = desiredBlock * coreNeedShare;
    const double coreSpending = std::min(actualSpending, coreNeedBlock);
    const double coreShortfall = std::max(0.0, coreNeedBlock - coreSpending);
    const double coreShortfallStress = clamp01(coreShortfall / std::max(1.0, coreNeedBlock));
    const double discretionaryNeed = std::max(0.0, desiredBlock - coreNeedBlock);
    const double discretionarySpending = std::max(0.0, actualSpending - coreSpending);
    const double discretionaryShortfall = std::max(0.0, discretionaryNeed - discretionarySpending);
    const double discretionaryShortfallStress = clamp01(discretionaryShortfall / std::max(1.0, discretionaryNeed));
    const double serviceShortfallStress = clamp01(0.78 * coreShortfallStress + 0.22 * discretionaryShortfallStress);

    const double borrowUsed = borrowingEnabled
        ? std::min(maxNewBorrowing, std::max(0.0, actualSpending - std::max(0.0, nonBorrowCapacity - debtServicePaid)))
        : 0.0;
    const double spendingFromOwnResources = std::max(0.0, actualSpending - borrowUsed);

    const double nonBorrowOutflow = debtServicePaid + spendingFromOwnResources;
    const double reservesUsed = std::max(0.0, nonBorrowOutflow - incomeBlock);
    const double incomeSurplusToReserves = std::max(0.0, incomeBlock - nonBorrowOutflow);
    m_gold = std::max(0.0, m_gold - reservesUsed + incomeSurplusToReserves);
    m_polity.debt = std::max(0.0, debtStart + debtServiceUnpaid + borrowUsed);

    const double shortfallStress = clamp01(shortfall / std::max(1.0, desiredBlock));
    const double debtToIncomeRaw = m_polity.debt / incomeSafe;
    const double serviceToIncomeRaw = debtServiceAnnual / incomeSafe;
    const double debtToIncome = std::clamp(debtToIncomeRaw, 0.0, 10.0);
    const double serviceToIncome = std::clamp(serviceToIncomeRaw, 0.0, 10.0);
    const double debtThreshold = 0.55 + 2.75 * std::max(financeLevel, debtMarketAccess);
    const double debtStress = clamp01((debtToIncome - debtThreshold) / 3.0);
    const double serviceStress = clamp01((serviceToIncome - serviceThreshold) / std::max(0.10, 1.0 - serviceThreshold));
    const double burdenStress = std::max(serviceStress, debtStress);

    ldbg.dbg_legit_budget_desiredBlock = desiredBlock;
    ldbg.dbg_legit_budget_actualSpending = actualSpending;
    ldbg.dbg_legit_budget_shortfall = shortfall;
    ldbg.dbg_legit_budget_shortfallStress = shortfallStress;
    ldbg.dbg_legit_budget_debtStart = debtStart;
    ldbg.dbg_legit_budget_debtEnd = std::max(0.0, m_polity.debt);
    ldbg.dbg_legit_budget_debtToIncome = debtToIncome;
    ldbg.dbg_legit_budget_debtToIncomeRaw = debtToIncomeRaw;
    ldbg.dbg_legit_budget_interestRate = interestRate;
    ldbg.dbg_legit_budget_debtServiceAnnual = debtServiceAnnual;
    ldbg.dbg_legit_budget_serviceToIncome = serviceToIncome;
    ldbg.dbg_legit_budget_serviceToIncomeRaw = serviceToIncomeRaw;
    ldbg.dbg_legit_budget_taxRate = std::clamp(m_polity.taxRate, 0.02, 0.45);
    ldbg.dbg_legit_budget_avgControl = std::clamp(m_avgControl, 0.0, 1.0);
    ldbg.dbg_legit_budget_stability = std::clamp(m_stability, 0.0, 1.0);
    ldbg.dbg_legit_budget_borrowingEnabled = borrowingEnabled;
    ldbg.dbg_legit_budget_debtLimit = debtLimit;
    ldbg.dbg_legit_budget_war = m_isAtWar;
    ldbg.dbg_legit_budget_plagueAffected = plagueAffected;
    ldbg.dbg_legit_budget_debtStress = debtStress;
    ldbg.dbg_legit_budget_serviceStress = serviceStress;
    ldbg.dbg_legit_budget_ratioOver5 = (debtToIncomeRaw > 5.0 || serviceToIncomeRaw > 5.0);

    auto applyBudgetLegitimacyDelta = [&](double delta) {
        const double before = clamp01(m_polity.legitimacy);
        const double target = before + delta;
        if (target < 0.0 && before > 0.0) {
            ldbg.dbg_legit_clamp_to_zero_budget++;
        }
        m_polity.legitimacy = clamp01(target);
        return clamp01(m_polity.legitimacy) - before;
    };

    // Financing shortfalls feed directly into state quality (without scripted policy rules).
    m_polity.adminCapacity = clamp01(m_polity.adminCapacity - yearsD * 0.012 * serviceShortfallStress * fiscalPressure);
    m_militaryStrength = std::max(
        0.0,
        m_militaryStrength * (1.0 - std::min(0.30, 0.08 * serviceShortfallStress * yearsD + 0.04 * discretionaryShortfallStress * yearsD)));
    ldbg.dbg_legit_budget_shortfall_direct = -(yearsD * 0.012 * serviceShortfallStress * fiscalPressure);
    applyBudgetLegitimacyDelta(ldbg.dbg_legit_budget_shortfall_direct);

    // Replace binary "negative gold crisis" with burden-scaled penalties.
    if (serviceToIncome > serviceThreshold || debtToIncome > debtThreshold) {
        const double before = m_stability;
        m_stability = clamp01(
            m_stability - yearsD * fiscalPressure *
            (0.012 * debtStress + 0.030 * serviceStress + 0.012 * serviceShortfallStress));
        sdbg.dbg_delta_debt_crisis += (m_stability - before);
        ldbg.dbg_legit_budget_burden_penalty =
            -(yearsD * fiscalPressure *
            (0.010 * debtStress + 0.026 * serviceStress + 0.010 * serviceShortfallStress));
        applyBudgetLegitimacyDelta(ldbg.dbg_legit_budget_burden_penalty);
        m_macro.leakageRate = std::clamp(
            m_macro.leakageRate + yearsD * fiscalPressure * (0.015 * burdenStress + 0.020 * serviceShortfallStress),
            0.02,
            0.95);
    } else {
        ldbg.dbg_legit_budget_burden_penalty = 0.0;
    }

    m_macro.educationInvestment = clamp01(m_polity.educationSpendingShare);
    m_macro.rndInvestment = clamp01(m_polity.rndSpendingShare);

    // Capacity accumulation (slow), driven by spending shares and current technical level.
    const double techFactor = 1.0 + 0.015 * static_cast<double>(std::max(0, techCount));
    m_polity.adminCapacity = clamp01(m_polity.adminCapacity + yearsD * (0.00035 * m_polity.adminSpendingShare * techFactor));
    m_polity.fiscalCapacity = clamp01(m_polity.fiscalCapacity + yearsD * (0.00030 * (0.8 * m_polity.adminSpendingShare + 0.2 * m_polity.rndSpendingShare) * techFactor));
    m_polity.logisticsReach = clamp01(m_polity.logisticsReach + yearsD * (0.00040 * m_polity.infraSpendingShare * techFactor));

    // Administrative capacity emerges from how many specialists a polity can sustain and coordinate.
    {
        const double specPop = std::max(0.0, m_specialistPopulation);
        const double specTerm = std::sqrt(std::max(0.0, specPop)); // diminishing returns
        const double eduShare = clamp01(m_polity.educationSpendingShare);
        const double stability = clamp01(m_stability);

        const double adminGrowth =
            yearsD * (3.0e-7 * specTerm * techFactor) *
            (0.45 + 0.55 * clamp01(m_polity.adminSpendingShare)) *
            (0.40 + 0.60 * eduShare) *
            (0.40 + 0.60 * stability);

        double stress = 0.0;
        if (m_isAtWar) stress += 1.0;
        stress += 0.9 * clamp01(m_polity.debt / std::max(1.0, incomeSafe * 6.0));
        stress += 0.7 * clamp01((0.60 - m_polity.legitimacy) / 0.60);
        stress += 0.7 * clamp01((0.70 - m_stability) / 0.70);
        stress += 0.8 * clamp01((0.92 - m_macro.foodSecurity) / 0.92);
        stress += 0.6 * clamp01((0.65 - m_avgControl) / 0.65);
        stress += 0.8 * serviceShortfallStress;

        const double adminDecay = yearsD * (0.00060 * stress);
        m_polity.adminCapacity = clamp01(m_polity.adminCapacity + adminGrowth - adminDecay);
    }

    // Legitimacy drift (annualized).
    {
        const double taxRate = std::clamp(m_polity.taxRate, 0.02, 0.45);
        const double control = std::clamp(m_avgControl, 0.0, 1.0);
        const double stability = std::clamp(m_stability, 0.0, 1.0);
        const double legitimacyNow = clamp01(m_polity.legitimacy);
        const double complianceNow = clamp01(m_macro.compliance);
        const double fiscalLegitWeight = std::clamp(
            1.0 - lowCapabilityWeight * (1.0 - std::clamp(simCfg.polity.earlyLegitimacyFiscalWeight, 0.0, 1.0)),
            0.05,
            1.0);
        const double earlyProvisioningWeight = std::clamp(simCfg.polity.earlyLegitimacyProvisioningWeight, 0.0, 1.0);
        const double provisioningSignal = clamp01(
            0.50 * clamp01(m_macro.foodSecurity) +
            0.28 * control +
            0.14 * (1.0 - warPressure) +
            0.08 * stability);
        const double provisioningNeed = clamp01(
            0.55 * faminePressure +
            0.25 * (1.0 - control) +
            0.20 * warPressure);
        const double provisioningDelta =
            yearsD *
            (0.012 + 0.016 * lowCapabilityWeight) *
            (provisioningSignal - provisioningNeed) *
            (0.25 + 0.75 * earlyProvisioningWeight);
        const double taxPain = clamp01(0.60 * (1.0 - legitimacyNow) + 0.40 * (1.0 - complianceNow));
        const double taxPenaltySlope = 0.014 + 0.026 * taxPain;
        ldbg.dbg_legit_budget_drift_stability = +0.002 * (stability - 0.5) * yearsD + provisioningDelta;
        ldbg.dbg_legit_budget_drift_tax = -std::max(0.0, taxRate - 0.12) * taxPenaltySlope * yearsD * fiscalPressure * fiscalLegitWeight;
        ldbg.dbg_legit_budget_drift_control = -(1.0 - control) * 0.010 * yearsD;
        ldbg.dbg_legit_budget_drift_debt = -0.008 * debtStress * yearsD * fiscalPressure * fiscalLegitWeight;
        ldbg.dbg_legit_budget_drift_service = -0.012 * serviceStress * yearsD * fiscalPressure * fiscalLegitWeight;
        ldbg.dbg_legit_budget_drift_shortfall = -0.010 * serviceShortfallStress * yearsD * fiscalPressure * fiscalLegitWeight;
        ldbg.dbg_legit_budget_drift_plague = plagueAffected ? (-0.02 * yearsD) : 0.0;
        ldbg.dbg_legit_budget_drift_war = m_isAtWar ? (-0.01 * yearsD) : 0.0;
        ldbg.dbg_legit_budget_drift_total =
            ldbg.dbg_legit_budget_drift_stability +
            ldbg.dbg_legit_budget_drift_tax +
            ldbg.dbg_legit_budget_drift_control +
            ldbg.dbg_legit_budget_drift_debt +
            ldbg.dbg_legit_budget_drift_service +
            ldbg.dbg_legit_budget_drift_shortfall +
            ldbg.dbg_legit_budget_drift_plague +
            ldbg.dbg_legit_budget_drift_war;
        applyBudgetLegitimacyDelta(ldbg.dbg_legit_budget_drift_total);

        // Recovery from deep legitimacy collapse when state capacity and basic welfare remain viable.
        const double lowLegit = clamp01((0.42 - clamp01(m_polity.legitimacy)) / 0.42);
        const double crisis = clamp01(
            0.45 * warPressure +
            0.35 * faminePressure +
            0.20 * serviceShortfallStress +
            0.20 * serviceStress);
        const double legitimacyRecovery =
            yearsD *
            std::max(0.0, simCfg.polity.legitimacyRecoveryStrength) *
            lowLegit *
            (0.35 + 0.65 * institutionCapacity) *
            (0.40 + 0.60 * clamp01(m_polity.adminCapacity)) *
            (0.45 + 0.55 * control) *
            (0.25 + 0.75 * clamp01(m_macro.foodSecurity)) *
            (1.0 - 0.80 * crisis);
        applyBudgetLegitimacyDelta(legitimacyRecovery);

        const double institutionalFloor =
            0.04 *
            clamp01(0.55 * institutionCapacity + 0.45 * clamp01(m_polity.adminCapacity)) *
            (1.0 - 0.80 * crisis);
        if (m_polity.legitimacy < institutionalFloor) {
            m_polity.legitimacy = institutionalFloor;
        }
    }

    // Low territorial control creates local failure that feeds back into stability.
    {
        const double before = m_stability;
        const double controlDecay = yearsD * (1.0 - std::clamp(m_avgControl, 0.0, 1.0)) * 0.006;
        m_stability = clamp01(m_stability - controlDecay);
        sdbg.dbg_delta_control_decay = (m_stability - before);
    }

    sdbg.dbg_gold = std::max(0.0, m_gold);
    sdbg.dbg_debt = std::max(0.0, m_polity.debt);
    sdbg.dbg_stab_after_budget = clamp01(m_stability);
    sdbg.dbg_stab_delta_budget = sdbg.dbg_stab_after_budget - sdbg.dbg_stab_after_country_update;
    ldbg.dbg_legit_budget_debtEnd = std::max(0.0, m_polity.debt);
    ldbg.dbg_legit_budget_taxRateAfter = std::clamp(m_polity.taxRate, 0.02, 0.45);
    ldbg.dbg_legit_after_budget = clamp01(m_polity.legitimacy);
    ldbg.dbg_legit_delta_budget = ldbg.dbg_legit_after_budget - ldbg.dbg_legit_after_economy;
}

void Country::setFragmentationCooldown(int years) {
    m_fragmentationCooldown = std::max(0, years);
}

void Country::setYearsSinceWar(int years) {
    m_yearsSinceWar = std::max(0, years);
}

void Country::resetStagnation() {
    m_stagnationYears = 0;
}

sf::Vector2i Country::getCapitalLocation() const {
    if (!m_cities.empty()) {
        for (const auto& city : m_cities) {
            if (city.getLocation() == m_startingPixel) {
                return m_startingPixel;
            }
        }
        const City* best = &m_cities.front();
        for (const auto& city : m_cities) {
            if (city.getPopulation() > best->getPopulation()) {
                best = &city;
                continue;
            }
            if (city.getPopulation() == best->getPopulation()) {
                const sf::Vector2i a = city.getLocation();
                const sf::Vector2i b = best->getLocation();
                if (a.y < b.y || (a.y == b.y && a.x < b.x)) {
                    best = &city;
                }
            }
        }
        return best->getLocation();
    }
    return m_startingPixel;
}

sf::Vector2i Country::getStartingPixel() const {
    return m_startingPixel;
}

void Country::setStartingPixel(const sf::Vector2i& cell) {
    m_startingPixel = cell;
}

void Country::setTerritory(const std::unordered_set<sf::Vector2i>& territory) {
    m_boundaryPixels = territory;
    m_territoryVec.assign(m_boundaryPixels.begin(), m_boundaryPixels.end());
    std::sort(m_territoryVec.begin(), m_territoryVec.end(), [](const sf::Vector2i& a, const sf::Vector2i& b) {
        if (a.y != b.y) return a.y < b.y;
        return a.x < b.x;
    });
    m_territoryIndex.clear();
    m_territoryIndex.reserve(m_territoryVec.size());
    for (size_t i = 0; i < m_territoryVec.size(); ++i) {
        m_territoryIndex[m_territoryVec[i]] = i;
    }
}

void Country::setCities(const std::vector<City>& cities) {
    m_cities = cities;
    m_hasCity = !m_cities.empty();
}

void Country::setRoads(const std::vector<sf::Vector2i>& roads) {
    m_roads = roads;
    m_roadsToCountries.clear();
}

void Country::clearRoadNetwork() {
    m_roads.clear();
    m_roadsToCountries.clear();
}

void Country::setFactories(const std::vector<sf::Vector2i>& factories) {
    m_factories = factories;
}

void Country::setPorts(const std::vector<sf::Vector2i>& ports) {
    m_ports = ports;
    std::sort(m_ports.begin(), m_ports.end(), [](const sf::Vector2i& a, const sf::Vector2i& b) {
        if (a.y != b.y) return a.y < b.y;
        return a.x < b.x;
    });
    m_ports.erase(std::unique(m_ports.begin(), m_ports.end()), m_ports.end());
}

void Country::clearPorts() {
    m_ports.clear();
}

// Check if another country is a neighbor
bool Country::isNeighbor(const Country& other) const {
    for (const auto& cell1 : m_boundaryPixels) {
        // Check all 8 neighboring cells (Moore neighborhood)
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue; // Skip the cell itself

                sf::Vector2i neighborCell = cell1 + sf::Vector2i(dx, dy);

                // Check if this neighboring cell belongs to the other country
                if (other.m_boundaryPixels.count(neighborCell)) {
                    return true;
                }
            }
        }
    }
    return false;
}

// 🔥 NUCLEAR OPTIMIZATION: Update the country's state each year
	void Country::update(const std::vector<std::vector<bool>>& isLandGrid, std::vector<std::vector<int>>& countryGrid, std::mutex& gridMutex, int gridCellSize, int regionSize, std::unordered_set<int>& dirtyRegions, int currentYear, const std::vector<std::vector<std::unordered_map<Resource::Type, double>>>& resourceGrid, News& news, bool plagueActive, long long& plagueDeaths, Map& map, const TechnologyManager& technologyManager, std::vector<Country>& allCountries) {
	    
	    std::mt19937_64& gen = m_rng;
	    const long long previousPopulation = (m_prevYearPopulation >= 0) ? m_prevYearPopulation : m_population;
	    const int techCount = static_cast<int>(technologyManager.getUnlockedTechnologies(*this).size());
	    const bool usePopGrid = map.isPopulationGridActive();

	    auto clamp01 = [](double v) {
	        return std::max(0.0, std::min(1.0, v));
	    };

        auto& sdbg = m_macro.stabilityDebug;
        sdbg.dbg_pop_country_before_update = static_cast<double>(std::max<long long>(0, m_population));
        sdbg.dbg_stab_start_year = clamp01(m_stability);

	    auto normalizeBudgetShares = [&]() {
	        m_polity.militarySpendingShare = std::max(0.02, m_polity.militarySpendingShare);
	        m_polity.adminSpendingShare = std::max(0.02, m_polity.adminSpendingShare);
	        m_polity.infraSpendingShare = std::max(0.02, m_polity.infraSpendingShare);
	        m_polity.healthSpendingShare = std::max(0.0, m_polity.healthSpendingShare);
	        m_polity.educationSpendingShare = std::max(0.0, m_polity.educationSpendingShare);
            m_polity.rndSpendingShare = std::max(0.0, m_polity.rndSpendingShare);
	        const double sum = m_polity.militarySpendingShare +
	                           m_polity.adminSpendingShare +
	                           m_polity.infraSpendingShare +
	                           m_polity.healthSpendingShare +
	                           m_polity.educationSpendingShare +
                               m_polity.rndSpendingShare;
	        if (sum <= 0.0) {
	            m_polity.militarySpendingShare = 0.34;
	            m_polity.adminSpendingShare = 0.28;
	            m_polity.infraSpendingShare = 0.38;
	            m_polity.healthSpendingShare = 0.0;
	            m_polity.educationSpendingShare = 0.0;
                m_polity.rndSpendingShare = 0.0;
	            return;
	        }
	        m_polity.militarySpendingShare /= sum;
	        m_polity.adminSpendingShare /= sum;
	        m_polity.infraSpendingShare /= sum;
	        m_polity.healthSpendingShare /= sum;
	        m_polity.educationSpendingShare /= sum;
            m_polity.rndSpendingShare /= sum;
	    };

	    normalizeBudgetShares();

	        // Phase 0-3 audit fix: ResourceManager must not accumulate free, static-map resources over time.
	        // Treat it as a per-year extraction/report scratch (it can be replaced by Phase 4 macro economy).
	        m_resourceManager = ResourceManager();

		    // Phase 4 integration: budgets/extraction are computed from the macro economy.
		    // Use last year's tax take as a local proxy for decision-making (updated in EconomyModelCPU).
		    const double income = std::max(0.0, m_lastTaxTake);
		    double spendRate = std::clamp(m_polity.treasurySpendRate, 0.3, 2.0);
		    if (m_isAtWar) {
		        spendRate = std::min(2.0, spendRate + 0.25);
		    }
		    const double expenses = income * spendRate;
        const SimulationConfig& simCfg = map.getConfig();

        // Regional polity state (cheap internal structure model for grievance/control/elite bargaining).
        {
            std::uniform_real_distribution<double> u01(0.0, 1.0);
            const int minRegions = std::max(1, simCfg.polity.regionCountMin);
            const int maxRegions = std::max(minRegions, simCfg.polity.regionCountMax);
            if (m_regions.empty()) {
                std::uniform_int_distribution<int> regionDist(minRegions, maxRegions);
                const int nRegions = regionDist(gen);
                m_regions.assign(static_cast<size_t>(nRegions), RegionalState{});

                std::vector<double> w(static_cast<size_t>(nRegions), 0.0);
                double sumW = 0.0;
                for (int r = 0; r < nRegions; ++r) {
                    w[static_cast<size_t>(r)] = 0.35 + u01(gen);
                    sumW += w[static_cast<size_t>(r)];
                }
                std::sort(w.begin(), w.end(), std::greater<double>());
                for (int r = 0; r < nRegions; ++r) {
                    RegionalState& rs = m_regions[static_cast<size_t>(r)];
                    rs.popShare = (sumW > 1e-9) ? (w[static_cast<size_t>(r)] / sumW) : (1.0 / static_cast<double>(nRegions));
                    rs.distancePenalty = (nRegions > 1) ? (static_cast<double>(r) / static_cast<double>(nRegions - 1)) : 0.0;
                    rs.localControl = clamp01(0.45 + 0.45 * m_avgControl * (1.0 - 0.6 * rs.distancePenalty));
                    rs.grievance = clamp01(0.08 + 0.20 * rs.distancePenalty);
                    rs.elitePower = clamp01(0.30 + 0.50 * u01(gen) + 0.15 * rs.distancePenalty);
                }
            }

            const double famine = clamp01(m_macro.famineSeverity);
            const double extraction = clamp01(m_polity.taxRate);
            const double legitimacy = clamp01(m_polity.legitimacy);
            const double adminCap = clamp01(m_polity.adminCapacity);
            const double infraShare = clamp01(m_polity.infraSpendingShare);
            const double war = m_isAtWar ? 1.0 : 0.0;
            const double eliteSensitivity = std::max(0.0, simCfg.polity.eliteDefectionSensitivity);
            const double farPenalty = std::max(0.0, simCfg.polity.farRegionPenalty);

            double defectionWeighted = 0.0;
            for (RegionalState& rs : m_regions) {
                const double controlTarget = clamp01(m_avgControl - farPenalty * rs.distancePenalty + 0.35 * adminCap + 0.15 * infraShare);
                rs.localControl = clamp01(rs.localControl + 0.35 * (controlTarget - rs.localControl));

                const double grievanceUp =
                    0.35 * extraction +
                    0.24 * (1.0 - legitimacy) +
                    0.20 * famine +
                    0.14 * war +
                    0.18 * (1.0 - rs.localControl) +
                    0.10 * rs.distancePenalty;
                const double grievanceDown =
                    0.32 * adminCap +
                    0.18 * infraShare +
                    0.18 * clamp01(m_macro.realWage / 2.0) +
                    0.10 * (1.0 - clamp01(m_macro.inequality));
                rs.grievance = clamp01(rs.grievance + 0.11 * grievanceUp - 0.08 * grievanceDown);

                const double defectionProb = clamp01(
                    eliteSensitivity *
                    (0.50 * rs.grievance +
                     0.22 * rs.elitePower +
                     0.18 * rs.distancePenalty +
                     0.10 * extraction) *
                    (1.0 - 0.55 * adminCap));
                if (u01(gen) < defectionProb * 0.10) {
                    rs.elitePower = clamp01(rs.elitePower + 0.06 + 0.12 * rs.grievance);
                    rs.grievance = clamp01(rs.grievance + 0.05);
                } else {
                    rs.elitePower = clamp01(rs.elitePower - 0.015 * (0.35 + adminCap));
                }

                defectionWeighted += rs.popShare * rs.elitePower * std::max(0.0, rs.grievance - 0.35);
            }
            m_eliteDefectionPressure = clamp01(0.85 * m_eliteDefectionPressure + 0.15 * defectionWeighted);

            // Succession shock cadence.
            if (currentYear >= m_nextSuccessionYear) {
                std::uniform_int_distribution<int> nextSuccession(
                    std::max(1, simCfg.polity.successionIntervalMin),
                    std::max(std::max(1, simCfg.polity.successionIntervalMin), simCfg.polity.successionIntervalMax));
                m_nextSuccessionYear = currentYear + nextSuccession(gen);

                const double leadershipFragility = clamp01(
                    0.35 * std::max(0.0, 0.55 - m_leader.competence) +
                    0.30 * std::max(0.0, 0.55 - m_leader.eliteAffinity) +
                    0.20 * m_eliteBargainingPressure +
                    0.15 * m_commonerPressure);
                const double risk = clamp01(
                    0.40 * m_eliteDefectionPressure +
                    0.22 * (1.0 - adminCap) +
                    0.18 * war +
                    0.14 * famine +
                    0.06 * (expenses > income ? 1.0 : 0.0) +
                    0.18 * leadershipFragility);
                const double draw = u01(gen);
                if (draw < risk) {
                    const double legitDrop = 0.06 + 0.16 * risk;
                    const double stabDrop = 0.04 + 0.12 * risk;
                    m_polity.legitimacy = clamp01(m_polity.legitimacy - legitDrop);
                    m_stability = clamp01(m_stability - stabDrop);
                    m_autonomyPressure = clamp01(m_autonomyPressure + 0.12 + 0.25 * risk);
                    m_autonomyOverThresholdYears += 2;
                    transitionLeader(currentYear, true, news);
                    news.addEvent("Succession crisis destabilizes " + m_name + ".");
                } else {
                    m_polity.legitimacy = clamp01(m_polity.legitimacy + 0.02 + 0.04 * (1.0 - risk));
                    m_stability = clamp01(m_stability + 0.01 + 0.03 * (1.0 - risk));
                    transitionLeader(currentYear, false, news);
                }
            }

            const double meanRegionalControl = std::accumulate(
                m_regions.begin(), m_regions.end(), 0.0,
                [](double acc, const RegionalState& rs) { return acc + rs.popShare * rs.localControl; });
            m_avgControl = clamp01(0.70 * m_avgControl + 0.30 * meanRegionalControl - 0.06 * m_eliteDefectionPressure);
            m_autonomyPressure = clamp01(m_autonomyPressure + 0.05 * m_eliteDefectionPressure);
        }

        tickAgenticSociety(currentYear, techCount, simCfg, news);

	    // Phase 1: pressures & constraint-driven action selection (cadenced).
	    struct Pressures { double survival = 0.0, revenue = 0.0, legitimacy = 0.0, opportunity = 0.0; };
	    Pressures pressures{};

	    auto militaryPower = [&](const Country& c) -> double {
	        const double pop = std::max(0.0, static_cast<double>(c.getPopulation()));
	        return c.getMilitaryStrength() * std::sqrt(std::max(1.0, pop / 10000.0));
	    };

	    const double ourPower = militaryPower(*this);
	    double worstThreatRatio = 0.0;
	    int bestTarget = -1;
	    double bestTargetScore = 0.0;
	    int borderExposure = 0;

	    const double attackerReadiness = clamp01(0.60 * clamp01(m_stability) + 0.40 * clamp01(m_polity.legitimacy));
	    for (int neighborIndex : map.getAdjacentCountryIndicesPublic(m_countryIndex)) {
	        if (neighborIndex < 0 || neighborIndex >= static_cast<int>(allCountries.size())) continue;
	        if (neighborIndex == m_countryIndex) continue;
	        const Country& n = allCountries[static_cast<size_t>(neighborIndex)];
	        if (n.getCountryIndex() != neighborIndex) continue;
	        if (n.getPopulation() <= 0) continue;

	        borderExposure++;
	        const double nPower = militaryPower(n);
	        const double threatRatio = (ourPower > 1e-6) ? (nPower / ourPower) : 1.0;
	        worstThreatRatio = std::max(worstThreatRatio, threatRatio);

        const double oppRatio = (nPower > 1e-6) ? (ourPower / nPower) : 2.0;
        const double preyFragility = clamp01(
            0.55 * (1.0 - clamp01(n.getStability())) +
            0.45 * (1.0 - clamp01(n.getLegitimacy())));
        const bool viableTarget =
            (oppRatio > 1.08) ||
            ((oppRatio > 0.92) && (preyFragility > 0.62));
        if (viableTarget) {
                const double affinity = computeCulturalAffinity(n);
                const double culturalDistance = 1.0 - affinity;
            const double score =
                std::min(2.2, std::max(0.65, oppRatio)) *
                (0.30 + 0.70 * preyFragility) *
                (0.45 + 0.55 * attackerReadiness) *
                    (0.50 + 0.80 * culturalDistance);
            if (score > bestTargetScore) {
                bestTargetScore = score;
                bestTarget = neighborIndex;
            }
	        }
	    }

	    pressures.survival = clamp01((worstThreatRatio - 1.0) * 0.7 + (std::min(12, borderExposure) / 12.0) * 0.3);
	    {
	        const double reservesYears = (income > 1.0) ? (m_gold / income) : 0.0;
	        const double debtYears = (income > 1.0) ? (m_polity.debt / income) : 0.0;
	        const double deficitRatio = (income > 1.0) ? std::max(0.0, (expenses - income) / income) : 0.0;
	        pressures.revenue = clamp01(0.40 * deficitRatio +
	                                    0.25 * std::max(0.0, 1.0 - reservesYears) +
	                                    0.20 * std::min(1.0, debtYears / 5.0) +
	                                    0.15 * std::max(0.0, 0.5 - m_polity.fiscalCapacity));
	    }
	    pressures.legitimacy = clamp01((1.0 - m_polity.legitimacy) * 0.7 + (1.0 - m_stability) * 0.3);

	    double frontierScore = 0.0;
	    if (!m_territoryVec.empty()) {
	        const int samples = std::min<int>(64, static_cast<int>(m_territoryVec.size()));
	        std::uniform_int_distribution<size_t> pick(0u, m_territoryVec.size() - 1u);
	        for (int s = 0; s < samples; ++s) {
	            const sf::Vector2i cell = m_territoryVec[pick(gen)];
	            static const int dirs4[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
	            for (const auto& d : dirs4) {
	                const int nx = cell.x + d[0];
	                const int ny = cell.y + d[1];
	                if (ny < 0 || ny >= static_cast<int>(countryGrid.size()) ||
	                    nx < 0 || nx >= static_cast<int>(countryGrid[0].size())) {
	                    continue;
	                }
	                if (!isLandGrid[static_cast<size_t>(ny)][static_cast<size_t>(nx)]) continue;
	                if (countryGrid[static_cast<size_t>(ny)][static_cast<size_t>(nx)] != -1) continue;
	                frontierScore += std::min(120.0, map.getCellFood(nx, ny));
	            }
	        }
	        frontierScore = std::min(1.0, frontierScore / (samples * 120.0));
	    }
    pressures.opportunity = clamp01(frontierScore * 0.65 + std::min(1.0, bestTargetScore / 2.0) * 0.35);

    const double leadershipCampaignDrive = clamp01(
        0.46 * m_leader.ambition +
        0.18 * m_leader.coercion +
        0.14 * m_leader.competence +
        0.22 * (1.0 - m_commonerPressure));
    const double weakStatePredation = clamp01(
        0.55 * (bestTargetScore > 0.0 ? std::min(1.0, bestTargetScore / 2.0) : 0.0) +
        0.25 * pressures.legitimacy +
        0.20 * pressures.opportunity);
    const double imperialDrive = clamp01(
        0.30 * leadershipCampaignDrive +
        0.20 * weakStatePredation +
        0.18 * clamp01(m_polity.logisticsReach) +
        0.14 * clamp01(m_polity.adminCapacity) +
        0.12 * clamp01(m_avgControl) +
        0.06 * ((m_ideology == Ideology::Empire || m_ideology == Ideology::Kingdom) ? 1.0 : 0.0));
    const double imperialWindow = clamp01((imperialDrive - 0.56) / 0.34);

	    const int cadence = (techCount < 25) ? 5 : 2;
	    if (currentYear - m_polity.lastPolicyYear >= cadence) {
	        m_polity.lastPolicyYear = currentYear;
	        m_expansionBudgetCells = 0;

	        double biggest = pressures.survival;
	        int kind = 0;
	        if (pressures.revenue > biggest) { biggest = pressures.revenue; kind = 1; }
	        if (pressures.legitimacy > biggest) { biggest = pressures.legitimacy; kind = 2; }
	        if (pressures.opportunity > biggest) { biggest = pressures.opportunity; kind = 3; }

	        if (kind == 0) {
	            m_polity.militarySpendingShare += 0.06;
	            m_polity.infraSpendingShare -= 0.03;
	            m_polity.adminSpendingShare -= 0.03;
	            m_polity.treasurySpendRate = std::min(2.0, m_polity.treasurySpendRate + 0.10);
	        } else if (kind == 1) {
	            if (m_polity.taxRate < 0.28) {
	                m_polity.taxRate += 0.02;
	            } else {
	                m_polity.treasurySpendRate = std::max(0.45, m_polity.treasurySpendRate - 0.10);
	            }
	            m_polity.adminSpendingShare += 0.03;
	            m_polity.infraSpendingShare -= 0.03;
	        } else if (kind == 2) {
	            m_polity.taxRate = std::max(0.02, m_polity.taxRate - 0.02);
	            m_polity.infraSpendingShare += 0.03;
	            m_polity.adminSpendingShare += 0.02;
	            m_polity.militarySpendingShare -= 0.05;
	            if (m_isAtWar && m_warDuration > 1) {
	                m_warDuration = std::min(m_warDuration, 2);
	            }
        } else {
            const double expansionScale =
                (0.45 + 0.90 * leadershipCampaignDrive) *
                (0.55 + 0.70 * clamp01(m_polity.logisticsReach)) *
                (0.55 + 0.55 * clamp01(m_avgControl)) *
                (1.0 + 0.95 * imperialWindow);
            const int expansionCap = std::clamp(
                60 + static_cast<int>(std::llround(70.0 * imperialWindow + 25.0 * m_conquestMomentum)),
                60, 150);
            m_expansionBudgetCells = std::clamp(
                static_cast<int>(std::llround((4.0 + 28.0 * pressures.opportunity) * expansionScale)),
                0, expansionCap);

            const int maxWars = std::max(1, simCfg.war.maxConcurrentWars);
            const double warThreshold =
                std::clamp(
                    simCfg.war.opportunisticWarThreshold -
                    simCfg.war.leaderAmbitionWarWeight * (leadershipCampaignDrive - 0.5) -
                    simCfg.war.weakStatePredationWeight * (weakStatePredation - 0.5) -
                    0.16 * imperialWindow,
                    0.25, 0.90);
            const bool canOpenNewWar =
                !m_isAtWar &&
                bestTarget >= 0 &&
                static_cast<int>(m_enemies.size()) < maxWars &&
                canDeclareWar();
            const bool diversionaryWar =
                (pressures.legitimacy > 0.62) &&
                (attackerReadiness > 0.46) &&
                (weakStatePredation > 0.40 || imperialWindow > 0.45);
            if (canOpenNewWar &&
                (pressures.opportunity > warThreshold || diversionaryWar) &&
                (m_gold > std::max(6.0, 0.10 * income))) {
                    const Country& target = allCountries[static_cast<size_t>(bestTarget)];
                    const double ourPowerLocal = militaryPower(*this);
                    const double theirPowerLocal = militaryPower(target);
                    const double powerRatio = (theirPowerLocal > 1e-6) ? (ourPowerLocal / theirPowerLocal) : 2.0;
                    const double scarcity = clamp01(m_macro.lastFoodShortage + m_macro.lastNonFoodShortage);
                    const double tribal = clamp01((0.25 - m_polity.adminCapacity) / 0.25);
                    const double institutional = clamp01(m_polity.adminCapacity);

                    struct GoalWeight {
                        WarGoal goal;
                        double weight;
                    };
                    const double targetWeakness = clamp01(
                        0.55 * (1.0 - target.getStability()) +
                        0.45 * (1.0 - target.getLegitimacy()));
                    std::vector<GoalWeight> goals = {
                        {WarGoal::Raid,          std::max(0.05, simCfg.war.objectiveRaidWeight + 0.28 * scarcity + 0.20 * tribal - 0.16 * imperialWindow)},
                        {WarGoal::BorderShift,   std::max(0.05, simCfg.war.objectiveBorderWeight + 0.20 * institutional + 0.22 * leadershipCampaignDrive + 0.24 * imperialWindow + 0.08 * targetWeakness)},
                        {WarGoal::Tribute,       std::max(0.01, simCfg.war.objectiveTributeWeight + 0.18 * institutional + 0.10 * scarcity + 0.08 * targetWeakness)},
                        {WarGoal::Vassalization, std::max(0.01, simCfg.war.objectiveVassalWeight + 0.22 * std::max(0.0, powerRatio - 1.0) + 0.14 * targetWeakness + 0.18 * imperialWindow)},
                        {WarGoal::RegimeChange,  std::max(0.01, simCfg.war.objectiveRegimeWeight + 0.14 * (1.0 - target.getLegitimacy()) + 0.08 * pressures.legitimacy)},
                        {WarGoal::Annihilation,  std::max(0.01, simCfg.war.objectiveAnnihilationWeight +
                                                               simCfg.war.earlyAnnihilationBias * tribal +
                                                               0.14 * std::max(0.0, powerRatio - 1.25) +
                                                               0.12 * targetWeakness * leadershipCampaignDrive +
                                                               0.14 * imperialWindow -
                                                               simCfg.war.highInstitutionAnnihilationDamp * institutional)}
                    };
                    std::vector<double> ws;
                    ws.reserve(goals.size());
                    for (const GoalWeight& g : goals) ws.push_back(std::max(0.0, g.weight));
                    std::discrete_distribution<int> pickGoal(ws.begin(), ws.end());
                    m_pendingWarGoal = goals[static_cast<size_t>(pickGoal(gen))].goal;
                    startWar(allCountries[static_cast<size_t>(bestTarget)], news);
                    m_conquestMomentum = std::min(1.0, m_conquestMomentum + 0.20);
                }
            m_polity.infraSpendingShare += 0.01;
            m_polity.adminSpendingShare += 0.02;
            m_polity.militarySpendingShare += 0.01;
        }

        normalizeBudgetShares();
        m_polity.taxRate = std::clamp(m_polity.taxRate, 0.02, 0.45);

        const int maxWars = std::max(1, simCfg.war.maxConcurrentWars);
        const bool canOpenNewWar =
            !m_isAtWar &&
            bestTarget >= 0 &&
            static_cast<int>(m_enemies.size()) < maxWars &&
            canDeclareWar();
        if (canOpenNewWar) {
            const double emergencyWarDrive = clamp01(
                0.38 * pressures.survival +
                0.34 * pressures.legitimacy +
                0.18 * weakStatePredation +
                0.10 * leadershipCampaignDrive +
                0.12 * imperialWindow);
            if (emergencyWarDrive > 0.72 &&
                m_gold > std::max(6.0, 0.05 * income)) {
                m_pendingWarGoal = (pressures.survival > pressures.legitimacy)
                    ? WarGoal::BorderShift
                    : WarGoal::RegimeChange;
                startWar(allCountries[static_cast<size_t>(bestTarget)], news);
                m_conquestMomentum = std::min(1.0, m_conquestMomentum + 0.15);
            }
        }
    }

	    // Phase 1: Replace the type-driven expansion contentment system and burst rails.
	    m_isContentWithSize = false;
	    m_contentmentDuration = 0;
    const double burstDrive = clamp01(
        0.40 * leadershipCampaignDrive +
        0.26 * pressures.opportunity +
        0.18 * clamp01(m_polity.logisticsReach) +
        0.16 * m_conquestMomentum);
    const bool doBurstExpansion =
        (burstDrive > 0.60) &&
        (techCount >= 20) &&
        (currentYear % std::max(4, 16 - std::min(10, techCount / 4)) == m_expansionStaggerOffset % std::max(4, 16 - std::min(10, techCount / 4)));

		    // AI expansion budget (replaces random growth as the primary engine).
    const int growthCap = std::clamp(
        60 + static_cast<int>(std::llround(90.0 * imperialWindow + 25.0 * m_conquestMomentum)),
        60, 170);
    int growth = std::clamp(m_expansionBudgetCells, 0, growthCap);

		    // Military readiness responds to spending and logistics (cheap, self-limiting).
		    {
		        const double baseType = (m_type == Type::Warmonger) ? 1.30 : (m_type == Type::Trader) ? 0.65 : 0.35;
		        const double desired = baseType *
		                               (0.70 + 1.10 * m_polity.militarySpendingShare) *
		                               (0.75 + 0.50 * m_polity.logisticsReach);
		        m_militaryStrength = 0.90 * m_militaryStrength + 0.10 * desired;
		    }

        // Phase 5: science/culture point currencies removed. Innovation is modeled as knowledge rates,
        // and culture as traits + institution adoption (handled in TechnologyManager/CultureManager).

    // Phase 1: soft overload expansion model (no hard territory clamp).
    // Capacity is still capability-driven, but load above capacity degrades growth smoothly instead of hard-stopping.
    const double nominalCapacity =
        std::max(24.0,
                 60.0 +
                 5000.0 * clamp01(m_polity.adminCapacity) +
                 120.0 * static_cast<double>(m_cities.size()) +
                 10.0 * static_cast<double>(std::max(0, techCount)));
    const int nominalCapacityPixels = std::max(24, static_cast<int>(std::llround(nominalCapacity)));
    const double logistics = clamp01(m_polity.logisticsReach);
    const double institutionCapSoft = clamp01(m_macro.institutionCapacity);
    const double connectivity = clamp01(m_macro.connectivityIndex);
    const double capabilityBlend = clamp01(0.45 * logistics + 0.35 * institutionCapSoft + 0.20 * connectivity);
    const size_t countrySize = m_boundaryPixels.size();
    const double countrySizeD = static_cast<double>(countrySize);
    const double governanceLoad =
        countrySizeD *
        (1.0 +
         0.35 * (1.0 - clamp01(m_avgControl)) +
         0.25 * clamp01(m_autonomyPressure) +
         0.20 * (m_isAtWar ? 1.0 : 0.0));
    const double loadRatio = governanceLoad / std::max(1.0, nominalCapacity);
    const double overload = std::max(0.0, loadRatio - 1.0);
    double growthSoftMultiplier = 1.0;
    if (overload > 0.0) {
        const double overloadDrag = 0.65 + 0.35 * (1.0 - capabilityBlend);
        growthSoftMultiplier = std::exp(-1.35 * overload * overloadDrag);
    } else {
        const double slack = std::max(0.0, 1.0 - loadRatio);
        growthSoftMultiplier = std::min(1.20, 1.0 + 0.08 * slack * (0.50 + 0.50 * capabilityBlend));
    }
    growth = std::clamp(static_cast<int>(std::llround(static_cast<double>(growth) * growthSoftMultiplier)), 0, 40);

    if (overload > 0.0) {
        const double overloadStress = overload * (0.40 + 0.60 * (1.0 - capabilityBlend));
        m_avgControl = clamp01(m_avgControl - 0.010 * overloadStress);
        m_polity.legitimacy = clamp01(m_polity.legitimacy - 0.008 * overloadStress);
        m_autonomyPressure = clamp01(m_autonomyPressure + 0.015 * overloadStress);
    } else {
        const double slack = std::max(0.0, 1.0 - loadRatio);
        const double recovery = std::min(0.01, 0.003 * slack * (0.40 + 0.60 * capabilityBlend));
        m_avgControl = clamp01(m_avgControl + recovery);
        m_autonomyPressure = clamp01(m_autonomyPressure - 0.50 * recovery);
    }

    std::vector<sf::Vector2i> newBoundaryPixels;
    // 🚀 NUCLEAR OPTIMIZATION: Don't copy entire grid - work directly on main grid with proper locking
    std::vector<sf::Vector2i> currentBoundaryPixels = m_territoryVec;

    // Type is flavor only: keep any behavioral weighting small.
    const double warmongerWarMultiplier = 1.10;

    if (isAtWar()) {
        // Wartime expansion (only into enemy territory)

        // Apply Warmonger multiplier
        if (m_type == Type::Warmonger) {
            growth = static_cast<int>(growth * warmongerWarMultiplier);
        }
        
        // 💥💥💥 WAR BURST CONQUEST CHECK - Blitzkrieg-style territorial seizure!
        bool doWarBurstConquest = false;
        int warBurstRadius = getWarBurstConquestRadius();
        int warBurstFreq = getWarBurstConquestFrequency();
        
	        if (warBurstFreq > 0 &&
                currentYear % warBurstFreq == 0 &&
                warBurstRadius > 1 &&
                m_activeWarGoal != WarGoal::Raid &&
                m_conquestMomentum > 0.22) {
	            doWarBurstConquest = true;
	            std::cout << "💥 " << m_name << " launches WAR BURST CONQUEST (radius " << warBurstRadius << ")!" << std::endl;
	        }

        Country* primaryEnemy = getEnemies().empty() ? nullptr : getEnemies().front();
        if (primaryEnemy && primaryEnemy->getPopulation() > 0 && !primaryEnemy->getBoundaryPixels().empty() && !currentBoundaryPixels.empty()) {
            const int enemyIndex = primaryEnemy->getCountryIndex();

            int captureBudget = std::clamp(growth * 25, 120, 900);
            if (m_type == Type::Warmonger) {
                captureBudget = static_cast<int>(captureBudget * 1.25);
            }
            captureBudget = static_cast<int>(captureBudget * (1.0 + std::min(1.0, getTerritoryCaptureBonusRate())));

            int maxDepth = 20;
            if (doWarBurstConquest) {
                captureBudget = std::min(3000, captureBudget * std::max(2, warBurstRadius));
                maxDepth = std::max(maxDepth, warBurstRadius * 6);
            }

            sf::Vector2i ourCapital = getCapitalLocation();
            sf::Vector2i enemyCapital = primaryEnemy->getCapitalLocation();
            sf::Vector2f attackDir(static_cast<float>(enemyCapital.x - ourCapital.x), static_cast<float>(enemyCapital.y - ourCapital.y));
            float attackDirLen = std::sqrt(attackDir.x * attackDir.x + attackDir.y * attackDir.y);
            if (attackDirLen > 0.001f) {
                attackDir.x /= attackDirLen;
                attackDir.y /= attackDirLen;
            } else {
                attackDir = sf::Vector2f(1.0f, 0.0f);
            }

            static const sf::Vector2i dirs8[] = {
                {1,0}, {-1,0}, {0,1}, {0,-1}, {1,1}, {1,-1}, {-1,1}, {-1,-1}
            };

            sf::Vector2i seedEnemyCell(-1, -1);
            float bestScore = -1e9f;
            std::vector<sf::Vector2i> captured;
            captured.reserve(static_cast<size_t>(captureBudget));

            {
                std::lock_guard<std::mutex> lock(gridMutex);

                const int sampleCount = std::min(250, static_cast<int>(currentBoundaryPixels.size()));
                for (int s = 0; s < sampleCount; ++s) {
                    size_t idx = static_cast<size_t>((static_cast<long long>(s) * static_cast<long long>(currentBoundaryPixels.size())) / std::max(1, sampleCount));
                    const sf::Vector2i base = currentBoundaryPixels[idx];

                    for (const auto& d : dirs8) {
                        sf::Vector2i probe = base + d;
                        if (probe.x < 0 || probe.x >= static_cast<int>(isLandGrid[0].size()) ||
                            probe.y < 0 || probe.y >= static_cast<int>(isLandGrid.size())) {
                            continue;
                        }
                        if (!isLandGrid[probe.y][probe.x]) {
                            continue;
                        }
                        if (countryGrid[probe.y][probe.x] != enemyIndex) {
                            continue;
                        }

                        sf::Vector2f rel(static_cast<float>(probe.x - ourCapital.x), static_cast<float>(probe.y - ourCapital.y));
                        float score = rel.x * attackDir.x + rel.y * attackDir.y;
                        if (score > bestScore) {
                            bestScore = score;
                            seedEnemyCell = probe;
                        }
                    }
                }

                if (seedEnemyCell.x != -1) {
                    struct Node { sf::Vector2i cell; int depth; };
                    std::queue<Node> frontier;
                    std::unordered_set<sf::Vector2i> visited;
                    visited.reserve(static_cast<size_t>(captureBudget) * 2u);

                    frontier.push({seedEnemyCell, 0});
                    visited.insert(seedEnemyCell);

                    while (!frontier.empty() && static_cast<int>(captured.size()) < captureBudget) {
                        Node node = frontier.front();
                        frontier.pop();

                        if (countryGrid[node.cell.y][node.cell.x] != enemyIndex) {
                            continue;
                        }

                        captured.push_back(node.cell);
                        if (node.depth >= maxDepth) {
                            continue;
                        }

                        for (int k = 0; k < 4; ++k) {
                            sf::Vector2i next = node.cell + dirs8[k];
                            if (next.x < 0 || next.x >= static_cast<int>(isLandGrid[0].size()) ||
                                next.y < 0 || next.y >= static_cast<int>(isLandGrid.size())) {
                                continue;
                            }
                            if (!isLandGrid[next.y][next.x]) {
                                continue;
                            }
                            if (visited.insert(next).second) {
                                frontier.push({next, node.depth + 1});
                            }
                        }
                    }

                    for (const auto& cell : captured) {
                        if (countryGrid[cell.y][cell.x] != enemyIndex) {
                            continue;
                        }
                        map.setCountryOwnerAssumingLocked(cell.x, cell.y, m_countryIndex);

                        int regionIndex = static_cast<int>((cell.y / regionSize) * (isLandGrid[0].size() / regionSize) + (cell.x / regionSize));
                        dirtyRegions.insert(regionIndex);
                    }
                }
            }

            if (!captured.empty()) {
                m_conquestMomentum = std::min(1.0, m_conquestMomentum + 0.10 + 0.0004 * static_cast<double>(captured.size()));
                int citiesCaptured = 0;
                std::unordered_set<sf::Vector2i> capturedSet(captured.begin(), captured.end());
                for (auto itCity = primaryEnemy->m_cities.begin(); itCity != primaryEnemy->m_cities.end();) {
                    if (capturedSet.count(itCity->getLocation())) {
                        addConqueredCity(*itCity);
                        itCity = primaryEnemy->m_cities.erase(itCity);
                        citiesCaptured++;
                    } else {
                        ++itCity;
                    }
                }

	                if (!usePopGrid) {
	                    long long enemyPop = primaryEnemy->getPopulation();
	                    if (enemyPop > 0) {
	                        double lossRate = 0.00003 * static_cast<double>(captured.size());
	                        if (citiesCaptured > 0) {
	                            lossRate += 0.03 * citiesCaptured;
	                        }
	                        lossRate = std::min(0.35, lossRate);
	                        long long loss = static_cast<long long>(static_cast<double>(enemyPop) * lossRate);
	                        primaryEnemy->setPopulation(std::max(0LL, enemyPop - loss));
	                    }
	                }

                if (doWarBurstConquest) {
                    std::cout << "   💥 " << m_name << " breakthrough captures " << captured.size() << " cells!" << std::endl;
                }
                if (m_activeWarGoal == WarGoal::Annihilation && canAnnihilateCountry(*primaryEnemy)) {
                    absorbCountry(*primaryEnemy, map, news);
                    m_conquestMomentum = std::min(1.0, m_conquestMomentum + 0.30);
                }
            } else {
                m_conquestMomentum = std::max(0.0, m_conquestMomentum - 0.02);
            }
        }
    }
    else {
        m_conquestMomentum = std::max(0.0, m_conquestMomentum - 0.015);
        // Peacetime expansion (normal expansion for all countries)
        // 🎯 RESPECT EXPANSION CONTENTMENT - Content countries don't expand
        int actualGrowth = m_isContentWithSize ? 0 : growth;
        
	        for (int i = 0; i < actualGrowth; ++i) {
	            if (currentBoundaryPixels.empty()) break;

	            std::uniform_int_distribution<size_t> boundaryIndexDist(0, currentBoundaryPixels.size() - 1);
	            size_t boundaryIndex = boundaryIndexDist(gen);
	            sf::Vector2i currentCell = currentBoundaryPixels[boundaryIndex];

	            currentBoundaryPixels.erase(currentBoundaryPixels.begin() + boundaryIndex);

	            // Phase 1: value-driven frontier settlement (no random direction, no calendar rails).
	            sf::Vector2i bestCell(-1, -1);
	            double bestFood = -1.0;
	            static const int dirs4[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
	            for (const auto& d : dirs4) {
	                const int nx = currentCell.x + d[0];
	                const int ny = currentCell.y + d[1];
	                if (ny < 0 || ny >= static_cast<int>(isLandGrid.size()) ||
	                    nx < 0 || nx >= static_cast<int>(isLandGrid[static_cast<size_t>(ny)].size())) {
	                    continue;
	                }
	                if (!isLandGrid[static_cast<size_t>(ny)][static_cast<size_t>(nx)]) continue;
	                if (countryGrid[static_cast<size_t>(ny)][static_cast<size_t>(nx)] != -1) continue;
	                const double food = map.getCellFood(nx, ny);
	                if (food > bestFood) {
	                    bestFood = food;
	                    bestCell = sf::Vector2i(nx, ny);
	                }
	            }

	            if (bestCell.x >= 0) {
	                std::lock_guard<std::mutex> lock(gridMutex);
	                if (countryGrid[bestCell.y][bestCell.x] == -1 && isLandGrid[bestCell.y][bestCell.x]) {
	                    map.setCountryOwnerAssumingLocked(bestCell.x, bestCell.y, m_countryIndex);
	                    int regionIndex = static_cast<int>((bestCell.y / regionSize) * (isLandGrid[0].size() / regionSize) + (bestCell.x / regionSize));
	                    dirtyRegions.insert(regionIndex);
	                    newBoundaryPixels.push_back(bestCell);
	                }
	            }
	        }
	    }


    // WARMONGER TERRITORIAL SURGE - occasional large-scale grabs beyond immediate border
	    if ((m_type == Type::Warmonger || burstDrive > 0.78) && !m_isContentWithSize && !m_boundaryPixels.empty()) {
	        std::uniform_real_distribution<> blobChance(0.0, 1.0);
	        if (blobChance(gen) < 0.5) {
	            int currentApproxSize = static_cast<int>(m_boundaryPixels.size());
	            int remainingCapacity = std::max(0, nominalCapacityPixels - currentApproxSize);

            int blobRadius = 5 + static_cast<int>(std::min(5.0, getMaxSizeMultiplier()));
            if (m_flatMaxSizeBonus >= 2000) blobRadius += 3;
            if (m_flatMaxSizeBonus >= 3000) blobRadius += 4;

            int blobTarget = blobRadius * blobRadius * 4;
            if (m_flatMaxSizeBonus >= 3000) blobTarget += 150;
            else if (m_flatMaxSizeBonus >= 2000) blobTarget += 90;
            blobTarget += static_cast<int>(getExpansionRateBonus() * 0.6);
            blobTarget = std::min(blobTarget, remainingCapacity);

            if (blobTarget > 0) {
                static const std::vector<sf::Vector2i> blobDirections = {
                    {1,0}, {1,1}, {0,1}, {-1,1}, {-1,0}, {-1,-1}, {0,-1}, {1,-1}
                };
                std::uniform_int_distribution<> dirDist(0, static_cast<int>(blobDirections.size()) - 1);

                std::vector<sf::Vector2i> boundaryVector = m_territoryVec;
                std::shuffle(boundaryVector.begin(), boundaryVector.end(), gen);

                sf::Vector2i chosenDir;
                sf::Vector2i seedCell;
                bool foundSeed = false;

                for (int attempt = 0; attempt < static_cast<int>(blobDirections.size()) && !foundSeed; ++attempt) {
                    chosenDir = blobDirections[dirDist(gen)];
                    for (const auto& boundaryCell : boundaryVector) {
                        sf::Vector2i probe = boundaryCell + chosenDir;
                        if (probe.x < 0 || probe.x >= static_cast<int>(isLandGrid[0].size()) ||
                            probe.y < 0 || probe.y >= static_cast<int>(isLandGrid.size()) ||
                            !isLandGrid[probe.y][probe.x]) {
                            continue;
                        }

                        int owner;
                        {
                            std::lock_guard<std::mutex> lock(gridMutex);
                            owner = countryGrid[probe.y][probe.x];
                        }

                        bool enemyCell = false;
                        if (owner >= 0 && owner != m_countryIndex) {
                            enemyCell = std::any_of(m_enemies.begin(), m_enemies.end(), [&](const Country* e){ return e->getCountryIndex() == owner; });
                        }

                        if (owner == -1 || enemyCell) {
                            seedCell = probe;
                            foundSeed = true;
                            break;
                        }
                    }
                }

                if (foundSeed) {
                    std::queue<std::pair<sf::Vector2i,int>> frontier;
                    std::unordered_set<sf::Vector2i> visited;
                    frontier.push({seedCell, 0});
                    visited.insert(seedCell);
                    std::vector<sf::Vector2i> blobCells;
                    blobCells.reserve(blobTarget);
                    const int radiusSq = blobRadius * blobRadius; // bias flood fill toward circular shapes

                    while (!frontier.empty() && static_cast<int>(blobCells.size()) < blobTarget) {
                        auto [cell, distance] = frontier.front();
                        frontier.pop();

                        if (cell.x < 0 || cell.x >= static_cast<int>(isLandGrid[0].size()) ||
                            cell.y < 0 || cell.y >= static_cast<int>(isLandGrid.size()) ||
                            !isLandGrid[cell.y][cell.x]) {
                            continue;
                        }

                        sf::Vector2i relativeToSeed = cell - seedCell;
                        int distSq = relativeToSeed.x * relativeToSeed.x + relativeToSeed.y * relativeToSeed.y;
                        if (distSq > radiusSq) {
                            continue;
                        }

                        int owner;
                        {
                            std::lock_guard<std::mutex> lock(gridMutex);
                            owner = countryGrid[cell.y][cell.x];
                        }

                        bool enemyCell = false;
                        if (owner >= 0 && owner != m_countryIndex) {
                            enemyCell = std::any_of(m_enemies.begin(), m_enemies.end(), [&](const Country* e){ return e->getCountryIndex() == owner; });
                        }

                        if (owner == -1 || enemyCell) {
                            blobCells.push_back(cell);
                        }

                        if (distance >= blobRadius) {
                            continue;
                        }

                        static const sf::Vector2i offsets[8] = {
                            {1,0}, {1,1}, {0,1}, {-1,1}, {-1,0}, {-1,-1}, {0,-1}, {1,-1}
                        };
                        for (const auto& delta : offsets) {
                            sf::Vector2i next = cell + delta;
                            if (visited.count(next)) {
                                continue;
                            }

                            sf::Vector2i relative = next - seedCell;
                            int nextDistSq = relative.x * relative.x + relative.y * relative.y;
                            if (nextDistSq > radiusSq) {
                                continue;
                            }

                            visited.insert(next);
                            frontier.push({next, distance + 1});
                            if (static_cast<int>(visited.size()) >= blobTarget * 3) {
                                break;
                            }
                        }
                    }

                    if (!blobCells.empty()) {
                        if (static_cast<int>(blobCells.size()) > remainingCapacity) {
                            blobCells.resize(remainingCapacity);
                        }

                        std::vector<std::pair<Country*, sf::Vector2i>> capturedCells;
                        capturedCells.reserve(blobCells.size());

                        {
                            std::lock_guard<std::mutex> lock(gridMutex);
                            for (const auto& cell : blobCells) {
                                int prevOwner = countryGrid[cell.y][cell.x];
                                if (prevOwner == m_countryIndex) {
                                    continue;
                                }

                                Country* prevCountry = nullptr;
                                if (prevOwner >= 0) {
                                    if (prevOwner < static_cast<int>(allCountries.size()) && allCountries[prevOwner].getCountryIndex() == prevOwner) {
                                        prevCountry = &allCountries[prevOwner];
                                    } else {
                                        for (auto& candidate : allCountries) {
                                            if (candidate.getCountryIndex() == prevOwner) {
                                                prevCountry = &candidate;
                                                break;
                                            }
                                        }
                                    }
                                }

                                map.setCountryOwnerAssumingLocked(cell.x, cell.y, m_countryIndex);
                                int regionIndex = static_cast<int>((cell.y / regionSize) * (isLandGrid[0].size() / regionSize) + (cell.x / regionSize));
                                dirtyRegions.insert(regionIndex);

                                if (prevCountry) {
                                    capturedCells.emplace_back(prevCountry, cell);
                                }

                                newBoundaryPixels.push_back(cell);
                            }
                        }

		                        for (auto& entry : capturedCells) {
		                            Country* prevCountry = entry.first;
		                            const sf::Vector2i& cell = entry.second;
		                            if (!usePopGrid) {
		                                std::uniform_real_distribution<double> randomFactorDist(0.0, 1.0);
		                                double randomFactor = randomFactorDist(gen);
		                                long long baseLoss = static_cast<long long>(prevCountry->getPopulation() * (0.001 + (0.002 * randomFactor)));
		                                prevCountry->setPopulation(std::max(0LL, prevCountry->getPopulation() - baseLoss));
		                            }
		                        }

                        news.addEvent(m_name + " establishes a new frontier region!");
                    }
                }
            }
        }
    }

	    // 🚀⚡ SUPER OPTIMIZED BURST EXPANSION - Lightning fast! ⚡🚀
	    if (doBurstExpansion && !m_boundaryPixels.empty() && !m_isContentWithSize) {
	        
	        // OPTIMIZATION 1: Pre-calculate burst size to avoid expensive nested loops
	        const int burstRadius = getBurstExpansionRadius();
	        int targetBurstPixels = burstRadius * burstRadius * 3; // Scale bursts with modern logistics
	        int burstPixelCap = (m_flatMaxSizeBonus > 0) ? 240 : 120; // Navigation/Railroad unlock larger colonial waves
	        targetBurstPixels = std::min(targetBurstPixels, burstPixelCap); // Respect performance guardrail
        
        // OPTIMIZATION 2: Use random sampling instead of exhaustive search
        std::vector<sf::Vector2i> burstTargets;
        burstTargets.reserve(targetBurstPixels); // Pre-allocate memory
        
        // OPTIMIZATION 3: Sample random directions from random boundary pixels
        std::vector<sf::Vector2i> sampleBoundary;
        int sampleSize = std::min(static_cast<int>(m_territoryVec.size()), 20); // Only sample 20 boundary pixels
        sampleBoundary.reserve(sampleSize);

        if (sampleSize > 0) {
            std::uniform_int_distribution<int> pick(0, std::max(0, static_cast<int>(m_territoryVec.size()) - 1));
            for (int i = 0; i < sampleSize; ++i) {
                sampleBoundary.push_back(m_territoryVec[static_cast<size_t>(pick(gen))]);
            }
        }
        
        // OPTIMIZATION 4: Direct random sampling within burst radius
        for (const auto& boundaryPixel : sampleBoundary) {
            for (int attempt = 0; attempt < targetBurstPixels / sampleSize; ++attempt) {
                // Random direction within burst radius
                std::uniform_int_distribution<> radiusDist(1, burstRadius);
                std::uniform_int_distribution<> angleDist(0, 7);
                
                int radius = radiusDist(gen);
                int angle = angleDist(gen);
                
                // 8 cardinal/diagonal directions for speed
                const int dx[] = {0, 1, 1, 1, 0, -1, -1, -1};
                const int dy[] = {-1, -1, 0, 1, 1, 1, 0, -1};
                
                sf::Vector2i targetCell = boundaryPixel + sf::Vector2i(dx[angle] * radius, dy[angle] * radius);
                
                // OPTIMIZATION 5: Single bounds check + direct grid access
                if (targetCell.x >= 0 && targetCell.x < static_cast<int>(isLandGrid[0].size()) && 
                    targetCell.y >= 0 && targetCell.y < isLandGrid.size() &&
                    isLandGrid[targetCell.y][targetCell.x] && countryGrid[targetCell.y][targetCell.x] == -1) {
                    burstTargets.push_back(targetCell);
                }
                
                if (burstTargets.size() >= targetBurstPixels) break;
            }
            if (burstTargets.size() >= targetBurstPixels) break;
        }
        
        // OPTIMIZATION 6: Batch apply all changes with single lock
        if (!burstTargets.empty()) {
            std::lock_guard<std::mutex> lock(gridMutex);
            
            for (const auto& targetCell : burstTargets) {
                map.setCountryOwnerAssumingLocked(targetCell.x, targetCell.y, m_countryIndex);
                
                // Batch dirty region marking
                int regionIndex = static_cast<int>((targetCell.y / regionSize) * (isLandGrid[0].size() / regionSize) + (targetCell.x / regionSize));
                dirtyRegions.insert(regionIndex);
            }
        }
        
        if (!burstTargets.empty()) {
            std::cout << "   ⚡ " << m_name << " OPTIMIZED burst expanded by " << burstTargets.size() << " pixels!" << std::endl;
        }
    }

    // 🚀 NUCLEAR OPTIMIZATION: Grid updates happen directly during expansion - no extra copying needed!

    // PERFORMANCE OPTIMIZATION: Use cached boundary pixels instead of scanning entire grid
    double foodConsumption = m_population * 0.001;
    double foodAvailable = 0.0;

    for (const auto& cell : m_territoryVec) {
        if (cell.x >= 0 && cell.x < static_cast<int>(resourceGrid[0].size()) && 
            cell.y >= 0 && cell.y < static_cast<int>(resourceGrid.size())) {
            auto it = resourceGrid[cell.y][cell.x].find(Resource::Type::FOOD);
            if (it != resourceGrid[cell.y][cell.x].end()) {
                foodAvailable += it->second;
            }
            const auto& cellResources = resourceGrid[cell.y][cell.x];
            for (Resource::Type type : Resource::kAllTypes) {
                if (type == Resource::Type::FOOD) continue;
                auto rit = cellResources.find(type);
                if (rit != cellResources.end() && rit->second > 0.0) {
                    m_resourceManager.addResource(type, rit->second);
                }
            }
        }
    }

	    if (!usePopGrid) {
	        // Legacy country-level demography path (disabled when PopulationGrid is active).
	        double kMult = TechnologyManager::techKMultiplier(technologyManager, *this);
	        double r = TechnologyManager::techGrowthRateR(technologyManager, *this);

	        // Small type modifier only, keep narrow to avoid runaway
	        double typeMult = 1.0;
	        if (m_type == Type::Trader) typeMult = 1.05;
	        if (m_type == Type::Pacifist) typeMult = 0.95;
	        r *= typeMult;

	        // Plague effects on growth rate - only for affected countries
	        if (plagueActive && map.isCountryAffectedByPlague(m_countryIndex)) {
	            r *= 0.1; // Severely reduce growth during plagues, but don't eliminate it
	        }

	        stepLogistic(r, resourceGrid, kMult, /*climate*/1.0);

	        // Plague effects - only affect countries in plague cluster
	        if (plagueActive && map.isCountryAffectedByPlague(m_countryIndex)) {
	            // Store the pre-plague population at the start of the plague
	            if (currentYear == map.getPlagueStartYear()) {
	                m_prePlaguePopulation = m_population;
	            }

	            // NEW TECH-DEPENDENT PLAGUE SYSTEM - Apply once per country per outbreak
	            double baseDeathRate = 0.05; // 5% typical country hit
	            long long deaths = static_cast<long long>(std::llround(m_population * baseDeathRate * getPlagueMortalityMultiplier(technologyManager)));
	            deaths = std::min(deaths, m_population); // Clamp deaths to population
	            m_population -= deaths;
	            if (m_population < 0) m_population = 0;

	            // Update the total death toll
	            plagueDeaths += deaths;
	        }
	    }
    
    // Stability system: war, plague, and stagnation reduce stability over time
    double growthRatio = 0.0;
    if (previousPopulation > 0) {
        growthRatio = static_cast<double>(m_population - previousPopulation) / static_cast<double>(previousPopulation);
    }

    if (growthRatio < 0.001) {
        m_stagnationYears++;
    } else {
        m_stagnationYears = 0;
    }

    const double yearsD = 1.0;
    bool plagueAffected = plagueActive && map.isCountryAffectedByPlague(m_countryIndex);
    double stabilityDelta = 0.0;
    double deltaWar = 0.0;
    double deltaPlague = 0.0;
    double deltaStagnation = 0.0;
    double deltaPeaceRecover = 0.0;
    const double institution = clamp01(m_macro.institutionCapacity);
    const double adminCap = clamp01(m_polity.adminCapacity);
    const double controlNow = clamp01(m_avgControl);
    const double legitNow = clamp01(m_polity.legitimacy);
    const double healthSpend = clamp01(m_polity.healthSpendingShare);
    const double resilience = clamp01(
        0.42 * institution +
        0.30 * adminCap +
        0.16 * controlNow +
        0.12 * legitNow);
    if (isAtWar()) {
        const double warExhaust = clamp01(m_warExhaustion);
        deltaWar =
            -yearsD *
            std::max(0.0, simCfg.polity.yearlyWarStabilityHit) *
            (0.70 + 0.90 * warExhaust) *
            (1.0 - 0.45 * resilience);
        stabilityDelta += deltaWar;
    }
    if (plagueAffected) {
        deltaPlague =
            -yearsD *
            std::max(0.0, simCfg.polity.yearlyPlagueStabilityHit) *
            (1.0 - 0.40 * healthSpend - 0.35 * institution);
        stabilityDelta += deltaPlague;
    }
    if (m_stagnationYears > 20) {
        deltaStagnation =
            -yearsD *
            std::max(0.0, simCfg.polity.yearlyStagnationStabilityHit) *
            (0.70 + 0.30 * (1.0 - resilience));
        stabilityDelta += deltaStagnation;
    }
    if (!isAtWar() && !plagueAffected) {
        const double baseRecovery =
            (growthRatio > 0.003)
            ? std::max(0.0, simCfg.polity.peaceRecoveryHighGrowth)
            : std::max(0.0, simCfg.polity.peaceRecoveryLowGrowth);
        deltaPeaceRecover = yearsD * baseRecovery * (0.45 + 0.55 * resilience);
        stabilityDelta += deltaPeaceRecover;
    }
    const double crisis = clamp01(
        0.50 * (isAtWar() ? 1.0 : 0.0) +
        0.35 * (plagueAffected ? 1.0 : 0.0) +
        0.25 * clamp01(m_macro.famineSeverity));
    const double lowStability = clamp01((0.40 - clamp01(m_stability)) / 0.40);
    const double tailRecovery =
        yearsD *
        std::max(0.0, simCfg.polity.resilienceRecoveryStrength) *
        lowStability *
        resilience *
        (1.0 - 0.75 * crisis);
    stabilityDelta += tailRecovery;

    m_stability = std::max(0.0, std::min(1.0, m_stability + stabilityDelta));
    {
        // Avoid permanent hard-zero traps for capable polities outside active acute crises.
        const double structuralFloor = 0.04 * resilience * (1.0 - 0.85 * crisis);
        if (m_stability < structuralFloor) {
            m_stability = structuralFloor;
        }
    }
    sdbg.dbg_growthRatio_used = growthRatio;
    sdbg.dbg_stagnationYears = m_stagnationYears;
    sdbg.dbg_isAtWar = isAtWar();
    sdbg.dbg_plagueAffected = plagueAffected;
    sdbg.dbg_delta_war = deltaWar;
    sdbg.dbg_delta_plague = deltaPlague;
    sdbg.dbg_delta_stagnation = deltaStagnation;
    sdbg.dbg_delta_peace_recover = deltaPeaceRecover;
    sdbg.dbg_stab_after_country_update = clamp01(m_stability);
    sdbg.dbg_stab_delta_update = sdbg.dbg_stab_after_country_update - sdbg.dbg_stab_start_year;
    if (m_fragmentationCooldown > 0) {
        m_fragmentationCooldown--;
    }
    // 🏙️ CITY GROWTH AND FOUNDING SYSTEM
    attemptFactoryConstruction(technologyManager, isLandGrid, countryGrid, gen, news);

		    if (!usePopGrid) {
		        checkCityGrowth(currentYear, news);
	        
	        // Legacy random city founding (PopulationGrid mode uses Map::updateCitiesFromPopulation()).
	        if (m_population >= 10000 && canFoundCity(technologyManager) && !m_boundaryPixels.empty()) {
	            foundCity(randomTerritoryCell(gen), news);
	        }
	    }

    // 🏛️ CHECK FOR IDEOLOGY CHANGES
    checkIdeologyChange(currentYear, news, technologyManager);
    
    // 🛣️ ROAD BUILDING SYSTEM - Build roads to other countries
    buildRoads(allCountries, map, isLandGrid, technologyManager, currentYear, news);

    // ⚓ PORT BUILDING SYSTEM - Build coastal ports (for future boats)
    buildPorts(isLandGrid, countryGrid, currentYear, gen, news);

    // ✈️ AIRWAY CONNECTIONS - Invisible long-range connections (for future air travel)
    buildAirways(allCountries, map, technologyManager, currentYear, news);

    // War logistics/exhaustion dynamics (goal-agnostic constraints; no hard rarity rules).
    if (isAtWar()) {
        const double logistics = clamp01(getLogisticsReach());
        const double market = clamp01(getMarketAccess());
        const double control = clamp01(getAvgControl());
        const double roadMobility = clamp01(std::sqrt(static_cast<double>(m_roads.size())) / 140.0 +
                                            std::sqrt(static_cast<double>(m_ports.size())) / 20.0);
        const double terrainRuggedness = clamp01(
            map.getCountryConstructionPotential(m_countryIndex) /
            (20.0 + map.getCountryFoodPotential(m_countryIndex)));
        const double terrainDefense = clamp01(std::max(0.0, simCfg.war.terrainDefenseWeight) * terrainRuggedness);
        const double energy = clamp01(m_macro.lastGoodsOutput / std::max(1.0, static_cast<double>(m_population) * 0.0002));
        const double foodStockRatio = clamp01(m_macro.foodStock / std::max(1.0, m_macro.foodStockCap));
        const double supplyScore = clamp01(
            std::max(0.0, simCfg.war.supplyBase) +
            std::max(0.0, simCfg.war.supplyLogisticsWeight) * logistics +
            std::max(0.0, simCfg.war.supplyMarketWeight) * market +
            std::max(0.0, simCfg.war.supplyControlWeight) * control +
            std::max(0.0, simCfg.war.supplyEnergyWeight) * energy +
            std::max(0.0, simCfg.war.supplyFoodStockWeight) * foodStockRatio +
            0.10 * roadMobility +
            0.10 * terrainDefense);
        m_warSupplyCapacity = supplyScore;

        const double demandScore = clamp01(0.20 + 1.25 * m_polity.militarySpendingShare +
                                           0.15 * (1.0 - roadMobility) +
                                           (m_activeWarGoal == WarGoal::Annihilation ? 0.25 : 0.0));
        const double overdraw = std::max(0.0, demandScore - supplyScore);
        const double exhaustionDelta =
            std::max(0.0, simCfg.war.exhaustionRise) * (0.50 + overdraw) +
            std::max(0.0, simCfg.war.overSupplyAttrition) * overdraw +
            0.02 * (1.0 - clamp01(m_stability));
        m_warExhaustion = clamp01(m_warExhaustion + exhaustionDelta);

        if (overdraw > 1e-6) {
            const double attrition = std::min(0.30, std::max(0.0, simCfg.war.overSupplyAttrition) * overdraw);
            m_militaryStrength = std::max(0.0, m_militaryStrength * (1.0 - attrition));
            m_stability = clamp01(m_stability - 0.03 * overdraw);
            m_polity.legitimacy = clamp01(m_polity.legitimacy - 0.02 * overdraw);
            m_macro.foodStock = std::max(0.0, m_macro.foodStock * (1.0 - 0.08 * overdraw)); // border devastation proxy
        }
    } else {
        m_warExhaustion = std::max(0.0, m_warExhaustion - 0.08);
        m_warSupplyCapacity = 0.0;
    }

    // Decrement war and peace durations
    if (isAtWar()) {
        if (m_warExhaustion >= simCfg.war.exhaustionPeaceThreshold) {
            m_warDuration = 0;
        }
        decrementWarDuration();
        if (m_warDuration <= 0) {
            const double endedExhaustion = m_warExhaustion;
            const WarGoal endedGoal = m_activeWarGoal;
            // Get the name of the enemy before ending the war
            std::string enemyName;
            if (!m_enemies.empty()) {
                enemyName = (*m_enemies.begin())->getName(); // Assuming you only have one enemy at a time
            }

            endWar(currentYear);
            {
                const double reconBase = std::max(0.0, simCfg.war.peaceReconstructionDrag);
                const double recon = clamp01(reconBase * (0.55 + 0.90 * endedExhaustion));
                m_macro.capitalStock = std::max(0.0, m_macro.capitalStock * (1.0 - recon));
                m_macro.infraStock = std::max(0.0, m_macro.infraStock * (1.0 - 0.85 * recon));
                m_macro.lastGoodsOutput = std::max(0.0, m_macro.lastGoodsOutput * (1.0 - 0.70 * recon));
                m_macro.lastServicesOutput = std::max(0.0, m_macro.lastServicesOutput * (1.0 - 0.45 * recon));

                double legitShift = 0.0;
                switch (endedGoal) {
                    case WarGoal::Raid: legitShift += 0.01; break;
                    case WarGoal::BorderShift: legitShift += 0.00; break;
                    case WarGoal::Tribute: legitShift += 0.02; break;
                    case WarGoal::Vassalization: legitShift += 0.01; break;
                    case WarGoal::RegimeChange: legitShift -= 0.01; break;
                    case WarGoal::Annihilation: legitShift -= 0.04; break;
                    default: break;
                }
                legitShift -= 0.08 * endedExhaustion;
                m_polity.legitimacy = clamp01(m_polity.legitimacy + legitShift);
                m_stability = clamp01(m_stability - 0.06 * endedExhaustion + 0.02 * (1.0 - recon));

                if (endedGoal == WarGoal::Tribute || endedGoal == WarGoal::Vassalization) {
                    const double transfer = (simCfg.war.peaceTributeWeight + simCfg.war.peaceReparationsWeight) *
                                            std::max(0.0, m_macro.lastGoodsOutput) * 0.08;
                    m_gold += std::max(0.0, transfer);
                }
            }
            if (m_polity.adminCapacity < 0.18) {
                m_peaceDuration = std::max(0, simCfg.war.cooldownMinYears / 2); // tribal follow-up wars can occur quickly
            } else {
                const int cdMin = std::max(0, simCfg.war.cooldownMinYears);
                const int cdMax = std::max(cdMin, simCfg.war.cooldownMaxYears);
                m_peaceDuration = std::clamp(m_peaceDuration, cdMin, cdMax);
            }

            // Add news event after ending the war
            if (!enemyName.empty()) {
                news.addEvent("The war between " + m_name + " and " + enemyName + " has ended!");
            }
        }
    }
    else if (m_peaceDuration > 0) {
        decrementPeaceDuration();
    }

    if (isAtWar()) {
        m_yearsSinceWar = 0;
    } else {
        m_yearsSinceWar = std::min(m_yearsSinceWar + 1, 10000);
    }

    renormalizePopulationCohortsToTotal();
}

namespace {
bool isCoastalLandCell(const std::vector<std::vector<bool>>& isLandGrid, int x, int y) {
    if (y < 0 || y >= static_cast<int>(isLandGrid.size())) {
        return false;
    }
    if (x < 0 || x >= static_cast<int>(isLandGrid[y].size())) {
        return false;
    }
    if (!isLandGrid[y][x]) {
        return false;
    }
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;
            const int nx = x + dx;
            const int ny = y + dy;
            if (ny < 0 || ny >= static_cast<int>(isLandGrid.size())) {
                continue;
            }
            if (nx < 0 || nx >= static_cast<int>(isLandGrid[ny].size())) {
                continue;
            }
            if (!isLandGrid[ny][nx]) {
                return true;
            }
        }
    }
    return false;
}
} // namespace

// Get the current population
long long Country::getPopulation() const {
    return m_population;
}

// Get the country's color
sf::Color Country::getColor() const {
    return m_color;
}

// Get the country's index
int Country::getCountryIndex() const {
    return m_countryIndex;
}

// Add a boundary pixel to the country
void Country::addBoundaryPixel(const sf::Vector2i& cell) {
    addTerritoryCell(cell);
}

// Get the country's boundary pixels
const std::unordered_set<sf::Vector2i>& Country::getBoundaryPixels() const {
    return m_boundaryPixels;
}

void Country::addTerritoryCell(const sf::Vector2i& c) {
    if (m_boundaryPixels.insert(c).second) {
        const size_t idx = m_territoryVec.size();
        m_territoryVec.push_back(c);
        m_territoryIndex[c] = idx;
    }
}

void Country::removeTerritoryCell(const sf::Vector2i& c) {
    auto it = m_territoryIndex.find(c);
    if (it == m_territoryIndex.end()) {
        m_boundaryPixels.erase(c);
        return;
    }
    const size_t idx = it->second;
    const size_t last = m_territoryVec.empty() ? 0 : (m_territoryVec.size() - 1);
    if (idx != last) {
        const sf::Vector2i moved = m_territoryVec[last];
        m_territoryVec[idx] = moved;
        m_territoryIndex[moved] = idx;
    }
    m_territoryVec.pop_back();
    m_territoryIndex.erase(it);
    m_boundaryPixels.erase(c);
}

sf::Vector2i Country::randomTerritoryCell(std::mt19937_64& rng) const {
    if (m_territoryVec.empty()) {
        return getCapitalLocation();
    }
    std::uniform_int_distribution<size_t> pick(0u, m_territoryVec.size() - 1u);
    return m_territoryVec[pick(rng)];
}

sf::Vector2i Country::deterministicTerritoryAnchor() const {
    if (m_territoryVec.empty()) {
        return getCapitalLocation();
    }
    sf::Vector2i best = m_territoryVec.front();
    for (const sf::Vector2i& cell : m_territoryVec) {
        if (cell.y < best.y || (cell.y == best.y && cell.x < best.x)) {
            best = cell;
        }
    }
    return best;
}

void Country::canonicalizeDeterministicContainers() {
    auto coordLess = [](const sf::Vector2i& a, const sf::Vector2i& b) {
        if (a.y != b.y) return a.y < b.y;
        return a.x < b.x;
    };

    // Canonicalize territory order used by random sampling to avoid insertion-history drift.
    if (!m_territoryVec.empty()) {
        std::sort(m_territoryVec.begin(), m_territoryVec.end(), coordLess);
        m_territoryVec.erase(std::unique(m_territoryVec.begin(), m_territoryVec.end()), m_territoryVec.end());
    }
    m_boundaryPixels.clear();
    m_boundaryPixels.reserve(m_territoryVec.size());
    m_territoryIndex.clear();
    m_territoryIndex.reserve(m_territoryVec.size());
    for (size_t i = 0; i < m_territoryVec.size(); ++i) {
        const sf::Vector2i& c = m_territoryVec[i];
        m_boundaryPixels.insert(c);
        m_territoryIndex[c] = i;
    }

    auto sortUniqueCoords = [&](std::vector<sf::Vector2i>& v) {
        if (v.empty()) return;
        std::sort(v.begin(), v.end(), coordLess);
        v.erase(std::unique(v.begin(), v.end()), v.end());
    };
    sortUniqueCoords(m_ports);
    sortUniqueCoords(m_roads);
    sortUniqueCoords(m_factories);

    if (!m_cities.empty()) {
        std::sort(m_cities.begin(), m_cities.end(), [&](const City& a, const City& b) {
            return coordLess(a.getLocation(), b.getLocation());
        });
        const auto itCap = std::find_if(m_cities.begin(), m_cities.end(), [&](const City& c) {
            return c.getLocation() == m_startingPixel;
        });
        if (itCap != m_cities.end() && itCap != m_cities.begin()) {
            std::iter_swap(m_cities.begin(), itCap);
        }
    }

    std::sort(m_enemies.begin(), m_enemies.end(), [](const Country* a, const Country* b) {
        if (a == b) return false;
        if (!a) return false;
        if (!b) return true;
        return a->getCountryIndex() < b->getCountryIndex();
    });
    m_enemies.erase(std::unique(m_enemies.begin(), m_enemies.end()), m_enemies.end());
}

void Country::canonicalizeDeterministicScalars(double fineScale, double govScale) {
    auto q = [](double v, double scale) -> double {
        if (!std::isfinite(v)) return v;
        return std::round(v * scale) / scale;
    };
    auto clamp01 = [](double v) { return std::max(0.0, std::min(1.0, v)); };

    m_gold = std::max(0.0, q(m_gold, fineScale));
    m_wealth = std::max(0.0, q(m_wealth, fineScale));
    m_gdp = std::max(0.0, q(m_gdp, fineScale));
    m_exports = std::max(0.0, q(m_exports, fineScale));
    m_totalCityPopulation = std::max(0.0, q(m_totalCityPopulation, fineScale));
    m_lastTaxBase = std::max(0.0, q(m_lastTaxBase, fineScale));
    m_lastTaxTake = std::max(0.0, q(m_lastTaxTake, fineScale));
    m_revenueTrendFast = q(m_revenueTrendFast, fineScale);
    m_revenueTrendSlow = q(m_revenueTrendSlow, fineScale);
    m_culturalDrift = std::max(0.0, q(m_culturalDrift, govScale));
    m_eliteBargainingPressure = clamp01(q(m_eliteBargainingPressure, govScale));
    m_commonerPressure = clamp01(q(m_commonerPressure, govScale));
    m_eliteDefectionPressure = clamp01(q(m_eliteDefectionPressure, govScale));

    m_polity.legitimacy = clamp01(q(m_polity.legitimacy, govScale));
    m_polity.adminCapacity = clamp01(q(m_polity.adminCapacity, govScale));
    m_polity.fiscalCapacity = clamp01(q(m_polity.fiscalCapacity, govScale));
    m_polity.logisticsReach = clamp01(q(m_polity.logisticsReach, govScale));
    m_polity.taxRate = std::clamp(q(m_polity.taxRate, govScale), 0.0, 0.8);
    m_polity.treasurySpendRate = std::clamp(q(m_polity.treasurySpendRate, govScale), 0.3, 2.2);
    m_polity.debt = std::max(0.0, q(m_polity.debt, fineScale));

    m_leader.competence = clamp01(q(m_leader.competence, govScale));
    m_leader.coercion = clamp01(q(m_leader.coercion, govScale));
    m_leader.diplomacy = clamp01(q(m_leader.diplomacy, govScale));
    m_leader.reformism = clamp01(q(m_leader.reformism, govScale));
    m_leader.eliteAffinity = clamp01(q(m_leader.eliteAffinity, govScale));
    m_leader.commonerAffinity = clamp01(q(m_leader.commonerAffinity, govScale));
    m_leader.ambition = clamp01(q(m_leader.ambition, govScale));

    m_polity.militarySpendingShare = std::max(0.0, q(m_polity.militarySpendingShare, govScale));
    m_polity.adminSpendingShare = std::max(0.0, q(m_polity.adminSpendingShare, govScale));
    m_polity.infraSpendingShare = std::max(0.0, q(m_polity.infraSpendingShare, govScale));
    m_polity.healthSpendingShare = std::max(0.0, q(m_polity.healthSpendingShare, govScale));
    m_polity.educationSpendingShare = std::max(0.0, q(m_polity.educationSpendingShare, govScale));
    m_polity.rndSpendingShare = std::max(0.0, q(m_polity.rndSpendingShare, govScale));
    const double shareSum = m_polity.militarySpendingShare +
                            m_polity.adminSpendingShare +
                            m_polity.infraSpendingShare +
                            m_polity.healthSpendingShare +
                            m_polity.educationSpendingShare +
                            m_polity.rndSpendingShare;
    if (shareSum > 1.0e-12) {
        m_polity.militarySpendingShare /= shareSum;
        m_polity.adminSpendingShare /= shareSum;
        m_polity.infraSpendingShare /= shareSum;
        m_polity.healthSpendingShare /= shareSum;
        m_polity.educationSpendingShare /= shareSum;
        m_polity.rndSpendingShare /= shareSum;
    }

    double eliteInfluenceSum = 0.0;
    for (EliteBlocState& bloc : m_eliteBlocs) {
        bloc.influence = std::max(0.0, q(bloc.influence, govScale));
        bloc.loyalty = clamp01(q(bloc.loyalty, govScale));
        bloc.grievance = clamp01(q(bloc.grievance, govScale));
        bloc.extractionTolerance = clamp01(q(bloc.extractionTolerance, govScale));
        eliteInfluenceSum += bloc.influence;
    }
    if (eliteInfluenceSum > 1.0e-12) {
        for (EliteBlocState& bloc : m_eliteBlocs) {
            bloc.influence /= eliteInfluenceSum;
        }
    }

    double classSum = 0.0;
    for (double& share : m_socialClasses.shares) {
        share = std::max(0.0, q(share, govScale));
        classSum += share;
    }
    if (classSum > 1.0e-12) {
        for (double& share : m_socialClasses.shares) {
            share /= classSum;
        }
    }

    double classInfluenceSum = 0.0;
    for (ClassAgentState& agent : m_classAgents) {
        agent.sentiment = clamp01(q(agent.sentiment, govScale));
        agent.influence = std::max(0.0, q(agent.influence, govScale));
        agent.tradePreference = clamp01(q(agent.tradePreference, govScale));
        agent.innovationPreference = clamp01(q(agent.innovationPreference, govScale));
        agent.redistributionPreference = clamp01(q(agent.redistributionPreference, govScale));
        agent.externalNetwork = clamp01(q(agent.externalNetwork, govScale));
        classInfluenceSum += agent.influence;
    }
    if (classInfluenceSum > 1.0e-12) {
        for (ClassAgentState& agent : m_classAgents) {
            agent.influence /= classInfluenceSum;
        }
    }

    for (float& a : m_adoptionTechDense) {
        const double qv = q(static_cast<double>(a), fineScale);
        a = static_cast<float>(std::clamp(qv, 0.0, 1.0));
    }
}

// Get the resource manager
const ResourceManager& Country::getResourceManager() const {
    return m_resourceManager;
}

// Get the country's name
const std::string& Country::getName() const {
    return m_name;
}

void Country::setName(const std::string& name) {
    m_name = name;
}

void Country::setSpawnRegionKey(const std::string& key) {
    m_spawnRegionKey = key;
    assignRegionalIdentityFromSpawnKey();
}

double Country::computeCulturalAffinity(const Country& other) const {
    const auto clamp01 = [](double v) { return std::max(0.0, std::min(1.0, v)); };
    const double languageAffinity = (m_languageFamilyId == other.m_languageFamilyId) ? 1.0 : 0.25;
    const double cultureAffinity = (m_cultureFamilyId == other.m_cultureFamilyId) ? 1.0 : 0.30;

    double traitDistSq = 0.0;
    for (size_t i = 0; i < m_traits.size(); ++i) {
        const double d = static_cast<double>(m_traits[i]) - static_cast<double>(other.m_traits[i]);
        traitDistSq += d * d;
    }
    const double traitAffinity = 1.0 - std::min(1.0, std::sqrt(traitDistSq / static_cast<double>(m_traits.size())));
    const double driftPenalty = std::min(0.25, std::abs(m_culturalDrift - other.m_culturalDrift) * 0.10);
    return clamp01(0.35 * languageAffinity + 0.35 * cultureAffinity + 0.30 * traitAffinity - driftPenalty);
}

// FAST FORWARD MODE: Optimized growth simulation
void Country::fastForwardGrowth(int yearIndex, int currentYear, const std::vector<std::vector<bool>>& isLandGrid, 
                               std::vector<std::vector<int>>& countryGrid, 
                               const std::vector<std::vector<std::unordered_map<Resource::Type, double>>>& resourceGrid,
                               News& news, Map& map, const TechnologyManager& technologyManager,
                               std::mt19937_64& gen, bool plagueAffected) {
    
    const bool usePopGrid = map.isPopulationGridActive();
    if (!usePopGrid) {
        // Legacy country-level demography path (disabled when PopulationGrid is active).
        double kMult = TechnologyManager::techKMultiplier(technologyManager, *this);
        double r = TechnologyManager::techGrowthRateR(technologyManager, *this);
    
        // Small type modifier only
        double typeMult = 1.0;
        if (m_type == Type::Trader) typeMult = 1.05;
        if (m_type == Type::Pacifist) typeMult = 0.95;
        r *= typeMult;
    
        if (plagueAffected) {
            r *= 0.1;
        }

        // Use Map cached aggregates for carrying capacity (much faster, closer to normal-mode timing).
        double foodSum = map.getCountryFoodSum(m_countryIndex);
        const sf::Vector2i start = getStartingPixel();
        if (map.getCellOwner(start.x, start.y) == m_countryIndex) {
            const double rawFood = map.getCellFood(start.x, start.y);
            if (rawFood < 417.0) {
                foodSum += (417.0 - rawFood);
            }
        }
        stepLogisticFromFoodSum(r, foodSum, kMult, 1.0);
    }

	    attemptFactoryConstruction(technologyManager, isLandGrid, countryGrid, gen, news);

    // Phase 5: science/culture point currencies removed (handled by knowledge + traits/institutions).
    
    // 🚀 ENHANCED FAST FORWARD EXPANSION - Use same advanced mechanics as normal update
    (void)yearIndex;
    if (!plagueAffected && (currentYear % 2 == 0) && !m_isContentWithSize) { // Every 2 years, respect contentment
        
        // Use technology-enhanced expansion rate (same as normal update)
        std::uniform_int_distribution<> growthDist(20, 40); // Higher base for fast forward
        int growth = growthDist(gen);
        growth += getExpansionRateBonus(); // Apply all technology bonuses!
        
        const auto clamp01 = [](double v) { return std::max(0.0, std::min(1.0, v)); };
        const int techCount = static_cast<int>(technologyManager.getUnlockedTechnologies(*this).size());
        const double nominalCapacity =
            std::max(24.0,
                     60.0 +
                     5000.0 * clamp01(m_polity.adminCapacity) +
                     120.0 * static_cast<double>(m_cities.size()) +
                     10.0 * static_cast<double>(std::max(0, techCount)));
        const double logistics = clamp01(m_polity.logisticsReach);
        const double institution = clamp01(m_macro.institutionCapacity);
        const double connectivity = clamp01(m_macro.connectivityIndex);
        const double capabilityBlend = clamp01(0.45 * logistics + 0.35 * institution + 0.20 * connectivity);
        const double countrySize = static_cast<double>(m_boundaryPixels.size());
        const double governanceLoad =
            countrySize *
            (1.0 +
             0.35 * (1.0 - clamp01(m_avgControl)) +
             0.25 * clamp01(m_autonomyPressure) +
             0.20 * (m_isAtWar ? 1.0 : 0.0));
        const double loadRatio = governanceLoad / std::max(1.0, nominalCapacity);
        const double overload = std::max(0.0, loadRatio - 1.0);
        double growthSoftMultiplier = 1.0;
        if (overload > 0.0) {
            const double overloadDrag = 0.65 + 0.35 * (1.0 - capabilityBlend);
            growthSoftMultiplier = std::exp(-1.35 * overload * overloadDrag);
        } else {
            const double slack = std::max(0.0, 1.0 - loadRatio);
            growthSoftMultiplier = std::min(1.20, 1.0 + 0.08 * slack * (0.50 + 0.50 * capabilityBlend));
        }
        growth = std::clamp(static_cast<int>(std::llround(static_cast<double>(growth) * growthSoftMultiplier)), 0, 45);

        if (overload > 0.0) {
            const double overloadStress = overload * (0.40 + 0.60 * (1.0 - capabilityBlend));
            m_avgControl = clamp01(m_avgControl - 0.010 * overloadStress);
            m_polity.legitimacy = clamp01(m_polity.legitimacy - 0.008 * overloadStress);
            m_autonomyPressure = clamp01(m_autonomyPressure + 0.015 * overloadStress);
        } else {
            const double slack = std::max(0.0, 1.0 - loadRatio);
            const double recovery = std::min(0.01, 0.003 * slack * (0.40 + 0.60 * capabilityBlend));
            m_avgControl = clamp01(m_avgControl + recovery);
            m_autonomyPressure = clamp01(m_autonomyPressure - 0.50 * recovery);
        }

        if (growth > 0) {
            // Normal expansion with technology bonuses
            std::vector<sf::Vector2i> currentBoundary = m_territoryVec;
            for (int i = 0; i < growth; ++i) {
                if (currentBoundary.empty()) break;

                std::uniform_int_distribution<size_t> boundaryDist(0, currentBoundary.size() - 1);
                size_t boundaryIndex = boundaryDist(gen);
                sf::Vector2i currentCell = currentBoundary[boundaryIndex];
                currentBoundary.erase(currentBoundary.begin() + boundaryIndex);

                std::uniform_int_distribution<> dirDist(-1, 1);
                int dx = dirDist(gen);
                int dy = dirDist(gen);
                if (dx == 0 && dy == 0) continue;

                sf::Vector2i newCell = currentCell + sf::Vector2i(dx, dy);

                if (newCell.x >= 0 && newCell.x < static_cast<int>(isLandGrid[0].size()) &&
                    newCell.y >= 0 && newCell.y < isLandGrid.size() &&
                    isLandGrid[newCell.y][newCell.x] && countryGrid[newCell.y][newCell.x] == -1) {

                    map.setCountryOwner(newCell.x, newCell.y, m_countryIndex);

                    // 🔥 CRITICAL FIX: Mark region as dirty for visual update!
                    int regionSize = map.getRegionSize();
                    int regionIndex = static_cast<int>((newCell.y / regionSize) * (isLandGrid[0].size() / regionSize) + (newCell.x / regionSize));
                    map.insertDirtyRegion(regionIndex);
                }
            }

            // ⚡🚀 SUPER OPTIMIZED FAST FORWARD BURST - Lightning fast! 🚀⚡
            int burstRadius = getBurstExpansionRadius();
            int burstFreq = getBurstExpansionFrequency();
            
            if (burstFreq > 0 && (currentYear + m_expansionStaggerOffset) % burstFreq == 0 && burstRadius > 1) {
                
                // HYPER OPTIMIZATION: Direct random placement instead of nested loops
                int targetPixels = std::min(burstRadius * 15, 80); // Simple calculation
                std::vector<sf::Vector2i> burstTargets;
                burstTargets.reserve(targetPixels);
                
                // Sample only 10 boundary pixels for speed
                std::vector<sf::Vector2i> quickSample;
                const int sampleCount = std::min(10, static_cast<int>(m_territoryVec.size()));
                if (sampleCount > 0) {
                    const size_t stride = std::max<size_t>(1u, m_territoryVec.size() / static_cast<size_t>(sampleCount));
                    for (int i = 0; i < sampleCount; ++i) {
                        quickSample.push_back(m_territoryVec[(static_cast<size_t>(i) * stride) % m_territoryVec.size()]);
                    }
                }
                
                // Fast random expansion from sampled points
                for (const auto& basePixel : quickSample) {
                    for (int i = 0; i < targetPixels / 10; ++i) {
                        // Fast random direction
                        std::uniform_int_distribution<> fastDir(-burstRadius, burstRadius);
                        int dx = fastDir(gen);
                        int dy = fastDir(gen);
                        
                        sf::Vector2i targetCell = basePixel + sf::Vector2i(dx, dy);
                        
                        // Quick validation
                        if (targetCell.x >= 0 && targetCell.x < static_cast<int>(isLandGrid[0].size()) && 
                            targetCell.y >= 0 && targetCell.y < isLandGrid.size() &&
                            isLandGrid[targetCell.y][targetCell.x] && countryGrid[targetCell.y][targetCell.x] == -1) {
                            burstTargets.push_back(targetCell);
                        }
                        
                        if (burstTargets.size() >= targetPixels) break;
                    }
                    if (burstTargets.size() >= targetPixels) break;
                }
                
                // Batch apply all changes
                for (const auto& targetCell : burstTargets) {
                    map.setCountryOwner(targetCell.x, targetCell.y, m_countryIndex);
                    
                    // Fast dirty region marking
                    int regionSize = map.getRegionSize();
                    int regionIndex = static_cast<int>((targetCell.y / regionSize) * (isLandGrid[0].size() / regionSize) + (targetCell.x / regionSize));
                    map.insertDirtyRegion(regionIndex);
                }
                
                if (!burstTargets.empty()) {
                    std::cout << "⚡ " << m_name << " HYPER-FAST burst: " << burstTargets.size() << " pixels!" << std::endl;
                }
            }
        }
    }
    
	    // Simplified city founding - every 20 years (legacy path only)
	    if (!usePopGrid) {
	        if (!plagueAffected && (currentYear % 20 == 0) && m_population >= 10000 && canFoundCity(technologyManager) && !m_boundaryPixels.empty()) {
	            foundCity(randomTerritoryCell(gen), news);
	        }
	    }
    
		    // Ideology changes - keep cadence calendar-based (avoids chunk artifacts).
			    if (currentYear % 10 == 0) {
			        checkIdeologyChange(currentYear, news, technologyManager);
			    }

	    // Track population across years for stability/stagnation calculations (works for both internal and grid-driven demography).
	    m_prevYearPopulation = m_population;
        renormalizePopulationCohortsToTotal();
	}

// Apply plague deaths during fast forward
void Country::applyPlagueDeaths(long long deaths) {
    m_population -= deaths;
    if (m_population < 0) m_population = 0;
}

// 🧬 TECHNOLOGY EFFECTS SYSTEM
void Country::applyTechnologyBonus(int techId, double scale) {
    const double s = std::clamp(scale, 0.0, 1.0);
    if (s <= 0.0) {
        return;
    }
    auto addInt = [&](int& v, double delta) {
        v += static_cast<int>(std::llround(delta * s));
    };
    auto addDouble = [&](double& v, double delta) {
        v += delta * s;
    };
    auto applyMult = [&](double& v, double fullMultiplier) {
        v *= (1.0 + (fullMultiplier - 1.0) * s);
    };
    auto blendUpInt = [&](int& v, int base, int target) {
        const int candidate = base + static_cast<int>(std::llround(static_cast<double>(target - base) * s));
        v = std::max(v, candidate);
    };
    auto blendFreq = [&](int& v, int target) {
        if (s < 0.25 || target <= 0) return;
        const double inv = std::max(0.25, s);
        const int candidate = std::max(1, static_cast<int>(std::llround(static_cast<double>(target) / inv)));
        if (v <= 0) v = candidate;
        else v = std::min(v, candidate);
    };

    switch (techId) {
        case 10: addDouble(m_maxSizeMultiplier, 0.2); break;
        case 20: addDouble(m_maxSizeMultiplier, 0.3); addInt(m_expansionRateBonus, 5); break;

        case 11: addDouble(m_sciencePointsBonus, 3.0); break;
        case 14: addDouble(m_sciencePointsBonus, 5.0); break;
        case 22: addDouble(m_sciencePointsBonus, 8.0); break;
        case TechId::UNIVERSITIES:
            addDouble(m_sciencePointsBonus, 15.5);
            addDouble(m_maxSizeMultiplier, 0.30);
            applyMult(m_researchMultiplier, 1.10);
            break;
        case TechId::ASTRONOMY: addDouble(m_sciencePointsBonus, 20.0); break;
        case TechId::SCIENTIFIC_METHOD:
            addDouble(m_sciencePointsBonus, 50.0);
            applyMult(m_researchMultiplier, 1.10);
            break;
        case 54:
            addDouble(m_sciencePointsBonus, 30.0);
            applyMult(m_researchMultiplier, 1.05);
            break;
        case 69:
            addDouble(m_sciencePointsBonus, 100.0);
            applyMult(m_researchMultiplier, 1.10);
            break;
        case 76: addDouble(m_sciencePointsBonus, 75.0); break;
        case 79:
            addDouble(m_sciencePointsBonus, 200.0);
            applyMult(m_researchMultiplier, 1.10);
            break;
        case 80: addDouble(m_sciencePointsBonus, 150.0); break;
        case 85:
            addDouble(m_sciencePointsBonus, 300.0);
            applyMult(m_researchMultiplier, 1.15);
            break;
        case 93:
            addDouble(m_sciencePointsBonus, 250.0);
            applyMult(m_researchMultiplier, 1.10);
            break;

        case 3:
            addDouble(m_militaryStrengthBonus, 0.15);
            addDouble(m_territoryCaptureBonusRate, 0.10);
            break;
        case 9:
            addDouble(m_militaryStrengthBonus, 0.25);
            addDouble(m_defensiveBonus, 0.15);
            break;
        case 13:
            addDouble(m_militaryStrengthBonus, 0.40);
            addDouble(m_territoryCaptureBonusRate, 0.20);
            addDouble(m_defensiveBonus, 0.25);
            break;
        case 18:
            addDouble(m_militaryStrengthBonus, 0.30);
            addDouble(m_territoryCaptureBonusRate, 0.35);
            addDouble(m_warDurationReduction, 0.20);
            addInt(m_expansionRateBonus, 8);
            break;

        case 16:
            addDouble(m_maxSizeMultiplier, 0.25);
            addInt(m_expansionRateBonus, 3);
            break;
        case 17:
            addDouble(m_maxSizeMultiplier, 0.40);
            addInt(m_expansionRateBonus, 6);
            break;
        case 23:
            addDouble(m_maxSizeMultiplier, 0.50);
            addInt(m_expansionRateBonus, 8);
            break;
        case 32:
            addDouble(m_maxSizeMultiplier, 0.60);
            addInt(m_expansionRateBonus, 10);
            break;

        case 12:
            addDouble(m_maxSizeMultiplier, 0.50);
            addInt(m_expansionRateBonus, 12);
            blendUpInt(m_burstExpansionRadius, 1, 2);
            blendFreq(m_burstExpansionFrequency, 10);
            break;
        case 26:
            addDouble(m_maxSizeMultiplier, 0.75);
            addInt(m_expansionRateBonus, 20);
            blendUpInt(m_burstExpansionRadius, 1, 3);
            blendFreq(m_burstExpansionFrequency, 8);
            break;
        case TechId::NAVIGATION:
            addDouble(m_maxSizeMultiplier, 1.5);
            addInt(m_flatMaxSizeBonus, 2000);
            addInt(m_expansionRateBonus, 90);
            blendUpInt(m_burstExpansionRadius, 1, 6);
            blendFreq(m_burstExpansionFrequency, 4);
            break;

        case 34:
            addDouble(m_maxSizeMultiplier, 0.80);
            addInt(m_expansionRateBonus, 25);
            break;
        case TechId::ECONOMICS:
            addDouble(m_maxSizeMultiplier, 1.2);
            addInt(m_expansionRateBonus, 35);
            break;
        case 36:
            addDouble(m_maxSizeMultiplier, 0.60);
            addInt(m_expansionRateBonus, 15);
            addDouble(m_sciencePointsBonus, 0.3);
            break;
        case 55:
            addDouble(m_maxSizeMultiplier, 2.0);
            addInt(m_flatMaxSizeBonus, 3000);
            addInt(m_expansionRateBonus, 180);
            blendUpInt(m_burstExpansionRadius, 1, 10);
            blendFreq(m_burstExpansionFrequency, 2);
            break;

        case 28:
            addDouble(m_militaryStrengthBonus, 0.50);
            addDouble(m_defensiveBonus, 0.40);
            addDouble(m_territoryCaptureBonusRate, 0.25);
            blendUpInt(m_warBurstConquestRadius, 1, 3);
            blendFreq(m_warBurstConquestFrequency, 8);
            break;
        case 37:
            addDouble(m_militaryStrengthBonus, 0.75);
            addDouble(m_territoryCaptureBonusRate, 0.50);
            addDouble(m_warDurationReduction, 0.30);
            blendUpInt(m_warBurstConquestRadius, 1, 5);
            blendFreq(m_warBurstConquestFrequency, 5);
            break;
        case 47:
            addDouble(m_militaryStrengthBonus, 0.60);
            addDouble(m_territoryCaptureBonusRate, 0.40);
            addDouble(m_warDurationReduction, 0.25);
            blendUpInt(m_warBurstConquestRadius, 1, 4);
            blendFreq(m_warBurstConquestFrequency, 6);
            break;
        case 50:
            addDouble(m_militaryStrengthBonus, 0.35);
            addDouble(m_defensiveBonus, 0.50);
            blendUpInt(m_warBurstConquestRadius, 1, 6);
            blendFreq(m_warBurstConquestFrequency, 4);
            break;
        case 56:
            addDouble(m_militaryStrengthBonus, 0.45);
            addDouble(m_territoryCaptureBonusRate, 0.60);
            blendUpInt(m_warBurstConquestRadius, 1, 7);
            blendFreq(m_warBurstConquestFrequency, 3);
            break;
        case 68:
            addDouble(m_militaryStrengthBonus, 1.50);
            addDouble(m_warDurationReduction, 0.70);
            addDouble(m_territoryCaptureBonusRate, 0.80);
            blendUpInt(m_warBurstConquestRadius, 1, 10);
            blendFreq(m_warBurstConquestFrequency, 2);
            break;
        case 77:
            addDouble(m_militaryStrengthBonus, 0.40);
            addDouble(m_territoryCaptureBonusRate, 0.30);
            addDouble(m_defensiveBonus, 0.35);
            blendUpInt(m_warBurstConquestRadius, 1, 5);
            blendFreq(m_warBurstConquestFrequency, 5);
            break;
        case 84:
            addDouble(m_militaryStrengthBonus, 0.60);
            addDouble(m_warDurationReduction, 0.40);
            addDouble(m_territoryCaptureBonusRate, 0.45);
            blendUpInt(m_warBurstConquestRadius, 1, 8);
            blendFreq(m_warBurstConquestFrequency, 3);
            break;

        case 96: addDouble(m_plagueResistanceBonus, 0.30); break;
        case 53: addDouble(m_plagueResistanceBonus, 0.50); break;
        case 65: addDouble(m_plagueResistanceBonus, 0.60); break;
        case 71: break;
        case 81:
            addDouble(m_plagueResistanceBonus, 0.40);
            addDouble(m_militaryStrengthBonus, 0.30);
            break;
        case 90:
            addDouble(m_plagueResistanceBonus, 0.50);
            addDouble(m_militaryStrengthBonus, 0.25);
            break;
        default:
            break;
    }
}

void Country::resetTechnologyBonuses() {
    m_populationGrowthBonus = 0.0;
    m_plagueResistanceBonus = 0.0;
    m_militaryStrengthBonus = 0.0;
    m_territoryCaptureBonusRate = 0.0;
    m_defensiveBonus = 0.0;
    m_warDurationReduction = 0.0;
    m_maxSizeMultiplier = 1.0;
    m_expansionRateBonus = 0;
    m_flatMaxSizeBonus = 0;
    m_burstExpansionRadius = 1;
    m_burstExpansionFrequency = 0;
    m_warBurstConquestRadius = 1;
    m_warBurstConquestFrequency = 0;
    m_sciencePointsBonus = 0.0;
    m_researchMultiplier = 1.0;
}

double Country::getTotalPopulationGrowthRate() const {
    return m_populationGrowthRate + m_populationGrowthBonus;
}

double Country::getPlagueResistance() const {
    return std::min(0.95, m_plagueResistanceBonus); // Cap at 95% resistance
}

double Country::getMilitaryStrengthMultiplier() const {
    return 1.0 + m_militaryStrengthBonus; // Base 1.0 + technology bonuses
}

double Country::getTerritoryCaptureBonusRate() const {
    return m_territoryCaptureBonusRate; // Additional capture rate bonus
}

double Country::getDefensiveBonus() const {
    return m_defensiveBonus; // Defensive bonus against attacks
}

double Country::getWarDurationReduction() const {
    return std::min(0.80, m_warDurationReduction); // Cap at 80% reduction
}

double Country::getSciencePointsMultiplier() const {
    // Phase 5: "science points" are cosmetic only; they do not affect technology progress.
    // Keep researchMultiplier as a legacy UI display hook.
    return m_researchMultiplier;
}

double Country::calculateScienceGeneration() const {
    const auto clamp01 = [](double v) { return std::max(0.0, std::min(1.0, v)); };
    const double pop = static_cast<double>(std::max<long long>(1, m_population));
    const double urban = clamp01(m_totalCityPopulation / pop);
    const double human = clamp01(m_macro.humanCapital);
    const double know = clamp01(m_macro.knowledgeStock);
    const double conn = clamp01(m_macro.connectivityIndex);
    const double inst = clamp01(m_macro.institutionCapacity);
    const double stable = clamp01(m_stability);
    const double health = clamp01(1.0 - m_macro.diseaseBurden);
    const double faminePenalty = clamp01(1.0 - m_macro.famineSeverity);
    const double scale = std::sqrt(pop / 100000.0);

    double gen = 8.0 * scale * (0.10 + 0.90 * urban) * (0.10 + 0.90 * conn);
    gen *= (0.20 + 0.80 * know);
    gen *= (0.25 + 0.75 * human);
    gen *= (0.30 + 0.70 * inst);
    gen *= (0.35 + 0.65 * stable);
    gen *= (0.40 + 0.60 * health);
    gen *= (0.45 + 0.55 * faminePenalty);
    if (m_isAtWar) {
        gen *= 0.88;
    }
    return s_scienceScaler * std::max(0.0, gen);
}

// 🚀 OPTIMIZED KNOWLEDGE DIFFUSION - Cache neighbors and update less frequently
double Country::calculateNeighborScienceBonus(const std::vector<Country>& allCountries, const Map& map, const TechnologyManager& techManager, int currentYear) const {
    
    // 🚀 STAGGERED PERFORMANCE: Only recalculate neighbors every 20-80 years (random per country) or when territories change
    bool needsRecalculation = (currentYear - m_neighborBonusLastUpdated >= m_neighborRecalculationInterval) || m_cachedNeighborIndices.empty();
    
	    if (needsRecalculation) {
	        m_cachedNeighborIndices.clear();
	        
	        // Find all neighbors using the map adjacency graph (O(degree) instead of O(n²)).
	        for (int neighborIndex : map.getAdjacentCountryIndicesPublic(m_countryIndex)) {
	            if (neighborIndex < 0 || neighborIndex >= static_cast<int>(allCountries.size())) {
	                continue;
	            }
	            if (neighborIndex == m_countryIndex) {
	                continue;
	            }
	            if (allCountries[static_cast<size_t>(neighborIndex)].getCountryIndex() != neighborIndex) {
	                continue;
	            }
	            if (allCountries[static_cast<size_t>(neighborIndex)].getPopulation() <= 0) {
	                continue;
	            }
	            m_cachedNeighborIndices.push_back(neighborIndex);
	        }
	        m_neighborBonusLastUpdated = currentYear;
	        
	        // 🚀 RANDOM INTERVAL: Generate new random interval for next recalculation (keeps it staggered)
	        const std::uint64_t h = SimulationContext::mix64(
	            static_cast<std::uint64_t>(m_countryIndex) * 0x9E3779B97F4A7C15ull ^
	            static_cast<std::uint64_t>(currentYear) * 0xBF58476D1CE4E5B9ull ^
	            0x7D2F8A1C0B3E559Bull);
	        m_neighborRecalculationInterval = 20 + static_cast<int>(h % 61ull); // 20..80 inclusive
	    }
    
    // Endogenous neighbor diffusion bonus from connectivity and knowledge gaps.
    double totalBonus = 0.0;
    for (int neighborIndex : m_cachedNeighborIndices) {
        // Safety check - make sure neighbor still exists
        if (neighborIndex >= 0 && neighborIndex < static_cast<int>(allCountries.size())) {
            const Country& neighbor = allCountries[neighborIndex];

            const double ourKnow = std::clamp(m_macro.knowledgeStock, 0.0, 1.0);
            const double theirKnow = std::clamp(neighbor.getMacroEconomy().knowledgeStock, 0.0, 1.0);
            const double gap = std::max(0.0, theirKnow - ourKnow);
            const double border = std::max(1, map.getBorderContactCount(m_countryIndex, neighborIndex));
            const double contact = std::min(1.0, std::log1p(static_cast<double>(border)) / 3.0);
            const double conn = 0.5 * std::clamp(m_macro.connectivityIndex + neighbor.getMacroEconomy().connectivityIndex, 0.0, 1.0);
            const double add = 0.10 * gap * contact * (0.20 + 0.80 * conn);
            totalBonus += add;
        }
    }
    
    totalBonus = std::min(totalBonus, 0.25);
    
    return totalBonus;
}

double Country::getMaxSizeMultiplier() const {
    return m_maxSizeMultiplier; // Territory size multiplier from technology
}

int Country::getExpansionRateBonus() const {
    return m_expansionRateBonus; // Extra pixels per year from technology
}

int Country::getBurstExpansionRadius() const {
    return m_burstExpansionRadius; // How many pixels outward to expand in bursts
}

int Country::getBurstExpansionFrequency() const {
    return m_burstExpansionFrequency; // How often burst expansion occurs
}

int Country::getWarBurstConquestRadius() const {
    return m_warBurstConquestRadius; // How many enemy pixels deep to capture in war bursts
}

int Country::getWarBurstConquestFrequency() const {
    return m_warBurstConquestFrequency; // How often war burst conquest occurs
}

// 🏛️ IDEOLOGY SYSTEM IMPLEMENTATION
std::string Country::getIdeologyString() const {
    switch(m_ideology) {
        case Ideology::Tribal: return "Tribal";
        case Ideology::Chiefdom: return "Chiefdom";
        case Ideology::Kingdom: return "Kingdom";
        case Ideology::Empire: return "Empire";
        case Ideology::Republic: return "Republic";
        case Ideology::Democracy: return "Democracy";
        case Ideology::Dictatorship: return "Dictatorship";
        case Ideology::Federation: return "Federation";
        case Ideology::Theocracy: return "Theocracy";
        case Ideology::CityState: return "City-State";
        default: return "Unknown";
    }
}

bool Country::canChangeToIdeology(Ideology newIdeology) const {
    // Define valid ideology transitions
    switch(m_ideology) {
        case Ideology::Tribal:
            return (newIdeology == Ideology::Chiefdom || newIdeology == Ideology::CityState);
        case Ideology::Chiefdom:
            return (newIdeology == Ideology::Kingdom || newIdeology == Ideology::Republic);
        case Ideology::Kingdom:
            return (newIdeology == Ideology::Empire || newIdeology == Ideology::Democracy || 
                   newIdeology == Ideology::Dictatorship || newIdeology == Ideology::Theocracy);
        case Ideology::Empire:
            return (newIdeology == Ideology::Democracy || newIdeology == Ideology::Dictatorship || 
                   newIdeology == Ideology::Federation);
        case Ideology::Republic:
            return (newIdeology == Ideology::Democracy || newIdeology == Ideology::Dictatorship || 
                   newIdeology == Ideology::Empire);
        case Ideology::Democracy:
            return (newIdeology == Ideology::Federation || newIdeology == Ideology::Dictatorship);
        case Ideology::Dictatorship:
            return (newIdeology == Ideology::Democracy || newIdeology == Ideology::Empire);
        case Ideology::Federation:
            return (newIdeology == Ideology::Democracy || newIdeology == Ideology::Dictatorship);
        case Ideology::Theocracy:
            return (newIdeology == Ideology::Democracy || newIdeology == Ideology::Dictatorship || 
                   newIdeology == Ideology::Kingdom);
        case Ideology::CityState:
            return (newIdeology == Ideology::Democracy || newIdeology == Ideology::Dictatorship);
        default:
            return false;
    }
}

void Country::checkIdeologyChange(int currentYear, News& news, const TechnologyManager& techManager) {
    std::mt19937_64& gen = m_rng;
    
    // Check for ideology changes every 25 years
    if (currentYear % 25 != 0) return;
    
    // Must have sufficient population and development for ideology changes
    if (m_population < 5000) return;
    
    std::vector<Ideology> possibleIdeologies;

    const double pop = static_cast<double>(std::max<long long>(1, m_population));
    const double urban = std::clamp(m_totalCityPopulation / pop, 0.0, 1.0);
    const double admin = std::clamp(getAdminCapacity(), 0.0, 1.0);
    const double control = std::clamp(getAvgControl(), 0.0, 1.0);
    const double stability = std::clamp(getStability(), 0.0, 1.0);
    const double legit = std::clamp(getLegitimacy(), 0.0, 1.0);
    const double capability = std::clamp(
        0.30 * admin + 0.25 * control + 0.20 * legit + 0.15 * stability + 0.10 * urban,
        0.0, 1.0);
    const double ambition = std::clamp(m_leader.ambition, 0.0, 1.0);
    const bool hasProtoWriting = TechnologyManager::hasTech(techManager, *this, TechId::PROTO_WRITING);
    const bool hasNumeracy = TechnologyManager::hasTech(techManager, *this, TechId::NUMERACY_MEASUREMENT);
    const bool hasWriting = TechnologyManager::hasTech(techManager, *this, TechId::WRITING);
    const bool hasPaper = TechnologyManager::hasTech(techManager, *this, 33);
    const bool hasPrinting = TechnologyManager::hasTech(techManager, *this, 36);
    const bool hasEducation = TechnologyManager::hasTech(techManager, *this, TechId::EDUCATION);
    const bool hasCivilService = TechnologyManager::hasTech(techManager, *this, TechId::CIVIL_SERVICE);
    const bool hasBanking = TechnologyManager::hasTech(techManager, *this, TechId::BANKING);
    
    // Determine possible ideology transitions based on current ideology
    switch(m_ideology) {
        case Ideology::Tribal:
            if (m_population > 10000) possibleIdeologies.push_back(Ideology::Chiefdom);
            if (m_hasCity && capability > 0.10) possibleIdeologies.push_back(Ideology::CityState);
            break;
        case Ideology::Chiefdom:
            if (m_population > 25000 && capability > 0.15) possibleIdeologies.push_back(Ideology::Kingdom);
            if (hasProtoWriting && hasNumeracy &&
                admin > 0.12 && control > 0.35 && urban > 0.08 &&
                stability > 0.50 && legit > 0.50 &&
                currentYear >= -1500) {
                possibleIdeologies.push_back(Ideology::Republic);
            }
            break;
        case Ideology::Kingdom:
            if (m_boundaryPixels.size() > static_cast<size_t>(std::max(700, 1200 - static_cast<int>(320.0 * ambition))) &&
                admin > 0.14 && capability > 0.20 &&
                (ambition > 0.52 || m_type == Type::Warmonger)) {
                possibleIdeologies.push_back(Ideology::Empire);
            }
            if (hasEducation && hasCivilService && hasWriting && hasPaper && hasPrinting &&
                admin > 0.26 && control > 0.50 && urban > 0.22 &&
                stability > 0.65 && legit > 0.62 &&
                currentYear >= 1200) {
                possibleIdeologies.push_back(Ideology::Democracy);
            }
            if (m_type == Type::Warmonger && hasWriting &&
                admin > 0.18 && control > 0.44 && stability > 0.45 && capability > 0.24 &&
                currentYear >= -500) {
                possibleIdeologies.push_back(Ideology::Dictatorship);
            }
            break;
        case Ideology::Empire:
            if (hasEducation && hasCivilService && hasWriting && hasPaper && hasPrinting &&
                admin > 0.30 && control > 0.54 && urban > 0.24 &&
                stability > 0.66 && legit > 0.64 &&
                currentYear >= 1200) {
                possibleIdeologies.push_back(Ideology::Democracy);
            }
            if (hasWriting && admin > 0.22 && control > 0.48 && capability > 0.28 && currentYear >= -500) {
                possibleIdeologies.push_back(Ideology::Dictatorship);
            }
            if (m_boundaryPixels.size() > 5200 &&
                hasCivilService && hasBanking &&
                admin > 0.30 && control > 0.52 && stability > 0.62 &&
                currentYear >= 1000) {
                possibleIdeologies.push_back(Ideology::Federation);
            }
            break;
        case Ideology::Republic:
            if (hasEducation && hasCivilService && hasPrinting &&
                admin > 0.24 && control > 0.50 && urban > 0.20 &&
                stability > 0.65 && legit > 0.62 &&
                currentYear >= 1200) {
                possibleIdeologies.push_back(Ideology::Democracy);
            }
            if (hasWriting && capability > 0.28 && currentYear >= -500) {
                possibleIdeologies.push_back(Ideology::Dictatorship);
            }
            if (m_population > 80000 && admin > 0.18 && control > 0.42 &&
                (ambition > 0.58 || m_type == Type::Warmonger)) {
                possibleIdeologies.push_back(Ideology::Empire);
            }
            break;
        // Modern ideologies can transition between each other
        case Ideology::Democracy:
            if (hasCivilService && hasBanking && capability > 0.62 && currentYear >= 1200) {
                possibleIdeologies.push_back(Ideology::Federation);
            }
            if (m_type == Type::Warmonger && capability > 0.56 && currentYear >= 1200) {
                possibleIdeologies.push_back(Ideology::Dictatorship);
            }
            break;
        case Ideology::Dictatorship:
            if (hasEducation && hasCivilService &&
                admin > 0.28 && control > 0.55 && stability > 0.62 && legit > 0.58 &&
                currentYear >= 1200) {
                possibleIdeologies.push_back(Ideology::Democracy);
            }
            if (m_boundaryPixels.size() > 3000 && admin > 0.24 && control > 0.48) {
                possibleIdeologies.push_back(Ideology::Empire);
            }
            break;
        default:
            break;
    }
    
    if (!possibleIdeologies.empty()) {
        std::uniform_int_distribution<> changeChance(1, 100);
        int roll = changeChance(gen);
        
        // Keep regime changes relatively rare and path-dependent.
        int baseChance = 22;
        
        // Warmongers more likely to become empires/dictatorships
        if (m_type == Type::Warmonger) {
            for (Ideology ideology : possibleIdeologies) {
                if (ideology == Ideology::Empire || ideology == Ideology::Dictatorship) {
                    baseChance = 34;
                    break;
                }
            }
        }
        
        if (roll <= baseChance) {
            std::uniform_int_distribution<> ideologyChoice(0, possibleIdeologies.size() - 1);
            Ideology newIdeology = possibleIdeologies[ideologyChoice(gen)];
            
            std::string oldIdeologyStr = getIdeologyString();
            m_ideology = newIdeology;
            std::string newIdeologyStr = getIdeologyString();

            if (m_ideology == Ideology::Republic ||
                m_ideology == Ideology::Democracy ||
                m_ideology == Ideology::Federation) {
                scheduleNextElection(currentYear);
            } else {
                m_nextElectionYear = std::numeric_limits<int>::min();
            }
            
            news.addEvent("🏛️ POLITICAL REVOLUTION: " + m_name + " transforms from " + 
                         oldIdeologyStr + " to " + newIdeologyStr + "!");
            
            std::cout << "🏛️ " << m_name << " changed from " << oldIdeologyStr << " to " << newIdeologyStr << std::endl;
        }
    }
}

void Country::forceLeaderTransition(int currentYear, bool crisis, News& news) {
    transitionLeader(currentYear, crisis, news);
}

// 🗡️ CONQUEST ANNIHILATION SYSTEM - Advanced civs can absorb primitive ones
bool Country::canAnnihilateCountry(const Country& target) const {
    if (!isAtWar()) return false;
    if (target.getPopulation() <= 0) return false;

    const double myMilitaryPower = getMilitaryStrength() * std::sqrt(std::max(1.0, static_cast<double>(m_population) / 10000.0));
    const double targetMilitaryPower = target.getMilitaryStrength() * std::sqrt(std::max(1.0, static_cast<double>(target.getPopulation()) / 10000.0));
    const double powerRatio = (targetMilitaryPower > 1e-6) ? (myMilitaryPower / targetMilitaryPower) : 2.5;
    const double fragility = std::clamp(
        0.55 * (1.0 - target.getStability()) +
        0.45 * (1.0 - target.getLegitimacy()),
        0.0, 1.0);

    const bool canByScale =
        (m_population >= target.getPopulation() * 1.55) &&
        (m_boundaryPixels.size() >= target.getBoundaryPixels().size() * 1.30) &&
        (target.getPopulation() <= 280000);
    const bool canByCollapse =
        (fragility > 0.70) &&
        (powerRatio > 1.30) &&
        (target.getPopulation() <= 420000);

    return (powerRatio > 1.60) && (canByScale || canByCollapse);
}

void Country::absorbCountry(Country& target, Map& map, News& news) {
    std::cout << "🗡️💀 " << m_name << " COMPLETELY ANNIHILATES " << target.getName() << "!" << std::endl;
    
    // Absorb all territory
    const std::vector<sf::Vector2i> targetPixels = target.getTerritoryVec();
    const size_t absorbedTerritory = targetPixels.size();
    {
        std::lock_guard<std::mutex> lock(map.getGridMutex());
        for (const auto& pixel : targetPixels) {
            map.setCountryOwnerAssumingLocked(pixel.x, pixel.y, m_countryIndex);
        }
    }
    
    const bool usePopGrid = map.isPopulationGridActive();

    // Transfer people: in PopulationGrid mode, people stay in place and are re-attributed via ownership.
    const long long gained = std::max(0LL, target.getPopulation());
    if (!usePopGrid) {
        if (LLONG_MAX - m_population < gained) {
            m_population = LLONG_MAX;
        } else {
            m_population += gained;
        }
    }
    
    // Absorb cities
    const auto& targetCities = target.getCities();
    for (const auto& city : targetCities) {
        m_cities.push_back(city);
    }
    
    // Absorb resources and gold
    m_gold += target.getGold() * 0.8; // 80% of target's gold
    
    // Major historical event
    news.addEvent("🗡️💀 ANNIHILATION: " + m_name + " completely destroys " + target.getName() + 
                  " and absorbs " + std::to_string(gained) + " people!");
    
    // Mark the target polity as dead (population will be recomputed from the grid next tick if enabled).
    {
        const std::vector<Country*> enemyLinks = target.getEnemies();
        for (Country* enemy : enemyLinks) {
            if (!enemy) continue;
            enemy->removeEnemy(&target);
            if (enemy->getEnemies().empty()) {
                enemy->clearWarState();
            }
        }
    }
    target.setPopulation(0);
    target.setTerritory(std::unordered_set<sf::Vector2i>{});
    target.setCities(std::vector<City>{});
    target.clearRoadNetwork();
    target.clearWarState();
    
    std::cout << "   📊 Absorbed " << gained << " people and " << absorbedTerritory << " territory!" << std::endl;
}

// Found a new city at a specific location
void Country::foundCity(const sf::Vector2i& location, News& news) {
    m_cities.emplace_back(location);
    m_hasCity = true;
    news.addEvent(m_name + " has built a city!");
}

// Check if the country can found a new city
bool Country::canFoundCity() const {
    // Can found new cities every 2,500,000 population after the first city
    if (m_cities.empty()) return true; // First city always available
    
    // Calculate how many cities this population can support
    int maxCities = 1 + static_cast<int>(m_population / 2500000); // 1 city per 2.5M people
    return m_cities.size() < maxCities;
}

bool Country::canFoundCity(const TechnologyManager& technologyManager) const {
    constexpr float kAdoptionGate = 0.55f;
    // Sedentism + agriculture gate major city formation in deep prehistory.
    if (!hasAdoptedTechId(technologyManager, 113, kAdoptionGate) ||
        !hasAdoptedTechId(technologyManager, 20, kAdoptionGate)) {
        return false;
    }
    return canFoundCity();
}

// Get the list of cities
const std::vector<City>& Country::getCities() const {
    return m_cities;
}

std::vector<City>& Country::getCitiesMutable() {
    return m_cities;
}

// Get the current gold amount
double Country::getGold() const {
    return m_gold;
}

// Add gold to the country's treasury
void Country::addGold(double amount) {
    m_gold += amount;
    if (m_gold < 0.0) m_gold = 0.0; // Prevent negative gold
}

// Subtract gold from the country's treasury
void Country::subtractGold(double amount) {
    m_gold -= amount;
    if (m_gold < 0.0) m_gold = 0.0; // Prevent negative gold
}

// Set the country's gold amount
void Country::setGold(double amount) {
    m_gold = std::max(0.0, amount); // Ensure non-negative
}

// Get the country type
Country::Type Country::getType() const {
    return m_type;
}

// Get the military strength (enhanced by technology)
double Country::getMilitaryStrength() const {
    return m_militaryStrength * getMilitaryStrengthMultiplier();
}

double Country::getSciencePoints() const {
    return m_sciencePoints;
}

void Country::addSciencePoints(double points) {
    m_sciencePoints += points;
}

void Country::setSciencePoints(double points) {
    m_sciencePoints = points;
}

void Country::resetMilitaryStrength() {
    // Reset the military strength to the default based on country type.
    if (m_type == Type::Pacifist) {
        m_militaryStrength = 0.3;
    }
    else if (m_type == Type::Trader) {
        m_militaryStrength = 0.6;
    }
    else if (m_type == Type::Warmonger) {
        m_militaryStrength = 1.3;
    }
}

void Country::applyMilitaryBonus(double bonus) {
    m_militaryStrength *= bonus;
}

// If you have a science production value you want to modify, you could do something similar.
// For example, if you decide to boost the science points accumulation rate:
// (Assume you add a member like m_scienceProduction; if not, you can adjust how you use science points.)
void Country::resetScienceMultiplier() {
    m_scienceMultiplier = 1.0;
}

void Country::applyScienceMultiplier(double bonus) {
    // If multiple great science effects apply, use the highest bonus.
    if (bonus > m_scienceMultiplier) {
        m_scienceMultiplier = bonus;
    }
}

// NEW LOGISTIC POPULATION SYSTEM IMPLEMENTATIONS

double Country::computeYearlyFood(
    const std::vector<std::vector<std::unordered_map<Resource::Type,double>>>& resourceGrid) const {
    double f = 0.0;
    for (const auto& p : m_territoryVec) {
        if (p.y >= 0 && p.y < static_cast<int>(resourceGrid.size()) && 
            p.x >= 0 && p.x < static_cast<int>(resourceGrid[p.y].size())) {
            auto it = resourceGrid[p.y][p.x].find(Resource::Type::FOOD);
            if (it != resourceGrid[p.y][p.x].end()) {
                double pixelFood = it->second;
                
                // CAPITAL CITY BONUS: Starting pixel can support 500k people
                if (p == m_startingPixel) {
                    pixelFood = std::max(pixelFood, 417.0); // Ensures 500k capacity (417 * 1200 = 500,400)
                }
                
                f += pixelFood;
            }
        }
    }
    return f;
}

long long Country::stepLogistic(double r,
    const std::vector<std::vector<std::unordered_map<Resource::Type,double>>>& resourceGrid,
    double techKMultiplier, double climateKMultiplier) {
    
    const double baseK = std::max(1.0, computeYearlyFood(resourceGrid) * 1200.0);
    const double K = baseK * techKMultiplier * climateKMultiplier;

    const double pop = static_cast<double>(m_population);
    const double d = r * pop * (1.0 - pop / K);
    long long delta = static_cast<long long>(std::llround(d));
    long long np = m_population + delta;
    if (np < 0) np = 0;
    m_population = np;
    return delta;
}

long long Country::stepLogisticFromFoodSum(double r, double yearlyFoodSum, double techKMultiplier, double climateKMultiplier) {
    const double baseK = std::max(1.0, yearlyFoodSum * 1200.0);
    const double K = baseK * techKMultiplier * climateKMultiplier;

    const double pop = static_cast<double>(m_population);
    const double d = r * pop * (1.0 - pop / K);
    long long delta = static_cast<long long>(std::llround(d));
    long long np = m_population + delta;
    if (np < 0) np = 0;
    m_population = np;
    return delta;
}

double Country::getPlagueMortalityMultiplier(const TechnologyManager& tm) const {
    double mult = 1.0;
    if (TechnologyManager::hasTech(tm, *this, TechId::SANITATION)) mult *= 0.7;  // Sanitation
    if (TechnologyManager::hasTech(tm, *this, 53)) mult *= 0.6;  // Vaccination
    if (TechnologyManager::hasTech(tm, *this, 65)) mult *= 0.6;  // Penicillin
    return mult;
}

double Country::getCulturePoints() const {
    return m_culturePoints;
}

// TECHNOLOGY SHARING SYSTEM IMPLEMENTATIONS

void Country::initializeTechSharingTimer(int currentYear) {
    if (m_type != Type::Trader) return;
    
    std::uniform_int_distribution<> intervalDist(50, 200);
    
    m_nextTechSharingYear = currentYear + intervalDist(m_rng);
}

void Country::attemptTechnologySharing(int currentYear, std::vector<Country>& allCountries, const TechnologyManager& techManager, const Map& map, News& news) {
    // Only trader countries can share technology
    if (m_type != Type::Trader) return;
    
    // Check if it's time to share
    if (currentYear < m_nextTechSharingYear) return;
    
    // Get our technologies
    const auto& ourTechs = techManager.getUnlockedTechnologies(*this);
    if (ourTechs.empty()) {
        // Reset timer and return if we have no tech to share
        initializeTechSharingTimer(currentYear);
        return;
    }
    
    // Find potential recipients (neighbors only)
    std::vector<int> potentialRecipients;
    std::mt19937_64& gen = m_rng;
    
	    for (int neighborIndex : map.getAdjacentCountryIndicesPublic(m_countryIndex)) {
	        if (neighborIndex < 0 || neighborIndex >= static_cast<int>(allCountries.size())) continue;
	        if (neighborIndex == m_countryIndex) continue; // Skip ourselves
	        if (allCountries[static_cast<size_t>(neighborIndex)].getCountryIndex() != neighborIndex) continue;
	        if (allCountries[static_cast<size_t>(neighborIndex)].getPopulation() <= 0) continue; // Skip dead countries
	        
	        // Must be able to share with them (type preferences + war history)
	        if (!canShareTechWith(allCountries[static_cast<size_t>(neighborIndex)], currentYear)) continue;
	        
	        // Must have fewer technologies than us
	        const auto& theirTechs = techManager.getUnlockedTechnologies(allCountries[static_cast<size_t>(neighborIndex)]);
	        if (theirTechs.size() >= ourTechs.size()) continue;
	        
	        potentialRecipients.push_back(neighborIndex);
	    }
    
    if (potentialRecipients.empty()) {
        // Reset timer and return if no valid recipients
        initializeTechSharingTimer(currentYear);
        return;
    }
    
    // Select recipients (can choose multiple)
    std::uniform_int_distribution<> recipientCountDist(1, std::min(3, static_cast<int>(potentialRecipients.size())));
    int numRecipients = recipientCountDist(gen);
    
    std::shuffle(potentialRecipients.begin(), potentialRecipients.end(), gen);
    
    for (int r = 0; r < numRecipients && r < static_cast<int>(potentialRecipients.size()); ++r) {
        int recipientIndex = potentialRecipients[r];
        Country& recipient = allCountries[recipientIndex];

        // Phase 5A: replace direct tech gifting with knowledge diffusion boosts.
        const auto& kd = getKnowledge();
        auto& kr = recipient.getKnowledgeMutable();
        double totalGain = 0.0;
        for (int d = 0; d < Country::kDomains; ++d) {
            const double gap = kd[static_cast<size_t>(d)] - kr[static_cast<size_t>(d)];
            if (gap <= 0.0) continue;
            const double gain = 0.05 * gap;
            kr[static_cast<size_t>(d)] += gain;
            totalGain += gain;
        }

        if (totalGain > 0.0) {
            news.addEvent("📚💱 KNOWLEDGE TRANSFER: " + m_name + " spreads know-how to " + recipient.getName() + " through trade networks.");
        }
    }
    
    // Reset timer for next sharing opportunity
    initializeTechSharingTimer(currentYear);
}

bool Country::canShareTechWith(const Country& target, int currentYear) const {
    // Cannot share with ourselves
    if (target.getCountryIndex() == m_countryIndex) return false;
    
    // Type-based preferences
    Country::Type targetType = target.getType();

    const std::uint64_t h = SimulationContext::mix64(
        (static_cast<std::uint64_t>(m_countryIndex) << 32) ^
        static_cast<std::uint64_t>(target.getCountryIndex()) ^
        static_cast<std::uint64_t>(currentYear) * 0x9E3779B97F4A7C15ull ^
        0xD1B54A32D192ED03ull);
    const double u = SimulationContext::u01FromU64(h);
    
    if (targetType == Country::Type::Pacifist || targetType == Country::Type::Trader) {
        // 95% chance for pacifists and traders
        return u < 0.95;
    } 
    else if (targetType == Country::Type::Warmonger) {
        // Only 5% chance for warmongers
        if (u >= 0.05) return false;
        
        // Additional check: cannot share with warmongers we were at war with in past 500 years
        auto warEndIt = m_lastWarEndYear.find(target.getCountryIndex());
        if (warEndIt != m_lastWarEndYear.end()) {
            int yearsSinceWar = currentYear - warEndIt->second;
            if (yearsSinceWar < 500) {
                return false; // Too recent war history
            }
        }
        
        return true;
    }
    
    return false;
}

void Country::recordWarEnd(int enemyIndex, int currentYear) {
    m_lastWarEndYear[enemyIndex] = currentYear;
}

// 🏙️ CITY GROWTH SYSTEM - Handle major city upgrades and new city founding
void Country::checkCityGrowth(int currentYear, News& news) {
    
    // Check for major city upgrade when reaching 1 million population
    if (m_population >= 1000000 && !m_cities.empty() && !m_hasCheckedMajorCityUpgrade) {
        
        // Upgrade the first city to a major city (gold square)
        if (!m_cities[0].isMajorCity()) {
            m_cities[0].setMajorCity(true);
            news.addEvent("🏙️ METROPOLIS: " + m_name + " grows its capital into a magnificent major city!");
            std::cout << "🏙️ " << m_name << " upgraded their capital to a major city (gold square)!" << std::endl;
            
            m_hasCheckedMajorCityUpgrade = true; // Prevent multiple upgrades
            
            // Found a new city when upgrading to major city
            if (!m_boundaryPixels.empty()) {
                foundCity(randomTerritoryCell(m_rng), news);
                std::cout << "   📍 " << m_name << " also founded a new city!" << std::endl;
            }
        }
    }
    
    // Reset the upgrade check if population drops below 1 million
    if (m_population < 1000000) {
        m_hasCheckedMajorCityUpgrade = false;
    }
}

// 🛣️ ROAD BUILDING SYSTEM - Build roads between friendly countries
namespace {
int countOceanPixelsOnLine(const std::vector<std::vector<bool>>& isLandGrid,
                          const sf::Vector2i& start,
                          const sf::Vector2i& end) {
    int dx = std::abs(end.x - start.x);
    int dy = std::abs(end.y - start.y);
    int x = start.x;
    int y = start.y;
    int x_inc = (start.x < end.x) ? 1 : -1;
    int y_inc = (start.y < end.y) ? 1 : -1;
    int error = dx - dy;

    dx *= 2;
    dy *= 2;

    int ocean = 0;
    for (int n = dx + dy; n > 0; --n) {
        bool land = false;
        if (y >= 0 && y < static_cast<int>(isLandGrid.size()) &&
            x >= 0 && x < static_cast<int>(isLandGrid[static_cast<size_t>(y)].size())) {
            land = isLandGrid[static_cast<size_t>(y)][static_cast<size_t>(x)];
        }
        if (!land) {
            ocean++;
        }

        if (error > 0) {
            x += x_inc;
            error -= dy;
        } else {
            y += y_inc;
            error += dx;
        }
    }
    return ocean;
}

bool areCountriesAwareForAirways(const Country& a,
                                const Country& b,
                                const Map& map,
                                const TechnologyManager& techManager) {
    // Hook point for your awareness system. For now, we approximate "awareness" using
    // adjacency and long-range communication/navigation tech.
    if (map.areNeighbors(a, b)) {
        return true;
    }
    if (TechnologyManager::hasTech(techManager, a, 62) && TechnologyManager::hasTech(techManager, b, 62)) { // Radio
        return true;
    }
    if (TechnologyManager::hasTech(techManager, a, 73) && TechnologyManager::hasTech(techManager, b, 73)) { // Satellites
        return true;
    }
    if (TechnologyManager::hasTech(techManager, a, 79) && TechnologyManager::hasTech(techManager, b, 79)) { // Internet
        return true;
    }
    if (TechnologyManager::hasTech(techManager, a, TechId::NAVIGATION) &&
        TechnologyManager::hasTech(techManager, b, TechId::NAVIGATION)) { // Navigation
        return true;
    }
    return false;
}
} // namespace

void Country::buildRoads(std::vector<Country>& allCountries,
                         const Map& map,
                         const std::vector<std::vector<bool>>& isLandGrid,
                         const TechnologyManager& techManager,
                         int currentYear,
                         News& news) {
    
    // Only build roads if we have Construction or Roads technology
    if (!TechnologyManager::hasTech(techManager, *this, TechId::CONSTRUCTION) && // Construction
        !TechnologyManager::hasTech(techManager, *this, 17)) { // Roads
        return;
    }
    
    // Only check for road building every 25 years for performance
    if (currentYear < m_nextRoadCheckYear) return;
    
    std::mt19937_64& gen = m_rng;
    // Randomized per-country cadence to spread work: 20–120 years between checks
    std::uniform_int_distribution<> intervalDist(20, 120);
    m_nextRoadCheckYear = currentYear + intervalDist(gen);
    
    // Only build roads if we have cities
    if (m_cities.empty()) return;
    
	    // Find potential road partners (neighbors only).
	    for (int neighborIndex : map.getAdjacentCountryIndicesPublic(m_countryIndex)) {
	        if (neighborIndex < 0 || neighborIndex >= static_cast<int>(allCountries.size())) continue;
	        if (neighborIndex == m_countryIndex) continue; // Skip ourselves
	        Country& otherCountry = allCountries[static_cast<size_t>(neighborIndex)];
	        if (otherCountry.getCountryIndex() != neighborIndex) continue;
	        if (otherCountry.getPopulation() <= 0 || otherCountry.getCities().empty()) continue; // Skip dead/cityless countries
	        
	        // Check if we can build roads to this country
	        if (!canBuildRoadTo(otherCountry, currentYear)) continue;
	        
	        // Check if we already have roads to this country
	        if (m_roadsToCountries.find(otherCountry.getCountryIndex()) != m_roadsToCountries.end()) continue;

	        // Build road between closest cities
	        sf::Vector2i ourClosestCity = getClosestCityTo(otherCountry);
	        sf::Vector2i theirClosestCity = otherCountry.getClosestCityTo(*this);
        
        // Prevent unrealistic cross-ocean "roads": reject if the straight-line corridor
        // crosses too much water (roads only paint land pixels, which can look like
        // a road spanning oceans when adjacency is noisy).
        const int oceanPixels = countOceanPixelsOnLine(isLandGrid, ourClosestCity, theirClosestCity);
        if (oceanPixels > 100) {
            continue;
        }

        // Create road path using simple line algorithm
        std::vector<sf::Vector2i> roadPath = createRoadPath(ourClosestCity, theirClosestCity, map);
        
        if (!roadPath.empty()) {
            // Store the road
            m_roadsToCountries[otherCountry.getCountryIndex()] = roadPath;
            m_roads.insert(m_roads.end(), roadPath.begin(), roadPath.end());
            
            // Also add to the other country (mutual roads)
            otherCountry.m_roadsToCountries[m_countryIndex] = roadPath;
            otherCountry.m_roads.insert(otherCountry.m_roads.end(), roadPath.begin(), roadPath.end());
            
            // Add news event
            news.addEvent("🛣️ ROAD BUILT: " + m_name + " constructs a road network connecting to " + otherCountry.getName() + "!");
            std::cout << "🛣️ " << m_name << " built roads to " << otherCountry.getName() << " (" << roadPath.size() << " pixels)" << std::endl;
            
            // Only build one road per check cycle to prevent spam
            break;
        }
    }
}

bool Country::canBuildAirwayTo(const Country& otherCountry, int currentYear) const {
    (void)currentYear;
    if (otherCountry.getCountryIndex() == m_countryIndex) {
        return false;
    }
    if (otherCountry.getPopulation() <= 0 || otherCountry.getCities().empty()) {
        return false;
    }
    if (m_population <= 0 || m_cities.empty()) {
        return false;
    }
    if (m_airways.find(otherCountry.getCountryIndex()) != m_airways.end()) {
        return false;
    }
    return true;
}

void Country::buildAirways(std::vector<Country>& allCountries,
                           const Map& map,
                           const TechnologyManager& techManager,
                           int currentYear,
                           News& news) {
    if (!TechnologyManager::hasTech(techManager, *this, 61)) { // Flight
        return;
    }
    if (m_population <= 0 || m_cities.empty()) {
        return;
    }

    // Drop dead/out-of-range airways.
    if (!m_airways.empty()) {
        for (auto it = m_airways.begin(); it != m_airways.end(); ) {
            const int otherIndex = *it;
            if (otherIndex < 0 || otherIndex >= static_cast<int>(allCountries.size()) ||
                allCountries[static_cast<size_t>(otherIndex)].getPopulation() <= 0) {
                it = m_airways.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Only check occasionally for performance.
    if (currentYear < m_nextAirwayCheckYear) {
        return;
    }

    if (allCountries.empty()) {
        return;
    }

    std::mt19937_64& gen = m_rng;
    std::uniform_int_distribution<> intervalDist(40, 180);
    m_nextAirwayCheckYear = currentYear + intervalDist(gen);

    const int majorCities = static_cast<int>(std::count_if(m_cities.begin(), m_cities.end(), [](const City& c) { return c.isMajorCity(); }));
    const int maxAirways = std::max(1, std::min(6, 1 + majorCities));
    if (static_cast<int>(m_airways.size()) >= maxAirways) {
        return;
    }

    // Try a few random partners to avoid O(n) scanning every time.
    std::uniform_int_distribution<> pick(0, std::max(0, static_cast<int>(allCountries.size()) - 1));
    constexpr int kAttempts = 60;

    for (int attempt = 0; attempt < kAttempts; ++attempt) {
        int idx = pick(gen);
        if (idx < 0 || idx >= static_cast<int>(allCountries.size())) {
            continue;
        }
        Country& other = allCountries[static_cast<size_t>(idx)];
        if (!canBuildAirwayTo(other, currentYear)) {
            continue;
        }

        if (!TechnologyManager::hasTech(techManager, other, 61)) { // Flight
            continue;
        }

        if (!areCountriesAwareForAirways(*this, other, map, techManager)) {
            continue;
        }

        // Establish airway (mutual, invisible connection)
        m_airways.insert(other.getCountryIndex());
        other.m_airways.insert(m_countryIndex);

        news.addEvent("✈️ AIRWAY ESTABLISHED: " + m_name + " opens an airway connection with " + other.getName() + ".");

        // Small immediate bonus to make it feel impactful.
        addGold(8.0);
        other.addGold(8.0);

        break;
    }
}

void Country::buildPorts(const std::vector<std::vector<bool>>& isLandGrid,
                         const std::vector<std::vector<int>>& countryGrid,
                         int currentYear,
                         std::mt19937_64& gen,
                         News& news) {
    if (m_population <= 0 || m_cities.empty()) {
        return;
    }

    // Clean up ports that are no longer valid/owned.
    if (!m_ports.empty()) {
        m_ports.erase(std::remove_if(m_ports.begin(), m_ports.end(), [&](const sf::Vector2i& p) {
            if (p.y < 0 || p.y >= static_cast<int>(isLandGrid.size())) {
                return true;
            }
            if (p.x < 0 || p.x >= static_cast<int>(isLandGrid[p.y].size())) {
                return true;
            }
            if (!isLandGrid[p.y][p.x]) {
                return true;
            }
            if (countryGrid[p.y][p.x] != m_countryIndex) {
                return true;
            }
            return !isCoastalLandCell(isLandGrid, p.x, p.y);
        }), m_ports.end());
    }

    // Only check for port building occasionally for performance.
    if (currentYear < m_nextPortCheckYear) {
        return;
    }

    std::uniform_int_distribution<> intervalDist(30, 160);
    m_nextPortCheckYear = currentYear + intervalDist(gen);

    // Limit number of ports per country for now (future boats can scale this up).
    int majorCities = 0;
    for (const auto& city : m_cities) {
        if (city.isMajorCity()) {
            majorCities++;
        }
    }
    const int maxPorts = std::max(1, std::min(5, 1 + majorCities));
    if (static_cast<int>(m_ports.size()) >= maxPorts) {
        return;
    }

    auto spacingOk = [&](const sf::Vector2i& pos) {
        for (const auto& port : m_ports) {
            int dx = pos.x - port.x;
            int dy = pos.y - port.y;
            if (dx * dx + dy * dy < 20 * 20) {
                return false;
            }
        }
        return true;
    };

    auto canPlace = [&](const sf::Vector2i& pos) {
        if (pos.y < 0 || pos.y >= static_cast<int>(isLandGrid.size())) {
            return false;
        }
        if (pos.x < 0 || pos.x >= static_cast<int>(isLandGrid[pos.y].size())) {
            return false;
        }
        if (!isLandGrid[pos.y][pos.x]) {
            return false;
        }
        if (countryGrid[pos.y][pos.x] != m_countryIndex) {
            return false;
        }
        if (!isCoastalLandCell(isLandGrid, pos.x, pos.y)) {
            return false;
        }
        return spacingOk(pos);
    };

    auto tryNear = [&](const sf::Vector2i& base, int radius) {
        if (radius <= 0) {
            return false;
        }
        std::uniform_int_distribution<> off(-radius, radius);
        constexpr int kTries = 260;
        for (int i = 0; i < kTries; ++i) {
            int dx = off(gen);
            int dy = off(gen);
            if (dx * dx + dy * dy > radius * radius) {
                continue;
            }
            sf::Vector2i candidate(base.x + dx, base.y + dy);
            if (!canPlace(candidate)) {
                continue;
            }
            m_ports.push_back(candidate);
            std::sort(m_ports.begin(), m_ports.end(), [](const sf::Vector2i& a, const sf::Vector2i& b) {
                if (a.y != b.y) return a.y < b.y;
                return a.x < b.x;
            });
            m_ports.erase(std::unique(m_ports.begin(), m_ports.end()), m_ports.end());
            news.addEvent("⚓ PORT BUILT: " + m_name + " constructs a coastal port.");
            return true;
        }
        return false;
    };

    std::vector<sf::Vector2i> majorBases;
    std::vector<sf::Vector2i> regularBases;
    majorBases.reserve(m_cities.size());
    regularBases.reserve(m_cities.size());
    for (const auto& city : m_cities) {
        if (city.isMajorCity()) {
            majorBases.push_back(city.getLocation());
        } else {
            regularBases.push_back(city.getLocation());
        }
    }

    std::shuffle(majorBases.begin(), majorBases.end(), gen);
    std::shuffle(regularBases.begin(), regularBases.end(), gen);

    // Try major cities first, then regular cities, then boundary sampling.
    for (const auto& base : majorBases) {
        if (tryNear(base, 70)) {
            return;
        }
    }
    for (const auto& base : regularBases) {
        if (tryNear(base, 50)) {
            return;
        }
    }

    if (m_boundaryPixels.empty()) {
        return;
    }
    for (int attempt = 0; attempt < 400; ++attempt) {
        const sf::Vector2i candidate = randomTerritoryCell(gen);
        if (canPlace(candidate)) {
            m_ports.push_back(candidate);
            std::sort(m_ports.begin(), m_ports.end(), [](const sf::Vector2i& a, const sf::Vector2i& b) {
                if (a.y != b.y) return a.y < b.y;
                return a.x < b.x;
            });
            m_ports.erase(std::unique(m_ports.begin(), m_ports.end()), m_ports.end());
            news.addEvent("⚓ PORT BUILT: " + m_name + " establishes a coastal port.");
            return;
        }
    }
}

bool Country::canAttemptColonization(const TechnologyManager& techManager, const CultureManager& cultureManager) const {
    (void)cultureManager;
    if (m_population <= 0) return false;
    if (m_ports.empty()) return false;
    if (m_avgControl < 0.22) return false;
    if (m_polity.adminCapacity < 0.06) return false;
    if (m_stability < 0.25) return false;
    if (!TechnologyManager::hasTech(techManager, *this, TechId::NAVIGATION)) return false;
    return true;
}

float Country::computeColonizationPressure(const CultureManager& cultureManager,
                                           double marketAccess,
                                           double landPressure) const {
    auto clamp01 = [](double v) { return std::max(0.0, std::min(1.0, v)); };

    if (m_population <= 0) return 0.0f;
    if (m_ports.empty()) return 0.0f;

    const double pop = static_cast<double>(std::max<long long>(1, m_population));
    const double fs = clamp01(getFoodSecurity());
    const double foodStress = clamp01((0.98 - fs) / 0.20);
    const double landStress = clamp01((landPressure - 0.92) / 0.60);

    const auto& m = getMacroEconomy();
    const double nonFoodSurplus = std::max(0.0, m.lastNonFoodOutput - m.lastNonFoodCons);
    const double surplusPc = nonFoodSurplus / pop;
    const double surplusFactor = clamp01(surplusPc / 0.00075);

    const auto& t = getTraits();
    const double mercantile = clamp01(t[3]);
    const double openness = clamp01(t[5]);

    bool hasMaritimeAdmin = false;
    {
        const auto& civics = cultureManager.getUnlockedCivics(*this);
        hasMaritimeAdmin = (std::find(civics.begin(), civics.end(), 12) != civics.end());
    }

    const double stability = clamp01(getStability());
    const double admin = clamp01(getAdminCapacity());
    const double debt = std::max(0.0, getDebt());
    const double debtRatio = debt / (std::max(1.0, getLastTaxTake()) + 1.0);
    const double debtPenalty = clamp01((debtRatio - 1.5) / 4.0);

    const double overstretch = clamp01(static_cast<double>(m_exploration.colonialOverstretch));

    double drive = 0.10;
    drive += 0.55 * landStress;
    drive += 0.35 * foodStress;
    drive += 0.30 * surplusFactor;
    drive += 0.20 * ((mercantile + openness) * 0.5);
    if (hasMaritimeAdmin) drive += 0.14;

    drive *= (0.40 + 0.60 * clamp01(marketAccess));
    drive *= (0.50 + 0.50 * clamp01(getAvgControl()));

    // Constraints.
    drive *= (0.45 + 0.55 * stability);
    drive *= (0.55 + 0.45 * admin);
    drive *= (1.0 - 0.60 * debtPenalty);
    drive *= (1.0 - 0.70 * overstretch);

    return static_cast<float>(clamp01(drive));
}

double Country::computeNavalRangePx(const TechnologyManager& techManager, const CultureManager& cultureManager) const {
    (void)cultureManager;
    auto clamp01 = [](double v) { return std::max(0.0, std::min(1.0, v)); };

    const double logi = clamp01(getLogisticsReach());
    const double admin = clamp01(getAdminCapacity());
    const double access = clamp01(getMarketAccess());

    // Base range in grid-cells ("pixels" in sim units). Scales primarily with logistics/admin.
    double r = 220.0 + 1350.0 * logi + 420.0 * admin;
    r *= (0.45 + 0.55 * access);
    r *= (0.85 + 0.15 * std::min(1.0, std::sqrt(static_cast<double>(m_ports.size()) / 3.0)));

    // Navigation tech is already required for eligibility; these are mild extensions for later improvements.
    if (TechnologyManager::hasTech(techManager, *this, TechId::ASTRONOMY)) r *= 1.20;
    if (TechnologyManager::hasTech(techManager, *this, TechId::SCIENTIFIC_METHOD)) r *= 1.10;
    if (TechnologyManager::hasTech(techManager, *this, 51)) r *= 1.10; // Steam Engine (better ships/logistics)
    if (TechnologyManager::hasTech(techManager, *this, 61)) r *= 1.40; // Flight (effective reach)

    // Conservative caps to avoid "global teleport colonization".
    r = std::max(120.0, std::min(r, 4200.0));
    return r;
}

bool Country::forceAddPort(const Map& map, const sf::Vector2i& pos) {
    const auto& isLand = map.getIsLandGrid();
    const auto& owners = map.getCountryGrid();
    if (isLand.empty() || owners.empty()) return false;

    const int h = static_cast<int>(isLand.size());
    const int w = (h > 0) ? static_cast<int>(isLand[0].size()) : 0;
    if (pos.x < 0 || pos.y < 0 || pos.x >= w || pos.y >= h) return false;
    if (!isLand[static_cast<size_t>(pos.y)][static_cast<size_t>(pos.x)]) return false;
    if (owners[static_cast<size_t>(pos.y)][static_cast<size_t>(pos.x)] != m_countryIndex) return false;

    if (!isCoastalLandCell(isLand, pos.x, pos.y)) {
        return false;
    }

    for (const auto& p : m_ports) {
        if (p == pos) {
            return true;
        }
        const int dx = p.x - pos.x;
        const int dy = p.y - pos.y;
        if (dx * dx + dy * dy < 3 * 3) {
            return true;
        }
    }

    // Bypass spacing and cadence checks, but keep a hard cap.
    if (static_cast<int>(m_ports.size()) >= 8) {
        return false;
    }

    m_ports.push_back(pos);
    std::sort(m_ports.begin(), m_ports.end(), [](const sf::Vector2i& a, const sf::Vector2i& b) {
        if (a.y != b.y) return a.y < b.y;
        return a.x < b.x;
    });
    m_ports.erase(std::unique(m_ports.begin(), m_ports.end()), m_ports.end());
    return true;
}

// Helper function to check if we can build roads to another country
bool Country::canBuildRoadTo(const Country& otherCountry, int currentYear) const {
    
    // Can always build roads to other Traders and Pacifists
    if ((m_type == Type::Trader || m_type == Type::Pacifist) &&
        (otherCountry.getType() == Type::Trader || otherCountry.getType() == Type::Pacifist)) {
        return true;
    }
    
    // Can build roads to Warmongers only if they haven't declared war on us in 500 years
    if (otherCountry.getType() == Type::Warmonger || m_type == Type::Warmonger) {
        
        // Check our war history with them
        auto warEndIt = m_lastWarEndYear.find(otherCountry.getCountryIndex());
        if (warEndIt != m_lastWarEndYear.end()) {
            int yearsSinceWar = currentYear - warEndIt->second;
            if (yearsSinceWar < 500) {
                return false; // Too recent war history
            }
        }
        
        // Check their war history with us
        auto theirWarEndIt = otherCountry.m_lastWarEndYear.find(m_countryIndex);
        if (theirWarEndIt != otherCountry.m_lastWarEndYear.end()) {
            int yearsSinceWar = currentYear - theirWarEndIt->second;
            if (yearsSinceWar < 500) {
                return false; // Too recent war history
            }
        }
        
        // If warmonger countries are currently at war with each other, no roads
        if (isAtWar() && std::find(m_enemies.begin(), m_enemies.end(), &otherCountry) != m_enemies.end()) {
            return false;
        }
        
        return true; // Can build roads if no recent wars
    }
    
    return false;
}

// Helper function to get the closest city to another country
sf::Vector2i Country::getClosestCityTo(const Country& otherCountry) const {
    
    if (m_cities.empty() || otherCountry.getCities().empty()) {
        return sf::Vector2i(0, 0); // Return invalid if no cities
    }
    
    sf::Vector2i closestCity = m_cities[0].getLocation();
    double shortestDistance = std::numeric_limits<double>::max();
    
    for (const auto& ourCity : m_cities) {
        for (const auto& theirCity : otherCountry.getCities()) {
            sf::Vector2i ourPos = ourCity.getLocation();
            sf::Vector2i theirPos = theirCity.getLocation();
            
            double distance = std::sqrt(std::pow(ourPos.x - theirPos.x, 2) + std::pow(ourPos.y - theirPos.y, 2));
            
            if (distance < shortestDistance) {
                shortestDistance = distance;
                closestCity = ourPos;
            }
        }
    }
    
    return closestCity;
}

// Helper function to calculate distance to another country
double Country::calculateDistanceToCountry(const Country& otherCountry) const {
    
    if (m_boundaryPixels.empty() || otherCountry.getBoundaryPixels().empty()) {
        return 1000.0; // Very far if no territory
    }
    
    sf::Vector2i ourCenter = getCapitalLocation();
    sf::Vector2i theirCenter = otherCountry.getCapitalLocation();
    
    return std::sqrt(std::pow(ourCenter.x - theirCenter.x, 2) + std::pow(ourCenter.y - theirCenter.y, 2));
}

// Helper function to create a road path between two points
std::vector<sf::Vector2i> Country::createRoadPath(sf::Vector2i start, sf::Vector2i end, const Map& map) const {
    
    std::vector<sf::Vector2i> path;
    
    // Simple Bresenham's line algorithm for road building
    int dx = std::abs(end.x - start.x);
    int dy = std::abs(end.y - start.y);
    int x = start.x;
    int y = start.y;
    int x_inc = (start.x < end.x) ? 1 : -1;
    int y_inc = (start.y < end.y) ? 1 : -1;
    int error = dx - dy;
    
    dx *= 2;
    dy *= 2;
    
    for (int n = dx + dy; n > 0; --n) {
        // Check if the current pixel is valid for road building
        if (map.isValidRoadPixel(x, y)) {
            path.push_back(sf::Vector2i(x, y));
        }
        
        if (error > 0) {
            x += x_inc;
            error -= dy;
        } else {
            y += y_inc;
            error += dx;
        }
    }
    
    return path;
}

void Country::addCulturePoints(double points) {
    m_culturePoints += points;
}

void Country::setCulturePoints(double points) {
    m_culturePoints = points;
}

void Country::resetCultureMultiplier() {
    m_cultureMultiplier = 1.0;
}

void Country::applyCultureMultiplier(double bonus) {
    // If multiple great culture effects apply, use the highest bonus.
    if (bonus > m_cultureMultiplier) {
        m_cultureMultiplier = bonus;
    }
}


void Country::attemptFactoryConstruction(const TechnologyManager& techManager,
                                         const std::vector<std::vector<bool>>& isLandGrid,
                                         const std::vector<std::vector<int>>& countryGrid,
                                         std::mt19937_64& gen,
                                         News& news) {
    constexpr int kMaxFactories = 5;
    if (!TechnologyManager::hasTech(techManager, *this, 52)) {
        return;
    }
    if (static_cast<int>(m_factories.size()) >= kMaxFactories) {
        return;
    }
    if (m_cities.empty()) {
        return;
    }

    auto spacingOk = [&](const sf::Vector2i& pos) {
        for (const auto& factory : m_factories) {
            int dx = pos.x - factory.x;
            int dy = pos.y - factory.y;
            if (dx * dx + dy * dy < 100) {
                return false;
            }
        }
        return true;
    };

    std::vector<sf::Vector2i> majorCandidates;
    std::vector<sf::Vector2i> regularCandidates;
    for (const auto& city : m_cities) {
        sf::Vector2i loc = city.getLocation();
        if (loc.y < 0 || loc.y >= static_cast<int>(isLandGrid.size())) {
            continue;
        }
        if (loc.x < 0 || loc.x >= static_cast<int>(isLandGrid[loc.y].size())) {
            continue;
        }
        if (!isLandGrid[loc.y][loc.x]) {
            continue;
        }
        if (countryGrid[loc.y][loc.x] != m_countryIndex) {
            continue;
        }
        if (city.isMajorCity()) {
            majorCandidates.push_back(loc);
        } else {
            regularCandidates.push_back(loc);
        }
    }

    if (majorCandidates.empty() && regularCandidates.empty()) {
        return;
    }

    auto tryPlaceFrom = [&](std::vector<sf::Vector2i>& pool) {
        std::shuffle(pool.begin(), pool.end(), gen);
        for (const auto& candidate : pool) {
            if (!spacingOk(candidate)) {
                continue;
            }
            m_factories.push_back(candidate);
            news.addEvent(m_name + " builds a new national factory complex.");
            return true;
        }
        return false;
    };

    if (!tryPlaceFrom(majorCandidates)) {
        tryPlaceFrom(regularCandidates);
    }
}
