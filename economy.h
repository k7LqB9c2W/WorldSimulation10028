// economy.h
#pragma once

#include <SFML/Graphics.hpp>
#include <vector>

#include "country.h"
#include "map.h"
#include "technology.h"

class EconomyGPU {
public:
    struct Config {
        int econCellSize = 6;
        int tradeIters = 12;
        int updateReadbackEveryNYears = 1;
        float maxInvFood = 200.0f;
        float maxInvMat = 200.0f;
        float maxInvCons = 200.0f;
        float maxCapital = 500.0f;
    };

    EconomyGPU() = default;

    void init(const Map& map, int maxCountries, const Config& cfg);
    void onTerritoryChanged(const Map& map);
    void onStaticResourcesChanged(const Map& map);

    void tickYear(int year,
                  const Map& map,
                  const std::vector<Country>& countries,
                  const TechnologyManager& tech);

    void applyCountryMetrics(std::vector<Country>& countries) const;

    const sf::Texture& getDebugWealthHeatmapTexture() const;

private:
    Config m_cfg{};
    bool m_initialized = false;

    int m_mapW = 0, m_mapH = 0;
    int m_econW = 0, m_econH = 0;
    int m_maxCountries = 0;

    // Textures
    sf::Texture m_countryIdTex;        // econW x econH, RG encodes (countryIndex + 1)
    sf::Texture m_resourcePotential;   // econW x econH, RGB: food/mat/cons potentials
    sf::Texture m_countryStatsTex;     // palette: (maxCountries + 1) x 1, RGBA stats

    // Ping-pong state (inventories/capital)
    sf::RenderTexture m_stateA;
    sf::RenderTexture m_stateB;
    bool m_stateSrcIsA = true;

    // Reused as infrastructure/capacity storage (RG), reserved otherwise
    sf::RenderTexture m_priceA;
    sf::RenderTexture m_priceB;

    // Debug output heatmap
    sf::RenderTexture m_debugWealthHeatmap;

    // Shaders
    sf::Shader m_prodConsumeShader;
    sf::Shader m_tradeShader;
    sf::Shader m_debugHeatmapShader;

    // CPU buffers for uploads/readback
    std::vector<sf::Uint8> m_countryIdPixels;     // econW*econH*4
    std::vector<sf::Uint8> m_resourcePixels;      // econW*econH*4
    std::vector<sf::Uint8> m_countryStatsPixels;  // paletteW*1*4
    std::vector<sf::Uint8> m_infraPixels;         // econW*econH*4 (access/capacity)

    // CPU cached access per econ cell (for exports estimation)
    std::vector<float> m_accessCPU;               // econW*econH

    // Per-country results (computed on CPU from readback), indexed by encoded id (0..maxCountries)
    mutable std::vector<double> m_countryWealth;
    mutable std::vector<double> m_countryGDP;
    mutable std::vector<double> m_countryExports;

    // CPU cached invest rates per encoded id (0..maxCountries), for GDP estimation
    std::vector<float> m_countryInvestRate;

    // Readback history for GDP estimation
    std::vector<sf::Uint8> m_prevStatePixels;
    int m_prevReadbackYear = 0;
    bool m_hasPrevReadback = false;
    int m_lastReadbackYear = 0;
    bool m_hasAnyReadback = false;

    void rebuildCountryId(const Map& map);
    void rebuildResourcePotential(const Map& map);
    void rebuildCountryStats(const std::vector<Country>& countries,
                             const TechnologyManager& tech);

    sf::RenderTexture& stateSrc();
    sf::RenderTexture& stateDst();
    const sf::Texture& stateSrcTexture() const;
    void flipState();
    void computeCountryMetricsCPU(int year);
};
