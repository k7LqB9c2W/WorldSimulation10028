// economy.cpp

#include "economy.h"
#include "trade.h"
#include "news.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <limits>
#include <unordered_map>

namespace {
const char* kProdConsumeFragmentShader = R"(
uniform sampler2D countryIdTex;
uniform sampler2D resourceTex;
uniform sampler2D countryStatsTex;
uniform sampler2D stateTex;
uniform sampler2D infraTex;
uniform float paletteSize;

uniform float maxFood;
uniform float maxMat;
uniform float maxCons;
uniform float maxCap;
uniform float dtYears;

uniform float baseFoodDemand;
uniform float baseConsDemand;

float decodeCountryId(vec4 enc) {
    float low = floor(enc.r * 255.0 + 0.5);
    float high = floor(enc.g * 255.0 + 0.5);
    return low + high * 256.0;
}

vec4 sampleCountryStats(float id) {
    float u = (id + 0.5) / paletteSize;
    return texture2D(countryStatsTex, vec2(u, 0.5));
}

void main() {
    vec2 uv = gl_TexCoord[0].xy;

    float id = decodeCountryId(texture2D(countryIdTex, uv));
    if (id < 0.5) {
        gl_FragColor = vec4(0.0);
        return;
    }

    vec4 stats = sampleCountryStats(id);
    float popF = stats.r;
    float prodF = stats.g;
    float stability = stats.b;
    float investRate = stats.a;

    vec4 res = texture2D(resourceTex, uv);
    float foodPot = res.r;
    float matPot = res.g;
    float consPot = res.b;

    vec4 st = texture2D(stateTex, uv);
    float food = st.r * maxFood;
    float mat  = st.g * maxMat;
    float cons = st.b * maxCons;
    float capN = clamp(st.a, 0.0, 1.0);
    float cap  = (capN * capN) * maxCap;

    vec4 infra = texture2D(infraTex, uv);
    float access = clamp(infra.r, 0.05, 1.0);
    float capacity = clamp(infra.g, 0.05, 1.0);

    float workforce = popF;
    float prod = (0.6 + 0.8 * prodF) * stability;

    float years = max(0.0, dtYears);
    float foodProd = foodPot * workforce * access * capacity * prod * 25.0 * years;
    float matProd  = max(matPot, 0.12) * workforce * access * capacity * prod * 22.0 * years;

    float convert = min(mat, workforce * access * prod * 14.0 * years);
    float servicesProd = consPot * workforce * access * capacity * (0.45 + 0.55 * prodF) * 16.0 * years;
    float consProd = convert * (0.7 + 0.9 * prodF) + servicesProd;

    food += foodProd;
    mat  += matProd - convert;
    cons += consProd;

    float foodDem = baseFoodDemand * popF * 30.0 * years;
    float consDem = baseConsDemand * popF * 18.0 * years;

    food = max(0.0, food - foodDem);
    cons = max(0.0, cons - consDem);

    float valueAdded = foodProd * 1.0 + matProd * 1.5 + consProd * 2.2;
    cap += max(0.0, valueAdded) * investRate * 0.05;

    food = clamp(food, 0.0, maxFood);
    mat  = clamp(mat,  0.0, maxMat);
    cons = clamp(cons, 0.0, maxCons);
    cap  = clamp(cap,  0.0, maxCap);

    float capOutN = (maxCap > 0.0) ? sqrt(cap / maxCap) : 0.0;
    gl_FragColor = vec4(food / maxFood, mat / maxMat, cons / maxCons, capOutN);
}
)";

const char* kTradeFragmentShader = R"(
uniform sampler2D stateTex;
uniform sampler2D infraTex;
uniform vec2 texelStep;
uniform float kFlow;

void main() {
    vec2 uv = gl_TexCoord[0].xy;

    vec4 sC = texture2D(stateTex, uv);
    vec4 sL = texture2D(stateTex, uv + vec2(-texelStep.x, 0.0));
    vec4 sR = texture2D(stateTex, uv + vec2( texelStep.x, 0.0));
    vec4 sU = texture2D(stateTex, uv + vec2(0.0, -texelStep.y));
    vec4 sD = texture2D(stateTex, uv + vec2(0.0,  texelStep.y));

    float access = clamp(texture2D(infraTex, uv).r, 0.05, 1.0);

    vec3 invC = sC.rgb;
    vec3 avgN = (sL.rgb + sR.rgb + sU.rgb + sD.rgb) * 0.25;

    vec3 delta = (avgN - invC) * (kFlow * access);
    vec3 invOut = clamp(invC + delta, 0.0, 1.0);

    gl_FragColor = vec4(invOut, sC.a);
}
)";

const char* kDebugWealthHeatmapFragmentShader = R"(
uniform sampler2D stateTex;
uniform float maxFood;
uniform float maxMat;
uniform float maxCons;
uniform float maxCap;

vec3 ramp(float t) {
    t = clamp(t, 0.0, 1.0);
    vec3 a = vec3(0.10, 0.10, 0.16);
    vec3 b = vec3(0.20, 0.35, 0.70);
    vec3 c = vec3(0.95, 0.75, 0.18);
    vec3 d = vec3(0.95, 0.22, 0.12);
    if (t < 0.5) {
        return mix(a, b, t * 2.0);
    }
    if (t < 0.85) {
        return mix(b, c, (t - 0.5) / 0.35);
    }
    return mix(c, d, (t - 0.85) / 0.15);
}

void main() {
    vec2 uv = gl_TexCoord[0].xy;
    vec4 st = texture2D(stateTex, uv);
    float food = st.r * maxFood;
    float mat  = st.g * maxMat;
    float cons = st.b * maxCons;
    float capN = clamp(st.a, 0.0, 1.0);
    float cap  = (capN * capN) * maxCap;

    float wealth = food * 1.0 + mat * 1.5 + cons * 2.2 + cap * 3.0;
    float t = wealth / (maxFood + maxMat * 1.5 + maxCons * 2.2 + maxCap * 3.0);
    gl_FragColor = vec4(ramp(t), 0.75);
}
)";

inline float clamp01(float v) {
    return std::max(0.0f, std::min(1.0f, v));
}
} // namespace

void EconomyGPU::init(const Map& map, int maxCountries, const Config& cfg) {
    m_cfg = cfg;
    m_maxCountries = std::max(0, maxCountries);

    // Always allocate per-country metric buffers so a CPU fallback can run even when GPU init fails.
    m_countryWealth.assign(static_cast<size_t>(m_maxCountries + 1), 0.0);
    m_countryGDP.assign(static_cast<size_t>(m_maxCountries + 1), 0.0);
    m_countryExports.assign(static_cast<size_t>(m_maxCountries + 1), 0.0);
    m_countryInvestRate.assign(static_cast<size_t>(m_maxCountries + 1), 0.0f);
    m_fallbackPrevWealth.assign(static_cast<size_t>(m_maxCountries + 1), 0.0);
    m_hasFallbackPrev = false;

    const auto& grid = map.getCountryGrid();
    m_mapH = static_cast<int>(grid.size());
    m_mapW = (m_mapH > 0) ? static_cast<int>(grid[0].size()) : 0;

    if (m_mapW <= 0 || m_mapH <= 0 || m_cfg.econCellSize <= 0) {
        m_initialized = false;
        return;
    }

    m_econW = (m_mapW + m_cfg.econCellSize - 1) / m_cfg.econCellSize;
    m_econH = (m_mapH + m_cfg.econCellSize - 1) / m_cfg.econCellSize;

    if (!sf::Shader::isAvailable()) {
        m_initialized = false;
        return;
    }

    if (!m_countryIdTex.create(static_cast<unsigned>(m_econW), static_cast<unsigned>(m_econH))) {
        m_initialized = false;
        return;
    }
    m_countryIdTex.setSmooth(false);

    if (!m_resourcePotential.create(static_cast<unsigned>(m_econW), static_cast<unsigned>(m_econH))) {
        m_initialized = false;
        return;
    }
    m_resourcePotential.setSmooth(false);

    const unsigned paletteSize = static_cast<unsigned>(m_maxCountries + 1);
    if (!m_countryStatsTex.create(paletteSize, 1u)) {
        m_initialized = false;
        return;
    }
    m_countryStatsTex.setSmooth(false);

    if (!m_stateA.create(static_cast<unsigned>(m_econW), static_cast<unsigned>(m_econH)) ||
        !m_stateB.create(static_cast<unsigned>(m_econW), static_cast<unsigned>(m_econH)) ||
        !m_priceA.create(static_cast<unsigned>(m_econW), static_cast<unsigned>(m_econH)) ||
        !m_priceB.create(static_cast<unsigned>(m_econW), static_cast<unsigned>(m_econH))) {
        m_initialized = false;
        return;
    }

    m_stateA.setSmooth(false);
    m_stateB.setSmooth(false);
    m_priceA.setSmooth(false);
    m_priceB.setSmooth(false);

    if (!m_debugWealthHeatmap.create(static_cast<unsigned>(m_econW), static_cast<unsigned>(m_econH))) {
        m_initialized = false;
        return;
    }
    m_debugWealthHeatmap.setSmooth(false);

    const bool okProd = m_prodConsumeShader.loadFromMemory(kProdConsumeFragmentShader, sf::Shader::Fragment);
    const bool okTrade = m_tradeShader.loadFromMemory(kTradeFragmentShader, sf::Shader::Fragment);
    const bool okHeat = m_debugHeatmapShader.loadFromMemory(kDebugWealthHeatmapFragmentShader, sf::Shader::Fragment);
    if (!okProd || !okTrade || !okHeat) {
        std::cout << "EconomyGPU: shader compile failed:"
                  << " prodConsume=" << (okProd ? "ok" : "FAIL")
                  << " trade=" << (okTrade ? "ok" : "FAIL")
                  << " heatmap=" << (okHeat ? "ok" : "FAIL")
                  << std::endl;
        m_initialized = false;
        return;
    }

    // (re)allocated above for fallback; keep existing storage.
    m_countryIdPixels.assign(static_cast<size_t>(m_econW) * static_cast<size_t>(m_econH) * 4u, 0);
    m_resourcePixels.assign(static_cast<size_t>(m_econW) * static_cast<size_t>(m_econH) * 4u, 0);
    m_infraPixels.assign(static_cast<size_t>(m_econW) * static_cast<size_t>(m_econH) * 4u, 0);
    m_accessCPU.assign(static_cast<size_t>(m_econW) * static_cast<size_t>(m_econH), 1.0f);

    // Clear state & infra.
    m_stateA.clear(sf::Color(0, 0, 0, 0));
    m_stateA.display();
    m_stateB.clear(sf::Color(0, 0, 0, 0));
    m_stateB.display();
    m_stateSrcIsA = true;

    m_priceA.clear(sf::Color::Black);
    m_priceA.display();
    m_priceB.clear(sf::Color::Black);
    m_priceB.display();

    m_debugWealthHeatmap.clear(sf::Color::Black);
    m_debugWealthHeatmap.display();

    m_hasPrevReadback = false;
    m_hasAnyReadback = false;
    m_prevReadbackYear = 0;
    m_lastReadbackYear = 0;

    // Build initial downsampled inputs.
    rebuildCountryId(map);
    rebuildResourcePotential(map);

    m_initialized = true;
    std::cout << "EconomyGPU: initialized (" << m_econW << "x" << m_econH
              << ", maxCountries=" << m_maxCountries << ", econCellSize=" << m_cfg.econCellSize << ")"
              << std::endl;
}

void EconomyGPU::onTerritoryChanged(const Map& map) {
    if (!m_initialized) {
        return;
    }
    rebuildCountryId(map);
}

void EconomyGPU::onStaticResourcesChanged(const Map& map) {
    if (!m_initialized) {
        return;
    }
    rebuildResourcePotential(map);
}

void EconomyGPU::tickYear(int year,
                          const Map&,
                          const std::vector<Country>& countries,
                          const TechnologyManager& tech) {
    if (!m_initialized) {
        computeCountryMetricsFallback(year, countries, tech, 1.0f);
        m_lastReadbackYear = year;
        m_hasAnyReadback = true;
        return;
    }

    rebuildCountryStats(countries, tech);

    // Pass A: production + consumption
    {
        m_prodConsumeShader.setUniform("countryIdTex", m_countryIdTex);
        m_prodConsumeShader.setUniform("resourceTex", m_resourcePotential);
        m_prodConsumeShader.setUniform("countryStatsTex", m_countryStatsTex);
        m_prodConsumeShader.setUniform("infraTex", m_priceA.getTexture());
        m_prodConsumeShader.setUniform("paletteSize", static_cast<float>(m_maxCountries + 1));
        m_prodConsumeShader.setUniform("maxFood", m_cfg.maxInvFood);
        m_prodConsumeShader.setUniform("maxMat", m_cfg.maxInvMat);
        m_prodConsumeShader.setUniform("maxCons", m_cfg.maxInvCons);
        m_prodConsumeShader.setUniform("maxCap", m_cfg.maxCapital);
        m_prodConsumeShader.setUniform("dtYears", 1.0f);
        m_prodConsumeShader.setUniform("baseFoodDemand", 0.18f);
        m_prodConsumeShader.setUniform("baseConsDemand", 0.06f);
        m_prodConsumeShader.setUniform("stateTex", sf::Shader::CurrentTexture);

        sf::Sprite sprite(stateSrcTexture());
        stateDst().clear(sf::Color(0, 0, 0, 0));
        sf::RenderStates states;
        states.shader = &m_prodConsumeShader;
        states.blendMode = sf::BlendNone;
        stateDst().draw(sprite, states);
        stateDst().display();
        flipState();
    }

    // Readback & aggregate per-country metrics occasionally.
    // Do this immediately after production/consumption (before diffusion) so exports
    // are measured before trade smoothing reduces border gradients.
    const int everyN = std::max(1, m_cfg.updateReadbackEveryNYears);
    if (!m_hasAnyReadback || (year - m_lastReadbackYear) >= everyN) {
        computeCountryMetricsCPU(year);
        m_lastReadbackYear = year;
        m_hasAnyReadback = true;
    }

    // Pass B: trade diffusion (approximate)
    {
        m_tradeShader.setUniform("infraTex", m_priceA.getTexture());
        m_tradeShader.setUniform("texelStep",
                                 sf::Glsl::Vec2(1.0f / static_cast<float>(m_econW),
                                               1.0f / static_cast<float>(m_econH)));
        m_tradeShader.setUniform("kFlow", 0.06f);
        m_tradeShader.setUniform("stateTex", sf::Shader::CurrentTexture);

        for (int i = 0; i < std::max(0, m_cfg.tradeIters); ++i) {
            sf::Sprite sprite(stateSrcTexture());
            stateDst().clear(sf::Color(0, 0, 0, 0));
            sf::RenderStates states;
            states.shader = &m_tradeShader;
            states.blendMode = sf::BlendNone;
            stateDst().draw(sprite, states);
            stateDst().display();
            flipState();
        }
    }

    // Debug heatmap
    {
        m_debugHeatmapShader.setUniform("stateTex", sf::Shader::CurrentTexture);
        m_debugHeatmapShader.setUniform("maxFood", m_cfg.maxInvFood);
        m_debugHeatmapShader.setUniform("maxMat", m_cfg.maxInvMat);
        m_debugHeatmapShader.setUniform("maxCons", m_cfg.maxInvCons);
        m_debugHeatmapShader.setUniform("maxCap", m_cfg.maxCapital);

        sf::Sprite sprite(stateSrcTexture());
        m_debugWealthHeatmap.clear(sf::Color::Transparent);
        sf::RenderStates states;
        states.shader = &m_debugHeatmapShader;
        states.blendMode = sf::BlendNone;
        m_debugWealthHeatmap.draw(sprite, states);
        m_debugWealthHeatmap.display();
    }
}

void EconomyGPU::tickStepGpuOnly(int year,
                                 const Map&,
                                 const std::vector<Country>& countries,
                                 const TechnologyManager& tech,
                                 float dtYears,
                                 int tradeItersOverride,
                                 bool generateDebugHeatmap,
                                 bool readbackMetricsBeforeDiffusion) {
    if (!m_initialized) {
        computeCountryMetricsFallback(year, countries, tech, dtYears);
        m_lastReadbackYear = year;
        m_hasAnyReadback = true;
        return;
    }

    const int iters = std::max(0, tradeItersOverride);
    const float years = std::max(0.0f, dtYears);

    rebuildCountryStats(countries, tech);

    // Pass A: production + consumption (scaled by dtYears)
    {
        m_prodConsumeShader.setUniform("countryIdTex", m_countryIdTex);
        m_prodConsumeShader.setUniform("resourceTex", m_resourcePotential);
        m_prodConsumeShader.setUniform("countryStatsTex", m_countryStatsTex);
        m_prodConsumeShader.setUniform("infraTex", m_priceA.getTexture());
        m_prodConsumeShader.setUniform("paletteSize", static_cast<float>(m_maxCountries + 1));
        m_prodConsumeShader.setUniform("maxFood", m_cfg.maxInvFood);
        m_prodConsumeShader.setUniform("maxMat", m_cfg.maxInvMat);
        m_prodConsumeShader.setUniform("maxCons", m_cfg.maxInvCons);
        m_prodConsumeShader.setUniform("maxCap", m_cfg.maxCapital);
        m_prodConsumeShader.setUniform("dtYears", years);
        m_prodConsumeShader.setUniform("baseFoodDemand", 0.18f);
        m_prodConsumeShader.setUniform("baseConsDemand", 0.06f);
        m_prodConsumeShader.setUniform("stateTex", sf::Shader::CurrentTexture);

        sf::Sprite sprite(stateSrcTexture());
        stateDst().clear(sf::Color(0, 0, 0, 0));
        sf::RenderStates states;
        states.shader = &m_prodConsumeShader;
        states.blendMode = sf::BlendNone;
        stateDst().draw(sprite, states);
        stateDst().display();
        flipState();
    }

    // Optional readback & aggregation immediately after production/consumption (before diffusion),
    // to keep GDP/exports meaningful during mega fast-forwards.
    if (readbackMetricsBeforeDiffusion) {
        const int everyN = std::max(1, m_cfg.updateReadbackEveryNYears);
        if (!m_hasAnyReadback || (year - m_lastReadbackYear) >= everyN) {
            computeCountryMetricsCPU(year);
            m_lastReadbackYear = year;
            m_hasAnyReadback = true;
        }
    }

    // Pass B: diffusion (scaled for dtYears but using fewer iterations).
    if (iters > 0) {
        const float baseFlow = 0.06f;
        const float totalFlow = 1.0f - std::pow(1.0f - baseFlow, years);
        const float perIterFlow = 1.0f - std::pow(std::max(0.0f, 1.0f - totalFlow), 1.0f / static_cast<float>(iters));

        m_tradeShader.setUniform("infraTex", m_priceA.getTexture());
        m_tradeShader.setUniform("texelStep",
                                 sf::Glsl::Vec2(1.0f / static_cast<float>(m_econW),
                                               1.0f / static_cast<float>(m_econH)));
        m_tradeShader.setUniform("kFlow", perIterFlow);
        m_tradeShader.setUniform("stateTex", sf::Shader::CurrentTexture);

        for (int i = 0; i < iters; ++i) {
            sf::Sprite sprite(stateSrcTexture());
            stateDst().clear(sf::Color(0, 0, 0, 0));
            sf::RenderStates states;
            states.shader = &m_tradeShader;
            states.blendMode = sf::BlendNone;
            stateDst().draw(sprite, states);
            stateDst().display();
            flipState();
        }
    }

    if (generateDebugHeatmap) {
        m_debugHeatmapShader.setUniform("stateTex", sf::Shader::CurrentTexture);
        m_debugHeatmapShader.setUniform("maxFood", m_cfg.maxInvFood);
        m_debugHeatmapShader.setUniform("maxMat", m_cfg.maxInvMat);
        m_debugHeatmapShader.setUniform("maxCons", m_cfg.maxInvCons);
        m_debugHeatmapShader.setUniform("maxCap", m_cfg.maxCapital);

        sf::Sprite sprite(stateSrcTexture());
        m_debugWealthHeatmap.clear(sf::Color::Transparent);
        sf::RenderStates states;
        states.shader = &m_debugHeatmapShader;
        states.blendMode = sf::BlendNone;
        m_debugWealthHeatmap.draw(sprite, states);
        m_debugWealthHeatmap.display();
    }

    (void)year;
}

void EconomyGPU::tickMegaChunkGpuOnly(int endYear,
                                      int yearsInChunk,
                                      const Map& map,
                                      const std::vector<Country>& countries,
                                      const TechnologyManager& tech,
                                      int yearsPerStep,
                                      int tradeItersPerStep) {
    if (!m_initialized) {
        return;
    }
    if (yearsInChunk <= 0) {
        return;
    }

    const int step = std::max(1, yearsPerStep);
    int remaining = yearsInChunk;
    int simYear = endYear - yearsInChunk;

    while (remaining > 0) {
        const int thisStep = std::min(step, remaining);
        simYear += thisStep;
        tickStepGpuOnly(simYear,
                        map,
                        countries,
                        tech,
                        static_cast<float>(thisStep),
                        tradeItersPerStep,
                        /*heatmap*/false,
                        /*readbackMetricsBeforeDiffusion*/true);
        remaining -= thisStep;
    }
}

void EconomyGPU::readbackMetrics(int year) {
    if (!m_initialized) {
        return;
    }
    if (m_hasAnyReadback && year <= m_lastReadbackYear) {
        return;
    }
    computeCountryMetricsCPU(year);
    m_lastReadbackYear = year;
    m_hasAnyReadback = true;
}

void EconomyGPU::applyCountryMetrics(std::vector<Country>& countries,
                                     const std::vector<double>* tradeExportsValue) const {
    const size_t paletteSize = m_countryWealth.size();
    if (paletteSize == 0) {
        return;
    }
    for (size_t i = 0; i < countries.size(); ++i) {
        const size_t id = i + 1;
        if (id >= paletteSize) {
            break;
        }
        countries[i].setWealth(m_countryWealth[id]);
        countries[i].setGDP(m_countryGDP[id]);
        double ex = m_countryExports[id];
        if (tradeExportsValue && i < tradeExportsValue->size()) {
            ex += (*tradeExportsValue)[i];
        }
        countries[i].setExports(ex);
    }
}

const sf::Texture& EconomyGPU::getDebugWealthHeatmapTexture() const {
    return m_debugWealthHeatmap.getTexture();
}

sf::RenderTexture& EconomyGPU::stateSrc() {
    return m_stateSrcIsA ? m_stateA : m_stateB;
}

sf::RenderTexture& EconomyGPU::stateDst() {
    return m_stateSrcIsA ? m_stateB : m_stateA;
}

const sf::Texture& EconomyGPU::stateSrcTexture() const {
    return m_stateSrcIsA ? m_stateA.getTexture() : m_stateB.getTexture();
}

void EconomyGPU::flipState() {
    m_stateSrcIsA = !m_stateSrcIsA;
}

void EconomyGPU::rebuildCountryId(const Map& map) {
    const auto& grid = map.getCountryGrid();
    if (grid.empty() || grid[0].empty()) {
        return;
    }

    std::fill(m_countryIdPixels.begin(), m_countryIdPixels.end(), 0);

    std::vector<int> counts(static_cast<size_t>(m_maxCountries + 1), 0);
    std::vector<int> touched;
    touched.reserve(static_cast<size_t>(m_cfg.econCellSize) * static_cast<size_t>(m_cfg.econCellSize));

    for (int ey = 0; ey < m_econH; ++ey) {
        for (int ex = 0; ex < m_econW; ++ex) {
            touched.clear();

            const int x0 = ex * m_cfg.econCellSize;
            const int y0 = ey * m_cfg.econCellSize;

            for (int dy = 0; dy < m_cfg.econCellSize; ++dy) {
                const int y = y0 + dy;
                if (y < 0 || y >= static_cast<int>(grid.size())) {
                    continue;
                }
                for (int dx = 0; dx < m_cfg.econCellSize; ++dx) {
                    const int x = x0 + dx;
                    if (x < 0 || x >= static_cast<int>(grid[0].size())) {
                        continue;
                    }

                    int c = grid[static_cast<size_t>(y)][static_cast<size_t>(x)];
                    int id = (c >= 0) ? (c + 1) : 0;
                    if (id < 0 || id > m_maxCountries) {
                        id = 0;
                    }
                    if (counts[static_cast<size_t>(id)] == 0) {
                        touched.push_back(id);
                    }
                    counts[static_cast<size_t>(id)]++;
                }
            }

            // Prefer any owned land over "empty" so sparse early territories still register.
            // Only fall back to 0 when the block has no owned pixels at all.
            int bestId = 0;
            int bestCount = -1;
            for (int id : touched) {
                int v = counts[static_cast<size_t>(id)];
                if (id != 0 && v > bestCount) {
                    bestCount = v;
                    bestId = id;
                }
                counts[static_cast<size_t>(id)] = 0;
            }
            if (bestCount < 0) {
                bestId = 0;
            }

            const sf::Uint8 low = static_cast<sf::Uint8>(bestId & 255);
            const sf::Uint8 high = static_cast<sf::Uint8>((bestId >> 8) & 255);

            const size_t idx = (static_cast<size_t>(ey) * static_cast<size_t>(m_econW) + static_cast<size_t>(ex)) * 4u;
            m_countryIdPixels[idx + 0] = low;
            m_countryIdPixels[idx + 1] = high;
            m_countryIdPixels[idx + 2] = 0;
            m_countryIdPixels[idx + 3] = 255;
        }
    }

    m_countryIdTex.update(m_countryIdPixels.data());

    // Refresh infra (access/capacity) whenever territory changes so newly claimed/emptied cells
    // get consistent values without requiring a resource rebuild.
    for (int ey = 0; ey < m_econH; ++ey) {
        for (int ex = 0; ex < m_econW; ++ex) {
            const size_t idx = (static_cast<size_t>(ey) * static_cast<size_t>(m_econW) + static_cast<size_t>(ex)) * 4u;
            const int id = static_cast<int>(m_countryIdPixels[idx + 0]) + (static_cast<int>(m_countryIdPixels[idx + 1]) << 8);

            const float foodPot = (m_resourcePixels.size() == m_countryIdPixels.size())
                                      ? (static_cast<float>(m_resourcePixels[idx + 0]) / 255.0f)
                                      : 0.0f;
            const float matPot = (m_resourcePixels.size() == m_countryIdPixels.size())
                                     ? (static_cast<float>(m_resourcePixels[idx + 1]) / 255.0f)
                                     : 0.0f;

            const float access = (id > 0) ? (0.65f + 0.35f * foodPot) : 0.0f;
            const float capacity = (id > 0) ? (0.75f + 0.25f * matPot) : 0.0f;
            m_infraPixels[idx + 0] = static_cast<sf::Uint8>(std::round(clamp01(access) * 255.0f));
            m_infraPixels[idx + 1] = static_cast<sf::Uint8>(std::round(clamp01(capacity) * 255.0f));
            m_infraPixels[idx + 2] = 0;
            m_infraPixels[idx + 3] = 255;

            if (m_accessCPU.size() == static_cast<size_t>(m_econW) * static_cast<size_t>(m_econH)) {
                m_accessCPU[static_cast<size_t>(ey) * static_cast<size_t>(m_econW) + static_cast<size_t>(ex)] = access;
            }
        }
    }

    sf::Texture infraTex;
    infraTex.create(static_cast<unsigned>(m_econW), static_cast<unsigned>(m_econH));
    infraTex.setSmooth(false);
    infraTex.update(m_infraPixels.data());

    sf::Sprite sprite(infraTex);
    m_priceA.clear(sf::Color::Black);
    m_priceA.draw(sprite);
    m_priceA.display();
    m_priceB.clear(sf::Color::Black);
    m_priceB.draw(sprite);
    m_priceB.display();
}

void EconomyGPU::rebuildResourcePotential(const Map& map) {
    const auto& resGrid = map.getResourceGrid();
    const auto& countryGrid = map.getCountryGrid();
    if (resGrid.empty() || resGrid[0].empty() || countryGrid.empty() || countryGrid[0].empty()) {
        return;
    }

    m_accessCPU.assign(static_cast<size_t>(m_econW) * static_cast<size_t>(m_econH), 1.0f);

    for (int ey = 0; ey < m_econH; ++ey) {
        for (int ex = 0; ex < m_econW; ++ex) {
            const int x0 = ex * m_cfg.econCellSize;
            const int y0 = ey * m_cfg.econCellSize;

            double sumFood = 0.0;
            double sumMat = 0.0;
            int samples = 0;

            for (int dy = 0; dy < m_cfg.econCellSize; ++dy) {
                const int y = y0 + dy;
                if (y < 0 || y >= static_cast<int>(resGrid.size())) {
                    continue;
                }
                for (int dx = 0; dx < m_cfg.econCellSize; ++dx) {
                    const int x = x0 + dx;
                    if (x < 0 || x >= static_cast<int>(resGrid[0].size())) {
                        continue;
                    }
                    if (countryGrid[static_cast<size_t>(y)][static_cast<size_t>(x)] < 0) {
                        continue; // treat ocean/empty as no economy resources
                    }

                    const auto& cell = resGrid[static_cast<size_t>(y)][static_cast<size_t>(x)];
                    auto itFood = cell.find(Resource::Type::FOOD);
                    if (itFood != cell.end()) {
                        sumFood += itFood->second;
                    }
                    auto itIron = cell.find(Resource::Type::IRON);
                    if (itIron != cell.end()) {
                        sumMat += itIron->second;
                    }
                    auto itCoal = cell.find(Resource::Type::COAL);
                    if (itCoal != cell.end()) {
                        sumMat += itCoal->second;
                    }
                    auto itGold = cell.find(Resource::Type::GOLD);
                    if (itGold != cell.end()) {
                        sumMat += itGold->second;
                    }
                    samples++;
                }
            }

            float foodPot = 0.0f;
            float matPot = 0.0f;
            if (samples > 0) {
                const double avgFood = sumFood / static_cast<double>(samples);
                const double avgMat = sumMat / static_cast<double>(samples);
                foodPot = clamp01(static_cast<float>(avgFood / 102.4)); // inland ~0.5, coastal ~1.0
                matPot = clamp01(static_cast<float>(avgMat / 3.0));     // tuned to typical hotspot magnitudes
                matPot = std::max(matPot, 0.08f + 0.12f * foodPot);     // baseline material throughput for non-hotspot cells
            }

            const float consBase = 0.10f;

            const size_t idx = (static_cast<size_t>(ey) * static_cast<size_t>(m_econW) + static_cast<size_t>(ex)) * 4u;
            m_resourcePixels[idx + 0] = static_cast<sf::Uint8>(std::round(foodPot * 255.0f));
            m_resourcePixels[idx + 1] = static_cast<sf::Uint8>(std::round(matPot * 255.0f));
            m_resourcePixels[idx + 2] = static_cast<sf::Uint8>(std::round(consBase * 255.0f));
            m_resourcePixels[idx + 3] = 255;

            const int id = static_cast<int>(m_countryIdPixels[idx + 0]) + (static_cast<int>(m_countryIdPixels[idx + 1]) << 8);

            // Access proxy: coastal-heavy cells trade more easily; capacity proxy: resource potential adds slack.
            const float access = (id > 0) ? (0.65f + 0.35f * foodPot) : 0.0f;
            const float capacity = (id > 0) ? (0.75f + 0.25f * matPot) : 0.0f;

            m_accessCPU[static_cast<size_t>(ey) * static_cast<size_t>(m_econW) + static_cast<size_t>(ex)] = access;

            m_infraPixels[idx + 0] = static_cast<sf::Uint8>(std::round(clamp01(access) * 255.0f));
            m_infraPixels[idx + 1] = static_cast<sf::Uint8>(std::round(clamp01(capacity) * 255.0f));
            m_infraPixels[idx + 2] = 0;
            m_infraPixels[idx + 3] = 255;
        }
    }

    m_resourcePotential.update(m_resourcePixels.data());

    sf::Texture infraTex;
    infraTex.create(static_cast<unsigned>(m_econW), static_cast<unsigned>(m_econH));
    infraTex.setSmooth(false);
    infraTex.update(m_infraPixels.data());

    sf::Sprite sprite(infraTex);
    m_priceA.clear(sf::Color::Black);
    m_priceA.draw(sprite);
    m_priceA.display();
    m_priceB.clear(sf::Color::Black);
    m_priceB.draw(sprite);
    m_priceB.display();
}

void EconomyGPU::rebuildCountryStats(const std::vector<Country>& countries,
                                    const TechnologyManager& tech) {
    const size_t paletteSize = static_cast<size_t>(m_maxCountries + 1);
    m_countryStatsPixels.assign(paletteSize * 4u, 0);
    m_countryInvestRate.assign(paletteSize, 0.0f);

    for (size_t i = 0; i < countries.size() && (i + 1) < paletteSize; ++i) {
        const Country& c = countries[i];
        const size_t id = i + 1;

        const long long pop = std::max(0LL, c.getPopulation());
        const double popLog = (pop > 0) ? std::log2(static_cast<double>(pop) + 1.0) : 0.0;
        const float popF = clamp01(static_cast<float>(popLog / 30.0));

        const double k = TechnologyManager::techKMultiplier(tech, c);
        const float prodF = clamp01(static_cast<float>((k - 0.8) / 1.5));

        const float stability = clamp01(static_cast<float>(c.getStability()));
        const float investRate = clamp01(0.10f + 0.25f * stability + 0.15f * prodF);
        m_countryInvestRate[id] = investRate;

        m_countryStatsPixels[id * 4u + 0u] = static_cast<sf::Uint8>(std::round(popF * 255.0f));
        m_countryStatsPixels[id * 4u + 1u] = static_cast<sf::Uint8>(std::round(prodF * 255.0f));
        m_countryStatsPixels[id * 4u + 2u] = static_cast<sf::Uint8>(std::round(stability * 255.0f));
        m_countryStatsPixels[id * 4u + 3u] = static_cast<sf::Uint8>(std::round(investRate * 255.0f));
    }

    m_countryStatsTex.update(m_countryStatsPixels.data());
}

void EconomyGPU::computeCountryMetricsCPU(int year) {
    if (!m_initialized) {
        return;
    }

    const sf::Image img = stateSrcTexture().copyToImage();
    const sf::Uint8* pixels = img.getPixelsPtr();
    if (!pixels) {
        return;
    }

    const size_t cellCount = static_cast<size_t>(m_econW) * static_cast<size_t>(m_econH);
    const size_t bytes = cellCount * 4u;
    if (m_prevStatePixels.size() != bytes) {
        m_prevStatePixels.assign(bytes, 0);
    }

    std::fill(m_countryWealth.begin(), m_countryWealth.end(), 0.0);
    std::fill(m_countryGDP.begin(), m_countryGDP.end(), 0.0);
    std::fill(m_countryExports.begin(), m_countryExports.end(), 0.0);

    int yearsElapsed = 1;
    if (m_hasPrevReadback) {
        yearsElapsed = std::max(1, year - m_prevReadbackYear);
    }

    const float wFood = 1.0f;
    const float wMat = 1.5f;
    const float wCons = 2.2f;
    const float wCap = 3.0f;
    const float kCapFromVA = 0.05f;
    const float kExport = 0.06f;
    const double cellAreaScale = static_cast<double>(m_cfg.econCellSize) * static_cast<double>(m_cfg.econCellSize);
    const double edgeScale = static_cast<double>(m_cfg.econCellSize);

    // Wealth + GDP
    for (size_t i = 0; i < cellCount; ++i) {
        const size_t idx = i * 4u;

        const int id = static_cast<int>(m_countryIdPixels[idx + 0]) + (static_cast<int>(m_countryIdPixels[idx + 1]) << 8);
        if (id <= 0 || id > m_maxCountries) {
            continue;
        }

        const float food = (static_cast<float>(pixels[idx + 0]) / 255.0f) * m_cfg.maxInvFood;
        const float mat = (static_cast<float>(pixels[idx + 1]) / 255.0f) * m_cfg.maxInvMat;
        const float cons = (static_cast<float>(pixels[idx + 2]) / 255.0f) * m_cfg.maxInvCons;
        const float capN = static_cast<float>(pixels[idx + 3]) / 255.0f;
        const float cap = (capN * capN) * m_cfg.maxCapital;

        const double wealthCell = static_cast<double>(food * wFood + mat * wMat + cons * wCons + cap * wCap);
        m_countryWealth[static_cast<size_t>(id)] += wealthCell * cellAreaScale;

        if (m_hasPrevReadback) {
            const float capPrevN = static_cast<float>(m_prevStatePixels[idx + 3]) / 255.0f;
            const float capPrev = (capPrevN * capPrevN) * m_cfg.maxCapital;
            const float dCap = std::max(0.0f, cap - capPrev);

            const float inv = std::max(0.02f, m_countryInvestRate[static_cast<size_t>(id)]);
            const float va = dCap / (kCapFromVA * inv);
            m_countryGDP[static_cast<size_t>(id)] +=
                (static_cast<double>(va) / static_cast<double>(yearsElapsed)) * cellAreaScale;
        }
    }

    // Exports (approximate cross-border flows from inventory gradients)
    auto sampleInvValue = [&](size_t iCell, float& food, float& mat, float& cons) {
        const size_t idx = iCell * 4u;
        food = (static_cast<float>(pixels[idx + 0]) / 255.0f) * m_cfg.maxInvFood;
        mat = (static_cast<float>(pixels[idx + 1]) / 255.0f) * m_cfg.maxInvMat;
        cons = (static_cast<float>(pixels[idx + 2]) / 255.0f) * m_cfg.maxInvCons;
    };

    for (int y = 0; y < m_econH; ++y) {
        for (int x = 0; x < m_econW; ++x) {
            const size_t iCell = static_cast<size_t>(y) * static_cast<size_t>(m_econW) + static_cast<size_t>(x);
            const size_t idx = iCell * 4u;
            const int id = static_cast<int>(m_countryIdPixels[idx + 0]) + (static_cast<int>(m_countryIdPixels[idx + 1]) << 8);
            if (id <= 0 || id > m_maxCountries) {
                continue;
            }

            const float access = (iCell < m_accessCPU.size()) ? m_accessCPU[iCell] : 1.0f;
            float fC = 0.f, mC = 0.f, cC = 0.f;
            sampleInvValue(iCell, fC, mC, cC);

            auto considerEdge = [&](int nx, int ny) {
                if (nx < 0 || nx >= m_econW || ny < 0 || ny >= m_econH) {
                    return;
                }
                const size_t nCell = static_cast<size_t>(ny) * static_cast<size_t>(m_econW) + static_cast<size_t>(nx);
                const size_t nIdx = nCell * 4u;
                const int idN = static_cast<int>(m_countryIdPixels[nIdx + 0]) + (static_cast<int>(m_countryIdPixels[nIdx + 1]) << 8);
                if (idN <= 0 || idN > m_maxCountries || idN == id) {
                    return;
                }

                float fN = 0.f, mN = 0.f, cN = 0.f;
                sampleInvValue(nCell, fN, mN, cN);

                const float a = std::min(access, (nCell < m_accessCPU.size()) ? m_accessCPU[nCell] : 1.0f);

                const float df = std::max(0.0f, fC - fN);
                const float dm = std::max(0.0f, mC - mN);
                const float dc = std::max(0.0f, cC - cN);

                float exportSignal = (df * wFood + dm * wMat + dc * wCons);
                if (exportSignal < 0.01f && m_resourcePixels.size() == m_countryIdPixels.size()) {
                    const float fPC = static_cast<float>(m_resourcePixels[idx + 0]) / 255.0f;
                    const float mPC = static_cast<float>(m_resourcePixels[idx + 1]) / 255.0f;
                    const float cPC = static_cast<float>(m_resourcePixels[idx + 2]) / 255.0f;

                    const float fPN = static_cast<float>(m_resourcePixels[nIdx + 0]) / 255.0f;
                    const float mPN = static_cast<float>(m_resourcePixels[nIdx + 1]) / 255.0f;
                    const float cPN = static_cast<float>(m_resourcePixels[nIdx + 2]) / 255.0f;

                    const float dpf = std::max(0.0f, fPC - fPN);
                    const float dpm = std::max(0.0f, mPC - mPN);
                    const float dpc = std::max(0.0f, cPC - cPN);

                    exportSignal = dpf * (wFood * 18.0f) + dpm * (wMat * 22.0f) + dpc * (wCons * 12.0f);
                }

                const double expValue = static_cast<double>(exportSignal * (kExport * a)) * edgeScale;
                m_countryExports[static_cast<size_t>(id)] += expValue;
            };

            // Right and down only to avoid double counting.
            considerEdge(x + 1, y);
            considerEdge(x, y + 1);
        }
    }

    // Update readback history.
    std::copy(pixels, pixels + bytes, m_prevStatePixels.begin());
    m_prevReadbackYear = year;
    m_hasPrevReadback = true;
}

void EconomyGPU::computeCountryMetricsFallback(int year,
                                       const std::vector<Country>& countries,
                                       const TechnologyManager& tech,
                                       float dtYears) {
    if (m_maxCountries <= 0) {
        return;
    }

    std::fill(m_countryWealth.begin(), m_countryWealth.end(), 0.0);
    std::fill(m_countryGDP.begin(), m_countryGDP.end(), 0.0);
    std::fill(m_countryExports.begin(), m_countryExports.end(), 0.0);

    const double yearsElapsed = [&] {
        if (m_hasFallbackPrev) {
            const int dy = year - m_fallbackPrevYear;
            if (dy > 0) {
                return static_cast<double>(dy);
            }
        }
        return static_cast<double>(std::max(1.0f, dtYears));
    }();

    for (size_t i = 0; i < countries.size(); ++i) {
        const size_t id = i + 1;
        if (id >= m_countryWealth.size()) {
            break;
        }

        const Country& c = countries[i];
        const double pop = static_cast<double>(std::max<long long>(0, c.getPopulation()));
        const double gold = std::max(0.0, c.getGold());

        // Coarse "tier" proxy from tech count to avoid runaway scaling.
        const auto& unlocked = tech.getUnlockedTechnologies(c);
        const double techCount = static_cast<double>(unlocked.size());
        const double techTier = std::max(0.0, std::min(6.0, techCount / 18.0));

        const double wealthProxy = pop * (1.0 + techTier) + gold;
        m_countryWealth[id] = wealthProxy;

        if (m_hasFallbackPrev && id < m_fallbackPrevWealth.size()) {
            const double dWealth = std::max(0.0, wealthProxy - m_fallbackPrevWealth[id]);
            m_countryGDP[id] = dWealth / yearsElapsed;
        } else {
            // First sample: approximate "flow" GDP as a small share of wealth stock.
            m_countryGDP[id] = wealthProxy * 0.02;
        }
    }

    if (m_fallbackPrevWealth.size() == m_countryWealth.size()) {
        m_fallbackPrevWealth = m_countryWealth;
        m_fallbackPrevYear = year;
        m_hasFallbackPrev = true;
    }
}

// ============================================================
// Phase 4: CPU macro economy + directed trade (authoritative)
// ============================================================

EconomyModelCPU::EconomyModelCPU(SimulationContext& ctx)
    : m_ctx(&ctx) {}

std::uint64_t EconomyModelCPU::pairKey(int a, int b) {
    const std::uint32_t lo = static_cast<std::uint32_t>(std::min(a, b));
    const std::uint32_t hi = static_cast<std::uint32_t>(std::max(a, b));
    return (static_cast<std::uint64_t>(hi) << 32) | static_cast<std::uint64_t>(lo);
}

void EconomyModelCPU::tickYear(int year,
                               int dtYears,
                               const Map& map,
                               std::vector<Country>& countries,
                               const TechnologyManager& tech,
                               TradeManager& tradeManager,
                               News& news) {
    const int years = std::max(1, dtYears);
    const double yearsD = static_cast<double>(years);
    const int n = static_cast<int>(countries.size());
    if (n <= 0) return;

    map.prepareCountryClimateCaches(n);

    m_lastTradeYear = year;
    m_lastTradeN = n;
    m_tradeIntensity.assign(static_cast<size_t>(n) * static_cast<size_t>(n), 0.0f);

    auto clamp01 = [](double v) { return std::max(0.0, std::min(1.0, v)); };
    auto smoothScarcityPrice = [](double basePrice, double demand, double supply, double alpha, double minMul, double maxMul) {
        const double ratio = (supply > 1e-9) ? (demand / supply) : 6.0;
        const double x = std::log(std::max(1e-6, ratio));
        const double mul = std::clamp(std::exp(alpha * std::clamp(x, -2.5, 2.5)), minMul, maxMul);
        return basePrice * mul;
    };

    auto isEnemy = [&](int a, int b) -> bool {
        if (a < 0 || b < 0 || a >= n || b >= n || a == b) return false;
        const auto& enemies = countries[static_cast<size_t>(a)].getEnemies();
        for (const Country* e : enemies) {
            if (e && e->getCountryIndex() == b) return true;
        }
        return false;
    };

    tradeManager.establishShippingRoutes(countries, year, tech, map, news);

    // Sea control influence for shipping chokepoints.
    const int gridH = static_cast<int>(map.getCountryGrid().size());
    const int gridW = (gridH > 0) ? static_cast<int>(map.getCountryGrid()[0].size()) : 0;
    const int navStep = 6;
    const int navW = (gridW + navStep - 1) / navStep;
    const int navH = (gridH + navStep - 1) / navStep;
    std::vector<int> seaOwner;
    std::vector<std::uint16_t> seaDist;
    if (navW > 0 && navH > 0) {
        seaOwner.assign(static_cast<size_t>(navW) * static_cast<size_t>(navH), -1);
        seaDist.assign(static_cast<size_t>(navW) * static_cast<size_t>(navH), std::numeric_limits<std::uint16_t>::max());
        const int R = std::max(1, m_cfg.seaInfluenceRadiusNav);
        for (int i = 0; i < n; ++i) {
            const Country& c = countries[static_cast<size_t>(i)];
            if (c.getPopulation() <= 0) continue;
            for (const auto& p : c.getPorts()) {
                const int px = std::max(0, std::min(navW - 1, p.x / navStep));
                const int py = std::max(0, std::min(navH - 1, p.y / navStep));
                for (int dy = -R; dy <= R; ++dy) {
                    for (int dx = -R; dx <= R; ++dx) {
                        const int nx = px + dx;
                        const int ny = py + dy;
                        if (nx < 0 || ny < 0 || nx >= navW || ny >= navH) continue;
                        const int d = std::abs(dx) + std::abs(dy);
                        if (d > R) continue;
                        const size_t idx = static_cast<size_t>(ny) * static_cast<size_t>(navW) + static_cast<size_t>(nx);
                        const std::uint16_t du = static_cast<std::uint16_t>(std::min(65535, d));
                        if (du < seaDist[idx] || (du == seaDist[idx] && (seaOwner[idx] < 0 || i < seaOwner[idx]))) {
                            seaDist[idx] = du;
                            seaOwner[idx] = i;
                        }
                    }
                }
            }
        }
    }

    // Country vectors.
    std::vector<double> foodAvailPreTrade(static_cast<size_t>(n), 0.0);
    std::vector<double> goodsAvailPreTrade(static_cast<size_t>(n), 0.0);
    std::vector<double> servicesAvail(static_cast<size_t>(n), 0.0);
    std::vector<double> militaryAvail(static_cast<size_t>(n), 0.0);

    std::vector<double> foodNeed(static_cast<size_t>(n), 0.0);
    std::vector<double> goodsNeed(static_cast<size_t>(n), 0.0);
    std::vector<double> servicesNeed(static_cast<size_t>(n), 0.0);
    std::vector<double> militaryNeed(static_cast<size_t>(n), 0.0);

    std::vector<double> foodSupply(static_cast<size_t>(n), 0.0);
    std::vector<double> goodsSupply(static_cast<size_t>(n), 0.0);
    std::vector<double> foodDemand(static_cast<size_t>(n), 0.0);
    std::vector<double> goodsDemand(static_cast<size_t>(n), 0.0);

    std::vector<double> foodPrice(static_cast<size_t>(n), 1.0);
    std::vector<double> goodsPrice(static_cast<size_t>(n), 2.0);
    std::vector<double> servicesPrice(static_cast<size_t>(n), 1.6);
    std::vector<double> militaryPrice(static_cast<size_t>(n), 2.4);

    std::vector<double> laborFood(static_cast<size_t>(n), 0.0);
    std::vector<double> laborGoods(static_cast<size_t>(n), 0.0);
    std::vector<double> laborServices(static_cast<size_t>(n), 0.0);
    std::vector<double> laborAdmin(static_cast<size_t>(n), 0.0);
    std::vector<double> laborMilitary(static_cast<size_t>(n), 0.0);

    std::vector<double> foodOutAnnual(static_cast<size_t>(n), 0.0);
    std::vector<double> goodsOutAnnual(static_cast<size_t>(n), 0.0);
    std::vector<double> servicesOutAnnual(static_cast<size_t>(n), 0.0);
    std::vector<double> militaryOutAnnual(static_cast<size_t>(n), 0.0);
    std::vector<double> taxableBaseAnnual(static_cast<size_t>(n), 0.0);
    std::vector<double> netRevenueAnnual(static_cast<size_t>(n), 0.0);
    std::vector<double> oldRealWage(static_cast<size_t>(n), 0.0);

    for (int i = 0; i < n; ++i) {
        Country& c = countries[static_cast<size_t>(i)];
        Country::MacroEconomyState& m = c.getMacroEconomyMutable();
        oldRealWage[static_cast<size_t>(i)] = m.realWage;

        const double pop = static_cast<double>(std::max<long long>(0, c.getPopulation()));
        const double foodPot = std::max(0.0, map.getCountryFoodPotential(i));
        const double nonFoodPot = std::max(0.0, map.getCountryNonFoodPotential(i));
        const double climateMult = std::max(0.05, static_cast<double>(map.getCountryClimateFoodMultiplier(i)));

        const double control = clamp01(c.getAvgControl());
        const double legitimacy = clamp01(c.getLegitimacy());
        const double stability = clamp01(c.getStability());
        const double logi = clamp01(c.getLogisticsReach());
        const double admin = clamp01(c.getAdminCapacity());
        const double urban = (pop > 1.0) ? clamp01(c.getTotalCityPopulation() / pop) : 0.0;
        const double diseaseBurden = clamp01(m.diseaseBurden);

        const double portTerm = std::min(1.0, std::sqrt(static_cast<double>(c.getPorts().size()) / 8.0));
        const double marketAccessRaw = clamp01(0.08 + 0.48 * logi + 0.18 * admin + 0.26 * portTerm);
        m.marketAccess = marketAccessRaw * (0.35 + 0.65 * control) * (c.isAtWar() ? 0.80 : 1.0);

        if (!m.initialized) {
            m.initialized = true;
            m.foodStock = foodPot * 0.20;
            m.foodStockCap = std::max(1.0, foodPot * 0.80);
            m.nonFoodStock = nonFoodPot * 0.10;
            m.capitalStock = std::max(10.0, 0.00002 * pop);
            m.infraStock = 5.0 + 30.0 * m.marketAccess;
            m.servicesStock = 0.0;
            m.militarySupplyStock = 0.0;
            m.foodSecurity = 1.0;
            m.compliance = 0.45;
            m.leakageRate = 0.20;
            m.institutionCapacity = clamp01(0.20 + 0.40 * control + 0.25 * legitimacy);
        }

        // Endogenous budget shares from macro stress (not player knobs).
        const double faminePressure = clamp01(m.famineSeverity + std::max(0.0, 0.92 - m.foodSecurity));
        const double diseasePressure = diseaseBurden;
        const double warPressure = c.isAtWar() ? 1.0 : 0.0;
        const double controlPressure = clamp01((0.65 - control) / 0.65);
        const double developmentPressure = clamp01((0.45 - m.humanCapital) * 0.5 + (0.35 - m.knowledgeStock) * 0.5);

        double sMilitary = 0.16 + 0.70 * warPressure + 0.22 * clamp01(c.getInequality());
        double sAdmin = 0.18 + 0.55 * controlPressure;
        double sInfra = 0.18 + 0.35 * controlPressure + 0.12 * faminePressure;
        double sHealth = 0.10 + 0.60 * diseasePressure + 0.20 * faminePressure;
        double sEducation = 0.12 + 0.45 * developmentPressure * (1.0 - warPressure);
        double sRnd = 0.05 + 0.35 * developmentPressure * (0.40 + 0.60 * m.humanCapital) * (1.0 - warPressure);
        c.setBudgetShares(sMilitary, sAdmin, sInfra, sHealth, sEducation, sRnd);

        const double taxStress = 0.35 * controlPressure + 0.25 * faminePressure + 0.20 * diseasePressure + 0.20 * warPressure;
        const double taxTarget = std::clamp(0.08 + 0.20 * (1.0 - clamp01(c.getInequality())) + 0.08 * (1.0 - taxStress), 0.03, 0.42);
        c.setTaxRate(0.80 * c.getTaxRate() + 0.20 * taxTarget);

        const auto& cohorts = c.getPopulationCohorts();
        const double laborTotal = std::max(0.0, c.getWorkingAgeLaborSupply());
        const double minAdminLabor = laborTotal * (0.03 + 0.08 * controlPressure);
        const double minMilitaryLabor = laborTotal * (0.02 + 0.16 * warPressure);

        std::array<double, 5> l = {0.0, 0.0, 0.0, minAdminLabor, minMilitaryLabor}; // food,goods,services,admin,military
        double allocated = minAdminLabor + minMilitaryLabor;
        double laborLeft = std::max(0.0, laborTotal - allocated);
        const int quanta = 80;
        const double q = (laborLeft > 0.0) ? (laborLeft / static_cast<double>(quanta)) : 0.0;

        auto sectorOutput = [&](int sector, double workers) -> double {
            const double w = std::max(0.0, workers);
            const double cap = std::sqrt(std::max(1.0, m.capitalStock));
            const double inst = 0.35 + 0.65 * m.institutionCapacity;
            const double ctl = 0.30 + 0.70 * control;
            const double access = 0.30 + 0.70 * m.marketAccess;
            if (sector == 0) { // food
                const bool irr = TechnologyManager::hasTech(tech, c, 10);
                const bool agr = TechnologyManager::hasTech(tech, c, 20);
                const double techMul = 1.0 + (irr ? 0.16 : 0.0) + (agr ? 0.30 : 0.0);
                return foodPot * climateMult * techMul * ctl * (1.0 - 0.30 * diseaseBurden) * std::pow(w + 1.0, 0.72) * 0.014;
            }
            if (sector == 1) { // goods
                const bool mining = TechnologyManager::hasTech(tech, c, 4);
                const bool metal = TechnologyManager::hasTech(tech, c, TechId::METALLURGY);
                const bool ind = TechnologyManager::hasTech(tech, c, 52);
                const double techMul = 1.0 + (mining ? 0.18 : 0.0) + (metal ? 0.25 : 0.0) + (ind ? 0.45 : 0.0);
                return nonFoodPot * techMul * ctl * access * std::pow(w + 1.0, 0.70) * (0.010 + 0.0006 * cap);
            }
            if (sector == 2) { // services
                const double urbanMul = 0.20 + 0.80 * urban;
                return (0.002 + 0.0004 * cap) * inst * access * urbanMul * std::pow(w + 1.0, 0.78) * pop;
            }
            if (sector == 3) { // admin
                return (0.00025 * pop) * (0.30 + 0.70 * admin) * std::pow(w + 1.0, 0.80);
            }
            // military
            return (0.00018 * pop) * (0.25 + 0.75 * logi) * std::pow(w + 1.0, 0.84);
        };

        const double subsistenceFoodNeedAnnual =
            cohorts[0] * 0.00085 +
            cohorts[1] * 0.00100 +
            cohorts[2] * 0.00120 +
            cohorts[3] * 0.00110 +
            cohorts[4] * 0.00095;

        for (int step = 0; step < quanta && laborLeft > 1e-9; ++step) {
            int bestSector = 0;
            double bestMB = -1e100;
            for (int s = 0; s < 5; ++s) {
                if (s == 3 && l[3] < minAdminLabor) {
                    bestSector = 3;
                    bestMB = 1e99;
                    break;
                }
                if (s == 4 && l[4] < minMilitaryLabor) {
                    bestSector = 4;
                    bestMB = 1e99;
                    break;
                }
                const double before = sectorOutput(s, l[static_cast<size_t>(s)]);
                const double after = sectorOutput(s, l[static_cast<size_t>(s)] + q);
                const double marginal = std::max(0.0, after - before);

                double weight = 1.0;
                if (s == 0) {
                    const double projected = foodOutAnnual[static_cast<size_t>(i)] + before;
                    const double gap = std::max(0.0, (subsistenceFoodNeedAnnual - projected) / std::max(1e-9, subsistenceFoodNeedAnnual));
                    weight = 2.0 + 6.0 * gap;
                } else if (s == 1) {
                    weight = 1.1 + 0.7 * m.marketAccess;
                } else if (s == 2) {
                    weight = 0.8 + 1.0 * urban;
                } else if (s == 3) {
                    weight = 0.9 + 1.2 * controlPressure;
                } else if (s == 4) {
                    weight = 0.9 + 1.8 * warPressure;
                }
                const double score = marginal * weight;
                if (score > bestMB || (score == bestMB && s < bestSector)) {
                    bestMB = score;
                    bestSector = s;
                }
            }
            const double grant = std::min(q, laborLeft);
            l[static_cast<size_t>(bestSector)] += grant;
            laborLeft -= grant;
        }

        laborFood[static_cast<size_t>(i)] = l[0];
        laborGoods[static_cast<size_t>(i)] = l[1];
        laborServices[static_cast<size_t>(i)] = l[2];
        laborAdmin[static_cast<size_t>(i)] = l[3];
        laborMilitary[static_cast<size_t>(i)] = l[4];

        foodOutAnnual[static_cast<size_t>(i)] = sectorOutput(0, l[0]);
        goodsOutAnnual[static_cast<size_t>(i)] = sectorOutput(1, l[1]);
        servicesOutAnnual[static_cast<size_t>(i)] = sectorOutput(2, l[2]);
        militaryOutAnnual[static_cast<size_t>(i)] = sectorOutput(4, l[4]);
        const double adminOutAnnual = sectorOutput(3, l[3]);

        m.lastFoodOutput = foodOutAnnual[static_cast<size_t>(i)];
        m.lastGoodsOutput = goodsOutAnnual[static_cast<size_t>(i)];
        m.lastServicesOutput = servicesOutAnnual[static_cast<size_t>(i)];
        m.lastMilitaryOutput = militaryOutAnnual[static_cast<size_t>(i)];
        m.lastNonFoodOutput = m.lastGoodsOutput + m.lastServicesOutput;

        // Storage/spoilage dynamics.
        m.foodStockCap = std::max(1.0, (0.35 + 1.25 * m.marketAccess + 0.95 * m.institutionCapacity) * std::max(1.0, subsistenceFoodNeedAnnual));
        const double climateSpoilage = clamp01((1.0 - climateMult) * 0.6 + 0.3);
        m.spoilageRate = std::clamp(0.08 + 0.20 * climateSpoilage - 0.06 * m.institutionCapacity - 0.04 * TechnologyManager::hasTech(tech, c, 71), 0.03, 0.35);
        const double spoilageTotal = m.foodStock * (1.0 - std::pow(std::max(0.0, 1.0 - m.spoilageRate), yearsD));

        const double incomeProxy = std::max(0.0, oldRealWage[static_cast<size_t>(i)]);
        const double goodsNeedAnnualHousehold = pop * (0.00022 + 0.00034 * clamp01(incomeProxy / 2.5));
        const double servicesNeedAnnualHousehold = pop * (0.00016 + 0.00028 * clamp01(incomeProxy / 2.5));
        const double militaryNeedAnnual = pop * (0.00004 + 0.00020 * c.getMilitarySpendingShare()) * (c.isAtWar() ? 1.5 : 1.0);
        const double govGoodsNeedAnnual = pop * (0.00005 + 0.00018 * c.getInfraSpendingShare() + 0.00012 * c.getAdminSpendingShare());
        const double govServicesNeedAnnual = pop * (0.00004 + 0.00020 * c.getHealthSpendingShare() + 0.00018 * c.getEducationSpendingShare() + 0.00018 * c.getRndSpendingShare());

        foodNeed[static_cast<size_t>(i)] = subsistenceFoodNeedAnnual * yearsD;
        goodsNeed[static_cast<size_t>(i)] = (goodsNeedAnnualHousehold + govGoodsNeedAnnual) * yearsD;
        servicesNeed[static_cast<size_t>(i)] = (servicesNeedAnnualHousehold + govServicesNeedAnnual) * yearsD;
        militaryNeed[static_cast<size_t>(i)] = militaryNeedAnnual * yearsD;

        foodAvailPreTrade[static_cast<size_t>(i)] = std::max(0.0, m.foodStock + foodOutAnnual[static_cast<size_t>(i)] * yearsD - spoilageTotal);
        goodsAvailPreTrade[static_cast<size_t>(i)] = std::max(0.0, m.nonFoodStock + goodsOutAnnual[static_cast<size_t>(i)] * yearsD);
        servicesAvail[static_cast<size_t>(i)] = std::max(0.0, m.servicesStock + servicesOutAnnual[static_cast<size_t>(i)] * yearsD);
        militaryAvail[static_cast<size_t>(i)] = std::max(0.0, m.militarySupplyStock + militaryOutAnnual[static_cast<size_t>(i)] * yearsD);

        foodSupply[static_cast<size_t>(i)] = std::max(0.0, foodAvailPreTrade[static_cast<size_t>(i)] - foodNeed[static_cast<size_t>(i)]);
        goodsSupply[static_cast<size_t>(i)] = std::max(0.0, goodsAvailPreTrade[static_cast<size_t>(i)] - goodsNeed[static_cast<size_t>(i)]);
        foodDemand[static_cast<size_t>(i)] = std::max(0.0, foodNeed[static_cast<size_t>(i)] - foodAvailPreTrade[static_cast<size_t>(i)]);
        goodsDemand[static_cast<size_t>(i)] = std::max(0.0, goodsNeed[static_cast<size_t>(i)] - goodsAvailPreTrade[static_cast<size_t>(i)]);

        foodPrice[static_cast<size_t>(i)] = smoothScarcityPrice(1.0, foodNeed[static_cast<size_t>(i)], std::max(1e-9, foodAvailPreTrade[static_cast<size_t>(i)]), 0.85, 0.35, 6.0);
        goodsPrice[static_cast<size_t>(i)] = smoothScarcityPrice(2.0, goodsNeed[static_cast<size_t>(i)], std::max(1e-9, goodsAvailPreTrade[static_cast<size_t>(i)]), 0.75, 0.45, 7.0);
        servicesPrice[static_cast<size_t>(i)] = smoothScarcityPrice(1.4, servicesNeed[static_cast<size_t>(i)], std::max(1e-9, servicesAvail[static_cast<size_t>(i)]), 0.65, 0.55, 5.0);
        militaryPrice[static_cast<size_t>(i)] = smoothScarcityPrice(2.4, militaryNeed[static_cast<size_t>(i)], std::max(1e-9, militaryAvail[static_cast<size_t>(i)]), 0.70, 0.60, 6.0);

        m.importsValue = 0.0;
        m.exportsValue = 0.0;
        m.lastFoodShortage = 0.0;
        m.lastNonFoodShortage = 0.0;
        m.lastFoodCons = subsistenceFoodNeedAnnual;
        m.lastNonFoodCons = goodsNeedAnnualHousehold + servicesNeedAnnualHousehold;

        const double valueAddedAnnual =
            foodOutAnnual[static_cast<size_t>(i)] * foodPrice[static_cast<size_t>(i)] +
            goodsOutAnnual[static_cast<size_t>(i)] * goodsPrice[static_cast<size_t>(i)] +
            servicesOutAnnual[static_cast<size_t>(i)] * servicesPrice[static_cast<size_t>(i)] +
            militaryOutAnnual[static_cast<size_t>(i)] * militaryPrice[static_cast<size_t>(i)];

        const double householdIncomeProxy = std::max(0.0, pop * 0.45 * (goodsPrice[static_cast<size_t>(i)] + servicesPrice[static_cast<size_t>(i)]));
        taxableBaseAnnual[static_cast<size_t>(i)] = std::max(0.0, 0.45 * valueAddedAnnual + 0.35 * householdIncomeProxy * m.marketAccess + 0.0002 * adminOutAnnual);

        m.compliance = clamp01(
            0.20 +
            0.28 * legitimacy +
            0.20 * m.institutionCapacity +
            0.14 * control -
            0.26 * clamp01(m.inequality) -
            0.20 * faminePressure -
            0.16 * diseasePressure -
            0.12 * warPressure);
        m.leakageRate = std::clamp(
            0.08 +
            0.28 * clamp01(m.inequality) +
            0.22 * (1.0 - m.institutionCapacity) +
            0.16 * (1.0 - control),
            0.02, 0.80);

        const double collectedTaxAnnual = std::max(0.0, c.getTaxRate()) * taxableBaseAnnual[static_cast<size_t>(i)] * m.compliance * control;
        netRevenueAnnual[static_cast<size_t>(i)] = collectedTaxAnnual * (1.0 - m.leakageRate);
        m.netRevenue = netRevenueAnnual[static_cast<size_t>(i)];
    }

    struct Edge {
        int a = -1;
        int b = -1;
        double capFood = 0.0;
        double capGoods = 0.0;
        double costPerUnit = 0.0;
    };

    std::vector<Edge> edges;
    edges.reserve(static_cast<size_t>(n) * 6u);
    std::unordered_map<std::uint64_t, int> edgeIndex;
    edgeIndex.reserve(static_cast<size_t>(n) * 6u);

    auto addEdge = [&](int a, int b, double capFood, double capGoods, double cost) {
        if (a < 0 || b < 0 || a >= n || b >= n || a == b) return;
        if (isEnemy(a, b) || isEnemy(b, a)) return;
        const std::uint64_t key = pairKey(a, b);
        auto it = edgeIndex.find(key);
        if (it != edgeIndex.end()) {
            Edge& e = edges[static_cast<size_t>(it->second)];
            e.capFood += capFood;
            e.capGoods += capGoods;
            e.costPerUnit = std::min(e.costPerUnit, cost);
            return;
        }
        Edge e;
        e.a = a;
        e.b = b;
        e.capFood = capFood;
        e.capGoods = capGoods;
        e.costPerUnit = cost;
        edgeIndex[key] = static_cast<int>(edges.size());
        edges.push_back(e);
    };

    for (int a = 0; a < n; ++a) {
        const Country& ca = countries[static_cast<size_t>(a)];
        if (ca.getPopulation() <= 0) continue;
        for (int b : map.getAdjacentCountryIndicesPublic(a)) {
            if (b <= a || b < 0 || b >= n) continue;
            const Country& cb = countries[static_cast<size_t>(b)];
            if (cb.getPopulation() <= 0) continue;

            const int contact = std::max(1, map.getBorderContactCount(a, b));
            const double contactScale = std::sqrt(static_cast<double>(contact));
            const double logi = std::min(clamp01(ca.getLogisticsReach()), clamp01(cb.getLogisticsReach()));
            const double control = std::min(clamp01(ca.getAvgControl()), clamp01(cb.getAvgControl()));
            const double connectivity = std::min(clamp01(ca.getConnectivityIndex()), clamp01(cb.getConnectivityIndex()));

            double cap = m_cfg.baseLandCapacity * (0.30 + 0.70 * logi) * (0.30 + 0.70 * control) * (0.45 + 0.55 * connectivity) * std::min(4.0, 0.35 + contactScale * 0.08);
            double cost = m_cfg.baseLandCost * (1.0 / std::max(1.0, contactScale)) * (1.25 - 0.65 * logi) * (1.15 - 0.55 * control);
            if (ca.isAtWar() || cb.isAtWar()) {
                cap *= 0.62;
                cost *= 1.20;
            }
            cap *= yearsD;
            addEdge(a, b, cap, cap * 0.85, cost);
        }
    }

    for (const auto& r : tradeManager.getShippingRoutes()) {
        if (!r.isActive) continue;
        const int a = r.fromCountryIndex;
        const int b = r.toCountryIndex;
        if (a < 0 || b < 0 || a >= n || b >= n || a == b) continue;
        if (countries[static_cast<size_t>(a)].getPopulation() <= 0) continue;
        if (countries[static_cast<size_t>(b)].getPopulation() <= 0) continue;
        if (isEnemy(a, b) || isEnemy(b, a)) continue;

        const Country& ca = countries[static_cast<size_t>(a)];
        const Country& cb = countries[static_cast<size_t>(b)];
        const double logi = std::min(clamp01(ca.getLogisticsReach()), clamp01(cb.getLogisticsReach()));
        const double connectivity = std::min(clamp01(ca.getConnectivityIndex()), clamp01(cb.getConnectivityIndex()));

        double cap = m_cfg.baseSeaCapacity * (0.35 + 0.65 * logi) * (0.50 + 0.50 * connectivity);
        cap *= (0.75 + 0.25 * std::min(1.0, static_cast<double>(ca.getPorts().size() + cb.getPorts().size()) / 6.0));

        double blockadeMult = 1.0;
        if (!seaOwner.empty() && !r.navPath.empty()) {
            int hostileHits = 0;
            for (const auto& node : r.navPath) {
                if (node.x < 0 || node.y < 0 || node.x >= navW || node.y >= navH) continue;
                const size_t idx = static_cast<size_t>(node.y) * static_cast<size_t>(navW) + static_cast<size_t>(node.x);
                const int owner = seaOwner[idx];
                if (owner < 0 || owner == a || owner == b) continue;
                if (isEnemy(a, owner) || isEnemy(b, owner) || isEnemy(owner, a) || isEnemy(owner, b)) hostileHits++;
            }
            if (hostileHits > 0) {
                blockadeMult = 0.10;
                if (hostileHits > static_cast<int>(r.navPath.size() / 20u)) blockadeMult = 0.02;
            }
        }
        cap *= blockadeMult;
        double cost = m_cfg.seaCostPerLen * static_cast<double>(std::max(0.0f, r.totalLen)) * (1.20 - 0.45 * logi);
        if (ca.isAtWar() || cb.isAtWar()) {
            cap *= 0.58;
            cost *= 1.30;
        }
        cap *= yearsD;
        addEdge(a, b, cap, cap * 0.90, cost);
    }

    std::sort(edges.begin(), edges.end(), [](const Edge& l, const Edge& r) {
        if (l.a != r.a) return l.a < r.a;
        if (l.b != r.b) return l.b < r.b;
        return l.costPerUnit < r.costPerUnit;
    });

    std::vector<std::vector<int>> edgesByCountry(static_cast<size_t>(n));
    for (int ei = 0; ei < static_cast<int>(edges.size()); ++ei) {
        const Edge& e = edges[static_cast<size_t>(ei)];
        edgesByCountry[static_cast<size_t>(e.a)].push_back(ei);
        edgesByCountry[static_cast<size_t>(e.b)].push_back(ei);
    }

    auto tradeGood = [&](std::vector<double>& supply,
                         std::vector<double>& demand,
                         const std::vector<double>& localPrice,
                         bool foodCategory) {
        struct Cand {
            int supplier = -1;
            int edgeIdx = -1;
            double deliveredPrice = 0.0;
        };

        std::vector<double> net(static_cast<size_t>(n), 0.0);
        std::vector<int> buyers;
        buyers.reserve(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            if (countries[static_cast<size_t>(i)].getPopulation() <= 0) continue;
            if (demand[static_cast<size_t>(i)] > 1e-9) buyers.push_back(i);
        }
        std::sort(buyers.begin(), buyers.end(), [&](int a, int b) {
            const double pa = localPrice[static_cast<size_t>(a)];
            const double pb = localPrice[static_cast<size_t>(b)];
            if (pa != pb) return pa > pb;
            return a < b;
        });

        for (int buyer : buyers) {
            double need = demand[static_cast<size_t>(buyer)];
            if (need <= 1e-9) continue;

            std::vector<Cand> cands;
            cands.reserve(edgesByCountry[static_cast<size_t>(buyer)].size());
            for (int ei : edgesByCountry[static_cast<size_t>(buyer)]) {
                const Edge& e = edges[static_cast<size_t>(ei)];
                const int supplier = (e.a == buyer) ? e.b : e.a;
                if (supplier < 0 || supplier >= n) continue;
                if (supply[static_cast<size_t>(supplier)] <= 1e-9) continue;
                const double capRem = foodCategory ? e.capFood : e.capGoods;
                if (capRem <= 1e-9) continue;
                cands.push_back(Cand{supplier, ei, localPrice[static_cast<size_t>(supplier)] + e.costPerUnit});
            }
            std::sort(cands.begin(), cands.end(), [&](const Cand& l, const Cand& r) {
                if (l.deliveredPrice != r.deliveredPrice) return l.deliveredPrice < r.deliveredPrice;
                if (l.supplier != r.supplier) return l.supplier < r.supplier;
                return l.edgeIdx < r.edgeIdx;
            });
            if (cands.size() > 6) {
                cands.resize(6);
            }

            const double local = localPrice[static_cast<size_t>(buyer)];
            for (const Cand& cand : cands) {
                if (need <= 1e-9) break;
                if (cand.deliveredPrice >= local * 0.998) break;

                Edge& e = edges[static_cast<size_t>(cand.edgeIdx)];
                double& capRem = foodCategory ? e.capFood : e.capGoods;
                const double amount = std::min({need, supply[static_cast<size_t>(cand.supplier)], capRem});
                if (amount <= 1e-12) continue;
                need -= amount;
                supply[static_cast<size_t>(cand.supplier)] -= amount;
                capRem -= amount;
                net[static_cast<size_t>(buyer)] += amount;
                net[static_cast<size_t>(cand.supplier)] -= amount;

                const double value = amount * cand.deliveredPrice;
                Country::MacroEconomyState& mb = countries[static_cast<size_t>(buyer)].getMacroEconomyMutable();
                Country::MacroEconomyState& ms = countries[static_cast<size_t>(cand.supplier)].getMacroEconomyMutable();
                mb.importsValue += value;
                ms.exportsValue += value;
                const size_t ti = static_cast<size_t>(cand.supplier) * static_cast<size_t>(n) + static_cast<size_t>(buyer);
                if (ti < m_tradeIntensity.size()) {
                    const double popBuyer = static_cast<double>(std::max<long long>(1, countries[static_cast<size_t>(buyer)].getPopulation()));
                    const float inc = static_cast<float>(std::min(1.0, value / (8000.0 + popBuyer * 0.35)));
                    m_tradeIntensity[ti] = std::min(1.0f, m_tradeIntensity[ti] + inc);
                }
            }
            demand[static_cast<size_t>(buyer)] = need;
        }

        for (int i = 0; i < n; ++i) {
            if (foodCategory) {
                foodAvailPreTrade[static_cast<size_t>(i)] += net[static_cast<size_t>(i)];
            } else {
                goodsAvailPreTrade[static_cast<size_t>(i)] += net[static_cast<size_t>(i)];
            }
        }
    };

    tradeGood(foodSupply, foodDemand, foodPrice, true);
    tradeGood(goodsSupply, goodsDemand, goodsPrice, false);

    // Finalize country states after trade.
    double maxTradeRow = 1e-9;
    std::vector<double> tradeRow(static_cast<size_t>(n), 0.0);
    for (int a = 0; a < n; ++a) {
        double row = 0.0;
        for (int b = 0; b < n; ++b) {
            row += static_cast<double>(m_tradeIntensity[static_cast<size_t>(a) * static_cast<size_t>(n) + static_cast<size_t>(b)]);
        }
        tradeRow[static_cast<size_t>(a)] = row;
        maxTradeRow = std::max(maxTradeRow, row);
    }

    for (int i = 0; i < n; ++i) {
        Country& c = countries[static_cast<size_t>(i)];
        Country::MacroEconomyState& m = c.getMacroEconomyMutable();

        const double pop = static_cast<double>(std::max<long long>(0, c.getPopulation()));
        const double control = clamp01(c.getAvgControl());
        const double legitimacy = clamp01(c.getLegitimacy());
        const double stability = clamp01(c.getStability());
        const double urban = (pop > 1.0) ? clamp01(c.getTotalCityPopulation() / pop) : 0.0;
        const double diseaseBurden = clamp01(m.diseaseBurden);

        const double fNeed = foodNeed[static_cast<size_t>(i)];
        const double gNeed = goodsNeed[static_cast<size_t>(i)];
        const double sNeed = servicesNeed[static_cast<size_t>(i)];
        const double milNeed = militaryNeed[static_cast<size_t>(i)];

        const double fAvail = std::max(0.0, foodAvailPreTrade[static_cast<size_t>(i)]);
        const double gAvail = std::max(0.0, goodsAvailPreTrade[static_cast<size_t>(i)]);
        const double sAvail = servicesAvail[static_cast<size_t>(i)];
        const double milAvail = militaryAvail[static_cast<size_t>(i)];

        const double fShort = std::max(0.0, fNeed - fAvail);
        const double gShort = std::max(0.0, gNeed - gAvail);
        const double sShort = std::max(0.0, sNeed - sAvail);
        const double milShort = std::max(0.0, milNeed - milAvail);

        m.lastFoodShortage = fShort / yearsD;
        m.lastNonFoodShortage = (gShort + sShort) / yearsD;
        m.foodSecurity = clamp01(1.0 - (fNeed > 1e-9 ? (fShort / fNeed) : 0.0));
        m.famineSeverity = clamp01((fNeed > 1e-9) ? (fShort / fNeed) : 0.0);
        m.nutritionBalance = fAvail - fNeed;

        const double fRemaining = std::max(0.0, fAvail - fNeed);
        m.foodStock = std::min(m.foodStockCap, fRemaining);
        m.nonFoodStock = std::max(0.0, gAvail - gNeed);
        m.servicesStock = std::max(0.0, sAvail - sNeed);
        m.militarySupplyStock = std::max(0.0, milAvail - milNeed);

        const double depTotal = m_cfg.depRate * std::max(0.0, m.capitalStock) * yearsD;
        const double investInput = std::max(0.0, m.nonFoodStock) * (0.15 + 0.45 * c.getRndSpendingShare() + 0.40 * c.getInfraSpendingShare());
        const double investTotal = investInput * (0.25 + 0.75 * m.marketAccess);
        m.lastInvestment = investTotal / yearsD;
        m.lastDepreciation = depTotal / yearsD;
        m.capitalStock = std::max(0.0, m.capitalStock - depTotal + investTotal * m_cfg.investEfficiency);

        // Prices, wages, CPI.
        m.priceFood = smoothScarcityPrice(1.0, fNeed, std::max(1e-9, fAvail), 0.85, 0.35, 6.5);
        m.priceGoods = smoothScarcityPrice(2.0, gNeed, std::max(1e-9, gAvail), 0.75, 0.45, 7.0);
        m.priceServices = smoothScarcityPrice(1.4, sNeed, std::max(1e-9, sAvail), 0.65, 0.55, 5.0);
        m.priceMilitarySupply = smoothScarcityPrice(2.4, milNeed, std::max(1e-9, milAvail), 0.70, 0.60, 6.0);

        const double wFood = (laborFood[static_cast<size_t>(i)] > 1e-9) ? (m.priceFood * foodOutAnnual[static_cast<size_t>(i)] / laborFood[static_cast<size_t>(i)]) : 0.0;
        const double wGoods = (laborGoods[static_cast<size_t>(i)] > 1e-9) ? (m.priceGoods * goodsOutAnnual[static_cast<size_t>(i)] / laborGoods[static_cast<size_t>(i)]) : 0.0;
        const double wServ = (laborServices[static_cast<size_t>(i)] > 1e-9) ? (m.priceServices * servicesOutAnnual[static_cast<size_t>(i)] / laborServices[static_cast<size_t>(i)]) : 0.0;
        const double wAdmin = (laborAdmin[static_cast<size_t>(i)] > 1e-9) ? (1.1 * m.priceServices * std::max(1.0, c.getAdminCapacity()) / std::max(1.0, laborAdmin[static_cast<size_t>(i)])) : 0.0;
        const double wMil = (laborMilitary[static_cast<size_t>(i)] > 1e-9) ? (m.priceMilitarySupply * militaryOutAnnual[static_cast<size_t>(i)] / laborMilitary[static_cast<size_t>(i)]) : 0.0;
        const double laborTot = laborFood[static_cast<size_t>(i)] + laborGoods[static_cast<size_t>(i)] + laborServices[static_cast<size_t>(i)] + laborAdmin[static_cast<size_t>(i)] + laborMilitary[static_cast<size_t>(i)];
        m.wage = (laborTot > 1e-9)
            ? ((wFood * laborFood[static_cast<size_t>(i)] +
                wGoods * laborGoods[static_cast<size_t>(i)] +
                wServ * laborServices[static_cast<size_t>(i)] +
                wAdmin * laborAdmin[static_cast<size_t>(i)] +
                wMil * laborMilitary[static_cast<size_t>(i)]) / laborTot)
            : 0.0;
        const double cpiFoodW = 0.55;
        const double cpiGoodsW = 0.25;
        const double cpiServW = 0.18;
        const double cpiMilW = 0.02;
        m.cpi = cpiFoodW * m.priceFood + cpiGoodsW * m.priceGoods + cpiServW * m.priceServices + cpiMilW * m.priceMilitarySupply;
        m.realWage = (m.cpi > 1e-9) ? (m.wage / m.cpi) : 0.0;

        // Connectivity from trade matrix row sums + market access (smoothed).
        const double tradeNorm = clamp01(tradeRow[static_cast<size_t>(i)] / maxTradeRow);
        const double connTarget = clamp01(0.62 * m.marketAccess + 0.38 * tradeNorm);
        m.connectivityIndex = clamp01(m.connectivityIndex + (connTarget - m.connectivityIndex) * (1.0 - std::exp(-yearsD / 4.0)));

        // Institution capacity derived from control/legitimacy/leakage/admin-investment.
        const double instTarget = clamp01(
            0.34 * control +
            0.20 * legitimacy +
            0.17 * stability +
            0.13 * (1.0 - m.leakageRate) +
            0.10 * c.getAdminSpendingShare() +
            0.06 * m.compliance);
        m.institutionCapacity = clamp01(m.institutionCapacity + (instTarget - m.institutionCapacity) * (1.0 - std::exp(-yearsD / 6.0)));

        // Human capital and knowledge stock (slow, endogenous, path-dependent).
        const double hGrow =
            c.getEducationSpendingShare() *
            (0.20 + 0.80 * stability) *
            (0.25 + 0.75 * m.foodSecurity) *
            (0.20 + 0.80 * (1.0 - diseaseBurden)) *
            (0.20 + 0.80 * m.institutionCapacity);
        const double hDecay =
            0.45 * m.famineSeverity +
            0.24 * diseaseBurden +
            0.18 * (c.isAtWar() ? 1.0 : 0.0) +
            0.16 * clamp01(m.inequality);
        m.humanCapital = clamp01(m.humanCapital + yearsD * (0.022 * hGrow - 0.014 * hDecay));

        const double popScale = (pop > 0.0) ? std::min(1.8, 0.35 + 0.22 * std::log1p(pop / 50000.0)) : 0.0;
        const double kGrow =
            c.getRndSpendingShare() *
            popScale *
            (0.20 + 0.80 * urban) *
            (0.20 + 0.80 * m.connectivityIndex) *
            (0.25 + 0.75 * m.humanCapital);
        const double collapse =
            (m.institutionCapacity < 0.22 && control < 0.35)
            ? (0.010 + 0.030 * clamp01((0.22 - m.institutionCapacity) / 0.22) + 0.020 * (c.isAtWar() ? 1.0 : 0.0))
            : 0.0;
        m.knowledgeStock = clamp01(m.knowledgeStock + yearsD * (0.018 * kGrow - collapse - 0.0012));

        // Inequality dynamics and feedback loops.
        const double tradeRent = clamp01(tradeNorm * (0.50 + 0.50 * m.marketAccess));
        const double extraction = clamp01(c.getTaxRate() * (1.0 - m.compliance));
        const double wageGain = (std::max(0.0, m.realWage - oldRealWage[static_cast<size_t>(i)]) / std::max(0.2, oldRealWage[static_cast<size_t>(i)] + 0.2));
        const double ineqUp = 0.020 * tradeRent + 0.012 * extraction + 0.010 * urban;
        const double ineqDown = 0.016 * m.institutionCapacity + 0.014 * m.humanCapital + 0.010 * clamp01(wageGain);
        m.inequality = clamp01(m.inequality + yearsD * (ineqUp - ineqDown));
        m.migrationPressureOut = clamp01(
            0.35 * m.famineSeverity +
            0.22 * diseaseBurden +
            0.18 * (1.0 - control) +
            0.15 * clamp01(m.inequality) +
            0.10 * (c.isAtWar() ? 1.0 : 0.0));
        m.migrationAttractiveness = clamp01(
            0.32 * clamp01(m.realWage / 2.0) +
            0.25 * m.foodSecurity +
            0.15 * (1.0 - diseaseBurden) +
            0.16 * m.institutionCapacity +
            0.12 * m.connectivityIndex);

        // Political feedback.
        const double legitDrift =
            + 0.010 * m.institutionCapacity
            + 0.008 * clamp01(wageGain)
            - 0.020 * m.famineSeverity
            - 0.015 * clamp01(m.inequality)
            - 0.010 * diseaseBurden;
        c.setLegitimacy(legitimacy + legitDrift * yearsD);
        c.setStability(stability + (0.008 * m.institutionCapacity - 0.018 * m.famineSeverity - 0.012 * diseaseBurden - 0.008 * (c.isAtWar() ? 1.0 : 0.0)) * yearsD);

        // Apply fiscal state update with compliance/leakage-adjusted net revenue.
        const int techCount = static_cast<int>(tech.getUnlockedTechnologies(c).size());
        const bool plagueAffected = map.isPlagueActive() && map.isCountryAffectedByPlague(i);
        c.applyBudgetFromEconomy(taxableBaseAnnual[static_cast<size_t>(i)], netRevenueAnnual[static_cast<size_t>(i)], years, techCount, plagueAffected);

        // Annualize trade values.
        m.importsValue /= yearsD;
        m.exportsValue /= yearsD;

        // Headline stats.
        const double gdpAnnual =
            m.lastFoodOutput * m.priceFood +
            m.lastGoodsOutput * m.priceGoods +
            m.lastServicesOutput * m.priceServices +
            0.8 * m.lastMilitaryOutput * m.priceMilitarySupply +
            m.lastInvestment * 2.0;
        const double wealth =
            m.foodStock * m.priceFood +
            m.nonFoodStock * m.priceGoods +
            m.servicesStock * m.priceServices +
            m.militarySupplyStock * m.priceMilitarySupply +
            m.capitalStock * 4.5 +
            std::max(0.0, c.getGold());
        c.setGDP(std::max(0.0, gdpAnnual));
        c.setWealth(std::max(0.0, wealth));
        c.setExports(std::max(0.0, m.exportsValue));
    }
}
