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

    m_extractorVertices.setPrimitiveType(sf::Quads);
    const auto& resourceGridInit = map.getResourceGrid();
    for (size_t y = 0; y < resourceGridInit.size(); ++y) {
        for (size_t x = 0; x < resourceGridInit[y].size(); ++x) {
            const auto& resources = resourceGridInit[y][x];
            for (const auto& entry : resources) {
                if (entry.first == Resource::Type::FOOD) {
                    continue;
                }
                m_resourceCells.push_back({sf::Vector2i(static_cast<int>(x), static_cast<int>(y)), entry.first});
            }
        }
    }

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

    // Performance optimization: Viewport culling - only draw cities that are visible
    sf::Vector2f viewCenter = m_window.getView().getCenter();
    sf::Vector2f viewSize = m_window.getView().getSize();
    sf::FloatRect visibleArea(viewCenter.x - viewSize.x/2, viewCenter.y - viewSize.y/2, viewSize.x, viewSize.y);

    // Refresh infrastructure overlays before drawing per-country assets
    updateExtractorVertices(map, countries, technologyManager);

    // Draw roads/rails first (so they appear under cities)
    for (const auto& country : countries) {
        drawRoadNetwork(country, map, technologyManager, visibleArea);
    }

    if (m_extractorVertices.getVertexCount() > 0) {
        m_window.draw(m_extractorVertices);
    }

    // Draw cities with viewport culling (on top of infrastructure)
    for (const auto& country : countries) {
        for (const auto& city : country.getCities()) {
            sf::Vector2i cityLocation = city.getLocation();
            sf::Vector2f cityWorldPos(cityLocation.x * map.getGridCellSize(), cityLocation.y * map.getGridCellSize());
            
            // Only draw cities that are visible in the current viewport
            if (visibleArea.contains(cityWorldPos)) {
                sf::RectangleShape cityShape(sf::Vector2f(3, 3));
                
                // 🏙️ MAJOR CITY SYSTEM: Gold squares for major cities, black for regular cities
                if (city.isMajorCity()) {
                    cityShape.setFillColor(sf::Color::Yellow); // Gold square for major cities
                } else {
                    cityShape.setFillColor(sf::Color::Black); // Black square for regular cities
                }
                
                cityShape.setPosition(cityWorldPos.x - 1, cityWorldPos.y - 1);
                m_window.draw(cityShape);
            }
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

    // NUCLEAR OPTIMIZATION: Show performance mode indicator
    extern bool turboMode; // Access from main.cpp
    if (turboMode) {
        sf::Text turboText;
        turboText.setFont(m_font);
        turboText.setCharacterSize(28);
        turboText.setFillColor(sf::Color::Red);
        turboText.setString("🚀 TURBO MODE - 10 YEARS/SEC");
        turboText.setPosition(10, static_cast<float>(m_window.getSize().y - 100));
        m_window.draw(turboText);
    }

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
    // 🎨 REDESIGNED COUNTRY INFO PANEL - Clean and organized!
    sf::Vector2u windowSize = m_window.getSize();
    
    // Larger, better positioned window
    m_infoWindowBackground.setSize(sf::Vector2f(500, 600));
    m_infoWindowBackground.setPosition((windowSize.x - 500) / 2, (windowSize.y - 600) / 2);
    m_infoWindowBackground.setFillColor(sf::Color(20, 20, 30, 220)); // Darker, more professional
    m_infoWindowBackground.setOutlineColor(sf::Color(100, 100, 150));
    m_infoWindowBackground.setOutlineThickness(2);

    m_infoWindowText.setFont(m_font);
    m_window.draw(m_infoWindowBackground);

    float startX = m_infoWindowBackground.getPosition().x + 20;
    float startY = m_infoWindowBackground.getPosition().y + 20;
    float lineHeight = 25;
    float currentY = startY;

    // 🏛️ COUNTRY HEADER - Name and Color
    sf::Text headerText;
    headerText.setFont(m_font);
    headerText.setCharacterSize(24);
    headerText.setFillColor(sf::Color::Yellow);
    headerText.setString(country->getName());
    headerText.setPosition(startX + 30, currentY);
    m_window.draw(headerText);

    // Color square next to name
    m_infoWindowColorSquare.setFillColor(country->getColor());
    m_infoWindowColorSquare.setSize(sf::Vector2f(20, 20));
    m_infoWindowColorSquare.setPosition(startX, currentY + 2);
    m_window.draw(m_infoWindowColorSquare);
    
    currentY += 35;

    // BASIC INFO SECTION
    sf::Text sectionText;
    sectionText.setFont(m_font);
    sectionText.setCharacterSize(18);
    sectionText.setFillColor(sf::Color(150, 200, 255));
    sectionText.setString("=== BASIC INFO ===");
    sectionText.setPosition(startX, currentY);
    m_window.draw(sectionText);
    currentY += 30;

    // Calculate total pixels owned
    int totalPixels = country->getBoundaryPixels().size();

    // Basic info with better formatting
    sf::Text infoText;
    infoText.setFont(m_font);
    infoText.setCharacterSize(16);
    infoText.setFillColor(sf::Color::White);
    
    std::string basicInfo;
    basicInfo += "Population: " + std::to_string(country->getPopulation()) + "\n";
    basicInfo += "Territory: " + std::to_string(totalPixels) + " pixels\n";
    basicInfo += "Cities: " + std::to_string(country->getCities().size()) + "\n";
    basicInfo += "Gold: " + std::to_string(static_cast<int>(country->getGold())) + "\n";
    
    // Country type
    basicInfo += "Type: ";
    switch (country->getType()) {
        case Country::Type::Warmonger: basicInfo += "Warmonger\n"; break;
        case Country::Type::Pacifist: basicInfo += "Pacifist\n"; break;
        case Country::Type::Trader: basicInfo += "Trader\n"; break;
    }
    
    // Science type
    basicInfo += "Science Type: ";
    switch (country->getScienceType()) {
        case Country::ScienceType::NS: basicInfo += "Normal Science\n"; break;
        case Country::ScienceType::LS: basicInfo += "Less Science\n"; break;
        case Country::ScienceType::MS: basicInfo += "More Science\n"; break;
    }
    
    // Culture type
    basicInfo += "Culture Type: ";
    switch (country->getCultureType()) {
        case Country::CultureType::NC: basicInfo += "Normal Culture\n"; break;
        case Country::CultureType::LC: basicInfo += "Less Culture\n"; break;
        case Country::CultureType::MC: basicInfo += "More Culture\n"; break;
    }
    
    // IDEOLOGY - Prominently displayed
    basicInfo += "Ideology: " + country->getIdeologyString() + "\n";
    
    infoText.setString(basicInfo);
    infoText.setPosition(startX, currentY);
    m_window.draw(infoText);
    currentY += 200;

    // SCIENCE & CULTURE SECTION
    sectionText.setString("=== DEVELOPMENT ===");
    sectionText.setPosition(startX, currentY);
    m_window.draw(sectionText);
    currentY += 30;

    std::string devInfo;
    devInfo += "Science Points: " + std::to_string(static_cast<int>(country->getSciencePoints())) + "\n";
    devInfo += "Culture Points: " + std::to_string(static_cast<int>(country->getCulturePoints())) + "\n";
    devInfo += "Military Strength: " + std::to_string(static_cast<int>(country->getMilitaryStrength() * 100)) + "%\n";
    
    infoText.setString(devInfo);
    infoText.setPosition(startX, currentY);
    m_window.draw(infoText);
    currentY += 80;

    // TECHNOLOGIES SECTION
    sectionText.setString("=== TECHNOLOGIES ===");
    sectionText.setPosition(startX, currentY);
    m_window.draw(sectionText);
    currentY += 30;

    // Draw scrollable technology list
    drawTechList(country, techManager, startX, currentY, 460, 180); // Custom position and size
}

void Renderer::drawTechList(const Country* country, const TechnologyManager& techManager, float x, float y, float width, float height) {
    const std::vector<int>& unlockedTechs = techManager.getUnlockedTechnologies(*country);
    const std::unordered_map<int, Technology>& allTechs = techManager.getTechnologies();

    // 📜 SCROLLABLE TECHNOLOGY LIST
    float yIncrement = 18;
    float totalHeight = unlockedTechs.size() * yIncrement;

    // Calculate scroll bounds
    m_maxTechScrollOffset = 0;
    if (totalHeight > height) {
        m_maxTechScrollOffset = static_cast<int>(totalHeight - height);
        if (m_techScrollOffset > m_maxTechScrollOffset) {
            m_techScrollOffset = m_maxTechScrollOffset;
        }
    } else {
        m_techScrollOffset = 0;
    }

    // Draw scrollable background
    sf::RectangleShape techListBg(sf::Vector2f(width, height));
    techListBg.setPosition(x, y);
    techListBg.setFillColor(sf::Color(40, 40, 50, 180));
    techListBg.setOutlineColor(sf::Color(80, 80, 100));
    techListBg.setOutlineThickness(1);
    m_window.draw(techListBg);

    // Draw scroll indicator if needed
    if (totalHeight > height) {
        sf::Text scrollHint;
        scrollHint.setFont(m_font);
        scrollHint.setCharacterSize(12);
        scrollHint.setFillColor(sf::Color(200, 200, 200));
        scrollHint.setString("↑↓ Shift+Scroll to navigate");
        scrollHint.setPosition(x + width - 150, y + height - 15);
        m_window.draw(scrollHint);
    }

    // Draw each technology item
    sf::Text techText;
    techText.setFont(m_font);
    techText.setCharacterSize(14);
    techText.setFillColor(sf::Color(200, 255, 200)); // Light green for techs
    
    for (size_t i = 0; i < unlockedTechs.size(); ++i) {
        int techId = unlockedTechs[i];
        float yPos = y + 10 + (i * yIncrement) - m_techScrollOffset;

        // Only draw if within visible area
        if (yPos + yIncrement > y && yPos < y + height) {
            techText.setString("- " + allTechs.at(techId).name);
            techText.setPosition(x + 10, yPos);
            m_window.draw(techText);
        }
    }
    
    // Show tech count
    sf::Text techCount;
    techCount.setFont(m_font);
    techCount.setCharacterSize(12);
    techCount.setFillColor(sf::Color(150, 150, 150));
    techCount.setString("(" + std::to_string(unlockedTechs.size()) + " technologies)");
    techCount.setPosition(x + 10, y + height + 5);
    m_window.draw(techCount);
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
    // Performance optimization: Only update dirty regions instead of entire image
    const auto& dirtyRegions = map.getDirtyRegions();
    if (dirtyRegions.empty()) {
        return; // No updates needed
    }

    int gridCellSize = map.getGridCellSize();
    int regionSize = map.getRegionSize();
    
    // Clear only the dirty regions instead of the entire image
    for (int regionIndex : dirtyRegions) {
        int regionsPerRow = static_cast<int>(m_countryImage.getSize().x) / (gridCellSize * regionSize);
        int regionY = regionIndex / regionsPerRow;
        int regionX = regionIndex % regionsPerRow;
        
        int startX = regionX * regionSize;
        int startY = regionY * regionSize;
        int endX = std::min(startX + regionSize, static_cast<int>(countryGrid[0].size()));
        int endY = std::min(startY + regionSize, static_cast<int>(countryGrid.size()));
        
        // Only update pixels in the dirty region
        for (int y = startY; y < endY; ++y) {
            for (int x = startX; x < endX; ++x) {
                if (y >= 0 && y < static_cast<int>(countryGrid.size()) && 
                    x >= 0 && x < static_cast<int>(countryGrid[0].size())) {
                    
                    int countryIndex = countryGrid[y][x];
                    sf::Color pixelColor = (countryIndex != -1) ? countries[countryIndex].getColor() : sf::Color::Transparent;

                    // Set all pixels in the grid cell at once instead of pixel by pixel
                    for (int i = 0; i < gridCellSize; ++i) {
                        for (int j = 0; j < gridCellSize; ++j) {
                            int pixelX = x * gridCellSize + i;
                            int pixelY = y * gridCellSize + j;
                            if (pixelX >= 0 && pixelX < static_cast<int>(m_countryImage.getSize().x) && 
                                pixelY >= 0 && pixelY < static_cast<int>(m_countryImage.getSize().y)) {
                                m_countryImage.setPixel(pixelX, pixelY, pixelColor);
                            }
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

void Renderer::renderMegaTimeJumpScreen(const std::string& inputText, const sf::Font& font) {
    // Clear with a solid dark background (no game world underneath)
    m_window.clear(sf::Color(20, 20, 20));
    
    // Reset view for GUI
    m_window.setView(m_window.getDefaultView());
    
    // Dark overlay (optional, since we already have dark background)
    sf::RectangleShape overlay(sf::Vector2f(m_window.getSize().x, m_window.getSize().y));
    overlay.setFillColor(sf::Color(0, 0, 0, 200));
    m_window.draw(overlay);
    
    // Input box background
    sf::RectangleShape inputBox(sf::Vector2f(600, 300));
    inputBox.setPosition(m_window.getSize().x / 2 - 300, m_window.getSize().y / 2 - 150);
    inputBox.setFillColor(sf::Color(40, 40, 40));
    inputBox.setOutlineColor(sf::Color::Yellow);
    inputBox.setOutlineThickness(3);
    m_window.draw(inputBox);
    
    // Title text
    sf::Text titleText;
    titleText.setFont(font);
    titleText.setCharacterSize(36);
    titleText.setFillColor(sf::Color::Yellow);
    titleText.setString("MEGA TIME JUMP");
    titleText.setPosition(m_window.getSize().x / 2 - 150, m_window.getSize().y / 2 - 130);
    m_window.draw(titleText);
    
    // Instructions
    sf::Text instructionText;
    instructionText.setFont(font);
    instructionText.setCharacterSize(20);
    instructionText.setFillColor(sf::Color::White);
    instructionText.setString("Enter target year (-5000 to 2025):");
    instructionText.setPosition(m_window.getSize().x / 2 - 150, m_window.getSize().y / 2 - 60);
    m_window.draw(instructionText);
    
    // Input text
    sf::Text inputTextDisplay;
    inputTextDisplay.setFont(font);
    inputTextDisplay.setCharacterSize(24);
    inputTextDisplay.setFillColor(sf::Color::Cyan);
    inputTextDisplay.setString(inputText + "_"); // Add cursor
    inputTextDisplay.setPosition(m_window.getSize().x / 2 - 100, m_window.getSize().y / 2 - 20);
    m_window.draw(inputTextDisplay);
    
    // Controls text
    sf::Text controlsText;
    controlsText.setFont(font);
    controlsText.setCharacterSize(16);
    controlsText.setFillColor(sf::Color(200, 200, 200));
    controlsText.setString("Press ENTER to jump, ESC to cancel");
    controlsText.setPosition(m_window.getSize().x / 2 - 120, m_window.getSize().y / 2 + 60);
    m_window.draw(controlsText);
    
    m_window.display();
}

void Renderer::renderCountryAddEditor(const std::string& inputText, int editorState, int maxTechId, int maxCultureId, const sf::Font& font) {
    // Clear with a solid dark background
    m_window.clear(sf::Color(15, 15, 15));
    
    // Reset view for GUI
    m_window.setView(m_window.getDefaultView());
    
    // Main window background
    sf::RectangleShape mainBox(sf::Vector2f(700, 500));
    mainBox.setPosition(m_window.getSize().x / 2 - 350, m_window.getSize().y / 2 - 250);
    mainBox.setFillColor(sf::Color(40, 40, 40));
    mainBox.setOutlineColor(sf::Color::Green);
    mainBox.setOutlineThickness(3);
    m_window.draw(mainBox);
    
    // Title text
    sf::Text titleText;
    titleText.setFont(font);
    titleText.setCharacterSize(32);
    titleText.setFillColor(sf::Color::Green);
    titleText.setString("🏗️ COUNTRY ADD EDITOR");
    titleText.setPosition(m_window.getSize().x / 2 - 180, m_window.getSize().y / 2 - 230);
    m_window.draw(titleText);
    
    std::string stateText;
    std::string instructionText;
    
    switch (editorState) {
        case 0: // Technology selection
            stateText = "1. Select Technologies (1-" + std::to_string(maxTechId) + ")";
            instructionText = "Enter tech IDs (e.g., 1,5,12) or type 'all' to unlock everything";
            break;
        case 1: // Population input
            stateText = "2. Set Starting Population";
            instructionText = "Enter population number (e.g., 50000)";
            break;
        case 2: // Culture selection
            stateText = "3. Select Cultures (1-" + std::to_string(maxCultureId) + ")";
            instructionText = "Enter culture IDs (e.g., 1,3,7) or type 'all' to unlock everything";
            break;
        case 3: // Country type
            stateText = "4. Choose Country Type";
            instructionText = "1=Warmonger, 2=Pacifist, 3=Trader";
            break;
        case 4: // Science type
            stateText = "5. Choose Science Type";
            instructionText = "1=Normal Science, 2=Less Science, 3=More Science";
            break;
        case 5: // Culture type
            stateText = "6. Choose Culture Type";
            instructionText = "1=Normal Culture, 2=Less Culture, 3=More Culture";
            break;
        case 6: // Save/Reset
            stateText = "7. Save or Reset";
            instructionText = "1=Save Template, 2=Reset to Random";
            break;
    }
    
    // State text
    sf::Text stateDisplay;
    stateDisplay.setFont(font);
    stateDisplay.setCharacterSize(24);
    stateDisplay.setFillColor(sf::Color::Yellow);
    stateDisplay.setString(stateText);
    stateDisplay.setPosition(m_window.getSize().x / 2 - 300, m_window.getSize().y / 2 - 170);
    m_window.draw(stateDisplay);
    
    // Instruction text
    sf::Text instruction;
    instruction.setFont(font);
    instruction.setCharacterSize(18);
    instruction.setFillColor(sf::Color::White);
    instruction.setString(instructionText);
    instruction.setPosition(m_window.getSize().x / 2 - 300, m_window.getSize().y / 2 - 120);
    m_window.draw(instruction);
    
    // Input box
    sf::RectangleShape inputBox(sf::Vector2f(600, 50));
    inputBox.setPosition(m_window.getSize().x / 2 - 300, m_window.getSize().y / 2 - 70);
    inputBox.setFillColor(sf::Color(60, 60, 60));
    inputBox.setOutlineColor(sf::Color::Cyan);
    inputBox.setOutlineThickness(2);
    m_window.draw(inputBox);
    
    // Input text
    sf::Text input;
    input.setFont(font);
    input.setCharacterSize(20);
    input.setFillColor(sf::Color::Cyan);
    input.setString(inputText.empty() ? "_" : inputText + "_");
    input.setPosition(m_window.getSize().x / 2 - 290, m_window.getSize().y / 2 - 60);
    m_window.draw(input);
    
    // Help text
    sf::Text helpText;
    helpText.setFont(font);
    helpText.setCharacterSize(16);
    helpText.setFillColor(sf::Color(200, 200, 200));
    helpText.setString("Press ENTER to continue | ESC to cancel\nStep " + std::to_string(editorState + 1) + " of 7");
    helpText.setPosition(m_window.getSize().x / 2 - 150, m_window.getSize().y / 2 + 50);
    m_window.draw(helpText);
    
    // Progress indicator
    float progressWidth = 500.0f;
    float progressHeight = 10.0f;
    float progressFill = (static_cast<float>(editorState + 1) / 7.0f) * progressWidth;
    
    sf::RectangleShape progressBg(sf::Vector2f(progressWidth, progressHeight));
    progressBg.setPosition(m_window.getSize().x / 2 - progressWidth / 2, m_window.getSize().y / 2 + 150);
    progressBg.setFillColor(sf::Color(80, 80, 80));
    m_window.draw(progressBg);
    
    sf::RectangleShape progressBar(sf::Vector2f(progressFill, progressHeight));
    progressBar.setPosition(m_window.getSize().x / 2 - progressWidth / 2, m_window.getSize().y / 2 + 150);
    progressBar.setFillColor(sf::Color::Green);
    m_window.draw(progressBar);
    
    m_window.display();
}

void Renderer::updateExtractorVertices(const Map& map, const std::vector<Country>& countries, const TechnologyManager& technologyManager) {
    m_extractorVertices.clear();
    m_extractorVertices.setPrimitiveType(sf::Quads);

    if (m_resourceCells.empty()) {
        return;
    }

    const auto& countryGrid = map.getCountryGrid();
    const auto& resourceGrid = map.getResourceGrid();
    float cellSize = static_cast<float>(map.getGridCellSize());

    for (const auto& cell : m_resourceCells) {
        int x = cell.position.x;
        int y = cell.position.y;

        if (y < 0 || y >= static_cast<int>(countryGrid.size())) {
            continue;
        }
        if (x < 0 || x >= static_cast<int>(countryGrid[y].size())) {
            continue;
        }

        int ownerIndex = countryGrid[y][x];
        if (ownerIndex < 0) {
            continue;
        }

        const Country* owner = nullptr;
        if (ownerIndex < static_cast<int>(countries.size()) && countries[ownerIndex].getCountryIndex() == ownerIndex) {
            owner = &countries[ownerIndex];
        } else {
            for (const auto& candidate : countries) {
                if (candidate.getCountryIndex() == ownerIndex) {
                    owner = &candidate;
                    break;
                }
            }
        }

        if (!owner) {
            continue;
        }

        int requiredTech = getExtractorUnlockTech(cell.type);
        if (requiredTech != 0 && !TechnologyManager::hasTech(technologyManager, *owner, requiredTech)) {
            continue;
        }

        const auto& resourcesInCell = resourceGrid[y][x];
        auto it = resourcesInCell.find(cell.type);
        if (it == resourcesInCell.end() || it->second <= 0.0) {
            continue;
        }

        sf::Color glyphColor = getExtractorColor(cell.type);
        float worldX = static_cast<float>(x) * cellSize;
        float worldY = static_cast<float>(y) * cellSize;
        float glyphSize = std::max(0.6f, cellSize * 0.6f);
        float offset = (cellSize - glyphSize) / 2.f;

        sf::Vertex v0(sf::Vector2f(worldX + offset, worldY + offset), glyphColor);
        sf::Vertex v1(sf::Vector2f(worldX + offset + glyphSize, worldY + offset), glyphColor);
        sf::Vertex v2(sf::Vector2f(worldX + offset + glyphSize, worldY + offset + glyphSize), glyphColor);
        sf::Vertex v3(sf::Vector2f(worldX + offset, worldY + offset + glyphSize), glyphColor);

        m_extractorVertices.append(v0);
        m_extractorVertices.append(v1);
        m_extractorVertices.append(v2);
        m_extractorVertices.append(v3);
    }
}

sf::Color Renderer::getExtractorColor(Resource::Type type) const {
    switch (type) {
        case Resource::Type::GOLD:
            return sf::Color(255, 215, 0, 220);
        case Resource::Type::IRON:
            return sf::Color(190, 190, 190, 220);
        case Resource::Type::COAL:
            return sf::Color(60, 60, 60, 220);
        case Resource::Type::HORSES:
            return sf::Color(176, 121, 66, 220);
        case Resource::Type::SALT:
            return sf::Color(210, 230, 255, 220);
        default:
            return sf::Color(255, 255, 255, 200);
    }
}

int Renderer::getExtractorUnlockTech(Resource::Type type) const {
    switch (type) {
        case Resource::Type::IRON:
            return 13; // Iron Working
        case Resource::Type::COAL:
            return 55; // Railroad
        case Resource::Type::GOLD:
            return 34; // Banking
        case Resource::Type::HORSES:
            return 18; // Horseback Riding
        case Resource::Type::SALT:
            return 16; // Construction
        default:
            return 0;
    }
}

void Renderer::drawRoadNetwork(const Country& country, const Map& map, const TechnologyManager& technologyManager, const sf::FloatRect& visibleArea) {
    const auto& roads = country.getRoads();
    if (roads.empty()) {
        return;
    }

    bool hasRail = TechnologyManager::hasTech(technologyManager, country, 55);
    float cellSize = static_cast<float>(map.getGridCellSize());

    sf::RectangleShape roadSurface(sf::Vector2f(cellSize, cellSize));
    roadSurface.setFillColor(sf::Color(105, 100, 90, 205));

    float highlightSize = std::max(0.6f, cellSize * 0.5f);
    sf::RectangleShape roadHighlight(sf::Vector2f(highlightSize, highlightSize));
    roadHighlight.setOrigin(highlightSize / 2.f, highlightSize / 2.f);
    sf::Color highlightColor = country.getColor();
    highlightColor.a = 200;
    roadHighlight.setFillColor(highlightColor);

    sf::RectangleShape railBase(sf::Vector2f(cellSize, cellSize));
    railBase.setFillColor(sf::Color(75, 78, 90, 220));

    float glowSize = std::max(0.7f, cellSize * 0.35f);
    sf::RectangleShape railGlow(sf::Vector2f(glowSize, glowSize));
    railGlow.setOrigin(glowSize / 2.f, glowSize / 2.f);
    sf::Color glowColor = highlightColor;
    glowColor.r = static_cast<sf::Uint8>(std::min(255, glowColor.r + 80));
    glowColor.g = static_cast<sf::Uint8>(std::min(255, glowColor.g + 80));
    glowColor.b = static_cast<sf::Uint8>(std::min(255, glowColor.b + 40));
    glowColor.a = 230;
    railGlow.setFillColor(glowColor);

    for (const auto& roadPixel : roads) {
        sf::Vector2f roadWorldPos(roadPixel.x * cellSize, roadPixel.y * cellSize);
        sf::FloatRect roadBounds(roadWorldPos.x, roadWorldPos.y, cellSize, cellSize);
        if (!visibleArea.intersects(roadBounds)) {
            continue;
        }

        if (hasRail) {
            railBase.setPosition(roadWorldPos);
            m_window.draw(railBase);

            railGlow.setPosition(roadWorldPos.x + cellSize / 2.f, roadWorldPos.y + cellSize / 2.f);
            m_window.draw(railGlow);
        } else {
            roadSurface.setPosition(roadWorldPos);
            m_window.draw(roadSurface);

            roadHighlight.setPosition(roadWorldPos.x + cellSize / 2.f, roadWorldPos.y + cellSize / 2.f);
            m_window.draw(roadHighlight);
        }
    }
}
