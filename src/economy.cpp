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
                    auto itCopper = cell.find(Resource::Type::COPPER);
                    if (itCopper != cell.end()) {
                        sumMat += itCopper->second;
                    }
                    auto itTin = cell.find(Resource::Type::TIN);
                    if (itTin != cell.end()) {
                        sumMat += itTin->second;
                    }
                    auto itClay = cell.find(Resource::Type::CLAY);
                    if (itClay != cell.end()) {
                        sumMat += itClay->second;
                    }
                    samples++;
                }
            }

            float foodPot = 0.0f;
            float matPot = 0.0f;
            if (samples > 0) {
                const double avgFood = sumFood / static_cast<double>(samples);
                const double avgMat = sumMat / static_cast<double>(samples);
                foodPot = clamp01(static_cast<float>(avgFood / 153.6)); // riverland/floodplains normalize near 1.0
                matPot = clamp01(static_cast<float>(avgMat / 5.0));     // includes copper/tin/clay material spectrum
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

void EconomyModelCPU::setExternalTradeIntensityHint(const std::vector<float>& hint, int n, float blend) {
    m_externalTradeHintN = std::max(0, n);
    m_externalTradeHintBlend = std::clamp(blend, 0.0f, 1.0f);
    const size_t need = static_cast<size_t>(std::max(0, n)) * static_cast<size_t>(std::max(0, n));
    if (need == 0 || hint.size() < need || m_externalTradeHintBlend <= 0.0f) {
        m_externalTradeHint.clear();
        m_externalTradeHintN = 0;
        m_externalTradeHintBlend = 0.0f;
        return;
    }
    const auto count = static_cast<std::vector<float>::difference_type>(need);
    m_externalTradeHint.assign(hint.begin(), hint.begin() + count);
    for (float& v : m_externalTradeHint) {
        if (!std::isfinite(v) || v < 0.0f) {
            v = 0.0f;
        } else if (v > 1.0f) {
            v = 1.0f;
        }
    }
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
    const SimulationConfig& cfg = m_ctx->config;
    const double twoPi = 6.28318530717958647692;
    const double seedPhase = static_cast<double>(m_ctx->worldSeed & 0xFFFFull) / 65535.0;
    const double globalCycleA = std::sin(twoPi * (static_cast<double>(year) + 37.0 * seedPhase) / 17.0);
    const double globalCycleB = std::sin(twoPi * (static_cast<double>(year) + 83.0 * seedPhase) / 61.0);
    const double globalFoodShock = std::clamp(0.65 * globalCycleA + 0.35 * globalCycleB, -1.0, 1.0);

    map.prepareCountryClimateCaches(n);

    m_lastTradeYear = year;
    m_lastTradeN = n;
    const size_t tradeNeed = static_cast<size_t>(n) * static_cast<size_t>(n);
    if (m_tradeIntensity.size() != tradeNeed) {
        m_tradeIntensity.assign(tradeNeed, 0.0f);
    } else {
        const float memory = static_cast<float>(std::clamp(cfg.economy.tradeIntensityMemory, 0.0, 0.98));
        for (float& v : m_tradeIntensity) {
            v *= memory;
        }
    }
    if (m_externalTradeHintN == n &&
        m_externalTradeHint.size() >= tradeNeed &&
        m_externalTradeHintBlend > 0.0f) {
        const float b = std::clamp(m_externalTradeHintBlend, 0.0f, 1.0f);
        for (size_t i = 0; i < tradeNeed; ++i) {
            m_tradeIntensity[i] = std::clamp(m_tradeIntensity[i] + b * m_externalTradeHint[i], 0.0f, 1.0f);
        }
    }

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
    std::vector<double> resourceTightness(static_cast<size_t>(n), 0.0);

    for (int i = 0; i < n; ++i) {
        Country& c = countries[static_cast<size_t>(i)];
        Country::MacroEconomyState& m = c.getMacroEconomyMutable();
        auto& ldbg = m.legitimacyDebug;
        oldRealWage[static_cast<size_t>(i)] = m.realWage;

        const double pop = static_cast<double>(std::max<long long>(0, c.getPopulation()));
        const double foragingPot = std::max(0.0, map.getCountryForagingPotential(i));
        const double farmingPot = std::max(0.0, map.getCountryFarmingPotential(i));
        const double foodPot = std::max(0.0, map.getCountryFoodPotential(i));
        const double orePotRaw = std::max(0.0, map.getCountryOrePotential(i));
        const double energyPotRaw = std::max(0.0, map.getCountryEnergyPotential(i));
        const double constructionPot = std::max(0.0, map.getCountryConstructionPotential(i));
        const double oreBase = std::max(1.0, orePotRaw * 600.0);
        const double coalBase = std::max(1.0, energyPotRaw * 600.0);
        const double oreDecline = 1.0 / (1.0 + std::max(0.0, cfg.resources.oreDepletionRate) *
                                                 (m.cumulativeOreExtraction / oreBase));
        const double coalDecline = 1.0 / (1.0 + std::max(0.0, cfg.resources.coalDepletionRate) *
                                                  (m.cumulativeCoalExtraction / coalBase));
        const double orePot = orePotRaw * oreDecline;
        const double energyPot = energyPotRaw * coalDecline;
        const double nonFoodPot = std::max(0.0, 0.55 * orePot + 0.30 * energyPot + 0.25 * constructionPot);
        const double climateMult = std::max(0.05, static_cast<double>(map.getCountryClimateFoodMultiplier(i)));
        const double resourcePerCap =
            (0.65 * orePot + 0.35 * energyPot) /
            std::max(1.0, pop * 0.00035);
        resourceTightness[static_cast<size_t>(i)] = std::clamp(1.0 / (1.0 + resourcePerCap), 0.0, 1.0);

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

        // Low-capability societies should face larger year-to-year food variability and weaker
        // smoothing buffers. Anchor this to the same knowledge scale used by CLI capability metrics.
        const auto& domains = c.getKnowledge();
        double meanDomain = 0.0;
        for (double kv : domains) meanDomain += std::max(0.0, kv);
        meanDomain /= static_cast<double>(Country::kDomains);
        const double techScale = std::max(1.0, 16000.0 * std::max(0.25, cfg.tech.capabilityThresholdScale));
        const double techCapabilityApprox = std::clamp(
            (meanDomain * (0.7 + 0.3 * m.marketAccess) * (0.7 + 0.3 * m.institutionCapacity)) / techScale,
            0.0, 1.0);
        const double lowCapExposure = clamp01((0.35 - techCapabilityApprox) / 0.35);
        const double climateVolatility = clamp01(std::abs(1.0 - climateMult) / 0.50);
        const double buffering = clamp01(0.55 * m.marketAccess + 0.45 * m.institutionCapacity);
        const double localPeriod = 11.0 + static_cast<double>(i % 7);
        const double localPhase = std::fmod(19.0 + static_cast<double>((i * 37) % 97) + 53.0 * seedPhase, 251.0);
        const double localCycle = std::sin(twoPi * (static_cast<double>(year) + localPhase) / localPeriod);
        const double shockAmplitude = lowCapExposure * (0.14 + 0.10 * climateVolatility) * (1.0 - 0.65 * buffering);
        const double harvestShockMul = std::clamp(1.0 + shockAmplitude * (0.65 * globalFoodShock + 0.35 * localCycle), 0.65, 1.30);

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
        const double fiscalCapacity = clamp01(c.getFiscalCapacity());
        const double institutionCapacityForTax = clamp01(m.institutionCapacity);
        const double taxCapacity = clamp01(0.50 * fiscalCapacity + 0.50 * institutionCapacityForTax);
        const double taxFloor = 0.02 + 0.02 * taxCapacity;
        const double taxCeiling = 0.10 + 0.24 * taxCapacity;
        const double taxTargetRaw =
            0.03 +
            0.08 * taxCapacity +
            0.06 * (1.0 - clamp01(c.getInequality())) * (0.35 + 0.65 * taxCapacity) +
            0.03 * (1.0 - taxStress) * (0.30 + 0.70 * taxCapacity);
        const double taxTarget = std::clamp(taxTargetRaw, taxFloor, taxCeiling);
        const double taxBefore = c.getTaxRate();
        c.setTaxRate(0.80 * taxBefore + 0.20 * taxTarget);
        ldbg.dbg_legit_budget_taxRateTarget = taxTarget;
        ldbg.dbg_legit_budget_taxRateBefore = taxBefore;
        ldbg.dbg_legit_budget_taxRateAfter = c.getTaxRate();
        ldbg.dbg_legit_budget_taxRateSource = 1;

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
                const bool storage = TechnologyManager::hasTech(tech, c, 1); // Pottery
                const double foragingShare = agr ? std::clamp(cfg.food.foragingWithAgriShare, 0.0, 1.0)
                                                 : std::clamp(cfg.food.foragingNoAgriShare, 0.0, 1.0);
                const double farmingShare = agr ? std::clamp(cfg.food.farmingWithAgriShare, 0.0, 2.0) : 0.10;
                const double farmTechMul = 1.0 + (irr ? 0.22 : 0.0) + (agr ? 0.45 : 0.0) + (storage ? 0.10 : 0.0);
                const double laborScale = std::max(1.0, 0.0014 * (foragingPot + farmingPot) /
                                                        std::max(0.35, 0.65 * ctl + 0.35 * std::min(1.8, farmTechMul)));
                const double laborFrac = (w > 0.0) ? (w / (w + laborScale)) : 0.0;
                const double deploy = clamp01((0.45 + 0.55 * ctl) * (0.80 + 0.20 * std::min(1.8, farmTechMul)));
                const double env = clamp01(climateMult * (1.0 - 0.30 * diseaseBurden) * (0.40 + 0.60 * std::max(0.2, 1.0 - cfg.food.climateSensitivity * (1.0 - climateMult))));
                const double exploitation = clamp01(laborFrac * deploy * env);
                const double foragingOut = foragingPot * foragingShare * exploitation;
                const double farmingOut = farmingPot * farmingShare * exploitation *
                                          (0.40 + 0.60 * ctl) * (0.35 + 0.65 * inst);
                return std::max(0.0, foragingOut + farmingOut);
            }
            if (sector == 1) { // goods
                const bool mining = TechnologyManager::hasTech(tech, c, 4);
                const bool metal = TechnologyManager::hasTech(tech, c, TechId::METALLURGY);
                const bool ind = TechnologyManager::hasTech(tech, c, 52);
                const double techMul = 1.0 + (mining ? 0.18 : 0.0) + (metal ? 0.25 : 0.0) + (ind ? 0.45 : 0.0);
                const double oreCap = std::max(1.0, orePot * (0.25 + 0.75 * ctl));
                const double energyCap = std::max(1.0, energyPot * (0.20 + 0.80 * access));
                const double constrCap = std::max(1.0, constructionPot * (0.30 + 0.70 * inst));
                const double oreNeed = 45.0 + std::max(0.05, cfg.economy.oreIntensity) * std::pow(w + 1.0, 0.85);
                const double energyNeed = 35.0 + std::max(0.05, cfg.economy.energyIntensity) * std::pow(w + 1.0, 0.85);
                const double constrNeed = 20.0 + 0.40 * std::pow(w + 1.0, 0.70);
                const double inputLimiter = std::clamp(std::min({oreCap / oreNeed, energyCap / energyNeed, constrCap / constrNeed}), 0.0, 1.0);
                const double base = (0.45 * oreCap + 0.25 * constrCap + 0.30 * energyCap);
                return base * techMul * ctl * access * inputLimiter *
                       std::pow(w + 1.0, std::max(0.35, cfg.economy.goodsLaborElasticity)) *
                       (0.004 + 0.00035 * cap);
            }
            if (sector == 2) { // services
                const double urbanMul = 0.20 + 0.80 * urban;
                const double energySoftLimit = 0.85 + 0.15 * std::clamp(energyPot / (energyPot + 75.0), 0.0, 1.0);
                return (0.002 + 0.0004 * cap) * inst * access * urbanMul * energySoftLimit *
                       std::pow(w + 1.0, std::max(0.35, cfg.economy.servicesLaborElasticity));
            }
            if (sector == 3) { // admin
                return (0.00025 * pop) * (0.30 + 0.70 * admin) * std::pow(w + 1.0, 0.80);
            }
            // military
            const double milInputNeed = 28.0 + std::max(0.05, cfg.economy.goodsToMilitary) * std::pow(w + 1.0, 0.88);
            const double milInputAvail = 0.55 * std::max(1.0, orePot) + 0.45 * std::max(1.0, energyPot);
            const double milInputLimiter = std::clamp(milInputAvail / milInputNeed, 0.0, 1.0);
            return (0.00018 * pop) * (0.25 + 0.75 * logi) * milInputLimiter * std::pow(w + 1.0, 0.84);
        };

        const double subsistenceFoodNeedAnnual =
            cohorts[0] * 0.00085 +
            cohorts[1] * 0.00100 +
            cohorts[2] * 0.00120 +
            cohorts[3] * 0.00110 +
            cohorts[4] * 0.00095;

        const double subsistenceBuffer = 0.10;
        const double subsistenceTargetAnnual = subsistenceFoodNeedAnnual * (1.0 + subsistenceBuffer);

        if (laborLeft > 1e-9) {
            const double maxFoodWithRemainingLabor = sectorOutput(0, l[0] + laborLeft);
            if (maxFoodWithRemainingLabor <= subsistenceTargetAnnual + 1e-9) {
                l[0] += laborLeft;
                laborLeft = 0.0;
            } else {
                double lo = l[0];
                double hi = l[0] + laborLeft;
                for (int it = 0; it < 28; ++it) {
                    const double mid = 0.5 * (lo + hi);
                    const double out = sectorOutput(0, mid);
                    if (out < subsistenceTargetAnnual) {
                        lo = mid;
                    } else {
                        hi = mid;
                    }
                }
                const double foodLaborNeeded = std::max(0.0, hi - l[0]);
                const double grant = std::min(laborLeft, foodLaborNeeded);
                l[0] += grant;
                laborLeft -= grant;
            }
        }

        const double lastPriceFood = std::max(1e-6, m.priceFood);
        const double lastPriceGoods = std::max(1e-6, m.priceGoods);
        const double lastPriceServices = std::max(1e-6, m.priceServices);
        const double lastPriceMilitary = std::max(1e-6, m.priceMilitarySupply);

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

                double priceSignal = lastPriceFood;
                if (s == 1) {
                    priceSignal = lastPriceGoods;
                } else if (s == 2 || s == 3) {
                    priceSignal = lastPriceServices;
                } else if (s == 4) {
                    priceSignal = lastPriceMilitary;
                }
                const double score = marginal * priceSignal;
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
        m.lastLaborFoodShare = (laborTotal > 1e-9) ? clamp01(l[0] / laborTotal) : 0.0;

        foodOutAnnual[static_cast<size_t>(i)] = sectorOutput(0, l[0]) * harvestShockMul;
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
        const bool potteryStorage = TechnologyManager::hasTech(tech, c, 1); // Pottery/storage
        const bool preservation = TechnologyManager::hasTech(tech, c, 71);  // Preservation
        const double storageTechBonus = (potteryStorage ? 0.30 : 0.0) + (preservation ? 0.22 : 0.0);
        m.foodStockCap = std::max(1.0, (std::max(0.05, cfg.food.storageBase) + 1.10 * m.marketAccess + 0.85 * m.institutionCapacity + storageTechBonus) * std::max(1.0, subsistenceFoodNeedAnnual));
        const double climateSpoilage = clamp01((1.0 - climateMult) * 0.6 + 0.3);
        const double lowCapFragility = lowCapExposure * (0.35 + 0.65 * (1.0 - buffering));
        const double shockStress = std::max(0.0, 1.0 - harvestShockMul);
        m.spoilageRate = std::clamp(std::max(0.0, cfg.food.spoilageBase) + 0.18 * climateSpoilage - 0.05 * m.institutionCapacity -
                                        (potteryStorage ? 0.04 : 0.0) - (preservation ? 0.05 : 0.0) +
                                        0.08 * lowCapFragility + 0.05 * shockStress,
                                    0.01, 0.35);
        const double availableBeforeLosses = std::max(0.0, m.foodStock + foodOutAnnual[static_cast<size_t>(i)] * yearsD);
        const double spoilageTotal = m.foodStock * (1.0 - std::pow(std::max(0.0, 1.0 - m.spoilageRate), yearsD));
        const double handlingLossRate = std::clamp(
            lowCapExposure * (0.12 + 0.12 * (1.0 - m.institutionCapacity) + 0.08 * climateVolatility + (c.isAtWar() ? 0.05 : 0.0)),
            0.0, 0.60);
        const double remainingAfterSpoilage = std::max(0.0, availableBeforeLosses - spoilageTotal);
        const double handlingLossTotal =
            remainingAfterSpoilage * (1.0 - std::pow(std::max(0.0, 1.0 - handlingLossRate), yearsD));

        const double incomeProxy = std::max(0.0, oldRealWage[static_cast<size_t>(i)]);
        const double goodsNeedAnnualHousehold = pop * (0.00022 + 0.00034 * clamp01(incomeProxy / 2.5));
        const double servicesNeedAnnualHousehold = pop * (0.00016 + 0.00028 * clamp01(incomeProxy / 2.5));
        const double militaryNeedAnnual = pop * (0.00004 + 0.00020 * c.getMilitarySpendingShare()) * (c.isAtWar() ? 1.5 : 1.0);
        const double govGoodsNeedAnnual = pop * (0.00005 + 0.00018 * c.getInfraSpendingShare() + 0.00012 * c.getAdminSpendingShare());
        const double govServicesNeedAnnual = pop * (0.00004 + 0.00020 * c.getHealthSpendingShare() + 0.00018 * c.getEducationSpendingShare() + 0.00018 * c.getRndSpendingShare());

        foodNeed[static_cast<size_t>(i)] = subsistenceFoodNeedAnnual * yearsD;
        const double mismatchNeedBoost =
            1.0 +
            std::max(0.0, cfg.economy.tradeResourceMismatchDemandBoost) *
                resourceTightness[static_cast<size_t>(i)] *
                (0.35 + 0.65 * m.marketAccess);
        goodsNeed[static_cast<size_t>(i)] = (goodsNeedAnnualHousehold + govGoodsNeedAnnual) * yearsD * mismatchNeedBoost;
        servicesNeed[static_cast<size_t>(i)] = (servicesNeedAnnualHousehold + govServicesNeedAnnual) * yearsD;
        militaryNeed[static_cast<size_t>(i)] = militaryNeedAnnual * yearsD;

        foodAvailPreTrade[static_cast<size_t>(i)] = std::max(0.0, availableBeforeLosses - spoilageTotal - handlingLossTotal);
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
        m.lastFoodAvailableBeforeLosses = availableBeforeLosses / yearsD;
        m.lastFoodSpoilageLoss = spoilageTotal / yearsD;
        m.lastFoodStorageLoss = handlingLossTotal / yearsD;
        m.skilledMigrationInRate = 0.0;
        m.skilledMigrationOutRate = 0.0;

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
            const double scarcityMismatch = std::abs(
                resourceTightness[static_cast<size_t>(a)] - resourceTightness[static_cast<size_t>(b)]);
            const double scarcityBoost =
                1.0 + std::max(0.0, cfg.economy.tradeScarcityCapacityBoost) * scarcityMismatch;

            double cap = m_cfg.baseLandCapacity * (0.30 + 0.70 * logi) * (0.30 + 0.70 * control) * (0.45 + 0.55 * connectivity) * std::min(4.0, 0.35 + contactScale * 0.08);
            cap *= scarcityBoost;
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
        const double scarcityMismatch = std::abs(
            resourceTightness[static_cast<size_t>(a)] - resourceTightness[static_cast<size_t>(b)]);
        const double scarcityBoost =
            1.0 + std::max(0.0, cfg.economy.tradeScarcityCapacityBoost) * scarcityMismatch;

        double cap = m_cfg.baseSeaCapacity * (0.35 + 0.65 * logi) * (0.50 + 0.50 * connectivity);
        cap *= (0.75 + 0.25 * std::min(1.0, static_cast<double>(ca.getPorts().size() + cb.getPorts().size()) / 6.0));
        cap *= scarcityBoost;

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

    std::vector<double> marketSophistication(static_cast<size_t>(n), 0.0);
    std::vector<double> searchFriction(static_cast<size_t>(n), 0.0);
    std::vector<double> creditFriction(static_cast<size_t>(n), 0.0);
    std::vector<double> infoFriction(static_cast<size_t>(n), 0.0);
    std::vector<double> priceStickiness(static_cast<size_t>(n), 0.0);
    std::vector<double> priceAdjustSpeed(static_cast<size_t>(n), 0.0);
    double sophisticationSum = 0.0;
    for (int i = 0; i < n; ++i) {
        const Country& c = countries[static_cast<size_t>(i)];
        const auto& m = c.getMacroEconomy();
        const double bourgeois = clamp01(c.getBourgeoisInfluence());
        const double merchantSent =
            clamp01(c.getClassSentiment(Country::SocialClass::Merchants));
        const double artisanSent =
            clamp01(c.getClassSentiment(Country::SocialClass::Artisans));
        const double sophistication = clamp01(
            0.26 * clamp01(m.marketAccess) +
            0.22 * clamp01(m.connectivityIndex) +
            0.20 * clamp01(m.institutionCapacity) +
            0.14 * clamp01(m.humanCapital) +
            0.10 * bourgeois +
            0.08 * clamp01(0.5 * merchantSent + 0.5 * artisanSent));
        marketSophistication[static_cast<size_t>(i)] = sophistication;
        sophisticationSum += sophistication;

        const double debtStress =
            clamp01(c.getDebt() / std::max(1.0, 22.0 * std::max(1.0, m.netRevenue + 0.25 * c.getLastTaxBase())));
        const double creditWeight = std::clamp(cfg.economy.creditFrictionWeight, 0.0, 1.0);
        const double infoWeight = std::clamp(cfg.economy.informationFrictionWeight, 0.0, 1.0);
        const double baseSearch = std::clamp(cfg.economy.tradeSearchFrictionBase, 0.0, 1.0);

        const double credit = clamp01(
            creditWeight *
            (0.62 * debtStress + 0.38 * clamp01(m.leakageRate)) *
            (1.0 - 0.35 * clamp01(m.credibleCommitmentIndex)));
        const double info = clamp01(
            infoWeight *
            (1.0 - (0.45 * clamp01(m.connectivityIndex) +
                    0.35 * clamp01(m.mediaThroughputIndex) +
                    0.20 * clamp01(m.ideaMarketIntegrationIndex))));

        creditFriction[static_cast<size_t>(i)] = credit;
        infoFriction[static_cast<size_t>(i)] = info;
        searchFriction[static_cast<size_t>(i)] = std::clamp(
            baseSearch +
                0.32 * (1.0 - sophistication) +
                0.25 * credit +
                0.25 * info +
                0.18 * (c.isAtWar() ? 1.0 : 0.0),
            0.05,
            0.95);
        const double stickBase = std::clamp(cfg.economy.priceStickinessBase, 0.0, 0.98);
        priceStickiness[static_cast<size_t>(i)] = std::clamp(
            stickBase +
                0.28 * (1.0 - sophistication) +
                0.22 * credit,
            0.05,
            0.96);
        const double baseAdj = std::clamp(cfg.economy.priceAdjustmentSpeed, 0.01, 1.0);
        priceAdjustSpeed[static_cast<size_t>(i)] = std::clamp(
            baseAdj *
                (0.45 + 0.55 * sophistication) *
                (1.0 - 0.45 * priceStickiness[static_cast<size_t>(i)]),
            0.01,
            0.55);
    }
    const double worldMarketSophistication = clamp01(sophisticationSum / std::max(1.0, static_cast<double>(n)));
    const int baseIterations = std::max(1, cfg.economy.marketClearingBaseIterations);
    const int maxIterations = std::max(baseIterations, cfg.economy.marketClearingMaxIterations);
    const int marketIterations = std::clamp(
        baseIterations + static_cast<int>(std::lround((maxIterations - baseIterations) * worldMarketSophistication)),
        baseIterations,
        maxIterations);

    auto tradeGood = [&](std::vector<double>& supply,
                         std::vector<double>& demand,
                         const std::vector<double>& seedPrice,
                         const std::vector<double>& scarcityIndex,
                         bool foodCategory) {
        struct Cand {
            int supplier = -1;
            int edgeIdx = -1;
            double deliveredPrice = 0.0;
        };

        std::vector<double> net(static_cast<size_t>(n), 0.0);
        std::vector<double> roundPrice = seedPrice;
        const double basePrice = foodCategory ? 1.0 : 2.0;
        const double alphaCost = foodCategory ? 0.85 : 0.75;
        const double minMul = foodCategory ? 0.35 : 0.45;
        const double maxMul = foodCategory ? 6.5 : 7.0;

        for (int iter = 0; iter < marketIterations; ++iter) {
            bool anyTrade = false;
            std::vector<int> buyers;
            buyers.reserve(static_cast<size_t>(n));
            for (int i = 0; i < n; ++i) {
                if (countries[static_cast<size_t>(i)].getPopulation() <= 0) continue;
                if (demand[static_cast<size_t>(i)] <= 1e-9) continue;
                buyers.push_back(i);

                const double targetPrice = smoothScarcityPrice(
                    basePrice,
                    std::max(1e-9, demand[static_cast<size_t>(i)]),
                    std::max(1e-9, supply[static_cast<size_t>(i)]),
                    alphaCost,
                    minMul,
                    maxMul);
                const double stick = priceStickiness[static_cast<size_t>(i)];
                const double step = priceAdjustSpeed[static_cast<size_t>(i)];
                double p = roundPrice[static_cast<size_t>(i)];
                p = (stick * p) + (1.0 - stick) * targetPrice;
                p = p * (1.0 + step * (targetPrice - p) / std::max(1e-9, p));
                roundPrice[static_cast<size_t>(i)] = std::clamp(p, basePrice * 0.22, basePrice * 12.0);
            }
            std::sort(buyers.begin(), buyers.end(), [&](int a, int b) {
                const double pa = roundPrice[static_cast<size_t>(a)];
                const double pb = roundPrice[static_cast<size_t>(b)];
                if (pa != pb) return pa > pb;
                return a < b;
            });

            for (int buyer : buyers) {
                double need = demand[static_cast<size_t>(buyer)];
                if (need <= 1e-9) continue;
                const double needInitial = need;
                const double localSupply = std::max(0.0, supply[static_cast<size_t>(buyer)]);
                const double local = std::max(1e-9, roundPrice[static_cast<size_t>(buyer)]);
                const double shortagePressure = clamp01(needInitial / std::max(1e-9, needInitial + localSupply));
                const double priceStress = clamp01((local / std::max(1e-9, basePrice) - 1.0) / 3.0);
                const double scarcityStress =
                    (buyer >= 0 && buyer < n) ? clamp01(scarcityIndex[static_cast<size_t>(buyer)]) : 0.0;
                const double urgency = clamp01(0.55 * shortagePressure + 0.25 * priceStress + 0.20 * scarcityStress);
                const double maxPremium = std::max(1.0, cfg.economy.tradeMaxPricePremium);
                const double frictionTolerance =
                    std::clamp(1.0 - 0.60 * searchFriction[static_cast<size_t>(buyer)] - 0.35 * creditFriction[static_cast<size_t>(buyer)],
                               0.25,
                               1.0);
                const double acceptableDelivered =
                    local * (1.0 + (maxPremium - 1.0) * urgency * frictionTolerance);

                std::vector<Cand> cands;
                cands.reserve(edgesByCountry[static_cast<size_t>(buyer)].size());
                for (int ei : edgesByCountry[static_cast<size_t>(buyer)]) {
                    const Edge& e = edges[static_cast<size_t>(ei)];
                    const int supplier = (e.a == buyer) ? e.b : e.a;
                    if (supplier < 0 || supplier >= n) continue;
                    if (supply[static_cast<size_t>(supplier)] <= 1e-9) continue;
                    const double capRem = foodCategory ? e.capFood : e.capGoods;
                    if (capRem <= 1e-9) continue;
                    const double searchMix = 0.5 * (searchFriction[static_cast<size_t>(buyer)] + searchFriction[static_cast<size_t>(supplier)]);
                    const double creditMix = 0.5 * (creditFriction[static_cast<size_t>(buyer)] + creditFriction[static_cast<size_t>(supplier)]);
                    const double infoMix = 0.5 * (infoFriction[static_cast<size_t>(buyer)] + infoFriction[static_cast<size_t>(supplier)]);
                    const double transportCost = e.costPerUnit * (1.0 + 0.70 * searchMix + 0.45 * creditMix + 0.35 * infoMix);
                    cands.push_back(Cand{
                        supplier,
                        ei,
                        roundPrice[static_cast<size_t>(supplier)] + transportCost});
                }
                std::sort(cands.begin(), cands.end(), [&](const Cand& l, const Cand& r) {
                    if (l.deliveredPrice != r.deliveredPrice) return l.deliveredPrice < r.deliveredPrice;
                    if (l.supplier != r.supplier) return l.supplier < r.supplier;
                    return l.edgeIdx < r.edgeIdx;
                });
                int maxCand = std::clamp(
                    2 + static_cast<int>(std::lround(6.0 * (1.0 - searchFriction[static_cast<size_t>(buyer)]) +
                                                     2.0 * marketSophistication[static_cast<size_t>(buyer)])),
                    2,
                    10);
                if (static_cast<int>(cands.size()) > maxCand) {
                    cands.resize(static_cast<size_t>(maxCand));
                }

                for (const Cand& cand : cands) {
                    if (need <= 1e-9) break;
                    if (cand.deliveredPrice > acceptableDelivered) break;

                    Edge& e = edges[static_cast<size_t>(cand.edgeIdx)];
                    double& capRem = foodCategory ? e.capFood : e.capGoods;
                    const double searchMix = 0.5 * (searchFriction[static_cast<size_t>(buyer)] + searchFriction[static_cast<size_t>(cand.supplier)]);
                    const double creditMix = 0.5 * (creditFriction[static_cast<size_t>(buyer)] + creditFriction[static_cast<size_t>(cand.supplier)]);
                    const double tradableCap = capRem * std::clamp(1.0 - 0.55 * searchMix, 0.25, 1.0);
                    const double execution = std::clamp(1.0 - 0.35 * searchMix - 0.25 * creditMix, 0.25, 1.0);
                    const double gross = std::min({need, supply[static_cast<size_t>(cand.supplier)], tradableCap});
                    const double amount = gross * execution;
                    if (amount <= 1e-12) continue;

                    anyTrade = true;
                    need -= amount;
                    supply[static_cast<size_t>(cand.supplier)] -= amount;
                    capRem -= amount;
                    net[static_cast<size_t>(buyer)] += amount;
                    net[static_cast<size_t>(cand.supplier)] -= amount;

                    const double value = amount * cand.deliveredPrice;
                    Country::MacroEconomyState& mb =
                        countries[static_cast<size_t>(buyer)].getMacroEconomyMutable();
                    Country::MacroEconomyState& ms =
                        countries[static_cast<size_t>(cand.supplier)].getMacroEconomyMutable();
                    mb.importsValue += value;
                    ms.exportsValue += value;
                    const size_t ti = static_cast<size_t>(cand.supplier) * static_cast<size_t>(n) +
                                      static_cast<size_t>(buyer);
                    if (ti < m_tradeIntensity.size()) {
                        const double normBase = std::max(1.0, cfg.economy.tradeIntensityValueNormBase);
                        const double popBuyer = static_cast<double>(std::max<long long>(
                            1, countries[static_cast<size_t>(buyer)].getPopulation()));
                        const Country& cb = countries[static_cast<size_t>(buyer)];
                        const Country& cs = countries[static_cast<size_t>(cand.supplier)];
                        const double connectivity =
                            0.5 * (clamp01(cb.getConnectivityIndex()) + clamp01(cs.getConnectivityIndex()));
                        const double access =
                            0.5 * (clamp01(cb.getMacroEconomy().marketAccess) +
                                   clamp01(cs.getMacroEconomy().marketAccess));
                        const double scale = std::max(0.01, cfg.economy.tradeIntensityScale);
                        const double denom = normBase + popBuyer * 0.10;
                        const double devMul =
                            (0.20 + 0.80 * connectivity) * (0.30 + 0.70 * access);
                        const float inc = static_cast<float>(std::min(
                            1.0, (value / denom) * scale * (0.70 + 0.30 * urgency) * devMul));
                        m_tradeIntensity[ti] = std::min(1.0f, m_tradeIntensity[ti] + inc);
                    }
                }
                demand[static_cast<size_t>(buyer)] = need;
            }
            if (!anyTrade) {
                break;
            }
        }

        for (int i = 0; i < n; ++i) {
            if (foodCategory) {
                foodAvailPreTrade[static_cast<size_t>(i)] += net[static_cast<size_t>(i)];
            } else {
                goodsAvailPreTrade[static_cast<size_t>(i)] += net[static_cast<size_t>(i)];
            }
        }
    };

    std::vector<double> foodScarcity(static_cast<size_t>(n), 0.0);
    for (int i = 0; i < n; ++i) {
        const double need = std::max(0.0, foodDemand[static_cast<size_t>(i)]);
        const double avail = std::max(0.0, foodSupply[static_cast<size_t>(i)]);
        foodScarcity[static_cast<size_t>(i)] = clamp01(need / std::max(1e-9, need + avail));
    }

    tradeGood(foodSupply, foodDemand, foodPrice, foodScarcity, true);
    tradeGood(goodsSupply, goodsDemand, goodsPrice, resourceTightness, false);

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

    // Class-network spillovers: cross-border merchant/artisan/bureaucratic sentiment transfer.
    if (m_tradeIntensity.size() >= static_cast<size_t>(n) * static_cast<size_t>(n)) {
        for (int i = 0; i < n; ++i) {
            Country& c = countries[static_cast<size_t>(i)];
            if (c.getPopulation() <= 0) continue;
            double wSum = 0.0;
            double artisan = 0.0;
            double merchant = 0.0;
            double bureaucrat = 0.0;
            for (int j = 0; j < n; ++j) {
                if (j == i) continue;
                const Country& o = countries[static_cast<size_t>(j)];
                if (o.getPopulation() <= 0) continue;
                const double out = static_cast<double>(m_tradeIntensity[static_cast<size_t>(i) * static_cast<size_t>(n) + static_cast<size_t>(j)]);
                const double in = static_cast<double>(m_tradeIntensity[static_cast<size_t>(j) * static_cast<size_t>(n) + static_cast<size_t>(i)]);
                const double w = out + 0.60 * in;
                if (w <= 1e-4) continue;
                wSum += w;
                artisan += w * o.getClassSentiment(Country::SocialClass::Artisans);
                merchant += w * o.getClassSentiment(Country::SocialClass::Merchants);
                bureaucrat += w * o.getClassSentiment(Country::SocialClass::Bureaucrats);
            }
            if (wSum > 1e-8) {
                c.applyClassNetworkSignals(artisan / wSum, merchant / wSum, bureaucrat / wSum, years);
            }
        }
    }

    for (int i = 0; i < n; ++i) {
        Country& c = countries[static_cast<size_t>(i)];
        Country::MacroEconomyState& m = c.getMacroEconomyMutable();
        auto& ldbg = m.legitimacyDebug;

        const double pop = static_cast<double>(std::max<long long>(0, c.getPopulation()));
        const double control = clamp01(c.getAvgControl());
        const double legitimacy = clamp01(c.getLegitimacy());
        const double stability = clamp01(c.getStability());
        const double urban = (pop > 1.0) ? clamp01(c.getTotalCityPopulation() / pop) : 0.0;
        const double diseaseBurden = clamp01(m.diseaseBurden);
        const double orePot = std::max(0.0, map.getCountryOrePotential(i));
        const double energyPot = std::max(0.0, map.getCountryEnergyPotential(i));

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

        // Lightweight extraction depletion tracking (best deposits are exhausted first).
        const double oreExtractAnnual =
            std::max(0.0, m.lastGoodsOutput) * std::max(0.05, cfg.economy.oreIntensity) +
            std::max(0.0, m.lastMilitaryOutput) * 0.35 * std::max(0.05, cfg.economy.oreIntensity);
        const double coalExtractAnnual =
            (std::max(0.0, m.lastGoodsOutput) + 0.80 * std::max(0.0, m.lastMilitaryOutput)) *
            std::max(0.05, cfg.economy.energyIntensity);
        if (orePot > 1e-6) {
            m.cumulativeOreExtraction = std::max(0.0, m.cumulativeOreExtraction + oreExtractAnnual * yearsD);
        }
        if (energyPot > 1e-6) {
            m.cumulativeCoalExtraction = std::max(0.0, m.cumulativeCoalExtraction + coalExtractAnnual * yearsD);
        }

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

        // Mechanism indices: competition/idea-market/commitment/factor-prices/media/merchant coalition.
        int adjacentLive = 0;
        int adjacentPeerPower = 0;
        int adjacentEnemies = 0;
        const double ownPower =
            (0.50 + 0.50 * std::max(0.0, m.lastGoodsOutput + m.lastServicesOutput + 0.5 * m.lastMilitaryOutput)) *
            (0.30 + 0.70 * m.institutionCapacity) *
            (0.30 + 0.70 * m.connectivityIndex) *
            (0.45 + 0.55 * control) *
            std::max(1.0, pop);
        for (int j : map.getAdjacentCountryIndicesPublic(i)) {
            if (j < 0 || j >= n || j == i) continue;
            const Country& nb = countries[static_cast<size_t>(j)];
            if (nb.getPopulation() <= 0) continue;
            adjacentLive++;
            const auto& nm = nb.getMacroEconomy();
            const double nbPower =
                (0.50 + 0.50 * std::max(0.0, nm.lastGoodsOutput + nm.lastServicesOutput + 0.5 * nm.lastMilitaryOutput)) *
                (0.30 + 0.70 * clamp01(nm.institutionCapacity)) *
                (0.30 + 0.70 * clamp01(nm.connectivityIndex)) *
                (0.45 + 0.55 * clamp01(nb.getAvgControl())) *
                std::max(1.0, static_cast<double>(nb.getPopulation()));
            const double ratio = ownPower / std::max(1.0, nbPower);
            if (ratio >= 0.45 && ratio <= 2.20) {
                adjacentPeerPower++;
            }
            if (isEnemy(i, j) || isEnemy(j, i)) {
                adjacentEnemies++;
            }
        }
        const double neighborDensity = clamp01(static_cast<double>(adjacentLive) / 8.0);
        const double peerBalance = (adjacentLive > 0) ? clamp01(static_cast<double>(adjacentPeerPower) / static_cast<double>(adjacentLive)) : 0.0;
        const double threatPressure =
            clamp01(0.55 * (c.isAtWar() ? 1.0 : 0.0) +
                    0.45 * ((adjacentLive > 0) ? static_cast<double>(adjacentEnemies) / static_cast<double>(adjacentLive) : 0.0));
        const double exitOptions =
            clamp01(0.40 * m.connectivityIndex + 0.30 * m.marketAccess + 0.20 * tradeNorm + 0.10 * neighborDensity);
        m.competitionFragmentationIndex =
            clamp01(0.32 * neighborDensity + 0.28 * peerBalance + 0.22 * threatPressure + 0.18 * exitOptions);

        const bool hasWriting = TechnologyManager::hasTech(tech, c, TechId::WRITING) ||
                                TechnologyManager::hasTech(tech, c, TechId::PROTO_WRITING);
        const bool hasUniversities = TechnologyManager::hasTech(tech, c, TechId::UNIVERSITIES);
        const bool hasScientificMethod = TechnologyManager::hasTech(tech, c, TechId::SCIENTIFIC_METHOD);
        const bool hasPrinting = TechnologyManager::hasTech(tech, c, 46);
        const bool hasPaper = TechnologyManager::hasTech(tech, c, 33);
        const double mediaTech = clamp01(
            (hasWriting ? 0.28 : 0.0) +
            (hasPaper ? 0.18 : 0.0) +
            (hasPrinting ? 0.30 : 0.0) +
            (hasUniversities ? 0.16 : 0.0) +
            (hasScientificMethod ? 0.12 : 0.0));
        m.mediaThroughputIndex = clamp01(
            0.14 +
            0.22 * c.getEducationSpendingShare() +
            0.22 * m.institutionCapacity +
            0.20 * m.marketAccess +
            0.10 * m.connectivityIndex +
            0.12 * mediaTech);

        double affinityAcc = 0.0;
        int affinityCnt = 0;
        for (int j : map.getAdjacentCountryIndicesPublic(i)) {
            if (j < 0 || j >= n || j == i) continue;
            const Country& nb = countries[static_cast<size_t>(j)];
            if (nb.getPopulation() <= 0) continue;
            affinityAcc += c.computeCulturalAffinity(nb);
            affinityCnt++;
        }
        const double culturalBridge = (affinityCnt > 0) ? clamp01(affinityAcc / static_cast<double>(affinityCnt)) : 0.45;
        m.ideaMarketIntegrationIndex = clamp01(
            0.33 * tradeNorm +
            0.22 * m.connectivityIndex +
            0.18 * m.marketAccess +
            0.17 * m.mediaThroughputIndex +
            0.10 * culturalBridge);

        const double taxBurdenSignal = clamp01(c.getTaxRate() / 0.30);
        const double predationRisk = clamp01(
            0.40 * m.leakageRate +
            0.20 * (1.0 - m.compliance) +
            0.20 * (1.0 - control) +
            0.20 * taxBurdenSignal);
        const double incomeSafe = std::max(1.0, netRevenueAnnual[static_cast<size_t>(i)] + 0.20 * taxableBaseAnnual[static_cast<size_t>(i)]);
        const double debtToIncome = clamp01(std::max(0.0, c.getDebt()) / std::max(1.0, 25.0 * incomeSafe));
        const double debtServiceAnnual =
            std::max(0.0, c.getDebt()) * (0.02 + 0.05 * (1.0 - m.institutionCapacity));
        const double serviceStress = clamp01(debtServiceAnnual / std::max(1.0, incomeSafe));
        m.credibleCommitmentIndex = clamp01(
            0.32 * m.institutionCapacity +
            0.25 * legitimacy +
            0.18 * control +
            0.15 * m.compliance +
            0.10 * (1.0 - predationRisk) -
            0.15 * debtToIncome -
            0.10 * serviceStress);

        const auto sat = [](double x, double s) {
            const double d = std::max(1e-9, s);
            const double v = std::max(0.0, x);
            return v / (v + d);
        };
        const double wageHigh = clamp01(m.realWage / 1.35);
        const double energyCheap = sat(energyPot * (0.35 + 0.65 * m.marketAccess), 24.0 + 0.00012 * pop);
        const double materialCheap = sat(orePot * (0.35 + 0.65 * m.marketAccess), 24.0 + 0.00012 * pop);
        m.relativeFactorPriceIndex = clamp01(wageHigh * (0.60 * energyCheap + 0.40 * materialCheap));

        const double portTerm = std::min(1.0, std::sqrt(static_cast<double>(c.getPorts().size()) / 8.0));
        const double bourgeoisInfluence = clamp01(c.getBourgeoisInfluence());
        const double merchantSentiment = clamp01(c.getClassSentiment(Country::SocialClass::Merchants));
        const double artisanSentiment = clamp01(c.getClassSentiment(Country::SocialClass::Artisans));
        m.merchantPowerIndex = clamp01(
            0.30 * tradeNorm +
            0.24 * urban +
            0.16 * m.marketAccess +
            0.10 * portTerm +
            0.10 * m.ideaMarketIntegrationIndex +
            0.06 * bourgeoisInfluence +
            0.04 * clamp01(0.55 * merchantSentiment + 0.45 * artisanSentiment));

        const double constraintScore = clamp01(
            0.45 * m.institutionCapacity +
            0.30 * legitimacy +
            0.25 * (1.0 - m.leakageRate));
        const double merchantImpulse = yearsD * 0.028 * m.merchantPowerIndex;
        if (constraintScore >= 0.45) {
            const double gain = merchantImpulse * (0.35 + 0.65 * constraintScore);
            m.institutionCapacity = clamp01(m.institutionCapacity + 0.55 * gain);
            m.compliance = clamp01(m.compliance + 0.45 * gain);
            m.leakageRate = clamp01(m.leakageRate - 0.50 * gain);
            m.humanCapital = clamp01(m.humanCapital + 0.16 * gain);
            m.knowledgeStock = clamp01(m.knowledgeStock + 0.20 * gain);
            if (!c.isAtWar()) {
                c.setLegitimacy(c.getLegitimacy() + 0.010 * gain);
                c.setStability(c.getStability() + 0.008 * gain);
            }
        } else {
            const double extractive = merchantImpulse * (1.0 - constraintScore);
            m.inequality = clamp01(m.inequality + 0.60 * extractive);
            m.leakageRate = clamp01(m.leakageRate + 0.42 * extractive);
            c.setLegitimacy(c.getLegitimacy() - 0.010 * extractive);
            c.setStability(c.getStability() - 0.008 * extractive);
        }

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
        ldbg.dbg_legit_econ_instCap = m.institutionCapacity;
        ldbg.dbg_legit_econ_wageGain = clamp01(wageGain);
        ldbg.dbg_legit_econ_famineSeverity = m.famineSeverity;
        ldbg.dbg_legit_econ_ineq = clamp01(m.inequality);
        ldbg.dbg_legit_econ_disease = diseaseBurden;
        ldbg.dbg_legit_econ_yearsD = yearsD;
        ldbg.dbg_legit_econ_up_inst = +0.010 * m.institutionCapacity * yearsD;
        ldbg.dbg_legit_econ_up_wage = +0.008 * clamp01(wageGain) * yearsD;
        ldbg.dbg_legit_econ_down_famine = -0.020 * m.famineSeverity * yearsD;
        ldbg.dbg_legit_econ_down_ineq = -0.015 * clamp01(m.inequality) * yearsD;
        ldbg.dbg_legit_econ_down_disease = -0.010 * diseaseBurden * yearsD;
        const double legitDrift =
            ldbg.dbg_legit_econ_up_inst +
            ldbg.dbg_legit_econ_up_wage +
            ldbg.dbg_legit_econ_down_famine +
            ldbg.dbg_legit_econ_down_ineq +
            ldbg.dbg_legit_econ_down_disease;
        const double legitBeforeEconomy = clamp01(c.getLegitimacy());
        if ((legitBeforeEconomy + legitDrift) < 0.0 && legitBeforeEconomy > 0.0) {
            ldbg.dbg_legit_clamp_to_zero_economy++;
        }
        c.setLegitimacy(legitBeforeEconomy + legitDrift);
        ldbg.dbg_legit_after_economy = clamp01(c.getLegitimacy());
        ldbg.dbg_legit_delta_economy = ldbg.dbg_legit_after_economy - ldbg.dbg_legit_start;
        c.setStability(stability + (0.008 * m.institutionCapacity - 0.018 * m.famineSeverity - 0.012 * diseaseBurden - 0.008 * (c.isAtWar() ? 1.0 : 0.0)) * yearsD);

        // Apply fiscal state update with compliance/leakage-adjusted net revenue.
        const int techCount = static_cast<int>(tech.getUnlockedTechnologies(c).size());
        const bool plagueAffected = map.isPlagueActive() && map.isCountryAffectedByPlague(i);
        c.applyBudgetFromEconomy(taxableBaseAnnual[static_cast<size_t>(i)],
                                 netRevenueAnnual[static_cast<size_t>(i)],
                                 years,
                                 techCount,
                                 plagueAffected,
                                 cfg);

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

    // Skilled-mobility / brain-drain mechanism: flow of specialists and tacit know-how.
    const bool hasTradeMatrix =
        m_tradeIntensity.size() >= static_cast<size_t>(n) * static_cast<size_t>(n);
    std::vector<double> skilledOutShare(static_cast<size_t>(n), 0.0);
    std::vector<double> skilledInShare(static_cast<size_t>(n), 0.0);

    for (int i = 0; i < n; ++i) {
        Country& src = countries[static_cast<size_t>(i)];
        if (src.getPopulation() <= 0) continue;
        Country::MacroEconomyState& sm = src.getMacroEconomyMutable();

        const double predationPressure = clamp01(
            0.45 * (1.0 - sm.credibleCommitmentIndex) +
            0.22 * clamp01(src.getTaxRate() / 0.30) +
            0.18 * (src.isAtWar() ? 1.0 : 0.0) +
            0.15 * clamp01(sm.refugeePush));
        const double connectivityGate = clamp01(0.65 * sm.connectivityIndex + 0.35 * sm.marketAccess);
        const double talentBase = clamp01(0.55 * sm.humanCapital + 0.45 * sm.knowledgeStock);
        const double outShare =
            std::clamp(0.0012 * yearsD * predationPressure * connectivityGate * (0.30 + 0.70 * talentBase), 0.0, 0.025);
        if (outShare <= 1e-7) continue;

        struct Dest { int j = -1; double score = 0.0; };
        std::vector<Dest> dest;
        dest.reserve(static_cast<size_t>(n));

        for (int j = 0; j < n; ++j) {
            if (j == i) continue;
            const Country& dst = countries[static_cast<size_t>(j)];
            if (dst.getPopulation() <= 0) continue;
            const Country::MacroEconomyState& dm = dst.getMacroEconomy();

            double conn = 0.0;
            if (hasTradeMatrix) {
                const size_t ij = static_cast<size_t>(i) * static_cast<size_t>(n) + static_cast<size_t>(j);
                const size_t ji = static_cast<size_t>(j) * static_cast<size_t>(n) + static_cast<size_t>(i);
                conn = static_cast<double>(m_tradeIntensity[ij]) + 0.60 * static_cast<double>(m_tradeIntensity[ji]);
            }
            if (map.areCountryIndicesNeighbors(i, j)) {
                conn = std::max(conn, 0.12);
            }
            if (conn <= 1e-8) continue;

            const double affinity = src.computeCulturalAffinity(dst);
            const double attract = clamp01(
                0.34 * dm.credibleCommitmentIndex +
                0.24 * dm.ideaMarketIntegrationIndex +
                0.18 * dm.marketAccess +
                0.14 * clamp01(dm.realWage / 1.35) +
                0.10 * affinity);
            const double score = std::max(0.0, conn * attract);
            if (score > 1e-8) {
                dest.push_back(Dest{j, score});
            }
        }

        if (dest.empty()) continue;
        std::sort(dest.begin(), dest.end(), [](const Dest& a, const Dest& b) {
            if (a.score != b.score) return a.score > b.score;
            return a.j < b.j;
        });
        if (dest.size() > 8) dest.resize(8);

        double sumScore = 0.0;
        for (const Dest& d : dest) sumScore += d.score;
        if (sumScore <= 1e-9) continue;

        skilledOutShare[static_cast<size_t>(i)] += outShare;
        for (const Dest& d : dest) {
            skilledInShare[static_cast<size_t>(d.j)] += outShare * (d.score / sumScore);
        }
    }

    for (int i = 0; i < n; ++i) {
        Country& c = countries[static_cast<size_t>(i)];
        if (c.getPopulation() <= 0) continue;
        Country::MacroEconomyState& m = c.getMacroEconomyMutable();
        const double inShare = std::clamp(skilledInShare[static_cast<size_t>(i)], 0.0, 0.08);
        const double outShare = std::clamp(skilledOutShare[static_cast<size_t>(i)], 0.0, 0.08);

        m.skilledMigrationInRate = std::clamp(inShare / yearsD, 0.0, 1.0);
        m.skilledMigrationOutRate = std::clamp(outShare / yearsD, 0.0, 1.0);

        const double netTalent = inShare - outShare;
        m.humanCapital = clamp01(m.humanCapital + 0.30 * netTalent);
        m.knowledgeStock = clamp01(m.knowledgeStock + 0.42 * netTalent);
        const double infra = std::max(0.0, c.getKnowledgeInfra());
        c.setKnowledgeInfra(std::max(0.0, infra * (1.0 - 0.10 * outShare) + 12.0 * inShare));
    }
}
