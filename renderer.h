// renderer.h

#pragma once

#include <SFML/Graphics.hpp>
#include <vector>
#include "country.h"
#include "map.h"
#include "news.h"
#include "technology.h"
#include "culture.h"

class TradeManager;

class Renderer {
public:
    Renderer(sf::RenderWindow& window, const Map& map, const sf::Color& waterColor);
    void render(const std::vector<Country>& countries, const Map& map, News& news, const TechnologyManager& technologyManager, const CultureManager& cultureManager, const TradeManager& tradeManager, const Country* selectedCountry, bool showCountryInfo);
    void toggleWarmongerHighlights();
    void updateYearText(int year);
    void setNeedsUpdate(bool needsUpdate);
    bool needsUpdate() const { return m_needsUpdate; }
    void handleWindowRecreated(const Map& map);
    void showLoadingScreen();
    void setShowCountryAddModeText(bool show);
    void toggleWarHighlights();
    int getTechScrollOffset() const;
    int getMaxTechScrollOffset() const;
    void setTechScrollOffset(int offset);
    int getCivicScrollOffset() const;
    int getMaxCivicScrollOffset() const;
    void setCivicScrollOffset(int offset);
    void renderMegaTimeJumpScreen(const std::string& inputText, const sf::Font& font);
    void renderCountryAddEditor(const std::string& inputText, int editorState, int maxTechId, int maxCultureId, const sf::Font& font);

private:
    sf::RenderWindow& m_window;
    sf::Texture m_baseTexture;
    sf::Sprite m_baseSprite;
    sf::Font m_font;
    sf::Text m_yearText;
    sf::Color m_waterColor;

    // Country overlay (CPU fallback)
    sf::Image m_countryImage;
    sf::Texture m_countryTexture;
    sf::Sprite m_countrySprite;

    // Country overlay (GPU path: index texture + palette + shader)
    bool m_useGpuCountryOverlay;
    sf::Shader m_countryOverlayShader;
    sf::Texture m_countryIdTexture;
    sf::Sprite m_countryIdSprite;
    sf::Texture m_countryPaletteTexture;
    std::vector<sf::Uint8> m_countryPalettePixels;
    std::vector<sf::Uint8> m_countryIdUploadPixels;
    unsigned int m_countryGridWidth;
    unsigned int m_countryGridHeight;
    unsigned int m_countryPaletteSize;

    bool m_needsUpdate;
    bool m_showWarmongerHighlights;
    bool m_showWarHighlights;
    bool m_showCountryAddModeText;
    int m_currentYear;


    // Country info window variables
    sf::RectangleShape m_infoWindowBackground;
    sf::Text m_infoWindowText;
    sf::RectangleShape m_infoWindowColorSquare;
    int m_techScrollOffset;
    int m_maxTechScrollOffset;
    int m_civicScrollOffset;
    int m_maxCivicScrollOffset;

    // Infrastructure overlays
    struct ResourceCell {
        sf::Vector2i position;
        Resource::Type type;
    };
    std::vector<ResourceCell> m_resourceCells;
    sf::VertexArray m_extractorVertices;
    sf::Texture m_factoryTexture;
    sf::Sprite m_factorySprite;

    void updateCountryImage(const std::vector<std::vector<int>>& countryGrid, const std::vector<Country>& countries, const Map& map);
    void drawCountryInfo(const Country* country, const TechnologyManager& techManager);
    void drawWarmongerHighlights(const std::vector<Country>& countries, const Map& map);
    void drawWarHighlights(const std::vector<Country>& countries, const Map& map);
    void drawTechList(const Country* country, const TechnologyManager& techManager, float x, float y, float width, float height);
    void drawCivicList(const Country* country, const CultureManager& cultureManager);
    void updateExtractorVertices(const Map& map, const std::vector<Country>& countries, const TechnologyManager& technologyManager);
    sf::Color getExtractorColor(Resource::Type type) const;
    int getExtractorUnlockTech(Resource::Type type) const;
    void drawRoadNetwork(const Country& country, const Map& map, const TechnologyManager& technologyManager, const sf::FloatRect& visibleArea);
    void drawFactories(const std::vector<Country>& countries, const Map& map, const sf::FloatRect& visibleArea);
    void drawTradeRoutes(const TradeManager& tradeManager, const std::vector<Country>& countries, const Map& map, const sf::FloatRect& visibleArea);
    void drawPlagueOverlay(const Map& map, const std::vector<Country>& countries, const sf::FloatRect& visibleArea);
    void drawWarFrontlines(const std::vector<Country>& countries, const Map& map, const sf::FloatRect& visibleArea);

};

