// country.cpp

#include "country.h"
#include "map.h"
#include <random>
#include <algorithm>
#include <iostream>
#include <limits>

// Constructor
Country::Country(int countryIndex, const sf::Color & color, const sf::Vector2i & startCell, long long initialPopulation, double growthRate, const std::string & name, Type type, ScienceType scienceType) :
    m_countryIndex(countryIndex),
    m_color(color),
    m_population(initialPopulation),
    m_populationGrowthRate(growthRate),
    m_name(name),
    m_type(type),
    m_scienceType(scienceType),
    m_hasCity(false),
    m_gold(0.0),
    m_militaryStrength(0.0), // Initialize m_militaryStrength here
    m_isAtWar(false),
    m_warDuration(0),
    m_isWarofConquest(false),
    m_nextWarCheckYear(-5001),
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
    if (m_type == Type::Pacifist) {
        // 10% chance to be able to expand to 50% of the limit
        if (rand() % 10 == 0) { // 10% chance
            return static_cast<int>(baseLimit * 0.5);
        }
        else {
            return static_cast<int>(baseLimit * 0.1);
        }
    }
    else if (m_type == Type::Warmonger) {
        return static_cast<int>(baseLimit * 10);
    }
    else { // Trader
        return baseLimit;
    }
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
        m_warDuration = 1 + rand() % 100; // Random duration between 1 and 100 years
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

// Update the country's state each year
void Country::update(const std::vector<std::vector<bool>>& isLandGrid, std::vector<std::vector<int>>& countryGrid, std::mutex& gridMutex, int gridCellSize, int regionSize, std::unordered_set<int>& dirtyRegions, int currentYear, const std::vector<std::vector<std::unordered_map<Resource::Type, double>>>& resourceGrid, News& news, bool plagueActive, long long& plagueDeaths, const Map& map) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> growthDist(10, 25);
    int growth = growthDist(gen);

    // Science point generation
    double sciencePointIncrease = 1.0; // Base science point per year

    // Apply multipliers based on ScienceType
    if (m_scienceType == ScienceType::MS) {
        // MS countries get 1.1x to 2x science points
        sciencePointIncrease *= (1.1 + (static_cast<double>(rand()) / RAND_MAX) * (2.0 - 1.1));
    }
    else if (m_scienceType == ScienceType::LS) {
        // LS countries get 0.1x to 0.35x science points
        sciencePointIncrease *= (0.1 + (static_cast<double>(rand()) / RAND_MAX) * (0.35 - 0.1));
    }

    addSciencePoints(sciencePointIncrease);

    // Get the maximum expansion pixels for the current year
    int maxExpansionPixels = getMaxExpansionPixels(currentYear);

    // 10% chance to exceed the limit
    std::uniform_real_distribution<> chance(0.0, 1.0);
    if (chance(gen) < 0.1) {
        std::uniform_int_distribution<> extraPixels(40, 90);
        maxExpansionPixels += extraPixels(gen);
    }

    // Adjust growth based on the maximum allowed for the current year
    size_t countrySize = 0;
    for (size_t y = 0; y < countryGrid.size(); ++y) {
        for (size_t x = 0; x < countryGrid[y].size(); ++x) {
            if (countryGrid[y][x] == m_countryIndex) {
                countrySize++;
            }
        }
    }

    if (countrySize + growth > maxExpansionPixels) {
        growth = std::max(0, static_cast<int>(maxExpansionPixels - countrySize));
    }

    std::uniform_int_distribution<> neighborDist(-1, 1);

    std::vector<sf::Vector2i> newBoundaryPixels;
    std::vector<std::vector<int>> tempGrid = countryGrid; // Create tempGrid, initialized with current countryGrid

    std::vector<sf::Vector2i> currentBoundaryPixels(m_boundaryPixels.begin(), m_boundaryPixels.end());

    // Warmonger war multiplier (you can adjust this value)
    const double warmongerWarMultiplier = 2.0;

    if (isAtWar()) {
        // Wartime expansion (only into enemy territory)

        // Apply Warmonger multiplier
        if (m_type == Type::Warmonger) {
            growth = static_cast<int>(growth * warmongerWarMultiplier);
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
                // Check if the new cell belongs to an enemy
                bool isEnemyTerritory = false;
                for (int enemyIndex : enemyIndices) {
                    if (tempGrid[newCell.y][newCell.x] == enemyIndex) {
                        isEnemyTerritory = true;
                        break;
                    }
                }

                if (isEnemyTerritory) {
                    // Capture the cell (existing logic)
                    // Check for successful capture based on military strength
                    std::uniform_real_distribution<> captureChance(0.0, 1.0);
                    // Increased capture chance for Warmongers during war
                    double warmongerCaptureBonus = (m_type == Type::Warmonger) ? 0.2 : 0.0; // 20% bonus for Warmongers
                    if (captureChance(gen) < 0.6 + warmongerCaptureBonus) { // 60% chance of capturing
                        // Capture the cell
                        tempGrid[newCell.y][newCell.x] = m_countryIndex;
                        newBoundaryPixels.push_back(newCell);



                        // Reduce population of the defending country
                        for (Country* enemy : getEnemies()) {
                            if (countryGrid[newCell.y][newCell.x] == enemy->getCountryIndex()) {
                                double randomFactor = static_cast<double>(rand() % 101) / 100.0; // 0.00 to 1.00
                                long long populationLoss = static_cast<long long>(enemy->getPopulation() * (0.01 + (0.02 * randomFactor))); // 1-3% loss
                                enemy->setPopulation(std::max(0LL, enemy->getPopulation() - populationLoss));

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

                        int regionIndex = static_cast<int>((newCell.y / regionSize) * (isLandGrid[0].size() / regionSize) + (newCell.x / regionSize));
                        {
                            std::lock_guard<std::mutex> lock(gridMutex);
                            dirtyRegions.insert(regionIndex);
                        }

                        bool isNewBoundary = false;
                        for (int y = -1; y <= 1; ++y) {
                            for (int x = -1; x <= 1; ++x) {
                                if (x == 0 && y == 0) continue;
                                sf::Vector2i neighborCell = newCell + sf::Vector2i(x, y);
                                if (neighborCell.x >= 0 && neighborCell.x < tempGrid[0].size() && neighborCell.y >= 0 && neighborCell.y < tempGrid.size() && tempGrid[neighborCell.y][neighborCell.x] == -1) {
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
    }
    else {
        // Peacetime expansion (normal expansion for all countries)
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
                if (tempGrid[newCell.y][newCell.x] == -1 && isLandGrid[newCell.y][newCell.x]) {
                    tempGrid[newCell.y][newCell.x] = m_countryIndex;
                    newBoundaryPixels.push_back(newCell);

                    int regionIndex = static_cast<int>((newCell.y / regionSize) * (isLandGrid[0].size() / regionSize) + (newCell.x / regionSize));
                    {
                        std::lock_guard<std::mutex> lock(gridMutex);
                        dirtyRegions.insert(regionIndex);
                    }

                    bool isNewBoundary = false;
                    for (int y = -1; y <= 1; ++y) {
                        for (int x = -1; x <= 1; ++x) {
                            if (x == 0 && y == 0) continue;
                            sf::Vector2i neighborCell = newCell + sf::Vector2i(x, y);
                            if (neighborCell.x >= 0 && neighborCell.x < tempGrid[0].size() && neighborCell.y >= 0 && neighborCell.y < tempGrid.size() && tempGrid[neighborCell.y][neighborCell.x] == -1) {
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

    {
        std::lock_guard<std::mutex> lock(gridMutex);
        // **Corrected Grid Update:** Copy ALL changes from tempGrid to countryGrid
        for (size_t y = 0; y < tempGrid.size(); ++y) {
            for (size_t x = 0; x < tempGrid[y].size(); ++x) {
                countryGrid[y][x] = tempGrid[y][x];
            }
        }
    }

    // Resource Management
    double foodConsumption = m_population * 0.001;
    double foodAvailable = 0.0;

    for (size_t y = 0; y < countryGrid.size(); ++y) {
        for (size_t x = 0; x < countryGrid[y].size(); ++x) {
            if (countryGrid[y][x] == m_countryIndex) {
                // Use .at() to access elements of the const unordered_map
                foodAvailable += resourceGrid[y][x].at(Resource::Type::FOOD);
                for (const auto& [type, amount] : resourceGrid[y][x]) {
                    if (type != Resource::Type::FOOD) {
                        m_resourceManager.addResource(type, amount);
                    }
                }
            }
        }
    }

    if (foodAvailable >= foodConsumption) {
        m_resourceManager.consumeResource(Resource::Type::FOOD, foodConsumption);
        if (!plagueActive)
        {
            m_population += static_cast<long long>(m_population * m_populationGrowthRate);
        }
        else
        {
            m_population += static_cast<long long>(m_population * (m_populationGrowthRate / 10));
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

        // Kill 8% of the population each year
        long long deaths = static_cast<long long>(m_population * 0.08);
        m_population -= deaths;
        if (m_population < 0) m_population = 0;

        // Update the total death toll
        plagueDeaths += deaths;
    }
    // City foundation logic
    if (m_population >= 10000 && canFoundCity()) {
        // Find a suitable location for a city (a random tile within the country's borders)
        std::vector<sf::Vector2i> potentialCityLocations;
        for (size_t y = 0; y < countryGrid.size(); ++y) {
            for (size_t x = 0; x < countryGrid[y].size(); ++x) {
                if (countryGrid[y][x] == m_countryIndex) {
                    potentialCityLocations.emplace_back(x, y);
                }
            }
        }

        if (!potentialCityLocations.empty()) {
            std::uniform_int_distribution<size_t> cityLocationDist(0, potentialCityLocations.size() - 1);
            size_t cityLocationIndex = cityLocationDist(gen);
            sf::Vector2i cityLocation = potentialCityLocations[cityLocationIndex];

            // Create the city
            foundCity(cityLocation, news);
            m_hasCity = true;
        }
    }

    // City management (add gold for each city)
    for (const auto& city : m_cities) {
        m_gold += 1.0; // Each city produces 1 gold
    }

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

// Get the military strength
double Country::getMilitaryStrength() const {
    return m_militaryStrength;
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