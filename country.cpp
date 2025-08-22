// country.cpp

#include "country.h"
#include "map.h"
#include "technology.h"
#include <random>
#include <algorithm>
#include <iostream>
#include <limits>
#include <mutex>

// Constructor
Country::Country(int countryIndex, const sf::Color& color, const sf::Vector2i& startCell, long long initialPopulation, double growthRate, const std::string& name, Type type, ScienceType scienceType, CultureType cultureType) : 
    m_countryIndex(countryIndex),
    m_color(color),
    m_population(initialPopulation),
    m_populationGrowthRate(growthRate),
    m_culturePoints(0.0), // Initialize culture points to zero
    m_name(name),
    m_type(type),
    m_scienceType(scienceType),
    m_cultureType(cultureType),
    m_ideology(Ideology::Tribal), // All countries start as Tribal
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
    m_sciencePoints(0.0) // Initialize science points to zero
{
    m_boundaryPixels.insert(startCell);
    //intiialize war check

    // Set initial military strength based on type
    if (m_type == Type::Pacifist) {
        m_militaryStrength = 0.3;
    }
    else if (m_type == Type::Trader) {
        m_militaryStrength = 0.6;
    }
    else if (m_type == Type::Warmonger) {
        m_militaryStrength = 1.3;
    }

    // 🚀 STAGGERED OPTIMIZATION: Each country gets a random neighbor recalculation interval (20-80 years)
        std::random_device rd;
        std::mt19937 gen(rd());
    std::uniform_int_distribution<> intervalDist(20, 80);
    m_neighborRecalculationInterval = intervalDist(gen);
    
    // Also add a random offset so countries don't all update in the same year
    std::uniform_int_distribution<> offsetDist(0, m_neighborRecalculationInterval - 1);
    m_neighborBonusLastUpdated = -999999 + offsetDist(gen);

    // Stagger initial war check year for Warmongers
    if (m_type == Type::Warmonger) {
        std::uniform_int_distribution<> staggerDist(-4950, -4450); // Example: Range from -4950 to -4450 So countries will trigger their first look for war within -4950 to -4450
        m_nextWarCheckYear = staggerDist(gen);
    }
    
    // 🎯 INITIALIZE EXPANSION CONTENTMENT SYSTEM - reuse existing gen
    
    // Stagger burst expansion timing to prevent lag spikes
    std::uniform_int_distribution<> staggerDist(0, 20);
    m_expansionStaggerOffset = staggerDist(gen);
    
    // Set initial expansion contentment based on country type
    std::uniform_int_distribution<> contentmentChance(1, 100);
    int roll = contentmentChance(gen);
    
    if (m_type == Type::Pacifist) {
        // Pacifists: 60% chance to be content, 5% chance permanent
        if (roll <= 5) {
            m_isContentWithSize = true;
            m_contentmentDuration = 999999; // Permanent contentment
        } else if (roll <= 60) {
            m_isContentWithSize = true;
            std::uniform_int_distribution<> durationDist(50, 300);
            m_contentmentDuration = durationDist(gen);
        }
    } else if (m_type == Type::Trader) {
        // Traders: 40% chance to be content, 2% chance permanent
        if (roll <= 2) {
            m_isContentWithSize = true;
            m_contentmentDuration = 999999; // Permanent contentment
        } else if (roll <= 40) {
            m_isContentWithSize = true;
            std::uniform_int_distribution<> durationDist(30, 200);
            m_contentmentDuration = durationDist(gen);
        }
    } else { // Warmonger
        // Warmongers: 15% chance to be content, 0.5% chance permanent
        if (roll <= 1) { // 0.5% chance (rounded to 1%)
            m_isContentWithSize = true;
            m_contentmentDuration = 999999; // Permanent contentment (rare peaceful warmonger)
        } else if (roll <= 15) {
            m_isContentWithSize = true;
            std::uniform_int_distribution<> durationDist(10, 100);
            m_contentmentDuration = durationDist(gen);
        }
    }
}

// Get maximum expansion pixels based on the current year
int Country::getMaxExpansionPixels(int year) const {
    // Implementation of expansion limits based on year
    int baseLimit = 0;
    if (year >= -5000 && year < -4500) {
        baseLimit = 135;
    }
    else if (year >= -4500 && year < -4000) {
        baseLimit = 588;
    }
    else if (year >= -4000 && year < -3500) {
        baseLimit = 1176;
    }
    else if (year >= -3500 && year < -3000) {
        baseLimit = 2058;
    }
    else if (year >= -3000 && year < -2500) {
        baseLimit = 2941;
    }
    else if (year >= -2500 && year < -2000) {
        baseLimit = 4411;
    }
    else if (year >= -2000 && year < -1500) {
        baseLimit = 5882;
    }
    else if (year >= -1500 && year < -1000) {
        baseLimit = 8823;
    }
    else if (year >= -1000 && year < -500) {
        baseLimit = 11764;
    }
    else if (year >= -500 && year < 1) {
        baseLimit = 17647;
    }
    else if (year >= 1 && year < 500) {
        baseLimit = 23529;
    }
    else if (year >= 500 && year < 1000) {
        baseLimit = 29411;
    }
    else {
        return std::numeric_limits<int>::max(); // No limit after 1000 CE
    }
    // Modify the limit based on country type
    int typeModifiedLimit;
    if (m_type == Type::Pacifist) {
        // 10% chance to be able to expand to 50% of the limit
        if (rand() % 10 == 0) { // 10% chance
            typeModifiedLimit = static_cast<int>(baseLimit * 0.5);
        }
        else {
            typeModifiedLimit = static_cast<int>(baseLimit * 0.1);
        }
    }
    else if (m_type == Type::Warmonger) {
        typeModifiedLimit = static_cast<int>(baseLimit * 10);
    }
    else { // Trader
        typeModifiedLimit = baseLimit;
    }
    
    // 🚀 TECHNOLOGY EXPANSION MULTIPLIER - Advanced civs can build massive empires!
    double techMultiplier = getMaxSizeMultiplier();
    return static_cast<int>(typeModifiedLimit * techMultiplier);
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
        
        // 🗡️ TECHNOLOGY-ENHANCED WAR DURATION
        int baseWarDuration = 1 + rand() % 100; // Random duration between 1 and 100 years
        double durationReduction = getWarDurationReduction();
        m_warDuration = std::max(1, static_cast<int>(baseWarDuration * (1.0 - durationReduction)));
        
        m_preWarPopulation = m_population;

        // Determine war type
        if (rand() % 100 < 60) { // 60% chance of War of Conquest
            m_isWarofConquest = true;
            news.addEvent(m_name + " has declared a War of Conquest on " + target.getName() + "!");
        }
        else { // 40% chance of War of Annihilation
            m_isWarofAnnihilation = true;
            news.addEvent(m_name + " has declared a War of Annihilation on " + target.getName() + "!");
        }

        // Add target to the list of enemies
        addEnemy(&target);
    }
    else {
        // Target is ALREADY an enemy (do nothing, or maybe add a debug message)
        std::cout << m_name << " is already at war with " << target.getName() << "!" << std::endl;
    }
}

// End the current war
void Country::endWar() {
    m_isAtWar = false;
    m_warDuration = 0;
    m_isWarofAnnihilation = false;
    m_isWarofConquest = false;
    m_peaceDuration = 1 + rand() % 100; // Start a peace period of 1-100 years
    clearEnemies(); // Clear the list of enemies when the war ends
    // Reduce population by 10% for the losing country
    if (m_population > 0) {
        m_population = static_cast<long long>(m_population * 0.9);
    }
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
void Country::update(const std::vector<std::vector<bool>>& isLandGrid, std::vector<std::vector<int>>& countryGrid, std::mutex& gridMutex, int gridCellSize, int regionSize, std::unordered_set<int>& dirtyRegions, int currentYear, const std::vector<std::vector<std::unordered_map<Resource::Type, double>>>& resourceGrid, News& news, bool plagueActive, long long& plagueDeaths, const Map& map) {
    
    // PERFORMANCE OPTIMIZATION: Use static generators to avoid expensive random_device creation
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<> growthDist(10, 25);
    int growth = growthDist(gen);
    
    // 🚀 TECHNOLOGY EXPANSION RATE BONUS - Advanced civs expand faster!
    growth += getExpansionRateBonus();
    
    // 🌊 BURST EXPANSION CHECK - Staggered and contentment-aware
    bool doBurstExpansion = false;
    int burstRadius = getBurstExpansionRadius();
    int burstFrequency = getBurstExpansionFrequency();
    
    // Check contentment status first
    if (m_contentmentDuration > 0) {
        m_contentmentDuration--;
        if (m_contentmentDuration <= 0) {
            m_isContentWithSize = false; // End contentment period
        }
    }
    
    // 🎯 EXPANSION CONTENTMENT - Some countries don't want to expand
    if (!m_isContentWithSize && burstFrequency > 0 && burstRadius > 1) {
        // Use staggered timing to prevent all countries expanding simultaneously
        int staggeredYear = currentYear + m_expansionStaggerOffset;
        if (staggeredYear % burstFrequency == 0) {
            doBurstExpansion = true;
            
            // 🎲 RANDOM CONTENTMENT CHECK - Countries may become content after expansion
            std::uniform_int_distribution<> contentmentRoll(1, 100);
            int roll = contentmentRoll(gen);
            
            if (m_type == Type::Pacifist && roll <= 25) { // 25% chance for pacifists
                m_isContentWithSize = true;
                std::uniform_int_distribution<> durationDist(30, 150);
                m_contentmentDuration = durationDist(gen);
            } else if (m_type == Type::Trader && roll <= 15) { // 15% chance for traders
                m_isContentWithSize = true;
                std::uniform_int_distribution<> durationDist(20, 100);
                m_contentmentDuration = durationDist(gen);
            } else if (m_type == Type::Warmonger && roll <= 3) { // 3% chance for warmongers
                m_isContentWithSize = true;
                std::uniform_int_distribution<> durationDist(5, 50);
                m_contentmentDuration = durationDist(gen);
            }
        }
    }

    // Science point generation - MAJOR ACCELERATION REBALANCE
    double sciencePointIncrease = 0.8; // Significantly increased base rate for faster progression

    // Culture point generation
    double culturePointIncrease = 1.0; // Base culture point per year

    // Apply multipliers based on CultureType
    if (m_cultureType == CultureType::MC) {
        // MC countries get 1.1x to 2x culture points
        culturePointIncrease *= (1.1 + (static_cast<double>(rand()) / RAND_MAX) * (2.0 - 1.1));
    }
    else if (m_cultureType == CultureType::LC) {
        // LC countries get 0.1x to 0.35x culture points
        culturePointIncrease *= (0.1 + (static_cast<double>(rand()) / RAND_MAX) * (0.35 - 0.1));
    }

    addCulturePoints(culturePointIncrease * m_cultureMultiplier);

    // Apply multipliers based on ScienceType - REALISTIC REBALANCE
    if (m_scienceType == ScienceType::MS) {
        // MS countries get modest 1.3x to 1.8x boost (was 1.2x to 2.5x - too high!)
        sciencePointIncrease *= (1.3 + (static_cast<double>(rand()) / RAND_MAX) * (1.8 - 1.3));
    }
    else if (m_scienceType == ScienceType::LS) {
        // LS countries get 0.3x to 0.6x science points (was 0.05x to 0.15x - too severe!)
        sciencePointIncrease *= (0.3 + (static_cast<double>(rand()) / RAND_MAX) * (0.6 - 0.3));
    }
    // NS countries keep 1.0x multiplier (no change)

    // Apply science points with technology and neighbor bonuses
    double totalScienceIncrease = sciencePointIncrease * m_scienceMultiplier * getSciencePointsMultiplier();
    addSciencePoints(totalScienceIncrease);

    // Get the maximum expansion pixels for the current year
    int maxExpansionPixels = getMaxExpansionPixels(currentYear);

    // 10% chance to exceed the limit
    std::uniform_real_distribution<> chance(0.0, 1.0);
    if (chance(gen) < 0.1) {
        std::uniform_int_distribution<> extraPixels(40, 90);
        maxExpansionPixels += extraPixels(gen);
    }

    // 🚀 NUCLEAR OPTIMIZATION: Use cached boundary pixels count instead of scanning entire grid
    size_t countrySize = m_boundaryPixels.size();

    if (countrySize + growth > maxExpansionPixels) {
        growth = std::max(0, static_cast<int>(maxExpansionPixels - countrySize));
    }

    std::uniform_int_distribution<> neighborDist(-1, 1);

    std::vector<sf::Vector2i> newBoundaryPixels;
    // 🚀 NUCLEAR OPTIMIZATION: Don't copy entire grid - work directly on main grid with proper locking
    std::vector<sf::Vector2i> currentBoundaryPixels(m_boundaryPixels.begin(), m_boundaryPixels.end());

    // Warmonger war multiplier (you can adjust this value)
    const double warmongerWarMultiplier = 2.0;

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
        
        if (warBurstFreq > 0 && currentYear % warBurstFreq == 0 && warBurstRadius > 1) {
            doWarBurstConquest = true;
            std::cout << "💥 " << m_name << " launches WAR BURST CONQUEST (radius " << warBurstRadius << ")!" << std::endl;
        }

        // Get enemy indices
        std::vector<int> enemyIndices;
        for (Country* enemy : getEnemies()) {
            enemyIndices.push_back(enemy->getCountryIndex());
        }

        for (int i = 0; i < growth; ++i) {
            if (currentBoundaryPixels.empty()) break;

            std::uniform_int_distribution<size_t> boundaryIndexDist(0, currentBoundaryPixels.size() - 1);
            size_t boundaryIndex = boundaryIndexDist(gen);
            sf::Vector2i currentCell = currentBoundaryPixels[boundaryIndex];

            currentBoundaryPixels.erase(currentBoundaryPixels.begin() + boundaryIndex);

            int dx = neighborDist(gen);
            int dy = neighborDist(gen);
            if (dx == 0 && dy == 0) continue;

            sf::Vector2i newCell = currentCell + sf::Vector2i(dx, dy);

            if (newCell.x >= 0 && newCell.x < static_cast<int>(isLandGrid[0].size()) && newCell.y >= 0 && newCell.y < isLandGrid.size()) {
                // 🛡️ DEADLOCK FIX: Check enemy territory first without lock
                bool isEnemyTerritory = false;
                int currentCellOwner;
                
                {
                    std::lock_guard<std::mutex> lock(gridMutex);
                    currentCellOwner = countryGrid[newCell.y][newCell.x];
                }
                
                // Check if the new cell belongs to an enemy
                for (int enemyIndex : enemyIndices) {
                    if (currentCellOwner == enemyIndex) {
                        isEnemyTerritory = true;
                        break;
                    }
                }

                if (isEnemyTerritory) {
                    // Capture the cell (existing logic)
                    // 🗡️ TECHNOLOGY-ENHANCED CAPTURE MECHANICS
                    std::uniform_real_distribution<> captureChance(0.0, 1.0);
                    
                    // Base capture chance: 60%
                    double baseCaptureRate = 0.6;
                    
                    // Warmonger bonus
                    double warmongerCaptureBonus = (m_type == Type::Warmonger) ? 0.2 : 0.0;
                    
                    // Technology capture bonus
                    double techCaptureBonus = getTerritoryCaptureBonusRate();
                    
                    // Total capture chance
                    double totalCaptureRate = baseCaptureRate + warmongerCaptureBonus + techCaptureBonus;
                    totalCaptureRate = std::min(0.95, totalCaptureRate); // Cap at 95%
                    
                    if (captureChance(gen) < totalCaptureRate) { // Technology-enhanced capture chance
                        // 🛡️ DEADLOCK FIX: Separate lock for grid update
                        {
                            std::lock_guard<std::mutex> lock(gridMutex);
                            countryGrid[newCell.y][newCell.x] = m_countryIndex;
                        }
                        newBoundaryPixels.push_back(newCell);



                        // 💀 ENHANCED POPULATION LOSS - Territory loss = population loss
                        for (Country* enemy : getEnemies()) {
                            if (countryGrid[newCell.y][newCell.x] == enemy->getCountryIndex()) {
                                // FIXED: Much more reasonable population loss
                                double randomFactor = static_cast<double>(rand() % 101) / 100.0; // 0.00 to 1.00
                                long long baseLoss = static_cast<long long>(enemy->getPopulation() * (0.001 + (0.002 * randomFactor))); // 0.1-0.3% per pixel (much lower!)
                                
                                enemy->setPopulation(std::max(0LL, enemy->getPopulation() - baseLoss));
                                
                                // Remove the lost pixel from enemy's boundary
                                enemy->m_boundaryPixels.erase(newCell);

                                // If the captured cell contains a city, transfer it to the attacker
                                for (auto it = enemy->m_cities.begin(); it != enemy->m_cities.end(); ) {
                                    if (it->getLocation() == newCell) {
                                        // Transfer city
                                        addConqueredCity(*it);
                                        it = enemy->m_cities.erase(it);

                                        // Reduce population by 10% for losing a city
                                        long long cityPopulationLoss = static_cast<long long>(enemy->getPopulation() * 0.10);
                                        enemy->setPopulation(std::max(0LL, enemy->getPopulation() - cityPopulationLoss));
                                        break;
                                    }
                                    else {
                                        ++it;
                                    }
                                }
                                break;
                            }
                        }

                        // 🛡️ DEADLOCK FIX: Update dirty regions separately
                        int regionIndex = static_cast<int>((newCell.y / regionSize) * (isLandGrid[0].size() / regionSize) + (newCell.x / regionSize));
                        {
                            std::lock_guard<std::mutex> lock(gridMutex);
                            dirtyRegions.insert(regionIndex);
                        }

                        // 🚀 OPTIMIZATION: Quick boundary check
                        bool isNewBoundary = false;
                        for (int y = -1; y <= 1; ++y) {
                            for (int x = -1; x <= 1; ++x) {
                                if (x == 0 && y == 0) continue;
                                sf::Vector2i neighborCell = newCell + sf::Vector2i(x, y);
                                if (neighborCell.x >= 0 && neighborCell.x < countryGrid[0].size() && neighborCell.y >= 0 && neighborCell.y < countryGrid.size() && countryGrid[neighborCell.y][neighborCell.x] == -1) {
                                    isNewBoundary = true;
                                    break;
                                }
                            }
                            if (isNewBoundary) break;
                        }
                        if (isNewBoundary) {
                            m_boundaryPixels.insert(newCell);
                        }
                    }
                }
            }
        }
        
        // ⚡💥 HYPER-OPTIMIZED WAR BURST CONQUEST - Lightning fast! 💥⚡
        if (doWarBurstConquest && !enemyIndices.empty()) {
            
            // OPTIMIZATION 1: Pre-calculate target count
            int targetCaptures = std::min(warBurstRadius * 20, 150); // Simple calculation
            
            // OPTIMIZATION 2: Fast enemy territory sampling
            std::vector<sf::Vector2i> enemyTargets;
            enemyTargets.reserve(targetCaptures);
            
            // OPTIMIZATION 3: Sample only 15 boundary pixels
            std::vector<sf::Vector2i> warSample;
            auto it = currentBoundaryPixels.begin();
            for (int i = 0; i < 15 && it != currentBoundaryPixels.end(); ++i) {
                warSample.push_back(*it);
                std::advance(it, std::max(1, static_cast<int>(currentBoundaryPixels.size()) / 15));
            }
            
            // OPTIMIZATION 4: Fast directional enemy search
            for (const auto& basePixel : warSample) {
                for (int attempt = 0; attempt < targetCaptures / 15; ++attempt) {
                    // Random direction within war burst radius
                    std::uniform_int_distribution<> warDir(-warBurstRadius, warBurstRadius);
                    int dx = warDir(gen);
                    int dy = warDir(gen);
                    
                    sf::Vector2i targetCell = basePixel + sf::Vector2i(dx, dy);
                    
                    // Quick bounds check
                    if (targetCell.x >= 0 && targetCell.x < static_cast<int>(isLandGrid[0].size()) && 
                        targetCell.y >= 0 && targetCell.y < isLandGrid.size()) {
                        
                        int cellOwner = countryGrid[targetCell.y][targetCell.x]; // Direct access
                        
                        // Fast enemy check
                        for (int enemyIndex : enemyIndices) {
                            if (cellOwner == enemyIndex) {
                                enemyTargets.push_back(targetCell);
                                break;
                            }
                        }
                    }
                    
                    if (enemyTargets.size() >= targetCaptures) break;
                }
                if (enemyTargets.size() >= targetCaptures) break;
            }
            
            // OPTIMIZATION 5: Batch capture with high success rate
            int successfulCaptures = 0;
            double enhancedCaptureRate = 0.85 + getTerritoryCaptureBonusRate(); // High base rate
            enhancedCaptureRate = std::min(0.95, enhancedCaptureRate);
            
            for (const auto& targetCell : enemyTargets) {
                std::uniform_real_distribution<> quickCapture(0.0, 1.0);
                if (quickCapture(gen) < enhancedCaptureRate) {
                    countryGrid[targetCell.y][targetCell.x] = m_countryIndex;
                    m_boundaryPixels.insert(targetCell);
                    
                    // Fast dirty region marking
                    int regionSize = map.getRegionSize();
                    int regionIndex = static_cast<int>((targetCell.y / regionSize) * (isLandGrid[0].size() / regionSize) + (targetCell.x / regionSize));
                    const_cast<Map&>(map).insertDirtyRegion(regionIndex);
                    
                    successfulCaptures++;
                }
            }
            
            if (successfulCaptures > 0) {
                std::cout << "   ⚡💥 " << m_name << " HYPER-FAST war burst: " << successfulCaptures << " enemy pixels!" << std::endl;
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

            int dx = neighborDist(gen);
            int dy = neighborDist(gen);
            if (dx == 0 && dy == 0) continue;

            sf::Vector2i newCell = currentCell + sf::Vector2i(dx, dy);

            if (newCell.x >= 0 && newCell.x < static_cast<int>(isLandGrid[0].size()) && newCell.y >= 0 && newCell.y < isLandGrid.size()) {
                // 🛡️ DEADLOCK FIX: Check availability first, then update
                bool canExpand = false;
                
                {
                    std::lock_guard<std::mutex> lock(gridMutex);
                    canExpand = (countryGrid[newCell.y][newCell.x] == -1 && isLandGrid[newCell.y][newCell.x]);
                }
                
                if (canExpand) {
                    {
                        std::lock_guard<std::mutex> lock(gridMutex);
                        countryGrid[newCell.y][newCell.x] = m_countryIndex;
                        int regionIndex = static_cast<int>((newCell.y / regionSize) * (isLandGrid[0].size() / regionSize) + (newCell.x / regionSize));
                        dirtyRegions.insert(regionIndex);
                    }
                    newBoundaryPixels.push_back(newCell);

                    bool isNewBoundary = false;
                    for (int y = -1; y <= 1; ++y) {
                        for (int x = -1; x <= 1; ++x) {
                            if (x == 0 && y == 0) continue;
                            sf::Vector2i neighborCell = newCell + sf::Vector2i(x, y);
                            if (neighborCell.x >= 0 && neighborCell.x < countryGrid[0].size() && neighborCell.y >= 0 && neighborCell.y < countryGrid.size() && countryGrid[neighborCell.y][neighborCell.x] == -1) {
                                isNewBoundary = true;
                                break;
                            }
                        }
                        if (isNewBoundary) break;
                    }
                    if (isNewBoundary) {
                        m_boundaryPixels.insert(newCell);
                    }
                }
            }
        }
    }

    // 🚀⚡ SUPER OPTIMIZED BURST EXPANSION - Lightning fast! ⚡🚀
    if (doBurstExpansion && !m_boundaryPixels.empty() && !m_isContentWithSize) {
        
        // OPTIMIZATION 1: Pre-calculate burst size to avoid expensive nested loops
        int targetBurstPixels = burstRadius * burstRadius * 2; // Approximate burst area
        targetBurstPixels = std::min(targetBurstPixels, 120); // Cap for performance
        
        // OPTIMIZATION 2: Use random sampling instead of exhaustive search
        std::vector<sf::Vector2i> burstTargets;
        burstTargets.reserve(targetBurstPixels); // Pre-allocate memory
        
        // OPTIMIZATION 3: Sample random directions from random boundary pixels
        std::vector<sf::Vector2i> sampleBoundary;
        int sampleSize = std::min(static_cast<int>(m_boundaryPixels.size()), 20); // Only sample 20 boundary pixels
        sampleBoundary.reserve(sampleSize);
        
        auto it = m_boundaryPixels.begin();
        for (int i = 0; i < sampleSize; ++i) {
            std::advance(it, gen() % std::max(1, static_cast<int>(m_boundaryPixels.size()) / sampleSize));
            if (it != m_boundaryPixels.end()) {
                sampleBoundary.push_back(*it);
                it = m_boundaryPixels.begin(); // Reset iterator
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
                countryGrid[targetCell.y][targetCell.x] = m_countryIndex;
                m_boundaryPixels.insert(targetCell);
                
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

    if (foodAvailable >= foodConsumption) {
        m_resourceManager.consumeResource(Resource::Type::FOOD, foodConsumption);
        if (!plagueActive)
        {
            // 🧬 USE TECHNOLOGY-ENHANCED GROWTH RATE
            double enhancedGrowthRate = getTotalPopulationGrowthRate();
            m_population += static_cast<long long>(m_population * enhancedGrowthRate);
        }
        else
        {
            // During plagues, still get some growth but reduced
            double enhancedGrowthRate = getTotalPopulationGrowthRate();
            m_population += static_cast<long long>(m_population * (enhancedGrowthRate / 10));
        }
    }
    else {
        double populationLoss = m_population * (0.001 + (0.009 * (1.0 - (foodAvailable / foodConsumption))));
        m_population -= static_cast<long long>(populationLoss);
        if (m_population < 0) m_population = 0;
    }
    // Plague effects
    if (plagueActive) {
        // Store the pre-plague population at the start of the plague
        if (currentYear == map.getPlagueStartYear()) {
            m_prePlaguePopulation = m_population;
        }

        // 🧬 TECHNOLOGY-MODIFIED PLAGUE DEATHS
        double basePlagueDeathRate = 0.08; // 8% base death rate
        double resistance = getPlagueResistance();
        double actualDeathRate = basePlagueDeathRate * (1.0 - resistance);
        
        long long deaths = static_cast<long long>(m_population * actualDeathRate);
        m_population -= deaths;
        if (m_population < 0) m_population = 0;

        // Update the total death toll
        plagueDeaths += deaths;
    }
    // 🚀 NUCLEAR OPTIMIZATION: Streamlined city founding
    if (m_population >= 10000 && canFoundCity() && !m_boundaryPixels.empty()) {
        // Use a random boundary pixel directly (no vector copy needed)
        auto it = m_boundaryPixels.begin();
        std::advance(it, gen() % m_boundaryPixels.size());
        foundCity(*it, news);
        m_hasCity = true;
    }

    // City management (add gold for each city)
    m_gold += m_cities.size() * 1.0; // Each city produces 1 gold (optimized)
    
    // 🏛️ CHECK FOR IDEOLOGY CHANGES
    checkIdeologyChange(currentYear, news);

    // Decrement war and peace durations
    if (isAtWar()) {
        decrementWarDuration();
        if (m_warDuration <= 0) {
            // Get the name of the enemy before ending the war
            std::string enemyName;
            if (!m_enemies.empty()) {
                enemyName = (*m_enemies.begin())->getName(); // Assuming you only have one enemy at a time
            }

            endWar();

            // Add news event after ending the war
            if (!enemyName.empty()) {
                news.addEvent("The war between " + m_name + " and " + enemyName + " has ended!");
            }
        }
    }
    else if (m_peaceDuration > 0) {
        decrementPeaceDuration();
    }
}

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
    m_boundaryPixels.insert(cell);
}

// Get the country's boundary pixels
const std::unordered_set<sf::Vector2i>& Country::getBoundaryPixels() const {
    return m_boundaryPixels;
}

// Get the resource manager
const ResourceManager& Country::getResourceManager() const {
    return m_resourceManager;
}

// Get the country's name
const std::string& Country::getName() const {
    return m_name;
}

// FAST FORWARD MODE: Optimized growth simulation
void Country::fastForwardGrowth(int yearIndex, int currentYear, const std::vector<std::vector<bool>>& isLandGrid, 
                               std::vector<std::vector<int>>& countryGrid, 
                               const std::vector<std::vector<std::unordered_map<Resource::Type, double>>>& resourceGrid,
                               News& news, const Map& map) {
    
    std::random_device rd;
    std::mt19937 gen(rd());
    
    // 🧬 TECHNOLOGY-ENHANCED POPULATION GROWTH
    double enhancedGrowthRate = getTotalPopulationGrowthRate();
    m_population += static_cast<long long>(m_population * enhancedGrowthRate);
    
    // Add science and culture points (simplified)
    addSciencePoints(5.0 * m_scienceMultiplier * getSciencePointsMultiplier());  // 5x normal rate + tech bonuses for fast forward
    addCulturePoints(5.0 * m_cultureMultiplier);
    
    // 🚀 ENHANCED FAST FORWARD EXPANSION - Use same advanced mechanics as normal update
    if (yearIndex % 2 == 0 && !m_isContentWithSize) { // Every 2 years, respect contentment
        
        // Use technology-enhanced expansion rate (same as normal update)
        std::uniform_int_distribution<> growthDist(20, 40); // Higher base for fast forward
        int growth = growthDist(gen);
        growth += getExpansionRateBonus(); // Apply all technology bonuses!
        
        int maxExpansionPixels = getMaxExpansionPixels(currentYear); // Uses technology multipliers!
        int currentPixels = m_boundaryPixels.size();
        
        if (currentPixels < maxExpansionPixels) {
            // Apply growth limit
            if (currentPixels + growth > maxExpansionPixels) {
                growth = std::max(0, maxExpansionPixels - currentPixels);
            }
            
            // Normal expansion with technology bonuses
            std::vector<sf::Vector2i> currentBoundary(m_boundaryPixels.begin(), m_boundaryPixels.end());
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
                    
                    countryGrid[newCell.y][newCell.x] = m_countryIndex;
                    m_boundaryPixels.insert(newCell);
                    
                    // 🔥 CRITICAL FIX: Mark region as dirty for visual update!
                    int regionSize = map.getRegionSize();
                    int regionIndex = static_cast<int>((newCell.y / regionSize) * (isLandGrid[0].size() / regionSize) + (newCell.x / regionSize));
                    const_cast<Map&>(map).insertDirtyRegion(regionIndex);
                }
            }
            
            // ⚡🚀 SUPER OPTIMIZED FAST FORWARD BURST - Lightning fast! 🚀⚡
            int burstRadius = getBurstExpansionRadius();
            int burstFreq = getBurstExpansionFrequency();
            
            if (burstFreq > 0 && (yearIndex + m_expansionStaggerOffset) % burstFreq == 0 && burstRadius > 1) {
                
                // HYPER OPTIMIZATION: Direct random placement instead of nested loops
                int targetPixels = std::min(burstRadius * 15, 80); // Simple calculation
                std::vector<sf::Vector2i> burstTargets;
                burstTargets.reserve(targetPixels);
                
                // Sample only 10 boundary pixels for speed
                std::vector<sf::Vector2i> quickSample;
                auto it = m_boundaryPixels.begin();
                for (int i = 0; i < 10 && it != m_boundaryPixels.end(); ++i) {
                    quickSample.push_back(*it);
                    std::advance(it, std::max(1, static_cast<int>(m_boundaryPixels.size()) / 10));
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
                    countryGrid[targetCell.y][targetCell.x] = m_countryIndex;
                    m_boundaryPixels.insert(targetCell);
                    
                    // Fast dirty region marking
                    int regionSize = map.getRegionSize();
                    int regionIndex = static_cast<int>((targetCell.y / regionSize) * (isLandGrid[0].size() / regionSize) + (targetCell.x / regionSize));
                    const_cast<Map&>(map).insertDirtyRegion(regionIndex);
                }
                
                if (!burstTargets.empty()) {
                    std::cout << "⚡ " << m_name << " HYPER-FAST burst: " << burstTargets.size() << " pixels!" << std::endl;
                }
            }
        }
    }
    
    // Simplified city founding - every 20 years
    if (yearIndex % 20 == 0 && m_population >= 10000 && canFoundCity() && !m_boundaryPixels.empty()) {
        auto it = m_boundaryPixels.begin();
        std::advance(it, gen() % m_boundaryPixels.size());
        foundCity(*it, news);
        m_hasCity = true;
    }
    
    // Update gold from cities
    m_gold += m_cities.size() * 5.0; // 5x rate for fast forward
    
    // 🏛️ FAST FORWARD IDEOLOGY CHANGES - Every 10 years
    if (yearIndex % 10 == 0) {
        checkIdeologyChange(currentYear, news);
    }
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
            m_populationGrowthBonus += 0.003; // +0.3% growth rate
            m_maxSizeMultiplier += 0.2; // +20% max territory size
            break;
        case 20: // Agriculture
            m_populationGrowthBonus += 0.005; // +0.5% growth rate
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
        case 38: // Universities
            m_sciencePointsBonus += 15.5; // +15.5 science points per year (organized learning + education)
            m_maxSizeMultiplier += 0.30; // +30% max territory size (educated population)
            break;
        case 39: // Astronomy
            m_sciencePointsBonus += 20.0; // +20.0 science points per year (scientific observation)
            break;
        case 48: // Scientific Method
            m_sciencePointsBonus += 50.0; // +50.0 science points per year (RESEARCH REVOLUTION!)
            break;
        case 54: // Electricity
            m_sciencePointsBonus += 30.0; // +30.0 science points per year (electrical age)
            break;
        case 69: // Computers
            m_sciencePointsBonus += 100.0; // +100.0 science points per year (COMPUTATIONAL REVOLUTION!)
            break;
        case 76: // Integrated Circuit
            m_sciencePointsBonus += 75.0; // +75.0 science points per year (microprocessor age)
            break;
        case 79: // Internet
            m_sciencePointsBonus += 200.0; // +200.0 science points per year (INFORMATION EXPLOSION!)
            break;
        case 80: // Personal Computers
            m_sciencePointsBonus += 150.0; // +150.0 science points per year (computing for all)
            break;
        case 85: // Artificial Intelligence
            m_sciencePointsBonus += 300.0; // +300.0 science points per year (AI BREAKTHROUGH!)
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
        case 42: // Navigation
            m_maxSizeMultiplier += 1.5; // +150% max territory size (MASSIVE COLONIAL EMPIRES!)
            m_expansionRateBonus += 50; // +50 pixels per year (rapid colonization)
            m_burstExpansionRadius = 5; // Expand 5 pixels outward in bursts (MASSIVE WAVES!)
            m_burstExpansionFrequency = 5; // Burst expansion every 5 years
            break;
            
        // 💰 ECONOMIC EXPANSION TECHNOLOGIES
        case 34: // Banking
            m_maxSizeMultiplier += 0.80; // +80% max territory size (economic expansion)
            m_expansionRateBonus += 25; // +25 pixels per year (funded expansion)
            break;
        case 44: // Economics
            m_maxSizeMultiplier += 1.2; // +120% max territory size (economic empires)
            m_expansionRateBonus += 35; // +35 pixels per year (economic efficiency)
            break;
        case 35: // Printing
            m_maxSizeMultiplier += 0.60; // +60% max territory size (information spread)
            m_expansionRateBonus += 15; // +15 pixels per year (administrative efficiency)
            m_sciencePointsBonus += 0.3; // +0.3 science points per year (knowledge spread)
            break;
            
        // 🚂 INDUSTRIAL EXPANSION
        case 55: // Railroad
            m_maxSizeMultiplier += 2.0; // +200% max territory size (CONTINENTAL EMPIRES!)
            m_expansionRateBonus += 100; // +100 pixels per year (RAPID RAIL EXPANSION!)
            m_burstExpansionRadius = 8; // Expand 8 pixels outward in bursts (RAILROAD NETWORKS!)
            m_burstExpansionFrequency = 3; // Burst expansion every 3 years
            break;
            
        // ⚔️ MEDIEVAL MILITARY TECHNOLOGIES
        case 28: // Steel
            m_militaryStrengthBonus += 0.50; // +50% military strength
            m_defensiveBonus += 0.40; // +40% defensive bonus
            m_territoryCaptureBonusRate += 0.25; // +25% capture rate
            m_warBurstConquestRadius = 3; // Capture 3 pixels deep in war bursts
            m_warBurstConquestFrequency = 8; // War burst every 8 years
            break;
        case 36: // Gunpowder
            m_militaryStrengthBonus += 0.75; // +75% military strength (revolutionary!)
            m_territoryCaptureBonusRate += 0.50; // +50% capture rate
            m_warDurationReduction += 0.30; // -30% war duration (decisive battles)
            m_warBurstConquestRadius = 5; // Capture 5 pixels deep in war bursts (CANNON BREAKTHROUGHS!)
            m_warBurstConquestFrequency = 5; // War burst every 5 years
            break;
            
        // 🔫 INDUSTRIAL MILITARY TECHNOLOGIES
        case 46: // Firearms
            m_militaryStrengthBonus += 0.60; // +60% military strength
            m_territoryCaptureBonusRate += 0.40; // +40% capture rate
            m_warDurationReduction += 0.25; // -25% war duration
            m_warBurstConquestRadius = 4; // Capture 4 pixels deep (rifle advances)
            m_warBurstConquestFrequency = 6; // War burst every 6 years
            break;
        case 49: // Rifling
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
        case 52: // Sanitation
            m_plagueResistanceBonus += 0.30; // 30% plague resistance
            m_populationGrowthBonus += 0.002; // +0.2% growth from better health
            break;
        case 53: // Vaccination
            m_plagueResistanceBonus += 0.50; // 50% plague resistance
            m_populationGrowthBonus += 0.003; // +0.3% growth from disease prevention
            break;
        case 65: // Penicillin
            m_plagueResistanceBonus += 0.60; // 60% plague resistance
            m_populationGrowthBonus += 0.004; // +0.4% growth from antibiotics
            break;
            
        // 🥶 FOOD/PRESERVATION TECHNOLOGIES
        case 71: // Refrigeration
            m_populationGrowthBonus += 0.006; // +0.6% growth from food preservation
            break;
            
        // 🔬 ADVANCED TECHNOLOGIES
        case 81: // Genetic Engineering
            m_populationGrowthBonus += 0.008; // +0.8% growth from genetic improvements
            m_plagueResistanceBonus += 0.40; // 40% additional plague resistance
            m_militaryStrengthBonus += 0.30; // +30% military strength (enhanced soldiers)
            break;
        case 90: // Biotechnology
            m_populationGrowthBonus += 0.010; // +1.0% growth from biotech advances
            m_plagueResistanceBonus += 0.50; // 50% additional plague resistance
            m_militaryStrengthBonus += 0.25; // +25% military strength (bio-enhancements)
            break;
    }
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
    return 1.0 + m_sciencePointsBonus; // Base 1.0 + technology bonuses
}

// 🚀 OPTIMIZED KNOWLEDGE DIFFUSION - Cache neighbors and update less frequently
double Country::calculateNeighborScienceBonus(const std::vector<Country>& allCountries, const Map& map, const TechnologyManager& techManager, int currentYear) const {
    
    // 🚀 STAGGERED PERFORMANCE: Only recalculate neighbors every 20-80 years (random per country) or when territories change
    bool needsRecalculation = (currentYear - m_neighborBonusLastUpdated >= m_neighborRecalculationInterval) || m_cachedNeighborIndices.empty();
    
    if (needsRecalculation) {
        m_cachedNeighborIndices.clear();
        
        // Find all neighbors (this is the expensive O(n²) part, but now only every 20-80 years per country)
    for (const auto& otherCountry : allCountries) {
        if (otherCountry.getCountryIndex() == m_countryIndex) continue; // Skip self
        
            // Check if they're neighbors - only do this expensive check during recalculation
        if (map.areNeighbors(*this, otherCountry)) {
                m_cachedNeighborIndices.push_back(otherCountry.getCountryIndex());
            }
        }
        m_neighborBonusLastUpdated = currentYear;
        
        // 🚀 RANDOM INTERVAL: Generate new random interval for next recalculation (keeps it staggered)
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> intervalDist(20, 80);
        m_neighborRecalculationInterval = intervalDist(gen);
    }
    
    // Now calculate bonus using cached neighbor list (much faster!)
    double totalBonus = 0.0;
    for (int neighborIndex : m_cachedNeighborIndices) {
        // Safety check - make sure neighbor still exists
        if (neighborIndex >= 0 && neighborIndex < static_cast<int>(allCountries.size())) {
            const Country& neighbor = allCountries[neighborIndex];
            
            // Calculate science bonus based on neighbor's science type
            double neighborBonus = 0.0;
            
            switch(neighbor.getScienceType()) {
                case ScienceType::MS: // More Science neighbors provide small bonus
                    neighborBonus = 0.02; // Reduced from 0.12 to 0.02 for realism
                    break;
                case ScienceType::NS: // Normal Science neighbors provide tiny bonus
                    neighborBonus = 0.01; // Reduced from 0.06 to 0.01 for realism
                    break;
                case ScienceType::LS: // Less Science neighbors provide no bonus
                    neighborBonus = 0.0;
                    break;
            }
            
            // 🚀 OPTIMIZATION: Simplified tech multiplier - just use a rough estimate
            // Instead of expensive lookup, use a simpler calculation based on science type
            double techMultiplier = 1.0;
            if (neighbor.getScienceType() == ScienceType::MS) {
                techMultiplier = 1.1; // Reduced from 1.5 to 1.1 for realism
            } else if (neighbor.getScienceType() == ScienceType::NS) {
                techMultiplier = 1.05; // Reduced from 1.2 to 1.05 for realism
            }
            // LS countries get 1.0 (no bonus)
            
            neighborBonus *= techMultiplier;
            totalBonus += neighborBonus;
        }
    }
    
    // Cap the total neighbor bonus to prevent runaway effects - REALISTIC REBALANCE
    totalBonus = std::min(totalBonus, 0.2); // Reduced from 1.5 to 0.2 for realism
    
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

void Country::checkIdeologyChange(int currentYear, News& news) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    
    // Check for ideology changes every 25 years
    if (currentYear % 25 != 0) return;
    
    // Must have sufficient population and development for ideology changes
    if (m_population < 5000) return;
    
    std::vector<Ideology> possibleIdeologies;
    
    // Determine possible ideology transitions based on current ideology
    switch(m_ideology) {
        case Ideology::Tribal:
            if (m_population > 10000) possibleIdeologies.push_back(Ideology::Chiefdom);
            if (m_hasCity) possibleIdeologies.push_back(Ideology::CityState);
            break;
        case Ideology::Chiefdom:
            if (m_population > 25000) possibleIdeologies.push_back(Ideology::Kingdom);
            if (m_culturePoints > 100) possibleIdeologies.push_back(Ideology::Republic);
            break;
        case Ideology::Kingdom:
            if (m_boundaryPixels.size() > 1000) possibleIdeologies.push_back(Ideology::Empire);
            if (m_culturePoints > 500) possibleIdeologies.push_back(Ideology::Democracy);
            if (m_type == Type::Warmonger) possibleIdeologies.push_back(Ideology::Dictatorship);
            break;
        case Ideology::Empire:
            if (m_culturePoints > 1000) possibleIdeologies.push_back(Ideology::Democracy);
            possibleIdeologies.push_back(Ideology::Dictatorship);
            if (m_boundaryPixels.size() > 5000) possibleIdeologies.push_back(Ideology::Federation);
            break;
        case Ideology::Republic:
            if (m_culturePoints > 800) possibleIdeologies.push_back(Ideology::Democracy);
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

void Country::absorbCountry(Country& target, std::vector<std::vector<int>>& countryGrid, News& news) {
    std::cout << "🗡️💀 " << m_name << " COMPLETELY ANNIHILATES " << target.getName() << "!" << std::endl;
    
    // Absorb all territory
    const auto& targetPixels = target.getBoundaryPixels();
    for (const auto& pixel : targetPixels) {
        countryGrid[pixel.y][pixel.x] = m_countryIndex;
        m_boundaryPixels.insert(pixel);
    }
    
    // Absorb population (with some losses from conquest)
    long long absorbedPopulation = static_cast<long long>(target.getPopulation() * 0.7); // 30% casualties
    m_population += absorbedPopulation;
    
    // Absorb cities
    const auto& targetCities = target.getCities();
    for (const auto& city : targetCities) {
        m_cities.push_back(city);
    }
    
    // Absorb resources and gold
    m_gold += target.getGold() * 0.8; // 80% of target's gold
    
    // Major historical event
    news.addEvent("🗡️💀 ANNIHILATION: " + m_name + " completely destroys " + target.getName() + 
                  " and absorbs " + std::to_string(absorbedPopulation) + " people!");
    
    // Set target population to 0 to mark for removal
    target.setPopulation(0);
    
    std::cout << "   📊 Absorbed " << absorbedPopulation << " people and " << targetPixels.size() << " territory!" << std::endl;
}

// Found a new city at a specific location
void Country::foundCity(const sf::Vector2i& location, News& news) {
    m_cities.emplace_back(location);
    news.addEvent(m_name + " has built a city!");
}

// Check if the country can found a new city
bool Country::canFoundCity() const {
    return m_cities.empty();
}

// Get the list of cities
const std::vector<City>& Country::getCities() const {
    return m_cities;
}

// Get the current gold amount
double Country::getGold() const {
    return m_gold;
}

// Get the country type
Country::Type Country::getType() const {
    return m_type;
}

// Get the science type
Country::ScienceType Country::getScienceType() const {
    return m_scienceType;
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

// Get the culture type
Country::CultureType Country::getCultureType() const {
    return m_cultureType;
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

double Country::getCulturePoints() const {
    return m_culturePoints;
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