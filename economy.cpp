// economy.cpp

#include "economy.h"
#include "trade.h"
#include "news.h"

#include <algorithm>
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
    if (n <= 0) {
        return;
    }

    // Phase 6: climate aggregation (field-grid scan, once per tick).
    map.prepareCountryClimateCaches(n);

    m_lastTradeYear = year;
    m_lastTradeN = n;
    m_tradeIntensity.assign(static_cast<size_t>(n) * static_cast<size_t>(n), 0.0f);

    auto clamp01 = [](double v) { return std::max(0.0, std::min(1.0, v)); };

    auto isEnemy = [&](int a, int b) -> bool {
        if (a < 0 || b < 0 || a >= n || b >= n || a == b) return false;
        const auto& enemies = countries[static_cast<size_t>(a)].getEnemies();
        for (const Country* e : enemies) {
            if (e && e->getCountryIndex() == b) return true;
        }
        return false;
    };

    // Maintain shipping routes/ports (kept in TradeManager for rendering).
    tradeManager.establishShippingRoutes(countries, year, tech, map, news);

    // Sea influence (ports project control onto nav grid; hostile influence can choke shipping).
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
            const auto& ports = c.getPorts();
            for (const auto& p : ports) {
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

    // Precompute macro production/consumption and scarcity prices.
    std::vector<double> foodPre(static_cast<size_t>(n), 0.0);
    std::vector<double> nonFoodPre(static_cast<size_t>(n), 0.0);
    std::vector<double> foodCons(static_cast<size_t>(n), 0.0);
    std::vector<double> nonFoodCons(static_cast<size_t>(n), 0.0);
    std::vector<double> foodScarcity(static_cast<size_t>(n), 0.0);
    std::vector<double> nonFoodScarcity(static_cast<size_t>(n), 0.0);

    for (int i = 0; i < n; ++i) {
        Country& c = countries[static_cast<size_t>(i)];
        Country::MacroEconomyState& m = c.getMacroEconomyMutable();

        const double pop = static_cast<double>(std::max<long long>(0, c.getPopulation()));
        const double foodPot = std::max(0.0, map.getCountryFoodPotential(i));
        const double nonFoodPot = std::max(0.0, map.getCountryNonFoodPotential(i));

        const double urban = (pop > 1.0) ? clamp01(c.getTotalCityPopulation() / pop) : 0.0;
        const double control = clamp01(c.getAvgControl());
        const double logi = clamp01(c.getLogisticsReach());
        const double admin = clamp01(c.getAdminCapacity());

        // Market access: logistics + admin + ports, reduced by low control and war risk.
        const double portTerm = std::min(1.0, std::sqrt(static_cast<double>(c.getPorts().size()) / 8.0));
        double marketAccess = clamp01(0.10 + 0.55 * logi + 0.15 * admin + 0.20 * portTerm);
        marketAccess *= (0.35 + 0.65 * control);
        if (c.isAtWar()) {
            marketAccess *= 0.70;
        }
        m.marketAccess = marketAccess;

        if (!m.initialized) {
            m.initialized = true;
            m.foodStock = foodPot * 0.10;
            m.nonFoodStock = nonFoodPot * 0.05;
            m.capitalStock = 20.0;
            m.infraStock = 10.0 * (0.25 + 0.75 * marketAccess);
            m.foodSecurity = 1.0;
        }

        const double controlMult = 0.60 + 0.40 * control;
        const double laborPerFood = 1800.0;
        const double laborMult = (foodPot > 1e-6) ? std::min(1.0, pop / (foodPot * laborPerFood)) : 0.0;

        const bool hasIrrigation = TechnologyManager::hasTech(tech, c, 10);
        const bool hasAgriculture = TechnologyManager::hasTech(tech, c, 20);
        const double yieldMult = 0.040 * (1.0 + (hasIrrigation ? 0.18 : 0.0) + (hasAgriculture ? 0.28 : 0.0));

        const double climateMult = std::max(0.05, static_cast<double>(map.getCountryClimateFoodMultiplier(i)));
        const double foodOutAnnual = foodPot * yieldMult * laborMult * controlMult * (0.80 + 0.20 * marketAccess) * climateMult;

        const bool hasMining = TechnologyManager::hasTech(tech, c, 4);
        const bool hasMetallurgy = TechnologyManager::hasTech(tech, c, TechId::METALLURGY);
        const bool hasIndustrial = TechnologyManager::hasTech(tech, c, 52);
        const double extractionMult = 0.0018 * (1.0 + (hasMining ? 0.30 : 0.0) + (hasMetallurgy ? 0.45 : 0.0) + (hasIndustrial ? 0.80 : 0.0));

        const double urbanBoost = (pop > 1.0)
            ? (0.000060 * std::sqrt(std::max(1.0, c.getTotalCityPopulation())) * std::sqrt(std::max(1.0, m.capitalStock)) * (0.60 + 0.40 * marketAccess))
            : 0.0;

        const double nonFoodOutAnnual = nonFoodPot * extractionMult * controlMult + urbanBoost;

        const double foodConsAnnual = pop * m_cfg.foodPerCapita;
        const double nonFoodConsAnnual = pop * m_cfg.nonFoodPerCapita * (0.30 + 0.70 * urban) * (0.50 + 0.50 * marketAccess);

        const double foodOutTotal = foodOutAnnual * yearsD;
        const double nonFoodOutTotal = nonFoodOutAnnual * yearsD;
        const double foodConsTotal = foodConsAnnual * yearsD;
        const double nonFoodConsTotal = nonFoodConsAnnual * yearsD;

        // Depreciation/investment planning uses non-food availability after consumption.
        const double depTotal = m_cfg.depRate * std::max(0.0, m.capitalStock) * yearsD;
        const double nonFoodPre0 = m.nonFoodStock + nonFoodOutTotal - nonFoodConsTotal;
        const double investTotal = m_cfg.investRate * std::max(0.0, nonFoodPre0) * (0.30 + 0.70 * marketAccess) * (0.65 + 0.35 * clamp01(c.getStability()));

        // Apply capital dynamics (investment converts non-food into capital).
        m.lastInvestment = investTotal / yearsD;
        m.lastDepreciation = depTotal / yearsD;
        m.capitalStock = std::max(0.0, m.capitalStock - depTotal + investTotal * m_cfg.investEfficiency);

	        // Record annualized outputs/consumption.
	        m.lastFoodOutput = foodOutAnnual;
	        m.lastNonFoodOutput = nonFoodOutAnnual;
	        m.lastFoodCons = foodConsAnnual;
	        m.lastNonFoodCons = nonFoodConsAnnual;

	        // Phase 4/5 integration: government extraction is tied to real output and market access.
	        // Taxable base is value-added proxy: non-food + a fraction of food, scaled by market access.
	        const double taxableBaseAnnual = std::max(0.0, (nonFoodOutAnnual + 0.30 * foodOutAnnual) * marketAccess);
	        const double taxRate = std::clamp(c.getTaxRate(), 0.02, 0.45);
	        const double fiscal = std::clamp(c.getFiscalCapacity(), 0.05, 1.0);
	        double taxTakeAnnual = taxableBaseAnnual * taxRate * fiscal;
	        taxTakeAnnual *= (0.65 + 0.35 * clamp01(c.getStability()));
	        taxTakeAnnual *= (0.55 + 0.45 * control);
	        const int techCount = static_cast<int>(tech.getUnlockedTechnologies(c).size());
	        const bool plagueAffected = map.isPlagueActive() && map.isCountryAffectedByPlague(i);
	        c.applyBudgetFromEconomy(taxableBaseAnnual, taxTakeAnnual, years, techCount, plagueAffected);

	        foodCons[static_cast<size_t>(i)] = foodConsTotal;
	        nonFoodCons[static_cast<size_t>(i)] = nonFoodConsTotal;

        foodPre[static_cast<size_t>(i)] = m.foodStock + foodOutTotal - foodConsTotal;
        nonFoodPre[static_cast<size_t>(i)] = nonFoodPre0 - investTotal;

        // Reset yearly trade stats (annualized later).
        m.importsValue = 0.0;
        m.exportsValue = 0.0;
        m.lastFoodShortage = 0.0;
        m.lastNonFoodShortage = 0.0;
    }

    // Build trade edges (land adjacency + shipping routes).
    struct Edge {
        int a = -1;
        int b = -1;
        double capFood = 0.0;
        double capNonFood = 0.0;
        double costPerUnit = 0.0;
    };

    std::vector<Edge> edges;
    edges.reserve(static_cast<size_t>(n) * 6u);
    std::unordered_map<std::uint64_t, int> edgeIndex;
    edgeIndex.reserve(static_cast<size_t>(n) * 6u);

    auto addEdge = [&](int a, int b, double cap, double cost) {
        if (a < 0 || b < 0 || a >= n || b >= n || a == b) return;
        if (isEnemy(a, b) || isEnemy(b, a)) return;
        const std::uint64_t key = pairKey(a, b);
        auto it = edgeIndex.find(key);
        if (it != edgeIndex.end()) {
            Edge& e = edges[static_cast<size_t>(it->second)];
            e.capFood += cap;
            e.capNonFood += cap;
            e.costPerUnit = std::min(e.costPerUnit, cost);
            return;
        }
        Edge e;
        e.a = a;
        e.b = b;
        e.capFood = cap;
        e.capNonFood = cap;
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
            double cap = m_cfg.baseLandCapacity * (0.25 + 0.75 * logi) * (0.30 + 0.70 * control) * std::min(4.0, 0.35 + contactScale * 0.08);
            double cost = m_cfg.baseLandCost * (1.0 / std::max(1.0, contactScale)) * (1.20 - 0.70 * logi) * (1.10 - 0.60 * control);

            if (ca.isAtWar() || cb.isAtWar()) {
                cap *= 0.65;
                cost *= 1.20;
            }
            cap *= yearsD;
            addEdge(a, b, cap, cost);
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
        double cap = m_cfg.baseSeaCapacity * (0.35 + 0.65 * logi);
        cap *= (0.75 + 0.25 * std::min(1.0, (static_cast<double>(ca.getPorts().size() + cb.getPorts().size()) / 6.0)));

        // Chokepoints/blockade proxy: hostile sea influence along the nav path reduces capacity.
        double blockadeMult = 1.0;
        if (!seaOwner.empty() && !r.navPath.empty()) {
            int hostileHits = 0;
            for (const auto& node : r.navPath) {
                if (node.x < 0 || node.y < 0 || node.x >= navW || node.y >= navH) continue;
                const size_t idx = static_cast<size_t>(node.y) * static_cast<size_t>(navW) + static_cast<size_t>(node.x);
                const int owner = seaOwner[idx];
                if (owner < 0 || owner == a || owner == b) continue;
                if (isEnemy(a, owner) || isEnemy(b, owner) || isEnemy(owner, a) || isEnemy(owner, b)) {
                    hostileHits++;
                }
            }
            if (hostileHits > 0) {
                blockadeMult = 0.10;
                if (hostileHits > static_cast<int>(r.navPath.size() / 20u)) {
                    blockadeMult = 0.02;
                }
            }
        }
        cap *= blockadeMult;

        double cost = m_cfg.seaCostPerLen * static_cast<double>(std::max(0.0f, r.totalLen)) * (1.10 - 0.40 * logi);
        if (ca.isAtWar() || cb.isAtWar()) {
            cap *= 0.60;
            cost *= 1.35;
        }

        cap *= yearsD;
        addEdge(a, b, cap, cost);
    }

    // Per-country edge lists for 1-hop matching.
    std::vector<std::vector<int>> edgesByCountry(static_cast<size_t>(n));
    edgesByCountry.shrink_to_fit();
    edgesByCountry.assign(static_cast<size_t>(n), {});
    for (int ei = 0; ei < static_cast<int>(edges.size()); ++ei) {
        const Edge& e = edges[static_cast<size_t>(ei)];
        edgesByCountry[static_cast<size_t>(e.a)].push_back(ei);
        edgesByCountry[static_cast<size_t>(e.b)].push_back(ei);
    }

    auto tradeGood = [&](bool isFood) {
        const double basePrice = isFood ? 1.0 : 3.0;
        const double alpha = isFood ? 2.0 : 1.6;

        std::vector<double> supply(static_cast<size_t>(n), 0.0);
        std::vector<double> demand(static_cast<size_t>(n), 0.0);
        std::vector<double> cons = isFood ? foodCons : nonFoodCons;
        std::vector<double> pre = isFood ? foodPre : nonFoodPre;
        std::vector<double> net(static_cast<size_t>(n), 0.0);
        std::vector<double> price(static_cast<size_t>(n), basePrice);

        for (int i = 0; i < n; ++i) {
            const double p = pre[static_cast<size_t>(i)];
            if (p >= 0.0) supply[static_cast<size_t>(i)] = p;
            else demand[static_cast<size_t>(i)] = -p;
            const double scarcity = (cons[static_cast<size_t>(i)] > 1e-9) ? (demand[static_cast<size_t>(i)] / (cons[static_cast<size_t>(i)] + 1e-9)) : 0.0;
            price[static_cast<size_t>(i)] = basePrice * std::exp(alpha * std::min(2.5, scarcity));
        }

        std::vector<int> buyers;
        buyers.reserve(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            if (demand[static_cast<size_t>(i)] > 1e-9 && countries[static_cast<size_t>(i)].getPopulation() > 0) {
                buyers.push_back(i);
            }
        }
        std::sort(buyers.begin(), buyers.end(), [&](int a, int b) {
            const double pa = price[static_cast<size_t>(a)];
            const double pb = price[static_cast<size_t>(b)];
            if (pa != pb) return pa > pb;
            return a < b;
        });

        for (int buyer : buyers) {
            double need = demand[static_cast<size_t>(buyer)];
            if (need <= 1e-9) continue;

            struct Cand { int supplier; int edgeIdx; double delivered; };
            std::vector<Cand> cands;
            cands.reserve(24);

            for (int ei : edgesByCountry[static_cast<size_t>(buyer)]) {
                const Edge& e = edges[static_cast<size_t>(ei)];
                const int supplier = (e.a == buyer) ? e.b : e.a;
                if (supplier < 0 || supplier >= n) continue;
                if (supply[static_cast<size_t>(supplier)] <= 1e-9) continue;
                const double capRem = isFood ? e.capFood : e.capNonFood;
                if (capRem <= 1e-9) continue;
                const double delivered = price[static_cast<size_t>(supplier)] + e.costPerUnit;
                cands.push_back({supplier, ei, delivered});
            }

            std::sort(cands.begin(), cands.end(), [&](const Cand& a, const Cand& b) {
                if (a.delivered != b.delivered) return a.delivered < b.delivered;
                if (a.supplier != b.supplier) return a.supplier < b.supplier;
                return a.edgeIdx < b.edgeIdx;
            });

            const double local = price[static_cast<size_t>(buyer)];
            for (const auto& cand : cands) {
                if (need <= 1e-9) break;
                if (cand.delivered >= local * 0.995) {
                    break;
                }

                Edge& e = edges[static_cast<size_t>(cand.edgeIdx)];
                double& capRem = isFood ? e.capFood : e.capNonFood;
                const double amt = std::min({need, supply[static_cast<size_t>(cand.supplier)], capRem});
                if (amt <= 1e-12) continue;

                supply[static_cast<size_t>(cand.supplier)] -= amt;
                need -= amt;
                capRem -= amt;

                net[static_cast<size_t>(buyer)] += amt;
                net[static_cast<size_t>(cand.supplier)] -= amt;

                const double value = amt * cand.delivered;
                Country::MacroEconomyState& mb = countries[static_cast<size_t>(buyer)].getMacroEconomyMutable();
                Country::MacroEconomyState& ms = countries[static_cast<size_t>(cand.supplier)].getMacroEconomyMutable();
                mb.importsValue += value;
                ms.exportsValue += value;

                const double popBuyer = static_cast<double>(std::max<long long>(1, countries[static_cast<size_t>(buyer)].getPopulation()));
                const float inc = static_cast<float>(std::min(1.0, value / (10000.0 + 0.50 * popBuyer)));
                const size_t ti = static_cast<size_t>(cand.supplier) * static_cast<size_t>(n) + static_cast<size_t>(buyer);
                if (ti < m_tradeIntensity.size()) {
                    m_tradeIntensity[ti] = std::min(1.0f, m_tradeIntensity[ti] + inc);
                }
            }
            demand[static_cast<size_t>(buyer)] = need;
        }

        for (int i = 0; i < n; ++i) {
            Country::MacroEconomyState& m = countries[static_cast<size_t>(i)].getMacroEconomyMutable();
            if (isFood) {
                m.foodStock = pre[static_cast<size_t>(i)] + net[static_cast<size_t>(i)];
            } else {
                m.nonFoodStock = pre[static_cast<size_t>(i)] + net[static_cast<size_t>(i)];
            }
        }
    };

    tradeGood(true);
    tradeGood(false);

    // Finalize shortages, security, and derive GDP/wealth/exports.
    for (int i = 0; i < n; ++i) {
        Country& c = countries[static_cast<size_t>(i)];
        Country::MacroEconomyState& m = c.getMacroEconomyMutable();

        const double foodConsTotal = foodCons[static_cast<size_t>(i)];
        if (m.foodStock < 0.0) {
            const double shortage = -m.foodStock;
            m.lastFoodShortage = shortage / yearsD;
            m.foodStock = 0.0;
            const double ratio = (foodConsTotal > 1e-9) ? std::min(1.0, shortage / (foodConsTotal + 1e-9)) : 1.0;
            m.foodSecurity = clamp01(1.0 - ratio);
            c.setStability(c.getStability() - 0.10 * (1.0 - m.foodSecurity));
            c.setLegitimacy(c.getLegitimacy() - 0.08 * (1.0 - m.foodSecurity));
        } else {
            m.foodSecurity = std::min(1.0, m.foodSecurity + 0.05);
        }

        const double nonFoodConsTotal = nonFoodCons[static_cast<size_t>(i)];
        if (m.nonFoodStock < 0.0) {
            const double shortage = -m.nonFoodStock;
            m.lastNonFoodShortage = shortage / yearsD;
            m.nonFoodStock = 0.0;
            (void)nonFoodConsTotal;
        }

        // Annualize trade value stats.
        m.importsValue /= yearsD;
        m.exportsValue /= yearsD;

        // Simple macro-derived headline stats (used by UI).
        const double gdpAnnual = (m.lastFoodOutput * 1.0 + m.lastNonFoodOutput * 2.0 + m.lastInvestment * 3.0);
        const double wealth = m.foodStock * 1.0 + m.nonFoodStock * 2.0 + m.capitalStock * 5.0 + std::max(0.0, c.getGold());
        c.setGDP(gdpAnnual);
        c.setWealth(wealth);
        c.setExports(m.exportsValue);
    }
}
