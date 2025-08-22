// renderer.h

#pragma once

#include <SFML/Graphics.hpp>
#include <vector>
#include "country.h"
#include "map.h"
#include "news.h"
#include "technology.h"
#include "culture.h"


class Renderer {
public:
    Renderer(sf::RenderWindow& window, const Map& map, const sf::Color& waterColor);
    void render(const std::vector<Country>& countries, const Map& map, News& news, const TechnologyManager& technologyManager, const CultureManager& cultureManager, const Country* selectedCountry, bool showCountryInfo);
    void toggleWarmongerHighlights();
    void updateYearText(int year);
    void setNeedsUpdate(bool needsUpdate);
    bool needsUpdate() const { return m_needsUpdate; }
    void showLoadingScreen();
    void setShowCountryAddModeText(bool show);
    void toggleWarHighlights();
    int getTechScrollOffset() const;
    int getMaxTechScrollOffset() const;
    void setTechScrollOffset(int offset);
    int getCivicScrollOffset() const;
    int getMaxCivicScrollOffset() const;
    void setCivicScrollOffset(int offset);

private:
    sf::RenderWindow& m_window;
    sf::Texture m_baseTexture;
    sf::Sprite m_baseSprite;
    sf::Font m_font;
    sf::Text m_yearText;
    sf::Color m_waterColor;
    sf::Image m_countryImage;
    sf::Texture m_countryTexture;
    sf::Sprite m_countrySprite;
    bool m_needsUpdate;
    bool m_showWarmongerHighlights;
    bool m_showWarHighlights;
    bool m_showCountryAddModeText;


    // Country info window variables
    sf::RectangleShape m_infoWindowBackground;
    sf::Text m_infoWindowText;
    sf::RectangleShape m_infoWindowColorSquare;
    int m_techScrollOffset;
    int m_maxTechScrollOffset;
    int m_civicScrollOffset;
    int m_maxCivicScrollOffset;

    void updateCountryImage(const std::vector<std::vector<int>>& countryGrid, const std::vector<Country>& countries, const Map& map);
    void drawCountryInfo(const Country* country, const TechnologyManager& techManager);
    void drawWarmongerHighlights(const std::vector<Country>& countries, const Map& map);
    void drawWarHighlights(const std::vector<Country>& countries, const Map& map);
    void drawTechList(const Country* country, const TechnologyManager& techManager, float x, float y, float width, float height);
    void drawCivicList(const Country* country, const CultureManager& cultureManager);

};