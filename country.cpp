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
#include <cmath> // For std::llround

// Initialize static science scaler (tuned for realistic science progression)
double Country::s_scienceScaler = 0.1;

// Constructor
Country::Country(int countryIndex,
                 const sf::Color& color,
                 const sf::Vector2i& startCell,
                 long long initialPopulation,
                 double growthRate,
                 const std::string& name,
                 Type type,
                 std::uint64_t rngSeed) :
    m_countryIndex(countryIndex),
	    m_rng(rngSeed),
	    m_color(color),
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
        initializeTechSharingTimer(-5000); // Start from 5000 BCE
    }

    // 🚀 STAGGERED OPTIMIZATION: Each country gets a random neighbor recalculation interval (20-80 years)
    std::uniform_int_distribution<> intervalDist(20, 80);
    m_neighborRecalculationInterval = intervalDist(m_rng);
    
    // Also add a random offset so countries don't all update in the same year
    std::uniform_int_distribution<> offsetDist(0, m_neighborRecalculationInterval - 1);
    m_neighborBonusLastUpdated = -999999 + offsetDist(m_rng);

    // Stagger initial war check year for Warmongers
    if (m_type == Type::Warmonger) {
        std::uniform_int_distribution<> staggerDist(-4950, -4450); // Example: Range from -4950 to -4450 So countries will trigger their first look for war within -4950 to -4450
        m_nextWarCheckYear = staggerDist(m_rng);
    }

    // Stagger initial road-building check year to offset load
    {
        std::uniform_int_distribution<> initialRoadOffset(0, 120);
        m_nextRoadCheckYear = -5000 + initialRoadOffset(m_rng);
    }

    // Stagger initial port-building check year to offset load
    {
        std::uniform_int_distribution<> initialPortOffset(0, 160);
        m_nextPortCheckYear = -5000 + initialPortOffset(m_rng);
    }

    // Stagger initial airway-building check year to offset load
    {
        std::uniform_int_distribution<> initialAirwayOffset(0, 220);
        m_nextAirwayCheckYear = -5000 + initialAirwayOffset(m_rng);
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

	    // Phase 1 polity initialization (type is flavor only; budgets and constraints drive behavior).
	    m_polity.legitimacy = 0.70;
	    m_polity.adminCapacity = 0.07;
	    m_polity.fiscalCapacity = 0.10;
	    m_polity.logisticsReach = 0.10;
	    m_polity.taxRate = 0.08;
	    m_polity.treasurySpendRate = 1.05;
	    m_polity.debt = 0.0;
	    if (m_type == Type::Warmonger) {
	        m_polity.militarySpendingShare = 0.44;
	        m_polity.adminSpendingShare = 0.28;
	        m_polity.infraSpendingShare = 0.28;
	    } else if (m_type == Type::Trader) {
	        m_polity.militarySpendingShare = 0.26;
	        m_polity.adminSpendingShare = 0.30;
	        m_polity.infraSpendingShare = 0.44;
	    } else {
	        m_polity.militarySpendingShare = 0.24;
	        m_polity.adminSpendingShare = 0.34;
	        m_polity.infraSpendingShare = 0.42;
	    }
	    std::uniform_int_distribution<int> policyOffset(0, 4);
	    m_polity.lastPolicyYear = -5000 + policyOffset(m_rng);

        initializePopulationCohorts();
	}

// Check if the country can declare war
bool Country::canDeclareWar() const {
    // Check if the country is at peace and not already at war with the maximum number of countries
    return m_peaceDuration == 0 && m_enemies.size() < 3;
}

// Start a war with a target country
void Country::startWar(Country& target, News& news) {
    // Check if the target is already an enemy
    if (std::find(m_enemies.begin(), m_enemies.end(), &target) == m_enemies.end()) {
        // Target is NOT already an enemy

        m_isAtWar = true;
        
        // Phase 1: deterministic war duration (pressure will end wars early via budgets/legitimacy).
        const double ourPower = getMilitaryStrength() * std::sqrt(std::max(1.0, static_cast<double>(m_population) / 10000.0));
        const double theirPower = target.getMilitaryStrength() * std::sqrt(std::max(1.0, static_cast<double>(target.getPopulation()) / 10000.0));
        const double ratio = (theirPower > 1e-6) ? (ourPower / theirPower) : 2.0;
        const int baseWarDuration = std::clamp(8 + static_cast<int>(std::round(10.0 / std::max(0.6, ratio))), 6, 24);
        const double durationReduction = getWarDurationReduction();
        m_warDuration = std::max(3, static_cast<int>(std::round(static_cast<double>(baseWarDuration) * (1.0 - durationReduction))));
        
        m_preWarPopulation = m_population;

        // Phase 1: war is an explicit action; default to conquest.
        m_isWarofConquest = true;
        m_isWarofAnnihilation = false;
        news.addEvent(m_name + " has declared war on " + target.getName() + "!");

        // Add target to the list of enemies
        addEnemy(&target);
    }
    else {
        // Target is ALREADY an enemy (do nothing, or maybe add a debug message)
        std::cout << m_name << " is already at war with " << target.getName() << "!" << std::endl;
    }
}

// End the current war
void Country::endWar(int currentYear) {
    m_isAtWar = false;
    m_warDuration = 0;
    m_isWarofAnnihilation = false;
    m_isWarofConquest = false;
    m_peaceDuration = std::clamp(10 + static_cast<int>(std::round(30.0 * (1.0 - m_stability))), 8, 40);
    
    // Record war end time for technology sharing history
    for (Country* enemy : m_enemies) {
        recordWarEnd(enemy->getCountryIndex(), currentYear);
        enemy->recordWarEnd(m_countryIndex, currentYear);
    }
    
    clearEnemies(); // Clear the list of enemies when the war ends
    // Reduce population by 10% for the losing country
    if (m_population > 0) {
        m_population = static_cast<long long>(m_population * 0.9);
    }
}

void Country::clearWarState() {
    m_isAtWar = false;
    m_warDuration = 0;
    m_isWarofAnnihilation = false;
    m_isWarofConquest = false;
    m_peaceDuration = 0;
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
                                    bool plagueAffected) {
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

    // Desired spending is pressure-driven, then capped by what can be financed.
    const double institutionCapacity = clamp01(m_macro.institutionCapacity);
    const double connectivity = clamp01(m_macro.connectivityIndex);
    const double financeLevel = clamp01(0.5 * institutionCapacity + 0.5 * connectivity);

    const double control = clamp01(m_avgControl);
    const double lowControlPressure = clamp01((0.65 - control) / 0.65);
    const double faminePressure = clamp01(m_macro.famineSeverity + std::max(0.0, 0.92 - m_macro.foodSecurity));
    const double warPressure = m_isAtWar ? 1.0 : 0.0;
    const double opportunityPressure = clamp01(0.5 * clamp01(m_macro.marketAccess) + 0.5 * connectivity);

    double desiredSpendFactor = std::clamp(m_polity.treasurySpendRate, 0.35, 2.20);
    desiredSpendFactor +=
        0.22 * warPressure +
        0.18 * lowControlPressure +
        0.18 * faminePressure +
        0.08 * opportunityPressure;

    // Endogenous fiscal correction under debt-service pressure.
    const double debtStart = std::max(0.0, m_polity.debt);
    const double debtToIncomeStartRaw = debtStart / incomeSafe;
    const double debtToIncomeStart = std::clamp(debtToIncomeStartRaw, 0.0, 10.0);
    const double debtThresholdStart = 1.0 + 2.6 * financeLevel;
    const double stressAboveDebtThreshold = clamp01((debtToIncomeStart - debtThresholdStart) / 3.0);
    const double baselineInterest = 0.30 + (0.03 - 0.30) * financeLevel;
    const double serviceToIncomeStartRaw = (debtStart * baselineInterest) / incomeSafe;
    const double serviceToIncomeStart = std::clamp(serviceToIncomeStartRaw, 0.0, 10.0);
    const double serviceStressStart = clamp01((serviceToIncomeStart - 0.25) / 0.35);
    if (serviceToIncomeStart > 0.25 || debtToIncomeStart > debtThresholdStart) {
        const double correction = yearsD * (0.03 + 0.05 * serviceStressStart + 0.04 * stressAboveDebtThreshold);
        m_polity.treasurySpendRate = std::max(0.55, m_polity.treasurySpendRate - correction);

        const double fiscalHeadroom = clamp01((m_polity.fiscalCapacity - 0.20) / 0.80);
        const double taxEffort = yearsD * 0.010 * fiscalHeadroom * (0.35 + 0.65 * std::max(serviceStressStart, stressAboveDebtThreshold));
        m_polity.taxRate = std::clamp(m_polity.taxRate + taxEffort, 0.02, 0.45);
        ldbg.dbg_legit_budget_taxRateSource = 2; // budget debt-service adjustment

        desiredSpendFactor = std::max(0.55, desiredSpendFactor - (0.20 * serviceStressStart + 0.15 * stressAboveDebtThreshold));
    }

    const double desiredAnnual = std::max(0.0, incomeAnnual * desiredSpendFactor);
    const double desiredBlock = desiredAnnual * yearsD;

    const double reserveMonthsTarget = std::clamp(0.75 - 0.45 * financeLevel, 0.25, 0.75);
    const double reserveTarget = incomeAnnual * reserveMonthsTarget;
    const double maxDrawFromReserves = std::max(0.0, m_gold - reserveTarget);

    const bool borrowingEnabled = (financeLevel >= 0.15);
    const double debtLimit =
        incomeAnnual *
        (0.2 + 3.0 * financeLevel) *
        (0.3 + 0.7 * institutionCapacity);
    const double maxNewBorrowing = borrowingEnabled ? std::max(0.0, debtLimit - debtStart) : 0.0;

    const double interestRate = 0.30 + (0.03 - 0.30) * financeLevel;
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
    const double debtThreshold = 1.0 + 2.6 * financeLevel;
    const double debtStress = clamp01((debtToIncome - debtThreshold) / 3.0);
    const double serviceStress = clamp01((serviceToIncome - 0.25) / 0.35);
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
    m_polity.adminCapacity = clamp01(m_polity.adminCapacity - yearsD * 0.012 * shortfallStress);
    m_militaryStrength = std::max(0.0, m_militaryStrength * (1.0 - std::min(0.30, 0.10 * shortfallStress * yearsD)));
    ldbg.dbg_legit_budget_shortfall_direct = -(yearsD * 0.012 * shortfallStress);
    applyBudgetLegitimacyDelta(ldbg.dbg_legit_budget_shortfall_direct);

    // Replace binary "negative gold crisis" with burden-scaled penalties.
    if (serviceToIncome > 0.25 || debtToIncome > debtThreshold) {
        const double before = m_stability;
        m_stability = clamp01(m_stability - yearsD * (0.012 * debtStress + 0.030 * serviceStress + 0.012 * shortfallStress));
        sdbg.dbg_delta_debt_crisis += (m_stability - before);
        ldbg.dbg_legit_budget_burden_penalty =
            -(yearsD * (0.010 * debtStress + 0.026 * serviceStress + 0.010 * shortfallStress));
        applyBudgetLegitimacyDelta(ldbg.dbg_legit_budget_burden_penalty);
        m_macro.leakageRate = std::clamp(m_macro.leakageRate + yearsD * (0.015 * burdenStress + 0.020 * shortfallStress), 0.02, 0.95);
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
        stress += 0.8 * shortfallStress;

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
        const double taxPain = clamp01(0.60 * (1.0 - legitimacyNow) + 0.40 * (1.0 - complianceNow));
        const double taxPenaltySlope = 0.014 + 0.026 * taxPain;
        ldbg.dbg_legit_budget_drift_stability = +0.002 * (stability - 0.5) * yearsD;
        ldbg.dbg_legit_budget_drift_tax = -std::max(0.0, taxRate - 0.12) * taxPenaltySlope * yearsD;
        ldbg.dbg_legit_budget_drift_control = -(1.0 - control) * 0.010 * yearsD;
        ldbg.dbg_legit_budget_drift_debt = -0.008 * debtStress * yearsD;
        ldbg.dbg_legit_budget_drift_service = -0.012 * serviceStress * yearsD;
        ldbg.dbg_legit_budget_drift_shortfall = -0.010 * shortfallStress * yearsD;
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
        return m_cities.front().getLocation();
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
	        if (oppRatio > 1.15) {
	            const double score = std::min(2.0, oppRatio) * (0.5 + 0.5 * (1.0 - n.getStability()));
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
	            m_expansionBudgetCells = 3 + static_cast<int>(std::round(12.0 * pressures.opportunity));
	            if (!m_isAtWar && bestTarget >= 0 && pressures.opportunity > 0.75 && m_gold > income * 0.5 && canDeclareWar()) {
	                startWar(allCountries[static_cast<size_t>(bestTarget)], news);
	            }
	            m_polity.infraSpendingShare += 0.02;
	            m_polity.adminSpendingShare += 0.02;
	            m_polity.militarySpendingShare -= 0.04;
	        }

	        normalizeBudgetShares();
	        m_polity.taxRate = std::clamp(m_polity.taxRate, 0.02, 0.45);
	    }

	    // Phase 1: Replace the type-driven expansion contentment system and burst rails.
	    m_isContentWithSize = false;
	    m_contentmentDuration = 0;
	    const bool doBurstExpansion = false;

		    // AI expansion budget (replaces random growth as the primary engine).
		    int growth = std::clamp(m_expansionBudgetCells, 0, 25);

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

    // Phase 0/1: Replace the year-based expansion rail with an admin/logistics cap.
    const int maxExpansionPixels = std::max(20,
                                           static_cast<int>(60 +
                                                            5000.0 * m_polity.adminCapacity +
                                                            120.0 * static_cast<double>(m_cities.size()) +
                                                            10.0 * static_cast<double>(techCount)));

    // 🚀 NUCLEAR OPTIMIZATION: Use cached boundary pixels count instead of scanning entire grid
    size_t countrySize = m_boundaryPixels.size();

    if (countrySize + growth > static_cast<size_t>(maxExpansionPixels)) {
        growth = std::max(0, static_cast<int>(static_cast<long long>(maxExpansionPixels) - static_cast<long long>(countrySize)));
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
        
	        if (false && warBurstFreq > 0 && currentYear % warBurstFreq == 0 && warBurstRadius > 1) {
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
            }
        }
    }
    else {
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
	    if (false && m_type == Type::Warmonger && !m_isContentWithSize && !m_boundaryPixels.empty()) {
	        std::uniform_real_distribution<> blobChance(0.0, 1.0);
	        if (blobChance(gen) < 0.5) {
	            int currentApproxSize = static_cast<int>(m_boundaryPixels.size());
	            int remainingCapacity = std::max(0, maxExpansionPixels - currentApproxSize);

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

    for (const auto& cell : m_boundaryPixels) {
        if (cell.x >= 0 && cell.x < static_cast<int>(resourceGrid[0].size()) && 
            cell.y >= 0 && cell.y < static_cast<int>(resourceGrid.size())) {
                // Use .at() to access elements of the const unordered_map
            auto it = resourceGrid[cell.y][cell.x].find(Resource::Type::FOOD);
            if (it != resourceGrid[cell.y][cell.x].end()) {
                foodAvailable += it->second;
            }
            for (const auto& [type, amount] : resourceGrid[cell.y][cell.x]) {
                    if (type != Resource::Type::FOOD) {
                        m_resourceManager.addResource(type, amount);
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

    bool plagueAffected = plagueActive && map.isCountryAffectedByPlague(m_countryIndex);
    double stabilityDelta = 0.0;
    double deltaWar = 0.0;
    double deltaPlague = 0.0;
    double deltaStagnation = 0.0;
    double deltaPeaceRecover = 0.0;
    if (isAtWar()) {
        deltaWar = -0.05;
        stabilityDelta += deltaWar;
    }
    if (plagueAffected) {
        deltaPlague = -0.08;
        stabilityDelta += deltaPlague;
    }
    if (m_stagnationYears > 20) {
        deltaStagnation = -0.02;
        stabilityDelta += deltaStagnation;
    }
    if (!isAtWar() && !plagueAffected) {
        deltaPeaceRecover = (growthRatio > 0.003) ? 0.02 : 0.005;
        stabilityDelta += deltaPeaceRecover;
    }

    m_stability = std::max(0.0, std::min(1.0, m_stability + stabilityDelta));
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
	        if (m_population >= 10000 && canFoundCity() && !m_boundaryPixels.empty()) {
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

    // Decrement war and peace durations
    if (isAtWar()) {
        decrementWarDuration();
        if (m_warDuration <= 0) {
            // Get the name of the enemy before ending the war
            std::string enemyName;
            if (!m_enemies.empty()) {
                enemyName = (*m_enemies.begin())->getName(); // Assuming you only have one enemy at a time
            }

            endWar(currentYear);

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
        
        const int techCount = static_cast<int>(technologyManager.getUnlockedTechnologies(*this).size());
        const int maxExpansionPixels = std::max(20,
                                               static_cast<int>(60 +
                                                                5000.0 * m_polity.adminCapacity +
                                                                120.0 * static_cast<double>(m_cities.size()) +
                                                                10.0 * static_cast<double>(techCount)));
        int currentPixels = m_boundaryPixels.size();
        
        if (currentPixels < maxExpansionPixels) {
            // Apply growth limit
            if (currentPixels + growth > maxExpansionPixels) {
                growth = std::max(0, maxExpansionPixels - currentPixels);
            }
            
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
	        if (!plagueAffected && (currentYear % 20 == 0) && m_population >= 10000 && canFoundCity() && !m_boundaryPixels.empty()) {
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
void Country::applyTechnologyBonus(int techId) {
	    switch(techId) {
        // 🌾 EARLY AGRICULTURAL TECHNOLOGIES
        case 10: // Irrigation
            // Population growth now handled by logistic system
            m_maxSizeMultiplier += 0.2; // +20% max territory size
            break;
        case 20: // Agriculture
            // Population growth now handled by logistic system
            m_maxSizeMultiplier += 0.3; // +30% max territory size
            m_expansionRateBonus += 5; // +5 extra pixels per year
            break;
            
        // RESEARCH BOOST TECHNOLOGIES - MASSIVE EXPONENTIAL ACCELERATION
        case 11: // Writing
            m_sciencePointsBonus += 3.0; // +3.0 science points per year (written knowledge)
            break;
        case 14: // Mathematics
            m_sciencePointsBonus += 5.0; // +5.0 science points per year (mathematical thinking)
            break;
	        case 22: // Philosophy
	            m_sciencePointsBonus += 8.0; // +8.0 science points per year (systematic thinking)
	            break;
	        case TechId::UNIVERSITIES: // Universities
	            m_sciencePointsBonus += 15.5; // +15.5 science points per year (organized learning + education)
	            m_maxSizeMultiplier += 0.30; // +30% max territory size (educated population)
	            m_researchMultiplier *= 1.10; // +10% research speed (organized learning)
	            break;
	        case TechId::ASTRONOMY: // Astronomy
	            m_sciencePointsBonus += 20.0; // +20.0 science points per year (scientific observation)
	            break;
	        case TechId::SCIENTIFIC_METHOD: // Scientific Method
	            m_sciencePointsBonus += 50.0; // +50.0 science points per year (RESEARCH REVOLUTION!)
	            m_researchMultiplier *= 1.10; // +10% research speed (scientific methodology)
	            break;
        case 54: // Electricity
            m_sciencePointsBonus += 30.0; // +30.0 science points per year (electrical age)
            m_researchMultiplier *= 1.05; // +5% research speed (electrical infrastructure)
            break;
        case 69: // Computers
            m_sciencePointsBonus += 100.0; // +100.0 science points per year (COMPUTATIONAL REVOLUTION!)
            m_researchMultiplier *= 1.10; // +10% research speed (computational power)
            break;
        case 76: // Integrated Circuit
            m_sciencePointsBonus += 75.0; // +75.0 science points per year (microprocessor age)
            break;
        case 79: // Internet
            m_sciencePointsBonus += 200.0; // +200.0 science points per year (INFORMATION EXPLOSION!)
            m_researchMultiplier *= 1.10; // +10% research speed (global knowledge sharing)
            break;
        case 80: // Personal Computers
            m_sciencePointsBonus += 150.0; // +150.0 science points per year (computing for all)
            break;
        case 85: // Artificial Intelligence
            m_sciencePointsBonus += 300.0; // +300.0 science points per year (AI BREAKTHROUGH!)
            m_researchMultiplier *= 1.15; // +15% research speed (AI-assisted research)
            break;
        case 93: // Machine Learning
            m_sciencePointsBonus += 250.0; // +250.0 science points per year (ML algorithms)
            m_researchMultiplier *= 1.10; // +10% research speed (automated pattern recognition)
            break;
            
        // 🗡️ ANCIENT MILITARY TECHNOLOGIES
        case 3: // Archery
            m_militaryStrengthBonus += 0.15; // +15% military strength
            m_territoryCaptureBonusRate += 0.10; // +10% capture rate
            break;
        case 9: // Bronze Working
            m_militaryStrengthBonus += 0.25; // +25% military strength
            m_defensiveBonus += 0.15; // +15% defensive bonus
            break;
        case 13: // Iron Working
            m_militaryStrengthBonus += 0.40; // +40% military strength
            m_territoryCaptureBonusRate += 0.20; // +20% capture rate
            m_defensiveBonus += 0.25; // +25% defensive bonus
            break;
        case 18: // Horseback Riding
            m_militaryStrengthBonus += 0.30; // +30% military strength
            m_territoryCaptureBonusRate += 0.35; // +35% capture rate (cavalry speed)
            m_warDurationReduction += 0.20; // -20% war duration (mobile warfare)
            m_expansionRateBonus += 8; // +8 pixels per year (mobile expansion)
            break;
            
        // 🏗️ INFRASTRUCTURE TECHNOLOGIES
        case 16: // Construction
            m_maxSizeMultiplier += 0.25; // +25% max territory size
            m_expansionRateBonus += 3; // +3 pixels per year
            break;
        case 17: // Roads
            m_maxSizeMultiplier += 0.40; // +40% max territory size
            m_expansionRateBonus += 6; // +6 pixels per year (road networks)
            break;
        case 23: // Engineering
            m_maxSizeMultiplier += 0.50; // +50% max territory size
            m_expansionRateBonus += 8; // +8 pixels per year (advanced construction)
            break;
        case 32: // Civil Service
            m_maxSizeMultiplier += 0.60; // +60% max territory size (organized administration)
            m_expansionRateBonus += 10; // +10 pixels per year
            break;
            
        // 🌊 RENAISSANCE EXPLORATION TECHNOLOGIES - MASSIVE COLONIAL EXPANSION!
        case 12: // Shipbuilding
            m_maxSizeMultiplier += 0.50; // +50% max territory size (overseas expansion)
            m_expansionRateBonus += 12; // +12 pixels per year
            m_burstExpansionRadius = 2; // Expand 2 pixels outward in bursts
            m_burstExpansionFrequency = 10; // Burst expansion every 10 years
            break;
	        case 26: // Compass
	            m_maxSizeMultiplier += 0.75; // +75% max territory size (navigation mastery)
	            m_expansionRateBonus += 20; // +20 pixels per year (precise navigation)
	            m_burstExpansionRadius = 3; // Expand 3 pixels outward in bursts
	            m_burstExpansionFrequency = 8; // Burst expansion every 8 years
	            break;
	        case TechId::NAVIGATION: // Navigation
	            m_maxSizeMultiplier += 1.5; // +150% max territory size (MASSIVE COLONIAL EMPIRES!)
	            m_flatMaxSizeBonus += 2000; // +2000 coastal/ocean pixels (~190k sq mi) unlocked by blue-water navigation
	            m_expansionRateBonus += 90; // +90 pixels per year (enhanced colonial logistics)
	            m_burstExpansionRadius = 6; // Expand 6 pixels outward in bursts (deeper ocean corridors)
	            m_burstExpansionFrequency = 4; // Burst expansion every 4 years
	            break;
            
        // 💰 ECONOMIC EXPANSION TECHNOLOGIES
	        case 34: // Banking
	            m_maxSizeMultiplier += 0.80; // +80% max territory size (economic expansion)
	            m_expansionRateBonus += 25; // +25 pixels per year (funded expansion)
	            break;
	        case TechId::ECONOMICS: // Economics
	            m_maxSizeMultiplier += 1.2; // +120% max territory size (economic empires)
	            m_expansionRateBonus += 35; // +35 pixels per year (economic efficiency)
	            break;
	        case 36: // Printing
	            m_maxSizeMultiplier += 0.60; // +60% max territory size (information spread)
	            m_expansionRateBonus += 15; // +15 pixels per year (administrative efficiency)
	            m_sciencePointsBonus += 0.3; // +0.3 science points per year (knowledge spread)
	            break;
            
        // 🚂 INDUSTRIAL EXPANSION
        case 55: // Railroad
            m_maxSizeMultiplier += 2.0; // +200% max territory size (CONTINENTAL EMPIRES!)
            m_flatMaxSizeBonus += 3000; // +3000 inland pixels (~285k sq mi) opened by continental rail grids
            m_expansionRateBonus += 180; // +180 pixels per year (rapid rail logistics)
            m_burstExpansionRadius = 10; // Expand 10 pixels outward in bursts (rail hub surges)
            m_burstExpansionFrequency = 2; // Burst expansion every 2 years
            break;
            
	        // ⚔️ MEDIEVAL MILITARY TECHNOLOGIES
	        case 28: // Steel
	            m_militaryStrengthBonus += 0.50; // +50% military strength
	            m_defensiveBonus += 0.40; // +40% defensive bonus
	            m_territoryCaptureBonusRate += 0.25; // +25% capture rate
	            m_warBurstConquestRadius = 3; // Capture 3 pixels deep in war bursts
	            m_warBurstConquestFrequency = 8; // War burst every 8 years
	            break;
	        case 37: // Gunpowder
	            m_militaryStrengthBonus += 0.75; // +75% military strength (revolutionary!)
	            m_territoryCaptureBonusRate += 0.50; // +50% capture rate
	            m_warDurationReduction += 0.30; // -30% war duration (decisive battles)
	            m_warBurstConquestRadius = 5; // Capture 5 pixels deep in war bursts (CANNON BREAKTHROUGHS!)
	            m_warBurstConquestFrequency = 5; // War burst every 5 years
	            break;
            
	        // 🔫 INDUSTRIAL MILITARY TECHNOLOGIES
	        case 47: // Firearms
	            m_militaryStrengthBonus += 0.60; // +60% military strength
	            m_territoryCaptureBonusRate += 0.40; // +40% capture rate
	            m_warDurationReduction += 0.25; // -25% war duration
	            m_warBurstConquestRadius = 4; // Capture 4 pixels deep (rifle advances)
	            m_warBurstConquestFrequency = 6; // War burst every 6 years
	            break;
	        case 50: // Rifling
	            m_militaryStrengthBonus += 0.35; // +35% additional strength
	            m_defensiveBonus += 0.50; // +50% defensive bonus (accuracy)
	            m_warBurstConquestRadius = 6; // Capture 6 pixels deep (precision warfare)
	            m_warBurstConquestFrequency = 4; // War burst every 4 years
	            break;
        case 56: // Dynamite
            m_militaryStrengthBonus += 0.45; // +45% military strength
            m_territoryCaptureBonusRate += 0.60; // +60% capture rate (explosive breakthroughs)
            m_warBurstConquestRadius = 7; // Capture 7 pixels deep (EXPLOSIVE BREAKTHROUGHS!)
            m_warBurstConquestFrequency = 3; // War burst every 3 years
            break;
            
        // 💣 MODERN MILITARY TECHNOLOGIES
        case 68: // Nuclear Fission
            m_militaryStrengthBonus += 1.50; // +150% military strength (nuclear supremacy!)
            m_warDurationReduction += 0.70; // -70% war duration (decisive advantage)
            m_territoryCaptureBonusRate += 0.80; // +80% capture rate
            m_warBurstConquestRadius = 10; // Capture 10 pixels deep (NUCLEAR DEVASTATION!)
            m_warBurstConquestFrequency = 2; // War burst every 2 years
            break;
        case 77: // Advanced Ballistics
            m_militaryStrengthBonus += 0.40; // +40% military strength
            m_territoryCaptureBonusRate += 0.30; // +30% capture rate
            m_defensiveBonus += 0.35; // +35% defensive bonus
            m_warBurstConquestRadius = 5; // Capture 5 pixels deep (precision strikes)
            m_warBurstConquestFrequency = 5; // War burst every 5 years
            break;
        case 84: // Stealth Technology
            m_militaryStrengthBonus += 0.60; // +60% military strength
            m_warDurationReduction += 0.40; // -40% war duration (surprise attacks)
            m_territoryCaptureBonusRate += 0.45; // +45% capture rate
            m_warBurstConquestRadius = 8; // Capture 8 pixels deep (stealth infiltration)
            m_warBurstConquestFrequency = 3; // War burst every 3 years
            break;
            
        // 🏥 MEDICAL/HEALTH TECHNOLOGIES  
        case 96: // Sanitation
            m_plagueResistanceBonus += 0.30; // 30% plague resistance
            // Population growth now handled by logistic system
            break;
        case 53: // Vaccination
            m_plagueResistanceBonus += 0.50; // 50% plague resistance
            // Population growth now handled by logistic system
            break;
        case 65: // Penicillin
            m_plagueResistanceBonus += 0.60; // 60% plague resistance
            // Population growth now handled by logistic system
            break;
            
        // 🥶 FOOD/PRESERVATION TECHNOLOGIES
        case 71: // Refrigeration
            // Population growth now handled by logistic system
            break;
            
        // 🔬 ADVANCED TECHNOLOGIES
        case 81: // Genetic Engineering
            // Population growth now handled by logistic system
            m_plagueResistanceBonus += 0.40; // 40% additional plague resistance
            m_militaryStrengthBonus += 0.30; // +30% military strength (enhanced soldiers)
            break;
        case 90: // Biotechnology
            // Population growth now handled by logistic system
            m_plagueResistanceBonus += 0.50; // 50% additional plague resistance
            m_militaryStrengthBonus += 0.25; // +25% military strength (bio-enhancements)
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
    
    // Determine possible ideology transitions based on current ideology
    switch(m_ideology) {
        case Ideology::Tribal:
            if (m_population > 10000) possibleIdeologies.push_back(Ideology::Chiefdom);
            if (m_hasCity) possibleIdeologies.push_back(Ideology::CityState);
            break;
        case Ideology::Chiefdom:
            if (m_population > 25000) possibleIdeologies.push_back(Ideology::Kingdom);
            // Republican institutions require literacy + administrative capacity.
            if (TechnologyManager::hasTech(techManager, *this, TechId::WRITING) &&
                admin > 0.08 && control > 0.28 && urban > 0.06) {
                possibleIdeologies.push_back(Ideology::Republic);
            }
            break;
        case Ideology::Kingdom:
            if (m_boundaryPixels.size() > 1200 && admin > 0.10) possibleIdeologies.push_back(Ideology::Empire);
            // Democracy is gated by education + state capacity, not a points currency.
            if (TechnologyManager::hasTech(techManager, *this, TechId::EDUCATION) &&
                TechnologyManager::hasTech(techManager, *this, TechId::CIVIL_SERVICE) &&
                admin > 0.14 && control > 0.35 && urban > 0.12 && stability > 0.55 && legit > 0.55) {
                possibleIdeologies.push_back(Ideology::Democracy);
            }
            if (m_type == Type::Warmonger) possibleIdeologies.push_back(Ideology::Dictatorship);
            break;
        case Ideology::Empire:
            if (TechnologyManager::hasTech(techManager, *this, TechId::EDUCATION) &&
                TechnologyManager::hasTech(techManager, *this, TechId::CIVIL_SERVICE) &&
                admin > 0.18 && control > 0.40 && urban > 0.14 && stability > 0.60 && legit > 0.60) {
                possibleIdeologies.push_back(Ideology::Democracy);
            }
            possibleIdeologies.push_back(Ideology::Dictatorship);
            if (m_boundaryPixels.size() > 5200 &&
                TechnologyManager::hasTech(techManager, *this, TechId::CIVIL_SERVICE) &&
                admin > 0.22 && control > 0.45 && stability > 0.55) {
                possibleIdeologies.push_back(Ideology::Federation);
            }
            break;
        case Ideology::Republic:
            if (TechnologyManager::hasTech(techManager, *this, TechId::EDUCATION) &&
                admin > 0.12 && control > 0.34 && urban > 0.10 && stability > 0.55 && legit > 0.55) {
                possibleIdeologies.push_back(Ideology::Democracy);
            }
            possibleIdeologies.push_back(Ideology::Dictatorship);
            if (m_population > 100000) possibleIdeologies.push_back(Ideology::Empire);
            break;
        // Modern ideologies can transition between each other
        case Ideology::Democracy:
            possibleIdeologies.push_back(Ideology::Federation);
            if (m_type == Type::Warmonger) possibleIdeologies.push_back(Ideology::Dictatorship);
            break;
        case Ideology::Dictatorship:
            possibleIdeologies.push_back(Ideology::Democracy);
            if (m_boundaryPixels.size() > 3000) possibleIdeologies.push_back(Ideology::Empire);
            break;
        default:
            break;
    }
    
    if (!possibleIdeologies.empty()) {
        std::uniform_int_distribution<> changeChance(1, 100);
        int roll = changeChance(gen);
        
        // Base 50% chance, modified by country type
        int baseChance = 50;
        
        // Warmongers more likely to become empires/dictatorships
        if (m_type == Type::Warmonger) {
            for (Ideology ideology : possibleIdeologies) {
                if (ideology == Ideology::Empire || ideology == Ideology::Dictatorship) {
                    baseChance = 70; // 70% chance for warmongers
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
            
            news.addEvent("🏛️ POLITICAL REVOLUTION: " + m_name + " transforms from " + 
                         oldIdeologyStr + " to " + newIdeologyStr + "!");
            
            std::cout << "🏛️ " << m_name << " changed from " << oldIdeologyStr << " to " << newIdeologyStr << std::endl;
        }
    }
}

// 🗡️ CONQUEST ANNIHILATION SYSTEM - Advanced civs can absorb primitive ones
bool Country::canAnnihilateCountry(const Country& target) const {
    // Must be a warmonger to annihilate
    if (m_type != Type::Warmonger) return false;
    
    // Must be at war with the target
    if (!isAtWar()) return false;
    
    // Check if this country is significantly more advanced
    double myMilitaryPower = getMilitaryStrength();
    double targetMilitaryPower = target.getMilitaryStrength();
    
    // Must have overwhelming military superiority (3x stronger)
    if (myMilitaryPower < targetMilitaryPower * 3.0) return false;
    
    // Must have significantly larger population (2x larger)
    if (m_population < target.getPopulation() * 2) return false;
    
    // Must have more territory (1.5x larger)
    if (m_boundaryPixels.size() < target.getBoundaryPixels().size() * 1.5) return false;
    
    // Additional check: Target must be small enough to absorb (less than 50k population)
    if (target.getPopulation() > 50000) return false;
    
    return true;
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
    for (const auto& p : m_boundaryPixels) {
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
