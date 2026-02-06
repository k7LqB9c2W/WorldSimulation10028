// renderer.cpp

#include "renderer.h"
#include "trade.h"
#include <string>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <random>
#include <imgui.h>
#include <imgui-SFML.h>

extern bool turboMode;
extern bool paused;

namespace {
const char* kCountryOverlayFragmentShader = R"(
// Compatibility: support both legacy GLSL (texture2D) and newer GLSL (texture).
#if __VERSION__ >= 130
    #define TEX_SAMPLE texture
#else
    #define TEX_SAMPLE texture2D
#endif

uniform sampler2D u_countryIdTex;
uniform sampler2D palette;
uniform float paletteSize;
uniform float showHover;
uniform float hoveredId;
uniform vec2 idTexel;

void main()
{
    vec4 encoded = TEX_SAMPLE(u_countryIdTex, gl_TexCoord[0].xy);
    float low = floor(encoded.r * 255.0 + 0.5);
    float high = floor(encoded.g * 255.0 + 0.5);
    float id = low + high * 256.0; // 0 = empty, otherwise countryIndex + 1

    if (id < 0.5) {
        gl_FragColor = vec4(0.0);
        return;
    }

    if (showHover > 0.5 && abs(id - hoveredId) < 0.5) {
        vec2 uv = gl_TexCoord[0].xy;
        vec4 eL = TEX_SAMPLE(u_countryIdTex, uv + vec2(-idTexel.x, 0.0));
        vec4 eR = TEX_SAMPLE(u_countryIdTex, uv + vec2(idTexel.x, 0.0));
        vec4 eU = TEX_SAMPLE(u_countryIdTex, uv + vec2(0.0, -idTexel.y));
        vec4 eD = TEX_SAMPLE(u_countryIdTex, uv + vec2(0.0, idTexel.y));

        float l = floor(eL.r * 255.0 + 0.5) + floor(eL.g * 255.0 + 0.5) * 256.0;
        float r = floor(eR.r * 255.0 + 0.5) + floor(eR.g * 255.0 + 0.5) * 256.0;
        float u = floor(eU.r * 255.0 + 0.5) + floor(eU.g * 255.0 + 0.5) * 256.0;
        float d = floor(eD.r * 255.0 + 0.5) + floor(eD.g * 255.0 + 0.5) * 256.0;

        bool edge = (abs(l - id) > 0.5) || (abs(r - id) > 0.5) || (abs(u - id) > 0.5) || (abs(d - id) > 0.5);
        if (edge) {
            gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        } else {
            gl_FragColor = vec4(1.0, 1.0, 1.0, 0.95);
        }
        return;
    }

    if (id > paletteSize - 0.5) {
        gl_FragColor = vec4(0.0);
        return;
    }

    float u = (id + 0.5) / paletteSize;
    gl_FragColor = TEX_SAMPLE(palette, vec2(u, 0.5));
}
	)";
	} // namespace

namespace {
const char* kGlobeFragmentShader = R"(
#if __VERSION__ >= 130
    #define TEX_SAMPLE texture
#else
    #define TEX_SAMPLE texture2D
#endif

uniform sampler2D u_worldTex;
uniform float u_yaw;
uniform float u_pitch;
uniform vec3 u_lightDir;
uniform float u_ambient;

const float PI = 3.14159265358979323846264;

vec3 rotY(vec3 v, float a) {
    float c = cos(a);
    float s = sin(a);
    return vec3(c * v.x + s * v.z, v.y, -s * v.x + c * v.z);
}

vec3 rotX(vec3 v, float a) {
    float c = cos(a);
    float s = sin(a);
    return vec3(v.x, c * v.y - s * v.z, s * v.y + c * v.z);
}

void main() {
    vec2 uv = gl_TexCoord[0].xy;
    vec2 p = uv * 2.0 - 1.0;
    p.y = -p.y;

    float r2 = dot(p, p);
    if (r2 > 1.0) {
        discard;
    }

    float z = sqrt(max(0.0, 1.0 - r2));
    vec3 nView = normalize(vec3(p.x, p.y, z));

    vec3 n = rotX(rotY(nView, u_yaw), u_pitch);
    float lon = atan(n.x, n.z);
    float lat = asin(clamp(n.y, -1.0, 1.0));

    float tu = lon / (2.0 * PI) + 0.5;
    float tv = 0.5 - lat / PI;
    tu = fract(tu);
    tv = clamp(tv, 0.0, 1.0);

    vec4 col = TEX_SAMPLE(u_worldTex, vec2(tu, tv));

    vec3 L = normalize(u_lightDir);
    float diff = max(0.0, dot(nView, L));
    float shade = u_ambient + (1.0 - u_ambient) * diff;
    float rim = pow(1.0 - nView.z, 2.0);
    vec3 outRgb = col.rgb * shade + vec3(0.12, 0.14, 0.18) * rim;

    gl_FragColor = vec4(outRgb, col.a);
}
)";
} // namespace

namespace {
bool tryLoadPlaneTexture(sf::Texture& tex, const char* primary, const char* secondary) {
    if (tex.loadFromFile(primary)) {
        return true;
    }
    if (secondary && secondary[0] != '\0') {
        return tex.loadFromFile(secondary);
    }
    return false;
}
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
    m_hoveredCountryIndex(-1),
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
            m_countryOverlayShader.setUniform("showHover", 0.f);
            m_countryOverlayShader.setUniform("hoveredId", 0.f);
            m_countryOverlayShader.setUniform("u_countryIdTex", sf::Shader::CurrentTexture);
            if (m_countryGridWidth > 0 && m_countryGridHeight > 0) {
                m_countryOverlayShader.setUniform("idTexel", sf::Glsl::Vec2(1.f / static_cast<float>(m_countryGridWidth),
                                                                           1.f / static_cast<float>(m_countryGridHeight)));
            }
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

	    if (tryLoadPlaneTexture(m_planeTexture, "plane.png", "Plane.png")) {
	        sf::Vector2u texSize = m_planeTexture.getSize();
	        m_planeSprite.setTexture(m_planeTexture);
	        m_planeSprite.setOrigin(static_cast<float>(texSize.x) * 0.5f, static_cast<float>(texSize.y) * 0.5f);
        float targetSize = std::max(10.f, static_cast<float>(map.getGridCellSize()) * 14.f);
        float maxDimension = static_cast<float>(std::max(texSize.x, texSize.y));
        if (maxDimension > 0.f) {
            float scale = targetSize / maxDimension;
            m_planeSprite.setScale(scale, scale);
        }
        m_planeSprite.setColor(sf::Color(255, 255, 255, 230));
    }

    if (tryLoadPlaneTexture(m_shipTexture, "containership.png", "ContainerShip.png")) {
        sf::Vector2u texSize = m_shipTexture.getSize();
        m_shipSprite.setTexture(m_shipTexture);
        m_shipSprite.setOrigin(static_cast<float>(texSize.x) * 0.5f, static_cast<float>(texSize.y) * 0.5f);
        float targetH = std::max(5.f, static_cast<float>(map.getGridCellSize()) * 6.f);
        if (texSize.y > 0u) {
            float scale = targetH / static_cast<float>(texSize.y);
            m_shipSprite.setScale(scale, scale);
        }
        m_shipSprite.setColor(sf::Color(255, 255, 255, 235));
    }


    // Initialize country info window elements
    m_infoWindowBackground.setFillColor(sf::Color(0, 0, 0, 175));
    m_infoWindowBackground.setSize(sf::Vector2f(400, 350));

    m_infoWindowText.setCharacterSize(16);
    m_infoWindowText.setFillColor(sf::Color::White);

    m_infoWindowColorSquare.setSize(sf::Vector2f(20, 20));
}

void Renderer::render(const std::vector<Country>& countries,
                      const Map& map,
                      News& news,
                      const TechnologyManager& technologyManager,
                      const CultureManager& cultureManager,
                      const TradeManager& tradeManager,
                      const Country* selectedCountry,
                      bool showCountryInfo,
                      ViewMode viewMode) {
    sf::View worldView = m_window.getView();

    bool globeMode = false;
    sf::View drawView = worldView;
    sf::FloatRect visibleArea;
    sf::RenderTarget* worldTarget = &m_window;

    if (viewMode == ViewMode::Globe && ensureWorldComposite(map) && m_globeShaderReady) {
        globeMode = true;
        const sf::Vector2u mapSize = map.getBaseImage().getSize();
        drawView = sf::View(sf::FloatRect(0.f, 0.f, static_cast<float>(mapSize.x), static_cast<float>(mapSize.y)));
        drawView.setCenter(static_cast<float>(mapSize.x) * 0.5f, static_cast<float>(mapSize.y) * 0.5f);
        visibleArea = sf::FloatRect(0.f, 0.f, static_cast<float>(mapSize.x), static_cast<float>(mapSize.y));
        worldTarget = &m_worldCompositeRT;

        m_worldCompositeRT.setView(drawView);
        m_worldCompositeRT.clear(sf::Color::Transparent);
    } else {
        sf::Vector2f viewCenter = worldView.getCenter();
        sf::Vector2f viewSize = worldView.getSize();
        visibleArea = sf::FloatRect(viewCenter.x - viewSize.x / 2.f, viewCenter.y - viewSize.y / 2.f, viewSize.x, viewSize.y);

        m_window.clear();
    }

	    worldTarget->setView(drawView);
	    worldTarget->draw(m_baseSprite); // Draw the base map (map.png)

        if (m_showClimateOverlay) {
            updateClimateOverlayTexture(map);
            worldTarget->draw(m_climateSprite);
        }

        if (m_showOverseasOverlay) {
            updateOverseasOverlayTexture(map);
            worldTarget->draw(m_overseasSprite);
        }

        if (m_showUrbanOverlay) {
            updateUrbanOverlayTexture(map);
            worldTarget->draw(m_urbanSprite);
        }

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
        if (m_hoveredCountryIndex >= 0) {
            m_countryOverlayShader.setUniform("showHover", 1.f);
            m_countryOverlayShader.setUniform("hoveredId", static_cast<float>(m_hoveredCountryIndex + 1));
        } else {
            m_countryOverlayShader.setUniform("showHover", 0.f);
            m_countryOverlayShader.setUniform("hoveredId", 0.f);
        }
        worldTarget->draw(m_countryIdSprite, states);
    } else {
        worldTarget->draw(m_countrySprite); // Draw the countries
    }

    drawPlagueOverlay(*worldTarget, map, countries, visibleArea);

    // Refresh infrastructure overlays before drawing per-country assets
    updateExtractorVertices(map, countries, technologyManager);

    // Draw roads/rails first (so they appear under cities)
    for (const auto& country : countries) {
        drawRoadNetwork(*worldTarget, country, map, technologyManager, visibleArea);
    }

    drawTradeRoutes(*worldTarget, tradeManager, countries, map, visibleArea);
    drawAirwayPlanes(*worldTarget, countries, map);

    if (m_extractorVertices.getVertexCount() > 0) {
        worldTarget->draw(m_extractorVertices);
    }

    drawFactories(*worldTarget, countries, map, visibleArea);
    drawPorts(*worldTarget, countries, map, visibleArea);
    drawShippingShips(*worldTarget, tradeManager, map, visibleArea);

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
            worldTarget->draw(cityGlow);

            sf::RectangleShape cityShape(sf::Vector2f(citySize, citySize));
            cityShape.setOrigin(citySize / 2.0f, citySize / 2.0f);

            if (city.isMajorCity()) {
                cityShape.setFillColor(sf::Color(255, 210, 80));
            } else {
                cityShape.setFillColor(sf::Color(20, 20, 20));
            }

            cityShape.setPosition(cityCenter);
            worldTarget->draw(cityShape);

            if (cityLocation == capitalLocation) {
                float markerSize = std::max(1.2f, map.getGridCellSize() * 1.2f);
                float clusterRadius = citySize + 2.0f + densityScale * 2.0f;
                sf::RectangleShape marker(sf::Vector2f(markerSize, markerSize));
                marker.setOrigin(markerSize / 2.0f, markerSize / 2.0f);
                marker.setFillColor(sf::Color(255, 230, 160, 200));

                marker.setPosition(cityCenter.x + clusterRadius, cityCenter.y);
                worldTarget->draw(marker);

                marker.setPosition(cityCenter.x - clusterRadius * 0.7f, cityCenter.y + clusterRadius * 0.6f);
                worldTarget->draw(marker);

                marker.setPosition(cityCenter.x - clusterRadius * 0.7f, cityCenter.y - clusterRadius * 0.6f);
                worldTarget->draw(marker);

                sf::CircleShape ring(clusterRadius);
                ring.setOrigin(clusterRadius, clusterRadius);
                ring.setPosition(cityCenter);
                ring.setFillColor(sf::Color::Transparent);
                ring.setOutlineColor(sf::Color(255, 220, 120, 120));
                ring.setOutlineThickness(1.0f);
                worldTarget->draw(ring);
            }
        }
    }

    // Draw Warmonger highlights if the flag is true
    if (m_showWarmongerHighlights) {
        drawWarmongerHighlights(*worldTarget, countries, map);
    }

    // Draw war highlights if the flag is true
    if (m_showWarHighlights) {
        drawWarFrontlines(*worldTarget, countries, map, visibleArea);
        drawWarHighlights(*worldTarget, countries, map);
    }

    // Always show a primary-target war arrow when a country is at war.
    drawWarArrows(*worldTarget, countries, map, visibleArea);

    // CPU fallback hover marker (GPU path handles full highlight via shader).
    if (!m_useGpuCountryOverlay && m_hoveredCountryIndex >= 0 &&
        m_hoveredCountryIndex < static_cast<int>(countries.size()) &&
        countries[static_cast<size_t>(m_hoveredCountryIndex)].getCountryIndex() == m_hoveredCountryIndex &&
        countries[static_cast<size_t>(m_hoveredCountryIndex)].getPopulation() > 0) {
        sf::Vector2i cap = countries[static_cast<size_t>(m_hoveredCountryIndex)].getCapitalLocation();
        sf::Vector2f pos((static_cast<float>(cap.x) + 0.5f) * map.getGridCellSize(),
                         (static_cast<float>(cap.y) + 0.5f) * map.getGridCellSize());
        float r = std::max(4.0f, static_cast<float>(map.getGridCellSize()) * 6.0f);
        sf::CircleShape marker(r);
        marker.setOrigin(r, r);
        marker.setPosition(pos);
        marker.setFillColor(sf::Color(255, 255, 255, 60));
        marker.setOutlineColor(sf::Color::Black);
        marker.setOutlineThickness(std::max(1.0f, r * 0.18f));
        worldTarget->draw(marker);
    }

    if (globeMode) {
        m_worldCompositeRT.display();
        ensureStarfield();

        m_window.setView(m_window.getDefaultView());
        m_window.clear(sf::Color::Black);
        if (m_starVerts.getVertexCount() > 0) {
            m_window.draw(m_starVerts);
        }

        const float r = globeRadiusPx();
        sf::CircleShape globe(r, 180);
        globe.setOrigin(r, r);
        globe.setPosition(globeCenter());
        globe.setTexture(&m_worldCompositeRT.getTexture());
        globe.setFillColor(sf::Color::White);

        m_globeShader.setUniform("u_worldTex", sf::Shader::CurrentTexture);
        m_globeShader.setUniform("u_yaw", m_globeYaw);
        m_globeShader.setUniform("u_pitch", m_globePitch);
        m_globeShader.setUniform("u_lightDir", sf::Glsl::Vec3(-0.35f, 0.20f, 1.0f));
        m_globeShader.setUniform("u_ambient", 0.28f);

        sf::RenderStates states;
        states.shader = &m_globeShader;
        m_window.draw(globe, states);

        sf::CircleShape outline(r, 180);
        outline.setOrigin(r, r);
        outline.setPosition(globeCenter());
        outline.setFillColor(sf::Color::Transparent);
        outline.setOutlineThickness(std::max(1.0f, r * 0.01f));
        outline.setOutlineColor(sf::Color(255, 255, 255, 60));
        m_window.draw(outline);
    }

    if (m_guiVisible && ImGui::GetCurrentContext() != nullptr) {
        m_window.setView(m_window.getDefaultView());
        ImGui::SFML::Render(m_window);
    }

    m_window.setView(worldView);
    m_window.display();
		}

void Renderer::drawWarArrows(sf::RenderTarget& target, const std::vector<Country>& countries, const Map& map, const sf::FloatRect& visibleArea) {
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
        target.draw(tris);
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
    devInfo += "Food Security: " + std::to_string(static_cast<int>(std::round(country->getFoodSecurity() * 100.0))) + "%\n";
    devInfo += "Market Access: " + std::to_string(static_cast<int>(std::round(country->getMarketAccess() * 100.0))) + "%\n";
    devInfo += "Innovation: " + std::to_string(static_cast<int>(std::round(country->getInnovationRate()))) + "\n";
    {
        const auto& t = country->getTraits();
        // Trait order (0..6): Religiosity, Collectivism, Militarism, Mercantile, Hierarchy, Openness, Legalism
        devInfo += "Openness: " + std::to_string(static_cast<int>(std::round(t[5] * 100.0))) + "%\n";
        devInfo += "Militarism: " + std::to_string(static_cast<int>(std::round(t[2] * 100.0))) + "%\n";
        devInfo += "Legalism: " + std::to_string(static_cast<int>(std::round(t[6] * 100.0))) + "%\n";
    }
    devInfo += "Military Strength: " + std::to_string(static_cast<int>(country->getMilitaryStrength() * 100)) + "%\n";
    
    infoText.setString(devInfo);
    infoText.setPosition(startX, currentY);
    m_window.draw(infoText);
    currentY += 140;

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

void Renderer::setGuiVisible(bool visible) {
    m_guiVisible = visible;
}

void Renderer::setWarmongerHighlights(bool enabled) {
    m_showWarmongerHighlights = enabled;
}

void Renderer::setWarHighlights(bool enabled) {
    m_showWarHighlights = enabled;
}

void Renderer::toggleWealthLeaderboard() {
    m_showWealthLeaderboard = !m_showWealthLeaderboard;
}

void Renderer::toggleClimateOverlay() {
    m_showClimateOverlay = !m_showClimateOverlay;
}

void Renderer::cycleClimateOverlayMode() {
    m_showClimateOverlay = true;
    m_climateOverlayMode = (m_climateOverlayMode + 1) % 4;
}

void Renderer::setClimateOverlay(bool enabled) {
    m_showClimateOverlay = enabled;
}

void Renderer::setClimateOverlayMode(int mode) {
    m_showClimateOverlay = true;
    m_climateOverlayMode = std::max(0, std::min(mode, 3));
}

void Renderer::toggleUrbanOverlay() {
    m_showUrbanOverlay = !m_showUrbanOverlay;
}

void Renderer::cycleUrbanOverlayMode() {
    m_showUrbanOverlay = true;
    m_urbanOverlayMode = (m_urbanOverlayMode + 1) % 4;
}

void Renderer::setUrbanOverlay(bool enabled) {
    m_showUrbanOverlay = enabled;
}

void Renderer::setUrbanOverlayMode(int mode) {
    m_showUrbanOverlay = true;
    m_urbanOverlayMode = std::max(0, std::min(mode, 3));
}

void Renderer::toggleOverseasOverlay() {
    m_showOverseasOverlay = !m_showOverseasOverlay;
}

void Renderer::setOverseasOverlay(bool enabled) {
    m_showOverseasOverlay = enabled;
}

void Renderer::toggleWarHighlights() {
    m_showWarHighlights = !m_showWarHighlights;
}

void Renderer::updateClimateOverlayTexture(const Map& map) {
    const int w = map.getFieldWidth();
    const int h = map.getFieldHeight();
    if (w <= 0 || h <= 0) {
        return;
    }

    const bool sizeChanged = (w != m_climateW) || (h != m_climateH);
    const bool modeChanged = (m_climateOverlayMode != m_climateOverlayLastMode);
    const bool yearChanged = (m_currentYear != m_climateOverlayLastYear);
    if (!sizeChanged && !modeChanged && !yearChanged && !m_climatePixels.empty()) {
        return;
    }

    m_climateW = w;
    m_climateH = h;
    m_climateOverlayLastMode = m_climateOverlayMode;
    m_climateOverlayLastYear = m_currentYear;

    const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h);
    m_climatePixels.assign(n * 4u, 0u);

    auto clamp01f = [](float v) { return std::max(0.0f, std::min(1.0f, v)); };
    auto lerp = [](sf::Uint8 a, sf::Uint8 b, float t) -> sf::Uint8 {
        const float v = static_cast<float>(a) + (static_cast<float>(b) - static_cast<float>(a)) * t;
        return static_cast<sf::Uint8>(std::max(0.0f, std::min(255.0f, v)));
    };
    auto lerpColor = [&](sf::Color a, sf::Color b, float t) -> sf::Color {
        t = clamp01f(t);
        return sf::Color(lerp(a.r, b.r, t), lerp(a.g, b.g, t), lerp(a.b, b.b, t), 255);
    };

    auto tempRamp = [&](float t) -> sf::Color {
        t = clamp01f(t);
        if (t < 0.25f) return lerpColor(sf::Color(40, 60, 220), sf::Color(60, 190, 255), t / 0.25f);
        if (t < 0.50f) return lerpColor(sf::Color(60, 190, 255), sf::Color(80, 220, 140), (t - 0.25f) / 0.25f);
        if (t < 0.75f) return lerpColor(sf::Color(80, 220, 140), sf::Color(245, 210, 70), (t - 0.50f) / 0.25f);
        return lerpColor(sf::Color(245, 210, 70), sf::Color(235, 60, 40), (t - 0.75f) / 0.25f);
    };

    auto precipRamp = [&](float p) -> sf::Color {
        p = clamp01f(p);
        if (p < 0.5f) return lerpColor(sf::Color(190, 150, 80), sf::Color(90, 190, 90), p / 0.5f);
        return lerpColor(sf::Color(90, 190, 90), sf::Color(70, 150, 255), (p - 0.5f) / 0.5f);
    };

    const auto& zone = map.getFieldClimateZone();
    const auto& biome = map.getFieldBiome();
    const auto& temp = map.getFieldTempMean();
    const auto& prec = map.getFieldPrecipMean();

    for (size_t i = 0; i < n; ++i) {
        const uint8_t z = (i < zone.size()) ? zone[i] : 255u;
        if (z == 255u) {
            // Water/invalid: fully transparent.
            m_climatePixels[i * 4u + 3u] = 0u;
            continue;
        }

        sf::Color c(0, 0, 0, 255);
        if (m_climateOverlayMode == 0) {
            switch (z) {
                case 0: c = sf::Color(255, 90, 30); break;   // equatorial
                case 1: c = sf::Color(255, 180, 60); break;  // tropical/subtropical
                case 2: c = sf::Color(80, 210, 120); break;  // temperate
                case 3: c = sf::Color(80, 140, 255); break;  // boreal
                case 4: c = sf::Color(235, 235, 255); break; // polar
                default: c = sf::Color(60, 60, 60); break;
            }
        } else if (m_climateOverlayMode == 1) {
            const uint8_t b = (i < biome.size()) ? biome[i] : 255u;
            switch (b) {
                case 0: c = sf::Color(235, 245, 255); break; // Ice
                case 1: c = sf::Color(190, 190, 200); break; // Tundra
                case 2: c = sf::Color(70, 120, 70); break;   // Taiga
                case 3: c = sf::Color(50, 170, 70); break;   // Temperate forest
                case 4: c = sf::Color(170, 210, 90); break;  // Grassland
                case 5: c = sf::Color(225, 195, 130); break; // Desert
                case 6: c = sf::Color(210, 190, 90); break;  // Savanna
                case 7: c = sf::Color(30, 140, 60); break;   // Tropical forest
                case 8: c = sf::Color(150, 190, 90); break;  // Mediterranean
                default: c = sf::Color(60, 60, 60); break;
            }
        } else if (m_climateOverlayMode == 2) {
            const float tc = (i < temp.size()) ? temp[i] : 0.0f;
            const float t01 = clamp01f((tc + 25.0f) / 60.0f);
            c = tempRamp(t01);
        } else if (m_climateOverlayMode == 3) {
            const float p = (i < prec.size()) ? prec[i] : 0.0f;
            c = precipRamp(p);
        }

        m_climatePixels[i * 4u + 0u] = c.r;
        m_climatePixels[i * 4u + 1u] = c.g;
        m_climatePixels[i * 4u + 2u] = c.b;
        m_climatePixels[i * 4u + 3u] = 255u;
    }

    if (sizeChanged || m_climateTex.getSize().x != static_cast<unsigned>(w) || m_climateTex.getSize().y != static_cast<unsigned>(h)) {
        m_climateTex.create(static_cast<unsigned>(w), static_cast<unsigned>(h));
        m_climateTex.setSmooth(false);
        m_climateSprite.setTexture(m_climateTex, true);
        m_climateSprite.setPosition(0.f, 0.f);
    }
    m_climateTex.update(m_climatePixels.data());

    const float scale = static_cast<float>(map.getGridCellSize() * Map::kFieldCellSize);
    m_climateSprite.setScale(scale, scale);
    m_climateSprite.setColor(sf::Color(255, 255, 255, 150));
}

void Renderer::updateUrbanOverlayTexture(const Map& map) {
    const int w = map.getFieldWidth();
    const int h = map.getFieldHeight();
    if (w <= 0 || h <= 0) {
        return;
    }

    const bool sizeChanged = (w != m_urbanW) || (h != m_urbanH);
    const bool modeChanged = (m_urbanOverlayMode != m_urbanOverlayLastMode);
    const bool yearChanged = (m_currentYear != m_urbanOverlayLastYear);
    if (!sizeChanged && !modeChanged && !yearChanged && !m_urbanPixels.empty()) {
        return;
    }

    m_urbanW = w;
    m_urbanH = h;
    m_urbanOverlayLastMode = m_urbanOverlayMode;
    m_urbanOverlayLastYear = m_currentYear;

    const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h);
    m_urbanPixels.assign(n * 4u, 0u);

    auto clamp01f = [](float v) { return std::max(0.0f, std::min(1.0f, v)); };
    auto lerp = [](sf::Uint8 a, sf::Uint8 b, float t) -> sf::Uint8 {
        const float v = static_cast<float>(a) + (static_cast<float>(b) - static_cast<float>(a)) * t;
        return static_cast<sf::Uint8>(std::max(0.0f, std::min(255.0f, v)));
    };
    auto lerpColor = [&](sf::Color a, sf::Color b, float t) -> sf::Color {
        t = clamp01f(t);
        return sf::Color(lerp(a.r, b.r, t), lerp(a.g, b.g, t), lerp(a.b, b.b, t), 255);
    };

    const auto& food = map.getFieldFoodPotential();
    const auto& crowd = map.getFieldCrowding();
    const auto& spec = map.getFieldSpecialization();
    const auto& urbanShare = map.getFieldUrbanShare();
    const auto& urbanPop = map.getFieldUrbanPop();

    const float denomUrbanPop = std::log1p(250'000.0f);

    for (size_t i = 0; i < n; ++i) {
        const float fp = (i < food.size()) ? food[i] : 0.0f;
        if (fp <= 0.0f) {
            m_urbanPixels[i * 4u + 3u] = 0u;
            continue;
        }

        float t = 0.0f;
        sf::Color c(0, 0, 0, 255);

        if (m_urbanOverlayMode == 0) {
            const float cr = (i < crowd.size()) ? crowd[i] : 0.0f;
            t = clamp01f((cr - 0.8f) / 1.2f);
            c = lerpColor(sf::Color(35, 70, 140), sf::Color(235, 70, 55), t);
        } else if (m_urbanOverlayMode == 1) {
            t = (i < spec.size()) ? clamp01f(spec[i]) : 0.0f;
            c = lerpColor(sf::Color(70, 75, 135), sf::Color(245, 220, 70), t);
        } else if (m_urbanOverlayMode == 2) {
            const float us = (i < urbanShare.size()) ? urbanShare[i] : 0.0f;
            t = clamp01f(us / 0.45f);
            c = lerpColor(sf::Color(40, 140, 70), sf::Color(245, 150, 50), t);
        } else if (m_urbanOverlayMode == 3) {
            const float up = (i < urbanPop.size()) ? urbanPop[i] : 0.0f;
            t = (denomUrbanPop > 1e-6f) ? clamp01f(std::log1p(std::max(0.0f, up)) / denomUrbanPop) : 0.0f;
            c = lerpColor(sf::Color(45, 45, 45), sf::Color(235, 235, 235), t);
        }

        const sf::Uint8 a = static_cast<sf::Uint8>(std::round(50.0f + 190.0f * t));
        m_urbanPixels[i * 4u + 0u] = c.r;
        m_urbanPixels[i * 4u + 1u] = c.g;
        m_urbanPixels[i * 4u + 2u] = c.b;
        m_urbanPixels[i * 4u + 3u] = a;
    }

    if (sizeChanged || m_urbanTex.getSize().x != static_cast<unsigned>(w) || m_urbanTex.getSize().y != static_cast<unsigned>(h)) {
        m_urbanTex.create(static_cast<unsigned>(w), static_cast<unsigned>(h));
        m_urbanTex.setSmooth(false);
        m_urbanSprite.setTexture(m_urbanTex, true);
        m_urbanSprite.setPosition(0.f, 0.f);
    }
    m_urbanTex.update(m_urbanPixels.data());

    const float scale = static_cast<float>(map.getGridCellSize() * Map::kFieldCellSize);
    m_urbanSprite.setScale(scale, scale);
    m_urbanSprite.setColor(sf::Color(255, 255, 255, 255));
}

void Renderer::updateOverseasOverlayTexture(const Map& map) {
    const int w = map.getFieldWidth();
    const int h = map.getFieldHeight();
    if (w <= 0 || h <= 0) {
        return;
    }

    const bool sizeChanged = (w != m_overseasW) || (h != m_overseasH);
    const bool yearChanged = (m_currentYear != m_overseasOverlayLastYear);
    if (!sizeChanged && !yearChanged && !m_overseasPixels.empty()) {
        return;
    }

    m_overseasW = w;
    m_overseasH = h;
    m_overseasOverlayLastYear = m_currentYear;

    const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h);
    m_overseasPixels.assign(n * 4u, 0u);

    const auto& mask = map.getFieldOverseasMask();
    for (size_t i = 0; i < n; ++i) {
        const uint8_t v = (i < mask.size()) ? mask[i] : 0u;
        if (v == 0u) {
            m_overseasPixels[i * 4u + 3u] = 0u;
            continue;
        }
        // Overseas highlight: magenta-red.
        m_overseasPixels[i * 4u + 0u] = 235u;
        m_overseasPixels[i * 4u + 1u] = 80u;
        m_overseasPixels[i * 4u + 2u] = 180u;
        m_overseasPixels[i * 4u + 3u] = 255u;
    }

    if (sizeChanged || m_overseasTex.getSize().x != static_cast<unsigned>(w) || m_overseasTex.getSize().y != static_cast<unsigned>(h)) {
        m_overseasTex.create(static_cast<unsigned>(w), static_cast<unsigned>(h));
        m_overseasTex.setSmooth(false);
        m_overseasSprite.setTexture(m_overseasTex, true);
        m_overseasSprite.setPosition(0.f, 0.f);
    }
    m_overseasTex.update(m_overseasPixels.data());

    const float scale = static_cast<float>(map.getGridCellSize() * Map::kFieldCellSize);
    m_overseasSprite.setScale(scale, scale);
    m_overseasSprite.setColor(sf::Color(255, 255, 255, 140));
}

void Renderer::drawWealthLeaderboard(const std::vector<Country>& countries) {
    sf::View prev = m_window.getView();
    m_window.setView(m_window.getDefaultView());

    struct Row {
        int idx;
        double wealth;
        double gdp;
        double exports;
        long long pop;
    };

    std::vector<Row> rows;
    rows.reserve(countries.size());

    for (int i = 0; i < static_cast<int>(countries.size()); ++i) {
        const Country& c = countries[static_cast<size_t>(i)];
        if (c.getPopulation() <= 0) {
            continue;
        }
        rows.push_back({i, c.getWealth(), c.getGDP(), c.getExports(), c.getPopulation()});
    }

    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        if (a.wealth != b.wealth) return a.wealth > b.wealth;
        return a.idx < b.idx;
    });

    const sf::Vector2u ws = m_window.getSize();
    const float panelW = 760.f;
    const float panelH = 760.f;
    const float x = (static_cast<float>(ws.x) - panelW) * 0.5f;
    const float y = (static_cast<float>(ws.y) - panelH) * 0.5f;

    sf::RectangleShape bg(sf::Vector2f(panelW, panelH));
    bg.setPosition(x, y);
    bg.setFillColor(sf::Color(10, 10, 15, 220));
    bg.setOutlineColor(sf::Color(120, 120, 160, 220));
    bg.setOutlineThickness(2.f);
    m_window.draw(bg);

    sf::Text title;
    title.setFont(m_font);
    title.setCharacterSize(28);
    title.setFillColor(sf::Color(255, 220, 60));
    title.setString("Wealth Leaderboard");
    title.setPosition(x + 20.f, y + 14.f);
    m_window.draw(title);

    auto fmtMoney = [](double v) {
        const double av = std::abs(v);
        char buf[64];
        if (av >= 1e12) std::snprintf(buf, sizeof(buf), "%.2fT", v / 1e12);
        else if (av >= 1e9) std::snprintf(buf, sizeof(buf), "%.2fB", v / 1e9);
        else if (av >= 1e6) std::snprintf(buf, sizeof(buf), "%.2fM", v / 1e6);
        else if (av >= 1e3) std::snprintf(buf, sizeof(buf), "%.2fK", v / 1e3);
        else std::snprintf(buf, sizeof(buf), "%.2f", v);
        return std::string(buf);
    };

    sf::Text header;
    header.setFont(m_font);
    header.setCharacterSize(16);
    header.setFillColor(sf::Color(200, 200, 220));
    header.setString("Rank   Country                           Wealth        GDP        Exports      Pop");
    header.setPosition(x + 20.f, y + 60.f);
    m_window.draw(header);

    const int maxRows = 25;
    float lineY = y + 90.f;
    const float lineH = 24.f;

    for (int r = 0; r < static_cast<int>(rows.size()) && r < maxRows; ++r) {
        const Country& c = countries[static_cast<size_t>(rows[r].idx)];

        sf::RectangleShape colorBox(sf::Vector2f(14.f, 14.f));
        colorBox.setFillColor(c.getColor());
        colorBox.setPosition(x + 22.f, lineY + 4.f);
        m_window.draw(colorBox);

        sf::Text t;
        t.setFont(m_font);
        t.setCharacterSize(16);
        t.setFillColor(sf::Color::White);

        std::string s =
            std::to_string(r + 1) + "     " +
            c.getName() + "     " +
            fmtMoney(rows[r].wealth) + "     " +
            fmtMoney(rows[r].gdp) + "     " +
            fmtMoney(rows[r].exports) + "     " +
            std::to_string(rows[r].pop);

        t.setString(s);
        t.setPosition(x + 44.f, lineY);
        m_window.draw(t);

        lineY += lineH;
    }

    sf::Text hint;
    hint.setFont(m_font);
    hint.setCharacterSize(14);
    hint.setFillColor(sf::Color(180, 180, 200));
    hint.setString("Press L to close");
    hint.setPosition(x + 20.f, y + panelH - 30.f);
    m_window.draw(hint);

    m_window.setView(prev);
}

void Renderer::drawWarmongerHighlights(sf::RenderTarget& target, const std::vector<Country>& countries, const Map& map) {
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

                target.draw(border);
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

void Renderer::drawWarHighlights(sf::RenderTarget& target, const std::vector<Country>& countries, const Map& map) {
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

            target.draw(border);
        }
    }
}

void Renderer::setNeedsUpdate(bool needsUpdate) {
    m_needsUpdate = needsUpdate;
}

void Renderer::setHoveredCountryIndex(int countryIndex) {
    m_hoveredCountryIndex = countryIndex;
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

sf::FloatRect Renderer::getViewToggleButtonBounds() const {
    const float w = 120.0f;
    const float h = 34.0f;
    const float pad = 12.0f;
    return sf::FloatRect(static_cast<float>(m_window.getSize().x) - w - pad, pad, w, h);
}

void Renderer::resetGlobeView() {
    m_globeYaw = 0.0f;
    m_globePitch = 0.0f;
    m_globeRadiusScale = 0.45f;
}

void Renderer::addGlobeRotation(float deltaYawRadians, float deltaPitchRadians) {
    m_globeYaw += deltaYawRadians;
    m_globePitch += deltaPitchRadians;

    const float pi = 3.14159265358979323846f;
    if (m_globeYaw > pi) {
        m_globeYaw -= 2.0f * pi;
    } else if (m_globeYaw < -pi) {
        m_globeYaw += 2.0f * pi;
    }

    const float maxPitch = 1.35f;
    m_globePitch = std::max(-maxPitch, std::min(maxPitch, m_globePitch));
}

void Renderer::addGlobeRadiusScale(float delta) {
    m_globeRadiusScale = std::max(0.25f, std::min(0.49f, m_globeRadiusScale + delta));
}

sf::Vector2f Renderer::globeCenter() const {
    return sf::Vector2f(static_cast<float>(m_window.getSize().x) * 0.5f,
                        static_cast<float>(m_window.getSize().y) * 0.5f + 20.0f);
}

float Renderer::globeRadiusPx() const {
    const float w = static_cast<float>(m_window.getSize().x);
    const float h = static_cast<float>(m_window.getSize().y);
    return std::min(w, h) * m_globeRadiusScale;
}

void Renderer::ensureStarfield() {
    const sf::Vector2u sz = m_window.getSize();
    if (m_starWindowSize == sz && m_starVerts.getVertexCount() > 0) {
        return;
    }
    m_starWindowSize = sz;

    const int w = static_cast<int>(sz.x);
    const int h = static_cast<int>(sz.y);
    if (w <= 0 || h <= 0) {
        m_starVerts.clear();
        return;
    }

    const int desired = std::max(800, std::min(3000, (w * h) / 2500));

    std::mt19937 rng(1337u);
    std::uniform_real_distribution<float> xDist(0.0f, static_cast<float>(w));
    std::uniform_real_distribution<float> yDist(0.0f, static_cast<float>(h));
    std::uniform_int_distribution<int> brightDist(150, 255);
    std::uniform_int_distribution<int> alphaDist(60, 200);

    m_starVerts.setPrimitiveType(sf::Points);
    m_starVerts.resize(static_cast<size_t>(desired));
    for (int i = 0; i < desired; ++i) {
        sf::Vertex v;
        v.position = sf::Vector2f(xDist(rng), yDist(rng));
        int b = brightDist(rng);
        int a = alphaDist(rng);
        v.color = sf::Color(static_cast<sf::Uint8>(b), static_cast<sf::Uint8>(b), static_cast<sf::Uint8>(b), static_cast<sf::Uint8>(a));
        m_starVerts[static_cast<size_t>(i)] = v;
    }
}

bool Renderer::ensureWorldComposite(const Map& map) {
    if (!sf::Shader::isAvailable()) {
        return false;
    }

    const sf::Vector2u mapSize = map.getBaseImage().getSize();
    if (mapSize.x == 0 || mapSize.y == 0) {
        return false;
    }

    const unsigned int maxTex = sf::Texture::getMaximumSize();
    const unsigned int cap = std::min(maxTex, 4096u);

    float scale = 1.0f;
    if (mapSize.x > cap || mapSize.y > cap) {
        scale = std::min(static_cast<float>(cap) / static_cast<float>(mapSize.x),
                         static_cast<float>(cap) / static_cast<float>(mapSize.y));
        scale = std::max(0.01f, std::min(1.0f, scale));
    }

    const unsigned int w = std::max(1u, static_cast<unsigned int>(std::lround(static_cast<double>(mapSize.x) * scale)));
    const unsigned int h = std::max(1u, static_cast<unsigned int>(std::lround(static_cast<double>(mapSize.y) * scale)));

    if (m_worldCompositeRT.getSize().x != w || m_worldCompositeRT.getSize().y != h) {
        if (!m_worldCompositeRT.create(w, h)) {
            return false;
        }
        m_worldCompositeRT.setSmooth(true);
    }
    m_worldCompositeScale = scale;

    if (!m_globeShaderReady) {
        m_globeShaderReady = m_globeShader.loadFromMemory(kGlobeFragmentShader, sf::Shader::Fragment);
    }
    return m_globeShaderReady;
}

bool Renderer::globeScreenToMapPixel(sf::Vector2i mousePx, const Map& map, sf::Vector2f& outMapPixel) const {
    const float r = globeRadiusPx();
    if (r <= 1.0f) {
        return false;
    }

    const sf::Vector2f c = globeCenter();
    const float x = (static_cast<float>(mousePx.x) + 0.5f - c.x) / r;
    const float y = (static_cast<float>(mousePx.y) + 0.5f - c.y) / r;

    const float r2 = x * x + y * y;
    if (r2 > 1.0f) {
        return false;
    }

    const float z = std::sqrt(std::max(0.0f, 1.0f - r2));
    float vx = x;
    float vy = -y;
    float vz = z;

    const float cy = std::cos(m_globeYaw);
    const float sy = std::sin(m_globeYaw);
    float rx = cy * vx + sy * vz;
    float ry = vy;
    float rz = -sy * vx + cy * vz;

    const float cx = std::cos(m_globePitch);
    const float sx = std::sin(m_globePitch);
    float fx = rx;
    float fy = cx * ry - sx * rz;
    float fz = sx * ry + cx * rz;

    const float pi = 3.14159265358979323846f;
    float lon = std::atan2(fx, fz);
    float lat = std::asin(std::max(-1.0f, std::min(1.0f, fy)));

    float u = lon / (2.0f * pi) + 0.5f;
    u = u - std::floor(u);
    float v = 0.5f - lat / pi;
    v = std::max(0.0f, std::min(1.0f, v));

    const sf::Vector2u mapSize = map.getBaseImage().getSize();
    outMapPixel.x = u * static_cast<float>(mapSize.x);
    outMapPixel.y = v * static_cast<float>(mapSize.y);
    return true;
}

bool Renderer::globeScreenToGrid(sf::Vector2i mousePx, const Map& map, sf::Vector2i& outGrid) const {
    sf::Vector2f mapPixel;
    if (!globeScreenToMapPixel(mousePx, map, mapPixel)) {
        return false;
    }
    outGrid = map.pixelToGrid(mapPixel);
    return true;
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
        if (m_countryGridWidth > 0 && m_countryGridHeight > 0) {
            m_countryOverlayShader.setUniform("idTexel", sf::Glsl::Vec2(1.f / static_cast<float>(m_countryGridWidth),
                                                                       1.f / static_cast<float>(m_countryGridHeight)));
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

    if (tryLoadPlaneTexture(m_planeTexture, "plane.png", "Plane.png")) {
        sf::Vector2u texSize = m_planeTexture.getSize();
        m_planeSprite.setTexture(m_planeTexture);
        m_planeSprite.setOrigin(static_cast<float>(texSize.x) * 0.5f, static_cast<float>(texSize.y) * 0.5f);
        float targetSize = std::max(10.f, static_cast<float>(map.getGridCellSize()) * 14.f);
        float maxDimension = static_cast<float>(std::max(texSize.x, texSize.y));
        if (maxDimension > 0.f) {
            float scale = targetSize / maxDimension;
            m_planeSprite.setScale(scale, scale);
        }
	        m_planeSprite.setColor(sf::Color(255, 255, 255, 230));
	    }

    if (tryLoadPlaneTexture(m_shipTexture, "containership.png", "ContainerShip.png")) {
        sf::Vector2u texSize = m_shipTexture.getSize();
        m_shipSprite.setTexture(m_shipTexture);
        m_shipSprite.setOrigin(static_cast<float>(texSize.x) * 0.5f, static_cast<float>(texSize.y) * 0.5f);
        float targetH = std::max(5.f, static_cast<float>(map.getGridCellSize()) * 6.f);
        if (texSize.y > 0u) {
            float scale = targetH / static_cast<float>(texSize.y);
            m_shipSprite.setScale(scale, scale);
        }
        m_shipSprite.setColor(sf::Color(255, 255, 255, 235));
    }

	    // Globe/star caches depend on window size and GPU resources.
	    m_starWindowSize = sf::Vector2u(0u, 0u);
	    m_starVerts.clear();

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

    // Header
    {
        sf::Text header;
        header.setFont(m_font);
        header.setCharacterSize(16);
        header.setFillColor(sf::Color(150, 200, 255));
        header.setString("=== INSTITUTIONS ===");
        header.setPosition(m_infoWindowBackground.getPosition().x + 20, startY - 24);
        m_window.draw(header);
    }

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

sf::FloatRect Renderer::getMegaTimeJumpDebugCheckboxBounds() const {
    const float boxSize = 24.0f;
    const float x = static_cast<float>(m_window.getSize().x) * 0.5f - 180.0f;
    const float y = static_cast<float>(m_window.getSize().y) * 0.5f + 95.0f;
    return sf::FloatRect(x, y, boxSize, boxSize);
}

void Renderer::renderMegaTimeJumpScreen(const std::string& inputText, const sf::Font& font, bool debugLogEnabled) {
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

    const sf::FloatRect checkboxBounds = getMegaTimeJumpDebugCheckboxBounds();
    sf::RectangleShape checkbox(sf::Vector2f(checkboxBounds.width, checkboxBounds.height));
    checkbox.setPosition(checkboxBounds.left, checkboxBounds.top);
    checkbox.setFillColor(sf::Color(30, 30, 30));
    checkbox.setOutlineColor(sf::Color::White);
    checkbox.setOutlineThickness(2.0f);
    m_window.draw(checkbox);

    if (debugLogEnabled) {
        sf::RectangleShape mark(sf::Vector2f(checkboxBounds.width - 8.0f, checkboxBounds.height - 8.0f));
        mark.setPosition(checkboxBounds.left + 4.0f, checkboxBounds.top + 4.0f);
        mark.setFillColor(sf::Color(0, 180, 120));
        m_window.draw(mark);
    }

    sf::Text debugLabel;
    debugLabel.setFont(font);
    debugLabel.setCharacterSize(18);
    debugLabel.setFillColor(sf::Color::White);
    debugLabel.setString("Debug CSV (100y population sums)");
    debugLabel.setPosition(checkboxBounds.left + checkboxBounds.width + 12.0f, checkboxBounds.top - 1.0f);
    m_window.draw(debugLabel);

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
        case 4: // Save/Reset
            stateText = "5. Save or Reset";
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

void Renderer::drawRoadNetwork(sf::RenderTarget& target, const Country& country, const Map& map, const TechnologyManager& technologyManager, const sf::FloatRect& visibleArea) {
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
            target.draw(railBase);

            railGlow.setPosition(roadWorldPos.x + cellSize / 2.f, roadWorldPos.y + cellSize / 2.f);
            target.draw(railGlow);
        } else {
            roadSurface.setPosition(roadWorldPos);
            target.draw(roadSurface);

            roadHighlight.setPosition(roadWorldPos.x + cellSize / 2.f, roadWorldPos.y + cellSize / 2.f);
            target.draw(roadHighlight);
        }
    }
}

void Renderer::drawFactories(sf::RenderTarget& target, const std::vector<Country>& countries, const Map& map, const sf::FloatRect& visibleArea) {
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
            target.draw(m_factorySprite);
        }
    }

    m_factorySprite.setColor(sf::Color::White);
}

void Renderer::drawPorts(sf::RenderTarget& target, const std::vector<Country>& countries, const Map& map, const sf::FloatRect& visibleArea) {
    const float cellSize = static_cast<float>(map.getGridCellSize());
    const float symbolSize = std::max(8.f, cellSize * 12.f);
    const float half = symbolSize * 0.6f;

    sf::RectangleShape stem(sf::Vector2f(std::max(1.0f, symbolSize * 0.14f), symbolSize * 0.75f));
    stem.setOrigin(stem.getSize().x * 0.5f, stem.getSize().y * 0.15f);

    sf::RectangleShape cross(sf::Vector2f(symbolSize * 0.46f, std::max(1.0f, symbolSize * 0.12f)));
    cross.setOrigin(cross.getSize().x * 0.5f, cross.getSize().y * 0.5f);

    sf::CircleShape ring(symbolSize * 0.18f);
    ring.setOrigin(ring.getRadius(), ring.getRadius());
    ring.setFillColor(sf::Color::Transparent);
    ring.setOutlineThickness(std::max(1.0f, symbolSize * 0.08f));

    sf::CircleShape base(symbolSize * 0.32f);
    base.setOrigin(base.getRadius(), base.getRadius());
    base.setFillColor(sf::Color::Transparent);
    base.setOutlineThickness(std::max(1.0f, symbolSize * 0.08f));

    sf::VertexArray flukes(sf::Lines, 4);

    auto drawAnchor = [&](const sf::Vector2f& center, const sf::Color& tint) {
        sf::Color shadow(0, 0, 0, 170);
        sf::Vector2f shadowOff(1.2f, 1.2f);

        const float midY = center.y - symbolSize * 0.05f;
        const float baseY = center.y + symbolSize * 0.28f;

        ring.setOutlineColor(shadow);
        ring.setPosition(center + shadowOff + sf::Vector2f(0.f, -symbolSize * 0.42f));
        target.draw(ring);

        stem.setFillColor(shadow);
        stem.setPosition(center + shadowOff);
        target.draw(stem);

        cross.setFillColor(shadow);
        cross.setPosition(center + shadowOff + sf::Vector2f(0.f, -symbolSize * 0.10f));
        target.draw(cross);

        base.setOutlineColor(shadow);
        base.setPosition(center + shadowOff + sf::Vector2f(0.f, symbolSize * 0.22f));
        target.draw(base);

        flukes[0].position = center + shadowOff + sf::Vector2f(-symbolSize * 0.36f, baseY);
        flukes[1].position = center + shadowOff + sf::Vector2f(-symbolSize * 0.10f, midY);
        flukes[2].position = center + shadowOff + sf::Vector2f(symbolSize * 0.36f, baseY);
        flukes[3].position = center + shadowOff + sf::Vector2f(symbolSize * 0.10f, midY);
        flukes[0].color = shadow;
        flukes[1].color = shadow;
        flukes[2].color = shadow;
        flukes[3].color = shadow;
        target.draw(flukes);

        sf::Color main = tint;
        main.a = 235;
        ring.setOutlineColor(main);
        ring.setPosition(center + sf::Vector2f(0.f, -symbolSize * 0.42f));
        target.draw(ring);

        stem.setFillColor(main);
        stem.setPosition(center);
        target.draw(stem);

        cross.setFillColor(main);
        cross.setPosition(center + sf::Vector2f(0.f, -symbolSize * 0.10f));
        target.draw(cross);

        base.setOutlineColor(main);
        base.setPosition(center + sf::Vector2f(0.f, symbolSize * 0.22f));
        target.draw(base);

        flukes[0].position = center + sf::Vector2f(-symbolSize * 0.36f, baseY);
        flukes[1].position = center + sf::Vector2f(-symbolSize * 0.10f, midY);
        flukes[2].position = center + sf::Vector2f(symbolSize * 0.36f, baseY);
        flukes[3].position = center + sf::Vector2f(symbolSize * 0.10f, midY);
        flukes[0].color = main;
        flukes[1].color = main;
        flukes[2].color = main;
        flukes[3].color = main;
        target.draw(flukes);
    };

    for (const auto& country : countries) {
        if (country.getPopulation() <= 0) {
            continue;
        }
        const auto& ports = country.getPorts();
        if (ports.empty()) {
            continue;
        }
        for (const auto& portPos : ports) {
            sf::Vector2f worldCenter((static_cast<float>(portPos.x) + 0.5f) * cellSize,
                                     (static_cast<float>(portPos.y) + 0.5f) * cellSize);
            sf::FloatRect bounds(worldCenter.x - half, worldCenter.y - half, half * 2.f, half * 2.f);
            if (!visibleArea.intersects(bounds)) {
                continue;
            }
            drawAnchor(worldCenter, country.getColor());
        }
    }
}

void Renderer::drawAirwayPlanes(sf::RenderTarget& target, const std::vector<Country>& countries, const Map& map) {
    if (!m_planeTexture.getSize().x || !m_planeTexture.getSize().y) {
        m_planeAnimClock.restart();
        return;
    }

    float dt = m_planeAnimClock.restart().asSeconds();
    if (dt > 0.10f) {
        dt = 0.10f;
    }

    const float cellSize = static_cast<float>(map.getGridCellSize());
    const float speed = 320.0f * std::max(1.0f, cellSize); // world units per second

    std::vector<std::uint64_t> liveKeys;
    liveKeys.reserve(128);

    const float pi = 3.14159265358979323846f;

    for (int i = 0; i < static_cast<int>(countries.size()); ++i) {
        const Country& a = countries[static_cast<size_t>(i)];
        if (a.getPopulation() <= 0) {
            continue;
        }

        for (int j : a.getAirways()) {
            if (j <= i) {
                continue;
            }
            if (j < 0 || j >= static_cast<int>(countries.size())) {
                continue;
            }
            const Country& b = countries[static_cast<size_t>(j)];
            if (b.getPopulation() <= 0) {
                continue;
            }

            const std::uint64_t key = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(i)) << 32) |
                                      static_cast<std::uint32_t>(j);
            liveKeys.push_back(key);

            auto [it, inserted] = m_airwayAnim.insert({key, AirwayAnimState{}});
            AirwayAnimState& st = it->second;
            if (inserted) {
                st.t = static_cast<float>((key % 997u)) / 997.0f;
                st.forward = ((key & 1u) == 0u);
            }

            sf::Vector2i aCell = a.getCapitalLocation();
            sf::Vector2i bCell = b.getCapitalLocation();
            sf::Vector2f pA((static_cast<float>(aCell.x) + 0.5f) * cellSize,
                            (static_cast<float>(aCell.y) + 0.5f) * cellSize);
            sf::Vector2f pB((static_cast<float>(bCell.x) + 0.5f) * cellSize,
                            (static_cast<float>(bCell.y) + 0.5f) * cellSize);

            sf::Vector2f dSeg = pB - pA;
            float dist = std::sqrt(dSeg.x * dSeg.x + dSeg.y * dSeg.y);
            if (dist < 1.0f) {
                continue;
            }

            float deltaT = (speed / dist) * dt;
            if (st.forward) {
                st.t += deltaT;
                if (st.t >= 1.0f) {
                    st.t = 1.0f;
                    st.forward = false;
                }
            } else {
                st.t -= deltaT;
                if (st.t <= 0.0f) {
                    st.t = 0.0f;
                    st.forward = true;
                }
            }

            sf::Vector2f pos = pA + dSeg * st.t;
            sf::Vector2f dir = st.forward ? (pB - pA) : (pA - pB);
            float ang = std::atan2(dir.y, dir.x) * 180.0f / pi;

            m_planeSprite.setRotation(ang);
            m_planeSprite.setPosition(pos);
            target.draw(m_planeSprite);
        }
    }

    if (m_airwayAnim.empty()) {
        return;
    }

    // Remove stale animation states.
    if (!liveKeys.empty()) {
        std::sort(liveKeys.begin(), liveKeys.end());
        for (auto it = m_airwayAnim.begin(); it != m_airwayAnim.end(); ) {
            if (!std::binary_search(liveKeys.begin(), liveKeys.end(), it->first)) {
                it = m_airwayAnim.erase(it);
            } else {
                ++it;
            }
        }
    } else {
        m_airwayAnim.clear();
    }
}

void Renderer::drawShippingShips(sf::RenderTarget& target, const TradeManager& tradeManager, const Map& map, const sf::FloatRect& visibleArea) {
    if (!m_shipTexture.getSize().x || !m_shipTexture.getSize().y) {
        m_shipAnimClock.restart();
        return;
    }

    float dt = m_shipAnimClock.restart().asSeconds();
    if (dt > 0.10f) {
        dt = 0.10f;
    }

    const float cellSize = static_cast<float>(map.getGridCellSize());
    const float speed = (320.0f * std::max(1.0f, cellSize)) * 0.25f; // ~0.25x plane speed

    const auto& routes = tradeManager.getShippingRoutes();
    if (routes.empty()) {
        if (!m_shipAnim.empty()) {
            m_shipAnim.clear();
        }
        return;
    }

    std::vector<std::uint64_t> liveKeys;
    liveKeys.reserve(128);
    const float pi = 3.14159265358979323846f;

    for (const auto& route : routes) {
        if (!route.isActive) continue;
        if (route.navPath.size() < 2) continue;
        if (route.cumulativeLen.size() != route.navPath.size()) continue;
        if (route.totalLen <= 0.0f) continue;

        const int a = route.fromCountryIndex;
        const int b = route.toCountryIndex;
        if (a < 0 || b < 0) continue;

        const std::uint32_t lo = static_cast<std::uint32_t>(std::min(a, b));
        const std::uint32_t hi = static_cast<std::uint32_t>(std::max(a, b));
        const std::uint64_t key = (static_cast<std::uint64_t>(lo) << 32) | static_cast<std::uint64_t>(hi);
        liveKeys.push_back(key);

        auto [it, inserted] = m_shipAnim.insert({key, ShipAnimState{}});
        ShipAnimState& st = it->second;

        const float totalWorldLen = route.totalLen * cellSize;
        if (inserted) {
            float seed = static_cast<float>((key % 1009u)) / 1009.0f;
            st.s = seed * totalWorldLen;
            st.forward = ((key & 1u) == 0u);
        }

        const float travel = speed * dt;
        if (st.forward) {
            st.s += travel;
            if (st.s >= totalWorldLen) {
                st.s = totalWorldLen;
                st.forward = false;
            }
        } else {
            st.s -= travel;
            if (st.s <= 0.0f) {
                st.s = 0.0f;
                st.forward = true;
            }
        }

        const float sNav = (cellSize > 0.0f) ? (st.s / cellSize) : st.s;
        auto ub = std::upper_bound(route.cumulativeLen.begin(), route.cumulativeLen.end(), sNav);
        size_t seg = 0;
        if (ub == route.cumulativeLen.begin()) {
            seg = 0;
        } else {
            seg = static_cast<size_t>((ub - route.cumulativeLen.begin()) - 1);
        }
        if (seg >= route.navPath.size() - 1) {
            seg = route.navPath.size() - 2;
        }

        const float segStart = route.cumulativeLen[seg];
        const float segEnd = route.cumulativeLen[seg + 1];
        const float denom = std::max(0.0001f, segEnd - segStart);
        float t = (sNav - segStart) / denom;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;

        auto nodeToWorld = [&](const Vector2i& n) -> sf::Vector2f {
            float cx = (static_cast<float>(n.x) * static_cast<float>(route.navStep) + static_cast<float>(route.navStep) * 0.5f) * cellSize;
            float cy = (static_cast<float>(n.y) * static_cast<float>(route.navStep) + static_cast<float>(route.navStep) * 0.5f) * cellSize;
            return sf::Vector2f(cx, cy);
        };

        sf::Vector2f p0 = nodeToWorld(route.navPath[seg]);
        sf::Vector2f p1 = nodeToWorld(route.navPath[seg + 1]);
        sf::Vector2f pos = p0 + (p1 - p0) * t;

        if (!visibleArea.contains(pos)) {
            continue;
        }

        sf::Vector2f dir = (p1 - p0);
        if (!st.forward) {
            dir = -dir;
        }
        float ang = std::atan2(dir.y, dir.x) * 180.0f / pi;

        m_shipSprite.setRotation(ang);
        m_shipSprite.setPosition(pos);
        target.draw(m_shipSprite);
    }

    if (m_shipAnim.empty()) {
        return;
    }

    // Remove stale animation states.
    if (!liveKeys.empty()) {
        std::sort(liveKeys.begin(), liveKeys.end());
        for (auto it = m_shipAnim.begin(); it != m_shipAnim.end(); ) {
            if (!std::binary_search(liveKeys.begin(), liveKeys.end(), it->first)) {
                it = m_shipAnim.erase(it);
            } else {
                ++it;
            }
        }
    } else {
        m_shipAnim.clear();
    }
}

void Renderer::drawTradeRoutes(sf::RenderTarget& target, const TradeManager& tradeManager, const std::vector<Country>& countries, const Map& map, const sf::FloatRect& visibleArea) {
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
        target.draw(routeLines);
    }
}

void Renderer::drawPlagueOverlay(sf::RenderTarget& target, const Map& map, const std::vector<Country>& countries, const sf::FloatRect& visibleArea) {
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
        target.draw(plagueVertices);
    }
}

void Renderer::drawWarFrontlines(sf::RenderTarget& target, const std::vector<Country>& countries, const Map& map, const sf::FloatRect& visibleArea) {
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
        target.draw(frontline);
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
            target.draw(fort);
            fortRing.setPosition(pos);
            target.draw(fortRing);
        }
    }
}
