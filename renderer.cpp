// renderer.cpp

#include "renderer.h"
#include "trade.h"
#include <string>
#include <algorithm>
#include <cmath>

extern bool turboMode;
extern bool paused;

namespace {
const char* kCountryOverlayFragmentShader = R"(
uniform sampler2D palette;
uniform float paletteSize;

void main()
{
    vec4 encoded = texture2D(texture, gl_TexCoord[0].xy);
    float low = floor(encoded.r * 255.0 + 0.5);
    float high = floor(encoded.g * 255.0 + 0.5);
    float id = low + high * 256.0; // 0 = empty, otherwise countryIndex + 1

    if (id < 0.5) {
        gl_FragColor = vec4(0.0);
        return;
    }

    if (id > paletteSize - 0.5) {
        gl_FragColor = vec4(0.0);
        return;
    }

    float u = (id + 0.5) / paletteSize;
    gl_FragColor = texture2D(palette, vec2(u, 0.5));
}
)";
} // namespace

Renderer::Renderer(sf::RenderWindow& window, const Map& map, const sf::Color& waterColor) :
    m_window(window),
    m_waterColor(waterColor),
    m_useGpuCountryOverlay(false),
    m_countryGridWidth(0),
    m_countryGridHeight(0),
    m_countryPaletteSize(0),
    m_needsUpdate(true),
    m_showWarmongerHighlights(false),
    m_showWarHighlights(false),
    m_showCountryAddModeText(false),
    m_showPaintHud(false),
    m_paintHudText(""),
    m_currentYear(0),
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

    const auto& countryGridInit = map.getCountryGrid();
    if (!countryGridInit.empty() && !countryGridInit[0].empty()) {
        m_countryGridHeight = static_cast<unsigned int>(countryGridInit.size());
        m_countryGridWidth = static_cast<unsigned int>(countryGridInit[0].size());
    }

    if (sf::Shader::isAvailable() && m_countryGridWidth > 0 && m_countryGridHeight > 0) {
        if (m_countryOverlayShader.loadFromMemory(kCountryOverlayFragmentShader, sf::Shader::Fragment)) {
            m_useGpuCountryOverlay = true;

            m_countryIdTexture.create(m_countryGridWidth, m_countryGridHeight);
            m_countryIdTexture.setSmooth(false);
            {
                std::vector<sf::Uint8> clearPixels(static_cast<size_t>(m_countryGridWidth) * static_cast<size_t>(m_countryGridHeight) * 4u, 0);
                for (size_t i = 0; i < clearPixels.size(); i += 4) {
                    clearPixels[i + 3] = 255;
                }
                m_countryIdTexture.update(clearPixels.data());
            }
            m_countryIdSprite.setTexture(m_countryIdTexture, true);
            m_countryIdSprite.setPosition(0.f, 0.f);
            m_countryIdSprite.setScale(static_cast<float>(map.getGridCellSize()), static_cast<float>(map.getGridCellSize()));

            m_countryPaletteSize = 1;
            m_countryPaletteTexture.create(1, 1);
            m_countryPaletteTexture.setSmooth(false);
            sf::Uint8 transparentPixel[4] = {0, 0, 0, 0};
            m_countryPaletteTexture.update(transparentPixel);
            m_countryOverlayShader.setUniform("palette", m_countryPaletteTexture);
            m_countryOverlayShader.setUniform("paletteSize", static_cast<float>(m_countryPaletteSize));
        }
    }

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

    if (m_factoryTexture.loadFromFile("factory.png")) {
        sf::Vector2u texSize = m_factoryTexture.getSize();
        m_factorySprite.setTexture(m_factoryTexture);
        m_factorySprite.setOrigin(static_cast<float>(texSize.x) / 2.f, static_cast<float>(texSize.y) / 2.f);
        float targetSize = std::max(8.f, static_cast<float>(map.getGridCellSize()) * 12.f);
        float maxDimension = static_cast<float>(std::max(texSize.x, texSize.y));
        if (maxDimension > 0.f) {
            float scale = targetSize / maxDimension;
            m_factorySprite.setScale(scale, scale);
        }
    }


    // Initialize country info window elements
    m_infoWindowBackground.setFillColor(sf::Color(0, 0, 0, 175));
    m_infoWindowBackground.setSize(sf::Vector2f(400, 350));

    m_infoWindowText.setCharacterSize(16);
    m_infoWindowText.setFillColor(sf::Color::White);

    m_infoWindowColorSquare.setSize(sf::Vector2f(20, 20));
}

void Renderer::render(const std::vector<Country>& countries, const Map& map, News& news, const TechnologyManager& technologyManager, const CultureManager& cultureManager, const TradeManager& tradeManager, const Country* selectedCountry, bool showCountryInfo) {
    sf::View worldView = m_window.getView();

    m_window.clear();

    m_window.draw(m_baseSprite); // Draw the base map (map.png)

    if (m_needsUpdate) {
        updateCountryImage(map.getCountryGrid(), countries, map);
        if (!m_useGpuCountryOverlay) {
            m_countryTexture.update(m_countryImage);
        }
        m_needsUpdate = false;
    }

    if (m_useGpuCountryOverlay) {
        sf::RenderStates states;
        states.shader = &m_countryOverlayShader;
        m_window.draw(m_countryIdSprite, states);
    } else {
        m_window.draw(m_countrySprite); // Draw the countries
    }

    // Performance optimization: Viewport culling - only draw cities that are visible
    sf::Vector2f viewCenter = worldView.getCenter();
    sf::Vector2f viewSize = worldView.getSize();
    sf::FloatRect visibleArea(viewCenter.x - viewSize.x / 2.f, viewCenter.y - viewSize.y / 2.f, viewSize.x, viewSize.y);

    drawPlagueOverlay(map, countries, visibleArea);

    // Refresh infrastructure overlays before drawing per-country assets
    updateExtractorVertices(map, countries, technologyManager);

    // Draw roads/rails first (so they appear under cities)
    for (const auto& country : countries) {
        drawRoadNetwork(country, map, technologyManager, visibleArea);
    }

    drawTradeRoutes(tradeManager, countries, map, visibleArea);

    if (m_extractorVertices.getVertexCount() > 0) {
        m_window.draw(m_extractorVertices);
    }

    drawFactories(countries, map, visibleArea);

    // Draw cities with viewport culling (on top of infrastructure)
    for (const auto& country : countries) {
        if (country.getPopulation() <= 0 || country.getCities().empty()) {
            continue;
        }

        sf::Vector2i capitalLocation = country.getCapitalLocation();
        double popPerCity = static_cast<double>(country.getPopulation()) / std::max<size_t>(1, country.getCities().size());
        float densityScale = 0.0f;
        if (popPerCity > 0.0) {
            densityScale = static_cast<float>(std::min(1.0, std::max(0.0, (std::log10(popPerCity + 1.0) - 3.0) / 3.0)));
        }

        for (const auto& city : country.getCities()) {
            sf::Vector2i cityLocation = city.getLocation();
            sf::Vector2f cityCenter((static_cast<float>(cityLocation.x) + 0.5f) * map.getGridCellSize(),
                                    (static_cast<float>(cityLocation.y) + 0.5f) * map.getGridCellSize());

            // Only draw cities that are visible in the current viewport
            if (!visibleArea.contains(cityCenter)) {
                continue;
            }

            float baseSize = city.isMajorCity() ? std::max(3.0f, map.getGridCellSize() * 3.2f)
                                                : std::max(2.0f, map.getGridCellSize() * 2.0f);
            float citySize = baseSize + densityScale * 3.0f;
            float glowSize = citySize + 2.0f + densityScale * 2.0f;

            sf::Color glowColor = city.isMajorCity() ? sf::Color(255, 210, 120, 70)
                                                     : sf::Color(120, 120, 120, 40);
            sf::RectangleShape cityGlow(sf::Vector2f(glowSize, glowSize));
            cityGlow.setOrigin(glowSize / 2.0f, glowSize / 2.0f);
            cityGlow.setPosition(cityCenter);
            cityGlow.setFillColor(glowColor);
            m_window.draw(cityGlow);

            sf::RectangleShape cityShape(sf::Vector2f(citySize, citySize));
            cityShape.setOrigin(citySize / 2.0f, citySize / 2.0f);

            if (city.isMajorCity()) {
                cityShape.setFillColor(sf::Color(255, 210, 80));
            } else {
                cityShape.setFillColor(sf::Color(20, 20, 20));
            }

            cityShape.setPosition(cityCenter);
            m_window.draw(cityShape);

            if (cityLocation == capitalLocation) {
                float markerSize = std::max(1.2f, map.getGridCellSize() * 1.2f);
                float clusterRadius = citySize + 2.0f + densityScale * 2.0f;
                sf::RectangleShape marker(sf::Vector2f(markerSize, markerSize));
                marker.setOrigin(markerSize / 2.0f, markerSize / 2.0f);
                marker.setFillColor(sf::Color(255, 230, 160, 200));

                marker.setPosition(cityCenter.x + clusterRadius, cityCenter.y);
                m_window.draw(marker);

                marker.setPosition(cityCenter.x - clusterRadius * 0.7f, cityCenter.y + clusterRadius * 0.6f);
                m_window.draw(marker);

                marker.setPosition(cityCenter.x - clusterRadius * 0.7f, cityCenter.y - clusterRadius * 0.6f);
                m_window.draw(marker);

                sf::CircleShape ring(clusterRadius);
                ring.setOrigin(clusterRadius, clusterRadius);
                ring.setPosition(cityCenter);
                ring.setFillColor(sf::Color::Transparent);
                ring.setOutlineColor(sf::Color(255, 220, 120, 120));
                ring.setOutlineThickness(1.0f);
                m_window.draw(ring);
            }
        }
    }

    // Draw Warmonger highlights if the flag is true
    if (m_showWarmongerHighlights) {
        drawWarmongerHighlights(countries, map);
    }

    // Draw war highlights if the flag is true
    if (m_showWarHighlights) {
        drawWarFrontlines(countries, map, visibleArea);
        drawWarHighlights(countries, map);
    }

    // Always show a primary-target war arrow when a country is at war.
    drawWarArrows(countries, map, visibleArea);

    m_window.setView(m_window.getDefaultView());

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
    if (turboMode) {
        sf::Text turboText;
        turboText.setFont(m_font);
        turboText.setCharacterSize(28);
        turboText.setFillColor(sf::Color::Red);
        turboText.setString("🚀 TURBO MODE - 10 YEARS/SEC");
        turboText.setPosition(10, static_cast<float>(m_window.getSize().y - 100));
        m_window.draw(turboText);
    }

    if (m_showPaintHud && !m_paintHudText.empty()) {
        float y = 10.0f;
        if (m_showCountryAddModeText) {
            y += 30.0f;
        }

        sf::Text paintHud;
        paintHud.setFont(m_font);
        paintHud.setCharacterSize(18);
        paintHud.setFillColor(sf::Color::White);
        paintHud.setString(m_paintHudText);
        paintHud.setPosition(10.0f, y);

        sf::FloatRect bounds = paintHud.getLocalBounds();
        sf::RectangleShape bg(sf::Vector2f(bounds.width + 20.0f, bounds.height + 14.0f));
        bg.setPosition(paintHud.getPosition().x - 10.0f, paintHud.getPosition().y - 7.0f);
        bg.setFillColor(sf::Color(0, 0, 0, 140));
        bg.setOutlineColor(sf::Color(200, 200, 200, 160));
        bg.setOutlineThickness(1.0f);

        m_window.draw(bg);
        m_window.draw(paintHud);
    }

    if (paused) {
        sf::Text pausedText;
        pausedText.setFont(m_font);
        pausedText.setCharacterSize(36);
        pausedText.setFillColor(sf::Color(255, 255, 0));
        pausedText.setString("PAUSED (Space to resume)");

        sf::FloatRect bounds = pausedText.getLocalBounds();
        pausedText.setOrigin(bounds.left + bounds.width / 2.0f, bounds.top + bounds.height / 2.0f);
        pausedText.setPosition(static_cast<float>(m_window.getSize().x) / 2.0f, 70.0f);

        sf::RectangleShape bg(sf::Vector2f(bounds.width + 30.0f, bounds.height + 18.0f));
        bg.setOrigin((bounds.width + 30.0f) / 2.0f, (bounds.height + 18.0f) / 2.0f);
        bg.setPosition(pausedText.getPosition());
        bg.setFillColor(sf::Color(0, 0, 0, 160));
        bg.setOutlineColor(sf::Color(255, 255, 0, 200));
        bg.setOutlineThickness(2.0f);

        m_window.draw(bg);
        m_window.draw(pausedText);
    }

    // Render the news window if it's toggled on
    if (news.isWindowVisible()) {
        news.render(m_window, m_font);
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

    m_window.setView(worldView);
    m_window.display();
}

void Renderer::drawWarArrows(const std::vector<Country>& countries, const Map& map, const sf::FloatRect& visibleArea) {
    const int gridCellSize = map.getGridCellSize();
    if (gridCellSize <= 0) {
        return;
    }

    const float t = m_warArrowClock.getElapsedTime().asSeconds();
    const float pulse = 0.5f + 0.5f * std::sin(t * 2.0f);

    sf::Color outer(120, 0, 0, static_cast<sf::Uint8>(90 + 80 * pulse));
    sf::Color inner(255, 40, 40, static_cast<sf::Uint8>(140 + 90 * pulse));

    const float thickness = std::max(8.0f, static_cast<float>(gridCellSize) * 10.0f);
    const float innerThickness = thickness * 0.55f;

    sf::VertexArray tris(sf::Triangles);
    tris.clear();

    auto addQuad = [&](const sf::Vector2f& a, const sf::Vector2f& b, float w, const sf::Color& c) {
        sf::Vector2f d = b - a;
        float len = std::sqrt(d.x * d.x + d.y * d.y);
        if (len < 0.001f) {
            return;
        }
        sf::Vector2f dir(d.x / len, d.y / len);
        sf::Vector2f perp(-dir.y, dir.x);
        sf::Vector2f off = perp * (w * 0.5f);

        sf::Vector2f p0 = a + off;
        sf::Vector2f p1 = a - off;
        sf::Vector2f p2 = b - off;
        sf::Vector2f p3 = b + off;

        tris.append(sf::Vertex(p0, c));
        tris.append(sf::Vertex(p1, c));
        tris.append(sf::Vertex(p2, c));
        tris.append(sf::Vertex(p0, c));
        tris.append(sf::Vertex(p2, c));
        tris.append(sf::Vertex(p3, c));
    };

    auto addTriangle = [&](const sf::Vector2f& p0, const sf::Vector2f& p1, const sf::Vector2f& p2, const sf::Color& c) {
        tris.append(sf::Vertex(p0, c));
        tris.append(sf::Vertex(p1, c));
        tris.append(sf::Vertex(p2, c));
    };

    for (const auto& country : countries) {
        if (!country.isAtWar()) {
            continue;
        }
        const auto& enemies = country.getEnemies();
        if (enemies.empty() || enemies.front() == nullptr) {
            continue;
        }
        const Country* target = enemies.front();
        if (target->getPopulation() <= 0 || target->getBoundaryPixels().empty()) {
            continue;
        }

        sf::Vector2i aCell = country.getCapitalLocation();
        sf::Vector2i bCell = target->getCapitalLocation();
        sf::Vector2f a((static_cast<float>(aCell.x) + 0.5f) * gridCellSize,
                       (static_cast<float>(aCell.y) + 0.5f) * gridCellSize);
        sf::Vector2f b((static_cast<float>(bCell.x) + 0.5f) * gridCellSize,
                       (static_cast<float>(bCell.y) + 0.5f) * gridCellSize);

        sf::Vector2f d = b - a;
        float len = std::sqrt(d.x * d.x + d.y * d.y);
        if (len < 20.0f) {
            continue;
        }

        float pad = thickness * 0.75f;
        sf::FloatRect bounds(std::min(a.x, b.x) - pad,
                             std::min(a.y, b.y) - pad,
                             std::abs(a.x - b.x) + pad * 2.0f,
                             std::abs(a.y - b.y) + pad * 2.0f);
        if (!bounds.intersects(visibleArea)) {
            continue;
        }

        sf::Vector2f dir(d.x / len, d.y / len);
        float headLen = std::min(90.0f, len * 0.22f);
        float headWidth = thickness * 2.4f;
        sf::Vector2f headBase = b - dir * headLen;

        // Outer arrow (shadow/outline).
        addQuad(a, headBase, thickness, outer);
        sf::Vector2f perp(-dir.y, dir.x);
        sf::Vector2f hw = perp * (headWidth * 0.5f);
        addTriangle(b, headBase + hw, headBase - hw, outer);

        // Inner arrow (brighter).
        addQuad(a, headBase, innerThickness, inner);
        sf::Vector2f ihw = perp * ((headWidth * 0.5f) * 0.75f);
        addTriangle(b, headBase + ihw, headBase - ihw, inner);
    }

    if (tris.getVertexCount() > 0) {
        m_window.draw(tris);
    }
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

    if (m_useGpuCountryOverlay) {
        unsigned int newPaletteSize = 1;
        for (const auto& country : countries) {
            unsigned int idx = static_cast<unsigned int>(std::max(0, country.getCountryIndex())) + 2u;
            newPaletteSize = std::max(newPaletteSize, idx);
        }

        if (newPaletteSize != m_countryPaletteSize) {
            m_countryPaletteSize = newPaletteSize;
            m_countryPaletteTexture.create(m_countryPaletteSize, 1);
            m_countryPaletteTexture.setSmooth(false);
            m_countryPalettePixels.resize(static_cast<size_t>(m_countryPaletteSize) * 4u);
        } else if (m_countryPalettePixels.size() != static_cast<size_t>(m_countryPaletteSize) * 4u) {
            m_countryPalettePixels.resize(static_cast<size_t>(m_countryPaletteSize) * 4u);
        }

        std::fill(m_countryPalettePixels.begin(), m_countryPalettePixels.end(), static_cast<sf::Uint8>(0));
        for (const auto& country : countries) {
            int countryIndex = country.getCountryIndex();
            if (countryIndex < 0) {
                continue;
            }

            unsigned int paletteIndex = static_cast<unsigned int>(countryIndex) + 1u; // 0 reserved for empty/transparent
            if (paletteIndex >= m_countryPaletteSize) {
                continue;
            }

            sf::Color color = country.getColor();
            size_t base = static_cast<size_t>(paletteIndex) * 4u;
            m_countryPalettePixels[base + 0] = color.r;
            m_countryPalettePixels[base + 1] = color.g;
            m_countryPalettePixels[base + 2] = color.b;
            m_countryPalettePixels[base + 3] = color.a;
        }

        m_countryPaletteTexture.update(m_countryPalettePixels.data());
        m_countryOverlayShader.setUniform("palette", m_countryPaletteTexture);
        m_countryOverlayShader.setUniform("paletteSize", static_cast<float>(m_countryPaletteSize));

        int regionsPerRow = regionSize > 0 ? static_cast<int>(m_countryGridWidth) / regionSize : 0;
        if (regionsPerRow <= 0) {
            return;
        }

        for (int regionIndex : dirtyRegions) {
            int regionY = regionIndex / regionsPerRow;
            int regionX = regionIndex % regionsPerRow;

            int startX = regionX * regionSize;
            int startY = regionY * regionSize;
            int endX = std::min(startX + regionSize, static_cast<int>(countryGrid[0].size()));
            int endY = std::min(startY + regionSize, static_cast<int>(countryGrid.size()));

            if (startX >= endX || startY >= endY) {
                continue;
            }

            unsigned int updateW = static_cast<unsigned int>(endX - startX);
            unsigned int updateH = static_cast<unsigned int>(endY - startY);
            m_countryIdUploadPixels.resize(static_cast<size_t>(updateW) * static_cast<size_t>(updateH) * 4u);

            for (unsigned int y = 0; y < updateH; ++y) {
                for (unsigned int x = 0; x < updateW; ++x) {
                    int owner = countryGrid[startY + static_cast<int>(y)][startX + static_cast<int>(x)];
                    unsigned int encodedId = (owner >= 0) ? static_cast<unsigned int>(owner + 1) : 0u;
                    sf::Uint8 lo = static_cast<sf::Uint8>(encodedId & 0xFFu);
                    sf::Uint8 hi = static_cast<sf::Uint8>((encodedId >> 8) & 0xFFu);

                    size_t out = (static_cast<size_t>(y) * static_cast<size_t>(updateW) + static_cast<size_t>(x)) * 4u;
                    m_countryIdUploadPixels[out + 0] = lo;
                    m_countryIdUploadPixels[out + 1] = hi;
                    m_countryIdUploadPixels[out + 2] = 0;
                    m_countryIdUploadPixels[out + 3] = 255;
                }
            }

            m_countryIdTexture.update(
                m_countryIdUploadPixels.data(),
                updateW,
                updateH,
                static_cast<unsigned int>(startX),
                static_cast<unsigned int>(startY));
        }

        m_countryIdSprite.setScale(static_cast<float>(gridCellSize), static_cast<float>(gridCellSize));
        return;
    }

    int maxCountryIndex = -1;
    for (const auto& country : countries) {
        maxCountryIndex = std::max(maxCountryIndex, country.getCountryIndex());
    }

    std::vector<sf::Color> colorByCountryIndex;
    if (maxCountryIndex >= 0) {
        colorByCountryIndex.assign(static_cast<size_t>(maxCountryIndex + 1), sf::Color::Transparent);
        for (const auto& country : countries) {
            int countryIndex = country.getCountryIndex();
            if (countryIndex >= 0 && countryIndex <= maxCountryIndex) {
                colorByCountryIndex[static_cast<size_t>(countryIndex)] = country.getColor();
            }
        }
    }

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
                int countryIndex = countryGrid[y][x];
                sf::Color pixelColor = sf::Color::Transparent;
                if (countryIndex >= 0 && countryIndex < static_cast<int>(colorByCountryIndex.size())) {
                    pixelColor = colorByCountryIndex[static_cast<size_t>(countryIndex)];
                }

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

void Renderer::setPaintHud(bool show, const std::string& text) {
    m_showPaintHud = show;
    m_paintHudText = text;
}

void Renderer::updateYearText(int year) {
    m_currentYear = year;
    if (year < 0) {
        m_yearText.setString("Year: " + std::to_string(-year) + " BCE");
    }
    else {
        m_yearText.setString("Year: " + std::to_string(year) + " CE");
    }
    m_yearText.setPosition(static_cast<float>(m_window.getSize().x) / 2.0f - 100.0f, 20.0f);
}

void Renderer::handleWindowRecreated(const Map& map) {
    m_baseTexture.loadFromImage(map.getBaseImage());
    m_baseSprite.setTexture(m_baseTexture, true);

    m_countryTexture.loadFromImage(m_countryImage);
    m_countrySprite.setTexture(m_countryTexture, true);

    if (m_useGpuCountryOverlay) {
        m_countryIdSprite.setTexture(m_countryIdTexture, true);
        m_countryIdSprite.setPosition(0.f, 0.f);
        m_countryIdSprite.setScale(static_cast<float>(map.getGridCellSize()), static_cast<float>(map.getGridCellSize()));
        m_countryOverlayShader.setUniform("palette", m_countryPaletteTexture);
        m_countryOverlayShader.setUniform("paletteSize", static_cast<float>(m_countryPaletteSize));
    }

    if (m_factoryTexture.loadFromFile("factory.png")) {
        sf::Vector2u texSize = m_factoryTexture.getSize();
        m_factorySprite.setTexture(m_factoryTexture);
        m_factorySprite.setOrigin(static_cast<float>(texSize.x) / 2.f, static_cast<float>(texSize.y) / 2.f);
        float targetSize = std::max(8.f, static_cast<float>(map.getGridCellSize()) * 12.f);
        float maxDimension = static_cast<float>(std::max(texSize.x, texSize.y));
        if (maxDimension > 0.f) {
            float scale = targetSize / maxDimension;
            m_factorySprite.setScale(scale, scale);
        }
    }

    updateYearText(m_currentYear);
}

void Renderer::showLoadingScreen() {
    sf::View previousView = m_window.getView();
    m_window.setView(m_window.getDefaultView());

    sf::Text loadingText;
    loadingText.setFont(m_font);
    loadingText.setCharacterSize(30);
    loadingText.setFillColor(sf::Color::White);
    loadingText.setString("Loading...");
    loadingText.setPosition(m_window.getSize().x / 2 - loadingText.getLocalBounds().width / 2,
        m_window.getSize().y / 2 - loadingText.getLocalBounds().height / 2);

    m_window.clear();
    m_window.draw(loadingText);
    m_window.setView(previousView);
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
    sf::View previousView = m_window.getView();

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

    if (paused) {
        sf::Text pausedText;
        pausedText.setFont(font);
        pausedText.setCharacterSize(24);
        pausedText.setFillColor(sf::Color(255, 255, 0));
        pausedText.setString("PAUSED");
        pausedText.setPosition(10, 10);
        m_window.draw(pausedText);
    }
    
    m_window.setView(previousView);
    m_window.display();
}

void Renderer::renderCountryAddEditor(const std::string& inputText, int editorState, int maxTechId, int maxCultureId, const sf::Font& font) {
    sf::View previousView = m_window.getView();

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

    if (paused) {
        sf::Text pausedText;
        pausedText.setFont(font);
        pausedText.setCharacterSize(24);
        pausedText.setFillColor(sf::Color(255, 255, 0));
        pausedText.setString("PAUSED");
        pausedText.setPosition(10, 10);
        m_window.draw(pausedText);
    }
    
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
    
    m_window.setView(previousView);
    m_window.display();
}

void Renderer::renderTechEditor(const Country& country, const TechnologyManager& techManager, const std::string& inputText, const sf::Font& font) {
    sf::View previousView = m_window.getView();

    m_window.clear(sf::Color(15, 15, 20));
    m_window.setView(m_window.getDefaultView());

    sf::RectangleShape mainBox(sf::Vector2f(820, 560));
    mainBox.setPosition(m_window.getSize().x / 2 - 410, m_window.getSize().y / 2 - 280);
    mainBox.setFillColor(sf::Color(35, 35, 45));
    mainBox.setOutlineColor(sf::Color(255, 140, 60));
    mainBox.setOutlineThickness(3);
    m_window.draw(mainBox);

    sf::Text titleText;
    titleText.setFont(font);
    titleText.setCharacterSize(32);
    titleText.setFillColor(sf::Color(255, 140, 60));
    titleText.setString("🧠 TECHNOLOGY EDITOR");
    titleText.setPosition(m_window.getSize().x / 2 - 230, m_window.getSize().y / 2 - 260);
    m_window.draw(titleText);

    sf::Text countryText;
    countryText.setFont(font);
    countryText.setCharacterSize(22);
    countryText.setFillColor(sf::Color::White);
    countryText.setString("Country: " + country.getName());
    countryText.setPosition(m_window.getSize().x / 2 - 390, m_window.getSize().y / 2 - 210);
    m_window.draw(countryText);

    const auto& unlocked = techManager.getUnlockedTechnologies(country);
    sf::Text statsText;
    statsText.setFont(font);
    statsText.setCharacterSize(18);
    statsText.setFillColor(sf::Color(220, 220, 220));
    statsText.setString("Unlocked: " + std::to_string(unlocked.size()) + " techs");
    statsText.setPosition(m_window.getSize().x / 2 - 390, m_window.getSize().y / 2 - 175);
    m_window.draw(statsText);

    sf::Text instruction;
    instruction.setFont(font);
    instruction.setCharacterSize(18);
    instruction.setFillColor(sf::Color::White);
    instruction.setString("Commands: all | clear | add 1,2,3 | remove 5,7 | set 10,11,14\nPress ENTER to apply | ESC to cancel");
    instruction.setPosition(m_window.getSize().x / 2 - 390, m_window.getSize().y / 2 - 135);
    m_window.draw(instruction);

    sf::RectangleShape inputBox(sf::Vector2f(760, 54));
    inputBox.setPosition(m_window.getSize().x / 2 - 380, m_window.getSize().y / 2 - 55);
    inputBox.setFillColor(sf::Color(55, 55, 65));
    inputBox.setOutlineColor(sf::Color::Cyan);
    inputBox.setOutlineThickness(2);
    m_window.draw(inputBox);

    sf::Text inputDisplay;
    inputDisplay.setFont(font);
    inputDisplay.setCharacterSize(22);
    inputDisplay.setFillColor(sf::Color::Cyan);
    inputDisplay.setString(inputText.empty() ? "_" : inputText + "_");
    inputDisplay.setPosition(m_window.getSize().x / 2 - 370, m_window.getSize().y / 2 - 45);
    m_window.draw(inputDisplay);

    // Show the last few unlocked tech names (lightweight).
    sf::Text recentText;
    recentText.setFont(font);
    recentText.setCharacterSize(16);
    recentText.setFillColor(sf::Color(200, 200, 200));

    std::string recent = "Recent: ";
    const auto& techs = techManager.getTechnologies();
    int shown = 0;
    for (int i = static_cast<int>(unlocked.size()) - 1; i >= 0 && shown < 6; --i) {
        auto it = techs.find(unlocked[static_cast<size_t>(i)]);
        if (it == techs.end()) {
            continue;
        }
        if (shown > 0) {
            recent += ", ";
        }
        recent += it->second.name;
        shown++;
    }
    if (shown == 0) {
        recent += "<none>";
    }

    recentText.setString(recent);
    recentText.setPosition(m_window.getSize().x / 2 - 390, m_window.getSize().y / 2 + 20);
    m_window.draw(recentText);

    if (paused) {
        sf::Text pausedText;
        pausedText.setFont(font);
        pausedText.setCharacterSize(24);
        pausedText.setFillColor(sf::Color(255, 255, 0));
        pausedText.setString("PAUSED");
        pausedText.setPosition(10, 10);
        m_window.draw(pausedText);
    }

    m_window.setView(previousView);
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
    float popScale = 0.0f;
    if (country.getPopulation() > 0) {
        popScale = static_cast<float>(std::min(1.0, std::max(0.0, (std::log10(static_cast<double>(country.getPopulation()) + 1.0) - 4.0) / 4.0)));
    }

    sf::RectangleShape roadSurface(sf::Vector2f(cellSize, cellSize));
    roadSurface.setFillColor(sf::Color(105, 100, 90, 205));

    float highlightSize = std::max(0.6f, cellSize * (0.4f + 0.3f * popScale));
    sf::RectangleShape roadHighlight(sf::Vector2f(highlightSize, highlightSize));
    roadHighlight.setOrigin(highlightSize / 2.f, highlightSize / 2.f);
    sf::Color highlightColor = country.getColor();
    highlightColor.a = static_cast<sf::Uint8>(140 + 90 * popScale);
    roadHighlight.setFillColor(highlightColor);

    sf::RectangleShape railBase(sf::Vector2f(cellSize, cellSize));
    railBase.setFillColor(sf::Color(75, 78, 90, 220));

    float glowSize = std::max(0.7f, cellSize * (0.3f + 0.3f * popScale));
    sf::RectangleShape railGlow(sf::Vector2f(glowSize, glowSize));
    railGlow.setOrigin(glowSize / 2.f, glowSize / 2.f);
    sf::Color glowColor = highlightColor;
    glowColor.r = static_cast<sf::Uint8>(std::min(255, glowColor.r + 80));
    glowColor.g = static_cast<sf::Uint8>(std::min(255, glowColor.g + 80));
    glowColor.b = static_cast<sf::Uint8>(std::min(255, glowColor.b + 40));
    glowColor.a = static_cast<sf::Uint8>(180 + 60 * popScale);
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

void Renderer::drawFactories(const std::vector<Country>& countries, const Map& map, const sf::FloatRect& visibleArea) {
    if (!m_factoryTexture.getSize().x || !m_factoryTexture.getSize().y) {
        return;
    }

    float cellSize = static_cast<float>(map.getGridCellSize());
    sf::FloatRect spriteBounds;

    for (const auto& country : countries) {
        sf::Color tint = country.getColor();
        tint.a = 220;
        for (const auto& factoryPos : country.getFactories()) {
            sf::Vector2f worldCenter((static_cast<float>(factoryPos.x) + 0.5f) * cellSize,
                                     (static_cast<float>(factoryPos.y) + 0.5f) * cellSize);

            sf::Vector2f halfSize(m_factoryTexture.getSize().x * 0.5f * m_factorySprite.getScale().x,
                                  m_factoryTexture.getSize().y * 0.5f * m_factorySprite.getScale().y);
            spriteBounds = sf::FloatRect(worldCenter.x - halfSize.x,
                                         worldCenter.y - halfSize.y,
                                         halfSize.x * 2.f,
                                         halfSize.y * 2.f);

            if (!visibleArea.intersects(spriteBounds)) {
                continue;
            }

            m_factorySprite.setColor(tint);
            m_factorySprite.setPosition(worldCenter);
            m_window.draw(m_factorySprite);
        }
    }

    m_factorySprite.setColor(sf::Color::White);
}

void Renderer::drawTradeRoutes(const TradeManager& tradeManager, const std::vector<Country>& countries, const Map& map, const sf::FloatRect& visibleArea) {
    const auto& routes = tradeManager.getTradeRoutes();
    if (routes.empty()) {
        return;
    }

    float cellSize = static_cast<float>(map.getGridCellSize());
    sf::VertexArray routeLines(sf::Lines);

    for (const auto& route : routes) {
        if (!route.isActive) {
            continue;
        }
        if (route.fromCountryIndex < 0 || route.toCountryIndex < 0) {
            continue;
        }
        if (route.fromCountryIndex >= static_cast<int>(countries.size()) ||
            route.toCountryIndex >= static_cast<int>(countries.size())) {
            continue;
        }

        const Country& from = countries[route.fromCountryIndex];
        const Country& to = countries[route.toCountryIndex];
        if (from.getPopulation() <= 0 || to.getPopulation() <= 0) {
            continue;
        }

        sf::Vector2i fromLoc = from.getCapitalLocation();
        sf::Vector2i toLoc = to.getCapitalLocation();
        sf::Vector2f start((static_cast<float>(fromLoc.x) + 0.5f) * cellSize,
                           (static_cast<float>(fromLoc.y) + 0.5f) * cellSize);
        sf::Vector2f end((static_cast<float>(toLoc.x) + 0.5f) * cellSize,
                         (static_cast<float>(toLoc.y) + 0.5f) * cellSize);

        sf::FloatRect bounds(std::min(start.x, end.x), std::min(start.y, end.y),
                             std::max(1.0f, std::abs(end.x - start.x)),
                             std::max(1.0f, std::abs(end.y - start.y)));
        if (!visibleArea.intersects(bounds)) {
            continue;
        }

        float capacityFactor = static_cast<float>(std::min(1.0, route.capacity / 15.0));
        float efficiencyFactor = static_cast<float>(std::min(1.0, route.efficiency));
        sf::Uint8 alpha = static_cast<sf::Uint8>(80 + 140 * capacityFactor * efficiencyFactor);
        sf::Color routeColor(230, 190, 120, alpha);

        routeLines.append(sf::Vertex(start, routeColor));
        routeLines.append(sf::Vertex(end, routeColor));
    }

    if (routeLines.getVertexCount() > 0) {
        m_window.draw(routeLines);
    }
}

void Renderer::drawPlagueOverlay(const Map& map, const std::vector<Country>& countries, const sf::FloatRect& visibleArea) {
    if (!map.isPlagueActive()) {
        return;
    }

    sf::VertexArray plagueVertices(sf::Quads);
    float cellSize = static_cast<float>(map.getGridCellSize());
    float timePhase = static_cast<float>(m_currentYear) * 0.15f;

    for (size_t i = 0; i < countries.size(); ++i) {
        if (!map.isCountryAffectedByPlague(static_cast<int>(i))) {
            continue;
        }
        const Country& country = countries[i];
        if (country.getPopulation() <= 0) {
            continue;
        }

        for (const auto& cell : country.getBoundaryPixels()) {
            sf::Vector2f worldPos(cell.x * cellSize, cell.y * cellSize);
            sf::FloatRect cellRect(worldPos.x, worldPos.y, cellSize, cellSize);
            if (!visibleArea.intersects(cellRect)) {
                continue;
            }

            float wave = std::sin((cell.x * 0.15f + cell.y * 0.1f) + timePhase);
            sf::Uint8 alpha = static_cast<sf::Uint8>(70 + 50 * (0.5f + 0.5f * wave));
            sf::Color plagueColor(30, 30, 30, alpha);

            plagueVertices.append(sf::Vertex(sf::Vector2f(worldPos.x, worldPos.y), plagueColor));
            plagueVertices.append(sf::Vertex(sf::Vector2f(worldPos.x + cellSize, worldPos.y), plagueColor));
            plagueVertices.append(sf::Vertex(sf::Vector2f(worldPos.x + cellSize, worldPos.y + cellSize), plagueColor));
            plagueVertices.append(sf::Vertex(sf::Vector2f(worldPos.x, worldPos.y + cellSize), plagueColor));
        }
    }

    if (plagueVertices.getVertexCount() > 0) {
        m_window.draw(plagueVertices);
    }
}

void Renderer::drawWarFrontlines(const std::vector<Country>& countries, const Map& map, const sf::FloatRect& visibleArea) {
    sf::VertexArray frontline(sf::Quads);
    float cellSize = static_cast<float>(map.getGridCellSize());
    const auto& countryGrid = map.getCountryGrid();

    std::vector<sf::Vector2f> fortPositions;
    int frontierCount = 0;
    const int fortStride = 45;

    for (const auto& country : countries) {
        if (!country.isAtWar() || country.getPopulation() <= 0) {
            continue;
        }

        std::vector<int> enemyIndices;
        for (const auto* enemy : country.getEnemies()) {
            if (enemy) {
                enemyIndices.push_back(enemy->getCountryIndex());
            }
        }
        if (enemyIndices.empty()) {
            continue;
        }

        for (const auto& cell : country.getBoundaryPixels()) {
            if (cell.y < 0 || cell.y >= static_cast<int>(countryGrid.size()) ||
                cell.x < 0 || cell.x >= static_cast<int>(countryGrid[cell.y].size())) {
                continue;
            }

            sf::Vector2f worldPos(cell.x * cellSize, cell.y * cellSize);
            sf::FloatRect cellRect(worldPos.x, worldPos.y, cellSize, cellSize);
            if (!visibleArea.intersects(cellRect)) {
                continue;
            }

            bool isFrontline = false;
            const int dirs[4][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
            for (const auto& dir : dirs) {
                int nx = cell.x + dir[0];
                int ny = cell.y + dir[1];
                if (ny < 0 || ny >= static_cast<int>(countryGrid.size()) ||
                    nx < 0 || nx >= static_cast<int>(countryGrid[ny].size())) {
                    continue;
                }
                int owner = countryGrid[ny][nx];
                if (owner < 0) {
                    continue;
                }
                for (int enemyIndex : enemyIndices) {
                    if (owner == enemyIndex) {
                        isFrontline = true;
                        break;
                    }
                }
                if (isFrontline) {
                    break;
                }
            }

            if (!isFrontline) {
                continue;
            }

            float pulse = std::sin((cell.x + cell.y) * 0.08f + static_cast<float>(m_currentYear) * 0.2f);
            sf::Uint8 alpha = static_cast<sf::Uint8>(90 + 50 * (0.5f + 0.5f * pulse));
            sf::Color heatColor(220, 70, 55, alpha);

            frontline.append(sf::Vertex(sf::Vector2f(worldPos.x, worldPos.y), heatColor));
            frontline.append(sf::Vertex(sf::Vector2f(worldPos.x + cellSize, worldPos.y), heatColor));
            frontline.append(sf::Vertex(sf::Vector2f(worldPos.x + cellSize, worldPos.y + cellSize), heatColor));
            frontline.append(sf::Vertex(sf::Vector2f(worldPos.x, worldPos.y + cellSize), heatColor));

            if (frontierCount % fortStride == 0) {
                fortPositions.emplace_back(worldPos.x + cellSize / 2.0f, worldPos.y + cellSize / 2.0f);
            }
            frontierCount++;
        }
    }

    if (frontline.getVertexCount() > 0) {
        m_window.draw(frontline);
    }

    if (!fortPositions.empty()) {
        float fortSize = std::max(2.0f, cellSize * 1.4f);
        sf::RectangleShape fort(sf::Vector2f(fortSize, fortSize));
        fort.setOrigin(fortSize / 2.0f, fortSize / 2.0f);
        fort.setFillColor(sf::Color(90, 90, 90, 230));
        fort.setOutlineColor(sf::Color(40, 20, 20, 220));
        fort.setOutlineThickness(1.0f);

        sf::CircleShape fortRing(fortSize * 0.9f);
        fortRing.setOrigin(fortSize * 0.9f, fortSize * 0.9f);
        fortRing.setFillColor(sf::Color::Transparent);
        fortRing.setOutlineColor(sf::Color(200, 140, 120, 120));
        fortRing.setOutlineThickness(1.0f);

        for (const auto& pos : fortPositions) {
            fort.setPosition(pos);
            m_window.draw(fort);
            fortRing.setPosition(pos);
            m_window.draw(fortRing);
        }
    }
}
