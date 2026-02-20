#include "settlement_system.h"

#include "country.h"
#include "map.h"
#include "trade.h"

#include <SFML/Graphics.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <queue>
#include <sstream>
#include <unordered_map>
#include <tuple>

namespace {

constexpr double kEps = 1.0e-9;

std::uint64_t mixHash(std::uint64_t h, std::uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    return h;
}

std::uint64_t hashDouble(double v, double scale) {
    if (!std::isfinite(v)) {
        return 0xFFFFFFFFFFFFFFFFull;
    }
    const double q = std::round(v * scale) / scale;
    return static_cast<std::uint64_t>(static_cast<std::int64_t>(q * scale));
}

bool isAtWarPair(const Country& a, const Country& b) {
    const auto& enemies = a.getEnemies();
    for (const Country* e : enemies) {
        if (e && e->getCountryIndex() == b.getCountryIndex()) {
            return true;
        }
    }
    return false;
}

std::string trimAscii(std::string s) {
    auto isWs = [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
    };
    while (!s.empty() && isWs(static_cast<unsigned char>(s.front()))) {
        s.erase(s.begin());
    }
    while (!s.empty() && isWs(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }
    return s;
}

std::vector<std::string> splitCsvLine(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    bool inQuotes = false;
    for (char ch : line) {
        if (ch == '"') {
            inQuotes = !inQuotes;
            continue;
        }
        if (ch == ',' && !inQuotes) {
            out.push_back(trimAscii(cur));
            cur.clear();
            continue;
        }
        cur.push_back(ch);
    }
    out.push_back(trimAscii(cur));
    return out;
}

double syntheticPaleoTempOffsetC(int year) {
    if (year <= -20000) return -6.0;
    if (year <= -15000) {
        const double t = static_cast<double>(year + 20000) / 5000.0;
        return -6.0 + 2.0 * t;
    }
    if (year <= -12000) {
        const double t = static_cast<double>(year + 15000) / 3000.0;
        return -4.0 + 2.4 * t;
    }
    if (year <= -8000) {
        const double t = static_cast<double>(year + 12000) / 4000.0;
        return -1.6 + 1.3 * t;
    }
    if (year <= -5000) {
        const double t = static_cast<double>(year + 8000) / 3000.0;
        return -0.3 + 0.3 * t;
    }
    return 0.0;
}

double syntheticPaleoPrecipOffset01(int year) {
    if (year <= -20000) return -0.08;
    if (year <= -15000) {
        const double t = static_cast<double>(year + 20000) / 5000.0;
        return -0.08 + 0.02 * t;
    }
    if (year <= -12000) {
        const double t = static_cast<double>(year + 15000) / 3000.0;
        return -0.06 + 0.03 * t;
    }
    if (year <= -8000) {
        const double t = static_cast<double>(year + 12000) / 4000.0;
        return -0.03 + 0.02 * t;
    }
    if (year <= -5000) {
        const double t = static_cast<double>(year + 8000) / 3000.0;
        return -0.01 + 0.01 * t;
    }
    return 0.0;
}

constexpr int kRegimeNormal = 0;
constexpr int kRegimeDrought = 1;
constexpr int kRegimePluvial = 2;
constexpr int kRegimeCold = 3;

const char* kClimateFertilityShader = R"(
uniform sampler2D stateTex;   // r=fertility(0..1), g=regime(0..1), b=intensity(0..1)
uniform sampler2D climateTex; // r=precip(0..1), g=tempNorm(0..1)
uniform float yearNorm;
uniform float seedNorm;
uniform float regenBase;
uniform float depleteBase;

float rand1(vec2 uv) {
    return fract(sin(dot(uv + vec2(yearNorm, seedNorm), vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    vec2 uv = gl_TexCoord[0].xy;
    vec4 st = texture2D(stateTex, uv);
    vec4 cl = texture2D(climateTex, uv);

    float fert = clamp(st.r, 0.0, 1.0);
    float regimeN = clamp(st.g, 0.0, 1.0);
    float intensity = clamp(st.b, 0.0, 1.0);
    float precip = clamp(cl.r, 0.0, 1.0);
    float tempN = clamp(cl.g, 0.0, 1.0);

    // Decode regime from normalized storage.
    float regime = floor(regimeN * 3.99 + 0.5);
    float h = rand1(uv);

    float hot = clamp((tempN - 0.55) / 0.45, 0.0, 1.0);
    float cold = clamp((0.45 - tempN) / 0.45, 0.0, 1.0);

    float nextRegime = regime;
    if (regime < 0.5) { // normal
        float pD = clamp(0.03 + 0.06 * (1.0 - precip) + 0.03 * hot, 0.0, 0.40);
        float pP = clamp(0.02 + 0.05 * precip, 0.0, 0.30);
        float pC = clamp(0.01 + 0.03 * cold, 0.0, 0.20);
        if (h < pD) nextRegime = 1.0;
        else if (h < pD + pP) nextRegime = 2.0;
        else if (h < pD + pP + pC) nextRegime = 3.0;
        else nextRegime = 0.0;
    } else if (abs(regime - 1.0) < 0.25) { // drought
        if (h < 0.68) nextRegime = 1.0;
        else if (h < 0.82) nextRegime = 0.0;
        else if (h < 0.94) nextRegime = 2.0;
        else nextRegime = 3.0;
    } else if (abs(regime - 2.0) < 0.25) { // pluvial
        if (h < 0.63) nextRegime = 2.0;
        else if (h < 0.87) nextRegime = 0.0;
        else if (h < 0.95) nextRegime = 1.0;
        else nextRegime = 3.0;
    } else { // cold
        if (h < 0.62) nextRegime = 3.0;
        else if (h < 0.88) nextRegime = 0.0;
        else if (h < 0.95) nextRegime = 1.0;
        else nextRegime = 2.0;
    }

    float regimeMulRegen = 1.0;
    float regimeMulDep = 1.0;
    if (abs(nextRegime - 1.0) < 0.25) { regimeMulRegen = 0.62; regimeMulDep = 1.40; } // drought
    if (abs(nextRegime - 2.0) < 0.25) { regimeMulRegen = 1.24; regimeMulDep = 0.84; } // pluvial
    if (abs(nextRegime - 3.0) < 0.25) { regimeMulRegen = 0.80; regimeMulDep = 1.15; } // cold

    float regen = regenBase * (1.0 - intensity) * regimeMulRegen;
    float deplete = depleteBase * intensity * regimeMulDep;
    fert = clamp(fert + regen - deplete, 0.05, 1.0);

    float outRegimeN = clamp(nextRegime / 3.0, 0.0, 1.0);
    gl_FragColor = vec4(fert, outRegimeN, intensity, 1.0);
}
)";

const char* kSettlementDiseaseShader = R"(
uniform sampler2D stateTex; // r=S, g=I, b=R
uniform sampler2D paramTex; // r=betaEffNorm, g=inflowNorm, b=gammaNorm, a=dt
uniform float betaScale;
uniform float inflowScale;
uniform float gammaScale;

void main() {
    vec2 uv = gl_TexCoord[0].xy;
    vec4 st = texture2D(stateTex, uv);
    vec4 pm = texture2D(paramTex, uv);

    float S = clamp(st.r, 0.0, 1.0);
    float I = clamp(st.g, 0.0, 1.0);
    float R = clamp(st.b, 0.0, 1.0);

    float betaEff = max(0.0, pm.r * betaScale);
    float inflowI = max(0.0, pm.g * inflowScale);
    float gammaV = max(0.0, pm.b * gammaScale);
    float dt = clamp(pm.a, 0.0, 1.0);

    float infPressure = clamp(I + inflowI, 0.0, 1.0);
    float newInf = min(S, betaEff * S * infPressure * dt);
    float newRec = min(I + newInf, gammaV * I * dt);

    S = max(0.0, S - newInf);
    I = max(0.0, I + newInf - newRec);
    R = max(0.0, R + newRec);

    float sumSir = S + I + R;
    if (sumSir > 1.0e-6) {
        S /= sumSir;
        I /= sumSir;
        R /= sumSir;
    } else {
        S = 1.0;
        I = 0.0;
        R = 0.0;
    }

    gl_FragColor = vec4(S, I, R, 1.0);
}
)";

const char* kSettlementAdoptJoinShader = R"(
uniform sampler2D aTex; // r=neighborAdopt, g=suitability, b=eliteCap, a=risk
uniform sampler2D bTex; // r=security, g=tradeGain, b=publicGoods, a=taxBurden
uniform sampler2D cTex; // r=oppression, g=stayValue
uniform float theta0;
uniform float theta1;
uniform float theta2;
uniform float theta3;
uniform float theta4;

float sigmoid(float x) {
    if (x >= 20.0) return 1.0;
    if (x <= -20.0) return 0.0;
    return 1.0 / (1.0 + exp(-x));
}

void main() {
    vec2 uv = gl_TexCoord[0].xy;
    vec4 a = texture2D(aTex, uv);
    vec4 b = texture2D(bTex, uv);
    vec4 c = texture2D(cTex, uv);

    float neigh = clamp(a.r, 0.0, 1.0);
    float suit = clamp(a.g, 0.0, 1.0);
    float elite = clamp(a.b, 0.0, 1.0);
    float risk = clamp(a.a, 0.0, 1.0);

    float sec = clamp(b.r, 0.0, 1.0);
    float trade = clamp(b.g, 0.0, 1.0);
    float pub = clamp(b.b, 0.0, 1.0);
    float tax = clamp(b.a, 0.0, 1.0);
    float opp = clamp(c.r, 0.0, 1.0);
    float stay = clamp(c.g, 0.0, 1.0);

    float z = theta0 + theta1 * neigh + theta2 * suit + theta3 * elite - theta4 * risk;
    float pAdopt = sigmoid(z);

    float uJoin = sec + trade + pub - tax - opp - 0.5 * risk;
    float uDiff = clamp(uJoin - stay, -1.0, 1.0);
    float uNorm = 0.5 + 0.5 * uDiff;

    gl_FragColor = vec4(pAdopt, uNorm, 0.0, 1.0);
}
)";

const char* kSettlementEdgeKernelShader = R"(
uniform sampler2D edgeTex; // r=costNorm, g=capNorm, b=rel, a=warFlag
uniform sampler2D nodeTex; // r=popA, g=popB, b=specA, a=specB
uniform float attritionRate;
uniform float gravityKappa;
uniform float gravityAlpha;
uniform float gravityBeta;
uniform float gravityGamma;
uniform float migrationM0;
uniform float migrationDistDecay;
uniform float gravityScale;
uniform float migrationScale;

void main() {
    vec2 uv = gl_TexCoord[0].xy;
    vec4 e = texture2D(edgeTex, uv);
    vec4 n = texture2D(nodeTex, uv);

    float cost = max(0.01, e.r);
    float cap = max(0.0, e.g);
    float rel = clamp(e.b, 0.0, 1.0);
    float war = clamp(e.a, 0.0, 1.0);

    float popA = max(0.0, n.r);
    float popB = max(0.0, n.g);
    float specA = clamp(n.b, 0.0, 1.0);
    float specB = clamp(n.a, 0.0, 1.0);

    float demand = 0.20 * sqrt(max(0.0, popA * popB)) * (1.0 + 0.60 * war);
    float supply = cap * rel;
    float deficit = max(0.0, demand - supply);
    float attenuation = exp(-attritionRate * deficit);

    float Sa = max(0.01, popA * max(0.02, specA));
    float Sb = max(0.01, popB * max(0.02, specB));
    float gravity = gravityKappa * pow(Sa, gravityAlpha) * pow(Sb, gravityBeta) / pow(cost, max(0.01, gravityGamma));
    float migrationV = migrationM0 * exp(-migrationDistDecay * cost) * rel * attenuation;

    gl_FragColor = vec4(
        clamp(attenuation, 0.0, 1.0),
        clamp(gravity / max(1.0e-6, gravityScale), 0.0, 1.0),
        clamp(migrationV / max(1.0e-6, migrationScale), 0.0, 1.0),
        1.0);
}
)";

bool runShaderPass1D(const sf::Shader& shader,
                     const sf::Texture& sourceTex,
                     int width,
                     std::vector<sf::Uint8>& outRgba) {
    if (width <= 0) return false;
    sf::RenderTexture rt;
    if (!rt.create(static_cast<unsigned>(width), 1u)) {
        return false;
    }
    rt.setSmooth(false);
    rt.clear(sf::Color::Black);
    sf::Sprite sprite(sourceTex);
    sf::RenderStates states;
    states.shader = &shader;
    states.blendMode = sf::BlendNone;
    rt.draw(sprite, states);
    rt.display();

    const sf::Image img = rt.getTexture().copyToImage();
    if (img.getSize().x != static_cast<unsigned>(width) || img.getSize().y != 1u) {
        return false;
    }
    const sf::Uint8* px = img.getPixelsPtr();
    if (px == nullptr) return false;
    outRgba.assign(px, px + static_cast<size_t>(width) * 4u);
    return true;
}

bool runShaderPass2D(const sf::Shader& shader,
                     const sf::Texture& sourceTex,
                     int width,
                     int height,
                     std::vector<sf::Uint8>& outRgba) {
    if (width <= 0 || height <= 0) return false;
    sf::RenderTexture rt;
    if (!rt.create(static_cast<unsigned>(width), static_cast<unsigned>(height))) {
        return false;
    }
    rt.setSmooth(false);
    rt.clear(sf::Color::Black);
    sf::Sprite sprite(sourceTex);
    sf::RenderStates states;
    states.shader = &shader;
    states.blendMode = sf::BlendNone;
    rt.draw(sprite, states);
    rt.display();

    const sf::Image img = rt.getTexture().copyToImage();
    if (img.getSize().x != static_cast<unsigned>(width) || img.getSize().y != static_cast<unsigned>(height)) {
        return false;
    }
    const sf::Uint8* px = img.getPixelsPtr();
    if (px == nullptr) return false;
    outRgba.assign(px, px + static_cast<size_t>(width * height) * 4u);
    return true;
}

} // namespace

struct SettlementSystem::SettlementGpuRuntime {
    bool initAttempted = false;
    bool available = false;
    bool enabled = false;
    bool climateKernel = false;
    bool diseaseKernel = false;
    bool adoptionJoinKernel = false;
    bool edgeKernel = false;

    sf::Shader climateShader;
    sf::Shader diseaseShader;
    sf::Shader adoptionJoinShader;
    sf::Shader edgeShader;

    void initialize(const SimulationConfig::ResearchGpu& cfg) {
        if (initAttempted) {
            return;
        }
        initAttempted = true;
        enabled = cfg.enabled;
        if (!enabled) {
            return;
        }
        if (!sf::Shader::isAvailable()) {
            return;
        }
        available = true;
        if (cfg.climateFertility) {
            climateKernel = climateShader.loadFromMemory(kClimateFertilityShader, sf::Shader::Fragment);
        }
        if (cfg.settlementDisease) {
            diseaseKernel = diseaseShader.loadFromMemory(kSettlementDiseaseShader, sf::Shader::Fragment);
        }
        if (cfg.adoptionKernel || cfg.joinStayUtility) {
            adoptionJoinKernel = adoptionJoinShader.loadFromMemory(kSettlementAdoptJoinShader, sf::Shader::Fragment);
        }
        if (cfg.accelerateTransport || cfg.accelerateFlows || cfg.warfareLogistics) {
            edgeKernel = edgeShader.loadFromMemory(kSettlementEdgeKernelShader, sf::Shader::Fragment);
        }
    }
};

SettlementSystem::SettlementSystem(SimulationContext& ctx)
    : m_ctx(&ctx), m_gpu(std::make_unique<SettlementGpuRuntime>()) {}

SettlementSystem::~SettlementSystem() = default;

bool SettlementSystem::enabled() const {
    return (m_ctx != nullptr) && m_ctx->config.settlements.enabled;
}

float SettlementSystem::getCountryTradeHintBlend() const {
    if (m_ctx == nullptr) {
        return 0.0f;
    }
    return static_cast<float>(std::clamp(m_ctx->config.transport.tradeHintBlend, 0.0, 1.0));
}

double SettlementSystem::clamp01(double v) {
    return std::max(0.0, std::min(1.0, v));
}

double SettlementSystem::sigmoid(double x) {
    if (x >= 20.0) return 1.0;
    if (x <= -20.0) return 0.0;
    return 1.0 / (1.0 + std::exp(-x));
}

double SettlementSystem::finiteOr(double v, double fallback) {
    return std::isfinite(v) ? v : fallback;
}

void SettlementSystem::tickYear(int year,
                                Map& map,
                                std::vector<Country>& countries,
                                const TradeManager& tradeManager) {
    (void)tradeManager;
    if (!enabled()) {
        m_countryTradeHintMatrix.assign(static_cast<size_t>(countries.size()) * static_cast<size_t>(countries.size()), 0.0f);
        m_lastDeterminismHash = 0;
        return;
    }
    if (year <= m_lastTickYear) {
        return;
    }
    m_lastTickYear = year;

    if (m_gpu) {
        m_gpu->initialize(m_ctx->config.researchGpu);
    }

    ensureInitialized(year, map, countries);
    if (m_nodes.empty()) {
        m_countryTradeHintMatrix.assign(static_cast<size_t>(countries.size()) * static_cast<size_t>(countries.size()), 0.0f);
        m_lastDeterminismHash = 0;
        return;
    }

    if (!m_gpuStartupLogged && m_ctx->config.researchGpu.startupDiagnostics) {
        m_gpuStartupLogged = true;
        const bool req = m_ctx->config.researchGpu.enabled;
        const bool avail = (m_gpu && m_gpu->available);
        const bool c = (m_gpu && m_gpu->climateKernel);
        const bool d = (m_gpu && m_gpu->diseaseKernel);
        const bool a = (m_gpu && m_gpu->adoptionJoinKernel);
        const bool e = (m_gpu && m_gpu->edgeKernel);
        std::cout << "[ResearchGPU] requested=" << (req ? 1 : 0)
                  << " available=" << (avail ? 1 : 0)
                  << " climate=" << (c ? 1 : 0)
                  << " disease=" << (d ? 1 : 0)
                  << " adoptJoin=" << (a ? 1 : 0)
                  << " edge=" << (e ? 1 : 0)
                  << std::endl;
    }

    syncNodeTotalsToCountryPopulation(countries);
    updateSubsistenceMixAndPackages(year, map, countries);
    updateClimateRegimesAndFertility(year, map, countries);
    updatePastoralMobilityRoutes(year, map, countries);
    recomputeFoodCaloriesAndCapacity(map, countries);
    updateHouseholdsElitesExtraction(year, countries);
    rebuildTransportGraph(year, map, countries);
    updateKnowledgeAndExploration(year, countries);
    computeFlowsAndMigration(map, countries);
    updateCampaignLogisticsAndAttrition(year, countries);
    updateSettlementDisease(year, map, countries);
    applyGrowthAndSpecialization(year, countries);
    applyFission(year, map, countries);
    updateAdoptionAndJoinUtility(year, countries);
    applyPolityChoiceAssignment(year, countries);
    aggregateToCountries(countries);
    buildCountryTradeHintMatrix(static_cast<int>(countries.size()));
    rebuildOverlays();

    if (!m_startupLogged) {
        m_startupLogged = true;
        std::array<double, static_cast<size_t>(SubsistenceMode::Count)> meanMix{0.0, 0.0, 0.0, 0.0, 0.0};
        int packageCount = 0;
        double meanI = 0.0;
        double meanFert = 0.0;
        double meanSalinity = 0.0;
        double meanKnow = 0.0;
        int boostedEdges = 0;
        for (const SettlementNode& n : m_nodes) {
            for (size_t k = 0; k < meanMix.size(); ++k) {
                meanMix[k] += n.mix[k];
            }
            packageCount += static_cast<int>(n.adoptedPackages.size());
        }
        for (double iVal : m_nodeI) {
            meanI += iVal;
        }
        for (float fVal : m_fieldFertility) {
            meanFert += static_cast<double>(fVal);
        }
        for (float sVal : m_fieldSalinity) {
            meanSalinity += static_cast<double>(sVal);
        }
        for (double v : m_nodeKnowledgeCoverage) {
            meanKnow += v;
        }
        for (double v : m_edgeExplorationBoost) {
            if (v > 1.0e-9) ++boostedEdges;
        }
        const double invN = 1.0 / std::max(1.0, static_cast<double>(m_nodes.size()));
        for (double& v : meanMix) {
            v *= invN;
        }
        const double invNode = 1.0 / std::max(1.0, static_cast<double>(m_nodeI.size()));
        const double invField = 1.0 / std::max(1.0, static_cast<double>(m_fieldFertility.size()));
        const double invSal = 1.0 / std::max(1.0, static_cast<double>(m_fieldSalinity.size()));
        const double invKnow = 1.0 / std::max(1.0, static_cast<double>(m_nodeKnowledgeCoverage.size()));
        std::cout << "[Settlements] startup nodes=" << m_nodes.size()
                  << " edges=" << m_edges.size()
                  << " avgPackages=" << (static_cast<double>(packageCount) * invN)
                  << " avgInfected=" << (meanI * invNode)
                  << " avgFertility=" << (meanFert * invField)
                  << " avgSalinity=" << (meanSalinity * invSal)
                  << " avgKnowledge=" << (meanKnow * invKnow)
                  << " boostedEdges=" << boostedEdges
                  << " mix(f,farm,past,fish,craft)="
                  << std::fixed << std::setprecision(3)
                  << meanMix[0] << "," << meanMix[1] << "," << meanMix[2] << "," << meanMix[3] << "," << meanMix[4]
                  << std::endl;
        std::cout << "[ResearchSettlement] pastoral=" << (m_ctx->config.researchSettlement.pastoralMobility ? 1 : 0)
                  << " extraction=" << (m_ctx->config.researchSettlement.householdsExtraction ? 1 : 0)
                  << " polityChoice=" << (m_ctx->config.researchSettlement.polityChoiceAssignment ? 1 : 0)
                  << " controlGraph=" << (m_ctx->config.researchSettlement.polityControlGraph ? 1 : 0)
                  << " campaignLogistics=" << (m_ctx->config.researchSettlement.campaignLogistics ? 1 : 0)
                  << " irrigation=" << (m_ctx->config.researchSettlement.irrigationLoop ? 1 : 0)
                  << " pathRebuild=" << (m_ctx->config.researchSettlement.transportPathRebuild ? 1 : 0)
                  << std::endl;
        std::cout << "[DensityInit] enabled=" << (m_ctx->config.densityInit.enabled ? 1 : 0)
                  << " priorLoaded=" << (m_densityPriorLoaded ? 1 : 0)
                  << " priorWeight=" << m_ctx->config.densityInit.priorWeight
                  << std::endl;
        std::cout << "[PaleoClimate] active=" << (m_ctx->config.paleoClimate.enabled ? 1 : 0)
                  << " sourceSamples=" << m_paleoSeries.size()
                  << std::endl;
        std::cout << "[TransportRegimes] enabled=" << (m_ctx->config.transportRegimes.enabled ? 1 : 0)
                  << " exploration=" << (m_ctx->config.exploration.enabled ? 1 : 0)
                  << std::endl;
        std::cout << "[ResearchDiffusion] channels=" << (m_ctx->config.packageDiffusion.enabled ? 1 : 0)
                  << " knowledgeLoss=" << (m_ctx->config.knowledgeDynamics.enabled ? 1 : 0)
                  << " salinity=" << (m_ctx->config.salinity.enabled ? 1 : 0)
                  << std::endl;
    }

    computeDeterminismHash();

    if (m_debugEnabled) {
        printDebugSample(year, countries, 6);
    }
}

void SettlementSystem::ensureInitialized(int year, const Map& map, const std::vector<Country>& countries) {
    const int fw = map.getFieldWidth();
    const int fh = map.getFieldHeight();
    if (fw <= 0 || fh <= 0) {
        m_nodes.clear();
        m_edges.clear();
        m_initialized = true;
        m_fieldW = 0;
        m_fieldH = 0;
        return;
    }

    const bool shapeChanged = (!m_initialized || m_fieldW != fw || m_fieldH != fh);
    if (!shapeChanged && !m_nodes.empty()) {
        return;
    }

    m_fieldW = fw;
    m_fieldH = fh;
    m_nodes.clear();
    m_edges.clear();
    m_nextNodeId = 1;
    m_nodeOutgoingFlow.clear();
    m_nodeMarketPotential.clear();
    m_nodeUtility.clear();
    m_countryAgg.clear();
    m_fieldFertility.clear();
    m_fieldRegime.clear();
    m_fieldIrrigationCapital.clear();
    m_fieldSalinity.clear();
    m_fieldPaleoTempAdj.clear();
    m_fieldPaleoPrecipAdj.clear();
    m_nodeS.clear();
    m_nodeI.clear();
    m_nodeR.clear();
    m_nodeDiseaseBurden.clear();
    m_nodeImportedInfection.clear();
    m_nodeAdoptionPressure.clear();
    m_nodeJoinUtility.clear();
    m_nodeKnowledgeCoverage.clear();
    m_nodeUncertainty.clear();
    m_nodeExplorationValue.clear();
    m_nodeKnowledgeErosion.clear();
    m_nodePrevMarketPotential.clear();
    m_edgeExplorationBoost.clear();
    m_edgeLogisticsAttenuation.clear();
    m_nodeWarAttrition.clear();
    m_nodePastoralSeasonGain.clear();
    m_nodeExtractionRevenue.clear();
    m_nodePolitySwitchGain.clear();
    m_cachedPaleoYear = std::numeric_limits<int>::min();
    m_densityPriorTried = false;
    m_densityPriorLoaded = false;

    ensureDensityPriorLoaded();
    ensurePaleoSeriesLoaded();
    initializeNodesFromFieldPopulation(year, map, countries);
    m_initialized = true;
}

void SettlementSystem::ensureDensityPriorLoaded() {
    if (m_densityPriorTried) {
        return;
    }
    m_densityPriorTried = true;
    m_densityPriorLoaded = false;

    const int nField = std::max(0, m_fieldW * m_fieldH);
    m_densityPriorField.assign(static_cast<size_t>(nField), 0.0f);
    if (nField <= 0) {
        return;
    }
    if (!m_ctx->config.densityInit.enabled ||
        m_ctx->config.densityInit.priorPath.empty() ||
        m_ctx->config.densityInit.priorWeight <= 0.0) {
        return;
    }

    sf::Image img;
    if (!img.loadFromFile(m_ctx->config.densityInit.priorPath)) {
        if (!m_densityPriorLogged) {
            m_densityPriorLogged = true;
            std::cout << "[SettlementsDensityInit] priorLoaded=0 path='" << m_ctx->config.densityInit.priorPath
                      << "' reason=load_failed" << std::endl;
        }
        return;
    }
    const sf::Vector2u sz = img.getSize();
    if (sz.x == 0u || sz.y == 0u) {
        return;
    }

    float mn = std::numeric_limits<float>::max();
    float mx = std::numeric_limits<float>::lowest();
    for (int fy = 0; fy < m_fieldH; ++fy) {
        const unsigned sy0 = static_cast<unsigned>((static_cast<std::uint64_t>(fy) * sz.y) / std::max(1, m_fieldH));
        const unsigned sy1 = static_cast<unsigned>((static_cast<std::uint64_t>(fy + 1) * sz.y) / std::max(1, m_fieldH));
        for (int fx = 0; fx < m_fieldW; ++fx) {
            const unsigned sx0 = static_cast<unsigned>((static_cast<std::uint64_t>(fx) * sz.x) / std::max(1, m_fieldW));
            const unsigned sx1 = static_cast<unsigned>((static_cast<std::uint64_t>(fx + 1) * sz.x) / std::max(1, m_fieldW));
            const unsigned xb = std::max(sx0 + 1u, sx1);
            const unsigned yb = std::max(sy0 + 1u, sy1);

            double sumL = 0.0;
            std::uint64_t count = 0;
            for (unsigned y = sy0; y < yb && y < sz.y; ++y) {
                for (unsigned x = sx0; x < xb && x < sz.x; ++x) {
                    const sf::Color c = img.getPixel(x, y);
                    sumL += 0.2126 * static_cast<double>(c.r) +
                            0.7152 * static_cast<double>(c.g) +
                            0.0722 * static_cast<double>(c.b);
                    ++count;
                }
            }
            const float lum = (count > 0) ? static_cast<float>(sumL / (255.0 * static_cast<double>(count))) : 0.0f;
            const int fi = fieldIndex(fx, fy);
            if (fi >= 0 && static_cast<size_t>(fi) < m_densityPriorField.size()) {
                m_densityPriorField[static_cast<size_t>(fi)] = lum;
                mn = std::min(mn, lum);
                mx = std::max(mx, lum);
            }
        }
    }

    const float span = mx - mn;
    if (span > 1.0e-6f) {
        for (float& v : m_densityPriorField) {
            v = (v - mn) / span;
        }
    } else {
        std::fill(m_densityPriorField.begin(), m_densityPriorField.end(), 0.0f);
    }
    m_densityPriorLoaded = true;
    if (!m_densityPriorLogged) {
        m_densityPriorLogged = true;
        std::cout << "[SettlementsDensityInit] priorLoaded=1 path='" << m_ctx->config.densityInit.priorPath
                  << "' min=" << mn << " max=" << mx << std::endl;
    }
}

void SettlementSystem::ensurePaleoSeriesLoaded() {
    if (!m_paleoSeries.empty() || m_paleoStartupLogged) {
        return;
    }

    const SimulationConfig::PaleoClimate& cfg = m_ctx->config.paleoClimate;
    bool loadedCsv = false;

    if (cfg.enabled && !cfg.monthlyForcingCsv.empty()) {
        std::ifstream in(cfg.monthlyForcingCsv);
        if (in) {
            std::unordered_map<int, PaleoYearSample> byYear;
            std::unordered_map<int, std::array<std::uint8_t, 12>> seenMonth;
            std::string line;
            while (std::getline(in, line)) {
                line = trimAscii(line);
                if (line.empty()) continue;
                if (!line.empty() && line[0] == '#') continue;
                const auto cols = splitCsvLine(line);
                if (cols.empty()) continue;

                auto maybeInt = [](const std::string& s, int* out) -> bool {
                    if (s.empty()) return false;
                    char* end = nullptr;
                    const long v = std::strtol(s.c_str(), &end, 10);
                    if (end == s.c_str() || *end != '\0') return false;
                    *out = static_cast<int>(v);
                    return true;
                };
                auto maybeDouble = [](const std::string& s, double* out) -> bool {
                    if (s.empty()) return false;
                    char* end = nullptr;
                    const double v = std::strtod(s.c_str(), &end);
                    if (end == s.c_str() || *end != '\0' || !std::isfinite(v)) return false;
                    *out = v;
                    return true;
                };

                int year = 0;
                if (!maybeInt(cols[0], &year)) {
                    continue; // header or malformed
                }

                if (cols.size() >= 25) {
                    PaleoYearSample& s = byYear[year];
                    s.year = year;
                    bool ok = true;
                    for (int m = 0; m < 12; ++m) {
                        double tv = 0.0;
                        double pv = 0.0;
                        ok = ok && maybeDouble(cols[static_cast<size_t>(1 + m)], &tv);
                        ok = ok && maybeDouble(cols[static_cast<size_t>(13 + m)], &pv);
                        s.tempAnom[static_cast<size_t>(m)] = tv;
                        s.precipAnom[static_cast<size_t>(m)] = pv;
                    }
                    if (ok) {
                        loadedCsv = true;
                    }
                    continue;
                }

                if (cols.size() >= 4) {
                    int month = 0;
                    double t = 0.0;
                    double p = 0.0;
                    if (!maybeInt(cols[1], &month)) continue;
                    if (month < 1 || month > 12) continue;
                    if (!maybeDouble(cols[2], &t)) continue;
                    if (!maybeDouble(cols[3], &p)) continue;
                    PaleoYearSample& s = byYear[year];
                    s.year = year;
                    s.tempAnom[static_cast<size_t>(month - 1)] = t;
                    s.precipAnom[static_cast<size_t>(month - 1)] = p;
                    seenMonth[year][static_cast<size_t>(month - 1)] = 1u;
                    loadedCsv = true;
                }
            }

            if (loadedCsv) {
                m_paleoSeries.clear();
                m_paleoSeries.reserve(byYear.size());
                for (auto& it : byYear) {
                    PaleoYearSample s = it.second;
                    auto sm = seenMonth.find(it.first);
                    if (sm != seenMonth.end()) {
                        double tsum = 0.0;
                        double psum = 0.0;
                        int cnt = 0;
                        for (int m = 0; m < 12; ++m) {
                            if (sm->second[static_cast<size_t>(m)] != 0u) {
                                tsum += s.tempAnom[static_cast<size_t>(m)];
                                psum += s.precipAnom[static_cast<size_t>(m)];
                                ++cnt;
                            }
                        }
                        const double tf = (cnt > 0) ? (tsum / static_cast<double>(cnt)) : 0.0;
                        const double pf = (cnt > 0) ? (psum / static_cast<double>(cnt)) : 0.0;
                        for (int m = 0; m < 12; ++m) {
                            if (sm->second[static_cast<size_t>(m)] == 0u) {
                                s.tempAnom[static_cast<size_t>(m)] = tf;
                                s.precipAnom[static_cast<size_t>(m)] = pf;
                            }
                        }
                    }
                    m_paleoSeries.push_back(s);
                }
                std::sort(m_paleoSeries.begin(), m_paleoSeries.end(), [](const PaleoYearSample& a, const PaleoYearSample& b) {
                    return a.year < b.year;
                });
                m_paleoSeries.erase(std::unique(m_paleoSeries.begin(), m_paleoSeries.end(), [](const PaleoYearSample& a, const PaleoYearSample& b) {
                    return a.year == b.year;
                }), m_paleoSeries.end());
            }
        }
    }

    if (m_paleoSeries.empty()) {
        // Deterministic synthetic monthly forcing fallback.
        for (int year = -22000; year <= 2025; year += 25) {
            PaleoYearSample s;
            s.year = year;
            const double tBase = syntheticPaleoTempOffsetC(year);
            const double pBase = syntheticPaleoPrecipOffset01(year);
            for (int m = 0; m < 12; ++m) {
                const double phase = (2.0 * 3.14159265358979323846 * (static_cast<double>(m) + 0.5)) / 12.0;
                const double waveA = std::sin(phase);
                const double waveB = std::cos(phase * 2.0);
                s.tempAnom[static_cast<size_t>(m)] = tBase + 0.18 * waveA + 0.05 * waveB;
                s.precipAnom[static_cast<size_t>(m)] = pBase + 0.014 * waveA - 0.008 * waveB;
            }
            m_paleoSeries.push_back(s);
        }
    }

    if (cfg.startupDiagnostics) {
        m_paleoStartupLogged = true;
        std::cout << "[PaleoClimate] enabled=" << (cfg.enabled ? 1 : 0)
                  << " loaded=" << (!m_paleoSeries.empty() ? 1 : 0)
                  << " source=" << (loadedCsv ? "csv" : "synthetic")
                  << " samples=" << m_paleoSeries.size()
                  << " file='" << cfg.monthlyForcingCsv << "'"
                  << std::endl;
    }
}

SettlementSystem::PaleoYearForcing SettlementSystem::evaluatePaleoForcing(int year) {
    if (year == m_cachedPaleoYear) {
        return m_cachedPaleoForcing;
    }
    m_cachedPaleoYear = year;
    m_cachedPaleoForcing = PaleoYearForcing{};

    if (!m_ctx->config.paleoClimate.enabled) {
        return m_cachedPaleoForcing;
    }
    ensurePaleoSeriesLoaded();
    if (m_paleoSeries.empty()) {
        return m_cachedPaleoForcing;
    }

    auto it = std::lower_bound(m_paleoSeries.begin(), m_paleoSeries.end(), year, [](const PaleoYearSample& s, int y) {
        return s.year < y;
    });
    const PaleoYearSample* a = nullptr;
    const PaleoYearSample* b = nullptr;
    if (it == m_paleoSeries.begin()) {
        a = b = &(*it);
    } else if (it == m_paleoSeries.end()) {
        a = b = &m_paleoSeries.back();
    } else {
        b = &(*it);
        a = &(*(it - 1));
    }
    double t = 0.0;
    if (a != b) {
        t = static_cast<double>(year - a->year) / static_cast<double>(std::max(1, b->year - a->year));
        t = std::clamp(t, 0.0, 1.0);
    }

    for (int m = 0; m < 12; ++m) {
        const double ta = a->tempAnom[static_cast<size_t>(m)];
        const double tb = b->tempAnom[static_cast<size_t>(m)];
        const double pa = a->precipAnom[static_cast<size_t>(m)];
        const double pb = b->precipAnom[static_cast<size_t>(m)];
        m_cachedPaleoForcing.tempAnom[static_cast<size_t>(m)] = ta + (tb - ta) * t;
        m_cachedPaleoForcing.precipAnom[static_cast<size_t>(m)] = pa + (pb - pa) * t;
        m_cachedPaleoForcing.tempMean += m_cachedPaleoForcing.tempAnom[static_cast<size_t>(m)];
        m_cachedPaleoForcing.precipMean += m_cachedPaleoForcing.precipAnom[static_cast<size_t>(m)];
    }
    m_cachedPaleoForcing.tempMean /= 12.0;
    m_cachedPaleoForcing.precipMean /= 12.0;

    double varP = 0.0;
    double wet = 0.0;
    double dry = 0.0;
    for (int m = 0; m < 12; ++m) {
        const double p = m_cachedPaleoForcing.precipAnom[static_cast<size_t>(m)];
        const double d = p - m_cachedPaleoForcing.precipMean;
        varP += d * d;
        if (m >= 4 && m <= 8) {
            wet += p;
        } else {
            dry += p;
        }
    }
    m_cachedPaleoForcing.precipStd = std::sqrt(varP / 12.0);
    wet /= 5.0;
    dry /= 7.0;
    m_cachedPaleoForcing.monsoonPulse = std::clamp(wet - dry, -1.0, 1.0);
    m_cachedPaleoForcing.droughtPulse = std::clamp(
        std::max(0.0, -m_cachedPaleoForcing.precipMean * 5.0) + 1.8 * m_cachedPaleoForcing.precipStd,
        0.0, 1.0);
    m_cachedPaleoForcing.coolingPulse = std::clamp(-0.22 * m_cachedPaleoForcing.tempMean, 0.0, 1.0);

    return m_cachedPaleoForcing;
}

void SettlementSystem::initializeNodesFromFieldPopulation(int year,
                                                          const Map& map,
                                                          const std::vector<Country>& countries) {
    struct Candidate {
        int owner = -1;
        int idx = -1;
        double pop = 0.0;
        double score = 0.0;
    };

    const auto& owner = map.getFieldOwnerId();
    const auto& fieldPop = map.getFieldPopulation();
    const auto& landMask = map.getFieldLandMask();
    const auto& food = map.getFieldFoodPotential();
    const auto& corridor = map.getFieldCorridorWeight();
    const auto& climateYield = map.getFieldFoodYieldMult();

    if (owner.empty() || fieldPop.empty() || landMask.empty()) {
        return;
    }

    const int countryCount = static_cast<int>(countries.size());
    const size_t n = std::min({owner.size(), fieldPop.size(), landMask.size()});
    std::vector<Candidate> cand;
    cand.reserve(n / 16 + 16);

    const bool useDensity = m_ctx->config.densityInit.enabled;
    const bool requireOwned = m_ctx->config.densityInit.requireLandOwnership;
    const double minPop = std::max(1.0, m_ctx->config.settlements.initNodeMinPop);
    const double minPopFactor = std::clamp(m_ctx->config.densityInit.minPopFactor, 0.0, 2.0);
    const double minPopGate = useDensity ? (minPop * minPopFactor) : minPop;

    double maxPop = 1.0;
    double maxFood = 1.0;
    double maxCorr = 1.0;
    double maxClimate = 1.0;
    for (size_t i = 0; i < n; ++i) {
        if (landMask[i] == 0u) continue;
        if (requireOwned && (owner[i] < 0 || owner[i] >= countryCount)) continue;
        maxPop = std::max(maxPop, std::max(0.0, static_cast<double>(fieldPop[i])));
        if (i < food.size()) maxFood = std::max(maxFood, std::max(0.0, static_cast<double>(food[i])));
        if (i < corridor.size()) maxCorr = std::max(maxCorr, std::max(0.0, static_cast<double>(corridor[i])));
        if (i < climateYield.size()) maxClimate = std::max(maxClimate, std::max(0.0, static_cast<double>(climateYield[i])));
    }

    const double wPop = std::max(0.0, m_ctx->config.densityInit.weightPopulation);
    const double wFood = std::max(0.0, m_ctx->config.densityInit.weightFood);
    const double wCorr = std::max(0.0, m_ctx->config.densityInit.weightCorridor);
    const double wClimate = std::max(0.0, m_ctx->config.densityInit.weightClimate);
    const double wPrior = std::max(0.0, m_ctx->config.densityInit.priorWeight);
    const double wDenom = std::max(1.0e-9, wPop + wFood + wCorr + wClimate + wPrior);
    const double noiseAmp = std::clamp(m_ctx->config.densityInit.scoreNoise, 0.0, 0.5);

    for (size_t i = 0; i < n; ++i) {
        const int o = owner[i];
        if (o < 0 || o >= countryCount) continue;
        if (landMask[i] == 0u) continue;
        const double p = std::max(0.0, static_cast<double>(fieldPop[i]));
        if (p < minPopGate) continue;

        double score = p;
        if (useDensity) {
            const double popNorm = clamp01(p / maxPop);
            const double foodNorm = (i < food.size()) ? clamp01(std::max(0.0, static_cast<double>(food[i])) / maxFood) : 0.0;
            const double corrNorm = (i < corridor.size()) ? clamp01(std::max(0.0, static_cast<double>(corridor[i])) / maxCorr) : 0.0;
            const double climateNorm = (i < climateYield.size()) ? clamp01(std::max(0.0, static_cast<double>(climateYield[i])) / maxClimate) : 0.0;
            const double priorNorm = (i < m_densityPriorField.size()) ? clamp01(static_cast<double>(m_densityPriorField[i])) : 0.0;
            score = (wPop * popNorm + wFood * foodNorm + wCorr * corrNorm + wClimate * climateNorm + wPrior * priorNorm) / wDenom;
            if (noiseAmp > 0.0) {
                const std::uint64_t bits = SimulationContext::mix64(
                    m_ctx->worldSeed ^
                    (static_cast<std::uint64_t>(year + 70000) * 0x9E3779B97F4A7C15ull) ^
                    (static_cast<std::uint64_t>(i + 11) * 0xBF58476D1CE4E5B9ull));
                score += noiseAmp * (SimulationContext::u01FromU64(bits) * 2.0 - 1.0);
            }
            score = clamp01(score);
        }
        cand.push_back(Candidate{o, static_cast<int>(i), p, score});
    }

    std::sort(cand.begin(), cand.end(), [](const Candidate& a, const Candidate& b) {
        if (a.owner != b.owner) return a.owner < b.owner;
        if (a.score != b.score) return a.score > b.score;
        if (a.pop != b.pop) return a.pop > b.pop;
        return a.idx < b.idx;
    });

    const int globalCap = std::max(1, m_ctx->config.settlements.maxNodesGlobal);
    const int perCountryCap = std::max(1, m_ctx->config.settlements.maxNodesPerCountry);
    const int spacing = std::max(1, m_ctx->config.settlements.splitMinSpacingFields);

    std::vector<int> nodesByCountry(static_cast<size_t>(countryCount), 0);
    for (const Candidate& c : cand) {
        if (static_cast<int>(m_nodes.size()) >= globalCap) break;
        if (nodesByCountry[static_cast<size_t>(c.owner)] >= perCountryCap) continue;

        const int fx = c.idx % m_fieldW;
        const int fy = c.idx / m_fieldW;
        bool tooClose = false;
        for (const SettlementNode& nnode : m_nodes) {
            if (nnode.ownerCountry != c.owner) continue;
            const int dx = std::abs(nnode.fieldX - fx);
            const int dy = std::abs(nnode.fieldY - fy);
            if (std::max(dx, dy) < spacing) {
                tooClose = true;
                break;
            }
        }
        if (tooClose) continue;

        const double fp = (static_cast<size_t>(c.idx) < food.size()) ? std::max(0.0, static_cast<double>(food[static_cast<size_t>(c.idx)])) : 0.0;
        const Country& country = countries[static_cast<size_t>(c.owner)];
        const auto& m = country.getMacroEconomy();

        SettlementNode node;
        node.id = m_nextNodeId++;
        node.ownerCountry = c.owner;
        node.fieldX = fx;
        node.fieldY = fy;
        if (useDensity) {
            node.population = std::max(5.0, std::max(c.pop, minPop * (0.55 + 0.45 * c.score)));
        } else {
            node.population = std::max(5.0, c.pop);
        }
        node.carryingCapacity = std::max(node.population * 1.20, fp * std::max(1.0, m_ctx->config.settlements.kBasePerFoodUnit));
        node.specialistShare = clamp01(0.02 + 0.10 * m.marketAccess);
        node.storageStock = 0.08 + 0.20 * m.institutionCapacity;
        node.waterFactor = 1.0;
        node.soilFactor = 1.0;
        node.techFactor = 0.80 + 0.40 * m.knowledgeStock;
        node.irrigationCapital = clamp01(0.10 + 0.35 * m.institutionCapacity + 0.15 * m.marketAccess);
        node.eliteShare = clamp01(0.08 + 0.26 * m.inequality);
        node.localLegitimacy = clamp01(0.35 + 0.45 * country.getLegitimacy());
        node.localAdminCapacity = clamp01(0.22 + 0.48 * country.getAdminCapacity());
        node.extractionRate = clamp01(0.04 + 0.12 * m.institutionCapacity);
        node.foundedYear = year;
        node.lastSplitYear = -9999999;

        m_nodes.push_back(node);
        nodesByCountry[static_cast<size_t>(c.owner)] += 1;
    }

    // Ensure every live country has at least one node.
    for (const Country& c : countries) {
        const int ownerId = c.getCountryIndex();
        if (ownerId < 0 || ownerId >= countryCount) continue;
        if (c.getPopulation() <= 0) continue;
        if (nodesByCountry[static_cast<size_t>(ownerId)] > 0) continue;
        if (static_cast<int>(m_nodes.size()) >= globalCap) break;

        const sf::Vector2i start = c.getStartingPixel();
        int fx = std::clamp(start.x / Map::kFieldCellSize, 0, m_fieldW - 1);
        int fy = std::clamp(start.y / Map::kFieldCellSize, 0, m_fieldH - 1);
        int fi = fieldIndex(fx, fy);
        if (fi < 0 || static_cast<size_t>(fi) >= landMask.size() || landMask[static_cast<size_t>(fi)] == 0u) {
            bool found = false;
            for (int r = 1; r < 6 && !found; ++r) {
                for (int dy = -r; dy <= r && !found; ++dy) {
                    for (int dx = -r; dx <= r; ++dx) {
                        const int nx = fx + dx;
                        const int ny = fy + dy;
                        if (nx < 0 || ny < 0 || nx >= m_fieldW || ny >= m_fieldH) continue;
                        const int nfi = fieldIndex(nx, ny);
                        if (nfi < 0 || static_cast<size_t>(nfi) >= landMask.size()) continue;
                        if (landMask[static_cast<size_t>(nfi)] == 0u) continue;
                        fx = nx;
                        fy = ny;
                        fi = nfi;
                        found = true;
                        break;
                    }
                }
            }
        }

        SettlementNode node;
        node.id = m_nextNodeId++;
        node.ownerCountry = ownerId;
        node.fieldX = fx;
        node.fieldY = fy;
        node.population = std::max(50.0, static_cast<double>(c.getPopulation()));
        const double fp = (fi >= 0 && static_cast<size_t>(fi) < food.size()) ? std::max(1.0, static_cast<double>(food[static_cast<size_t>(fi)])) : 1.0;
        node.carryingCapacity = std::max(node.population * 1.20, fp * std::max(1.0, m_ctx->config.settlements.kBasePerFoodUnit));
        node.specialistShare = 0.02;
        node.storageStock = 0.08;
        node.techFactor = 0.90;
        node.irrigationCapital = 0.08;
        node.eliteShare = 0.12;
        node.localLegitimacy = clamp01(0.35 + 0.45 * c.getLegitimacy());
        node.localAdminCapacity = clamp01(0.20 + 0.45 * c.getAdminCapacity());
        node.extractionRate = 0.06;
        node.foundedYear = year;
        node.lastSplitYear = -9999999;
        m_nodes.push_back(node);
        nodesByCountry[static_cast<size_t>(ownerId)] += 1;
    }

    std::sort(m_nodes.begin(), m_nodes.end(), [](const SettlementNode& a, const SettlementNode& b) {
        if (a.id != b.id) return a.id < b.id;
        if (a.fieldY != b.fieldY) return a.fieldY < b.fieldY;
        return a.fieldX < b.fieldX;
    });
}

void SettlementSystem::syncNodeTotalsToCountryPopulation(const std::vector<Country>& countries) {
    const int nCountry = static_cast<int>(countries.size());
    std::vector<double> totalByCountry(static_cast<size_t>(nCountry), 0.0);
    for (const SettlementNode& n : m_nodes) {
        if (n.ownerCountry < 0 || n.ownerCountry >= nCountry) continue;
        totalByCountry[static_cast<size_t>(n.ownerCountry)] += std::max(0.0, n.population);
    }

    std::vector<double> scale(static_cast<size_t>(nCountry), 1.0);
    for (int i = 0; i < nCountry; ++i) {
        const double target = static_cast<double>(std::max<long long>(0, countries[static_cast<size_t>(i)].getPopulation()));
        const double current = totalByCountry[static_cast<size_t>(i)];
        if (current > kEps) {
            scale[static_cast<size_t>(i)] = target / current;
        } else {
            scale[static_cast<size_t>(i)] = (target <= 0.0) ? 1.0 : 0.0;
        }
    }

    for (SettlementNode& n : m_nodes) {
        if (n.ownerCountry < 0 || n.ownerCountry >= nCountry) continue;
        n.population = std::max(0.0, n.population * scale[static_cast<size_t>(n.ownerCountry)]);
    }
}

void SettlementSystem::updateSubsistenceMixAndPackages(int year,
                                                       const Map& map,
                                                       const std::vector<Country>& countries) {
    const auto& corridor = map.getFieldCorridorWeight();
    const auto& food = map.getFieldFoodPotential();
    const auto& temp = map.getFieldTempMean();
    const auto& precip = map.getFieldPrecipMean();
    const auto& landMask = map.getFieldLandMask();

    const auto& pkgs = getDefaultDomesticPackages();
    const double rate = std::clamp(m_ctx->config.subsistence.mixAdaptRate, 0.0, 1.0);

    for (size_t ni = 0; ni < m_nodes.size(); ++ni) {
        SettlementNode& n = m_nodes[ni];
        const int fi = fieldIndex(n.fieldX, n.fieldY);
        if (fi < 0) continue;

        const double fp = (static_cast<size_t>(fi) < food.size()) ? std::max(0.0, static_cast<double>(food[static_cast<size_t>(fi)])) : 0.0;
        const double fpNorm = clamp01(fp / 2600.0);
        const double corr = (static_cast<size_t>(fi) < corridor.size()) ? clamp01(static_cast<double>(corridor[static_cast<size_t>(fi)]) / 2.2) : 0.0;
        const double tMean = (static_cast<size_t>(fi) < temp.size()) ? static_cast<double>(temp[static_cast<size_t>(fi)]) : 12.0;
        const double pMean = (static_cast<size_t>(fi) < precip.size()) ? static_cast<double>(precip[static_cast<size_t>(fi)]) : 0.45;
        const double arid = clamp01(1.0 - pMean);
        const double cold = clamp01((7.0 - tMean) / 15.0);

        bool coastal = false;
        for (int d = 0; d < 4 && !coastal; ++d) {
            const int nx = n.fieldX + ((d == 0) ? 1 : (d == 1) ? -1 : 0);
            const int ny = n.fieldY + ((d == 2) ? 1 : (d == 3) ? -1 : 0);
            if (nx < 0 || ny < 0 || nx >= m_fieldW || ny >= m_fieldH) {
                coastal = true;
                continue;
            }
            const int nfi = fieldIndex(nx, ny);
            if (nfi < 0 || static_cast<size_t>(nfi) >= landMask.size() || landMask[static_cast<size_t>(nfi)] == 0u) {
                coastal = true;
            }
        }
        const double water = clamp01(0.60 * pMean + (coastal ? 0.35 : 0.0));

        const double market = (ni < m_nodeMarketPotential.size()) ? clamp01(m_nodeMarketPotential[ni] / 60.0) : 0.0;
        const double foragePay = 0.32 + 0.55 * (1.0 - arid) * (0.75 + 0.25 * (1.0 - cold));
        const double farmPay = 0.22 + 1.00 * fpNorm * (0.35 + 0.65 * water);
        const double pastoralPay = 0.20 + 0.78 * arid + 0.22 * corr;
        const double fishPay = (coastal ? (0.28 + 0.95 * water) : 0.05);
        const double craftPay = 0.08 + std::max(0.0, m_ctx->config.subsistence.craftFromMarketWeight) * market;

        std::array<double, static_cast<size_t>(SubsistenceMode::Count)> pay{
            foragePay,
            farmPay,
            pastoralPay,
            fishPay,
            craftPay,
        };

        // Existing domestic bundles modify mode payoffs.
        for (int pkgId : n.adoptedPackages) {
            if (pkgId < 0 || static_cast<size_t>(pkgId) >= pkgs.size()) continue;
            const DomesticPackageDefinition& pkg = pkgs[static_cast<size_t>(pkgId)];
            pay[static_cast<size_t>(SubsistenceMode::Foraging)] *= pkg.foragingMul;
            pay[static_cast<size_t>(SubsistenceMode::Farming)] *= pkg.farmingMul;
            pay[static_cast<size_t>(SubsistenceMode::Pastoral)] *= pkg.pastoralMul;
            pay[static_cast<size_t>(SubsistenceMode::Fishing)] *= pkg.fishingMul;
            pay[static_cast<size_t>(SubsistenceMode::Craft)] *= (1.0 + 0.20 * pkg.marketAffinity);
        }

        double weighted = 0.0;
        for (size_t k = 0; k < n.mix.size(); ++k) {
            weighted += n.mix[k] * pay[k];
        }
        for (size_t k = 0; k < n.mix.size(); ++k) {
            n.mix[k] = std::max(1.0e-4, n.mix[k] + rate * n.mix[k] * (pay[k] - weighted));
        }

        double sumMix = std::accumulate(n.mix.begin(), n.mix.end(), 0.0);
        if (!(sumMix > 0.0)) {
            n.mix = {0.42, 0.36, 0.10, 0.08, 0.04};
        } else {
            for (double& v : n.mix) v /= sumMix;
        }

        if (m_ctx->config.packages.enabled) {
            for (const DomesticPackageDefinition& pkg : pkgs) {
                if (std::find(n.adoptedPackages.begin(), n.adoptedPackages.end(), pkg.id) != n.adoptedPackages.end()) {
                    continue;
                }
                const double affinityNorm = std::max(1.0,
                    std::abs(pkg.waterAffinity) + std::abs(pkg.aridAffinity) + std::abs(pkg.coldAffinity) + std::abs(pkg.marketAffinity));
                double envScore =
                    pkg.waterAffinity * water +
                    pkg.aridAffinity * arid +
                    pkg.coldAffinity * cold +
                    pkg.marketAffinity * market;
                envScore = clamp01(envScore / affinityNorm);

                const std::uint64_t mix = SimulationContext::mix64(
                    m_ctx->worldSeed ^
                    (static_cast<std::uint64_t>(year + 20000) * 0x9E3779B97F4A7C15ull) ^
                    (static_cast<std::uint64_t>(n.id + 1) * 0xBF58476D1CE4E5B9ull) ^
                    (static_cast<std::uint64_t>(pkg.id + 19) * 0x94D049BB133111EBull));
                const double jitter = (SimulationContext::u01FromU64(mix) - 0.5) * 0.08;

                const double score =
                    std::max(0.0, m_ctx->config.packages.environmentWeight) * envScore +
                    std::max(0.0, m_ctx->config.packages.diffusionWeight) * market +
                    0.25 * corr +
                    jitter;
                const double threshold = 1.0 - std::clamp(m_ctx->config.packages.adoptionBase, 0.0, 1.0);
                if (score >= threshold) {
                    n.adoptedPackages.push_back(pkg.id);
                    n.storageStock = std::min(1.5, n.storageStock + std::max(0.0, pkg.storageBonus));
                }
            }
            std::sort(n.adoptedPackages.begin(), n.adoptedPackages.end());
        }

        n.waterFactor = 0.80 + 0.45 * water;
        n.soilFactor = 0.72 + 0.60 * fpNorm;
        if (n.ownerCountry >= 0 && static_cast<size_t>(n.ownerCountry) < countries.size()) {
            const auto& m = countries[static_cast<size_t>(n.ownerCountry)].getMacroEconomy();
            n.techFactor = 0.75 + 0.40 * clamp01(m.knowledgeStock) + 0.30 * clamp01(m.institutionCapacity);
        }
    }
}

void SettlementSystem::updateClimateRegimesAndFertility(int year, const Map& map, const std::vector<Country>& countries) {
    const int nField = std::max(0, m_fieldW * m_fieldH);
    if (nField <= 0) {
        m_fieldFertility.clear();
        m_fieldRegime.clear();
        return;
    }

    if (m_fieldFertility.size() != static_cast<size_t>(nField)) {
        m_fieldFertility.assign(static_cast<size_t>(nField), 0.65f);
    }
    if (m_fieldRegime.size() != static_cast<size_t>(nField)) {
        m_fieldRegime.assign(static_cast<size_t>(nField), static_cast<std::uint8_t>(kRegimeNormal));
    }
    if (m_fieldIrrigationCapital.size() != static_cast<size_t>(nField)) {
        m_fieldIrrigationCapital.assign(static_cast<size_t>(nField), 0.0f);
    }
    if (m_fieldSalinity.size() != static_cast<size_t>(nField)) {
        m_fieldSalinity.assign(static_cast<size_t>(nField), 0.0f);
    }
    if (m_fieldPaleoTempAdj.size() != static_cast<size_t>(nField)) {
        m_fieldPaleoTempAdj.assign(static_cast<size_t>(nField), 0.0f);
    }
    if (m_fieldPaleoPrecipAdj.size() != static_cast<size_t>(nField)) {
        m_fieldPaleoPrecipAdj.assign(static_cast<size_t>(nField), 0.0f);
    }

    const auto& precip = map.getFieldPrecipMean();
    const auto& temp = map.getFieldTempMean();
    const auto& landMask = map.getFieldLandMask();
    const auto& food = map.getFieldFoodPotential();
    const auto& corridor = map.getFieldCorridorWeight();
    const auto& owner = map.getFieldOwnerId();

    const PaleoYearForcing forcing = evaluatePaleoForcing(year);
    const double tempInfluence = std::max(0.0, m_ctx->config.paleoClimate.tempInfluence);
    const double precipInfluence = std::max(0.0, m_ctx->config.paleoClimate.precipInfluence);
    const double monsoonScale = std::max(0.0, m_ctx->config.paleoClimate.monsoonVarianceScale);
    const double droughtScale = std::max(0.0, m_ctx->config.paleoClimate.droughtClusterScale);
    const double coolingScale = std::max(0.0, m_ctx->config.paleoClimate.coolingPulseScale);

    const double globalTempAdj = tempInfluence * forcing.tempMean;
    const double globalPrecipAdj = precipInfluence * forcing.precipMean;
    for (int fi = 0; fi < nField; ++fi) {
        if (static_cast<size_t>(fi) >= landMask.size() || landMask[static_cast<size_t>(fi)] == 0u) {
            m_fieldPaleoTempAdj[static_cast<size_t>(fi)] = 0.0f;
            m_fieldPaleoPrecipAdj[static_cast<size_t>(fi)] = 0.0f;
            continue;
        }
        const int fy = fi / std::max(1, m_fieldW);
        const double lat = (m_fieldH > 1)
            ? std::abs((2.0 * static_cast<double>(fy) / static_cast<double>(m_fieldH - 1)) - 1.0)
            : 0.0;
        const double tropics = 1.0 - std::clamp((lat - 0.02) / 0.72, 0.0, 1.0);
        const double polar = std::clamp((lat - 0.35) / 0.65, 0.0, 1.0);
        const double monsoonTerm = monsoonScale * forcing.monsoonPulse * tropics;
        const double droughtTerm = -droughtScale * forcing.droughtPulse * (0.30 + 0.70 * tropics);
        const double coolTerm = -coolingScale * forcing.coolingPulse * polar;
        m_fieldPaleoTempAdj[static_cast<size_t>(fi)] = static_cast<float>(globalTempAdj + coolTerm);
        m_fieldPaleoPrecipAdj[static_cast<size_t>(fi)] = static_cast<float>(globalPrecipAdj + monsoonTerm + droughtTerm);
    }

    // Settlement land-use intensity proxy drives depletion term.
    std::vector<float> intensity(static_cast<size_t>(nField), 0.0f);
    for (const SettlementNode& n : m_nodes) {
        const int fi = fieldIndex(n.fieldX, n.fieldY);
        if (fi < 0 || fi >= nField) continue;
        const double farmShare = n.mix[static_cast<size_t>(SubsistenceMode::Farming)];
        const double pressure = std::max(0.0, n.population) * (0.20 + 0.80 * farmShare);
        intensity[static_cast<size_t>(fi)] += static_cast<float>(pressure);
    }
    for (int fi = 0; fi < nField; ++fi) {
        const double fp = (static_cast<size_t>(fi) < food.size()) ? std::max(1.0, static_cast<double>(food[static_cast<size_t>(fi)])) : 1.0;
        intensity[static_cast<size_t>(fi)] = static_cast<float>(clamp01(static_cast<double>(intensity[static_cast<size_t>(fi)]) / (120.0 * fp)));
    }

    bool usedGpu = false;
    if (m_gpu && m_gpu->enabled && m_gpu->climateKernel && m_ctx->config.researchGpu.climateFertility) {
        std::vector<sf::Uint8> statePx(static_cast<size_t>(nField) * 4u, 0u);
        std::vector<sf::Uint8> climatePx(static_cast<size_t>(nField) * 4u, 0u);
        for (int fi = 0; fi < nField; ++fi) {
            const size_t p = static_cast<size_t>(fi) * 4u;
            const double fert = clamp01(static_cast<double>(m_fieldFertility[static_cast<size_t>(fi)]));
            const double regimeN = clamp01(static_cast<double>(m_fieldRegime[static_cast<size_t>(fi)]) / 3.0);
            const double inten = clamp01(static_cast<double>(intensity[static_cast<size_t>(fi)]));
            const double pAdj = (static_cast<size_t>(fi) < m_fieldPaleoPrecipAdj.size()) ? static_cast<double>(m_fieldPaleoPrecipAdj[static_cast<size_t>(fi)]) : 0.0;
            const double tAdj = (static_cast<size_t>(fi) < m_fieldPaleoTempAdj.size()) ? static_cast<double>(m_fieldPaleoTempAdj[static_cast<size_t>(fi)]) : 0.0;
            const double pMean = (static_cast<size_t>(fi) < precip.size()) ? clamp01(static_cast<double>(precip[static_cast<size_t>(fi)]) + pAdj) : clamp01(0.50 + pAdj);
            const double tMean = (static_cast<size_t>(fi) < temp.size()) ? (static_cast<double>(temp[static_cast<size_t>(fi)]) + tAdj) : (12.0 + tAdj);
            const double tNorm = clamp01((tMean + 20.0) / 60.0);
            statePx[p + 0u] = static_cast<sf::Uint8>(std::round(fert * 255.0));
            statePx[p + 1u] = static_cast<sf::Uint8>(std::round(regimeN * 255.0));
            statePx[p + 2u] = static_cast<sf::Uint8>(std::round(inten * 255.0));
            statePx[p + 3u] = 255u;
            climatePx[p + 0u] = static_cast<sf::Uint8>(std::round(pMean * 255.0));
            climatePx[p + 1u] = static_cast<sf::Uint8>(std::round(tNorm * 255.0));
            climatePx[p + 2u] = 0u;
            climatePx[p + 3u] = 255u;
        }

        sf::Texture stateTex;
        sf::Texture climateTex;
        if (stateTex.create(static_cast<unsigned>(m_fieldW), static_cast<unsigned>(m_fieldH)) &&
            climateTex.create(static_cast<unsigned>(m_fieldW), static_cast<unsigned>(m_fieldH))) {
            stateTex.setSmooth(false);
            climateTex.setSmooth(false);
            stateTex.update(statePx.data());
            climateTex.update(climatePx.data());

            m_gpu->climateShader.setUniform("stateTex", stateTex);
            m_gpu->climateShader.setUniform("climateTex", climateTex);
            m_gpu->climateShader.setUniform("yearNorm", static_cast<float>((year + 30000) % 10000) / 10000.0f);
            m_gpu->climateShader.setUniform("seedNorm", static_cast<float>(m_ctx->worldSeed & 0xFFFFu) / 65535.0f);
            m_gpu->climateShader.setUniform("regenBase", 0.018f);
            m_gpu->climateShader.setUniform("depleteBase", 0.022f);

            std::vector<sf::Uint8> outPx;
            if (runShaderPass2D(m_gpu->climateShader, stateTex, m_fieldW, m_fieldH, outPx)) {
                usedGpu = true;
                for (int fi = 0; fi < nField; ++fi) {
                    const size_t p = static_cast<size_t>(fi) * 4u;
                    m_fieldFertility[static_cast<size_t>(fi)] = static_cast<float>(outPx[p + 0u]) / 255.0f;
                    const double regN = static_cast<double>(outPx[p + 1u]) / 255.0;
                    int regime = static_cast<int>(std::round(regN * 3.0));
                    regime = std::clamp(regime, 0, 3);
                    m_fieldRegime[static_cast<size_t>(fi)] = static_cast<std::uint8_t>(regime);
                }
            }
        }
    }

    if (!usedGpu) {
        for (int fi = 0; fi < nField; ++fi) {
            if (static_cast<size_t>(fi) >= landMask.size() || landMask[static_cast<size_t>(fi)] == 0u) {
                m_fieldRegime[static_cast<size_t>(fi)] = static_cast<std::uint8_t>(kRegimeNormal);
                m_fieldFertility[static_cast<size_t>(fi)] = 0.60f;
                continue;
            }
            const double pAdj = (static_cast<size_t>(fi) < m_fieldPaleoPrecipAdj.size()) ? static_cast<double>(m_fieldPaleoPrecipAdj[static_cast<size_t>(fi)]) : 0.0;
            const double tAdj = (static_cast<size_t>(fi) < m_fieldPaleoTempAdj.size()) ? static_cast<double>(m_fieldPaleoTempAdj[static_cast<size_t>(fi)]) : 0.0;
            const double pMean = (static_cast<size_t>(fi) < precip.size()) ? clamp01(static_cast<double>(precip[static_cast<size_t>(fi)]) + pAdj) : clamp01(0.50 + pAdj);
            const double tMean = (static_cast<size_t>(fi) < temp.size()) ? (static_cast<double>(temp[static_cast<size_t>(fi)]) + tAdj) : (12.0 + tAdj);
            const double hot = clamp01((tMean - 12.0) / 20.0);
            const double cold = clamp01((4.0 - tMean) / 14.0);
            const double forcingDry = clamp01(std::max(0.0, -pAdj) + 0.4 * forcing.droughtPulse);
            const double forcingCold = clamp01(std::max(0.0, -tAdj * 0.08) + 0.4 * forcing.coolingPulse);
            const int regime = static_cast<int>(m_fieldRegime[static_cast<size_t>(fi)]);
            const std::uint64_t mix = SimulationContext::mix64(
                m_ctx->worldSeed ^
                (static_cast<std::uint64_t>(year + 50000) * 0x9E3779B97F4A7C15ull) ^
                (static_cast<std::uint64_t>(fi + 17) * 0xBF58476D1CE4E5B9ull));
            const double h = SimulationContext::u01FromU64(mix);
            int nextRegime = regime;
            if (regime == kRegimeNormal) {
                const double pD = std::clamp(0.03 + 0.06 * (1.0 - pMean) + 0.03 * hot + 0.08 * droughtScale * forcingDry, 0.0, 0.60);
                const double pP = std::clamp(0.02 + 0.05 * pMean, 0.0, 0.30);
                const double pC = std::clamp(0.01 + 0.03 * cold + 0.06 * coolingScale * forcingCold, 0.0, 0.35);
                if (h < pD) nextRegime = kRegimeDrought;
                else if (h < pD + pP) nextRegime = kRegimePluvial;
                else if (h < pD + pP + pC) nextRegime = kRegimeCold;
                else nextRegime = kRegimeNormal;
            } else if (regime == kRegimeDrought) {
                const double stay = std::clamp(0.68 + 0.18 * droughtScale * forcingDry, 0.45, 0.95);
                if (h < stay) nextRegime = kRegimeDrought;
                else if (h < stay + 0.14) nextRegime = kRegimeNormal;
                else if (h < stay + 0.26) nextRegime = kRegimePluvial;
                else nextRegime = kRegimeCold;
            } else if (regime == kRegimePluvial) {
                if (h < 0.63) nextRegime = kRegimePluvial;
                else if (h < 0.87) nextRegime = kRegimeNormal;
                else if (h < 0.95) nextRegime = kRegimeDrought;
                else nextRegime = kRegimeCold;
            } else {
                const double stay = std::clamp(0.62 + 0.15 * coolingScale * forcingCold, 0.45, 0.92);
                if (h < stay) nextRegime = kRegimeCold;
                else if (h < stay + 0.26) nextRegime = kRegimeNormal;
                else if (h < stay + 0.33) nextRegime = kRegimeDrought;
                else nextRegime = kRegimePluvial;
            }
            m_fieldRegime[static_cast<size_t>(fi)] = static_cast<std::uint8_t>(nextRegime);

            double regenMul = 1.0;
            double depMul = 1.0;
            if (nextRegime == kRegimeDrought) { regenMul = 0.62; depMul = 1.40; }
            if (nextRegime == kRegimePluvial) { regenMul = 1.24; depMul = 0.84; }
            if (nextRegime == kRegimeCold) { regenMul = 0.80; depMul = 1.15; }

            const double inten = clamp01(static_cast<double>(intensity[static_cast<size_t>(fi)]));
            const double regen = 0.018 * (1.0 - inten) * regenMul;
            const double deplete = 0.022 * inten * depMul;
            const double nextF = std::clamp(static_cast<double>(m_fieldFertility[static_cast<size_t>(fi)]) + regen - deplete, 0.05, 1.0);
            m_fieldFertility[static_cast<size_t>(fi)] = static_cast<float>(nextF);
        }
    }

    // Eq19 extension: irrigation-capital stock update on the same field grid.
    if (m_ctx->config.researchSettlement.irrigationLoop) {
        std::vector<double> invest(static_cast<size_t>(nField), 0.0);
        for (const SettlementNode& n : m_nodes) {
            const int fi = fieldIndex(n.fieldX, n.fieldY);
            if (fi < 0 || fi >= nField) continue;
            const double farmShare = clamp01(n.mix[static_cast<size_t>(SubsistenceMode::Farming)]);
            invest[static_cast<size_t>(fi)] += std::max(0.0, n.irrigationCapital) * (0.0015 + 0.0025 * farmShare);
        }
        const double depr = std::clamp(m_ctx->config.researchSettlement.irrigationDepreciation, 0.0, 0.40);
        const double shield = std::max(0.0, m_ctx->config.researchSettlement.irrigationFertilityShield);
        for (int fi = 0; fi < nField; ++fi) {
            if (static_cast<size_t>(fi) >= landMask.size() || landMask[static_cast<size_t>(fi)] == 0u) {
                m_fieldIrrigationCapital[static_cast<size_t>(fi)] = 0.0f;
                continue;
            }
            double irr = static_cast<double>(m_fieldIrrigationCapital[static_cast<size_t>(fi)]);
            irr = std::clamp((1.0 - depr) * irr + invest[static_cast<size_t>(fi)], 0.0, 1.0);
            if (static_cast<int>(m_fieldRegime[static_cast<size_t>(fi)]) == kRegimeDrought) {
                const double fert = static_cast<double>(m_fieldFertility[static_cast<size_t>(fi)]);
                m_fieldFertility[static_cast<size_t>(fi)] = static_cast<float>(std::clamp(fert + 0.018 * shield * irr, 0.05, 1.0));
            }
            m_fieldIrrigationCapital[static_cast<size_t>(fi)] = static_cast<float>(irr);
        }
    }

    if (m_ctx->config.salinity.enabled) {
        const auto& sc = m_ctx->config.salinity;
        for (int fi = 0; fi < nField; ++fi) {
            if (static_cast<size_t>(fi) >= landMask.size() || landMask[static_cast<size_t>(fi)] == 0u) {
                m_fieldSalinity[static_cast<size_t>(fi)] = 0.0f;
                continue;
            }
            const double pAdj = (static_cast<size_t>(fi) < m_fieldPaleoPrecipAdj.size())
                ? static_cast<double>(m_fieldPaleoPrecipAdj[static_cast<size_t>(fi)])
                : 0.0;
            const double pMean = (static_cast<size_t>(fi) < precip.size())
                ? clamp01(static_cast<double>(precip[static_cast<size_t>(fi)]) + pAdj)
                : clamp01(0.50 + pAdj);
            const double arid = clamp01(1.0 - pMean);
            const double irr = (static_cast<size_t>(fi) < m_fieldIrrigationCapital.size())
                ? clamp01(static_cast<double>(m_fieldIrrigationCapital[static_cast<size_t>(fi)]))
                : 0.0;
            const double corr = (static_cast<size_t>(fi) < corridor.size())
                ? clamp01(static_cast<double>(corridor[static_cast<size_t>(fi)]) / 2.2)
                : 0.0;

            double infra = 0.0;
            if (static_cast<size_t>(fi) < owner.size()) {
                const int c = owner[static_cast<size_t>(fi)];
                if (c >= 0 && static_cast<size_t>(c) < countries.size()) {
                    const Country& country = countries[static_cast<size_t>(c)];
                    infra = clamp01(0.65 * clamp01(country.getInfraSpendingShare()) + 0.35 * clamp01(country.getAdminCapacity()));
                }
            }

            const double accum = std::max(0.0, sc.irrigationAccumulation) * irr *
                (1.0 + std::max(0.0, sc.aridityAccumulation) * arid);
            const double drainDrivers =
                std::max(0.0, sc.rainfallDrainageWeight) * pMean +
                std::max(0.0, sc.corridorDrainageWeight) * corr +
                std::max(0.0, sc.infraDrainageWeight) * infra;
            const double recover = std::max(0.0, sc.drainageRecovery) * clamp01(drainDrivers);
            const double prevSal = (static_cast<size_t>(fi) < m_fieldSalinity.size())
                ? clamp01(static_cast<double>(m_fieldSalinity[static_cast<size_t>(fi)]))
                : 0.0;
            const double sal = clamp01(prevSal + accum - recover);
            m_fieldSalinity[static_cast<size_t>(fi)] = static_cast<float>(sal);

            const double fert = clamp01(static_cast<double>(m_fieldFertility[static_cast<size_t>(fi)]));
            const double penalty = std::clamp(std::max(0.0, sc.yieldPenaltyMax) * sal, 0.0, 0.95);
            m_fieldFertility[static_cast<size_t>(fi)] = static_cast<float>(std::clamp(fert * (1.0 - penalty), 0.05, 1.0));
        }
    } else {
        std::fill(m_fieldSalinity.begin(), m_fieldSalinity.end(), 0.0f);
    }

    // Push climate/fertility multipliers into node productivity factors.
    for (size_t i = 0; i < m_nodes.size(); ++i) {
        SettlementNode& n = m_nodes[i];
        const int fi = fieldIndex(n.fieldX, n.fieldY);
        if (fi < 0 || fi >= nField) continue;
        const double fert = clamp01(static_cast<double>(m_fieldFertility[static_cast<size_t>(fi)]));
        const int regime = static_cast<int>(m_fieldRegime[static_cast<size_t>(fi)]);
        const double irr = (static_cast<size_t>(fi) < m_fieldIrrigationCapital.size())
            ? clamp01(static_cast<double>(m_fieldIrrigationCapital[static_cast<size_t>(fi)]))
            : 0.0;
        const double sal = (static_cast<size_t>(fi) < m_fieldSalinity.size())
            ? clamp01(static_cast<double>(m_fieldSalinity[static_cast<size_t>(fi)]))
            : 0.0;
        const double pAdj = (static_cast<size_t>(fi) < m_fieldPaleoPrecipAdj.size())
            ? static_cast<double>(m_fieldPaleoPrecipAdj[static_cast<size_t>(fi)])
            : 0.0;
        const double pMean = (static_cast<size_t>(fi) < precip.size())
            ? clamp01(static_cast<double>(precip[static_cast<size_t>(fi)]) + pAdj)
            : clamp01(0.50 + pAdj);

        double regimeWater = 1.0;
        if (regime == kRegimeDrought) regimeWater = 0.78;
        if (regime == kRegimePluvial) regimeWater = 1.08;
        if (regime == kRegimeCold) regimeWater = 0.86;

        const double irrWaterBoost = std::max(0.0, m_ctx->config.researchSettlement.irrigationWaterBoost);
        const double salPenalty = std::clamp(std::max(0.0, m_ctx->config.salinity.yieldPenaltyMax) * sal, 0.0, 0.95);
        n.soilFactor *= (0.65 + 0.75 * fert) * (1.0 + 0.18 * irr) * (1.0 - salPenalty);
        n.waterFactor *= (0.72 + 0.56 * pMean) * regimeWater * (1.0 + irrWaterBoost * irr);
        n.soilFactor = std::clamp(n.soilFactor, 0.25, 2.0);
        n.waterFactor = std::clamp(n.waterFactor, 0.25, 2.0);
        n.irrigationCapital = std::clamp(0.92 * n.irrigationCapital + 0.08 * irr, 0.0, 1.0);
    }
}

void SettlementSystem::updatePastoralMobilityRoutes(int year,
                                                    const Map& map,
                                                    const std::vector<Country>& countries) {
    const int nNode = static_cast<int>(m_nodes.size());
    m_nodePastoralSeasonGain.assign(static_cast<size_t>(nNode), 0.0);
    if (!m_ctx->config.researchSettlement.pastoralMobility || nNode <= 0) {
        return;
    }

    const auto& landMask = map.getFieldLandMask();
    const auto& precip = map.getFieldPrecipMean();
    const auto& temp = map.getFieldTempMean();
    const auto& corridor = map.getFieldCorridorWeight();

    const int routeRadius = std::max(2, m_ctx->config.researchSettlement.pastoralRouteRadius);
    const double moveShare = std::clamp(m_ctx->config.researchSettlement.pastoralMoveShare, 0.0, 0.25);
    const int season = ((year % 2) + 2) % 2; // 0=spring/summer, 1=autumn/winter

    std::vector<double> delta(static_cast<size_t>(nNode), 0.0);

    auto landScoreAt = [&](int fi, int seasonPhase) -> double {
        if (fi < 0 || static_cast<size_t>(fi) >= landMask.size() || landMask[static_cast<size_t>(fi)] == 0u) {
            return -1.0e9;
        }
        const double p = (static_cast<size_t>(fi) < precip.size()) ? clamp01(static_cast<double>(precip[static_cast<size_t>(fi)])) : 0.45;
        const double t = (static_cast<size_t>(fi) < temp.size()) ? static_cast<double>(temp[static_cast<size_t>(fi)]) : 12.0;
        const double c = (static_cast<size_t>(fi) < corridor.size()) ? clamp01(static_cast<double>(corridor[static_cast<size_t>(fi)]) / 2.0) : 0.5;
        const double temperate = 1.0 - clamp01(std::abs(t - 11.0) / 18.0);
        const double summerBias = clamp01((t - 8.0) / 18.0);
        const double winterBias = clamp01((14.0 - t) / 20.0);
        const double seasonTerm = (seasonPhase == 0) ? summerBias : winterBias;
        return 0.50 * p + 0.35 * temperate + 0.15 * c + 0.18 * seasonTerm;
    };

    for (int i = 0; i < nNode; ++i) {
        SettlementNode& src = m_nodes[static_cast<size_t>(i)];
        const double pastoralShare = clamp01(src.mix[static_cast<size_t>(SubsistenceMode::Pastoral)]);
        if (pastoralShare < 0.10 || src.population <= 20.0) continue;

        const int srcFi = fieldIndex(src.fieldX, src.fieldY);
        const double baseScore = landScoreAt(srcFi, season);
        if (!(baseScore > -1.0e8)) continue;

        double bestScore = baseScore;
        int bestFi = srcFi;
        for (int dy = -routeRadius; dy <= routeRadius; ++dy) {
            for (int dx = -routeRadius; dx <= routeRadius; ++dx) {
                const int nx = src.fieldX + dx;
                const int ny = src.fieldY + dy;
                if (nx < 0 || ny < 0 || nx >= m_fieldW || ny >= m_fieldH) continue;
                const int fi = fieldIndex(nx, ny);
                const double sc = landScoreAt(fi, season);
                if (sc > bestScore || (sc == bestScore && fi < bestFi)) {
                    bestScore = sc;
                    bestFi = fi;
                }
            }
        }

        const double gain = clamp01((bestScore - baseScore) + 0.22 * pastoralShare);
        m_nodePastoralSeasonGain[static_cast<size_t>(i)] = gain;

        const double move = std::max(0.0, src.population * pastoralShare * moveShare * gain);
        if (move <= 1.0) continue;

        int bestNode = -1;
        int bestDist = std::numeric_limits<int>::max();
        const int bfx = bestFi % std::max(1, m_fieldW);
        const int bfy = bestFi / std::max(1, m_fieldW);
        for (int j = 0; j < nNode; ++j) {
            if (j == i) continue;
            const SettlementNode& dst = m_nodes[static_cast<size_t>(j)];
            if (dst.ownerCountry != src.ownerCountry) continue;
            const int d = std::abs(dst.fieldX - bfx) + std::abs(dst.fieldY - bfy);
            if (d < bestDist || (d == bestDist && dst.id < m_nodes[static_cast<size_t>(bestNode < 0 ? j : bestNode)].id)) {
                bestDist = d;
                bestNode = j;
            }
        }
        if (bestNode < 0) continue;

        const double capped = std::min(move, 0.04 * src.population);
        delta[static_cast<size_t>(i)] -= capped;
        delta[static_cast<size_t>(bestNode)] += capped;
    }

    for (int i = 0; i < nNode; ++i) {
        SettlementNode& n = m_nodes[static_cast<size_t>(i)];
        n.population = std::max(0.0, n.population + delta[static_cast<size_t>(i)]);
        const double gain = (static_cast<size_t>(i) < m_nodePastoralSeasonGain.size()) ? m_nodePastoralSeasonGain[static_cast<size_t>(i)] : 0.0;
        n.waterFactor = std::clamp(n.waterFactor * (1.0 + 0.12 * gain), 0.25, 2.0);
        n.soilFactor = std::clamp(n.soilFactor * (1.0 + 0.08 * gain), 0.25, 2.0);
        if (n.ownerCountry >= 0 && static_cast<size_t>(n.ownerCountry) < countries.size()) {
            const double war = countries[static_cast<size_t>(n.ownerCountry)].isAtWar() ? 1.0 : 0.0;
            n.waterFactor = std::clamp(n.waterFactor * (1.0 - 0.05 * war), 0.25, 2.0);
        }
    }
}

void SettlementSystem::updateHouseholdsElitesExtraction(int year, std::vector<Country>& countries) {
    const int nNode = static_cast<int>(m_nodes.size());
    m_nodeExtractionRevenue.assign(static_cast<size_t>(nNode), 0.0);
    if (!m_ctx->config.researchSettlement.householdsExtraction || nNode <= 0) {
        return;
    }

    const int nCountry = static_cast<int>(countries.size());
    const double cal0 = std::max(1.0e-9, m_ctx->config.settlements.cal0);
    const double baseTau = std::clamp(m_ctx->config.researchSettlement.extractionBase, 0.0, 0.40);
    const double wAdmin = std::max(0.0, m_ctx->config.researchSettlement.extractionAdminWeight);
    const double wLegit = std::max(0.0, m_ctx->config.researchSettlement.extractionLegitimacyWeight);

    const double sShare = clamp01(m_ctx->config.researchSettlement.extractionStorageInvestShare);
    const double iShare = clamp01(m_ctx->config.researchSettlement.extractionIrrigationInvestShare);
    const double rShare = clamp01(m_ctx->config.researchSettlement.extractionRoadInvestShare);
    const double denom = std::max(1.0e-9, sShare + iShare + rShare);
    const double sAlloc = sShare / denom;
    const double iAlloc = iShare / denom;
    const double rAlloc = rShare / denom;

    std::vector<double> revByCountry(static_cast<size_t>(nCountry), 0.0);
    std::vector<double> popByCountry(static_cast<size_t>(nCountry), 0.0);
    std::vector<double> legitDelta(static_cast<size_t>(nCountry), 0.0);
    std::vector<double> controlDelta(static_cast<size_t>(nCountry), 0.0);

    for (int i = 0; i < nNode; ++i) {
        SettlementNode& n = m_nodes[static_cast<size_t>(i)];
        const double pop = std::max(0.0, n.population);
        if (pop <= 1.0) continue;
        const double subsistenceNeed = pop * cal0;
        const double surplus = std::max(0.0, n.calories - subsistenceNeed);

        double cAdmin = 0.30;
        double cLegit = 0.45;
        if (n.ownerCountry >= 0 && static_cast<size_t>(n.ownerCountry) < countries.size()) {
            cAdmin = clamp01(countries[static_cast<size_t>(n.ownerCountry)].getAdminCapacity());
            cLegit = clamp01(countries[static_cast<size_t>(n.ownerCountry)].getLegitimacy());
        }

        const double admin = clamp01(0.65 * n.localAdminCapacity + 0.35 * cAdmin);
        const double legit = clamp01(0.60 * n.localLegitimacy + 0.40 * cLegit);
        const double tauCap = std::clamp(baseTau + wAdmin * admin + wLegit * legit, 0.0, 0.55);
        const double targetTau = std::clamp(tauCap * (0.55 + 0.45 * (0.4 + n.eliteShare)), 0.0, 0.55);
        n.extractionRate = std::clamp(0.80 * n.extractionRate + 0.20 * targetTau, 0.0, 0.60);

        const double revenue = n.extractionRate * surplus;
        m_nodeExtractionRevenue[static_cast<size_t>(i)] = revenue;

        const double investStorage = revenue * sAlloc;
        const double investIrr = revenue * iAlloc;
        const double investRoad = revenue * rAlloc;
        const double eliteCons = std::max(0.0, revenue - investStorage - investIrr - investRoad);

        n.storageStock = std::clamp(n.storageStock + 0.0018 * investStorage, 0.0, 1.9);
        n.irrigationCapital = std::clamp(n.irrigationCapital + 0.0023 * investIrr, 0.0, 1.0);
        n.localAdminCapacity = std::clamp(n.localAdminCapacity + 0.00065 * investRoad - 0.00022 * eliteCons, 0.0, 1.0);
        n.localLegitimacy = std::clamp(
            n.localLegitimacy +
            0.00055 * (investStorage + investIrr + 0.5 * investRoad) -
            0.00065 * eliteCons -
            0.00045 * revenue,
            0.0, 1.0);
        n.eliteShare = std::clamp(0.96 * n.eliteShare + 0.04 * clamp01(0.35 + 0.7 * (eliteCons / std::max(1.0, revenue))), 0.02, 0.95);

        if (n.ownerCountry >= 0 && static_cast<size_t>(n.ownerCountry) < revByCountry.size()) {
            revByCountry[static_cast<size_t>(n.ownerCountry)] += revenue;
            popByCountry[static_cast<size_t>(n.ownerCountry)] += pop;
            legitDelta[static_cast<size_t>(n.ownerCountry)] += pop * (n.localLegitimacy - 0.5);
            controlDelta[static_cast<size_t>(n.ownerCountry)] += pop * (n.localAdminCapacity - 0.45);
        }
    }

    for (int ci = 0; ci < nCountry; ++ci) {
        const double popW = std::max(1.0, popByCountry[static_cast<size_t>(ci)]);
        Country& c = countries[static_cast<size_t>(ci)];
        Country::MacroEconomyState& m = c.getMacroEconomyMutable();
        m.netRevenue += revByCountry[static_cast<size_t>(ci)];
        m.institutionCapacity = clamp01(m.institutionCapacity + 1.0e-7 * revByCountry[static_cast<size_t>(ci)]);
        const double avgLeg = legitDelta[static_cast<size_t>(ci)] / popW;
        const double avgCtl = controlDelta[static_cast<size_t>(ci)] / popW;
        c.setLegitimacy(c.getLegitimacy() + 0.010 * avgLeg);
        c.setAvgControl(c.getAvgControl() + 0.012 * avgCtl);
        c.setTaxRate(c.getTaxRate() + 0.0015 * clamp01(revByCountry[static_cast<size_t>(ci)] / popW));
    }

    (void)year;
}

void SettlementSystem::recomputeFoodCaloriesAndCapacity(const Map& map,
                                                        const std::vector<Country>& countries) {
    const auto& food = map.getFieldFoodPotential();
    const auto& precip = map.getFieldPrecipMean();
    const auto& temp = map.getFieldTempMean();

    const auto& pkgs = getDefaultDomesticPackages();
    const double kBasePerFood = std::max(1.0, m_ctx->config.settlements.kBasePerFoodUnit);

    for (size_t i = 0; i < m_nodes.size(); ++i) {
        SettlementNode& n = m_nodes[i];
        const int fi = fieldIndex(n.fieldX, n.fieldY);
        const double baseFood = (fi >= 0 && static_cast<size_t>(fi) < food.size()) ? std::max(0.0, static_cast<double>(food[static_cast<size_t>(fi)])) : 0.0;
        const double water = (fi >= 0 && static_cast<size_t>(fi) < precip.size()) ? clamp01(0.20 + static_cast<double>(precip[static_cast<size_t>(fi)])) : 0.50;
        const double cold = (fi >= 0 && static_cast<size_t>(fi) < temp.size()) ? clamp01((8.0 - static_cast<double>(temp[static_cast<size_t>(fi)])) / 15.0) : 0.0;

        const double modeMul =
            n.mix[static_cast<size_t>(SubsistenceMode::Foraging)] * 0.86 +
            n.mix[static_cast<size_t>(SubsistenceMode::Farming)] * 1.18 +
            n.mix[static_cast<size_t>(SubsistenceMode::Pastoral)] * 0.95 +
            n.mix[static_cast<size_t>(SubsistenceMode::Fishing)] * 1.08 +
            n.mix[static_cast<size_t>(SubsistenceMode::Craft)] * 0.24;
        const double pastoralSeasonGain =
            (i < m_nodePastoralSeasonGain.size()) ? clamp01(m_nodePastoralSeasonGain[i]) : 0.0;
        const double irrigationBoost = 1.0 + 0.30 * clamp01(n.irrigationCapital);

        double pkgModeMul = 1.0;
        double pkgStorage = 0.0;
        for (int pkgId : n.adoptedPackages) {
            if (pkgId < 0 || static_cast<size_t>(pkgId) >= pkgs.size()) continue;
            const DomesticPackageDefinition& pkg = pkgs[static_cast<size_t>(pkgId)];
            const double weightedMul =
                n.mix[static_cast<size_t>(SubsistenceMode::Foraging)] * pkg.foragingMul +
                n.mix[static_cast<size_t>(SubsistenceMode::Farming)] * pkg.farmingMul +
                n.mix[static_cast<size_t>(SubsistenceMode::Pastoral)] * pkg.pastoralMul +
                n.mix[static_cast<size_t>(SubsistenceMode::Fishing)] * pkg.fishingMul +
                n.mix[static_cast<size_t>(SubsistenceMode::Craft)] * (1.0 + 0.25 * pkg.marketAffinity);
            pkgModeMul *= std::max(0.45, weightedMul);
            pkgStorage += std::max(0.0, pkg.storageBonus);
        }

        n.storageStock = std::clamp(0.96 * n.storageStock + pkgStorage, 0.0, 1.8);
        const double storageFactor = 1.0 + n.storageStock;

        n.carryingCapacity = std::max(
            80.0,
            baseFood * kBasePerFood * n.techFactor * n.soilFactor * n.waterFactor * storageFactor * irrigationBoost);

        n.foodProduced =
            baseFood * std::max(0.15, modeMul) * std::max(0.2, pkgModeMul) *
            std::max(0.1, n.techFactor) * std::max(0.1, n.soilFactor) * std::max(0.1, n.waterFactor) *
            std::max(0.2, 1.0 - 0.22 * cold) * (1.0 + 0.12 * pastoralSeasonGain) *
            0.045;

        n.foodImported = 0.0;
        n.foodExported = 0.0;
        n.calories = std::max(0.0, n.foodProduced);

        if (n.ownerCountry >= 0 && static_cast<size_t>(n.ownerCountry) < countries.size()) {
            const auto& m = countries[static_cast<size_t>(n.ownerCountry)].getMacroEconomy();
            // Keep coarse linkage to existing macro shocks while preserving deterministic behavior.
            const double stress = clamp01(0.55 * m.famineSeverity + 0.45 * m.diseaseBurden);
            n.carryingCapacity *= std::max(0.55, 1.0 - 0.20 * stress);
        }
    }
}

void SettlementSystem::updateSettlementDisease(int year,
                                               const Map& map,
                                               const std::vector<Country>& countries) {
    const int nNode = static_cast<int>(m_nodes.size());
    if (nNode <= 0) {
        m_nodeS.clear();
        m_nodeI.clear();
        m_nodeR.clear();
        m_nodeDiseaseBurden.clear();
        m_nodeImportedInfection.clear();
        return;
    }

    const double i0 = std::clamp(m_ctx->config.disease.initialInfectedShare, 0.0, 0.25);
    const double r0 = std::clamp(m_ctx->config.disease.initialRecoveredShare, 0.0, 0.95);
    const double s0 = std::max(0.0, 1.0 - i0 - r0);

    const size_t oldN = m_nodeS.size();
    m_nodeS.resize(static_cast<size_t>(nNode), s0);
    m_nodeI.resize(static_cast<size_t>(nNode), i0);
    m_nodeR.resize(static_cast<size_t>(nNode), r0);
    m_nodeDiseaseBurden.resize(static_cast<size_t>(nNode), 0.0);
    m_nodeImportedInfection.assign(static_cast<size_t>(nNode), 0.0);
    if (oldN < static_cast<size_t>(nNode)) {
        for (size_t i = oldN; i < static_cast<size_t>(nNode); ++i) {
            m_nodeS[i] = s0;
            m_nodeI[i] = i0;
            m_nodeR[i] = r0;
        }
    }

    const auto& precip = map.getFieldPrecipMean();
    const auto& corridor = map.getFieldCorridorWeight();
    std::vector<double> betaEff(static_cast<size_t>(nNode), 0.0);

    // Imported infection pressure from settlement connectivity.
    for (const TransportEdge& e : m_edges) {
        if (e.fromNode < 0 || e.toNode < 0 || e.fromNode >= nNode || e.toNode >= nNode) continue;
        const SettlementNode& a = m_nodes[static_cast<size_t>(e.fromNode)];
        const SettlementNode& b = m_nodes[static_cast<size_t>(e.toNode)];
        const double fracA = clamp01(m_nodeI[static_cast<size_t>(e.fromNode)]);
        const double fracB = clamp01(m_nodeI[static_cast<size_t>(e.toNode)]);
        const double cap = std::max(0.0, e.capacity * e.reliability);
        const double flow = std::min(cap, 0.015 * std::sqrt(std::max(1.0, a.population) * std::max(1.0, b.population)));
        if (flow <= 0.0) continue;
        m_nodeImportedInfection[static_cast<size_t>(e.toNode)] += flow * fracA / std::max(1.0, b.population);
        m_nodeImportedInfection[static_cast<size_t>(e.fromNode)] += flow * fracB / std::max(1.0, a.population);
    }

    const int subSteps = 4;
    const double dt = 1.0 / static_cast<double>(subSteps);
    const double gammaBase = std::clamp(0.06 + 0.20 * clamp01(m_ctx->config.disease.endemicInstitutionMitigation), 0.02, 0.30);
    const double betaBase = std::clamp(0.10 + 0.70 * std::max(0.0, m_ctx->config.disease.endemicBase), 0.08, 1.80);

    for (int i = 0; i < nNode; ++i) {
        const SettlementNode& n = m_nodes[static_cast<size_t>(i)];
        const int fi = fieldIndex(n.fieldX, n.fieldY);
        const double dens = clamp01(n.population / std::max(50.0, n.carryingCapacity));
        const double humid = (fi >= 0 && static_cast<size_t>(fi) < precip.size()) ? clamp01(static_cast<double>(precip[static_cast<size_t>(fi)])) : 0.50;
        const double corr = (fi >= 0 && static_cast<size_t>(fi) < corridor.size()) ? clamp01(static_cast<double>(corridor[static_cast<size_t>(fi)]) / 2.0) : 0.50;
        const double climateFactor = 0.65 + 0.45 * humid;
        const double densityFactor = 0.60 + 0.90 * dens;
        const double connectFactor = 0.75 + 0.40 * corr;
        betaEff[static_cast<size_t>(i)] = betaBase * densityFactor * climateFactor * connectFactor;
        m_nodeImportedInfection[static_cast<size_t>(i)] = std::clamp(m_nodeImportedInfection[static_cast<size_t>(i)], 0.0, 0.60);
    }

    bool usedGpu = false;
    if (m_gpu && m_gpu->enabled && m_gpu->diseaseKernel && m_ctx->config.researchGpu.settlementDisease) {
        const double betaScale = 2.0;
        const double inflowScale = 0.60;
        const double gammaScale = 0.40;

        std::vector<sf::Uint8> statePx(static_cast<size_t>(nNode) * 4u, 0u);
        std::vector<sf::Uint8> paramPx(static_cast<size_t>(nNode) * 4u, 0u);
        for (int i = 0; i < nNode; ++i) {
            const size_t p = static_cast<size_t>(i) * 4u;
            statePx[p + 0u] = static_cast<sf::Uint8>(std::round(clamp01(m_nodeS[static_cast<size_t>(i)]) * 255.0));
            statePx[p + 1u] = static_cast<sf::Uint8>(std::round(clamp01(m_nodeI[static_cast<size_t>(i)]) * 255.0));
            statePx[p + 2u] = static_cast<sf::Uint8>(std::round(clamp01(m_nodeR[static_cast<size_t>(i)]) * 255.0));
            statePx[p + 3u] = 255u;

            paramPx[p + 0u] = static_cast<sf::Uint8>(std::round(clamp01(betaEff[static_cast<size_t>(i)] / betaScale) * 255.0));
            paramPx[p + 1u] = static_cast<sf::Uint8>(std::round(clamp01(m_nodeImportedInfection[static_cast<size_t>(i)] / inflowScale) * 255.0));
            paramPx[p + 2u] = static_cast<sf::Uint8>(std::round(clamp01(gammaBase / gammaScale) * 255.0));
            paramPx[p + 3u] = static_cast<sf::Uint8>(std::round(clamp01(dt) * 255.0));
        }

        sf::Texture paramTex;
        if (paramTex.create(static_cast<unsigned>(nNode), 1u)) {
            paramTex.setSmooth(false);
            paramTex.update(paramPx.data());
            std::vector<sf::Uint8> outPx;
            for (int s = 0; s < subSteps; ++s) {
                sf::Texture stateTex;
                if (!stateTex.create(static_cast<unsigned>(nNode), 1u)) {
                    outPx.clear();
                    break;
                }
                stateTex.setSmooth(false);
                stateTex.update(statePx.data());

                m_gpu->diseaseShader.setUniform("stateTex", stateTex);
                m_gpu->diseaseShader.setUniform("paramTex", paramTex);
                m_gpu->diseaseShader.setUniform("betaScale", static_cast<float>(betaScale));
                m_gpu->diseaseShader.setUniform("inflowScale", static_cast<float>(inflowScale));
                m_gpu->diseaseShader.setUniform("gammaScale", static_cast<float>(gammaScale));

                if (!runShaderPass1D(m_gpu->diseaseShader, stateTex, nNode, outPx)) {
                    outPx.clear();
                    break;
                }
                statePx.swap(outPx);
            }
            if (!statePx.empty()) {
                usedGpu = true;
                for (int i = 0; i < nNode; ++i) {
                    const size_t p = static_cast<size_t>(i) * 4u;
                    m_nodeS[static_cast<size_t>(i)] = static_cast<double>(statePx[p + 0u]) / 255.0;
                    m_nodeI[static_cast<size_t>(i)] = static_cast<double>(statePx[p + 1u]) / 255.0;
                    m_nodeR[static_cast<size_t>(i)] = static_cast<double>(statePx[p + 2u]) / 255.0;
                }
            }
        }
    }

    if (!usedGpu) {
        for (int s = 0; s < subSteps; ++s) {
            for (int i = 0; i < nNode; ++i) {
                const size_t ii = static_cast<size_t>(i);
                const double S = clamp01(m_nodeS[ii]);
                const double I = clamp01(m_nodeI[ii]);
                const double R = clamp01(m_nodeR[ii]);
                const double infPressure = clamp01(I + m_nodeImportedInfection[ii]);
                const double newInf = std::min(S, betaEff[ii] * S * infPressure * dt);
                const double newRec = std::min(I + newInf, gammaBase * I * dt);
                double Sn = std::max(0.0, S - newInf);
                double In = std::max(0.0, I + newInf - newRec);
                double Rn = std::max(0.0, R + newRec);
                const double sum = Sn + In + Rn;
                if (sum > 1.0e-9) {
                    Sn /= sum;
                    In /= sum;
                    Rn /= sum;
                } else {
                    Sn = 1.0;
                    In = 0.0;
                    Rn = 0.0;
                }
                m_nodeS[ii] = Sn;
                m_nodeI[ii] = In;
                m_nodeR[ii] = Rn;
            }
        }
    }

    for (int i = 0; i < nNode; ++i) {
        m_nodeDiseaseBurden[static_cast<size_t>(i)] = clamp01(m_nodeI[static_cast<size_t>(i)]);
        if (m_nodes[static_cast<size_t>(i)].ownerCountry >= 0 &&
            static_cast<size_t>(m_nodes[static_cast<size_t>(i)].ownerCountry) < countries.size()) {
            const auto& m = countries[static_cast<size_t>(m_nodes[static_cast<size_t>(i)].ownerCountry)].getMacroEconomy();
            m_nodeDiseaseBurden[static_cast<size_t>(i)] =
                clamp01(0.65 * m_nodeDiseaseBurden[static_cast<size_t>(i)] + 0.35 * clamp01(m.diseaseBurden));
        }
    }

    (void)year;
}

void SettlementSystem::applyGrowthAndSpecialization(int year, const std::vector<Country>& countries) {
    const double rMin = m_ctx->config.settlements.growthRMin;
    const double rMax = m_ctx->config.settlements.growthRMax;
    const double cal0 = std::max(1.0e-9, m_ctx->config.settlements.cal0);
    const double calSlope = std::max(1.0e-9, m_ctx->config.settlements.calSlope);

    for (size_t i = 0; i < m_nodes.size(); ++i) {
        SettlementNode& n = m_nodes[i];
        const double pop = std::max(0.0, n.population);
        if (pop <= 1.0) {
            n.population = 0.0;
            continue;
        }

        const double perCap = n.calories / std::max(1.0, pop);
        const double r = rMin + (rMax - rMin) * sigmoid((perCap - cal0) / calSlope);

        double shockFrac = 0.0;
        const double localDisease =
            (i < m_nodeDiseaseBurden.size()) ? clamp01(m_nodeDiseaseBurden[i]) : 0.0;
        const double localWarAttr =
            (i < m_nodeWarAttrition.size()) ? clamp01(m_nodeWarAttrition[i]) : 0.0;
        if (n.ownerCountry >= 0 && static_cast<size_t>(n.ownerCountry) < countries.size()) {
            const Country& c = countries[static_cast<size_t>(n.ownerCountry)];
            const auto& m = c.getMacroEconomy();
            shockFrac += 0.06 * clamp01(m.famineSeverity);
            shockFrac += 0.03 * clamp01(m.diseaseBurden);
            shockFrac += 0.05 * localDisease;
            shockFrac += std::max(0.0, m_ctx->config.researchSettlement.campaignNodeShockScale) * localWarAttr;
            if (c.isAtWar()) {
                shockFrac += 0.03;
            }
        }

        const double K = std::max(1.0, n.carryingCapacity);
        const double growth = pop * r * (1.0 - pop / K);
        const double shock = pop * shockFrac;
        n.population = std::max(0.0, pop + growth - shock);

        const double marketNorm = (i < m_nodeMarketPotential.size()) ? clamp01(m_nodeMarketPotential[i] / 70.0) : 0.0;
        const double subsistenceRisk = clamp01((cal0 - perCap) / std::max(cal0, 1.0e-9));
        const double dSpec =
            std::max(0.0, m_ctx->config.transport.specialistEta) * marketNorm -
            std::max(0.0, m_ctx->config.transport.specialistLambda) * subsistenceRisk;
        n.specialistShare = clamp01(n.specialistShare + dSpec);

        // Deterministic tiny tie-break damping to prevent perfectly synchronized oscillations.
        const std::uint64_t u = SimulationContext::mix64(
            m_ctx->worldSeed ^
            (static_cast<std::uint64_t>(year + 30000) * 0x9E3779B97F4A7C15ull) ^
            (static_cast<std::uint64_t>(n.id + 7) * 0xBF58476D1CE4E5B9ull));
        const double damping = 1.0 - 0.002 * (SimulationContext::u01FromU64(u));
        n.population *= damping;
    }
}

void SettlementSystem::applyFission(int year,
                                    const Map& map,
                                    const std::vector<Country>& countries) {
    const int globalCap = std::max(1, m_ctx->config.settlements.maxNodesGlobal);
    if (static_cast<int>(m_nodes.size()) >= globalCap) {
        return;
    }

    const auto& landMask = map.getFieldLandMask();
    const auto& owner = map.getFieldOwnerId();
    const auto& food = map.getFieldFoodPotential();
    const auto& move = map.getFieldMoveCost();
    const auto& corridor = map.getFieldCorridorWeight();

    const int countryCount = static_cast<int>(countries.size());
    m_lastFissionConservationError = 0.0;
    std::vector<int> nodesByCountry(static_cast<size_t>(countryCount), 0);
    std::vector<int> occupied(static_cast<size_t>(std::max(0, m_fieldW * m_fieldH)), 0);
    for (const SettlementNode& n : m_nodes) {
        if (n.ownerCountry >= 0 && n.ownerCountry < countryCount) {
            nodesByCountry[static_cast<size_t>(n.ownerCountry)] += 1;
        }
        const int fi = fieldIndex(n.fieldX, n.fieldY);
        if (fi >= 0 && static_cast<size_t>(fi) < occupied.size()) {
            occupied[static_cast<size_t>(fi)] = 1;
        }
    }

    const int perCountryCap = std::max(1, m_ctx->config.settlements.maxNodesPerCountry);
    const int spacing = std::max(1, m_ctx->config.settlements.splitMinSpacingFields);
    const int cooldown = std::max(1, m_ctx->config.settlements.splitCooldownYears);
    const double threshold = std::max(1000.0, m_ctx->config.settlements.splitPopThreshold);

    const size_t originalCount = m_nodes.size();
    for (size_t ni = 0; ni < originalCount; ++ni) {
        if (static_cast<int>(m_nodes.size()) >= globalCap) break;

        SettlementNode& parent = m_nodes[ni];
        if (parent.ownerCountry < 0 || parent.ownerCountry >= countryCount) continue;
        if (nodesByCountry[static_cast<size_t>(parent.ownerCountry)] >= perCountryCap) continue;
        if (parent.population <= threshold) continue;
        if ((year - parent.lastSplitYear) < cooldown) continue;

        const int fx = parent.fieldX;
        const int fy = parent.fieldY;
        double bestScore = -std::numeric_limits<double>::infinity();
        int bestFi = -1;

        for (int r = spacing; r <= spacing + 5; ++r) {
            for (int dy = -r; dy <= r; ++dy) {
                for (int dx = -r; dx <= r; ++dx) {
                    if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                    const int nx = fx + dx;
                    const int ny = fy + dy;
                    if (nx < 0 || ny < 0 || nx >= m_fieldW || ny >= m_fieldH) continue;
                    const int fi = fieldIndex(nx, ny);
                    if (fi < 0 || static_cast<size_t>(fi) >= landMask.size()) continue;
                    if (landMask[static_cast<size_t>(fi)] == 0u) continue;
                    if (static_cast<size_t>(fi) < owner.size() && owner[static_cast<size_t>(fi)] != parent.ownerCountry) continue;
                    if (static_cast<size_t>(fi) < occupied.size() && occupied[static_cast<size_t>(fi)] != 0) continue;

                    bool tooClose = false;
                    for (const SettlementNode& n : m_nodes) {
                        if (n.ownerCountry != parent.ownerCountry) continue;
                        if (std::max(std::abs(n.fieldX - nx), std::abs(n.fieldY - ny)) < spacing) {
                            tooClose = true;
                            break;
                        }
                    }
                    if (tooClose) continue;

                    const double fp = (static_cast<size_t>(fi) < food.size()) ? std::max(0.0, static_cast<double>(food[static_cast<size_t>(fi)])) : 0.0;
                    const double mv = (static_cast<size_t>(fi) < move.size()) ? finiteOr(static_cast<double>(move[static_cast<size_t>(fi)]), 2.0) : 2.0;
                    if (!std::isfinite(mv) || mv <= 0.0) continue;
                    const double cor = (static_cast<size_t>(fi) < corridor.size()) ? std::max(0.01, static_cast<double>(corridor[static_cast<size_t>(fi)])) : 1.0;
                    const double score = fp * cor / std::max(0.1, mv);
                    if (score > bestScore || (score == bestScore && fi < bestFi)) {
                        bestScore = score;
                        bestFi = fi;
                    }
                }
            }
            if (bestFi >= 0) break;
        }

        if (bestFi < 0) {
            continue;
        }

        const std::uint64_t splitBits = SimulationContext::mix64(
            m_ctx->worldSeed ^
            (static_cast<std::uint64_t>(parent.id + 1) * 0x9E3779B97F4A7C15ull) ^
            (static_cast<std::uint64_t>(year + 25000) * 0xBF58476D1CE4E5B9ull));
        const double u = SimulationContext::u01FromU64(splitBits);
        const double alphaMin = std::clamp(m_ctx->config.settlements.splitAlphaMin, 0.05, 0.90);
        const double alphaMax = std::clamp(m_ctx->config.settlements.splitAlphaMax, 0.05, 0.90);
        const double alpha = alphaMin + (alphaMax - alphaMin) * u;
        const double childPop = parent.population * alpha;
        if (childPop < std::max(100.0, m_ctx->config.settlements.initNodeMinPop * 0.5)) {
            continue;
        }

        const double parentBefore = parent.population;
        parent.population -= childPop;
        parent.lastSplitYear = year;

        SettlementNode child = parent;
        child.id = m_nextNodeId++;
        child.fieldX = bestFi % m_fieldW;
        child.fieldY = bestFi / m_fieldW;
        child.population = childPop;
        child.foundedYear = year;
        child.lastSplitYear = year;

        m_nodes.push_back(child);
        m_lastFissionConservationError += std::abs(parentBefore - (parent.population + child.population));
        if (static_cast<size_t>(bestFi) < occupied.size()) {
            occupied[static_cast<size_t>(bestFi)] = 1;
        }
        nodesByCountry[static_cast<size_t>(parent.ownerCountry)] += 1;
    }

    std::sort(m_nodes.begin(), m_nodes.end(), [](const SettlementNode& a, const SettlementNode& b) {
        if (a.id != b.id) return a.id < b.id;
        if (a.fieldY != b.fieldY) return a.fieldY < b.fieldY;
        return a.fieldX < b.fieldX;
    });
}

void SettlementSystem::rebuildTransportGraph(int year,
                                             const Map& map,
                                             const std::vector<Country>& countries) {
    const int n = static_cast<int>(m_nodes.size());
    if (n <= 1) {
        m_edges.clear();
        m_edgeExplorationBoost.clear();
        return;
    }

    struct RegimeCaps {
        double land = 0.0;
        double sea = 0.0;
    };
    std::vector<RegimeCaps> regimeByCountry(countries.size(), RegimeCaps{});
    double avgLandCap = 0.0;
    double avgSeaCap = 0.0;
    if (m_ctx->config.transportRegimes.enabled) {
        const auto& tr = m_ctx->config.transportRegimes;
        const double wRoad = std::max(0.0, tr.roadInfraWeight);
        const double wPort = std::max(0.0, tr.portInfraWeight);
        const double wLogi = std::max(0.0, tr.logisticsWeight);
        const double wMarket = std::max(0.0, tr.marketWeight);
        const double wBase = std::max(1.0e-9, wRoad + wPort + wLogi + wMarket);
        for (size_t ci = 0; ci < countries.size(); ++ci) {
            const Country& c = countries[ci];
            const double popM = std::max(0.05, static_cast<double>(std::max<long long>(1, c.getPopulation())) / 1'000'000.0);
            const double roadNorm = clamp01(static_cast<double>(c.getRoads().size()) / (80.0 * std::sqrt(popM) + 10.0));
            const double portNorm = clamp01(static_cast<double>(c.getPorts().size()) / (24.0 * std::sqrt(popM) + 4.0));
            const double logi = clamp01(c.getLogisticsReach());
            const double market = clamp01(c.getMacroEconomy().marketAccess);

            double land = (wRoad * roadNorm + wLogi * logi + wMarket * market) / wBase;
            double sea = (wPort * portNorm + wLogi * logi + wMarket * market) / wBase;

            if (land >= tr.packThreshold) land = std::min(1.0, land + 0.10);
            if (land >= tr.tractionThreshold) land = std::min(1.0, land + 0.12);
            if (land >= tr.wheelThreshold) land = std::min(1.0, land + 0.15);
            if (sea >= tr.maritimeThreshold) sea = std::min(1.0, sea + 0.20);

            regimeByCountry[ci].land = clamp01(land);
            regimeByCountry[ci].sea = clamp01(sea);
            avgLandCap += regimeByCountry[ci].land;
            avgSeaCap += regimeByCountry[ci].sea;
        }
        if (!countries.empty()) {
            avgLandCap /= static_cast<double>(countries.size());
            avgSeaCap /= static_cast<double>(countries.size());
        }
    }

    auto edgeRegimeCap = [&](int ownerA, int ownerB, bool seaLink) -> double {
        if (!m_ctx->config.transportRegimes.enabled || countries.empty()) {
            return 0.0;
        }
        auto nodeCap = [&](int owner, bool sea) {
            if (owner >= 0 && static_cast<size_t>(owner) < regimeByCountry.size()) {
                return sea ? regimeByCountry[static_cast<size_t>(owner)].sea
                           : regimeByCountry[static_cast<size_t>(owner)].land;
            }
            return sea ? avgSeaCap : avgLandCap;
        };
        const double ca = nodeCap(ownerA, seaLink);
        const double cb = nodeCap(ownerB, seaLink);
        return clamp01(0.5 * (ca + cb));
    };

    const int interval = std::max(1, m_ctx->config.settlements.transportRebuildIntervalYears);
    if (!m_edges.empty() && (year % interval != 0)) {
        for (TransportEdge& e : m_edges) {
            if (e.fromNode < 0 || e.toNode < 0 ||
                e.fromNode >= static_cast<int>(m_nodes.size()) ||
                e.toNode >= static_cast<int>(m_nodes.size())) {
                continue;
            }
            const SettlementNode& a = m_nodes[static_cast<size_t>(e.fromNode)];
            const SettlementNode& b = m_nodes[static_cast<size_t>(e.toNode)];
            const double regime = edgeRegimeCap(a.ownerCountry, b.ownerCountry, e.seaLink);
            const double capMul =
                1.0 + regime * (std::max(1.0, m_ctx->config.transportRegimes.capacityMaxMult) - 1.0);
            const double relAdd = regime * std::clamp(m_ctx->config.transportRegimes.reliabilityMaxAdd, 0.0, 1.0);
            e.capacity = capMul *
                (24.0 + 0.06 * std::sqrt(std::max(1.0, a.population) * std::max(1.0, b.population))) /
                (1.0 + 0.08 * std::max(0.0, e.cost));
            e.reliability = std::clamp(1.0 / (1.0 + 0.06 * std::max(0.0, e.cost)) + relAdd, 0.05, 1.0);
        }
        m_edgeExplorationBoost.assign(m_edges.size(), 0.0);
        return;
    }
    m_edges.clear();

    const auto& move = map.getFieldMoveCost();
    const auto& corridor = map.getFieldCorridorWeight();
    const auto& landMask = map.getFieldLandMask();

    const int kNearest = std::max(1, m_ctx->config.transport.kNearest);
    const double maxEdgeCost = std::max(1.0, m_ctx->config.transport.maxEdgeCost);
    const double landMult = std::max(0.01, m_ctx->config.transport.landCostMult);
    const double seaMult = std::max(0.01, m_ctx->config.transport.seaCostMult);
    const double borderFriction = std::max(0.01, m_ctx->config.transport.borderFriction);
    const double warRiskMult = std::max(0.01, m_ctx->config.transport.warRiskMult);

    std::vector<std::vector<CandidateLink>> cand(static_cast<size_t>(n));
    std::vector<uint8_t> coastal(static_cast<size_t>(n), 0u);

    for (int i = 0; i < n; ++i) {
        const SettlementNode& node = m_nodes[static_cast<size_t>(i)];
        bool isCoastal = false;
        for (int d = 0; d < 4 && !isCoastal; ++d) {
            const int nx = node.fieldX + ((d == 0) ? 1 : (d == 1) ? -1 : 0);
            const int ny = node.fieldY + ((d == 2) ? 1 : (d == 3) ? -1 : 0);
            if (nx < 0 || ny < 0 || nx >= m_fieldW || ny >= m_fieldH) {
                isCoastal = true;
                continue;
            }
            const int fi = fieldIndex(nx, ny);
            if (fi < 0 || static_cast<size_t>(fi) >= landMask.size() || landMask[static_cast<size_t>(fi)] == 0u) {
                isCoastal = true;
            }
        }
        coastal[static_cast<size_t>(i)] = isCoastal ? 1u : 0u;
    }

    const bool pathRebuild = m_ctx->config.researchSettlement.transportPathRebuild;
    if (pathRebuild) {
        const int nField = std::max(0, m_fieldW * m_fieldH);
        std::vector<int> nodeByField(static_cast<size_t>(nField), -1);
        for (int i = 0; i < n; ++i) {
            const int fi = fieldIndex(m_nodes[static_cast<size_t>(i)].fieldX, m_nodes[static_cast<size_t>(i)].fieldY);
            if (fi < 0 || fi >= nField) continue;
            const int old = nodeByField[static_cast<size_t>(fi)];
            if (old < 0 ||
                m_nodes[static_cast<size_t>(i)].population > m_nodes[static_cast<size_t>(old)].population ||
                (m_nodes[static_cast<size_t>(i)].population == m_nodes[static_cast<size_t>(old)].population &&
                 m_nodes[static_cast<size_t>(i)].id < m_nodes[static_cast<size_t>(old)].id)) {
                nodeByField[static_cast<size_t>(fi)] = i;
            }
        }

        struct QItem {
            double d = 0.0;
            int fi = -1;
        };
        struct QCmp {
            bool operator()(const QItem& a, const QItem& b) const {
                if (a.d != b.d) return a.d > b.d;
                return a.fi > b.fi;
            }
        };
        const std::array<int, 8> dx{{1, -1, 0, 0, 1, 1, -1, -1}};
        const std::array<int, 8> dy{{0, 0, 1, -1, 1, -1, 1, -1}};

        for (int i = 0; i < n; ++i) {
            const SettlementNode& a = m_nodes[static_cast<size_t>(i)];
            const int srcFi = fieldIndex(a.fieldX, a.fieldY);
            if (srcFi < 0 || srcFi >= nField) continue;
            if (static_cast<size_t>(srcFi) >= landMask.size() || landMask[static_cast<size_t>(srcFi)] == 0u) continue;

            std::vector<double> dist(static_cast<size_t>(nField), std::numeric_limits<double>::infinity());
            std::vector<double> bestToNode(static_cast<size_t>(n), std::numeric_limits<double>::infinity());
            std::priority_queue<QItem, std::vector<QItem>, QCmp> pq;
            dist[static_cast<size_t>(srcFi)] = 0.0;
            pq.push(QItem{0.0, srcFi});

            while (!pq.empty()) {
                const QItem q = pq.top();
                pq.pop();
                if (q.fi < 0 || q.fi >= nField) continue;
                if (q.d > dist[static_cast<size_t>(q.fi)] + 1.0e-12) continue;
                if (q.d > maxEdgeCost * 1.05) break;

                const int nodeJ = nodeByField[static_cast<size_t>(q.fi)];
                if (nodeJ >= 0 && nodeJ != i) {
                    if (q.d < bestToNode[static_cast<size_t>(nodeJ)]) {
                        bestToNode[static_cast<size_t>(nodeJ)] = q.d;
                    }
                }

                const int fx = q.fi % m_fieldW;
                const int fy = q.fi / m_fieldW;
                const double c0 = (static_cast<size_t>(q.fi) < move.size()) ? finiteOr(static_cast<double>(move[static_cast<size_t>(q.fi)]), 2.0) : 2.0;
                const double w0 = (static_cast<size_t>(q.fi) < corridor.size()) ? std::max(0.01, static_cast<double>(corridor[static_cast<size_t>(q.fi)])) : 1.0;
                if (!std::isfinite(c0)) continue;

                for (int k = 0; k < 8; ++k) {
                    const int nx = fx + dx[static_cast<size_t>(k)];
                    const int ny = fy + dy[static_cast<size_t>(k)];
                    if (nx < 0 || ny < 0 || nx >= m_fieldW || ny >= m_fieldH) continue;
                    const int nfi = fieldIndex(nx, ny);
                    if (nfi < 0 || nfi >= nField) continue;
                    if (static_cast<size_t>(nfi) >= landMask.size() || landMask[static_cast<size_t>(nfi)] == 0u) continue;
                    const double c1 = (static_cast<size_t>(nfi) < move.size()) ? finiteOr(static_cast<double>(move[static_cast<size_t>(nfi)]), 2.0) : 2.0;
                    const double w1 = (static_cast<size_t>(nfi) < corridor.size()) ? std::max(0.01, static_cast<double>(corridor[static_cast<size_t>(nfi)])) : 1.0;
                    if (!std::isfinite(c1)) continue;
                    const double geom = (k < 4) ? 1.0 : 1.41421356237;
                    const double step = geom * landMult * 0.5 * (c0 + c1) / std::max(0.10, 0.5 * (w0 + w1));
                    const double nd = q.d + step;
                    if (nd + 1.0e-12 < dist[static_cast<size_t>(nfi)] && nd <= maxEdgeCost * 1.05) {
                        dist[static_cast<size_t>(nfi)] = nd;
                        pq.push(QItem{nd, nfi});
                    }
                }
            }

            for (int j = i + 1; j < n; ++j) {
                double landCost = bestToNode[static_cast<size_t>(j)];
                if (!std::isfinite(landCost)) continue;
                const SettlementNode& b = m_nodes[static_cast<size_t>(j)];
                const double dfx = static_cast<double>(a.fieldX - b.fieldX);
                const double dfy = static_cast<double>(a.fieldY - b.fieldY);
                const double distGeom = std::sqrt(dfx * dfx + dfy * dfy);
                double seaCost = std::numeric_limits<double>::infinity();
                bool sea = false;
                if (coastal[static_cast<size_t>(i)] != 0u && coastal[static_cast<size_t>(j)] != 0u) {
                    seaCost = distGeom * seaMult;
                }

                if (a.ownerCountry != b.ownerCountry) {
                    landCost *= borderFriction;
                    if (a.ownerCountry >= 0 && b.ownerCountry >= 0 &&
                        static_cast<size_t>(a.ownerCountry) < countries.size() &&
                        static_cast<size_t>(b.ownerCountry) < countries.size()) {
                        const Country& caCountry = countries[static_cast<size_t>(a.ownerCountry)];
                        const Country& cbCountry = countries[static_cast<size_t>(b.ownerCountry)];
                        if (isAtWarPair(caCountry, cbCountry) || isAtWarPair(cbCountry, caCountry)) {
                            landCost *= warRiskMult;
                            if (std::isfinite(seaCost)) seaCost *= warRiskMult;
                        }
                    }
                }
                const double regimeLand = edgeRegimeCap(a.ownerCountry, b.ownerCountry, false);
                const double regimeSea = edgeRegimeCap(a.ownerCountry, b.ownerCountry, true);
                if (m_ctx->config.transportRegimes.enabled) {
                    const double landFloor = std::clamp(m_ctx->config.transportRegimes.landMinCostMult, 0.05, 1.0);
                    const double seaFloor = std::clamp(m_ctx->config.transportRegimes.seaMinCostMult, 0.05, 1.0);
                    landCost *= std::max(landFloor, 1.0 - (1.0 - landFloor) * regimeLand);
                    if (std::isfinite(seaCost)) {
                        seaCost *= std::max(seaFloor, 1.0 - (1.0 - seaFloor) * regimeSea);
                    }
                }

                double cost = landCost;
                if (std::isfinite(seaCost) && seaCost < cost) {
                    cost = seaCost;
                    sea = true;
                }
                if (!(cost > 0.0) || cost > maxEdgeCost) continue;

                const double regime = sea ? regimeSea : regimeLand;
                const double capMul =
                    1.0 + regime * (std::max(1.0, m_ctx->config.transportRegimes.capacityMaxMult) - 1.0);
                const double relAdd = regime * std::clamp(m_ctx->config.transportRegimes.reliabilityMaxAdd, 0.0, 1.0);
                const double cap =
                    capMul *
                    (24.0 + 0.06 * std::sqrt(std::max(1.0, a.population) * std::max(1.0, b.population))) /
                    (1.0 + 0.08 * cost);
                const double rel = std::clamp(1.0 / (1.0 + 0.06 * cost) + relAdd, 0.05, 1.0);
                cand[static_cast<size_t>(i)].push_back(CandidateLink{j, cost, cap, rel, sea});
                cand[static_cast<size_t>(j)].push_back(CandidateLink{i, cost, cap, rel, sea});
            }
        }
    } else {
        const int bucketSize = 8;
        const int bxCount = std::max(1, (m_fieldW + bucketSize - 1) / bucketSize);
        const int byCount = std::max(1, (m_fieldH + bucketSize - 1) / bucketSize);
        std::vector<std::vector<int>> buckets(static_cast<size_t>(bxCount * byCount));
        for (int i = 0; i < n; ++i) {
            const int bx = std::clamp(m_nodes[static_cast<size_t>(i)].fieldX / bucketSize, 0, bxCount - 1);
            const int by = std::clamp(m_nodes[static_cast<size_t>(i)].fieldY / bucketSize, 0, byCount - 1);
            buckets[static_cast<size_t>(by * bxCount + bx)].push_back(i);
        }

        const double maxGeomDist = maxEdgeCost / std::min(landMult, seaMult);
        const int bucketRange = std::max(1, static_cast<int>(std::ceil(maxGeomDist / static_cast<double>(bucketSize))));

        for (int i = 0; i < n; ++i) {
            const SettlementNode& a = m_nodes[static_cast<size_t>(i)];
            const int bx = std::clamp(a.fieldX / bucketSize, 0, bxCount - 1);
            const int by = std::clamp(a.fieldY / bucketSize, 0, byCount - 1);

            for (int dy = -bucketRange; dy <= bucketRange; ++dy) {
                for (int dx = -bucketRange; dx <= bucketRange; ++dx) {
                    const int nbx = bx + dx;
                    const int nby = by + dy;
                    if (nbx < 0 || nby < 0 || nbx >= bxCount || nby >= byCount) continue;
                    const auto& bNodes = buckets[static_cast<size_t>(nby * bxCount + nbx)];
                    for (int j : bNodes) {
                        if (j <= i) continue;
                        const SettlementNode& b = m_nodes[static_cast<size_t>(j)];
                        const double dfx = static_cast<double>(a.fieldX - b.fieldX);
                        const double dfy = static_cast<double>(a.fieldY - b.fieldY);
                        const double dist = std::sqrt(dfx * dfx + dfy * dfy);
                        if (dist <= kEps || dist > maxGeomDist * 1.05) continue;

                        const int fia = fieldIndex(a.fieldX, a.fieldY);
                        const int fib = fieldIndex(b.fieldX, b.fieldY);
                        const double ma = (fia >= 0 && static_cast<size_t>(fia) < move.size()) ? finiteOr(static_cast<double>(move[static_cast<size_t>(fia)]), 2.0) : 2.0;
                        const double mb = (fib >= 0 && static_cast<size_t>(fib) < move.size()) ? finiteOr(static_cast<double>(move[static_cast<size_t>(fib)]), 2.0) : 2.0;
                        if (!std::isfinite(ma) || !std::isfinite(mb)) continue;
                        const double ca = (fia >= 0 && static_cast<size_t>(fia) < corridor.size()) ? std::max(0.01, static_cast<double>(corridor[static_cast<size_t>(fia)])) : 1.0;
                        const double cb = (fib >= 0 && static_cast<size_t>(fib) < corridor.size()) ? std::max(0.01, static_cast<double>(corridor[static_cast<size_t>(fib)])) : 1.0;

                        double landCost = dist * landMult * 0.5 * (ma + mb) / std::max(0.10, 0.5 * (ca + cb));
                        double seaCost = std::numeric_limits<double>::infinity();
                        bool sea = false;
                        if (coastal[static_cast<size_t>(i)] != 0u && coastal[static_cast<size_t>(j)] != 0u) {
                            seaCost = dist * seaMult;
                        }

                        if (a.ownerCountry != b.ownerCountry) {
                            landCost *= borderFriction;
                            if (a.ownerCountry >= 0 && b.ownerCountry >= 0 &&
                                static_cast<size_t>(a.ownerCountry) < countries.size() &&
                                static_cast<size_t>(b.ownerCountry) < countries.size()) {
                                const Country& caCountry = countries[static_cast<size_t>(a.ownerCountry)];
                                const Country& cbCountry = countries[static_cast<size_t>(b.ownerCountry)];
                                if (isAtWarPair(caCountry, cbCountry) || isAtWarPair(cbCountry, caCountry)) {
                                    landCost *= warRiskMult;
                                    if (std::isfinite(seaCost)) {
                                        seaCost *= warRiskMult;
                                    }
                                }
                            }
                        }
                        const double regimeLand = edgeRegimeCap(a.ownerCountry, b.ownerCountry, false);
                        const double regimeSea = edgeRegimeCap(a.ownerCountry, b.ownerCountry, true);
                        if (m_ctx->config.transportRegimes.enabled) {
                            const double landFloor = std::clamp(m_ctx->config.transportRegimes.landMinCostMult, 0.05, 1.0);
                            const double seaFloor = std::clamp(m_ctx->config.transportRegimes.seaMinCostMult, 0.05, 1.0);
                            landCost *= std::max(landFloor, 1.0 - (1.0 - landFloor) * regimeLand);
                            if (std::isfinite(seaCost)) {
                                seaCost *= std::max(seaFloor, 1.0 - (1.0 - seaFloor) * regimeSea);
                            }
                        }

                        double cost = landCost;
                        if (std::isfinite(seaCost) && seaCost < cost) {
                            cost = seaCost;
                            sea = true;
                        }

                        if (!(cost > 0.0) || cost > maxEdgeCost) continue;

                        const double regime = sea ? regimeSea : regimeLand;
                        const double capMul =
                            1.0 + regime * (std::max(1.0, m_ctx->config.transportRegimes.capacityMaxMult) - 1.0);
                        const double relAdd = regime * std::clamp(m_ctx->config.transportRegimes.reliabilityMaxAdd, 0.0, 1.0);
                        const double cap =
                            capMul *
                            (24.0 + 0.06 * std::sqrt(std::max(1.0, a.population) * std::max(1.0, b.population))) /
                            (1.0 + 0.08 * cost);
                        const double rel = std::clamp(1.0 / (1.0 + 0.06 * cost) + relAdd, 0.05, 1.0);

                        cand[static_cast<size_t>(i)].push_back(CandidateLink{j, cost, cap, rel, sea});
                        cand[static_cast<size_t>(j)].push_back(CandidateLink{i, cost, cap, rel, sea});
                    }
                }
            }
        }
    }

    std::vector<std::uint64_t> selected;
    selected.reserve(static_cast<size_t>(n * kNearest));
    for (int i = 0; i < n; ++i) {
        auto& lst = cand[static_cast<size_t>(i)];
        std::sort(lst.begin(), lst.end(), [](const CandidateLink& a, const CandidateLink& b) {
            if (a.cost != b.cost) return a.cost < b.cost;
            return a.neighborIndex < b.neighborIndex;
        });
        if (static_cast<int>(lst.size()) > kNearest) {
            lst.resize(static_cast<size_t>(kNearest));
        }
        for (const CandidateLink& c : lst) {
            const int lo = std::min(i, c.neighborIndex);
            const int hi = std::max(i, c.neighborIndex);
            selected.push_back((static_cast<std::uint64_t>(static_cast<std::uint32_t>(hi)) << 32) |
                               static_cast<std::uint64_t>(static_cast<std::uint32_t>(lo)));
        }
    }

    std::sort(selected.begin(), selected.end());
    selected.erase(std::unique(selected.begin(), selected.end()), selected.end());

    m_edges.reserve(selected.size());
    for (std::uint64_t key : selected) {
        const int i = static_cast<int>(key & 0xFFFFFFFFu);
        const int j = static_cast<int>((key >> 32) & 0xFFFFFFFFu);
        if (i < 0 || j < 0 || i >= n || j >= n || i == j) continue;

        CandidateLink best{};
        bool found = false;
        for (const CandidateLink& c : cand[static_cast<size_t>(i)]) {
            if (c.neighborIndex == j) {
                best = c;
                found = true;
                break;
            }
        }
        if (!found) continue;

        m_edges.push_back(TransportEdge{i, j, best.cost, best.capacity, best.reliability, best.seaLink});
    }

    std::sort(m_edges.begin(), m_edges.end(), [&](const TransportEdge& a, const TransportEdge& b) {
        const SettlementNode& an = m_nodes[static_cast<size_t>(a.fromNode)];
        const SettlementNode& bn = m_nodes[static_cast<size_t>(b.fromNode)];
        if (an.id != bn.id) return an.id < bn.id;
        const SettlementNode& at = m_nodes[static_cast<size_t>(a.toNode)];
        const SettlementNode& bt = m_nodes[static_cast<size_t>(b.toNode)];
        if (at.id != bt.id) return at.id < bt.id;
        if (a.cost != b.cost) return a.cost < b.cost;
        return a.capacity > b.capacity;
    });
    m_edgeExplorationBoost.assign(m_edges.size(), 0.0);
}

void SettlementSystem::updateKnowledgeAndExploration(int year, const std::vector<Country>& countries) {
    const int nNode = static_cast<int>(m_nodes.size());
    std::vector<double> prevCoverage = m_nodeKnowledgeCoverage;
    std::vector<double> prevMarket = m_nodePrevMarketPotential;
    m_nodeKnowledgeCoverage.assign(static_cast<size_t>(nNode), 0.0);
    m_nodeUncertainty.assign(static_cast<size_t>(nNode), 1.0);
    m_nodeExplorationValue.assign(static_cast<size_t>(nNode), 0.0);
    m_nodeKnowledgeErosion.assign(static_cast<size_t>(nNode), 0.0);
    m_nodePrevMarketPotential.assign(static_cast<size_t>(nNode), 0.0);
    m_edgeExplorationBoost.assign(m_edges.size(), 0.0);
    if (nNode <= 0) {
        return;
    }

    const bool enabled = m_ctx->config.exploration.enabled;
    const double localDecay = std::clamp(m_ctx->config.exploration.uncertaintyDecayLocal, 0.0, 1.0);
    const double neighDecay = std::clamp(m_ctx->config.exploration.uncertaintyDecayNeighbor, 0.0, 1.0);
    const double missionGain = std::clamp(m_ctx->config.exploration.explorationGain, 0.0, 1.0);

    for (int i = 0; i < nNode; ++i) {
        const SettlementNode& n = m_nodes[static_cast<size_t>(i)];
        double base = 0.20;
        if (n.ownerCountry >= 0 && static_cast<size_t>(n.ownerCountry) < countries.size()) {
            const Country& c = countries[static_cast<size_t>(n.ownerCountry)];
            const auto& m = c.getMacroEconomy();
            const double infra = clamp01(c.getKnowledgeInfra() / 3.0);
            const double exploreDrive = clamp01(static_cast<double>(c.getExploration().explorationDrive));
            base = clamp01(0.12 + 0.46 * clamp01(m.knowledgeStock) + 0.24 * infra + 0.18 * exploreDrive);
        }
        const double prev = (i < static_cast<int>(prevCoverage.size()))
            ? prevCoverage[static_cast<size_t>(i)]
            : base;
        m_nodeKnowledgeCoverage[static_cast<size_t>(i)] = clamp01((1.0 - localDecay) * prev + localDecay * base);
    }

    std::vector<double> neighAccum(static_cast<size_t>(nNode), 0.0);
    std::vector<double> neighWeight(static_cast<size_t>(nNode), 0.0);
    std::vector<double> neighMean(static_cast<size_t>(nNode), 0.0);
    for (size_t ei = 0; ei < m_edges.size(); ++ei) {
        const TransportEdge& e = m_edges[ei];
        if (e.fromNode < 0 || e.toNode < 0 || e.fromNode >= nNode || e.toNode >= nNode) continue;
        const double w = std::clamp(e.reliability, 0.05, 1.0) / std::max(0.15, e.cost);
        neighAccum[static_cast<size_t>(e.fromNode)] += w * m_nodeKnowledgeCoverage[static_cast<size_t>(e.toNode)];
        neighAccum[static_cast<size_t>(e.toNode)] += w * m_nodeKnowledgeCoverage[static_cast<size_t>(e.fromNode)];
        neighWeight[static_cast<size_t>(e.fromNode)] += w;
        neighWeight[static_cast<size_t>(e.toNode)] += w;
    }
    for (int i = 0; i < nNode; ++i) {
        if (neighWeight[static_cast<size_t>(i)] > 1.0e-9) {
            const double neigh = neighAccum[static_cast<size_t>(i)] / neighWeight[static_cast<size_t>(i)];
            neighMean[static_cast<size_t>(i)] = neigh;
            m_nodeKnowledgeCoverage[static_cast<size_t>(i)] = clamp01(
                (1.0 - neighDecay) * m_nodeKnowledgeCoverage[static_cast<size_t>(i)] + neighDecay * neigh);
        } else {
            neighMean[static_cast<size_t>(i)] = m_nodeKnowledgeCoverage[static_cast<size_t>(i)];
        }
    }

    if (m_ctx->config.knowledgeDynamics.enabled) {
        const auto& kd = m_ctx->config.knowledgeDynamics;
        const double specTh = std::max(1.0e-4, kd.maintenanceSpecialistThreshold);
        const double marketTh = std::max(1.0e-4, kd.maintenanceMarketThreshold);
        const double lossRate = std::clamp(kd.lossRate, 0.0, 1.0);
        const double collapseScale = std::max(0.0, kd.collapseLossScale);
        const double recoveryRate = std::clamp(kd.recoveryRate, 0.0, 1.0);
        const double relearn = std::clamp(kd.relearnFromNeighbors, 0.0, 1.0);

        for (int i = 0; i < nNode; ++i) {
            const SettlementNode& n = m_nodes[static_cast<size_t>(i)];
            const double marketNow = (static_cast<size_t>(i) < m_nodeMarketPotential.size())
                ? clamp01(m_nodeMarketPotential[static_cast<size_t>(i)] / 70.0)
                : 0.0;
            const double marketPrev = (static_cast<size_t>(i) < prevMarket.size())
                ? clamp01(prevMarket[static_cast<size_t>(i)])
                : marketNow;
            const double specRaw = std::max(0.0, n.specialistShare) / specTh;
            const double marketRaw = marketNow / marketTh;
            const double maintenanceRaw = std::min(specRaw, marketRaw);
            const double maintenance = clamp01(maintenanceRaw);
            const double collapse = clamp01((marketPrev - marketNow) * 2.2);
            const double loss = lossRate * (1.0 - maintenance) * (1.0 + collapseScale * collapse);
            const double surplus = std::max(0.0, maintenanceRaw - 1.0);
            const double recover = recoveryRate * std::min(1.0, surplus);
            const double neighborGap = clamp01(neighMean[static_cast<size_t>(i)] - m_nodeKnowledgeCoverage[static_cast<size_t>(i)]);

            double cov = m_nodeKnowledgeCoverage[static_cast<size_t>(i)];
            cov = cov * std::max(0.0, 1.0 - loss);
            cov += recover * (1.0 - cov);
            cov += relearn * neighborGap * (1.0 - cov);
            m_nodeKnowledgeCoverage[static_cast<size_t>(i)] = clamp01(cov);
            m_nodeKnowledgeErosion[static_cast<size_t>(i)] = std::max(0.0, loss - recover);
        }
    }

    for (int i = 0; i < nNode; ++i) {
        const double market = (static_cast<size_t>(i) < m_nodeMarketPotential.size())
            ? clamp01(m_nodeMarketPotential[static_cast<size_t>(i)] / 70.0)
            : 0.0;
        m_nodePrevMarketPotential[static_cast<size_t>(i)] = market;
        m_nodeUncertainty[static_cast<size_t>(i)] = clamp01(1.0 - m_nodeKnowledgeCoverage[static_cast<size_t>(i)]);
        m_nodeExplorationValue[static_cast<size_t>(i)] = clamp01(
            m_nodeUncertainty[static_cast<size_t>(i)] * (0.65 + 0.35 * (1.0 - market)));
    }

    if (enabled) {
        const int nCountry = static_cast<int>(countries.size());
        std::vector<std::vector<int>> nodesByCountry(static_cast<size_t>(nCountry));
        for (int i = 0; i < nNode; ++i) {
            const int c = m_nodes[static_cast<size_t>(i)].ownerCountry;
            if (c >= 0 && c < nCountry) {
                nodesByCountry[static_cast<size_t>(c)].push_back(i);
            }
        }

        const int baseMissions = std::max(0, m_ctx->config.exploration.baseMissionsPerCountry);
        const double perPopM = std::max(0.0, m_ctx->config.exploration.missionsPerPopMillion);
        const double missionCostCeil = std::max(1.0, m_ctx->config.exploration.missionCostCeiling);

        for (int c = 0; c < nCountry; ++c) {
            if (nodesByCountry[static_cast<size_t>(c)].empty()) continue;
            const Country& country = countries[static_cast<size_t>(c)];
            const double popM = static_cast<double>(std::max<long long>(0, country.getPopulation())) / 1'000'000.0;
            int budget = baseMissions + static_cast<int>(std::floor(perPopM * popM));
            budget = std::clamp(budget, 0, 12);
            if (budget <= 0) continue;

            std::vector<int> ranked = nodesByCountry[static_cast<size_t>(c)];
            std::sort(ranked.begin(), ranked.end(), [&](int a, int b) {
                const double va = m_nodeExplorationValue[static_cast<size_t>(a)];
                const double vb = m_nodeExplorationValue[static_cast<size_t>(b)];
                if (va != vb) return va > vb;
                return m_nodes[static_cast<size_t>(a)].id < m_nodes[static_cast<size_t>(b)].id;
            });
            if (static_cast<int>(ranked.size()) > budget) {
                ranked.resize(static_cast<size_t>(budget));
            }

            for (int src : ranked) {
                int bestEdge = -1;
                double bestScore = -1.0;
                for (int ei = 0; ei < static_cast<int>(m_edges.size()); ++ei) {
                    const TransportEdge& e = m_edges[static_cast<size_t>(ei)];
                    int dst = -1;
                    if (e.fromNode == src) dst = e.toNode;
                    else if (e.toNode == src) dst = e.fromNode;
                    else continue;
                    if (dst < 0 || dst >= nNode) continue;
                    if (e.cost > missionCostCeil) continue;
                    const double score =
                        m_nodeUncertainty[static_cast<size_t>(dst)] *
                        std::clamp(e.reliability, 0.05, 1.0) /
                        std::max(0.25, e.cost);
                    if (score > bestScore || (score == bestScore && ei < bestEdge)) {
                        bestScore = score;
                        bestEdge = ei;
                    }
                }
                if (bestEdge < 0) continue;
                const TransportEdge& e = m_edges[static_cast<size_t>(bestEdge)];
                const int dst = (e.fromNode == src) ? e.toNode : e.fromNode;
                if (dst < 0 || dst >= nNode) continue;
                m_edgeExplorationBoost[static_cast<size_t>(bestEdge)] = std::max(
                    m_edgeExplorationBoost[static_cast<size_t>(bestEdge)],
                    missionGain);
                m_nodeKnowledgeCoverage[static_cast<size_t>(src)] = clamp01(
                    m_nodeKnowledgeCoverage[static_cast<size_t>(src)] + 0.35 * missionGain);
                m_nodeKnowledgeCoverage[static_cast<size_t>(dst)] = clamp01(
                    m_nodeKnowledgeCoverage[static_cast<size_t>(dst)] + 0.75 * missionGain);
                m_nodeUncertainty[static_cast<size_t>(src)] = clamp01(1.0 - m_nodeKnowledgeCoverage[static_cast<size_t>(src)]);
                m_nodeUncertainty[static_cast<size_t>(dst)] = clamp01(1.0 - m_nodeKnowledgeCoverage[static_cast<size_t>(dst)]);
            }
        }

        for (int i = 0; i < nNode; ++i) {
            const double market = (static_cast<size_t>(i) < m_nodeMarketPotential.size())
                ? clamp01(m_nodeMarketPotential[static_cast<size_t>(i)] / 70.0)
                : 0.0;
            m_nodeExplorationValue[static_cast<size_t>(i)] = clamp01(
                m_nodeUncertainty[static_cast<size_t>(i)] * (0.65 + 0.35 * (1.0 - market)));
        }
    }

    (void)year;
}

void SettlementSystem::computeFlowsAndMigration(const Map& map, const std::vector<Country>& countries) {
    const int nNode = static_cast<int>(m_nodes.size());
    const int nCountry = static_cast<int>(countries.size());

    m_nodeOutgoingFlow.assign(static_cast<size_t>(nNode), 0.0);
    m_nodeMarketPotential.assign(static_cast<size_t>(nNode), 0.0);
    m_nodeUtility.assign(static_cast<size_t>(nNode), 0.0);

    m_countryTradeHintMatrix.assign(static_cast<size_t>(nCountry) * static_cast<size_t>(nCountry), 0.0f);

    if (nNode <= 0) {
        return;
    }

    const auto& corridor = map.getFieldCorridorWeight();
    const double exploreRelBoost = std::clamp(m_ctx->config.exploration.edgeReliabilityBoost, 0.0, 1.0);
    const double exploreCostRed = std::clamp(m_ctx->config.exploration.edgeCostReduction, 0.0, 0.9);

    m_edgeLogisticsAttenuation.assign(m_edges.size(), 1.0);
    std::vector<double> edgeGravityScale(m_edges.size(), 1.0);
    std::vector<double> edgeMigrationScale(m_edges.size(), 1.0);
    std::vector<double> edgeEffCost(m_edges.size(), 1.0);
    std::vector<double> edgeEffRel(m_edges.size(), 1.0);
    for (size_t ei = 0; ei < m_edges.size(); ++ei) {
        const TransportEdge& e = m_edges[ei];
        const double boost = (ei < m_edgeExplorationBoost.size()) ? clamp01(m_edgeExplorationBoost[ei]) : 0.0;
        edgeEffCost[ei] = std::max(0.05, std::max(0.0, e.cost) * (1.0 - exploreCostRed * boost));
        edgeEffRel[ei] = std::clamp(std::max(0.0, e.reliability) * (1.0 + exploreRelBoost * boost), 0.01, 1.0);
    }

    if (!m_edges.empty()) {
        bool usedGpuEdge = false;
        if (m_gpu && m_gpu->enabled && m_gpu->edgeKernel &&
            (m_ctx->config.researchGpu.accelerateFlows ||
             m_ctx->config.researchGpu.accelerateTransport ||
             m_ctx->config.researchGpu.warfareLogistics)) {
            const int nEdge = static_cast<int>(m_edges.size());
            std::vector<sf::Uint8> edgePx(static_cast<size_t>(nEdge) * 4u, 0u);
            std::vector<sf::Uint8> nodePx(static_cast<size_t>(nEdge) * 4u, 0u);

            for (int ei = 0; ei < nEdge; ++ei) {
                const TransportEdge& e = m_edges[static_cast<size_t>(ei)];
                if (e.fromNode < 0 || e.toNode < 0 || e.fromNode >= nNode || e.toNode >= nNode) continue;
                const SettlementNode& a = m_nodes[static_cast<size_t>(e.fromNode)];
                const SettlementNode& b = m_nodes[static_cast<size_t>(e.toNode)];
                const bool war =
                    (a.ownerCountry >= 0 && b.ownerCountry >= 0 &&
                     a.ownerCountry < nCountry && b.ownerCountry < nCountry &&
                     (isAtWarPair(countries[static_cast<size_t>(a.ownerCountry)], countries[static_cast<size_t>(b.ownerCountry)]) ||
                      isAtWarPair(countries[static_cast<size_t>(b.ownerCountry)], countries[static_cast<size_t>(a.ownerCountry)])));

                const size_t p = static_cast<size_t>(ei) * 4u;
                edgePx[p + 0u] = static_cast<sf::Uint8>(std::round(clamp01(edgeEffCost[static_cast<size_t>(ei)] / std::max(1.0, m_ctx->config.transport.maxEdgeCost)) * 255.0));
                edgePx[p + 1u] = static_cast<sf::Uint8>(std::round(clamp01(e.capacity / 220.0) * 255.0));
                edgePx[p + 2u] = static_cast<sf::Uint8>(std::round(clamp01(edgeEffRel[static_cast<size_t>(ei)]) * 255.0));
                edgePx[p + 3u] = war ? 255u : 0u;

                nodePx[p + 0u] = static_cast<sf::Uint8>(std::round(clamp01(a.population / 600000.0) * 255.0));
                nodePx[p + 1u] = static_cast<sf::Uint8>(std::round(clamp01(b.population / 600000.0) * 255.0));
                nodePx[p + 2u] = static_cast<sf::Uint8>(std::round(clamp01(a.specialistShare) * 255.0));
                nodePx[p + 3u] = static_cast<sf::Uint8>(std::round(clamp01(b.specialistShare) * 255.0));
            }

            sf::Texture edgeTex;
            sf::Texture nodeTex;
            if (edgeTex.create(static_cast<unsigned>(nEdge), 1u) &&
                nodeTex.create(static_cast<unsigned>(nEdge), 1u)) {
                edgeTex.setSmooth(false);
                nodeTex.setSmooth(false);
                edgeTex.update(edgePx.data());
                nodeTex.update(nodePx.data());

                m_gpu->edgeShader.setUniform("edgeTex", edgeTex);
                m_gpu->edgeShader.setUniform("nodeTex", nodeTex);
                m_gpu->edgeShader.setUniform("attritionRate", 0.42f);
                m_gpu->edgeShader.setUniform("gravityKappa", static_cast<float>(std::max(0.0, m_ctx->config.transport.gravityKappa)));
                m_gpu->edgeShader.setUniform("gravityAlpha", static_cast<float>(std::max(0.0, m_ctx->config.transport.gravityAlpha)));
                m_gpu->edgeShader.setUniform("gravityBeta", static_cast<float>(std::max(0.0, m_ctx->config.transport.gravityBeta)));
                m_gpu->edgeShader.setUniform("gravityGamma", static_cast<float>(std::max(0.0, m_ctx->config.transport.gravityGamma)));
                m_gpu->edgeShader.setUniform("migrationM0", static_cast<float>(std::max(0.0, m_ctx->config.transport.migrationM0)));
                m_gpu->edgeShader.setUniform("migrationDistDecay", static_cast<float>(std::max(0.0, m_ctx->config.transport.migrationDistDecay)));
                m_gpu->edgeShader.setUniform("gravityScale", 6000.0f);
                m_gpu->edgeShader.setUniform("migrationScale", 0.08f);

                std::vector<sf::Uint8> outPx;
                if (runShaderPass1D(m_gpu->edgeShader, edgeTex, nEdge, outPx)) {
                    usedGpuEdge = true;
                    for (int ei = 0; ei < nEdge; ++ei) {
                        const size_t p = static_cast<size_t>(ei) * 4u;
                        m_edgeLogisticsAttenuation[static_cast<size_t>(ei)] =
                            std::clamp(static_cast<double>(outPx[p + 0u]) / 255.0, 0.0, 1.0);
                        edgeGravityScale[static_cast<size_t>(ei)] =
                            std::max(0.0, static_cast<double>(outPx[p + 1u]) / 255.0 * 6000.0);
                        edgeMigrationScale[static_cast<size_t>(ei)] =
                            std::max(0.0, static_cast<double>(outPx[p + 2u]) / 255.0 * 0.08);
                    }
                }
            }
        }

        if (!usedGpuEdge) {
            for (size_t ei = 0; ei < m_edges.size(); ++ei) {
                const TransportEdge& e = m_edges[ei];
                if (e.fromNode < 0 || e.toNode < 0 || e.fromNode >= nNode || e.toNode >= nNode) continue;
                const SettlementNode& a = m_nodes[static_cast<size_t>(e.fromNode)];
                const SettlementNode& b = m_nodes[static_cast<size_t>(e.toNode)];
                const bool war =
                    (a.ownerCountry >= 0 && b.ownerCountry >= 0 &&
                     a.ownerCountry < nCountry && b.ownerCountry < nCountry &&
                     (isAtWarPair(countries[static_cast<size_t>(a.ownerCountry)], countries[static_cast<size_t>(b.ownerCountry)]) ||
                      isAtWarPair(countries[static_cast<size_t>(b.ownerCountry)], countries[static_cast<size_t>(a.ownerCountry)])));
                const double effCost = edgeEffCost[ei];
                const double effRel = edgeEffRel[ei];
                const double demand = 0.20 * std::sqrt(std::max(0.0, std::max(1.0, a.population) * std::max(1.0, b.population))) * (1.0 + (war ? 0.60 : 0.0));
                const double supply = std::max(0.0, e.capacity * effRel);
                const double deficit = std::max(0.0, demand - supply);
                const double attenuation = std::exp(-0.42 * deficit);
                m_edgeLogisticsAttenuation[ei] = std::clamp(attenuation, 0.0, 1.0);

                const double Sa = std::max(0.01, a.population * std::max(0.02, a.specialistShare));
                const double Sb = std::max(0.01, b.population * std::max(0.02, b.specialistShare));
                edgeGravityScale[ei] =
                    std::max(0.0, m_ctx->config.transport.gravityKappa) *
                    std::pow(Sa, std::max(0.0, m_ctx->config.transport.gravityAlpha)) *
                    std::pow(Sb, std::max(0.0, m_ctx->config.transport.gravityBeta)) /
                    std::pow(std::max(0.15, effCost), std::max(0.0, m_ctx->config.transport.gravityGamma));
                edgeMigrationScale[ei] =
                    std::max(0.0, m_ctx->config.transport.migrationM0) *
                    std::exp(-std::max(0.0, m_ctx->config.transport.migrationDistDecay) * std::max(0.0, effCost)) *
                    std::clamp(effRel * attenuation, 0.05, 1.0);
            }
        }
    }

    for (const TransportEdge& e : m_edges) {
        if (e.fromNode < 0 || e.toNode < 0 || e.fromNode >= nNode || e.toNode >= nNode) continue;
        const SettlementNode& a = m_nodes[static_cast<size_t>(e.fromNode)];
        const SettlementNode& b = m_nodes[static_cast<size_t>(e.toNode)];
        const size_t ei = static_cast<size_t>(&e - m_edges.data());
        const double effCost = (ei < edgeEffCost.size()) ? edgeEffCost[ei] : e.cost;
        m_nodeMarketPotential[static_cast<size_t>(e.fromNode)] += std::max(0.0, b.population) / (1.0 + effCost);
        m_nodeMarketPotential[static_cast<size_t>(e.toNode)] += std::max(0.0, a.population) / (1.0 + effCost);
    }

    const double cal0 = std::max(1.0e-9, m_ctx->config.settlements.cal0);

    for (size_t ei = 0; ei < m_edges.size(); ++ei) {
        TransportEdge& e = m_edges[ei];
        if (e.fromNode < 0 || e.toNode < 0 || e.fromNode >= nNode || e.toNode >= nNode) continue;
        SettlementNode& a = m_nodes[static_cast<size_t>(e.fromNode)];
        SettlementNode& b = m_nodes[static_cast<size_t>(e.toNode)];
        const double effCost = (ei < edgeEffCost.size()) ? edgeEffCost[ei] : e.cost;
        const double effRelRaw = (ei < edgeEffRel.size()) ? edgeEffRel[ei] : e.reliability;

        const double logistics = (ei < m_edgeLogisticsAttenuation.size()) ? std::clamp(m_edgeLogisticsAttenuation[ei], 0.0, 1.0) : 1.0;
        const double effReliability = std::clamp(effRelRaw * logistics, 0.01, 1.0);
        const double cap = std::max(0.0, e.capacity * effReliability);
        if (cap <= kEps) continue;

        const double gravity =
            (ei < edgeGravityScale.size() && edgeGravityScale[ei] > 0.0)
            ? edgeGravityScale[ei]
            : 0.0;

        auto tradeable = [&](const SettlementNode& n) {
            return std::max(0.0, 0.26 * n.foodProduced - n.foodExported);
        };
        auto need = [&](const SettlementNode& n) {
            return std::max(0.0, n.population * cal0 - (n.foodProduced + n.foodImported - n.foodExported));
        };

        const double totalFlow = std::min(cap, gravity);
        const double needA = need(a);
        const double needB = need(b);
        const double splitAB = clamp01(0.5 + 0.5 * (needB - needA) / std::max(1.0, needA + needB));
        const double flowAB = totalFlow * splitAB;
        const double flowBA = totalFlow - flowAB;

        const double aToB = std::min({flowAB, tradeable(a), needB + 0.10 * b.foodProduced});
        if (aToB > 0.0) {
            a.foodExported += aToB;
            b.foodImported += aToB;
        }

        const double bToA = std::min({flowBA, tradeable(b), needA + 0.10 * a.foodProduced});
        if (bToA > 0.0) {
            b.foodExported += bToA;
            a.foodImported += bToA;
        }

        m_nodeOutgoingFlow[static_cast<size_t>(e.fromNode)] += flowAB;
        m_nodeOutgoingFlow[static_cast<size_t>(e.toNode)] += flowBA;
        if (a.ownerCountry >= 0 && b.ownerCountry >= 0 && a.ownerCountry < nCountry && b.ownerCountry < nCountry && a.ownerCountry != b.ownerCountry) {
            if (flowAB > 0.0) {
                const size_t ij = static_cast<size_t>(a.ownerCountry) * static_cast<size_t>(nCountry) + static_cast<size_t>(b.ownerCountry);
                m_countryTradeHintMatrix[ij] += static_cast<float>(flowAB);
            }
            if (flowBA > 0.0) {
                const size_t ji = static_cast<size_t>(b.ownerCountry) * static_cast<size_t>(nCountry) + static_cast<size_t>(a.ownerCountry);
                m_countryTradeHintMatrix[ji] += static_cast<float>(flowBA);
            }
        }
    }

    for (SettlementNode& n : m_nodes) {
        n.calories = std::max(0.0, n.foodProduced + n.foodImported - n.foodExported);
    }

    std::vector<double> moveBudget(static_cast<size_t>(nNode), 0.0);
    std::vector<double> delta(static_cast<size_t>(nNode), 0.0);

    for (int i = 0; i < nNode; ++i) {
        moveBudget[static_cast<size_t>(i)] = std::max(0.0, m_nodes[static_cast<size_t>(i)].population) * 0.08;
        const SettlementNode& n = m_nodes[static_cast<size_t>(i)];
        const double perCap = n.calories / std::max(1.0, n.population);
        double risk = clamp01((cal0 - perCap) / std::max(cal0, 1.0e-9));
        if (n.ownerCountry >= 0 && static_cast<size_t>(n.ownerCountry) < countries.size()) {
            const auto& m = countries[static_cast<size_t>(n.ownerCountry)].getMacroEconomy();
            risk = clamp01(risk + 0.55 * m.famineSeverity + 0.45 * m.diseaseBurden);
        }
        const double market = clamp01(m_nodeMarketPotential[static_cast<size_t>(i)] / 70.0);
        m_nodeUtility[static_cast<size_t>(i)] = clamp01(
            0.50 * clamp01(perCap / std::max(cal0, 1.0e-9)) +
            0.35 * market +
            0.15 * (1.0 - risk));
    }

    const double m0 = std::max(0.0, m_ctx->config.transport.migrationM0);
    const double distDecay = std::max(0.0, m_ctx->config.transport.migrationDistDecay);

    for (size_t ei = 0; ei < m_edges.size(); ++ei) {
        const TransportEdge& e = m_edges[ei];
        if (e.fromNode < 0 || e.toNode < 0 || e.fromNode >= nNode || e.toNode >= nNode) continue;
        const int ia = e.fromNode;
        const int ib = e.toNode;
        const SettlementNode& a = m_nodes[static_cast<size_t>(ia)];
        const SettlementNode& b = m_nodes[static_cast<size_t>(ib)];

        const int fia = fieldIndex(a.fieldX, a.fieldY);
        const int fib = fieldIndex(b.fieldX, b.fieldY);
        const double corA = (fia >= 0 && static_cast<size_t>(fia) < corridor.size()) ? std::max(0.05, static_cast<double>(corridor[static_cast<size_t>(fia)])) : 1.0;
        const double corB = (fib >= 0 && static_cast<size_t>(fib) < corridor.size()) ? std::max(0.05, static_cast<double>(corridor[static_cast<size_t>(fib)])) : 1.0;
        const double cor = 0.5 * (corA + corB);
        const double edgeMigScale = (ei < edgeMigrationScale.size()) ? std::max(0.0, edgeMigrationScale[ei]) : 0.0;
        const double localRel = std::clamp(
            ((ei < edgeEffRel.size()) ? edgeEffRel[ei] : e.reliability) *
            ((ei < m_edgeLogisticsAttenuation.size()) ? m_edgeLogisticsAttenuation[ei] : 1.0), 0.05, 1.0);
        const double effCost = (ei < edgeEffCost.size()) ? edgeEffCost[ei] : e.cost;

        auto migrate = [&](int src, int dst) {
            const double du = m_nodeUtility[static_cast<size_t>(dst)] - m_nodeUtility[static_cast<size_t>(src)];
            if (du <= 1.0e-6) return;
            const double srcPop = std::max(0.0, m_nodes[static_cast<size_t>(src)].population);
            if (srcPop <= 1.0) return;
            const double move =
                srcPop * du *
                ((edgeMigScale > 0.0) ? edgeMigScale : (m0 * std::exp(-distDecay * std::max(0.0, effCost)))) *
                std::max(0.15, cor) *
                localRel;
            double flow = std::min(moveBudget[static_cast<size_t>(src)], std::max(0.0, move));
            flow = std::min(flow, 0.03 * srcPop);
            if (flow <= 0.0) return;
            moveBudget[static_cast<size_t>(src)] -= flow;
            delta[static_cast<size_t>(src)] -= flow;
            delta[static_cast<size_t>(dst)] += flow;
        };

        migrate(ia, ib);
        migrate(ib, ia);
    }

    for (int i = 0; i < nNode; ++i) {
        m_nodes[static_cast<size_t>(i)].population = std::max(0.0, m_nodes[static_cast<size_t>(i)].population + delta[static_cast<size_t>(i)]);

        const SettlementNode& n = m_nodes[static_cast<size_t>(i)];
        const double perCap = n.calories / std::max(1.0, n.population);
        const double market = clamp01(m_nodeMarketPotential[static_cast<size_t>(i)] / 70.0);
        const double risk = clamp01((cal0 - perCap) / std::max(cal0, 1.0e-9));
        const double dSpec =
            std::max(0.0, m_ctx->config.transport.specialistEta) * market -
            std::max(0.0, m_ctx->config.transport.specialistLambda) * risk;
        m_nodes[static_cast<size_t>(i)].specialistShare = clamp01(n.specialistShare + dSpec);
    }
}

void SettlementSystem::updateCampaignLogisticsAndAttrition(int year, const std::vector<Country>& countries) {
    const int nNode = static_cast<int>(m_nodes.size());
    m_nodeWarAttrition.assign(static_cast<size_t>(nNode), 0.0);
    if (!m_ctx->config.researchSettlement.campaignLogistics || nNode <= 0 || m_edges.empty()) {
        for (TransportEdge& e : m_edges) {
            e.campaignLoad = 0.0;
            e.campaignDeficit = 0.0;
            e.campaignAttrition = 1.0;
        }
        return;
    }

    const int nCountry = static_cast<int>(countries.size());
    for (TransportEdge& e : m_edges) {
        e.campaignLoad = 0.0;
        e.campaignDeficit = 0.0;
        e.campaignAttrition = 1.0;
    }

    std::vector<std::vector<int>> adj(static_cast<size_t>(nNode));
    for (int ei = 0; ei < static_cast<int>(m_edges.size()); ++ei) {
        const TransportEdge& e = m_edges[static_cast<size_t>(ei)];
        if (e.fromNode < 0 || e.toNode < 0 || e.fromNode >= nNode || e.toNode >= nNode) continue;
        adj[static_cast<size_t>(e.fromNode)].push_back(ei);
        adj[static_cast<size_t>(e.toNode)].push_back(ei);
    }

    std::vector<int> sourceByCountry(static_cast<size_t>(nCountry), -1);
    for (int c = 0; c < nCountry; ++c) {
        double best = -1.0;
        int bestNode = -1;
        for (int i = 0; i < nNode; ++i) {
            const SettlementNode& n = m_nodes[static_cast<size_t>(i)];
            if (n.ownerCountry != c) continue;
            const double score = std::max(0.0, n.population) * (0.35 + 0.35 * n.localAdminCapacity + 0.30 * n.localLegitimacy);
            if (score > best || (score == best && (bestNode < 0 || n.id < m_nodes[static_cast<size_t>(bestNode)].id))) {
                best = score;
                bestNode = i;
            }
        }
        sourceByCountry[static_cast<size_t>(c)] = bestNode;
    }

    struct Front {
        int country = -1;
        int frontierNode = -1;
    };
    std::vector<Front> fronts;
    fronts.reserve(m_edges.size() * 2);
    for (const TransportEdge& e : m_edges) {
        if (e.fromNode < 0 || e.toNode < 0 || e.fromNode >= nNode || e.toNode >= nNode) continue;
        const SettlementNode& a = m_nodes[static_cast<size_t>(e.fromNode)];
        const SettlementNode& b = m_nodes[static_cast<size_t>(e.toNode)];
        if (a.ownerCountry < 0 || b.ownerCountry < 0 || a.ownerCountry == b.ownerCountry) continue;
        if (a.ownerCountry >= nCountry || b.ownerCountry >= nCountry) continue;
        const bool war =
            isAtWarPair(countries[static_cast<size_t>(a.ownerCountry)], countries[static_cast<size_t>(b.ownerCountry)]) ||
            isAtWarPair(countries[static_cast<size_t>(b.ownerCountry)], countries[static_cast<size_t>(a.ownerCountry)]);
        if (!war) continue;
        fronts.push_back(Front{a.ownerCountry, e.fromNode});
        fronts.push_back(Front{b.ownerCountry, e.toNode});
    }
    std::sort(fronts.begin(), fronts.end(), [](const Front& x, const Front& y) {
        if (x.country != y.country) return x.country < y.country;
        return x.frontierNode < y.frontierNode;
    });
    fronts.erase(std::unique(fronts.begin(), fronts.end(), [](const Front& x, const Front& y) {
        return x.country == y.country && x.frontierNode == y.frontierNode;
    }), fronts.end());

    struct QItem {
        double d = 0.0;
        int node = -1;
    };
    struct QCmp {
        bool operator()(const QItem& a, const QItem& b) const {
            if (a.d != b.d) return a.d > b.d;
            return a.node > b.node;
        }
    };

    const double baseDemand = std::max(0.0, m_ctx->config.researchSettlement.campaignDemandBase);
    const double warScale = std::max(0.0, m_ctx->config.researchSettlement.campaignDemandWarScale);
    const double attrRate = std::max(0.0, m_ctx->config.researchSettlement.campaignAttritionRate);

    std::vector<double> nodeStress(static_cast<size_t>(nNode), 0.0);
    for (const Front& f : fronts) {
        if (f.country < 0 || f.country >= nCountry) continue;
        const int src = sourceByCountry[static_cast<size_t>(f.country)];
        const int dst = f.frontierNode;
        if (src < 0 || dst < 0 || src >= nNode || dst >= nNode || src == dst) continue;

        std::vector<double> dist(static_cast<size_t>(nNode), std::numeric_limits<double>::infinity());
        std::vector<int> prevEdge(static_cast<size_t>(nNode), -1);
        std::priority_queue<QItem, std::vector<QItem>, QCmp> pq;
        dist[static_cast<size_t>(src)] = 0.0;
        pq.push(QItem{0.0, src});

        while (!pq.empty()) {
            const QItem q = pq.top();
            pq.pop();
            if (q.node == dst) break;
            if (q.d > dist[static_cast<size_t>(q.node)] + 1.0e-12) continue;
            for (int ei : adj[static_cast<size_t>(q.node)]) {
                if (ei < 0 || static_cast<size_t>(ei) >= m_edges.size()) continue;
                const TransportEdge& e = m_edges[static_cast<size_t>(ei)];
                const int next = (e.fromNode == q.node) ? e.toNode : e.fromNode;
                if (next < 0 || next >= nNode) continue;
                const double logistics = (static_cast<size_t>(ei) < m_edgeLogisticsAttenuation.size()) ? m_edgeLogisticsAttenuation[static_cast<size_t>(ei)] : 1.0;
                const double rel = std::clamp(e.reliability * logistics, 0.05, 1.0);
                const double step = std::max(0.05, e.cost / rel);
                const double nd = q.d + step;
                if (nd + 1.0e-12 < dist[static_cast<size_t>(next)]) {
                    dist[static_cast<size_t>(next)] = nd;
                    prevEdge[static_cast<size_t>(next)] = ei;
                    pq.push(QItem{nd, next});
                }
            }
        }
        if (!std::isfinite(dist[static_cast<size_t>(dst)])) continue;

        const double srcPop = std::max(1.0, m_nodes[static_cast<size_t>(src)].population);
        const double dstPop = std::max(1.0, m_nodes[static_cast<size_t>(dst)].population);
        const double demand = baseDemand + warScale * std::sqrt(srcPop * dstPop);

        int cur = dst;
        while (cur != src) {
            const int ei = prevEdge[static_cast<size_t>(cur)];
            if (ei < 0 || static_cast<size_t>(ei) >= m_edges.size()) break;
            TransportEdge& e = m_edges[static_cast<size_t>(ei)];
            e.campaignLoad += demand;
            cur = (e.fromNode == cur) ? e.toNode : e.fromNode;
            if (cur < 0 || cur >= nNode) break;
        }
    }

    for (size_t ei = 0; ei < m_edges.size(); ++ei) {
        TransportEdge& e = m_edges[ei];
        const double logistics = (ei < m_edgeLogisticsAttenuation.size()) ? m_edgeLogisticsAttenuation[ei] : 1.0;
        const double cap = std::max(0.0, e.capacity * std::clamp(e.reliability * logistics, 0.05, 1.0));
        e.campaignDeficit = std::max(0.0, e.campaignLoad - cap);
        const double normDef = e.campaignDeficit / std::max(1.0, cap);
        e.campaignAttrition = std::exp(-attrRate * normDef);
        e.reliability = std::clamp(e.reliability * e.campaignAttrition, 0.03, 1.0);
        if (ei < m_edgeLogisticsAttenuation.size()) {
            m_edgeLogisticsAttenuation[ei] = std::clamp(m_edgeLogisticsAttenuation[ei] * e.campaignAttrition, 0.0, 1.0);
        }
        if (e.fromNode >= 0 && e.fromNode < nNode) {
            nodeStress[static_cast<size_t>(e.fromNode)] += 0.5 * normDef;
        }
        if (e.toNode >= 0 && e.toNode < nNode) {
            nodeStress[static_cast<size_t>(e.toNode)] += 0.5 * normDef;
        }
    }

    for (int i = 0; i < nNode; ++i) {
        m_nodeWarAttrition[static_cast<size_t>(i)] = clamp01(nodeStress[static_cast<size_t>(i)]);
    }

    (void)year;
}

void SettlementSystem::applyPolityChoiceAssignment(int year, std::vector<Country>& countries) {
    const int nNode = static_cast<int>(m_nodes.size());
    const int nCountry = static_cast<int>(countries.size());
    m_nodePolitySwitchGain.assign(static_cast<size_t>(nNode), 0.0);
    if (!m_ctx->config.researchSettlement.polityChoiceAssignment || nNode <= 0 || nCountry <= 0) {
        return;
    }

    std::vector<double> countryStrength(static_cast<size_t>(nCountry), 0.0);
    for (int c = 0; c < nCountry; ++c) {
        const Country& country = countries[static_cast<size_t>(c)];
        const auto& m = country.getMacroEconomy();
        countryStrength[static_cast<size_t>(c)] = clamp01(
            0.34 * country.getLegitimacy() +
            0.28 * country.getAvgControl() +
            0.18 * country.getAdminCapacity() +
            0.20 * clamp01(m.marketAccess));
    }

    std::vector<int> bestCountries;
    const int topCountries = std::clamp(m_ctx->config.researchSettlement.polityTopCountryCandidates, 1, 8);
    bestCountries.reserve(static_cast<size_t>(topCountries));
    for (int pick = 0; pick < topCountries; ++pick) {
        int best = -1;
        double bestV = -1.0;
        for (int c = 0; c < nCountry; ++c) {
            if (std::find(bestCountries.begin(), bestCountries.end(), c) != bestCountries.end()) continue;
            const double v = countryStrength[static_cast<size_t>(c)];
            if (v > bestV || (v == bestV && c < best)) {
                bestV = v;
                best = c;
            }
        }
        if (best >= 0) bestCountries.push_back(best);
    }

    struct Proposal {
        int node = -1;
        int fromCountry = -1;
        int toCountry = -1;
        double gain = 0.0;
    };
    std::vector<Proposal> proposals;
    const double threshold = std::max(0.0, m_ctx->config.researchSettlement.politySwitchThreshold);
    const bool useControlGraph = m_ctx->config.researchSettlement.polityControlGraph;

    struct AdjEdge {
        int to = -1;
        double cost = 0.0;
    };
    std::vector<std::vector<AdjEdge>> adj(static_cast<size_t>(nNode));
    for (size_t ei = 0; ei < m_edges.size(); ++ei) {
        const TransportEdge& e = m_edges[ei];
        if (e.fromNode < 0 || e.toNode < 0 || e.fromNode >= nNode || e.toNode >= nNode || e.fromNode == e.toNode) {
            continue;
        }
        const double logistics = (ei < m_edgeLogisticsAttenuation.size()) ? std::clamp(m_edgeLogisticsAttenuation[ei], 0.0, 1.0) : 1.0;
        const double rel = std::clamp(e.reliability * logistics, 0.03, 1.0);
        const double step = std::max(0.05, std::max(0.0, e.cost) / rel);
        adj[static_cast<size_t>(e.fromNode)].push_back(AdjEdge{e.toNode, step});
        adj[static_cast<size_t>(e.toNode)].push_back(AdjEdge{e.fromNode, step});
    }

    std::vector<int> centerNodeByCountry(static_cast<size_t>(nCountry), -1);
    std::vector<double> centerScoreByCountry(static_cast<size_t>(nCountry), -1.0);
    for (int i = 0; i < nNode; ++i) {
        const SettlementNode& n = m_nodes[static_cast<size_t>(i)];
        if (n.ownerCountry < 0 || n.ownerCountry >= nCountry) continue;
        const double s = std::max(0.0, n.population) * (0.45 + 0.35 * n.localAdminCapacity + 0.20 * n.localLegitimacy);
        const int c = n.ownerCountry;
        if (s > centerScoreByCountry[static_cast<size_t>(c)] ||
            (s == centerScoreByCountry[static_cast<size_t>(c)] &&
             (centerNodeByCountry[static_cast<size_t>(c)] < 0 ||
              n.id < m_nodes[static_cast<size_t>(centerNodeByCountry[static_cast<size_t>(c)])].id))) {
            centerScoreByCountry[static_cast<size_t>(c)] = s;
            centerNodeByCountry[static_cast<size_t>(c)] = i;
        }
    }

    const double maxTravel = std::max(1.0, m_ctx->config.researchSettlement.polityTravelMaxCost);
    std::unordered_map<int, std::vector<double>> distByCountry;
    if (useControlGraph) {
        struct QItem {
            double d = 0.0;
            int node = -1;
        };
        struct QCmp {
            bool operator()(const QItem& a, const QItem& b) const {
                if (a.d != b.d) return a.d > b.d;
                return a.node > b.node;
            }
        };
        std::vector<int> activeCountries;
        activeCountries.reserve(static_cast<size_t>(nCountry));
        for (int c = 0; c < nCountry; ++c) {
            if (centerNodeByCountry[static_cast<size_t>(c)] >= 0) {
                activeCountries.push_back(c);
            }
        }
        std::sort(activeCountries.begin(), activeCountries.end());

        for (int c : activeCountries) {
            const int src = centerNodeByCountry[static_cast<size_t>(c)];
            if (src < 0 || src >= nNode) continue;
            std::vector<double> dist(static_cast<size_t>(nNode), std::numeric_limits<double>::infinity());
            std::priority_queue<QItem, std::vector<QItem>, QCmp> pq;
            dist[static_cast<size_t>(src)] = 0.0;
            pq.push(QItem{0.0, src});
            while (!pq.empty()) {
                const QItem q = pq.top();
                pq.pop();
                if (q.d > dist[static_cast<size_t>(q.node)] + 1.0e-12) continue;
                if (q.d > maxTravel * 1.15) continue;
                for (const AdjEdge& ae : adj[static_cast<size_t>(q.node)]) {
                    if (ae.to < 0 || ae.to >= nNode) continue;
                    const double nd = q.d + ae.cost;
                    if (nd + 1.0e-12 < dist[static_cast<size_t>(ae.to)] && nd <= maxTravel * 1.15) {
                        dist[static_cast<size_t>(ae.to)] = nd;
                        pq.push(QItem{nd, ae.to});
                    }
                }
            }
            distByCountry.emplace(c, std::move(dist));
        }
    }

    const double tauBase = std::max(0.5, m_ctx->config.researchSettlement.polityTravelTauBase);
    const double tauAdminW = std::max(0.0, m_ctx->config.researchSettlement.polityTravelTauAdminWeight);
    const double tauLogiW = std::max(0.0, m_ctx->config.researchSettlement.polityTravelTauLogisticsWeight);
    const double wControl = std::max(0.0, m_ctx->config.researchSettlement.polityControlWeight);
    const double wStrength = std::max(0.0, m_ctx->config.researchSettlement.polityStrengthWeight);
    const double wDist = std::max(0.0, m_ctx->config.researchSettlement.polityDistancePenaltyWeight);
    const double wShock = std::max(0.0, m_ctx->config.researchSettlement.polityDefectionShockWeight);
    const double cal0 = std::max(1.0e-9, m_ctx->config.settlements.cal0);

    auto utilityFor = [&](int nodeIndex, int countryIndex) -> double {
        if (nodeIndex < 0 || nodeIndex >= nNode || countryIndex < 0 || countryIndex >= nCountry) {
            return -1.0e9;
        }
        const SettlementNode& n = m_nodes[static_cast<size_t>(nodeIndex)];
        const Country& c = countries[static_cast<size_t>(countryIndex)];
        const auto& m = c.getMacroEconomy();
        const double join = 0.5 + 0.5 * ((static_cast<size_t>(nodeIndex) < m_nodeJoinUtility.size()) ? m_nodeJoinUtility[static_cast<size_t>(nodeIndex)] : 0.0);

        const double disease = (static_cast<size_t>(nodeIndex) < m_nodeDiseaseBurden.size()) ? clamp01(m_nodeDiseaseBurden[static_cast<size_t>(nodeIndex)]) : 0.0;
        const double warAttr = (static_cast<size_t>(nodeIndex) < m_nodeWarAttrition.size()) ? clamp01(m_nodeWarAttrition[static_cast<size_t>(nodeIndex)]) : 0.0;
        const double perCap = n.calories / std::max(1.0, n.population);
        const double foodStress = clamp01((cal0 - perCap) / std::max(cal0, 1.0e-9));
        const double shock = clamp01(0.45 * disease + 0.35 * warAttr + 0.20 * foodStress);

        double travel = maxTravel + 1.0;
        if (useControlGraph) {
            auto it = distByCountry.find(countryIndex);
            if (it != distByCountry.end() && static_cast<size_t>(nodeIndex) < it->second.size()) {
                travel = it->second[static_cast<size_t>(nodeIndex)];
            }
            if (!std::isfinite(travel) || travel > maxTravel) {
                return -1.0e9;
            }
        } else {
            const sf::Vector2i cStart = c.getStartingPixel();
            const double cFx = static_cast<double>(cStart.x / Map::kFieldCellSize);
            const double cFy = static_cast<double>(cStart.y / Map::kFieldCellSize);
            travel = std::sqrt((n.fieldX - cFx) * (n.fieldX - cFx) + (n.fieldY - cFy) * (n.fieldY - cFy));
        }

        const double tau = tauBase + tauAdminW * clamp01(c.getAdminCapacity()) + tauLogiW * clamp01(c.getLogisticsReach());
        const double control = std::exp(-travel / std::max(0.5, tau));
        const double strength = countryStrength[static_cast<size_t>(countryIndex)];
        const double distancePenalty = wDist * clamp01(travel / maxTravel);
        const double shockPenalty = wShock * shock * (1.0 - control);
        const double warPenalty = c.isAtWar() ? 0.08 : 0.0;

        return join + wControl * control + wStrength * strength - distancePenalty - shockPenalty - warPenalty;
    };

    for (int i = 0; i < nNode; ++i) {
        const SettlementNode& n = m_nodes[static_cast<size_t>(i)];
        const int from = n.ownerCountry;
        if (from < 0 || from >= nCountry) continue;

        std::vector<int> candidates;
        candidates.push_back(from);
        for (const TransportEdge& e : m_edges) {
            int other = -1;
            if (e.fromNode == i) other = e.toNode;
            else if (e.toNode == i) other = e.fromNode;
            if (other < 0 || other >= nNode) continue;
            const int oc = m_nodes[static_cast<size_t>(other)].ownerCountry;
            if (oc >= 0 && oc < nCountry &&
                std::find(candidates.begin(), candidates.end(), oc) == candidates.end()) {
                candidates.push_back(oc);
            }
        }
        for (int bc : bestCountries) {
            if (std::find(candidates.begin(), candidates.end(), bc) == candidates.end()) {
                candidates.push_back(bc);
            }
        }
        std::sort(candidates.begin(), candidates.end());
        candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

        const double baseU = utilityFor(i, from);

        int bestCountry = from;
        double bestGain = 0.0;
        for (int c : candidates) {
            if (c < 0 || c >= nCountry || c == from) continue;
            const double u = utilityFor(i, c);
            if (!std::isfinite(u)) continue;
            const double gain = u - baseU;
            if (gain > bestGain || (gain == bestGain && c < bestCountry)) {
                bestGain = gain;
                bestCountry = c;
            }
        }
        if (bestCountry != from && bestGain >= threshold) {
            proposals.push_back(Proposal{i, from, bestCountry, bestGain});
        }
    }

    std::sort(proposals.begin(), proposals.end(), [&](const Proposal& a, const Proposal& b) {
        if (a.gain != b.gain) return a.gain > b.gain;
        return m_nodes[static_cast<size_t>(a.node)].id < m_nodes[static_cast<size_t>(b.node)].id;
    });

    const int maxSwitches = std::max(1, static_cast<int>(std::floor(
        std::clamp(m_ctx->config.researchSettlement.politySwitchMaxNodeShare, 0.0, 1.0) * static_cast<double>(nNode))));
    std::vector<uint8_t> switched(static_cast<size_t>(nNode), 0u);
    std::vector<double> countryDelta(static_cast<size_t>(nCountry), 0.0);
    int applied = 0;
    for (const Proposal& p : proposals) {
        if (applied >= maxSwitches) break;
        if (p.node < 0 || p.node >= nNode) continue;
        if (switched[static_cast<size_t>(p.node)] != 0u) continue;
        SettlementNode& n = m_nodes[static_cast<size_t>(p.node)];
        if (n.ownerCountry != p.fromCountry) continue;
        n.ownerCountry = p.toCountry;
        n.localLegitimacy = std::clamp(0.80 * n.localLegitimacy + 0.20 * countries[static_cast<size_t>(p.toCountry)].getLegitimacy(), 0.0, 1.0);
        n.localAdminCapacity = std::clamp(0.82 * n.localAdminCapacity + 0.18 * countries[static_cast<size_t>(p.toCountry)].getAdminCapacity(), 0.0, 1.0);
        m_nodePolitySwitchGain[static_cast<size_t>(p.node)] = p.gain;
        switched[static_cast<size_t>(p.node)] = 1u;
        const double pop = std::max(0.0, n.population);
        countryDelta[static_cast<size_t>(p.toCountry)] += pop * p.gain;
        countryDelta[static_cast<size_t>(p.fromCountry)] -= pop * p.gain;
        ++applied;
    }

    std::vector<double> controlWeighted(static_cast<size_t>(nCountry), 0.0);
    std::vector<double> controlPop(static_cast<size_t>(nCountry), 0.0);
    if (useControlGraph) {
        for (int i = 0; i < nNode; ++i) {
            const SettlementNode& n = m_nodes[static_cast<size_t>(i)];
            const int c = n.ownerCountry;
            if (c < 0 || c >= nCountry) continue;
            auto it = distByCountry.find(c);
            if (it == distByCountry.end() || static_cast<size_t>(i) >= it->second.size()) continue;
            const double travel = it->second[static_cast<size_t>(i)];
            if (!std::isfinite(travel) || travel > maxTravel) continue;
            const double tau = tauBase +
                               tauAdminW * clamp01(countries[static_cast<size_t>(c)].getAdminCapacity()) +
                               tauLogiW * clamp01(countries[static_cast<size_t>(c)].getLogisticsReach());
            const double ctl = std::exp(-travel / std::max(0.5, tau));
            const double pop = std::max(0.0, n.population);
            controlWeighted[static_cast<size_t>(c)] += pop * ctl;
            controlPop[static_cast<size_t>(c)] += pop;
        }
    }

    for (int c = 0; c < nCountry; ++c) {
        const double nrm = countryDelta[static_cast<size_t>(c)] / std::max(1.0, static_cast<double>(countries[static_cast<size_t>(c)].getPopulation()));
        const double ctlTarget = (controlPop[static_cast<size_t>(c)] > 1.0e-9)
            ? (controlWeighted[static_cast<size_t>(c)] / controlPop[static_cast<size_t>(c)])
            : countries[static_cast<size_t>(c)].getAvgControl();
        countries[static_cast<size_t>(c)].setLegitimacy(countries[static_cast<size_t>(c)].getLegitimacy() + 0.25 * nrm + 0.02 * (ctlTarget - 0.5));
        countries[static_cast<size_t>(c)].setAvgControl(
            0.85 * countries[static_cast<size_t>(c)].getAvgControl() +
            0.15 * ctlTarget +
            0.20 * nrm);
    }

    (void)year;
}

void SettlementSystem::updateAdoptionAndJoinUtility(int year, std::vector<Country>& countries) {
    const int nNode = static_cast<int>(m_nodes.size());
    if (nNode <= 0) {
        m_nodeAdoptionPressure.clear();
        m_nodeJoinUtility.clear();
        return;
    }

    m_nodeAdoptionPressure.assign(static_cast<size_t>(nNode), 0.0);
    m_nodeJoinUtility.assign(static_cast<size_t>(nNode), 0.0);

    std::vector<double> neighAdopt(static_cast<size_t>(nNode), 0.0);
    std::vector<double> demicAdopt(static_cast<size_t>(nNode), 0.0);
    std::vector<double> culturalAdopt(static_cast<size_t>(nNode), 0.0);
    std::vector<double> prestigeAdopt(static_cast<size_t>(nNode), 0.0);
    std::vector<double> coerciveAdopt(static_cast<size_t>(nNode), 0.0);
    std::vector<double> suit(static_cast<size_t>(nNode), 0.0);
    std::vector<double> elite(static_cast<size_t>(nNode), 0.0);
    std::vector<double> risk(static_cast<size_t>(nNode), 0.0);
    std::vector<double> sec(static_cast<size_t>(nNode), 0.0);
    std::vector<double> trade(static_cast<size_t>(nNode), 0.0);
    std::vector<double> pub(static_cast<size_t>(nNode), 0.0);
    std::vector<double> tax(static_cast<size_t>(nNode), 0.0);
    std::vector<double> opp(static_cast<size_t>(nNode), 0.0);
    std::vector<double> stay(static_cast<size_t>(nNode), 0.0);

    std::vector<double> neighWeight(static_cast<size_t>(nNode), 0.0);
    for (const TransportEdge& e : m_edges) {
        if (e.fromNode < 0 || e.toNode < 0 || e.fromNode >= nNode || e.toNode >= nNode) continue;
        const SettlementNode& a = m_nodes[static_cast<size_t>(e.fromNode)];
        const SettlementNode& b = m_nodes[static_cast<size_t>(e.toNode)];
        const double wa = std::max(0.01, e.reliability / (1.0 + e.cost));
        const double pa = clamp01(static_cast<double>(b.adoptedPackages.size()) / 6.0);
        const double pb = clamp01(static_cast<double>(a.adoptedPackages.size()) / 6.0);
        const double flowA = (static_cast<size_t>(e.fromNode) < m_nodeOutgoingFlow.size())
            ? clamp01(m_nodeOutgoingFlow[static_cast<size_t>(e.fromNode)] / std::max(1.0, a.population))
            : 0.0;
        const double flowB = (static_cast<size_t>(e.toNode) < m_nodeOutgoingFlow.size())
            ? clamp01(m_nodeOutgoingFlow[static_cast<size_t>(e.toNode)] / std::max(1.0, b.population))
            : 0.0;
        const double prestigeA = clamp01(0.52 * clamp01(a.specialistShare) + 0.48 * clamp01(std::log1p(std::max(0.0, a.population)) / 10.0));
        const double prestigeB = clamp01(0.52 * clamp01(b.specialistShare) + 0.48 * clamp01(std::log1p(std::max(0.0, b.population)) / 10.0));

        culturalAdopt[static_cast<size_t>(e.fromNode)] += wa * pa;
        culturalAdopt[static_cast<size_t>(e.toNode)] += wa * pb;
        demicAdopt[static_cast<size_t>(e.fromNode)] += wa * pa * flowB;
        demicAdopt[static_cast<size_t>(e.toNode)] += wa * pb * flowA;
        prestigeAdopt[static_cast<size_t>(e.fromNode)] += wa * pa * prestigeB;
        prestigeAdopt[static_cast<size_t>(e.toNode)] += wa * pb * prestigeA;
        neighWeight[static_cast<size_t>(e.fromNode)] += wa;
        neighWeight[static_cast<size_t>(e.toNode)] += wa;
    }

    for (int i = 0; i < nNode; ++i) {
        const SettlementNode& n = m_nodes[static_cast<size_t>(i)];
        const double fert =
            (fieldIndex(n.fieldX, n.fieldY) >= 0 && static_cast<size_t>(fieldIndex(n.fieldX, n.fieldY)) < m_fieldFertility.size())
            ? clamp01(static_cast<double>(m_fieldFertility[static_cast<size_t>(fieldIndex(n.fieldX, n.fieldY))]))
            : 0.6;
        const double market = clamp01((i < static_cast<int>(m_nodeMarketPotential.size())) ? (m_nodeMarketPotential[static_cast<size_t>(i)] / 70.0) : 0.0);
        const double know = (i < static_cast<int>(m_nodeKnowledgeCoverage.size())) ? clamp01(m_nodeKnowledgeCoverage[static_cast<size_t>(i)]) : 0.0;
        suit[static_cast<size_t>(i)] = clamp01(0.45 * fert + 0.35 * market + 0.20 * know);

        if (n.ownerCountry >= 0 && static_cast<size_t>(n.ownerCountry) < countries.size()) {
            const Country& c = countries[static_cast<size_t>(n.ownerCountry)];
            const auto& m = c.getMacroEconomy();
            elite[static_cast<size_t>(i)] = clamp01(c.getInstitutionCapacity());
            risk[static_cast<size_t>(i)] = clamp01(
                0.45 * ((i < static_cast<int>(m_nodeDiseaseBurden.size())) ? m_nodeDiseaseBurden[static_cast<size_t>(i)] : 0.0) +
                0.35 * clamp01(m.famineSeverity) +
                0.20 * (c.isAtWar() ? 1.0 : 0.0));
            sec[static_cast<size_t>(i)] = clamp01(0.60 * c.getAvgControl() + 0.40 * c.getAdminCapacity());
            trade[static_cast<size_t>(i)] = market;
            pub[static_cast<size_t>(i)] = clamp01(0.65 * c.getInstitutionCapacity() + 0.35 * c.getLegitimacy());
            tax[static_cast<size_t>(i)] = clamp01(c.getTaxRate());
            opp[static_cast<size_t>(i)] = clamp01(0.55 * (1.0 - c.getLegitimacy()) + 0.45 * c.getInequality());
            stay[static_cast<size_t>(i)] = clamp01(0.50 * sec[static_cast<size_t>(i)] + 0.25 * trade[static_cast<size_t>(i)] + 0.25 * (1.0 - risk[static_cast<size_t>(i)]));
        } else {
            elite[static_cast<size_t>(i)] = 0.25;
            risk[static_cast<size_t>(i)] = 0.35;
            sec[static_cast<size_t>(i)] = 0.30;
            trade[static_cast<size_t>(i)] = market;
            pub[static_cast<size_t>(i)] = 0.30;
            tax[static_cast<size_t>(i)] = 0.10;
            opp[static_cast<size_t>(i)] = 0.20;
            stay[static_cast<size_t>(i)] = 0.30;
        }

        const double switchGain = (static_cast<size_t>(i) < m_nodePolitySwitchGain.size())
            ? clamp01(std::abs(m_nodePolitySwitchGain[static_cast<size_t>(i)]))
            : 0.0;
        const double coerciveBase =
            std::max(0.0, m_ctx->config.packageDiffusion.coerciveExtractionWeight) * clamp01(n.extractionRate) +
            std::max(0.0, m_ctx->config.packageDiffusion.coercivePolitySwitchWeight) * switchGain;
        coerciveAdopt[static_cast<size_t>(i)] = clamp01(coerciveBase);
    }

    const bool diffusionEnabled = m_ctx->config.packageDiffusion.enabled;
    const double demicW = std::max(0.0, m_ctx->config.packageDiffusion.demicWeight);
    const double culturalW = std::max(0.0, m_ctx->config.packageDiffusion.culturalWeight);
    const double coerciveW = std::max(0.0, m_ctx->config.packageDiffusion.coerciveWeight);
    const double prestigeW = std::clamp(m_ctx->config.packageDiffusion.prestigeWeight, 0.0, 1.0);
    const double confTh = std::clamp(m_ctx->config.packageDiffusion.conformistThreshold, 0.0, 1.0);
    const double confStrength = std::max(0.0, m_ctx->config.packageDiffusion.conformistStrength);
    const double wDenom = std::max(1.0e-9, demicW + culturalW + coerciveW);

    for (int i = 0; i < nNode; ++i) {
        const size_t ii = static_cast<size_t>(i);
        if (neighWeight[ii] > 1.0e-9) {
            culturalAdopt[ii] /= neighWeight[ii];
            demicAdopt[ii] /= neighWeight[ii];
            prestigeAdopt[ii] /= neighWeight[ii];
        }
        const double culturalBlend = (1.0 - prestigeW) * culturalAdopt[ii] + prestigeW * prestigeAdopt[ii];
        const double raw =
            (demicW * demicAdopt[ii] + culturalW * culturalBlend + coerciveW * coerciveAdopt[ii]) / wDenom;
        const double conf = sigmoid((raw - confTh) * confStrength * 8.0);
        neighAdopt[ii] = diffusionEnabled
            ? clamp01(raw * (0.70 + 0.60 * conf))
            : clamp01(culturalAdopt[ii]);
    }

    bool usedGpu = false;
    if (m_gpu && m_gpu->enabled && m_gpu->adoptionJoinKernel &&
        (m_ctx->config.researchGpu.adoptionKernel || m_ctx->config.researchGpu.joinStayUtility)) {
        std::vector<sf::Uint8> aPx(static_cast<size_t>(nNode) * 4u, 0u);
        std::vector<sf::Uint8> bPx(static_cast<size_t>(nNode) * 4u, 0u);
        std::vector<sf::Uint8> cPx(static_cast<size_t>(nNode) * 4u, 0u);
        for (int i = 0; i < nNode; ++i) {
            const size_t p = static_cast<size_t>(i) * 4u;
            aPx[p + 0u] = static_cast<sf::Uint8>(std::round(clamp01(neighAdopt[static_cast<size_t>(i)]) * 255.0));
            aPx[p + 1u] = static_cast<sf::Uint8>(std::round(clamp01(suit[static_cast<size_t>(i)]) * 255.0));
            aPx[p + 2u] = static_cast<sf::Uint8>(std::round(clamp01(elite[static_cast<size_t>(i)]) * 255.0));
            aPx[p + 3u] = static_cast<sf::Uint8>(std::round(clamp01(risk[static_cast<size_t>(i)]) * 255.0));
            bPx[p + 0u] = static_cast<sf::Uint8>(std::round(clamp01(sec[static_cast<size_t>(i)]) * 255.0));
            bPx[p + 1u] = static_cast<sf::Uint8>(std::round(clamp01(trade[static_cast<size_t>(i)]) * 255.0));
            bPx[p + 2u] = static_cast<sf::Uint8>(std::round(clamp01(pub[static_cast<size_t>(i)]) * 255.0));
            bPx[p + 3u] = static_cast<sf::Uint8>(std::round(clamp01(tax[static_cast<size_t>(i)]) * 255.0));
            cPx[p + 0u] = static_cast<sf::Uint8>(std::round(clamp01(opp[static_cast<size_t>(i)]) * 255.0));
            cPx[p + 1u] = static_cast<sf::Uint8>(std::round(clamp01(stay[static_cast<size_t>(i)]) * 255.0));
            cPx[p + 2u] = 0u;
            cPx[p + 3u] = 255u;
        }

        sf::Texture aTex, bTex, cTex;
        if (aTex.create(static_cast<unsigned>(nNode), 1u) &&
            bTex.create(static_cast<unsigned>(nNode), 1u) &&
            cTex.create(static_cast<unsigned>(nNode), 1u)) {
            aTex.setSmooth(false);
            bTex.setSmooth(false);
            cTex.setSmooth(false);
            aTex.update(aPx.data());
            bTex.update(bPx.data());
            cTex.update(cPx.data());

            m_gpu->adoptionJoinShader.setUniform("aTex", aTex);
            m_gpu->adoptionJoinShader.setUniform("bTex", bTex);
            m_gpu->adoptionJoinShader.setUniform("cTex", cTex);
            m_gpu->adoptionJoinShader.setUniform("theta0", -0.55f);
            m_gpu->adoptionJoinShader.setUniform("theta1", 1.45f);
            m_gpu->adoptionJoinShader.setUniform("theta2", 1.10f);
            m_gpu->adoptionJoinShader.setUniform("theta3", 0.90f);
            m_gpu->adoptionJoinShader.setUniform("theta4", 1.40f);

            std::vector<sf::Uint8> outPx;
            if (runShaderPass1D(m_gpu->adoptionJoinShader, aTex, nNode, outPx)) {
                usedGpu = true;
                for (int i = 0; i < nNode; ++i) {
                    const size_t p = static_cast<size_t>(i) * 4u;
                    m_nodeAdoptionPressure[static_cast<size_t>(i)] = static_cast<double>(outPx[p + 0u]) / 255.0;
                    m_nodeJoinUtility[static_cast<size_t>(i)] =
                        std::clamp((static_cast<double>(outPx[p + 1u]) / 255.0) * 2.0 - 1.0, -1.0, 1.0);
                }
            }
        }
    }

    if (!usedGpu) {
        for (int i = 0; i < nNode; ++i) {
            const double z =
                -0.55 +
                1.45 * neighAdopt[static_cast<size_t>(i)] +
                1.10 * suit[static_cast<size_t>(i)] +
                0.90 * elite[static_cast<size_t>(i)] -
                1.40 * risk[static_cast<size_t>(i)];
            m_nodeAdoptionPressure[static_cast<size_t>(i)] = sigmoid(z);

            const double uJoin =
                sec[static_cast<size_t>(i)] +
                trade[static_cast<size_t>(i)] +
                pub[static_cast<size_t>(i)] -
                tax[static_cast<size_t>(i)] -
                opp[static_cast<size_t>(i)] -
                0.50 * risk[static_cast<size_t>(i)];
            m_nodeJoinUtility[static_cast<size_t>(i)] = std::clamp(uJoin - stay[static_cast<size_t>(i)], -1.0, 1.0);
        }
    }

    const auto& pkgs = getDefaultDomesticPackages();
    for (int i = 0; i < nNode; ++i) {
        SettlementNode& n = m_nodes[static_cast<size_t>(i)];
        const std::uint64_t bits = SimulationContext::mix64(
            m_ctx->worldSeed ^
            (static_cast<std::uint64_t>(year + 120000) * 0x9E3779B97F4A7C15ull) ^
            (static_cast<std::uint64_t>(n.id + 31) * 0xBF58476D1CE4E5B9ull));
        const double jitter = (SimulationContext::u01FromU64(bits) - 0.5) * 0.12;
        const double threshold = 0.58 + jitter;
        const double pAdopt = clamp01(m_nodeAdoptionPressure[static_cast<size_t>(i)]);
        if (pAdopt >= threshold && m_ctx->config.packages.enabled) {
            int bestId = -1;
            double bestScore = -1.0;
            for (const DomesticPackageDefinition& pkg : pkgs) {
                if (std::find(n.adoptedPackages.begin(), n.adoptedPackages.end(), pkg.id) != n.adoptedPackages.end()) {
                    continue;
                }
                const double sc =
                    0.42 * pAdopt +
                    0.28 * clamp01(suit[static_cast<size_t>(i)] + 0.25 * pkg.marketAffinity) +
                    0.20 * neighAdopt[static_cast<size_t>(i)] +
                    0.10 * coerciveAdopt[static_cast<size_t>(i)];
                if (sc > bestScore || (sc == bestScore && pkg.id < bestId)) {
                    bestScore = sc;
                    bestId = pkg.id;
                }
            }
            if (bestId >= 0) {
                n.adoptedPackages.push_back(bestId);
                std::sort(n.adoptedPackages.begin(), n.adoptedPackages.end());
                n.storageStock = std::min(1.8, n.storageStock + 0.02);
            }
        }
    }

    // Apply Eq24-like utility feedback to polity legitimacy/control signals.
    std::vector<double> weightedJoin(static_cast<size_t>(countries.size()), 0.0);
    std::vector<double> popWeight(static_cast<size_t>(countries.size()), 0.0);
    for (int i = 0; i < nNode; ++i) {
        const SettlementNode& n = m_nodes[static_cast<size_t>(i)];
        if (n.ownerCountry < 0 || static_cast<size_t>(n.ownerCountry) >= countries.size()) continue;
        const double pop = std::max(0.0, n.population);
        weightedJoin[static_cast<size_t>(n.ownerCountry)] += pop * m_nodeJoinUtility[static_cast<size_t>(i)];
        popWeight[static_cast<size_t>(n.ownerCountry)] += pop;
    }
    if (m_ctx->config.researchGpu.joinStayUtility) {
        for (size_t ci = 0; ci < countries.size(); ++ci) {
            if (popWeight[ci] <= 1.0e-9) continue;
            Country& c = countries[ci];
            const double avgJoin = weightedJoin[ci] / popWeight[ci];
            c.setLegitimacy(c.getLegitimacy() + 0.0025 * avgJoin);
            c.setAvgControl(c.getAvgControl() + 0.0030 * avgJoin);
        }
    }
}

void SettlementSystem::aggregateToCountries(std::vector<Country>& countries) {
    const int nCountry = static_cast<int>(countries.size());
    m_countryAgg.assign(static_cast<size_t>(nCountry), SettlementCountryAggregate{});
    std::vector<double> popWeight(static_cast<size_t>(nCountry), 0.0);

    const double cal0 = std::max(1.0e-9, m_ctx->config.settlements.cal0);

    for (size_t i = 0; i < m_nodes.size(); ++i) {
        const SettlementNode& n = m_nodes[i];
        if (n.ownerCountry < 0 || n.ownerCountry >= nCountry) continue;

        SettlementCountryAggregate& a = m_countryAgg[static_cast<size_t>(n.ownerCountry)];
        const double pop = std::max(0.0, n.population);
        const double market = clamp01((i < m_nodeMarketPotential.size()) ? (m_nodeMarketPotential[i] / 70.0) : 0.0);
        const double perCap = n.calories / std::max(1.0, pop);
        const double foodStress = clamp01((cal0 - perCap) / std::max(cal0, 1.0e-9));
        const double warAttr = (i < m_nodeWarAttrition.size()) ? clamp01(m_nodeWarAttrition[i]) : 0.0;
        const double extractionRev = (i < m_nodeExtractionRevenue.size()) ? std::max(0.0, m_nodeExtractionRevenue[i]) : 0.0;

        a.specialistPopulation += pop * clamp01(n.specialistShare);
        a.marketPotential += pop * market;
        a.migrationPressureOut += pop * clamp01(foodStress + 0.45 * warAttr);
        a.migrationAttractiveness += pop * clamp01(0.62 * market + 0.38 * (1.0 - foodStress));
        a.knowledgeInfraSignal += pop * clamp01(0.45 * n.specialistShare + 0.35 * market + 0.20 * clamp01(extractionRev / std::max(1.0, pop)));
        popWeight[static_cast<size_t>(n.ownerCountry)] += pop;
    }

    for (int i = 0; i < nCountry; ++i) {
        Country& c = countries[static_cast<size_t>(i)];
        Country::MacroEconomyState& m = c.getMacroEconomyMutable();
        const double w = std::max(1.0, popWeight[static_cast<size_t>(i)]);
        SettlementCountryAggregate& a = m_countryAgg[static_cast<size_t>(i)];

        a.marketPotential /= w;
        a.migrationPressureOut /= w;
        a.migrationAttractiveness /= w;
        a.knowledgeInfraSignal /= w;

        c.setSpecialistPopulation(a.specialistPopulation);

        const double infraTarget =
            0.08 +
            2.5e-6 * a.specialistPopulation +
            0.28 * clamp01(a.knowledgeInfraSignal) +
            0.16 * clamp01(a.marketPotential);
        c.setKnowledgeInfra(0.80 * c.getKnowledgeInfra() + 0.20 * infraTarget);

        m.marketAccess = clamp01(0.70 * clamp01(m.marketAccess) + 0.30 * clamp01(a.marketPotential));
        m.migrationPressureOut = clamp01(0.62 * clamp01(m.migrationPressureOut) + 0.38 * clamp01(a.migrationPressureOut));
        m.migrationAttractiveness = clamp01(0.62 * clamp01(m.migrationAttractiveness) + 0.38 * clamp01(a.migrationAttractiveness));
    }
}

void SettlementSystem::buildCountryTradeHintMatrix(int countryCount) {
    if (countryCount <= 0) {
        m_countryTradeHintMatrix.clear();
        return;
    }
    const size_t n = static_cast<size_t>(countryCount);
    if (m_countryTradeHintMatrix.size() != n * n) {
        m_countryTradeHintMatrix.assign(n * n, 0.0f);
    }

    for (int i = 0; i < countryCount; ++i) {
        size_t row = static_cast<size_t>(i) * n;
        double rowMax = 0.0;
        for (int j = 0; j < countryCount; ++j) {
            if (i == j) {
                m_countryTradeHintMatrix[row + static_cast<size_t>(j)] = 0.0f;
                continue;
            }
            rowMax = std::max(rowMax, static_cast<double>(m_countryTradeHintMatrix[row + static_cast<size_t>(j)]));
        }
        if (rowMax <= 1.0e-9) {
            continue;
        }
        for (int j = 0; j < countryCount; ++j) {
            if (i == j) {
                m_countryTradeHintMatrix[row + static_cast<size_t>(j)] = 0.0f;
                continue;
            }
            const double v = static_cast<double>(m_countryTradeHintMatrix[row + static_cast<size_t>(j)]) / rowMax;
            m_countryTradeHintMatrix[row + static_cast<size_t>(j)] = static_cast<float>(std::clamp(v, 0.0, 1.0));
        }
    }
}

void SettlementSystem::rebuildOverlays() {
    const int nField = std::max(0, m_fieldW * m_fieldH);
    m_overlayNodePopulation.assign(static_cast<size_t>(nField), 0.0f);
    m_overlayDominantMode.assign(static_cast<size_t>(nField), 255u);
    m_overlayTransportDensity.assign(static_cast<size_t>(nField), 0.0f);

    for (const SettlementNode& n : m_nodes) {
        const int fi = fieldIndex(n.fieldX, n.fieldY);
        if (fi < 0 || static_cast<size_t>(fi) >= m_overlayNodePopulation.size()) continue;
        m_overlayNodePopulation[static_cast<size_t>(fi)] = static_cast<float>(
            std::max(static_cast<double>(m_overlayNodePopulation[static_cast<size_t>(fi)]), n.population));

        size_t best = 0;
        double bestV = n.mix[0];
        for (size_t k = 1; k < n.mix.size(); ++k) {
            if (n.mix[k] > bestV) {
                bestV = n.mix[k];
                best = k;
            }
        }
        m_overlayDominantMode[static_cast<size_t>(fi)] = static_cast<std::uint8_t>(best);
    }

    for (const TransportEdge& e : m_edges) {
        if (e.fromNode < 0 || e.toNode < 0 ||
            e.fromNode >= static_cast<int>(m_nodes.size()) ||
            e.toNode >= static_cast<int>(m_nodes.size())) {
            continue;
        }
        const SettlementNode& a = m_nodes[static_cast<size_t>(e.fromNode)];
        const SettlementNode& b = m_nodes[static_cast<size_t>(e.toNode)];
        const int fia = fieldIndex(a.fieldX, a.fieldY);
        const int fib = fieldIndex(b.fieldX, b.fieldY);
        const double v = std::max(0.0, e.capacity * e.reliability / (1.0 + e.cost));
        if (fia >= 0 && static_cast<size_t>(fia) < m_overlayTransportDensity.size()) {
            m_overlayTransportDensity[static_cast<size_t>(fia)] += static_cast<float>(v);
        }
        if (fib >= 0 && static_cast<size_t>(fib) < m_overlayTransportDensity.size()) {
            m_overlayTransportDensity[static_cast<size_t>(fib)] += static_cast<float>(v);
        }
    }
}

void SettlementSystem::computeDeterminismHash() {
    std::uint64_t h = 0x51E771E5E771A9BFull;
    h = mixHash(h, static_cast<std::uint64_t>(m_nodes.size()));
    h = mixHash(h, static_cast<std::uint64_t>(m_edges.size()));

    for (const SettlementNode& n : m_nodes) {
        h = mixHash(h, static_cast<std::uint64_t>(std::max(0, n.id) + 1));
        h = mixHash(h, static_cast<std::uint64_t>(std::max(0, n.ownerCountry) + 1));
        h = mixHash(h, static_cast<std::uint64_t>(std::max(0, n.fieldX + 1)));
        h = mixHash(h, static_cast<std::uint64_t>(std::max(0, n.fieldY + 1)));
        h = mixHash(h, hashDouble(n.population, 1.0e3));
        h = mixHash(h, hashDouble(n.carryingCapacity, 1.0e3));
        h = mixHash(h, hashDouble(n.foodProduced, 1.0e5));
        h = mixHash(h, hashDouble(n.calories, 1.0e5));
        h = mixHash(h, hashDouble(n.specialistShare, 1.0e6));
        h = mixHash(h, hashDouble(n.irrigationCapital, 1.0e6));
        h = mixHash(h, hashDouble(n.eliteShare, 1.0e6));
        h = mixHash(h, hashDouble(n.localLegitimacy, 1.0e6));
        h = mixHash(h, hashDouble(n.localAdminCapacity, 1.0e6));
        h = mixHash(h, hashDouble(n.extractionRate, 1.0e6));
        for (double m : n.mix) {
            h = mixHash(h, hashDouble(m, 1.0e6));
        }
    }

    for (const TransportEdge& e : m_edges) {
        h = mixHash(h, static_cast<std::uint64_t>(std::max(0, e.fromNode) + 1));
        h = mixHash(h, static_cast<std::uint64_t>(std::max(0, e.toNode) + 1));
        h = mixHash(h, hashDouble(e.cost, 1.0e6));
        h = mixHash(h, hashDouble(e.capacity, 1.0e6));
        h = mixHash(h, hashDouble(e.reliability, 1.0e6));
        h = mixHash(h, static_cast<std::uint64_t>(e.seaLink ? 1 : 0));
        h = mixHash(h, hashDouble(e.campaignLoad, 1.0e6));
        h = mixHash(h, hashDouble(e.campaignDeficit, 1.0e6));
        h = mixHash(h, hashDouble(e.campaignAttrition, 1.0e6));
    }
    for (double v : m_nodeKnowledgeCoverage) {
        h = mixHash(h, hashDouble(v, 1.0e6));
    }
    for (double v : m_nodeUncertainty) {
        h = mixHash(h, hashDouble(v, 1.0e6));
    }
    for (double v : m_nodeKnowledgeErosion) {
        h = mixHash(h, hashDouble(v, 1.0e6));
    }
    for (double v : m_nodePrevMarketPotential) {
        h = mixHash(h, hashDouble(v, 1.0e6));
    }
    for (double v : m_edgeExplorationBoost) {
        h = mixHash(h, hashDouble(v, 1.0e6));
    }

    for (float f : m_fieldIrrigationCapital) {
        h = mixHash(h, hashDouble(static_cast<double>(f), 1.0e6));
    }
    for (float f : m_fieldSalinity) {
        h = mixHash(h, hashDouble(static_cast<double>(f), 1.0e6));
    }
    for (float f : m_fieldPaleoTempAdj) {
        h = mixHash(h, hashDouble(static_cast<double>(f), 1.0e6));
    }
    for (float f : m_fieldPaleoPrecipAdj) {
        h = mixHash(h, hashDouble(static_cast<double>(f), 1.0e6));
    }

    m_lastDeterminismHash = h;
}

std::string SettlementSystem::validateInvariants(const Map& map, int countryCount) const {
    if (!enabled()) {
        return std::string();
    }

    const auto& landMask = map.getFieldLandMask();
    for (size_t i = 0; i < m_nodes.size(); ++i) {
        const SettlementNode& n = m_nodes[i];
        if (n.fieldX < 0 || n.fieldY < 0 || n.fieldX >= m_fieldW || n.fieldY >= m_fieldH) {
            std::ostringstream oss;
            oss << "settlement node out of bounds id=" << n.id;
            return oss.str();
        }
        const int fi = fieldIndex(n.fieldX, n.fieldY);
        if (fi < 0 || static_cast<size_t>(fi) >= landMask.size() || landMask[static_cast<size_t>(fi)] == 0u) {
            std::ostringstream oss;
            oss << "settlement node not on land id=" << n.id;
            return oss.str();
        }
        if (n.ownerCountry < -1 || n.ownerCountry >= countryCount) {
            std::ostringstream oss;
            oss << "settlement owner out of range id=" << n.id;
            return oss.str();
        }
        if (!std::isfinite(n.population) || n.population < 0.0) {
            std::ostringstream oss;
            oss << "settlement population invalid id=" << n.id;
            return oss.str();
        }
        if (!std::isfinite(n.carryingCapacity) || n.carryingCapacity < 0.0) {
            std::ostringstream oss;
            oss << "settlement K invalid id=" << n.id;
            return oss.str();
        }
        if (!std::isfinite(n.calories) || !std::isfinite(n.foodProduced) ||
            !std::isfinite(n.foodImported) || !std::isfinite(n.foodExported)) {
            std::ostringstream oss;
            oss << "settlement food/calorie non-finite id=" << n.id;
            return oss.str();
        }
        if (!std::isfinite(n.irrigationCapital) || !std::isfinite(n.eliteShare) ||
            !std::isfinite(n.localLegitimacy) || !std::isfinite(n.localAdminCapacity) ||
            !std::isfinite(n.extractionRate)) {
            std::ostringstream oss;
            oss << "settlement polity stock non-finite id=" << n.id;
            return oss.str();
        }
        if (n.irrigationCapital < 0.0 || n.eliteShare < 0.0 || n.extractionRate < 0.0) {
            std::ostringstream oss;
            oss << "settlement polity stock negative id=" << n.id;
            return oss.str();
        }
        double sumMix = 0.0;
        for (double v : n.mix) {
            if (!std::isfinite(v) || v < 0.0) {
                std::ostringstream oss;
                oss << "settlement mix invalid id=" << n.id;
                return oss.str();
            }
            sumMix += v;
        }
        if (std::abs(sumMix - 1.0) > 1.0e-3) {
            std::ostringstream oss;
            oss << "settlement mix sum drift id=" << n.id << " sum=" << sumMix;
            return oss.str();
        }
    }

    for (const TransportEdge& e : m_edges) {
        if (e.fromNode < 0 || e.toNode < 0 ||
            e.fromNode >= static_cast<int>(m_nodes.size()) ||
            e.toNode >= static_cast<int>(m_nodes.size()) ||
            e.fromNode == e.toNode) {
            return "settlement edge endpoint invalid";
        }
        if (!std::isfinite(e.cost) || !std::isfinite(e.capacity) || !std::isfinite(e.reliability) ||
            !std::isfinite(e.campaignLoad) || !std::isfinite(e.campaignDeficit) || !std::isfinite(e.campaignAttrition) ||
            e.cost < 0.0 || e.capacity < 0.0 || e.reliability < 0.0 || e.campaignLoad < 0.0 || e.campaignDeficit < 0.0 || e.campaignAttrition < 0.0) {
            return "settlement edge non-finite/negative";
        }
    }
    for (double v : m_nodeKnowledgeCoverage) {
        if (!std::isfinite(v) || v < 0.0 || v > 1.0) {
            return "settlement knowledge coverage invalid";
        }
    }
    for (double v : m_nodeUncertainty) {
        if (!std::isfinite(v) || v < 0.0 || v > 1.0) {
            return "settlement uncertainty invalid";
        }
    }
    for (double v : m_nodeKnowledgeErosion) {
        if (!std::isfinite(v) || v < 0.0) {
            return "settlement knowledge erosion invalid";
        }
    }
    for (double v : m_nodePrevMarketPotential) {
        if (!std::isfinite(v) || v < 0.0 || v > 1.0) {
            return "settlement previous market signal invalid";
        }
    }
    for (double v : m_edgeExplorationBoost) {
        if (!std::isfinite(v) || v < 0.0) {
            return "settlement exploration edge boost invalid";
        }
    }

    for (float irr : m_fieldIrrigationCapital) {
        if (!std::isfinite(irr) || irr < 0.0f) {
            return "settlement irrigation field invalid";
        }
    }
    for (float s : m_fieldSalinity) {
        if (!std::isfinite(s) || s < 0.0f || s > 1.0f) {
            return "settlement salinity field invalid";
        }
    }
    for (float v : m_fieldPaleoTempAdj) {
        if (!std::isfinite(v)) {
            return "settlement paleo temp field invalid";
        }
    }
    for (float v : m_fieldPaleoPrecipAdj) {
        if (!std::isfinite(v)) {
            return "settlement paleo precip field invalid";
        }
    }

    for (float v : m_countryTradeHintMatrix) {
        if (!std::isfinite(v) || v < 0.0f) {
            return "settlement country trade hint invalid";
        }
    }
    if (!std::isfinite(m_lastFissionConservationError) || m_lastFissionConservationError > 1.0e-3) {
        return "settlement fission conservation drift";
    }

    return std::string();
}

void SettlementSystem::printDebugSample(int year, const std::vector<Country>& countries, int maxSamples) const {
    if (m_nodes.empty()) {
        std::cout << "[settlement-debug] year=" << year << " nodes=0 edges=" << m_edges.size() << std::endl;
        return;
    }

    struct Row {
        int idx = -1;
        double pop = 0.0;
    };
    std::vector<Row> rows;
    rows.reserve(m_nodes.size());
    for (int i = 0; i < static_cast<int>(m_nodes.size()); ++i) {
        rows.push_back(Row{i, m_nodes[static_cast<size_t>(i)].population});
    }
    std::sort(rows.begin(), rows.end(), [&](const Row& a, const Row& b) {
        if (a.pop != b.pop) return a.pop > b.pop;
        return m_nodes[static_cast<size_t>(a.idx)].id < m_nodes[static_cast<size_t>(b.idx)].id;
    });

    maxSamples = std::max(1, maxSamples);
    if (static_cast<int>(rows.size()) > maxSamples) {
        rows.resize(static_cast<size_t>(maxSamples));
    }

    std::cout << "[settlement-debug] year=" << year
              << " nodes=" << m_nodes.size()
              << " edges=" << m_edges.size()
              << " hintBlend=" << getCountryTradeHintBlend()
              << std::endl;

    for (const Row& r : rows) {
        const SettlementNode& n = m_nodes[static_cast<size_t>(r.idx)];
        const std::string ownerName =
            (n.ownerCountry >= 0 && static_cast<size_t>(n.ownerCountry) < countries.size())
            ? countries[static_cast<size_t>(n.ownerCountry)].getName()
            : std::string("<none>");
        const double outFlow = (static_cast<size_t>(r.idx) < m_nodeOutgoingFlow.size()) ? m_nodeOutgoingFlow[static_cast<size_t>(r.idx)] : 0.0;
        const double warAttr = (static_cast<size_t>(r.idx) < m_nodeWarAttrition.size()) ? m_nodeWarAttrition[static_cast<size_t>(r.idx)] : 0.0;
        const double rev = (static_cast<size_t>(r.idx) < m_nodeExtractionRevenue.size()) ? m_nodeExtractionRevenue[static_cast<size_t>(r.idx)] : 0.0;
        const double know = (static_cast<size_t>(r.idx) < m_nodeKnowledgeCoverage.size()) ? m_nodeKnowledgeCoverage[static_cast<size_t>(r.idx)] : 0.0;
        const double unc = (static_cast<size_t>(r.idx) < m_nodeUncertainty.size()) ? m_nodeUncertainty[static_cast<size_t>(r.idx)] : 1.0;
        const double knowLoss = (static_cast<size_t>(r.idx) < m_nodeKnowledgeErosion.size()) ? m_nodeKnowledgeErosion[static_cast<size_t>(r.idx)] : 0.0;
        const int fi = fieldIndex(n.fieldX, n.fieldY);
        const double sal = (fi >= 0 && static_cast<size_t>(fi) < m_fieldSalinity.size())
            ? static_cast<double>(m_fieldSalinity[static_cast<size_t>(fi)])
            : 0.0;
        std::cout << "  node=" << n.id
                  << " owner=" << n.ownerCountry << "(" << ownerName << ")"
                  << " field=(" << n.fieldX << "," << n.fieldY << ")"
                  << " pop=" << std::llround(n.population)
                  << " K=" << std::llround(n.carryingCapacity)
                  << " cal=" << std::fixed << std::setprecision(3) << n.calories
                  << " mix=[" << n.mix[0] << "," << n.mix[1] << "," << n.mix[2] << "," << n.mix[3] << "," << n.mix[4] << "]"
                  << " packages=" << n.adoptedPackages.size()
                  << " irr=" << n.irrigationCapital
                  << " extRev=" << rev
                  << " warAttr=" << warAttr
                  << " know=" << know
                  << " unc=" << unc
                  << " knowLoss=" << knowLoss
                  << " sal=" << sal
                  << " outFlow=" << outFlow
                  << std::endl;
    }
}
