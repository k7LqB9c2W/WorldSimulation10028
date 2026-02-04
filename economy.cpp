// economy.cpp

#include "economy.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <limits>

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

    m_countryWealth.assign(static_cast<size_t>(m_maxCountries + 1), 0.0);
    m_countryGDP.assign(static_cast<size_t>(m_maxCountries + 1), 0.0);
    m_countryExports.assign(static_cast<size_t>(m_maxCountries + 1), 0.0);
    m_countryInvestRate.assign(static_cast<size_t>(m_maxCountries + 1), 0.0f);

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
    if (!m_initialized) {
        return;
    }
    const size_t paletteSize = static_cast<size_t>(m_maxCountries + 1);
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
