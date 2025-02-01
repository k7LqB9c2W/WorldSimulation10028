// renderer.cpp

#include "renderer.h"
#include <string>
#include <algorithm>

Renderer::Renderer(sf::RenderWindow& window, const Map& map, const sf::Color& waterColor) :
    m_window(window),
    m_waterColor(waterColor),
    m_needsUpdate(true),
    m_showWarmongerHighlights(false),
    m_showWarHighlights(false),
    m_techScrollOffset(0),
    m_civicScrollOffset(0),
    m_maxCivicScrollOffset(0),
    m_maxTechScrollOffset(0)
{

    // Load the texture from the base image (map.png)
    m_baseTexture.loadFromImage(map.getBaseImage());
    m_baseSprite.setTexture(m_baseTexture);

    if (!m_font.loadFromFile("arial.ttf")) {
        // Handle error - font not found
    }

    m_yearText.setFont(m_font);
    m_yearText.setCharacterSize(30);
    m_yearText.setFillColor(sf::Color::White);
    m_yearText.setPosition(static_cast<float>(m_window.getSize().x) / 2.0f - 100.0f, 20.0f);

    m_countryImage.create(map.getBaseImage().getSize().x, map.getBaseImage().getSize().y, sf::Color::Transparent);
    m_countryTexture.loadFromImage(m_countryImage);
    m_countrySprite.setTexture(m_countryTexture);

    // Initialize country info window elements
    m_infoWindowBackground.setFillColor(sf::Color(0, 0, 0, 175));
    m_infoWindowBackground.setSize(sf::Vector2f(400, 350));

    m_infoWindowText.setCharacterSize(16);
    m_infoWindowText.setFillColor(sf::Color::White);

    m_infoWindowColorSquare.setSize(sf::Vector2f(20, 20));
}

void Renderer::render(const std::vector<Country>& countries, const Map& map, News& news, const TechnologyManager& technologyManager, const CultureManager& cultureManager, const Country* selectedCountry, bool showCountryInfo) {
    m_window.clear();

    m_window.draw(m_baseSprite); // Draw the base map (map.png)

    if (m_needsUpdate) {
        updateCountryImage(map.getCountryGrid(), countries, map);
        m_countryTexture.update(m_countryImage);
        m_needsUpdate = false;
    }

    m_window.draw(m_countrySprite); // Draw the countries

    // Draw cities
    for (const auto& country : countries) {
        for (const auto& city : country.getCities()) {
            sf::RectangleShape cityShape(sf::Vector2f(3, 3));
            cityShape.setFillColor(sf::Color::Black);
            sf::Vector2i cityLocation = city.getLocation();
            cityShape.setPosition(static_cast<float>(cityLocation.x * map.getGridCellSize() - 1), static_cast<float>(cityLocation.y * map.getGridCellSize() - 1));
            m_window.draw(cityShape);
        }
    }

    m_window.draw(m_yearText); // Draw the year

    long long totalPopulation = 0;
    for (const auto& country : countries) {
        totalPopulation += country.getPopulation();
    }

    sf::Text populationText;
    populationText.setFont(m_font);
    populationText.setCharacterSize(24);
    populationText.setFillColor(sf::Color::White);
    populationText.setString("Total World Population: " + std::to_string(totalPopulation));
    populationText.setPosition(static_cast<float>(m_window.getSize().x - 400), static_cast<float>(m_window.getSize().y - 60));

    m_window.draw(populationText); // Draw the population

    // Render the news window if it's toggled on
    if (news.isWindowVisible()) {
        news.render(m_window, m_font);
    }

    // Draw Warmonger highlights if the flag is true
    if (m_showWarmongerHighlights) {
        drawWarmongerHighlights(countries, map);
    }

    // Draw war highlights if the flag is true
    if (m_showWarHighlights) {
        drawWarHighlights(countries, map);
    }

    // Draw country info window if a country is selected and the flag is true
    if (showCountryInfo && selectedCountry != nullptr) {
        drawCountryInfo(selectedCountry, technologyManager);
        drawCivicList(selectedCountry, cultureManager);
    }

    if (m_showCountryAddModeText) {
        sf::Text countryAddModeText;
        countryAddModeText.setFont(m_font);
        countryAddModeText.setCharacterSize(24);
        countryAddModeText.setFillColor(sf::Color::White);
        countryAddModeText.setPosition(10, 10);
        countryAddModeText.setString("Country Add Mode");
        m_window.draw(countryAddModeText);
    }

    m_window.display();
}

void Renderer::drawCountryInfo(const Country* country, const TechnologyManager& techManager) {
    // Center the window
    sf::Vector2u windowSize = m_window.getSize();
    m_infoWindowBackground.setPosition((windowSize.x - m_infoWindowBackground.getSize().x) / 2,
        (windowSize.y - m_infoWindowBackground.getSize().y) / 2);

    m_infoWindowText.setFont(m_font);
    m_window.draw(m_infoWindowBackground);

    std::string infoString;
    infoString += "Name: " + country->getName() + "\n";

    // Color square
    m_infoWindowColorSquare.setFillColor(country->getColor());
    m_infoWindowColorSquare.setPosition(m_infoWindowBackground.getPosition().x + 10, m_infoWindowBackground.getPosition().y + 10);
    m_window.draw(m_infoWindowColorSquare);

    // Calculate total pixels owned
    int totalPixels = country->getBoundaryPixels().size();
    for (const auto& city : country->getCities()) {
        totalPixels++; // Add 1 for each city (assuming each city occupies one pixel)
    }

    // Draw civics title
    m_infoWindowText.setString("Civics:");
    m_infoWindowText.setPosition(m_infoWindowBackground.getPosition().x + 10, m_infoWindowBackground.getPosition().y + 360);
    m_window.draw(m_infoWindowText);

    infoString += "Type: ";
    switch (country->getType()) {
    case Country::Type::Warmonger:
        infoString += "Warmonger";
        break;
    case Country::Type::Pacifist:
        infoString += "Pacifist";
        break;
    case Country::Type::Trader:
        infoString += "Trader";
        break;
    }
    infoString += "\n";

    infoString += "Science Type: ";
    switch (country->getScienceType()) {
    case Country::ScienceType::NS:
        infoString += "NS";
        break;
    case Country::ScienceType::LS:
        infoString += "LS";
        break;
    case Country::ScienceType::MS:
        infoString += "MS";
        break;
    }
    infoString += "\n";

    infoString += "Culture Type: ";
    switch (country->getCultureType()) {
    case Country::CultureType::NC:
        infoString += "NC";
        break;
    case Country::CultureType::LC:
        infoString += "LC";
        break;
    case Country::CultureType::MC:
        infoString += "MC";
        break;
    }
    infoString += "\n";

    infoString += "Population: " + std::to_string(country->getPopulation()) + "\n";
    infoString += "Total Pixels: " + std::to_string(totalPixels) + "\n";

    // Draw technologies title
    m_infoWindowText.setString("Technologies:");
    m_infoWindowText.setPosition(m_infoWindowBackground.getPosition().x + 10, m_infoWindowBackground.getPosition().y + 220);
    m_window.draw(m_infoWindowText);

    // Draw the list of technologies with scrolling
    drawTechList(country, techManager);

    m_infoWindowText.setString(infoString);
    m_infoWindowText.setPosition(m_infoWindowBackground.getPosition().x + 40, m_infoWindowBackground.getPosition().y + 10);
    m_window.draw(m_infoWindowText);
}

void Renderer::drawTechList(const Country* country, const TechnologyManager& techManager) {
    const std::vector<int>& unlockedTechs = techManager.getUnlockedTechnologies(*country);
    const std::unordered_map<int, Technology>& allTechs = techManager.getTechnologies();

    // Calculate the starting y-position for the tech list
    float startY = m_infoWindowBackground.getPosition().y + 240;

    // Define the maximum height for the tech list
    float maxHeight = 150; // Adjust as needed

    // Define the y-increment for each tech item
    float yIncrement = 20;

    // Calculate the total height of the tech list
    float totalHeight = unlockedTechs.size() * yIncrement;

    // Adjust the scroll offset if necessary to keep the list within bounds
    m_maxTechScrollOffset = 0;
    if (totalHeight > maxHeight) {
        m_maxTechScrollOffset = static_cast<int>(totalHeight - maxHeight);
        if (m_techScrollOffset > m_maxTechScrollOffset)
        {
            m_techScrollOffset = m_maxTechScrollOffset;
        }
    }
    else {
        m_techScrollOffset = 0; // Reset scroll offset if the list fits within the max height
    }

    // Draw each tech item, taking the scroll offset into account
    for (size_t i = 0; i < unlockedTechs.size(); ++i) {
        int techId = unlockedTechs[i];
        float yPos = startY + (i * yIncrement) - m_techScrollOffset;

        // Only draw the tech if it's within the visible area
        if (yPos + yIncrement > startY && yPos < startY + maxHeight) {
            m_infoWindowText.setString("- " + allTechs.at(techId).name);
            m_infoWindowText.setPosition(m_infoWindowBackground.getPosition().x + 20, yPos);
            m_window.draw(m_infoWindowText);
        }
    }
}

void Renderer::toggleWarmongerHighlights() {
    m_showWarmongerHighlights = !m_showWarmongerHighlights;
}

void Renderer::toggleWarHighlights() {
    m_showWarHighlights = !m_showWarHighlights;
}

void Renderer::drawWarmongerHighlights(const std::vector<Country>& countries, const Map& map) {
    int gridCellSize = map.getGridCellSize();

    for (const auto& country : countries) {
        if (country.getType() == Country::Type::Warmonger) {
            // Get the bounding box of the country's territory
            sf::Vector2i minCoords(map.getBaseImage().getSize().x, map.getBaseImage().getSize().y);
            sf::Vector2i maxCoords(0, 0);

            if (!country.getBoundaryPixels().empty()) { // Check if boundaryPixels is not empty
                for (const auto& cell : country.getBoundaryPixels()) {
                    minCoords.x = std::min(minCoords.x, cell.x);
                    minCoords.y = std::min(minCoords.y, cell.y);
                    maxCoords.x = std::max(maxCoords.x, cell.x);
                    maxCoords.y = std::max(maxCoords.y, cell.y);
                }

                // Draw a red rectangle around the bounding box
                sf::RectangleShape border(sf::Vector2f((maxCoords.x - minCoords.x + 1) * gridCellSize, (maxCoords.y - minCoords.y + 1) * gridCellSize));
                border.setPosition(sf::Vector2f(minCoords.x * gridCellSize, minCoords.y * gridCellSize));
                border.setOutlineColor(sf::Color::Red);
                border.setOutlineThickness(2.0f);
                border.setFillColor(sf::Color::Transparent);

                m_window.draw(border);
            }
        }
    }
}

void Renderer::updateCountryImage(const std::vector<std::vector<int>>& countryGrid, const std::vector<Country>& countries, const Map& map) {
    int gridCellSize = map.getGridCellSize();
    int regionSize = map.getRegionSize();
    const auto& dirtyRegions = map.getDirtyRegions();

    for (int regionIndex : dirtyRegions) {
        int regionY = regionIndex / (map.getBaseImage().getSize().x / gridCellSize / regionSize);
        int regionX = regionIndex % (map.getBaseImage().getSize().x / gridCellSize / regionSize);

        int startY = regionY * regionSize;
        int startX = regionX * regionSize;
        int endY = std::min(startY + regionSize, static_cast<int>(countryGrid.size()));
        int endX = std::min(startX + regionSize, static_cast<int>(countryGrid[0].size()));

        for (int y = startY; y < endY; ++y) {
            for (int x = startX; x < endX; ++x) {
                int countryIndex = countryGrid[y][x];
                sf::Color pixelColor = (countryIndex != -1) ? countries[countryIndex].getColor() : sf::Color::Transparent;

                for (int i = 0; i < gridCellSize; ++i) {
                    for (int j = 0; j < gridCellSize; ++j) {
                        int pixelX = x * gridCellSize + i;
                        int pixelY = y * gridCellSize + j;
                        if (pixelX >= 0 && pixelX < static_cast<int>(m_countryImage.getSize().x) && pixelY >= 0 && pixelY < static_cast<int>(m_countryImage.getSize().y)) {
                            m_countryImage.setPixel(pixelX, pixelY, pixelColor);
                        }
                    }
                }
            }
        }
    }
}

void Renderer::drawWarHighlights(const std::vector<Country>& countries, const Map& map) {
    int gridCellSize = map.getGridCellSize();

    for (const auto& country : countries) {
        if (country.isAtWar()) { // Check if the country is at war
            // Get the bounding box of the country's territory
            sf::Vector2i minCoords(map.getBaseImage().getSize().x, map.getBaseImage().getSize().y);
            sf::Vector2i maxCoords(0, 0);

            for (const auto& cell : country.getBoundaryPixels()) {
                minCoords.x = std::min(minCoords.x, cell.x);
                minCoords.y = std::min(minCoords.y, cell.y);
                maxCoords.x = std::max(maxCoords.x, cell.x);
                maxCoords.y = std::max(maxCoords.y, cell.y);
            }

            // Draw a red rectangle around the bounding box
            sf::RectangleShape border(sf::Vector2f((maxCoords.x - minCoords.x + 1) * gridCellSize, (maxCoords.y - minCoords.y + 1) * gridCellSize));
            border.setPosition(sf::Vector2f(minCoords.x * gridCellSize, minCoords.y * gridCellSize));
            border.setOutlineColor(sf::Color::Red);
            border.setOutlineThickness(2.0f);
            border.setFillColor(sf::Color::Transparent);

            m_window.draw(border);
        }
    }
}

void Renderer::setNeedsUpdate(bool needsUpdate) {
    m_needsUpdate = needsUpdate;
}

void Renderer::updateYearText(int year) {
    if (year < 0) {
        m_yearText.setString("Year: " + std::to_string(-year) + " BCE");
    }
    else {
        m_yearText.setString("Year: " + std::to_string(year) + " CE");
    }
}

void Renderer::showLoadingScreen() {
    sf::Text loadingText;
    loadingText.setFont(m_font);
    loadingText.setCharacterSize(30);
    loadingText.setFillColor(sf::Color::White);
    loadingText.setString("Loading...");
    loadingText.setPosition(m_window.getSize().x / 2 - loadingText.getLocalBounds().width / 2,
        m_window.getSize().y / 2 - loadingText.getLocalBounds().height / 2);

    m_window.clear();
    m_window.draw(loadingText);
    m_window.display();
}

int Renderer::getTechScrollOffset() const
{
    return m_techScrollOffset;
}

int Renderer::getMaxTechScrollOffset() const
{
    return m_maxTechScrollOffset;
}

void Renderer::setTechScrollOffset(int offset)
{
    m_techScrollOffset = offset;
}

void Renderer::setShowCountryAddModeText(bool show) {
    m_showCountryAddModeText = show;
}

// Add the drawCivicList() function in renderer.cpp:
void Renderer::drawCivicList(const Country* country, const CultureManager& cultureManager) {
    const std::vector<int>& unlockedCivics = cultureManager.getUnlockedCivics(*country);
    const std::unordered_map<int, Civic>& allCivics = cultureManager.getCivics();

    // Calculate the starting y-position for the civic list
    float startY = m_infoWindowBackground.getPosition().y + 380;

    // Define the maximum height for the civic list
    float maxHeight = 150;

    // Define the y-increment for each civic item
    float yIncrement = 20;

    // Calculate the total height of the civic list
    float totalHeight = unlockedCivics.size() * yIncrement;

    // Adjust the scroll offset if necessary
    m_maxCivicScrollOffset = 0;
    if (totalHeight > maxHeight) {
        m_maxCivicScrollOffset = static_cast<int>(totalHeight - maxHeight);
        if (m_civicScrollOffset > m_maxCivicScrollOffset) {
            m_civicScrollOffset = m_maxCivicScrollOffset;
        }
    }
    else {
        m_civicScrollOffset = 0;
    }

    // Draw each civic item
    for (size_t i = 0; i < unlockedCivics.size(); ++i) {
        int civicId = unlockedCivics[i];
        float yPos = startY + (i * yIncrement) - m_civicScrollOffset;

        if (yPos + yIncrement > startY && yPos < startY + maxHeight) {
            m_infoWindowText.setString("- " + allCivics.at(civicId).name);
            m_infoWindowText.setPosition(m_infoWindowBackground.getPosition().x + 20, yPos);
            m_window.draw(m_infoWindowText);
        }
    }
}

// Add the getter and setter implementations in renderer.cpp:
int Renderer::getCivicScrollOffset() const {
    return m_civicScrollOffset;
}

int Renderer::getMaxCivicScrollOffset() const {
    return m_maxCivicScrollOffset;
}

void Renderer::setCivicScrollOffset(int offset) {
    m_civicScrollOffset = offset;
}