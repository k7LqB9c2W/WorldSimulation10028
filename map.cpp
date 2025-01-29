#include "map.h"
#include <random>
#include <iostream>

Map::Map(const sf::Image& baseImage, const sf::Image& resourceImage, int gridCellSize, const sf::Color& landColor, const sf::Color& waterColor, int regionSize) :
    m_gridCellSize(gridCellSize),
    m_regionSize(regionSize),
    m_landColor(landColor),
    m_waterColor(waterColor),
    m_baseImage(baseImage),
    m_resourceImage(resourceImage),
    m_plagueActive(false),
    m_plagueStartYear(0),
    m_plagueDeathToll(0)
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

void Map::initializeResourceGrid() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> resourceDist(0.2, 2.0);
    std::uniform_real_distribution<> hotspotMultiplier(2.0, 6.0);

    for (size_t y = 0; y < m_isLandGrid.size(); ++y) {
        for (size_t x = 0; x < m_isLandGrid[y].size(); ++x) {
            if (m_isLandGrid[y][x]) {
                // Calculate food based on distance to water
                double foodAmount = 1.0;
                bool foundWater = false;
                for (int dy = -3; dy <= 3; ++dy) {
                    for (int dx = -3; dx <= 3; ++dx) {
                        if (dx == 0 && dy == 0) continue;
                        int nx = static_cast<int>(x) + dx;
                        int ny = static_cast<int>(y) + dy;
                        if (nx >= 0 && nx < static_cast<int>(m_isLandGrid[0].size()) && ny >= 0 && ny < static_cast<int>(m_isLandGrid.size())) {
                            if (!m_isLandGrid[ny][nx]) {
                                int distance = std::max(std::abs(dx), std::abs(dy));
                                if (distance == 1) foodAmount = 5.0;
                                else if (distance == 2 && foodAmount < 4.0) foodAmount = 4.0;
                                else if (distance == 3 && foodAmount < 3.0) foodAmount = 3.0;
                                foundWater = true;
                            }
                        }
                    }
                    if (foundWater) break;
                }
                m_resourceGrid[y][x][Resource::Type::FOOD] = foodAmount;

                // Initialize other resources based on resourceImage
                sf::Vector2u pixelPos(static_cast<unsigned int>(x * m_gridCellSize), static_cast<unsigned int>(y * m_gridCellSize));
                sf::Color resourcePixelColor = m_resourceImage.getPixel(pixelPos.x, pixelPos.y);

                // Only process if the pixel is not fully transparent
                if (resourcePixelColor.a > 0) {
                    for (const auto& [color, type] : m_resourceColors) {
                        if (resourcePixelColor == color) {
                            double baseAmount = resourceDist(gen);
                            baseAmount *= hotspotMultiplier(gen);
                            m_resourceGrid[y][x][type] = baseAmount;
                            break;
                        }
                    }
                }
            }
        }
    }
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

void Map::initializeCountries(std::vector<Country>& countries, int numCountries) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> xDist(0, static_cast<int>(m_isLandGrid[0].size() - 1));
    std::uniform_int_distribution<> yDist(0, static_cast<int>(m_isLandGrid.size() - 1));
    std::uniform_int_distribution<> colorDist(50, 255);
    std::uniform_int_distribution<> popDist(1000, 10000); // Initial population between 1000 and 10000
    std::uniform_real_distribution<> growthRateDist(0.0003, 0.001); // Growth rate between 0.03% and 0.1%

    // Define some example country names
    //std::vector<std::string> countryNames = {
        //"Alvonian", "Bantorian", "Caledonian", "Dravidian", "Elysian",
        //"Feronian", "Galtian", "Hellenian", "Iberian", "Jovian",
        //"Keltian", "Lydian", "Mycenaean", "Norse", "Ophirian",
        //"Persian", "Qunari", "Roman", "Sumerian", "Thracian",
        //"Uruk", "Vandal", "Wessex", "Xanadu", "Yoruba",
      //  "Zargonian"
    //};

    std::uniform_int_distribution<> typeDist(0, 2); // 0 = Warmonger, 1 = Pacifist, 2 = Trader
    std::discrete_distribution<> scienceTypeDist({ 50, 40, 10 }); // 50% NS, 40% LS, 10% MS

    for (int i = 0; i < numCountries; ++i) {
        sf::Vector2i startCell;
        do {
            startCell.x = xDist(gen);
            startCell.y = yDist(gen);
        } while (!m_isLandGrid[startCell.y][startCell.x]);

        sf::Color countryColor(colorDist(gen), colorDist(gen), colorDist(gen));
        long long initialPopulation = popDist(gen);
        double growthRate = growthRateDist(gen);

        // Generate a unique random name:
        std::string countryName = generate_country_name();
        while (isNameTaken(countries, countryName)) {
            countryName = generate_country_name();
        }

        // Add " Tribe" to the generated name:
        countryName += " Tribe"; // Now done here

        // Assign a random type to the country
        Country::Type countryType = static_cast<Country::Type>(typeDist(gen));

        // Assign a random science type to the country
        Country::ScienceType scienceType = static_cast<Country::ScienceType>(scienceTypeDist(gen));

        countries.emplace_back(i, countryColor, startCell, initialPopulation, growthRate, countryName, countryType, scienceType);
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

void Map::updateCountries(std::vector<Country>& countries, int currentYear, News& news) {
    m_dirtyRegions.clear();

    // Declare rd and gen here, outside of any loops or conditional blocks
    std::random_device rd;
    std::mt19937 gen(rd());

    // Trigger the plague in the year 4950
    if (currentYear == m_nextPlagueYear) {
        startPlague(currentYear, news);
    }

    // Check if the plague should end
    if (m_plagueActive && (currentYear == m_plagueStartYear + 3)) {
        endPlague(news);
    }

    // No need for tempGrid here anymore - tempGrid is handled within Country::update
    // std::vector<std::vector<int>> tempGrid = m_countryGrid; // REMOVE THIS LINE

    // Use OpenMP to update countries in parallel
#pragma omp parallel for
    for (int i = 0; i < countries.size(); ++i) {
        countries[i].update(m_isLandGrid, m_countryGrid, m_gridMutex, m_gridCellSize, m_regionSize, m_dirtyRegions, currentYear, m_resourceGrid, news, m_plagueActive, m_plagueDeathToll, *this);
        // Check for war declarations only if not already at war and it's not before 4950 BCE
        if (currentYear >= -4950 && countries[i].getType() == Country::Type::Warmonger && countries[i].canDeclareWar() && !countries[i].isAtWar() && currentYear >= countries[i].getNextWarCheckYear()) {
            // Check for potential targets among neighboring countries

            std::cout << "Year " << currentYear << ": " << countries[i].getName() << " (Warmonger) is checking for war." << std::endl; // Debug: War check start

            std::vector<int> potentialTargets;
            for (int j = 0; j < countries.size(); ++j) {
                if (i != j && areNeighbors(countries[i], countries[j]) && countries[i].getMilitaryStrength() > countries[j].getMilitaryStrength()) {
                    potentialTargets.push_back(j);
                }
            }
            // If no potential targets were found, set the next war check year
            if (potentialTargets.empty()) {
                std::cout << "  No potential targets found for " << countries[i].getName() << "." << std::endl; // Debug: No targets
                std::uniform_int_distribution<> delayDist(50, 150);
                int delay = delayDist(gen);
                countries[i].setNextWarCheckYear(currentYear + delay);
            }
            else {
                // 25% chance to declare war even if a target is found
                std::uniform_real_distribution<> warChance(0.0, 1.0);
                if (warChance(gen) < 0.25) {
                    std::uniform_int_distribution<> targetDist(0, potentialTargets.size() - 1);
                    int targetIndex = potentialTargets[targetDist(gen)];

                    // Declare war on the chosen target
                    countries[i].startWar(countries[targetIndex], news);

                    // If a war is declared, also set a cooldown (you might want a different cooldown after a war)
                    std::uniform_int_distribution<> delayDist(5, 10); // Example: 5-10 years cooldown after a war
                    int delay = delayDist(gen);
                    countries[i].setNextWarCheckYear(currentYear + delay); // Use setter here
                }
                else {
                    // War not declared, set a shorter delay
                    std::cout << "  " << countries[i].getName() << " decided not to declare war this time." << std::endl;
                    std::uniform_int_distribution<> delayDist(10, 30); // Shorter delay
                    int delay = delayDist(gen);
                    countries[i].setNextWarCheckYear(currentYear + delay);
                }
            }
        }

        // Decrement war duration and peace duration
        if (countries[i].isAtWar()) {
            countries[i].decrementWarDuration();
            if (countries[i].getWarDuration() <= 0) {
                countries[i].endWar();
            }
        }
        else {
            countries[i].decrementPeaceDuration();
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

void Map::startPlague(int year, News& news) {
    m_plagueActive = true;
    m_plagueStartYear = year;
    m_plagueDeathToll = 0;
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
    news.addEvent("The Great Plague has ended. Total deaths: " + std::to_string(m_plagueDeathToll));
}

bool Map::isPlagueActive() const {
    return m_plagueActive;
}

bool Map::areNeighbors(const Country& country1, const Country& country2) const {
    int countryIndex1 = country1.getCountryIndex();
    int countryIndex2 = country2.getCountryIndex();

    for (size_t y = 0; y < m_countryGrid.size(); ++y) {
        for (size_t x = 0; x < m_countryGrid[y].size(); ++x) {
            if (m_countryGrid[y][x] == countryIndex1) { // Cell belongs to country1
                // Check 8 neighbors (Moore neighborhood)
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0) continue;
                        int nx = static_cast<int>(x) + dx;
                        int ny = static_cast<int>(y) + dy;

                        if (nx >= 0 && nx < static_cast<int>(m_countryGrid[0].size()) && ny >= 0 && ny < static_cast<int>(m_countryGrid.size())) {
                            if (m_countryGrid[ny][nx] == countryIndex2) { // Neighbor cell belongs to country2
                                return true; // Found a neighboring cell, they are neighbors
                            }
                        }
                    }
                }
            }
        }
    }
    return false; // No neighboring cells found
}

int Map::getPlagueStartYear() const {
    return m_plagueStartYear;
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