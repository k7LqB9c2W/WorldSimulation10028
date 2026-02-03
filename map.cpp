#include "map.h"
#include "technology.h"
#include "culture.h"
#include "great_people.h"
#include "trade.h"
#include <random>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <limits>
#include <queue>
#include <unordered_set>
#include <cmath>
#include <algorithm>

Map::Map(const sf::Image& baseImage, const sf::Image& resourceImage, int gridCellSize, const sf::Color& landColor, const sf::Color& waterColor, int regionSize) :
    m_gridCellSize(gridCellSize),
    m_regionSize(regionSize),
    m_landColor(landColor),
    m_waterColor(waterColor),
    m_baseImage(baseImage),
    m_resourceImage(resourceImage),
    m_plagueActive(false),
    m_plagueStartYear(0),
    m_plagueDeathToll(0),
    m_plagueAffectedCountries()
{

    m_countryGrid.resize(baseImage.getSize().y / gridCellSize, std::vector<int>(baseImage.getSize().x / gridCellSize, -1));

    m_isLandGrid.resize(baseImage.getSize().y / gridCellSize);
    for (size_t y = 0; y < m_isLandGrid.size(); ++y) {
        m_isLandGrid[y].resize(baseImage.getSize().x / gridCellSize);
        for (size_t x = 0; x < m_isLandGrid[y].size(); ++x) {
            sf::Vector2u pixelPos(static_cast<unsigned int>(x * gridCellSize), static_cast<unsigned int>(y * gridCellSize));
            m_isLandGrid[y][x] = (baseImage.getPixel(pixelPos.x, pixelPos.y) == landColor);
        }
    }

    m_resourceGrid.resize(baseImage.getSize().y / gridCellSize, std::vector<std::unordered_map<Resource::Type, double>>(baseImage.getSize().x / gridCellSize));

    m_resourceColors = {
        {sf::Color(242, 227, 21), Resource::Type::GOLD},
        {sf::Color(0, 0, 0), Resource::Type::IRON},
        {sf::Color(178, 0, 255), Resource::Type::SALT},
        {sf::Color(127, 0, 55), Resource::Type::HORSES}
    };

    initializeResourceGrid();
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> plagueIntervalDist(600, 700);
    m_plagueInterval = plagueIntervalDist(gen);
    m_nextPlagueYear = -5000 + m_plagueInterval; // First plague year
}

// 🔥 NUCLEAR OPTIMIZATION: Lightning-fast resource grid initialization
void Map::initializeResourceGrid() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> resourceDist(0.2, 2.0);
    std::uniform_real_distribution<> hotspotMultiplier(2.0, 6.0);

    std::cout << "🚀 INITIALIZING RESOURCES (Optimized)..." << std::endl;
    auto start = std::chrono::high_resolution_clock::now();

    // OPTIMIZATION 1: Use OpenMP parallel processing
    #pragma omp parallel for
    for (int y = 0; y < static_cast<int>(m_isLandGrid.size()); ++y) {
        // Each thread gets its own random generator to avoid conflicts
        std::mt19937 localGen(rd() + y);
        std::uniform_real_distribution<> localResourceDist(0.2, 2.0);
        std::uniform_real_distribution<> localHotspotMultiplier(2.0, 6.0);
        
        for (size_t x = 0; x < m_isLandGrid[y].size(); ++x) {
            if (m_isLandGrid[y][x]) {
                // OPTIMIZATION 2: Simplified food calculation (no nested loops)
                // Check only immediate neighbors for water (much faster)
                double foodAmount = 51.2; // Inland food (supports 61,440 people)
                
                // Quick water proximity check (only 8 directions, not 49 pixels!)
                const int dx[] = {-1, -1, -1, 0, 0, 1, 1, 1};
                const int dy[] = {-1, 0, 1, -1, 1, -1, 0, 1};
                
                for (int i = 0; i < 8; ++i) {
                    int nx = static_cast<int>(x) + dx[i];
                    int ny = y + dy[i];
                    if (nx >= 0 && nx < static_cast<int>(m_isLandGrid[0].size()) && 
                        ny >= 0 && ny < static_cast<int>(m_isLandGrid.size())) {
                        if (!m_isLandGrid[ny][nx]) { // Found water
                            foodAmount = 102.4; // Coastal bonus (supports 122,880 people)
                            break;
                        }
                    }
                }
                
                m_resourceGrid[y][x][Resource::Type::FOOD] = foodAmount;

                // OPTIMIZATION 3: Batch process special resources
                sf::Vector2u pixelPos(static_cast<unsigned int>(x * m_gridCellSize), static_cast<unsigned int>(y * m_gridCellSize));
                sf::Color resourcePixelColor = m_resourceImage.getPixel(pixelPos.x, pixelPos.y);

                // Only process if the pixel is not fully transparent
                if (resourcePixelColor.a > 0) {
                    for (const auto& [color, type] : m_resourceColors) {
                        if (resourcePixelColor == color) {
                            double baseAmount = localResourceDist(localGen);
                            baseAmount *= localHotspotMultiplier(localGen);
                            m_resourceGrid[y][x][type] = baseAmount;
                            break;
                        }
                    }
                }
            }
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "✅ RESOURCES INITIALIZED in " << duration.count() << " ms" << std::endl;
}

// Place the function definition in the .cpp file:
std::string generate_country_name() {
    std::vector<std::string> prefixes = { "", "New ", "Old ", "Great ", "North ", "South " };
    std::vector<std::string> syllables = { "na", "mar", "sol", "lin", "ter", "gar", "bel", "kin", "ran", "dus", "zen", "rom", "lor", "via", "qui" };
    std::vector<std::string> suffixes = { "", "ia", "land", "stan", "grad" };

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> numSyllablesDist(2, 3);
    std::uniform_int_distribution<> syllableIndexDist(0, syllables.size() - 1);
    std::uniform_int_distribution<> prefixIndexDist(0, prefixes.size() - 1);
    std::uniform_int_distribution<> suffixIndexDist(0, suffixes.size() - 1);

    std::string name = prefixes[prefixIndexDist(gen)];
    int numSyllables = numSyllablesDist(gen);
    for (int i = 0; i < numSyllables; ++i) {
        name += syllables[syllableIndexDist(gen)];
    }
    name += suffixes[suffixIndexDist(gen)];

    if (!name.empty()) {
        name[0] = std::toupper(name[0]);
    }

    return name; // Now returns only the generated name
}

// 🛣️ ROAD BUILDING SUPPORT - Check if a pixel is valid for road construction
bool Map::isValidRoadPixel(int x, int y) const {
    // Check bounds
    if (x < 0 || x >= static_cast<int>(m_isLandGrid[0].size()) || 
        y < 0 || y >= static_cast<int>(m_isLandGrid.size())) {
        return false;
    }
    
    // Roads can be built on land
    return m_isLandGrid[y][x];
}

bool Map::loadSpawnZones(const std::string& filename) {
    if (!m_spawnZoneImage.loadFromFile(filename)) {
        std::cerr << "Error: Could not load spawn zone image: " << filename << std::endl;
        return false;
    }
    return true;
}

sf::Vector2i Map::getRandomCellInPreferredZones(std::mt19937& gen) {
    std::uniform_int_distribution<> xDist(0, m_spawnZoneImage.getSize().x - 1);
    std::uniform_int_distribution<> yDist(0, m_spawnZoneImage.getSize().y - 1);

    while (true) {
        int x = xDist(gen);
        int y = yDist(gen);

        if (m_spawnZoneImage.getPixel(x, y) == m_spawnZoneColor && m_isLandGrid[y][x]) {
            return sf::Vector2i(x, y);
        }
    }
}

void Map::initializeCountries(std::vector<Country>& countries, int numCountries) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> xDist(0, static_cast<int>(m_isLandGrid[0].size() - 1));
    std::uniform_int_distribution<> yDist(0, static_cast<int>(m_isLandGrid.size() - 1));
    std::uniform_int_distribution<> colorDist(50, 255);
    std::uniform_int_distribution<> popDist(50000, 500000); // Realistic 5000 BCE populations
    std::uniform_real_distribution<> growthRateDist(0.0003, 0.001); // Legacy - not used in logistic system
    std::uniform_real_distribution<> spawnDist(0.0, 1.0);

    std::uniform_int_distribution<> typeDist(0, 2);
    std::discrete_distribution<> scienceTypeDist({ 50, 40, 10 });
    std::discrete_distribution<> cultureTypeDist({ 40, 40, 20 }); // 40% NC, 40% LC, 20% MC

    for (int i = 0; i < numCountries; ++i) {
        sf::Vector2i startCell;
        double spawnRoll = spawnDist(gen);

        if (spawnRoll < 0.75) {
            // Attempt to spawn in a preferred zone
            startCell = getRandomCellInPreferredZones(gen);
        }
        else {
            // 25% chance: Random spawn anywhere on land
            do {
                startCell.x = xDist(gen);
                startCell.y = yDist(gen);
            } while (!m_isLandGrid[startCell.y][startCell.x]);
        }

        sf::Color countryColor(colorDist(gen), colorDist(gen), colorDist(gen));
        long long initialPopulation = popDist(gen);
        double growthRate = growthRateDist(gen);

        std::string countryName = generate_country_name();
        while (isNameTaken(countries, countryName)) {
            countryName = generate_country_name();
        }

        countryName += " Tribe";

        Country::Type countryType = static_cast<Country::Type>(typeDist(gen));
        Country::ScienceType scienceType = static_cast<Country::ScienceType>(scienceTypeDist(gen));
        Country::CultureType cultureType = static_cast<Country::CultureType>(cultureTypeDist(gen));
        countries.emplace_back(i, countryColor, startCell, initialPopulation, growthRate, countryName, countryType, scienceType, cultureType);
        m_countryGrid[startCell.y][startCell.x] = i;

        int regionIndex = static_cast<int>(startCell.y / m_regionSize * (m_baseImage.getSize().x / m_gridCellSize / m_regionSize) + startCell.x / m_regionSize);
        m_dirtyRegions.insert(regionIndex);
    }
}

// Add a helper function to check for name uniqueness
bool isNameTaken(const std::vector<Country>& countries, const std::string& name) {
    for (const auto& country : countries) {
        if (country.getName() == name) {
            return true;
        }
    }
    return false;
}

void Map::updateCountries(std::vector<Country>& countries, int currentYear, News& news, TechnologyManager& technologyManager) {
    m_dirtyRegions.clear();

    // Declare rd and gen here, outside of any loops or conditional blocks
    std::random_device rd;
    std::mt19937 gen(rd());

    // Trigger the plague in the year 4950
    if (currentYear == m_nextPlagueYear) {
        startPlague(currentYear, news);
        initializePlagueCluster(countries); // Initialize geographic cluster
    }

    if (m_plagueActive && currentYear > m_plagueStartYear) {
        updatePlagueSpread(countries);
    }

    // Check if the plague should end
    if (m_plagueActive && (currentYear == m_plagueStartYear + 3)) {
        endPlague(news);
    }

    // No need for tempGrid here anymore - tempGrid is handled within Country::update
    // std::vector<std::vector<int>> tempGrid = m_countryGrid; // REMOVE THIS LINE

    // 🛡️ PERFORMANCE FIX: Remove OpenMP to prevent mutex contention and thread blocking
    for (int i = 0; i < countries.size(); ++i) {
        countries[i].update(m_isLandGrid, m_countryGrid, m_gridMutex, m_gridCellSize, m_regionSize, m_dirtyRegions, currentYear, m_resourceGrid, news, m_plagueActive, m_plagueDeathToll, *this, technologyManager, countries);
        // Check for war declarations only if not already at war and it's not before 4950 BCE
        if (currentYear >= -4950 && countries[i].getType() == Country::Type::Warmonger && countries[i].canDeclareWar() && !countries[i].isAtWar() && currentYear >= countries[i].getNextWarCheckYear()) {
            // Check for potential targets among neighboring countries

            // std::cout << "Year " << currentYear << ": " << countries[i].getName() << " (Warmonger) is checking for war." << std::endl; // Debug: War check start

            // 💀 PRIORITIZE ANNIHILATION - Check for weak neighbors first
            int annihilationTarget = -1;
            for (int j = 0; j < countries.size(); ++j) {
                if (i != j && areNeighbors(countries[i], countries[j]) && 
                    countries[i].canAnnihilateCountry(countries[j])) {
                    annihilationTarget = j;
                    break; // Take the first annihilation opportunity
                }
            }
            
            if (annihilationTarget != -1) {
                // 💀 INSTANT ANNIHILATION - 80% chance for overwhelming superiority
                std::uniform_real_distribution<> annihilationChance(0.0, 1.0);
                if (annihilationChance(gen) < 0.8) {
                    std::vector<sf::Vector2i> absorbedCells;
                    {
                        const auto& targetTerritory = countries[annihilationTarget].getBoundaryPixels();
                        absorbedCells.assign(targetTerritory.begin(), targetTerritory.end());
                    }
                    countries[i].startWar(countries[annihilationTarget], news);
                    countries[i].absorbCountry(countries[annihilationTarget], m_countryGrid, news);
                    if (!absorbedCells.empty()) {
                        int regionsPerRow = m_regionSize > 0 ? static_cast<int>(m_countryGrid[0].size()) / m_regionSize : 0;
                        if (regionsPerRow > 0) {
                            for (const auto& cell : absorbedCells) {
                                int regionIndex = (cell.y / m_regionSize) * regionsPerRow + (cell.x / m_regionSize);
                                m_dirtyRegions.insert(regionIndex);
                            }
                        }
                    }
                    
                    std::uniform_int_distribution<> delayDist(10, 30); // Shorter delay after annihilation
                    int delay = delayDist(gen);
                    countries[i].setNextWarCheckYear(currentYear + delay);
                }
            } else {
                // 🗡️ NORMAL WAR LOGIC - No annihilation opportunities
                std::vector<int> potentialTargets;
                for (int j = 0; j < countries.size(); ++j) {
                    if (i != j && areNeighbors(countries[i], countries[j]) && countries[i].getMilitaryStrength() > countries[j].getMilitaryStrength()) {
                        potentialTargets.push_back(j);
                    }
                }
                
                if (potentialTargets.empty()) {
                    std::uniform_int_distribution<> delayDist(25, 75);
                    int delay = delayDist(gen);
                    countries[i].setNextWarCheckYear(currentYear + delay);
                }
                else {
                    // 25% chance to declare war
                    std::uniform_real_distribution<> warChance(0.0, 1.0);
                    if (warChance(gen) < 0.25) {
                        std::uniform_int_distribution<> targetDist(0, potentialTargets.size() - 1);
                        int targetIndex = potentialTargets[targetDist(gen)];

                        // Declare war on the chosen target
                        countries[i].startWar(countries[targetIndex], news);

                    // If a war is declared, also set a cooldown (you might want a different cooldown after a war)
                    std::uniform_int_distribution<> delayDist(25, 75); // Example: 5-10 years cooldown after a war
                    int delay = delayDist(gen);
                    countries[i].setNextWarCheckYear(currentYear + delay); // Use setter here
                }
                else {
                    // War not declared, set a shorter delay
                    // std::cout << "  " << countries[i].getName() << " decided not to declare war this time." << std::endl;
                    std::uniform_int_distribution<> delayDist(25, 75); // Shorter delay
                    int delay = delayDist(gen);
                    countries[i].setNextWarCheckYear(currentYear + delay);
                    }
                }
            }
        }

        // Decrement war duration and peace duration
        if (countries[i].isAtWar()) {
            countries[i].decrementWarDuration();
            if (countries[i].getWarDuration() <= 0) {
                countries[i].endWar(currentYear);
            }
        }
        else {
            countries[i].decrementPeaceDuration();
        }
        
        // TECHNOLOGY SHARING - Trader countries attempt to share technology
        countries[i].attemptTechnologySharing(currentYear, countries, technologyManager, *this, news);
    }

    // Clean up extinct countries without erasing (keeps country indices stable).
    for (int i = 0; i < static_cast<int>(countries.size()); ++i) {
        if (countries[i].getPopulation() <= 0 || countries[i].getBoundaryPixels().empty()) {
            markCountryExtinct(countries, i, currentYear, news);
        }
    }

    // REMOVE THIS ENTIRE BLOCK - Grid update is now handled within Country::update
    /*
    // Update m_countryGrid from tempGrid after processing all countries
    {
        std::lock_guard<std::mutex> lock(m_gridMutex);
        m_countryGrid = tempGrid;
    }
    */
}

void Map::markCountryExtinct(std::vector<Country>& countries, int countryIndex, int currentYear, News& news) {
    if (countryIndex < 0 || countryIndex >= static_cast<int>(countries.size())) {
        return;
    }

    Country& extinct = countries[static_cast<size_t>(countryIndex)];
    if (extinct.getPopulation() <= 0 && extinct.getBoundaryPixels().empty() && !extinct.isAtWar() && extinct.getEnemies().empty()) {
        return; // Already processed
    }
    if (extinct.getPopulation() > 0 && !extinct.getBoundaryPixels().empty()) {
        return;
    }

    const int extinctId = extinct.getCountryIndex();
    if (extinctId < 0) {
        return;
    }

    const auto& territory = extinct.getBoundaryPixels();
    if (!territory.empty()) {
        std::lock_guard<std::mutex> lock(m_gridMutex);
        const int height = static_cast<int>(m_countryGrid.size());
        const int width = height > 0 ? static_cast<int>(m_countryGrid[0].size()) : 0;
        const int regionsPerRow = m_regionSize > 0 ? (width / m_regionSize) : 0;

        for (const auto& cell : territory) {
            if (cell.x < 0 || cell.y < 0 || cell.x >= width || cell.y >= height) {
                continue;
            }
            if (m_countryGrid[cell.y][cell.x] != extinctId) {
                continue;
            }

            m_countryGrid[cell.y][cell.x] = -1;
            if (regionsPerRow > 0) {
                int regionIndex = (cell.y / m_regionSize) * regionsPerRow + (cell.x / m_regionSize);
                m_dirtyRegions.insert(regionIndex);
            }
        }
    }

    // Remove from wars/enemy lists without invalidating pointers.
    for (auto& other : countries) {
        if (&other == &extinct) {
            continue;
        }
        if (!other.getEnemies().empty()) {
            other.removeEnemy(&extinct);
            if (other.isAtWar() && other.getEnemies().empty()) {
                other.clearWarState();
            }
        }
    }

    // Clear local state.
    extinct.clearWarState();
    extinct.clearEnemies();
    extinct.setTerritory(std::unordered_set<sf::Vector2i>{});
    extinct.setCities(std::vector<City>{});
    extinct.clearRoadNetwork();
    extinct.setGold(0.0);
    extinct.setSciencePoints(0.0);
    extinct.setPopulation(0);

    std::string event = "💀 " + extinct.getName() + " collapses and becomes extinct in " + std::to_string(currentYear);
    if (currentYear < 0) event += " BCE";
    else event += " CE";
    news.addEvent(event);
}

void Map::processPoliticalEvents(std::vector<Country>& countries, TradeManager& tradeManager, int currentYear, News& news) {
    if (countries.empty()) {
        return;
    }

    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<> chanceDist(0.0, 1.0);

    const int maxCountries = 400;
    const int minTerritoryCells = 180;
    const long long minPopulation = 40000;
    const int fragmentationCooldown = 150;

    auto trySplitCountry = [&](int countryIndex) -> bool {
        if (countryIndex < 0 || countryIndex >= static_cast<int>(countries.size())) {
            return false;
        }
        if (static_cast<int>(countries.size()) >= maxCountries) {
            return false;
        }
        if (countries.size() + 1 > countries.capacity()) {
            return false;
        }

        const Country& country = countries[countryIndex];
        if (country.getPopulation() < minPopulation) {
            return false;
        }

        const auto& territorySet = country.getBoundaryPixels();
        if (territorySet.size() < static_cast<size_t>(minTerritoryCells)) {
            return false;
        }

        std::vector<sf::Vector2i> territory(territorySet.begin(), territorySet.end());
        std::unordered_set<sf::Vector2i> groupA;
        std::unordered_set<sf::Vector2i> groupB;

        auto attemptSplit = [&](const sf::Vector2i& seedA, const sf::Vector2i& seedB) -> bool {
            std::unordered_map<sf::Vector2i, int> assignment;
            std::queue<sf::Vector2i> frontier;

            assignment[seedA] = 0;
            assignment[seedB] = 1;
            frontier.push(seedA);
            frontier.push(seedB);

            const int dirs[4][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
            while (!frontier.empty()) {
                sf::Vector2i current = frontier.front();
                frontier.pop();
                int label = assignment[current];

                for (const auto& dir : dirs) {
                    sf::Vector2i next(current.x + dir[0], current.y + dir[1]);
                    if (territorySet.count(next) == 0) {
                        continue;
                    }
                    if (assignment.find(next) != assignment.end()) {
                        continue;
                    }
                    assignment[next] = label;
                    frontier.push(next);
                }
            }

            for (const auto& cell : territorySet) {
                if (assignment.find(cell) != assignment.end()) {
                    continue;
                }
                int distA = std::abs(cell.x - seedA.x) + std::abs(cell.y - seedA.y);
                int distB = std::abs(cell.x - seedB.x) + std::abs(cell.y - seedB.y);
                assignment[cell] = (distA <= distB) ? 0 : 1;
            }

            groupA.clear();
            groupB.clear();
            for (const auto& entry : assignment) {
                if (entry.second == 0) {
                    groupA.insert(entry.first);
                } else {
                    groupB.insert(entry.first);
                }
            }

            size_t total = groupA.size() + groupB.size();
            if (total == 0) {
                return false;
            }
            double ratio = static_cast<double>(groupA.size()) / static_cast<double>(total);
            return ratio >= 0.3 && ratio <= 0.7;
        };

        bool splitBuilt = false;
        for (int attempt = 0; attempt < 4 && !splitBuilt; ++attempt) {
            std::uniform_int_distribution<size_t> seedDist(0, territory.size() - 1);
            sf::Vector2i seedA = territory[seedDist(gen)];
            sf::Vector2i seedB = seedA;
            int bestDist = -1;
            for (const auto& cell : territory) {
                int dist = (cell.x - seedA.x) * (cell.x - seedA.x) + (cell.y - seedA.y) * (cell.y - seedA.y);
                if (dist > bestDist) {
                    bestDist = dist;
                    seedB = cell;
                }
            }
            if (seedA == seedB) {
                continue;
            }
            splitBuilt = attemptSplit(seedA, seedB);
        }

        if (!splitBuilt || groupA.empty() || groupB.empty()) {
            return false;
        }

        sf::Vector2i capital = country.getCapitalLocation();
        if (groupB.count(capital) > 0) {
            std::swap(groupA, groupB);
        }

        double ratio = static_cast<double>(groupB.size()) / static_cast<double>(groupA.size() + groupB.size());
        long long totalPop = country.getPopulation();
        long long populationLoss = static_cast<long long>(totalPop * 0.05);
        long long remainingPop = std::max(0LL, totalPop - populationLoss);
        long long newPop = static_cast<long long>(remainingPop * ratio);
        long long oldPop = remainingPop - newPop;

        double totalGold = country.getGold();
        double goldLoss = totalGold * 0.1;
        double remainingGold = std::max(0.0, totalGold - goldLoss);
        double newGold = remainingGold * ratio;
        double oldGold = remainingGold - newGold;

        double totalScience = country.getSciencePoints();
        double newScience = totalScience * ratio;
        double oldScience = totalScience - newScience;

        double totalCulture = country.getCulturePoints();
        double newCulture = totalCulture * ratio;
        double oldCulture = totalCulture - newCulture;

        std::vector<City> oldCities;
        std::vector<City> newCities;
        for (const auto& city : country.getCities()) {
            if (groupB.count(city.getLocation()) > 0) {
                newCities.push_back(city);
            } else {
                oldCities.push_back(city);
            }
        }

        if (newCities.empty() && !groupB.empty()) {
            newCities.emplace_back(*groupB.begin());
        }
        if (oldCities.empty() && !groupA.empty()) {
            oldCities.emplace_back(*groupA.begin());
        }

        sf::Vector2i newStart = newCities.empty() ? *groupB.begin() : newCities.front().getLocation();
        sf::Vector2i oldStart = oldCities.empty() ? *groupA.begin() : oldCities.front().getLocation();

        std::vector<sf::Vector2i> oldRoads;
        std::vector<sf::Vector2i> newRoads;
        for (const auto& road : country.getRoads()) {
            if (groupB.count(road) > 0) {
                newRoads.push_back(road);
            } else if (groupA.count(road) > 0) {
                oldRoads.push_back(road);
            }
        }

        std::vector<sf::Vector2i> oldFactories;
        std::vector<sf::Vector2i> newFactories;
        for (const auto& factory : country.getFactories()) {
            if (groupB.count(factory) > 0) {
                newFactories.push_back(factory);
            } else if (groupA.count(factory) > 0) {
                oldFactories.push_back(factory);
            }
        }

        std::uniform_int_distribution<> colorDist(50, 255);
        sf::Color newColor(colorDist(gen), colorDist(gen), colorDist(gen));

        std::uniform_real_distribution<> growthRateDist(0.0003, 0.001);
        double growthRate = growthRateDist(gen);

        std::string newName = generate_country_name();
        while (isNameTaken(countries, newName)) {
            newName = generate_country_name();
        }
        newName += " Republic";

        int newIndex = static_cast<int>(countries.size());
        Country newCountry(newIndex, newColor, newStart, newPop, growthRate, newName, country.getType(),
                           country.getScienceType(), country.getCultureType());
        newCountry.setIdeology(country.getIdeology());
        newCountry.setStability(0.45);
        newCountry.setFragmentationCooldown(fragmentationCooldown);
        newCountry.setYearsSinceWar(0);
        newCountry.resetStagnation();
        newCountry.setTerritory(groupB);
        newCountry.setCities(newCities);
        newCountry.setRoads(newRoads);
        newCountry.setFactories(newFactories);
        newCountry.setGold(newGold);
        newCountry.setSciencePoints(newScience);
        newCountry.setCulturePoints(newCulture);
        newCountry.setStartingPixel(newStart);

        countries.push_back(newCountry);

        Country& updatedCountry = countries[countryIndex];
        updatedCountry.setPopulation(oldPop);
        updatedCountry.setGold(oldGold);
        updatedCountry.setSciencePoints(oldScience);
        updatedCountry.setCulturePoints(oldCulture);
        updatedCountry.setStability(0.45);
        updatedCountry.setFragmentationCooldown(fragmentationCooldown);
        updatedCountry.setYearsSinceWar(0);
        updatedCountry.resetStagnation();
        updatedCountry.setTerritory(groupA);
        updatedCountry.setCities(oldCities);
        updatedCountry.setRoads(oldRoads);
        updatedCountry.setFactories(oldFactories);
        updatedCountry.setStartingPixel(oldStart);

        const auto& resources = updatedCountry.getResourceManager().getResources();
        for (const auto& entry : resources) {
            Resource::Type type = entry.first;
            double amount = updatedCountry.getResourceManager().getResourceAmount(type);
            double movedAmount = amount * ratio;
            if (movedAmount > 0.0) {
                const_cast<ResourceManager&>(updatedCountry.getResourceManager()).consumeResource(type, movedAmount);
                const_cast<ResourceManager&>(countries[newIndex].getResourceManager()).addResource(type, movedAmount);
            }
        }

        int regionsPerRow = static_cast<int>(m_baseImage.getSize().x) / (m_gridCellSize * m_regionSize);
        {
            std::lock_guard<std::mutex> lock(m_gridMutex);
            for (const auto& cell : groupB) {
                m_countryGrid[cell.y][cell.x] = newIndex;
                int regionIndex = static_cast<int>((cell.y / m_regionSize) * regionsPerRow + (cell.x / m_regionSize));
                m_dirtyRegions.insert(regionIndex);
            }
        }

        news.addEvent("Civil war fractures " + updatedCountry.getName() + " into a new rival state: " + newName + "!");
        return true;
    };

    if (currentYear % 5 == 0) {
        int candidateCount = static_cast<int>(countries.size());
        int splits = 0;
        for (int i = 0; i < candidateCount && splits < 2; ++i) {
            Country& country = countries[i];
            if (country.getPopulation() <= 0) {
                continue;
            }
            if (!country.isFragmentationReady()) {
                continue;
            }
            bool underStress = country.isAtWar() || (m_plagueActive && isCountryAffectedByPlague(i));
            if (!underStress && country.getStability() > 0.15) {
                continue;
            }
            if (chanceDist(gen) < 0.35 && trySplitCountry(i)) {
                splits++;
            }
        }
    }

    if (currentYear % 50 == 0) {
        std::unordered_set<int> unified;
        int unifications = 0;
        int candidateCount = static_cast<int>(countries.size());
        for (int i = 0; i < candidateCount; ++i) {
            if (unified.count(i) > 0) {
                continue;
            }
            Country& a = countries[i];
            if (a.getPopulation() <= 0 || a.isAtWar() || a.getYearsSinceWar() < 50 || a.getStability() < 0.6) {
                continue;
            }

            for (int j = i + 1; j < candidateCount; ++j) {
                if (unified.count(j) > 0) {
                    continue;
                }
                Country& b = countries[j];
                if (b.getPopulation() <= 0 || b.isAtWar() || b.getYearsSinceWar() < 50 || b.getStability() < 0.6) {
                    continue;
                }
                if (!areNeighbors(a, b)) {
                    continue;
                }
                double tradeScore = tradeManager.getTradeScore(a.getCountryIndex(), b.getCountryIndex(), currentYear);
                if (tradeScore < 6.0) {
                    continue;
                }
                if (chanceDist(gen) > 0.25) {
                    continue;
                }

                int leaderIndex = (a.getPopulation() >= b.getPopulation()) ? i : j;
                int absorbedIndex = (leaderIndex == i) ? j : i;
                Country& leader = countries[leaderIndex];
                Country& absorbed = countries[absorbedIndex];

                std::unordered_set<sf::Vector2i> mergedTerritory = leader.getBoundaryPixels();
                for (const auto& cell : absorbed.getBoundaryPixels()) {
                    mergedTerritory.insert(cell);
                }

                std::vector<City> mergedCities = leader.getCities();
                for (const auto& city : absorbed.getCities()) {
                    mergedCities.push_back(city);
                }

                std::vector<sf::Vector2i> mergedRoads = leader.getRoads();
                mergedRoads.insert(mergedRoads.end(), absorbed.getRoads().begin(), absorbed.getRoads().end());

                std::vector<sf::Vector2i> mergedFactories = leader.getFactories();
                mergedFactories.insert(mergedFactories.end(), absorbed.getFactories().begin(), absorbed.getFactories().end());

                leader.setTerritory(mergedTerritory);
                leader.setCities(mergedCities);
                leader.setRoads(mergedRoads);
                leader.setFactories(mergedFactories);

                leader.setPopulation(leader.getPopulation() + absorbed.getPopulation());
                leader.setGold(leader.getGold() + absorbed.getGold());
                leader.setSciencePoints(leader.getSciencePoints() + absorbed.getSciencePoints());
                leader.setCulturePoints(leader.getCulturePoints() + absorbed.getCulturePoints());
                leader.setStability(std::max(leader.getStability(), 0.7));
                leader.setFragmentationCooldown(fragmentationCooldown);

                for (int r = 0; r < 6; ++r) {
                    Resource::Type type = static_cast<Resource::Type>(r);
                    double amount = absorbed.getResourceManager().getResourceAmount(type);
                    if (amount > 0.0) {
                        const_cast<ResourceManager&>(leader.getResourceManager()).addResource(type, amount);
                        const_cast<ResourceManager&>(absorbed.getResourceManager()).consumeResource(type, amount);
                    }
                }

                int regionsPerRow = static_cast<int>(m_baseImage.getSize().x) / (m_gridCellSize * m_regionSize);
                {
                    std::lock_guard<std::mutex> lock(m_gridMutex);
                    for (const auto& cell : absorbed.getBoundaryPixels()) {
                        m_countryGrid[cell.y][cell.x] = leader.getCountryIndex();
                        int regionIndex = static_cast<int>((cell.y / m_regionSize) * regionsPerRow + (cell.x / m_regionSize));
                        m_dirtyRegions.insert(regionIndex);
                    }
                }

                absorbed.setPopulation(0);
                absorbed.setGold(0.0);
                absorbed.setSciencePoints(0.0);
                absorbed.setCulturePoints(0.0);
                absorbed.setStability(0.0);
                absorbed.setTerritory({});
                absorbed.setCities({});
                absorbed.clearRoadNetwork();
                absorbed.setFactories({});

                news.addEvent("Unification: " + leader.getName() + " peacefully integrates " + absorbed.getName() + ".");

                unified.insert(leaderIndex);
                unified.insert(absorbedIndex);
                unifications++;
                if (unifications >= 1) {
                    return;
                }
            }
        }
    }
}

void Map::startPlague(int year, News& news) {
    m_plagueActive = true;
    m_plagueStartYear = year;
    m_plagueDeathToll = 0;
    m_plagueAffectedCountries.clear(); // Clear previous plague
    
    news.addEvent("The Great Plague of " + std::to_string(year) + " has started!");

    // Determine the next plague year
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> plagueIntervalDist(600, 700);
    m_plagueInterval = plagueIntervalDist(gen);
    m_nextPlagueYear = year + m_plagueInterval;
}

void Map::endPlague(News& news) {
    m_plagueActive = false;
    m_plagueAffectedCountries.clear(); // Clear affected countries
    news.addEvent("The Great Plague has ended. Total deaths: " + std::to_string(m_plagueDeathToll));
}

void Map::rebuildCountryAdjacency(const std::vector<Country>& countries) {
    int maxCountryIndex = -1;
    for (const auto& country : countries) {
        maxCountryIndex = std::max(maxCountryIndex, country.getCountryIndex());
    }

    int newSize = maxCountryIndex + 1;
    if (newSize <= 0) {
        m_countryAdjacencySize = 0;
        m_countryAdjacency.clear();
        return;
    }

    if (m_countryAdjacencySize != newSize) {
        m_countryAdjacencySize = newSize;
        m_countryAdjacency.assign(static_cast<size_t>(m_countryAdjacencySize), {});
    } else {
        for (auto& neighbors : m_countryAdjacency) {
            neighbors.clear();
        }
    }

    const int height = static_cast<int>(m_countryGrid.size());
    if (height <= 0) {
        return;
    }
    const int width = static_cast<int>(m_countryGrid[0].size());
    if (width <= 0) {
        return;
    }

    const int wordCount = (m_countryAdjacencySize + 63) / 64;
    std::vector<std::vector<std::uint64_t>> bits(
        static_cast<size_t>(m_countryAdjacencySize),
        std::vector<std::uint64_t>(static_cast<size_t>(wordCount), 0));

    auto addEdge = [&](int a, int b) {
        if (a < 0 || b < 0 || a >= m_countryAdjacencySize || b >= m_countryAdjacencySize || a == b) {
            return;
        }

        int word = b >> 6;
        std::uint64_t mask = 1ull << (b & 63);
        if ((bits[a][static_cast<size_t>(word)] & mask) == 0) {
            bits[a][static_cast<size_t>(word)] |= mask;
            m_countryAdjacency[static_cast<size_t>(a)].push_back(b);
        }

        word = a >> 6;
        mask = 1ull << (a & 63);
        if ((bits[b][static_cast<size_t>(word)] & mask) == 0) {
            bits[b][static_cast<size_t>(word)] |= mask;
            m_countryAdjacency[static_cast<size_t>(b)].push_back(a);
        }
    };

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int owner = m_countryGrid[y][x];
            if (owner < 0 || owner >= m_countryAdjacencySize) {
                continue;
            }

            if (x + 1 < width) {
                addEdge(owner, m_countryGrid[y][x + 1]);
            }
            if (y + 1 < height) {
                addEdge(owner, m_countryGrid[y + 1][x]);
            }
            if (x + 1 < width && y + 1 < height) {
                addEdge(owner, m_countryGrid[y + 1][x + 1]);
            }
            if (x - 1 >= 0 && y + 1 < height) {
                addEdge(owner, m_countryGrid[y + 1][x - 1]);
            }
        }
    }
}

const std::vector<int>& Map::getAdjacentCountryIndices(int countryIndex) const {
    static const std::vector<int> empty;
    if (countryIndex < 0 || countryIndex >= m_countryAdjacencySize) {
        return empty;
    }
    return m_countryAdjacency[static_cast<size_t>(countryIndex)];
}

void Map::initializePlagueCluster(const std::vector<Country>& countries) {
    if (countries.empty()) return;
    
    rebuildCountryAdjacency(countries);

    std::vector<int> countryIndexToVectorIndex(static_cast<size_t>(m_countryAdjacencySize), -1);
    for (size_t i = 0; i < countries.size(); ++i) {
        int idx = countries[i].getCountryIndex();
        if (idx >= 0 && idx < m_countryAdjacencySize) {
            countryIndexToVectorIndex[static_cast<size_t>(idx)] = static_cast<int>(i);
        }
    }

    // Select a random country with neighbors as starting point
    std::random_device rd;
    std::mt19937 gen(rd());
    std::vector<int> potentialStarters;
    
    // Find countries that have neighbors (to avoid isolated countries)
    for (size_t i = 0; i < countries.size(); ++i) {
        if (countries[i].getPopulation() <= 0) continue; // Skip dead countries
        
        // Check if this country has any living neighbor
        bool hasNeighbors = false;
        int countryIndex = countries[i].getCountryIndex();
        for (int neighborCountryIndex : getAdjacentCountryIndices(countryIndex)) {
            if (neighborCountryIndex < 0 || neighborCountryIndex >= m_countryAdjacencySize) {
                continue;
            }
            int neighborVecIndex = countryIndexToVectorIndex[static_cast<size_t>(neighborCountryIndex)];
            if (neighborVecIndex >= 0 && neighborVecIndex < static_cast<int>(countries.size()) &&
                countries[static_cast<size_t>(neighborVecIndex)].getPopulation() > 0) {
                hasNeighbors = true;
                break;
            }
        }
        if (hasNeighbors) {
            potentialStarters.push_back(static_cast<int>(i));
        }
    }
    
    if (potentialStarters.empty()) return; // No connected countries
    
    // Select random starting country
    std::uniform_int_distribution<> starterDist(0, static_cast<int>(potentialStarters.size() - 1));
    int startCountry = potentialStarters[starterDist(gen)];
    
    // Start BFS to build connected cluster
    std::queue<int> toProcess;
    std::unordered_set<int> visited;
    
    toProcess.push(startCountry);
    visited.insert(startCountry);
    m_plagueAffectedCountries.insert(startCountry);
    
    // Spread to neighbors with 70% probability each
    std::uniform_real_distribution<> spreadDist(0.0, 1.0);
    
    while (!toProcess.empty()) {
        int currentCountry = toProcess.front();
        toProcess.pop();
        
        int currentCountryIndex = countries[static_cast<size_t>(currentCountry)].getCountryIndex();
        for (int neighborCountryIndex : getAdjacentCountryIndices(currentCountryIndex)) {
            if (neighborCountryIndex < 0 || neighborCountryIndex >= m_countryAdjacencySize) {
                continue;
            }

            int neighborVecIndex = countryIndexToVectorIndex[static_cast<size_t>(neighborCountryIndex)];
            if (neighborVecIndex < 0 || neighborVecIndex >= static_cast<int>(countries.size())) {
                continue;
            }
            if (visited.count(neighborVecIndex) || countries[static_cast<size_t>(neighborVecIndex)].getPopulation() <= 0) {
                continue;
            }

            if (spreadDist(gen) < 0.7) {
                visited.insert(neighborVecIndex);
                m_plagueAffectedCountries.insert(neighborVecIndex);
                toProcess.push(neighborVecIndex);
            }
        }
    }
}

void Map::updatePlagueSpread(const std::vector<Country>& countries) {
    if (!m_plagueActive || m_plagueAffectedCountries.empty()) {
        return;
    }

    rebuildCountryAdjacency(countries);

    std::vector<int> countryIndexToVectorIndex(static_cast<size_t>(m_countryAdjacencySize), -1);
    for (size_t i = 0; i < countries.size(); ++i) {
        int idx = countries[i].getCountryIndex();
        if (idx >= 0 && idx < m_countryAdjacencySize) {
            countryIndexToVectorIndex[static_cast<size_t>(idx)] = static_cast<int>(i);
        }
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> spreadDist(0.0, 1.0);
    std::uniform_real_distribution<> recoveryDist(0.0, 1.0);

    std::unordered_set<int> nextAffected = m_plagueAffectedCountries;

    for (int countryIndex : m_plagueAffectedCountries) {
        if (countryIndex < 0 || countryIndex >= static_cast<int>(countries.size())) {
            continue;
        }

        if (recoveryDist(gen) < 0.15) {
            nextAffected.erase(countryIndex);
            continue;
        }

        int sourceCountryIndex = countries[static_cast<size_t>(countryIndex)].getCountryIndex();
        for (int neighborCountryIndex : getAdjacentCountryIndices(sourceCountryIndex)) {
            if (neighborCountryIndex < 0 || neighborCountryIndex >= m_countryAdjacencySize) {
                continue;
            }

            int neighborVecIndex = countryIndexToVectorIndex[static_cast<size_t>(neighborCountryIndex)];
            if (neighborVecIndex < 0 || neighborVecIndex >= static_cast<int>(countries.size())) {
                continue;
            }

            if (countries[static_cast<size_t>(neighborVecIndex)].getPopulation() <= 0) {
                continue;
            }
            if (nextAffected.count(neighborVecIndex) > 0) {
                continue;
            }

            if (spreadDist(gen) < 0.35) {
                nextAffected.insert(neighborVecIndex);
            }
        }
    }

    if (!nextAffected.empty()) {
        m_plagueAffectedCountries = std::move(nextAffected);
    }
}

bool Map::isCountryAffectedByPlague(int countryIndex) const {
    return m_plagueAffectedCountries.count(countryIndex) > 0;
}

bool Map::isPlagueActive() const {
    return m_plagueActive;
}

// 🚀 NUCLEAR OPTIMIZATION: Use boundary pixels instead of scanning entire map
bool Map::areNeighbors(const Country& country1, const Country& country2) const {
    int countryIndex2 = country2.getCountryIndex();
    
    // Check if any boundary pixel of country1 has country2 as a neighbor
    const auto& boundaryPixels = country1.getBoundaryPixels();
    
    for (const auto& pixel : boundaryPixels) {
        // Check 8 neighbors around this boundary pixel
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                int nx = pixel.x + dx;
                int ny = pixel.y + dy;

                if (nx >= 0 && nx < static_cast<int>(m_countryGrid[0].size()) && 
                    ny >= 0 && ny < static_cast<int>(m_countryGrid.size())) {
                    if (m_countryGrid[ny][nx] == countryIndex2) {
                        return true; // Found neighboring territory
                    }
                }
            }
        }
    }
    return false;
}

int Map::getPlagueStartYear() const {
    return m_plagueStartYear;
}

// MEGA TIME JUMP - SIMULATE THOUSANDS OF YEARS OF HISTORY
void Map::megaTimeJump(std::vector<Country>& countries, int& currentYear, int targetYear, News& news, 
                       TechnologyManager& techManager, CultureManager& cultureManager, 
                       GreatPeopleManager& greatPeopleManager, 
                       std::function<void(int, int, float)> progressCallback) {
    
    std::cout << "\nBEGINNING MEGA SIMULATION OF HUMAN HISTORY!" << std::endl;
    
    std::random_device rd;
    std::mt19937 gen(rd());
    
    int totalYears = targetYear - currentYear;
    int startYear = currentYear;
    
    // Track major historical events
    std::vector<std::string> majorEvents;
    std::vector<std::string> extinctCountries;
    std::vector<std::string> superPowers;
    int totalWars = 0;
    int totalPlagues = 0;
    int totalTechBreakthroughs = 0;
    
    std::cout << "SIMULATION PARAMETERS:" << std::endl;
    std::cout << "   Years to simulate: " << totalYears << std::endl;
    std::cout << "   Starting countries: " << countries.size() << std::endl;
    std::cout << "   Optimization level: MAXIMUM" << std::endl;
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Clear dirty regions and mark all for update
    m_dirtyRegions.clear();
    int totalRegions = (m_baseImage.getSize().x / m_gridCellSize / m_regionSize) * 
                      (m_baseImage.getSize().y / m_gridCellSize / m_regionSize);
    for (int i = 0; i < totalRegions; ++i) {
        m_dirtyRegions.insert(i);
    }
    
    // 🚀 SUPER OPTIMIZATION: Process in large chunks for maximum speed
    const int megaChunkSize = 50; // Process 50 years at a time
    int chunksProcessed = 0;
    int totalChunks = (totalYears + megaChunkSize - 1) / megaChunkSize;
    
    std::cout << "\nBEGINNING MEGA CHUNKS (" << totalChunks << " chunks of " << megaChunkSize << " years each)..." << std::endl;
    
    for (int chunkStart = 0; chunkStart < totalYears; chunkStart += megaChunkSize) {
        int chunkYears = std::min(megaChunkSize, totalYears - chunkStart);
        chunksProcessed++;
        
        // Calculate ETA
        auto currentTime = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(currentTime - startTime);
        float progressPercent = (float)chunkStart / totalYears * 100.0f;
        float etaSeconds = elapsed.count() * (100.0f / progressPercent - 1.0f);
        
        // Call progress callback if provided
        if (progressCallback && chunkStart > 0) {
            progressCallback(currentYear, targetYear, etaSeconds);
        }
        
        std::cout << "MEGA CHUNK " << chunksProcessed << "/" << totalChunks 
                  << " (" << std::fixed << std::setprecision(1) << progressPercent << "%) - "
                  << "Years " << currentYear << " to " << (currentYear + chunkYears)
                  << " | ETA: " << std::setprecision(0) << etaSeconds << "s" << std::endl;
        
        // Process this chunk
        for (int year = 0; year < chunkYears; ++year) {
            currentYear++;
            if (currentYear == 0) currentYear = 1; // Skip year 0
            
            // 🦠 CONSISTENT PLAGUE TIMING - Same as normal mode (600-700 years)
            if (currentYear == m_nextPlagueYear && !m_plagueActive) {
                startPlague(currentYear, news);
                initializePlagueCluster(countries); // Initialize geographic cluster
                totalPlagues++;
                
                std::string plagueEvent = "🦠 MEGA PLAGUE ravages the world in " + std::to_string(currentYear);
                if (currentYear < 0) plagueEvent += " BCE";
                else plagueEvent += " CE";
                majorEvents.push_back(plagueEvent);
            }
            if (m_plagueActive && (currentYear == m_plagueStartYear + 3)) { // 3 year plagues
                endPlague(news);
            }
            
            // 🚀 MEGA COUNTRY UPDATES - Realistic growth and expansion
            for (size_t i = 0; i < countries.size(); ++i) {
                if (countries[i].getPopulation() <= 0) continue; // Skip dead countries
                
                // 📈 ACCELERATED POPULATION GROWTH (every year)
                long long currentPop = countries[i].getPopulation();
                
                if (!m_plagueActive) {
                    // 🗺️ TERRITORIAL EXPANSION - More frequent for mega simulation
                    if (year % 3 == 0) { // Every 3 years instead of every 5
                        countries[i].fastForwardGrowth(year, currentYear, m_isLandGrid, m_countryGrid, m_resourceGrid, news, *this, techManager);
                    }
                    
                    // Let the fastForwardGrowth function handle population growth naturally
                    // based on territory size, resources, and food availability
                } else if (isCountryAffectedByPlague(static_cast<int>(i))) {
                    // NEW TECH-DEPENDENT PLAGUE SYSTEM - Only affect countries in plague cluster
                    double baseDeathRate = 0.05; // 5% typical country hit
                    long long deaths = static_cast<long long>(std::llround(currentPop * baseDeathRate * countries[i].getPlagueMortalityMultiplier(techManager)));
                    deaths = std::min(deaths, currentPop); // Clamp deaths to population
                    countries[i].applyPlagueDeaths(deaths);
                    m_plagueDeathToll += deaths;
                }
                
                // 🚀 MEGA OPTIMIZATION: Reduced frequency tech/culture progression (every 25 years instead of 10)
                if (year % 25 == 0) { // Much less frequent = much faster mega time jumps
                    // Apply neighbor science bonus first
                    double neighborBonus = countries[i].calculateNeighborScienceBonus(countries, *this, techManager, year);
                    if (neighborBonus > 0) {
                        countries[i].addSciencePoints(neighborBonus * 1.5); // Reduced from 2.5 to 1.5 for realism
                    }
                    
                    // Update tech/culture multiple times to compensate for reduced frequency
                    for (int techRounds = 0; techRounds < 3; ++techRounds) {
                        techManager.updateCountry(countries[i]);
                        cultureManager.updateCountry(countries[i]);
                    }
                    
                    // Count actual tech unlocks for statistics
                    static size_t lastTechCount = 0;
                    size_t currentTechCount = techManager.getUnlockedTechnologies(countries[i]).size();
                    if (currentTechCount > lastTechCount) {
                        totalTechBreakthroughs += (currentTechCount - lastTechCount);
                        lastTechCount = currentTechCount;
                    }
                }
                
                // 🏛️ MEGA TIME JUMP IDEOLOGY CHANGES - Every 30 years (OPTIMIZATION)
                if (year % 30 == 0) {
                    countries[i].checkIdeologyChange(currentYear, news);
                }
            }
            
            // 🗡️ MEGA WAR SIMULATION - Epic conflicts every 50 years instead of 25 (OPTIMIZATION)
            if (year % 50 == 0) {
                for (size_t i = 0; i < countries.size(); ++i) {
                    if (countries[i].getType() == Country::Type::Warmonger && 
                        !countries[i].isAtWar() && countries[i].getPopulation() > 1000) {
                        
                        // 💀 FIRST: Check for annihilation opportunities (weak neighbors)
                        Country* weakestNeighbor = nullptr;
                        double weakestMilitaryStrength = std::numeric_limits<double>::max();
                        
                        for (size_t j = 0; j < countries.size(); ++j) {
                            if (i != j && areNeighbors(countries[i], countries[j])) {
                                double targetStrength = countries[j].getMilitaryStrength();
                                if (targetStrength < weakestMilitaryStrength && 
                                    countries[i].canAnnihilateCountry(countries[j])) {
                                    weakestMilitaryStrength = targetStrength;
                                    weakestNeighbor = &countries[j];
                                }
                            }
                        }
                        
                        if (weakestNeighbor) {
                            // 💀 ANNIHILATION ATTACK - Complete absorption
                            countries[i].startWar(*weakestNeighbor, news);
                            countries[i].absorbCountry(*weakestNeighbor, m_countryGrid, news);
                            totalWars++;
                            
                            std::string annihilationEvent = "💀 ANNIHILATION: " + countries[i].getName() + 
                                                           " completely destroys " + weakestNeighbor->getName() + " in " + std::to_string(currentYear);
                            if (currentYear < 0) annihilationEvent += " BCE";
                            else annihilationEvent += " CE";
                            majorEvents.push_back(annihilationEvent);
                        } else {
                            // 🗡️ NORMAL WAR - Find the largest neighbor to attack
                            Country* largestNeighbor = nullptr;
                            long long largestPop = 0;
                            
                            for (size_t j = 0; j < countries.size(); ++j) {
                                if (i != j && areNeighbors(countries[i], countries[j])) {
                                    if (countries[j].getPopulation() > largestPop) {
                                        largestPop = countries[j].getPopulation();
                                        largestNeighbor = &countries[j];
                                    }
                                }
                            }
                            
                            if (largestNeighbor) {
                                countries[i].startWar(*largestNeighbor, news);
                                totalWars++;
                                
                                std::string warEvent = "⚔️ " + countries[i].getName() + " attacks " + largestNeighbor->getName() + " in " + std::to_string(currentYear);
                                if (currentYear < 0) warEvent += " BCE";
                                else warEvent += " CE";
                                majorEvents.push_back(warEvent);
                            }
                        }
                    }
                }
            }
            
            // 👑 MEGA GREAT PEOPLE - Every 10 years
            if (year % 10 == 0) {
                greatPeopleManager.updateEffects(currentYear, countries, news);
            }
        }
        
        // 📊 Mark extinct countries and track superpowers (keep indices stable by never erasing).
        long long totalWorldPopulation = 0;
        for (const auto& country : countries) {
            totalWorldPopulation += country.getPopulation();
        }

        long long superpowerThreshold = std::max(100000LL, totalWorldPopulation / 20); // Top 5% or min 100k

        for (int i = 0; i < static_cast<int>(countries.size()); ++i) {
            Country& country = countries[static_cast<size_t>(i)];

            const bool alreadyExtinct = (country.getPopulation() <= 0) && country.getBoundaryPixels().empty();
            const bool isExtinct = (country.getPopulation() <= 50) || country.getBoundaryPixels().empty();

            if (!alreadyExtinct && isExtinct) {
                extinctCountries.push_back(country.getName() + " (extinct in " + std::to_string(currentYear) +
                                           " - Pop: " + std::to_string(country.getPopulation()) +
                                           ", Territory: " + std::to_string(country.getBoundaryPixels().size()) + " pixels)");
                markCountryExtinct(countries, i, currentYear, news);
                continue;
            }

            if (!isExtinct && country.getPopulation() > superpowerThreshold) {
                bool alreadyTracked = false;
                for (const auto& power : superPowers) {
                    if (power.find(country.getName()) != std::string::npos) {
                        alreadyTracked = true;
                        break;
                    }
                }
                if (!alreadyTracked) {
                    superPowers.push_back("🏛️ " + country.getName() + " becomes a superpower (" + std::to_string(country.getPopulation()) + " people in " + std::to_string(currentYear) + ")");
                }
            }
        }
        
        // Track world population milestones naturally
        static long long lastMilestone = 0;
        // Only track significant natural milestones (powers of 10)
        long long currentMilestone = 1;
        while (currentMilestone <= totalWorldPopulation) {
            currentMilestone *= 10;
        }
        currentMilestone /= 10; // Get the largest power of 10 below current population
        
        if (currentMilestone > lastMilestone && currentMilestone >= 1000000) { // Only track millions and above
            majorEvents.push_back("🌍 World population reaches " + std::to_string(currentMilestone) + " in " + std::to_string(currentYear));
            lastMilestone = currentMilestone;
        }
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    std::cout << "\n🎉🎉🎉 MEGA TIME JUMP COMPLETE! 🎉🎉🎉" << std::endl;
    std::cout << "⏱️ Simulated " << totalYears << " years in " << (totalDuration.count() / 1000.0) << " seconds!" << std::endl;
    std::cout << "⚡ Performance: " << (totalYears / (totalDuration.count() / 1000.0)) << " years/second" << std::endl;
    
    // Calculate final world population
    long long finalWorldPopulation = 0;
    int survivingCountries = 0;
    for (const auto& country : countries) {
        finalWorldPopulation += country.getPopulation();
        if (country.getPopulation() > 0 && !country.getBoundaryPixels().empty()) {
            survivingCountries++;
        }
    }
    
    std::cout << "\n📈 MEGA STATISTICS:" << std::endl;
    std::cout << "   🏛️ Surviving countries: " << survivingCountries << std::endl;
    std::cout << "   💀 Extinct countries: " << extinctCountries.size() << std::endl;
    std::cout << "   🌍 Final world population: " << finalWorldPopulation << std::endl;
    std::cout << "   ⚔️ Total wars: " << totalWars << std::endl;
    std::cout << "   🦠 Total plagues: " << totalPlagues << std::endl;
    std::cout << "   💀 Total plague deaths: " << m_plagueDeathToll << std::endl;
    std::cout << "   🧠 Tech breakthroughs: " << totalTechBreakthroughs << std::endl;
    
    std::cout << "\n🌟 MAJOR HISTORICAL EVENTS:" << std::endl;
    int eventCount = 0;
    for (const auto& event : majorEvents) {
        if (eventCount++ >= 10) break; // Show top 10 events
        std::cout << "   " << event << std::endl;
    }
    
    std::cout << "\n💀 EXTINCT CIVILIZATIONS:" << std::endl;
    for (size_t i = 0; i < std::min(extinctCountries.size(), size_t(5)); ++i) {
        std::cout << "   " << extinctCountries[i] << std::endl;
    }
    
    std::cout << "\n🏛️ SUPERPOWERS EMERGED:" << std::endl;
    for (const auto& power : superPowers) {
        std::cout << "   " << power << std::endl;
    }
    
    std::cout << "\n🌍 Welcome to " << currentYear;
    if (currentYear < 0) std::cout << " BCE";
    else std::cout << " CE";
    std::cout << "! The world has changed dramatically!" << std::endl;
}

void Map::updatePlagueDeaths(int deaths) {
    m_plagueDeathToll += deaths;
}

const std::vector<std::vector<bool>>& Map::getIsLandGrid() const {
    return m_isLandGrid;
}

sf::Vector2i Map::pixelToGrid(const sf::Vector2f& pixel) const {
    return sf::Vector2i(static_cast<int>(pixel.x / m_gridCellSize), static_cast<int>(pixel.y / m_gridCellSize));
}

int Map::getGridCellSize() const {
    return m_gridCellSize;
}

std::mutex& Map::getGridMutex() {
    return m_gridMutex;
}

const sf::Image& Map::getBaseImage() const {
    return m_baseImage;
}

int Map::getRegionSize() const {
    return m_regionSize;
}

const std::unordered_set<int>& Map::getDirtyRegions() const {
    return m_dirtyRegions;
}

const std::vector<std::vector<int>>& Map::getCountryGrid() const {
    return m_countryGrid;
}

const std::vector<std::vector<std::unordered_map<Resource::Type, double>>>& Map::getResourceGrid() const {
    return m_resourceGrid;
}

// Getter implementation (in country.cpp)
int Country::getNextWarCheckYear() const {
    return m_nextWarCheckYear;
}

// Setter implementation (in country.cpp)
void Country::setNextWarCheckYear(int year) {
    m_nextWarCheckYear = year;
}

void Map::setCountryGridValue(int x, int y, int value) {
    m_countryGrid[y][x] = value;
}

void Map::insertDirtyRegion(int regionIndex) {
    m_dirtyRegions.insert(regionIndex);
}

bool Map::paintCells(int countryIndex,
                     const sf::Vector2i& center,
                     int radius,
                     bool erase,
                     bool allowOverwrite,
                     std::vector<int>& affectedCountries) {
    if (radius < 0) {
        radius = 0;
    }

    const int height = static_cast<int>(m_countryGrid.size());
    if (height <= 0) {
        return false;
    }
    const int width = static_cast<int>(m_countryGrid[0].size());
    if (width <= 0) {
        return false;
    }

    const int regionsPerRow = m_regionSize > 0 ? (width / m_regionSize) : 0;
    if (regionsPerRow <= 0) {
        return false;
    }

    const int minX = std::max(0, center.x - radius);
    const int maxX = std::min(width - 1, center.x + radius);
    const int minY = std::max(0, center.y - radius);
    const int maxY = std::min(height - 1, center.y + radius);

    const int r2 = radius * radius;
    bool anyChanged = false;

    std::lock_guard<std::mutex> lock(m_gridMutex);
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const int dx = x - center.x;
            const int dy = y - center.y;
            if (dx * dx + dy * dy > r2) {
                continue;
            }
            if (!m_isLandGrid[y][x]) {
                continue;
            }

            const int prevOwner = m_countryGrid[y][x];
            int nextOwner = prevOwner;
            if (erase) {
                nextOwner = -1;
            } else {
                if (countryIndex < 0) {
                    continue;
                }
                if (prevOwner == -1 || prevOwner == countryIndex) {
                    nextOwner = countryIndex;
                } else if (allowOverwrite) {
                    nextOwner = countryIndex;
                } else {
                    continue;
                }
            }

            if (nextOwner == prevOwner) {
                continue;
            }

            m_countryGrid[y][x] = nextOwner;
            anyChanged = true;

            if (prevOwner >= 0) {
                affectedCountries.push_back(prevOwner);
            }
            if (nextOwner >= 0) {
                affectedCountries.push_back(nextOwner);
            }

            const int regionX = x / m_regionSize;
            const int regionY = y / m_regionSize;
            m_dirtyRegions.insert(regionY * regionsPerRow + regionX);
        }
    }

    return anyChanged;
}

void Map::rebuildCountryBoundary(Country& country) {
    const int idx = country.getCountryIndex();
    if (idx < 0) {
        country.setTerritory(std::unordered_set<sf::Vector2i>{});
        return;
    }

    std::unordered_set<sf::Vector2i> territory;
    const int height = static_cast<int>(m_countryGrid.size());
    const int width = height > 0 ? static_cast<int>(m_countryGrid[0].size()) : 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (m_countryGrid[y][x] == idx) {
                territory.insert(sf::Vector2i(x, y));
            }
        }
    }
    country.setTerritory(territory);
}

void Map::rebuildBoundariesForCountries(std::vector<Country>& countries, const std::vector<int>& countryIndices) {
    if (countries.empty() || countryIndices.empty()) {
        return;
    }

    std::vector<int> unique = countryIndices;
    std::sort(unique.begin(), unique.end());
    unique.erase(std::unique(unique.begin(), unique.end()), unique.end());

    std::vector<int> valid;
    valid.reserve(unique.size());
    for (int idx : unique) {
        if (idx >= 0 && idx < static_cast<int>(countries.size())) {
            valid.push_back(idx);
        }
    }
    if (valid.empty()) {
        return;
    }

    std::vector<int> indexToSlot(countries.size(), -1);
    for (size_t slot = 0; slot < valid.size(); ++slot) {
        indexToSlot[static_cast<size_t>(valid[slot])] = static_cast<int>(slot);
    }

    std::vector<std::unordered_set<sf::Vector2i>> territories(valid.size());

    const int height = static_cast<int>(m_countryGrid.size());
    const int width = static_cast<int>(m_countryGrid[0].size());
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int owner = m_countryGrid[y][x];
            if (owner < 0 || owner >= static_cast<int>(indexToSlot.size())) {
                continue;
            }
            int slot = indexToSlot[static_cast<size_t>(owner)];
            if (slot < 0) {
                continue;
            }
            territories[static_cast<size_t>(slot)].insert(sf::Vector2i(x, y));
        }
    }

    for (size_t slot = 0; slot < valid.size(); ++slot) {
        countries[static_cast<size_t>(valid[slot])].setTerritory(territories[slot]);
    }
}

void Map::rebuildAdjacency(const std::vector<Country>& countries) {
    rebuildCountryAdjacency(countries);
}

std::vector<std::vector<int>>& Map::getCountryGrid() {
    return m_countryGrid;
}

std::unordered_set<int>& Map::getDirtyRegions() {
    return m_dirtyRegions;
}

// map.cpp (Modified section)

void Map::triggerPlague(int year, News& news) {
    startPlague(year, news); // Reuse the existing startPlague logic

    // Immediately reset the next plague year for "spamming"
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> plagueIntervalDist(600, 700);
    m_plagueInterval = plagueIntervalDist(gen);
    m_nextPlagueYear = year + m_plagueInterval;
}

// FAST FORWARD MODE: Optimized simulation for 100 years in 2 seconds
void Map::fastForwardSimulation(std::vector<Country>& countries, int& currentYear, int targetYears, News& news, TechnologyManager& technologyManager) {
    std::random_device rd;
    std::mt19937 gen(rd());
    
    // Clear dirty regions to start fresh
    m_dirtyRegions.clear();
    
    // Mark all regions as dirty for full update at the end
    int totalRegions = (m_baseImage.getSize().x / m_gridCellSize / m_regionSize) * 
                      (m_baseImage.getSize().y / m_gridCellSize / m_regionSize);
    for (int i = 0; i < totalRegions; ++i) {
        m_dirtyRegions.insert(i);
    }
    
    for (int year = 0; year < targetYears; ++year) {
        currentYear++;
        if (currentYear == 0) currentYear = 1;
        
        // Randomized plague logic - every 600-700 years (same as normal mode)
        if (currentYear == m_nextPlagueYear && !m_plagueActive) {
            startPlague(currentYear, news);
            initializePlagueCluster(countries); // Initialize geographic cluster
        }
        if (m_plagueActive && (currentYear == m_plagueStartYear + 3)) {
            endPlague(news);
        }
        
        // Batch update countries with simplified logic
        for (size_t i = 0; i < countries.size(); ++i) {
            // Simplified population growth and expansion
            if (!m_plagueActive) {
                countries[i].fastForwardGrowth(year, currentYear, m_isLandGrid, m_countryGrid, m_resourceGrid, news, *this, technologyManager);
            } else if (isCountryAffectedByPlague(static_cast<int>(i))) {
                // NEW TECH-DEPENDENT PLAGUE SYSTEM - Only affect countries in plague cluster
                double baseDeathRate = 0.05; // 5% typical country hit
                long long deaths = static_cast<long long>(std::llround(countries[i].getPopulation() * baseDeathRate * countries[i].getPlagueMortalityMultiplier(technologyManager)));
                deaths = std::min(deaths, countries[i].getPopulation()); // Clamp deaths to population
                countries[i].applyPlagueDeaths(deaths);
                m_plagueDeathToll += deaths;
            }
            
            // TECHNOLOGY SHARING - Trader countries attempt to share technology
            countries[i].attemptTechnologySharing(currentYear, countries, technologyManager, *this, news);
        }
        
        // Simplified war logic - only every 10 years for performance
        if (year % 10 == 0) {
            for (size_t i = 0; i < countries.size(); ++i) {
                if (countries[i].getType() == Country::Type::Warmonger && 
                    countries[i].canDeclareWar() && !countries[i].isAtWar()) {
                    
                    // Find potential targets (simplified)
                    std::vector<size_t> potentialTargets;
                    for (size_t j = 0; j < countries.size(); ++j) {
                        if (i != j && countries[i].getMilitaryStrength() > countries[j].getMilitaryStrength()) {
                            potentialTargets.push_back(j);
                        }
                    }
                    
                    // 15% chance to declare war (reduced for fast forward)
                    if (!potentialTargets.empty() && gen() % 100 < 15) {
                        size_t targetIndex = potentialTargets[gen() % potentialTargets.size()];
                        countries[i].startWar(countries[targetIndex], news);
                    }
                }
                
                // Update war status
                if (countries[i].isAtWar()) {
                    countries[i].decrementWarDuration();
                    if (countries[i].getWarDuration() <= 0) {
                        countries[i].endWar(currentYear);
                    }
                }
                
                // TECHNOLOGY SHARING - Trader countries attempt to share technology
                countries[i].attemptTechnologySharing(currentYear, countries, technologyManager, *this, news);
            }
        }
    }
}
