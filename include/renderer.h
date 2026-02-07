// renderer.h

#pragma once

#include <SFML/Graphics.hpp>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include "country.h"
#include "map.h"
#include "news.h"
#include "technology.h"
#include "culture.h"

class TradeManager;

enum class ViewMode { Flat2D, Globe };

class Renderer {
public:
    Renderer(sf::RenderWindow& window, const Map& map, const sf::Color& waterColor);
    void render(const std::vector<Country>& countries, const Map& map, News& news, const TechnologyManager& technologyManager, const CultureManager& cultureManager, const TradeManager& tradeManager, const Country* selectedCountry, bool showCountryInfo, ViewMode viewMode);
    void setGuiVisible(bool visible);
    bool isGuiVisible() const { return m_guiVisible; }
    void toggleWarmongerHighlights();
    void setWarmongerHighlights(bool enabled);
    bool warmongerHighlightsEnabled() const { return m_showWarmongerHighlights; }
    void setWarHighlights(bool enabled);
    bool warHighlightsEnabled() const { return m_showWarHighlights; }
    void toggleWealthLeaderboard();
    void toggleClimateOverlay();
    void cycleClimateOverlayMode();
    void setClimateOverlay(bool enabled);
    bool climateOverlayEnabled() const { return m_showClimateOverlay; }
    int climateOverlayMode() const { return m_climateOverlayMode; }
    void setClimateOverlayMode(int mode);
    void toggleUrbanOverlay();
    void cycleUrbanOverlayMode();
    void setUrbanOverlay(bool enabled);
    bool urbanOverlayEnabled() const { return m_showUrbanOverlay; }
    int urbanOverlayMode() const { return m_urbanOverlayMode; }
    void setUrbanOverlayMode(int mode);
    void toggleOverseasOverlay();
    void setOverseasOverlay(bool enabled);
    bool overseasOverlayEnabled() const { return m_showOverseasOverlay; }
    void updateYearText(int year);
    void setNeedsUpdate(bool needsUpdate);
    void setPaintHud(bool show, const std::string& text);
    void setHoveredCountryIndex(int countryIndex);
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
    void renderMegaTimeJumpScreen(const std::string& inputText, const sf::Font& font, bool debugLogEnabled);
    sf::FloatRect getMegaTimeJumpDebugCheckboxBounds() const;
    void renderCountryAddEditor(const std::string& inputText, int editorState, int maxTechId, int maxCultureId, const sf::Font& font);
    void renderTechEditor(const Country& country, const TechnologyManager& techManager, const std::string& inputText, const sf::Font& font);

    void resetGlobeView();
    void addGlobeRotation(float deltaYawRadians, float deltaPitchRadians);
    void addGlobeRadiusScale(float delta);
    sf::FloatRect getViewToggleButtonBounds() const;
    bool globeScreenToMapPixel(sf::Vector2i mousePx, const Map& map, sf::Vector2f& outMapPixel) const;
    bool globeScreenToGrid(sf::Vector2i mousePx, const Map& map, sf::Vector2i& outGrid) const;

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
    bool m_showPaintHud;
    std::string m_paintHudText;
    int m_hoveredCountryIndex;
    int m_currentYear;
    bool m_guiVisible = true;


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
    void drawTechList(const Country* country, const TechnologyManager& techManager, float x, float y, float width, float height);
    void drawCivicList(const Country* country, const CultureManager& cultureManager);
    void updateExtractorVertices(const Map& map, const std::vector<Country>& countries, const TechnologyManager& technologyManager);
    sf::Color getExtractorColor(Resource::Type type) const;
    int getExtractorUnlockTech(Resource::Type type) const;

		    sf::Clock m_warArrowClock;
		    bool m_showWealthLeaderboard = false;
		    void drawWealthLeaderboard(const std::vector<Country>& countries);

	    // Airway visuals (plane.png animation)
	    sf::Texture m_planeTexture;
	    sf::Sprite m_planeSprite;
	    sf::Clock m_planeAnimClock;
	    struct AirwayAnimState {
	        float t = 0.0f;     // 0..1 along segment
	        bool forward = true;
	    };
		    std::unordered_map<std::uint64_t, AirwayAnimState> m_airwayAnim;

        // Shipping visuals (containership.png animation)
        sf::Texture m_shipTexture;
        sf::Sprite m_shipSprite;
        sf::Clock m_shipAnimClock;
        struct ShipAnimState {
            float s = 0.0f;     // 0..totalLen along polyline (world units)
            bool forward = true;
        };
        std::unordered_map<std::uint64_t, ShipAnimState> m_shipAnim;

		    // Globe view
		    sf::RenderTexture m_worldCompositeRT;
		    float m_worldCompositeScale = 1.0f;
		    sf::Shader m_globeShader;
		    bool m_globeShaderReady = false;
		    sf::VertexArray m_starVerts;
		    sf::Vector2u m_starWindowSize{ 0u, 0u };
		    float m_globeYaw = 0.0f;
		    float m_globePitch = 0.0f;
		    float m_globeRadiusScale = 0.45f;

		    bool ensureWorldComposite(const Map& map);
		    void ensureStarfield();
	    sf::Vector2f globeCenter() const;
	    float globeRadiusPx() const;

	    void drawWarmongerHighlights(sf::RenderTarget& target, const std::vector<Country>& countries, const Map& map);
	    void drawWarHighlights(sf::RenderTarget& target, const std::vector<Country>& countries, const Map& map);
	    void drawRoadNetwork(sf::RenderTarget& target, const Country& country, const Map& map, const TechnologyManager& technologyManager, const sf::FloatRect& visibleArea);
	    void drawFactories(sf::RenderTarget& target, const std::vector<Country>& countries, const Map& map, const sf::FloatRect& visibleArea);
	    void drawPorts(sf::RenderTarget& target, const std::vector<Country>& countries, const Map& map, const sf::FloatRect& visibleArea);
	    void drawAirwayPlanes(sf::RenderTarget& target, const std::vector<Country>& countries, const Map& map);
        void drawShippingShips(sf::RenderTarget& target, const TradeManager& tradeManager, const Map& map, const sf::FloatRect& visibleArea);
	    void drawTradeRoutes(sf::RenderTarget& target, const TradeManager& tradeManager, const std::vector<Country>& countries, const Map& map, const sf::FloatRect& visibleArea);
	    void drawPlagueOverlay(sf::RenderTarget& target, const Map& map, const std::vector<Country>& countries, const sf::FloatRect& visibleArea);
	    void drawWarFrontlines(sf::RenderTarget& target, const std::vector<Country>& countries, const Map& map, const sf::FloatRect& visibleArea);
	    void drawWarArrows(sf::RenderTarget& target, const std::vector<Country>& countries, const Map& map, const sf::FloatRect& visibleArea);

        // Phase 6: climate debug overlay (field-grid texture).
        void updateClimateOverlayTexture(const Map& map);
        void updateUrbanOverlayTexture(const Map& map);
        void updateOverseasOverlayTexture(const Map& map);

        bool m_showClimateOverlay = false;
        int m_climateOverlayMode = 0; // 0=zone,1=biome,2=tempMean,3=precMean
        int m_climateOverlayLastYear = -9999999;
        int m_climateOverlayLastMode = -1;
        int m_climateW = 0;
        int m_climateH = 0;
        std::vector<sf::Uint8> m_climatePixels;
        sf::Texture m_climateTex;
        sf::Sprite m_climateSprite;

        // Rule-based urban debug overlay (field-grid texture).
        bool m_showUrbanOverlay = false;
        int m_urbanOverlayMode = 0; // 0=crowding,1=specialization,2=urbanShare,3=urbanPop
        int m_urbanOverlayLastYear = -9999999;
        int m_urbanOverlayLastMode = -1;
        int m_urbanW = 0;
        int m_urbanH = 0;
        std::vector<sf::Uint8> m_urbanPixels;
        sf::Texture m_urbanTex;
        sf::Sprite m_urbanSprite;

        // Phase 7: overseas debug overlay (field-grid texture).
        bool m_showOverseasOverlay = false;
        int m_overseasOverlayLastYear = -9999999;
        int m_overseasW = 0;
        int m_overseasH = 0;
        std::vector<sf::Uint8> m_overseasPixels;
        sf::Texture m_overseasTex;
        sf::Sprite m_overseasSprite;
				};
