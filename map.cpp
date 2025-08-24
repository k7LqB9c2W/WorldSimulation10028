#include "map.h"
#include "technology.h"
#include "culture.h"
#include "great_people.h"
#include <random>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <limits>
#include <queue>
#include <unordered_set>

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

    // Check if the plague should end
    if (m_plagueActive && (currentYear == m_plagueStartYear + 3)) {
        endPlague(news);
    }

    // No need for tempGrid here anymore - tempGrid is handled within Country::update
    // std::vector<std::vector<int>> tempGrid = m_countryGrid; // REMOVE THIS LINE

    // 🛡️ PERFORMANCE FIX: Remove OpenMP to prevent mutex contention and thread blocking
    for (int i = 0; i < countries.size(); ++i) {
        countries[i].update(m_isLandGrid, m_countryGrid, m_gridMutex, m_gridCellSize, m_regionSize, m_dirtyRegions, currentYear, m_resourceGrid, news, m_plagueActive, m_plagueDeathToll, *this, technologyManager);
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
                    countries[i].startWar(countries[annihilationTarget], news);
                    countries[i].absorbCountry(countries[annihilationTarget], m_countryGrid, news);
                    
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

    // REMOVE THIS ENTIRE BLOCK - Grid update is now handled within Country::update
    /*
    // Update m_countryGrid from tempGrid after processing all countries
    {
        std::lock_guard<std::mutex> lock(m_gridMutex);
        m_countryGrid = tempGrid;
    }
    */
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

void Map::initializePlagueCluster(const std::vector<Country>& countries) {
    if (countries.empty()) return;
    
    // Select a random country with neighbors as starting point
    std::random_device rd;
    std::mt19937 gen(rd());
    std::vector<int> potentialStarters;
    
    // Find countries that have neighbors (to avoid isolated countries)
    for (size_t i = 0; i < countries.size(); ++i) {
        if (countries[i].getPopulation() <= 0) continue; // Skip dead countries
        
        // Check if this country has neighbors
        bool hasNeighbors = false;
        for (size_t j = 0; j < countries.size(); ++j) {
            if (i != j && countries[j].getPopulation() > 0 && areNeighbors(countries[i], countries[j])) {
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
        
        // Check all potential neighbors
        for (size_t i = 0; i < countries.size(); ++i) {
            int neighborIndex = static_cast<int>(i);
            if (visited.count(neighborIndex) || countries[i].getPopulation() <= 0) continue;
            
            // If they are neighbors and plague spreads (70% chance)
            if (areNeighbors(countries[currentCountry], countries[i]) && spreadDist(gen) < 0.7) {
                visited.insert(neighborIndex);
                m_plagueAffectedCountries.insert(neighborIndex);
                toProcess.push(neighborIndex);
            }
        }
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
        
        // 📊 Remove extinct countries and track superpowers
        long long totalWorldPopulation = 0;
        for (auto it = countries.begin(); it != countries.end();) {
            totalWorldPopulation += it->getPopulation();
            
            // 💀 EXTINCTION CONDITIONS - No territory OR no population
            bool isExtinct = (it->getPopulation() <= 50) || (it->getBoundaryPixels().empty());
            
            if (isExtinct) {
                extinctCountries.push_back(it->getName() + " (extinct in " + std::to_string(currentYear) + 
                                         " - Pop: " + std::to_string(it->getPopulation()) + 
                                         ", Territory: " + std::to_string(it->getBoundaryPixels().size()) + " pixels)");
                it = countries.erase(it);
            } else {
                // Track superpowers - dynamic threshold based on world population
                long long superpowerThreshold = std::max(100000LL, totalWorldPopulation / 20); // Top 5% or min 100k
                if (it->getPopulation() > superpowerThreshold) {
                    bool alreadyTracked = false;
                    for (const auto& power : superPowers) {
                        if (power.find(it->getName()) != std::string::npos) {
                            alreadyTracked = true;
                            break;
                        }
                    }
                    if (!alreadyTracked) {
                        superPowers.push_back("🏛️ " + it->getName() + " becomes a superpower (" + std::to_string(it->getPopulation()) + " people in " + std::to_string(currentYear) + ")");
                    }
                }
                ++it;
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
    for (const auto& country : countries) {
        finalWorldPopulation += country.getPopulation();
    }
    
    std::cout << "\n📈 MEGA STATISTICS:" << std::endl;
    std::cout << "   🏛️ Surviving countries: " << countries.size() << std::endl;
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