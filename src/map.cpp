#include "map.h"
#include "technology.h"
#include "culture.h"
#include "great_people.h"
#include "trade.h"
#include "economy.h"
#include <iostream>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <limits>
#include <queue>
#include <unordered_set>
#include <cmath>
#include <algorithm>
#include <array>

namespace {
bool isColorNear(const sf::Color& pixel, const sf::Color& target, int tolerance = 0) {
    return std::abs(static_cast<int>(pixel.r) - static_cast<int>(target.r)) <= tolerance &&
           std::abs(static_cast<int>(pixel.g) - static_cast<int>(target.g)) <= tolerance &&
           std::abs(static_cast<int>(pixel.b) - static_cast<int>(target.b)) <= tolerance;
}
} // namespace

Map::Map(const sf::Image& baseImage,
         const sf::Image& resourceImage,
         const sf::Image& coalImage,
         const sf::Image& copperImage,
         const sf::Image& tinImage,
         const sf::Image& riverlandImage,
         int gridCellSize,
         const sf::Color& landColor,
         const sf::Color& waterColor,
         int regionSize,
         SimulationContext& ctx) :
    m_ctx(&ctx),
    m_gridCellSize(gridCellSize),
    m_regionSize(regionSize),
    m_landColor(landColor),
    m_waterColor(waterColor),
    m_baseImage(baseImage),
    m_resourceImage(resourceImage),
    m_coalImage(coalImage),
    m_copperImage(copperImage),
    m_tinImage(tinImage),
    m_riverlandImage(riverlandImage),
    m_plagueActive(false),
    m_plagueStartYear(0),
    m_plagueDeathToll(0),
    m_plagueAffectedCountries()
{

    m_countryGrid.resize(baseImage.getSize().y / gridCellSize, std::vector<int>(baseImage.getSize().x / gridCellSize, -1));

    m_isLandGrid.resize(baseImage.getSize().y / gridCellSize);
    for (size_t y = 0; y < m_isLandGrid.size(); ++y) {
        m_isLandGrid[y].resize(baseImage.getSize().x / gridCellSize);
        for (size_t x = 0; x < m_isLandGrid[y].size(); ++x) {
            sf::Vector2u pixelPos(static_cast<unsigned int>(x * gridCellSize), static_cast<unsigned int>(y * gridCellSize));
            m_isLandGrid[y][x] = (baseImage.getPixel(pixelPos.x, pixelPos.y) == landColor);
        }
    }

    m_resourceGrid.resize(baseImage.getSize().y / gridCellSize, std::vector<std::unordered_map<Resource::Type, double>>(baseImage.getSize().x / gridCellSize));

    m_resourceColors = {
        {sf::Color(242, 227, 21), Resource::Type::GOLD},
        {sf::Color(0, 0, 0), Resource::Type::IRON},
        {sf::Color(178, 0, 255), Resource::Type::SALT},
        {sf::Color(255, 199, 205), Resource::Type::SALT},
        {sf::Color(127, 0, 55), Resource::Type::HORSES}
    };

	    initializeResourceGrid();
		    rebuildCellFoodCache();
            rebuildCellOreCache();
            rebuildCellEnergyCache();
            rebuildCellConstructionCache();
		    ensureFieldGrids();
	        initializeClimateBaseline();
	        tickWeather(-5000, 1);
	        buildCoastalLandCandidates();
		    std::uniform_int_distribution<> plagueIntervalDist(600, 700);
		    m_plagueInterval = plagueIntervalDist(m_ctx->worldRng);
		    m_nextPlagueYear = -5000 + m_plagueInterval; // First plague year
		}

// 🔥 NUCLEAR OPTIMIZATION: Lightning-fast resource grid initialization
void Map::initializeResourceGrid() {
    std::cout << "🚀 INITIALIZING RESOURCES (Optimized)..." << std::endl;
    auto start = std::chrono::high_resolution_clock::now();

    long long coalCells = 0;
    long long copperCells = 0;
    long long tinCells = 0;
    long long clayCells = 0;
    long long riverlandCells = 0;

    // OPTIMIZATION 1: Use OpenMP parallel processing
    #pragma omp parallel for reduction(+:coalCells,copperCells,tinCells,clayCells,riverlandCells)
    for (int y = 0; y < static_cast<int>(m_isLandGrid.size()); ++y) {
        for (size_t x = 0; x < m_isLandGrid[y].size(); ++x) {
            if (m_isLandGrid[y][x]) {
                // Food potential baseline: strong ecological gradients from latitude + humidity + coast.
                double foodAmount = 0.0;

                const int mapH = static_cast<int>(m_isLandGrid.size());
                const int mapW = (mapH > 0) ? static_cast<int>(m_isLandGrid[0].size()) : 1;
                const double lat01 = std::abs(((static_cast<double>(y) + 0.5) / static_cast<double>(std::max(1, mapH))) - 0.5) * 2.0;
                const double x01 = (mapW > 1) ? (static_cast<double>(x) / static_cast<double>(mapW - 1)) : 0.5;

                auto clamp01d = [](double v) { return std::max(0.0, std::min(1.0, v)); };
                const int dx[] = {-1, -1, -1, 0, 0, 1, 1, 1};
                const int dy[] = {-1, 0, 1, -1, 1, -1, 0, 1};
                bool coastalAdj = false;
                for (int i = 0; i < 8; ++i) {
                    int nx = static_cast<int>(x) + dx[i];
                    int ny = y + dy[i];
                    if (nx >= 0 && nx < static_cast<int>(m_isLandGrid[0].size()) && 
                        ny >= 0 && ny < static_cast<int>(m_isLandGrid.size())) {
                        if (!m_isLandGrid[ny][nx]) { // Found water
                            coastalAdj = true;
                            break;
                        }
                    }
                }

                const double equatorialWet = std::exp(-std::pow(lat01 / 0.34, 2.0));
                const double subtropicalDry = std::exp(-std::pow((lat01 - 0.30) / 0.12, 2.0));
                const double polarPenalty = std::pow(lat01, 1.45);
                const double continentalWave = 0.5 + 0.5 * std::sin((x01 * 6.283185307179586) + (lat01 * 4.5));
                const double humidity = clamp01d(0.18 + 0.86 * equatorialWet + 0.24 * continentalWave - 0.52 * subtropicalDry - 0.36 * polarPenalty);

                const double coastBoost = coastalAdj ? std::max(1.0, m_ctx->config.food.coastalBonus) : 1.0;
                const double thermal = std::max(0.10, 1.22 - 1.30 * std::pow(lat01, 1.35));
                const double foragingPot = std::max(2.0, m_ctx->config.food.baseForaging *
                                                         (0.22 + 1.45 * humidity) *
                                                         (0.30 + 0.90 * thermal) *
                                                         (coastalAdj ? 1.08 : 1.0));
                const double farmingPot = std::max(2.0, m_ctx->config.food.baseFarming *
                                                        (0.12 + 1.60 * humidity) *
                                                        std::max(0.10, 0.18 + 1.05 * thermal) *
                                                        coastBoost);
                foodAmount = foragingPot + 0.40 * farmingPot;
                
                sf::Vector2u pixelPos(static_cast<unsigned int>(x * m_gridCellSize), static_cast<unsigned int>(y * m_gridCellSize));
                const sf::Color resourcePixelColor = m_resourceImage.getPixel(pixelPos.x, pixelPos.y);
                const sf::Color coalPixelColor = m_coalImage.getPixel(pixelPos.x, pixelPos.y);
                const sf::Color copperPixelColor = m_copperImage.getPixel(pixelPos.x, pixelPos.y);
                const sf::Color tinPixelColor = m_tinImage.getPixel(pixelPos.x, pixelPos.y);
                const sf::Color riverlandPixelColor = m_riverlandImage.getPixel(pixelPos.x, pixelPos.y);

                const std::uint64_t coord = (static_cast<std::uint64_t>(x) << 32) ^ static_cast<std::uint64_t>(y);
                auto unitHash = [&](std::uint64_t salt) -> double {
                    return SimulationContext::u01FromU64(SimulationContext::mix64(m_ctx->worldSeed ^ coord ^ salt));
                };

                const bool hasRiverland = riverlandPixelColor.a > 0 &&
                                          isColorNear(riverlandPixelColor, sf::Color(24, 255, 239), 6);
                if (hasRiverland) {
                    riverlandCells++;
                    const double uFood = unitHash(0x5249564552464F4Full);
                    foodAmount = std::max(foodAmount, m_ctx->config.food.riverlandFoodFloor);
                    foodAmount *= (1.0 + 0.08 * uFood);

                    const double uClay = unitHash(0x434C415942415345ull);
                    const double uClayHot = unitHash(0x434C4159484F5421ull);
                    const double clayMin = std::max(0.01, m_ctx->config.food.clayMin);
                    const double clayMax = std::max(clayMin, m_ctx->config.food.clayMax);
                    double clayAmount = clayMin + (clayMax - clayMin) * uClay;
                    if (uClayHot < std::clamp(m_ctx->config.food.clayHotspotChance, 0.0, 1.0)) {
                        clayAmount *= 2.0;
                    }
                    m_resourceGrid[y][x][Resource::Type::CLAY] += clayAmount;
                    if (clayAmount > 0.0) {
                        clayCells++;
                    }
                }

                m_resourceGrid[y][x][Resource::Type::FOOD] = foodAmount;

                // Only process if the pixel is not fully transparent
                if (resourcePixelColor.a > 0) {
                    for (const auto& [color, type] : m_resourceColors) {
                        if (resourcePixelColor == color) {
                            const std::uint64_t saltA = 0xA8F1B4D5E6C70123ull ^ static_cast<std::uint64_t>(type);
                            const std::uint64_t saltB = 0x3D2C1B0A99887766ull ^ (static_cast<std::uint64_t>(type) << 32);
                            const double u1 = unitHash(saltA);
                            const double u2 = unitHash(saltB);
                            const double baseAmount = 0.2 + u1 * (2.0 - 0.2);
                            const double hotspot = 2.0 + u2 * (6.0 - 2.0);
                            m_resourceGrid[y][x][type] = baseAmount * hotspot;
                            break;
                        }
                    }
                }

                auto addLayerDeposit = [&](const sf::Color& layerColor,
                                           const sf::Color& expectedColor,
                                           int colorTolerance,
                                           Resource::Type type,
                                           double baseMin,
                                           double baseMax,
                                           double hotspotMin,
                                           double hotspotMax,
                                           std::uint64_t saltBase) -> bool {
                    if (layerColor.a == 0 || !isColorNear(layerColor, expectedColor, colorTolerance)) {
                        return false;
                    }
                    const double uBase = unitHash(saltBase ^ 0xA3D27E4B11C9ull);
                    const double uHot = unitHash(saltBase ^ 0x1F5C6A9872D3ull);
                    const double baseAmount = baseMin + uBase * (baseMax - baseMin);
                    const double hotspot = hotspotMin + uHot * (hotspotMax - hotspotMin);
                    m_resourceGrid[y][x][type] += baseAmount * hotspot;
                    return true;
                };

                if (addLayerDeposit(copperPixelColor, sf::Color(136, 78, 68), 4, Resource::Type::COPPER,
                                    0.2, 2.0, 2.0, 6.0, 0x4355505045524C59ull)) {
                    copperCells++;
                }
                if (addLayerDeposit(tinPixelColor, sf::Color(39, 135, 132), 4, Resource::Type::TIN,
                                    0.12, 1.2, 2.0, 7.0, 0x54494E4C41594552ull)) {
                    tinCells++;
                }
                if (addLayerDeposit(coalPixelColor, sf::Color(53, 0, 62), 4, Resource::Type::COAL,
                                    0.2, 2.2, 2.0, 7.0, 0x434F414C4C415952ull)) {
                    coalCells++;
                }
            }
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "✅ RESOURCES INITIALIZED in " << duration.count() << " ms" << std::endl;
    std::cout << "   Resource ingestion: "
              << "coal=" << coalCells
              << ", copper=" << copperCells
              << ", tin=" << tinCells
              << ", clay=" << clayCells
              << ", riverland-cells=" << riverlandCells
              << std::endl;
}

void Map::rebuildCellFoodCache() {
    const int height = static_cast<int>(m_resourceGrid.size());
    const int width = (height > 0) ? static_cast<int>(m_resourceGrid[0].size()) : 0;
    m_cellFood.assign(static_cast<size_t>(height * width), 0.0);
    m_cellForaging.assign(static_cast<size_t>(height * width), 0.0);
    m_cellFarming.assign(static_cast<size_t>(height * width), 0.0);

    auto clamp01d = [](double v) { return std::max(0.0, std::min(1.0, v)); };
    const int dx[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
    const int dy[8] = {-1, 0, 1, -1, 1, -1, 0, 1};

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (!m_isLandGrid[static_cast<size_t>(y)][static_cast<size_t>(x)]) {
                continue;
            }

            bool coastalAdj = false;
            for (int k = 0; k < 8; ++k) {
                const int nx = x + dx[k];
                const int ny = y + dy[k];
                if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;
                if (!m_isLandGrid[static_cast<size_t>(ny)][static_cast<size_t>(nx)]) {
                    coastalAdj = true;
                    break;
                }
            }

            const double lat01 = std::abs(((static_cast<double>(y) + 0.5) / static_cast<double>(std::max(1, height))) - 0.5) * 2.0;
            const double eco = std::max(0.08, 1.15 - 0.95 * std::pow(lat01, 1.30));
            double foragingPot = std::max(1.0, m_ctx->config.food.baseForaging *
                                                (0.30 + 0.95 * eco) *
                                                (coastalAdj ? 1.08 : 1.0));
            double farmingPot = std::max(1.0, m_ctx->config.food.baseFarming *
                                               std::max(0.08, 0.18 + 1.15 * eco) *
                                               (coastalAdj ? std::max(1.0, m_ctx->config.food.coastalBonus) : 1.0));

            const auto& cell = m_resourceGrid[static_cast<size_t>(y)][static_cast<size_t>(x)];
            auto it = cell.find(Resource::Type::FOOD);
            if (it != cell.end()) {
                const double baseFood = std::max(0.0, it->second);
                const double denom = std::max(1e-6, foragingPot + 0.40 * farmingPot);
                const double scale = std::max(0.2, baseFood / denom);
                foragingPot *= scale;
                farmingPot *= scale;
            }

            const sf::Vector2u pixelPos(static_cast<unsigned int>(x * m_gridCellSize), static_cast<unsigned int>(y * m_gridCellSize));
            const sf::Color riverlandPixelColor = m_riverlandImage.getPixel(pixelPos.x, pixelPos.y);
            const bool hasRiverland = riverlandPixelColor.a > 0 &&
                                      isColorNear(riverlandPixelColor, sf::Color(24, 255, 239), 6);
            if (hasRiverland) {
                farmingPot = std::max(farmingPot, m_ctx->config.food.riverlandFoodFloor);
                foragingPot *= 1.06;
            }

            const size_t idx = static_cast<size_t>(y * width + x);
            m_cellForaging[idx] = std::max(0.0, foragingPot);
            m_cellFarming[idx] = std::max(0.0, farmingPot);
            m_cellFood[idx] = std::max(0.0, foragingPot + 0.40 * farmingPot);
        }
    }
}

void Map::rebuildCellOreCache() {
    const int height = static_cast<int>(m_resourceGrid.size());
    const int width = (height > 0) ? static_cast<int>(m_resourceGrid[0].size()) : 0;
    m_cellOre.assign(static_cast<size_t>(height * width), 0.0);

    auto getAmount = [&](const std::unordered_map<Resource::Type, double>& cell, Resource::Type t) -> double {
        auto it = cell.find(t);
        return (it != cell.end()) ? it->second : 0.0;
    };

    const double wIron = std::max(0.0, m_ctx->config.resources.oreWeightIron);
    const double wCopper = std::max(0.0, m_ctx->config.resources.oreWeightCopper);
    const double wTin = std::max(0.0, m_ctx->config.resources.oreWeightTin);
    const double scale = 120.0 / std::max(1e-6, m_ctx->config.resources.oreNormalization);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (!m_isLandGrid[static_cast<size_t>(y)][static_cast<size_t>(x)]) {
                continue;
            }
            const auto& cell = m_resourceGrid[static_cast<size_t>(y)][static_cast<size_t>(x)];
            const double iron = getAmount(cell, Resource::Type::IRON);
            const double copper = getAmount(cell, Resource::Type::COPPER);
            const double tin = getAmount(cell, Resource::Type::TIN);
            const double raw = iron * wIron + copper * wCopper + tin * wTin;
            m_cellOre[static_cast<size_t>(y * width + x)] = std::max(0.0, raw * scale);
        }
    }
}

void Map::rebuildCellEnergyCache() {
    const int height = static_cast<int>(m_resourceGrid.size());
    const int width = (height > 0) ? static_cast<int>(m_resourceGrid[0].size()) : 0;
    m_cellEnergy.assign(static_cast<size_t>(height * width), 0.0);

    auto getAmount = [&](const std::unordered_map<Resource::Type, double>& cell, Resource::Type t) -> double {
        auto it = cell.find(t);
        return (it != cell.end()) ? it->second : 0.0;
    };

    const double biomass = std::max(0.0, m_ctx->config.resources.energyBiomassBase);
    const double coalW = std::max(0.0, m_ctx->config.resources.energyCoalWeight);
    const double scale = 100.0 / std::max(1e-6, m_ctx->config.resources.energyNormalization);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (!m_isLandGrid[static_cast<size_t>(y)][static_cast<size_t>(x)]) {
                continue;
            }
            const auto& cell = m_resourceGrid[static_cast<size_t>(y)][static_cast<size_t>(x)];
            const double coal = getAmount(cell, Resource::Type::COAL);
            const double raw = biomass + coal * coalW;
            m_cellEnergy[static_cast<size_t>(y * width + x)] = std::max(0.0, raw * scale);
        }
    }
}

void Map::rebuildCellConstructionCache() {
    const int height = static_cast<int>(m_resourceGrid.size());
    const int width = (height > 0) ? static_cast<int>(m_resourceGrid[0].size()) : 0;
    m_cellConstruction.assign(static_cast<size_t>(height * width), 0.0);
    m_cellNonFood.assign(static_cast<size_t>(height * width), 0.0);

    auto getAmount = [&](const std::unordered_map<Resource::Type, double>& cell, Resource::Type t) -> double {
        auto it = cell.find(t);
        return (it != cell.end()) ? it->second : 0.0;
    };

    const double clayW = std::max(0.0, m_ctx->config.resources.constructionClayWeight);
    const double stoneBase = std::max(0.0, m_ctx->config.resources.constructionStoneBase);
    const double cScale = 100.0 / std::max(1e-6, m_ctx->config.resources.constructionNormalization);

    for (int y = 0; y < height; ++y) {
        const double lat01 = std::abs(((static_cast<double>(y) + 0.5) / static_cast<double>(std::max(1, height))) - 0.5) * 2.0;
        for (int x = 0; x < width; ++x) {
            if (!m_isLandGrid[static_cast<size_t>(y)][static_cast<size_t>(x)]) {
                continue;
            }
            const size_t idx = static_cast<size_t>(y * width + x);
            const auto& cell = m_resourceGrid[static_cast<size_t>(y)][static_cast<size_t>(x)];
            const double clay = getAmount(cell, Resource::Type::CLAY);
            const double stoneProxy = stoneBase * (0.65 + 0.55 * std::abs(0.35 - lat01));
            const double construction = std::max(0.0, (clay * clayW + stoneProxy) * cScale);
            m_cellConstruction[idx] = construction;

            const double ore = (idx < m_cellOre.size()) ? m_cellOre[idx] : 0.0;
            const double energy = (idx < m_cellEnergy.size()) ? m_cellEnergy[idx] : 0.0;
            const double salt = getAmount(cell, Resource::Type::SALT);
            const double horses = getAmount(cell, Resource::Type::HORSES);
            const double gold = getAmount(cell, Resource::Type::GOLD);
            m_cellNonFood[idx] = std::max(0.0,
                                          0.55 * ore +
                                          0.30 * energy +
                                          0.25 * construction +
                                          4.0 * salt +
                                          2.5 * horses +
                                          1.0 * gold);
        }
    }
}

void Map::ensureFieldGrids() {
    const int height = static_cast<int>(m_countryGrid.size());
    const int width = (height > 0) ? static_cast<int>(m_countryGrid[0].size()) : 0;

    const int newW = (width + kFieldCellSize - 1) / kFieldCellSize;
    const int newH = (height + kFieldCellSize - 1) / kFieldCellSize;

    if (newW == m_fieldW && newH == m_fieldH && !m_fieldOwnerId.empty()) {
        return;
    }

    m_fieldW = newW;
    m_fieldH = newH;

    const size_t n = static_cast<size_t>(std::max(0, m_fieldW)) * static_cast<size_t>(std::max(0, m_fieldH));
    m_fieldOwnerId.assign(n, -1);
    m_fieldControl.assign(n, 0.0f);
    m_fieldMoveCost.assign(n, 1.0f);
    m_fieldCorridorWeight.assign(n, 1.0f);
    m_fieldFoodPotential.assign(n, 0.0f);
    m_countryControlCache.clear();
    m_controlCacheDirty = true;

    rebuildFieldFoodPotential();
    ensureClimateGrids();
    rebuildFieldLandMask();
}

void Map::rebuildFieldFoodPotential() {
    if (m_fieldW <= 0 || m_fieldH <= 0) {
        return;
    }
    const int height = static_cast<int>(m_countryGrid.size());
    const int width = (height > 0) ? static_cast<int>(m_countryGrid[0].size()) : 0;
    if (width <= 0 || height <= 0) {
        return;
    }
    if (m_cellFood.size() != static_cast<size_t>(width * height)) {
        return;
    }

    for (int fy = 0; fy < m_fieldH; ++fy) {
        const int y0 = fy * kFieldCellSize;
        const int y1 = std::min(height, (fy + 1) * kFieldCellSize);
        for (int fx = 0; fx < m_fieldW; ++fx) {
            const int x0 = fx * kFieldCellSize;
            const int x1 = std::min(width, (fx + 1) * kFieldCellSize);

            double sum = 0.0;
            for (int y = y0; y < y1; ++y) {
                const int rowBase = y * width;
                for (int x = x0; x < x1; ++x) {
                    sum += m_cellFood[static_cast<size_t>(rowBase + x)];
                }
            }

            const size_t idx = static_cast<size_t>(fy) * static_cast<size_t>(m_fieldW) + static_cast<size_t>(fx);
            if (idx < m_fieldFoodPotential.size()) {
                m_fieldFoodPotential[idx] = static_cast<float>(sum);
            }
        }
    }
}

void Map::ensureClimateGrids() {
    if (m_fieldW <= 0 || m_fieldH <= 0) {
        m_fieldLandMask.clear();
        m_fieldClimateZone.clear();
        m_fieldBiome.clear();
        m_fieldTempMean.clear();
        m_fieldPrecipMean.clear();
        m_fieldTempAnom.clear();
        m_fieldPrecipAnom.clear();
        m_fieldFoodYieldMult.clear();
        return;
    }

    const size_t n = static_cast<size_t>(m_fieldW) * static_cast<size_t>(m_fieldH);
    m_fieldLandMask.assign(n, 0u);
    m_fieldClimateZone.assign(n, 255u);
    m_fieldBiome.assign(n, 255u);
    m_fieldTempMean.assign(n, 0.0f);
    m_fieldPrecipMean.assign(n, 0.0f);
    m_fieldTempAnom.assign(n, 0.0f);
    m_fieldPrecipAnom.assign(n, 0.0f);
    m_fieldFoodYieldMult.assign(n, 1.0f);
}

void Map::rebuildFieldLandMask() {
    if (m_fieldW <= 0 || m_fieldH <= 0 || m_fieldFoodPotential.empty()) {
        return;
    }
    const size_t n = static_cast<size_t>(m_fieldW) * static_cast<size_t>(m_fieldH);
    if (m_fieldLandMask.size() != n) {
        m_fieldLandMask.assign(n, 0u);
    }
    for (size_t i = 0; i < n; ++i) {
        m_fieldLandMask[i] = (i < m_fieldFoodPotential.size() && m_fieldFoodPotential[i] > 0.0f) ? 1u : 0u;
    }
}

void Map::initializeClimateBaseline() {
    ensureFieldGrids();
    ensureClimateGrids();
    rebuildFieldLandMask();

    if (m_fieldW <= 0 || m_fieldH <= 0) {
        return;
    }

    auto clamp01f = [](float v) { return std::max(0.0f, std::min(1.0f, v)); };
    auto clamp01d = [](double v) { return std::max(0.0, std::min(1.0, v)); };

    const int W = m_fieldW;
    const int H = m_fieldH;
    const size_t n = static_cast<size_t>(W) * static_cast<size_t>(H);

    // Multi-source BFS distance-to-water (field resolution).
    std::vector<std::uint16_t> dist(n, std::numeric_limits<std::uint16_t>::max());
    std::queue<int> q;
    for (int fy = 0; fy < H; ++fy) {
        for (int fx = 0; fx < W; ++fx) {
            const size_t idx = static_cast<size_t>(fy) * static_cast<size_t>(W) + static_cast<size_t>(fx);
            if (idx >= m_fieldLandMask.size()) continue;
            if (m_fieldLandMask[idx] == 0u) {
                dist[idx] = 0;
                q.push(static_cast<int>(idx));
            }
        }
    }

    while (!q.empty()) {
        const int cur = q.front();
        q.pop();
        const int cx = cur % W;
        const int cy = cur / W;
        const std::uint16_t cd = dist[static_cast<size_t>(cur)];
        if (cd == std::numeric_limits<std::uint16_t>::max()) continue;

        const int nx[4] = {cx + 1, cx - 1, cx, cx};
        const int ny[4] = {cy, cy, cy + 1, cy - 1};
        for (int k = 0; k < 4; ++k) {
            const int x = nx[k];
            const int y = ny[k];
            if (x < 0 || y < 0 || x >= W || y >= H) continue;
            const int ni = y * W + x;
            const size_t nidx = static_cast<size_t>(ni);
            const std::uint16_t nd = static_cast<std::uint16_t>(std::min<int>(65535, static_cast<int>(cd) + 1));
            if (nd < dist[nidx]) {
                dist[nidx] = nd;
                q.push(ni);
            }
        }
    }

    // Rain-shadow advection factor (0..1-ish), single pass per latitude row.
    std::vector<float> shadow(n, 1.0f);
    for (int fy = 0; fy < H; ++fy) {
        const double lat01 = std::abs(((static_cast<double>(fy) + 0.5) / static_cast<double>(H)) - 0.5) * 2.0;
        const bool eastToWest = (lat01 < 0.25) || (lat01 >= 0.75);

        float moisture = 1.0f;
        if (eastToWest) {
            for (int fx = W - 1; fx >= 0; --fx) {
                const size_t idx = static_cast<size_t>(fy) * static_cast<size_t>(W) + static_cast<size_t>(fx);
                if (m_fieldLandMask[idx] == 0u) {
                    moisture = 1.0f;
                    shadow[idx] = 1.0f;
                    continue;
                }
                shadow[idx] = moisture;
                const float continental = (dist[idx] > 6) ? 0.92f : 0.95f;
                moisture = std::max(0.0f, moisture * continental);
            }
        } else {
            for (int fx = 0; fx < W; ++fx) {
                const size_t idx = static_cast<size_t>(fy) * static_cast<size_t>(W) + static_cast<size_t>(fx);
                if (m_fieldLandMask[idx] == 0u) {
                    moisture = 1.0f;
                    shadow[idx] = 1.0f;
                    continue;
                }
                shadow[idx] = moisture;
                const float continental = (dist[idx] > 6) ? 0.92f : 0.95f;
                moisture = std::max(0.0f, moisture * continental);
            }
        }
    }

    // Baseline climate per field cell.
    for (int fy = 0; fy < H; ++fy) {
        const double lat01 = std::abs(((static_cast<double>(fy) + 0.5) / static_cast<double>(H)) - 0.5) * 2.0;
        for (int fx = 0; fx < W; ++fx) {
            const size_t idx = static_cast<size_t>(fy) * static_cast<size_t>(W) + static_cast<size_t>(fx);
            if (idx >= n || idx >= m_fieldLandMask.size()) continue;

            if (m_fieldLandMask[idx] == 0u) {
                m_fieldClimateZone[idx] = 255u;
                m_fieldBiome[idx] = 255u;
                m_fieldTempMean[idx] = 0.0f;
                m_fieldPrecipMean[idx] = 0.0f;
                continue;
            }

            // Climate zone strips (for debug overlay).
            uint8_t zone = 4;
            if (lat01 < 0.15) zone = 0;
            else if (lat01 < 0.35) zone = 1;
            else if (lat01 < 0.60) zone = 2;
            else if (lat01 < 0.80) zone = 3;
            else zone = 4;
            m_fieldClimateZone[idx] = zone;

            // Temperature mean (C): latitude curve + coastal moderation.
            const double baseTempC = 30.0 - 55.0 * std::pow(lat01, 1.15);
            const float coast = std::exp(-static_cast<float>(dist[idx]) / 6.0f);
            const double moderated = baseTempC + static_cast<double>(coast) * 0.18 * (15.0 - baseTempC);
            m_fieldTempMean[idx] = static_cast<float>(moderated);

            // Precipitation mean (0..1): latitude bands + advection shadow + coastal boost.
            const double equ = std::exp(-std::pow(lat01 / 0.18, 2.0));
            const double subtDry = std::exp(-std::pow((lat01 - 0.28) / 0.10, 2.0));
            const double midWet = std::exp(-std::pow((lat01 - 0.52) / 0.20, 2.0));
            const double polarDry = std::exp(-std::pow((lat01 - 0.88) / 0.10, 2.0));
            double basePrec = 0.18 + 0.85 * equ + 0.35 * midWet - 0.55 * subtDry - 0.25 * polarDry;
            basePrec = clamp01d(basePrec);

            const float coastalBoost = 0.18f * std::exp(-static_cast<float>(dist[idx]) / 4.0f);
            const float sh = shadow[idx];
            const float prec = clamp01f(static_cast<float>(basePrec) * (0.55f + 0.45f * sh) + coastalBoost);
            m_fieldPrecipMean[idx] = prec;

            // Biome classification (0..N).
            constexpr uint8_t Ice = 0;
            constexpr uint8_t Tundra = 1;
            constexpr uint8_t Taiga = 2;
            constexpr uint8_t TemperateForest = 3;
            constexpr uint8_t Grassland = 4;
            constexpr uint8_t Desert = 5;
            constexpr uint8_t Savanna = 6;
            constexpr uint8_t TropicalForest = 7;
            constexpr uint8_t Mediterranean = 8;

            const float t = m_fieldTempMean[idx];
            const float p = m_fieldPrecipMean[idx];
            uint8_t biome = Grassland;
            if (t < -6.0f) biome = Ice;
            else if (t < 2.0f) biome = Tundra;
            else if (t < 8.0f) biome = (p > 0.35f) ? Taiga : Grassland;
            else if (t < 18.0f) {
                if (p < 0.16f) biome = Desert;
                else if (p < 0.32f) biome = Grassland;
                else biome = TemperateForest;
            } else if (t < 24.0f) {
                if (p < 0.16f) biome = Desert;
                else if (p < 0.40f) biome = (coastalBoost > 0.10f) ? Mediterranean : Savanna;
                else biome = TemperateForest;
            } else {
                if (p < 0.18f) biome = Desert;
                else if (p < 0.45f) biome = Savanna;
                else biome = TropicalForest;
            }
            m_fieldBiome[idx] = biome;
        }
    }
}

void Map::buildCoastalLandCandidates() {
    m_fieldCoastalLandCandidates.clear();
    if (m_fieldW <= 0 || m_fieldH <= 0 || m_fieldLandMask.empty()) {
        return;
    }

    const int W = m_fieldW;
    const int H = m_fieldH;
    const size_t n = static_cast<size_t>(W) * static_cast<size_t>(H);
    m_fieldCoastalLandCandidates.reserve(n / 6u);

    auto isLand = [&](int fx, int fy) -> bool {
        if (fx < 0 || fy < 0 || fx >= W || fy >= H) return false;
        const size_t idx = static_cast<size_t>(fy) * static_cast<size_t>(W) + static_cast<size_t>(fx);
        return idx < m_fieldLandMask.size() && m_fieldLandMask[idx] != 0u;
    };

    for (int fy = 0; fy < H; ++fy) {
        for (int fx = 0; fx < W; ++fx) {
            if (!isLand(fx, fy)) continue;
            // Coastal land at field resolution: adjacent to any water cell.
            const bool coastal = (!isLand(fx + 1, fy)) || (!isLand(fx - 1, fy)) || (!isLand(fx, fy + 1)) || (!isLand(fx, fy - 1));
            if (!coastal) continue;
            const int idx = fy * W + fx;
            m_fieldCoastalLandCandidates.push_back(idx);
        }
    }
}

void Map::tickWeather(int year, int dtYears) {
    (void)dtYears;
    if (m_fieldW <= 0 || m_fieldH <= 0 || m_fieldLandMask.empty()) {
        return;
    }

    const int W = m_fieldW;
    const int H = m_fieldH;
    const size_t n = static_cast<size_t>(W) * static_cast<size_t>(H);

    // Coarse weather grid (fieldW/8 by fieldH/8, clamped to >=1).
    const int cw = std::max(1, W / 8);
    const int ch = std::max(1, H / 8);
    if (cw != m_weatherW || ch != m_weatherH || m_weatherTemp.empty() || m_weatherPrecip.empty()) {
        m_weatherW = cw;
        m_weatherH = ch;
        m_weatherTemp.assign(static_cast<size_t>(cw) * static_cast<size_t>(ch), 0.0f);
        m_weatherPrecip.assign(static_cast<size_t>(cw) * static_cast<size_t>(ch), 0.0f);
        m_lastWeatherUpdateYear = year - 2;
    }

    auto noiseSigned = [&](int yy, int ix, int iy, std::uint64_t salt) -> float {
        const std::uint64_t cell = static_cast<std::uint64_t>(ix + iy * m_weatherW);
        const std::uint64_t k = m_ctx->worldSeed ^ (static_cast<std::uint64_t>(static_cast<std::int64_t>(yy)) * 0x9E3779B97F4A7C15ull) ^ (cell * 0xD1B54A32D192ED03ull) ^ salt;
        const double u = SimulationContext::u01FromU64(SimulationContext::mix64(k));
        return static_cast<float>(u * 2.0 - 1.0);
    };

    // Update anomalies every k years (smooth AR(1) process).
    constexpr int kUpdateStep = 2;
    int fromYear = m_lastWeatherUpdateYear;
    if (fromYear > year) {
        fromYear = year - kUpdateStep;
    }
    for (int yy = fromYear + kUpdateStep; yy <= year; yy += kUpdateStep) {
        for (int iy = 0; iy < m_weatherH; ++iy) {
            for (int ix = 0; ix < m_weatherW; ++ix) {
                const size_t wi = static_cast<size_t>(iy) * static_cast<size_t>(m_weatherW) + static_cast<size_t>(ix);
                const float nt = noiseSigned(yy, ix, iy, 0x54454D50ull);   // "TEMP"
                const float np = noiseSigned(yy, ix, iy, 0x50524543ull);   // "PREC"
                m_weatherTemp[wi] = 0.85f * m_weatherTemp[wi] + 0.15f * (nt * 5.0f);
                m_weatherPrecip[wi] = 0.85f * m_weatherPrecip[wi] + 0.15f * (np * 0.18f);
            }
        }
        m_lastWeatherUpdateYear = yy;
    }

    auto clamp01f = [](float v) { return std::max(0.0f, std::min(1.0f, v)); };

    auto biomeBaseYield = [&](uint8_t biome) -> float {
        switch (biome) {
            case 0: return 0.10f; // Ice
            case 1: return 0.35f; // Tundra
            case 2: return 0.55f; // Taiga
            case 3: return 1.00f; // Temperate forest
            case 4: return 0.90f; // Grassland
            case 5: return 0.35f; // Desert
            case 6: return 0.75f; // Savanna
            case 7: return 1.12f; // Tropical forest
            case 8: return 0.92f; // Mediterranean
            default: return 1.0f;
        }
    };

    auto tempResponse = [&](float tempC) -> float {
        // Smooth bell-shaped response with a broad optimum around ~22C.
        const float z = (tempC - 22.0f) / 18.0f;
        const float r = std::exp(-(z * z));
        return std::max(0.08f, std::min(1.10f, r * 1.10f));
    };

    auto precipResponse = [&](float prec01) -> float {
        const float p = clamp01f(prec01);
        const float t = clamp01f((p - 0.12f) / (0.70f - 0.12f));
        const float s = t * t * (3.0f - 2.0f * t); // smoothstep
        return 0.15f + 0.85f * s;
    };

    // Upsample anomalies to field grid (nearest) and compute final food yield multipliers.
    for (int fy = 0; fy < H; ++fy) {
        const int cy = (fy * m_weatherH) / H;
        for (int fx = 0; fx < W; ++fx) {
            const int cx = (fx * m_weatherW) / W;
            const size_t idx = static_cast<size_t>(fy) * static_cast<size_t>(W) + static_cast<size_t>(fx);
            if (idx >= n) continue;
            if (m_fieldLandMask[idx] == 0u) {
                m_fieldTempAnom[idx] = 0.0f;
                m_fieldPrecipAnom[idx] = 0.0f;
                m_fieldFoodYieldMult[idx] = 0.0f;
                continue;
            }

            const size_t wi = static_cast<size_t>(cy) * static_cast<size_t>(m_weatherW) + static_cast<size_t>(cx);
            const float tA = (wi < m_weatherTemp.size()) ? m_weatherTemp[wi] : 0.0f;
            const float pA = (wi < m_weatherPrecip.size()) ? m_weatherPrecip[wi] : 0.0f;

            m_fieldTempAnom[idx] = tA;
            m_fieldPrecipAnom[idx] = pA;

            const float temp = m_fieldTempMean[idx] + tA;
            const float prec = m_fieldPrecipMean[idx] + pA;
            const float b = biomeBaseYield(m_fieldBiome[idx]);
            const float y = b * tempResponse(temp) * precipResponse(prec);
            m_fieldFoodYieldMult[idx] = std::max(0.05f, std::min(1.80f, y));
        }
    }

    // Invalidate per-country caches (recomputed on demand).
    m_countryClimateCacheN = 0;
}

void Map::prepareCountryClimateCaches(int countryCount) const {
    if (countryCount <= 0 || m_fieldOwnerId.empty() || m_fieldFoodYieldMult.empty() || m_fieldLandMask.empty()) {
        m_countryClimateFoodMult.clear();
        m_countryPrecipAnomMean.clear();
        m_countryClimateCacheN = 0;
        return;
    }

    m_countryClimateCacheN = countryCount;
    if (static_cast<int>(m_countryClimateFoodMult.size()) != countryCount) {
        m_countryClimateFoodMult.assign(static_cast<size_t>(countryCount), 1.0f);
    } else {
        std::fill(m_countryClimateFoodMult.begin(), m_countryClimateFoodMult.end(), 1.0f);
    }
    if (static_cast<int>(m_countryPrecipAnomMean.size()) != countryCount) {
        m_countryPrecipAnomMean.assign(static_cast<size_t>(countryCount), 0.0f);
    } else {
        std::fill(m_countryPrecipAnomMean.begin(), m_countryPrecipAnomMean.end(), 0.0f);
    }

    std::vector<double> sum(static_cast<size_t>(countryCount), 0.0);
    std::vector<double> wsum(static_cast<size_t>(countryCount), 0.0);
    std::vector<double> psum(static_cast<size_t>(countryCount), 0.0);
    std::vector<double> pwsum(static_cast<size_t>(countryCount), 0.0);

    const size_t n = std::min(m_fieldOwnerId.size(), m_fieldFoodYieldMult.size());
    for (size_t i = 0; i < n; ++i) {
        if (i >= m_fieldLandMask.size() || m_fieldLandMask[i] == 0u) continue;
        const int owner = m_fieldOwnerId[i];
        if (owner < 0 || owner >= countryCount) continue;
        const float w = (i < m_fieldFoodPotential.size()) ? std::max(0.0f, m_fieldFoodPotential[i]) : 1.0f;
        const double wd = std::max(1e-6, static_cast<double>(w));
        sum[static_cast<size_t>(owner)] += wd * static_cast<double>(m_fieldFoodYieldMult[i]);
        wsum[static_cast<size_t>(owner)] += wd;
        if (i < m_fieldPrecipAnom.size()) {
            psum[static_cast<size_t>(owner)] += wd * static_cast<double>(m_fieldPrecipAnom[i]);
            pwsum[static_cast<size_t>(owner)] += wd;
        }
    }

    for (int c = 0; c < countryCount; ++c) {
        const double w = wsum[static_cast<size_t>(c)];
        if (w > 1e-9) {
            m_countryClimateFoodMult[static_cast<size_t>(c)] = static_cast<float>(sum[static_cast<size_t>(c)] / w);
        } else {
            m_countryClimateFoodMult[static_cast<size_t>(c)] = 1.0f;
        }
        const double pw = pwsum[static_cast<size_t>(c)];
        if (pw > 1e-9) {
            m_countryPrecipAnomMean[static_cast<size_t>(c)] = static_cast<float>(psum[static_cast<size_t>(c)] / pw);
        } else {
            m_countryPrecipAnomMean[static_cast<size_t>(c)] = 0.0f;
        }
    }
}

float Map::getCountryClimateFoodMultiplier(int countryIndex) const {
    if (countryIndex < 0) {
        return 1.0f;
    }
    if (m_countryClimateCacheN <= 0 || countryIndex >= m_countryClimateCacheN) {
        prepareCountryClimateCaches(std::max(countryIndex + 1, m_countryClimateCacheN));
    }
    if (countryIndex < 0 || countryIndex >= static_cast<int>(m_countryClimateFoodMult.size())) {
        return 1.0f;
    }
    return m_countryClimateFoodMult[static_cast<size_t>(countryIndex)];
}

void Map::rebuildFieldOwnerIdAssumingLocked(int countryCount) {
    if (m_fieldW <= 0 || m_fieldH <= 0) {
        return;
    }
    const int height = static_cast<int>(m_countryGrid.size());
    const int width = (height > 0) ? static_cast<int>(m_countryGrid[0].size()) : 0;
    if (width <= 0 || height <= 0) {
        return;
    }
    if (countryCount <= 0) {
        std::fill(m_fieldOwnerId.begin(), m_fieldOwnerId.end(), -1);
        return;
    }

    std::vector<int> counts(static_cast<size_t>(countryCount), 0);
    std::vector<int> touched;
    touched.reserve(static_cast<size_t>(kFieldCellSize) * static_cast<size_t>(kFieldCellSize));

    for (int fy = 0; fy < m_fieldH; ++fy) {
        const int y0 = fy * kFieldCellSize;
        const int y1 = std::min(height, (fy + 1) * kFieldCellSize);
        for (int fx = 0; fx < m_fieldW; ++fx) {
            touched.clear();
            const int x0 = fx * kFieldCellSize;
            const int x1 = std::min(width, (fx + 1) * kFieldCellSize);

            for (int y = y0; y < y1; ++y) {
                const auto& row = m_countryGrid[static_cast<size_t>(y)];
                for (int x = x0; x < x1; ++x) {
                    const int c = row[static_cast<size_t>(x)];
                    if (c < 0 || c >= countryCount) {
                        continue;
                    }
                    if (counts[static_cast<size_t>(c)] == 0) {
                        touched.push_back(c);
                    }
                    counts[static_cast<size_t>(c)]++;
                }
            }

            int best = -1;
            int bestCount = 0;
            for (int c : touched) {
                const int v = counts[static_cast<size_t>(c)];
                if (v > bestCount) {
                    bestCount = v;
                    best = c;
                }
                counts[static_cast<size_t>(c)] = 0;
            }

            const size_t idx = static_cast<size_t>(fy) * static_cast<size_t>(m_fieldW) + static_cast<size_t>(fx);
            if (idx < m_fieldOwnerId.size()) {
                m_fieldOwnerId[idx] = best;
            }
        }
    }
}

void Map::rebuildFieldMoveCost(const std::vector<Country>& countries) {
    const size_t n = static_cast<size_t>(m_fieldW) * static_cast<size_t>(m_fieldH);
    if (n == 0) return;
    if (m_fieldMoveCost.size() != n) m_fieldMoveCost.assign(n, 1.0f);
    if (m_fieldCorridorWeight.size() != n) m_fieldCorridorWeight.assign(n, 1.0f);

    std::vector<float> roadFactor(n, 1.0f);
    std::vector<float> portFactor(n, 1.0f);
    std::vector<uint8_t> coastalMask(n, 0u);
    for (int fi : m_fieldCoastalLandCandidates) {
        if (fi >= 0 && static_cast<size_t>(fi) < n) {
            coastalMask[static_cast<size_t>(fi)] = 1u;
        }
    }

    for (const Country& c : countries) {
        if (c.getPopulation() <= 0) continue;
        for (const auto& p : c.getRoads()) {
            const int fx = std::max(0, std::min(m_fieldW - 1, p.x / kFieldCellSize));
            const int fy = std::max(0, std::min(m_fieldH - 1, p.y / kFieldCellSize));
            const size_t idx = static_cast<size_t>(fy) * static_cast<size_t>(m_fieldW) + static_cast<size_t>(fx);
            if (idx < roadFactor.size()) {
                roadFactor[idx] = std::min(roadFactor[idx], 0.62f);
            }
        }
        for (const auto& p : c.getPorts()) {
            const int fx = std::max(0, std::min(m_fieldW - 1, p.x / kFieldCellSize));
            const int fy = std::max(0, std::min(m_fieldH - 1, p.y / kFieldCellSize));
            const size_t idx = static_cast<size_t>(fy) * static_cast<size_t>(m_fieldW) + static_cast<size_t>(fx);
            if (idx < portFactor.size()) {
                portFactor[idx] = std::min(portFactor[idx], 0.70f);
            }
        }
    }

    for (size_t idx = 0; idx < n; ++idx) {
        if (idx >= m_fieldLandMask.size() || m_fieldLandMask[idx] == 0u) {
            m_fieldMoveCost[idx] = std::numeric_limits<float>::infinity();
            m_fieldCorridorWeight[idx] = 0.0f;
            continue;
        }
        const uint8_t biome = (idx < m_fieldBiome.size()) ? m_fieldBiome[idx] : 4u;
        float base = 1.0f;
        switch (biome) {
            case 0u: base = 3.20f; break; // Ice
            case 1u: base = 1.80f; break; // Tundra
            case 2u: base = 1.45f; break; // Taiga
            case 3u: base = 1.35f; break; // Temperate forest
            case 4u: base = 1.00f; break; // Grassland
            case 5u: base = 1.90f; break; // Desert
            case 6u: base = 1.15f; break; // Savanna
            case 7u: base = 1.65f; break; // Tropical forest
            case 8u: base = 1.05f; break; // Mediterranean
            default: base = 1.20f; break;
        }
        if (idx < m_fieldFoodPotential.size()) {
            const float avgFood = m_fieldFoodPotential[idx] / static_cast<float>(kFieldCellSize * kFieldCellSize);
            if (avgFood >= 140.0f) {
                // Riverland/floodplain-like cells are generally easier corridors for movement and settlement.
                base *= 0.92f;
            }
        }
        if (coastalMask[idx] != 0u) {
            base *= 0.92f;
        }
        base *= roadFactor[idx];
        base *= portFactor[idx];
        m_fieldMoveCost[idx] = std::max(0.12f, base);

        float corridor = 1.0f / std::max(0.12f, m_fieldMoveCost[idx]);
        if (coastalMask[idx] != 0u) {
            corridor += static_cast<float>(std::max(0.0, m_ctx->config.migration.corridorCoastBonus));
        }
        if (idx < m_fieldFoodPotential.size()) {
            const float avgFood = m_fieldFoodPotential[idx] / static_cast<float>(kFieldCellSize * kFieldCellSize);
            if (avgFood >= static_cast<float>(std::max(20.0, m_ctx->config.food.riverlandFoodFloor * 0.75))) {
                corridor += static_cast<float>(std::max(0.0, m_ctx->config.migration.corridorRiverlandBonus));
            }
        }
        if (biome == 4u || biome == 6u) {
            corridor += static_cast<float>(std::max(0.0, m_ctx->config.migration.corridorSteppeBonus));
        }
        if (biome == 5u) {
            corridor *= static_cast<float>(std::max(0.05, 1.0 - m_ctx->config.migration.corridorDesertPenalty));
        }
        if (biome == 0u || biome == 1u || biome == 2u) {
            corridor *= static_cast<float>(std::max(0.05, 1.0 - m_ctx->config.migration.corridorMountainPenalty));
        }
        m_fieldCorridorWeight[idx] = std::max(0.01f, corridor);
    }
}

void Map::updateControlGrid(std::vector<Country>& countries, int currentYear, int dtYears) {
    ensureFieldGrids();
    if (m_fieldW <= 0 || m_fieldH <= 0) return;

    const int countryCount = static_cast<int>(countries.size());
    {
        std::lock_guard<std::mutex> lock(m_gridMutex);
        rebuildFieldOwnerIdAssumingLocked(countryCount);
    }

    const size_t nField = static_cast<size_t>(m_fieldW) * static_cast<size_t>(m_fieldH);
    if (m_fieldControl.size() != nField) m_fieldControl.assign(nField, 0.0f);
    std::fill(m_fieldControl.begin(), m_fieldControl.end(), 0.0f);

    if (countryCount <= 0 || nField == 0) return;

    rebuildFieldMoveCost(countries);

    if (m_countryControlCache.size() < static_cast<size_t>(countryCount)) {
        m_countryControlCache.resize(static_cast<size_t>(countryCount));
    }

    std::vector<std::vector<int>> ownedByCountry(static_cast<size_t>(countryCount));
    for (size_t fi = 0; fi < nField; ++fi) {
        if (fi >= m_fieldOwnerId.size()) continue;
        const int owner = m_fieldOwnerId[fi];
        if (owner < 0 || owner >= countryCount) continue;
        ownedByCountry[static_cast<size_t>(owner)].push_back(static_cast<int>(fi));
    }

    auto clamp01 = [](double v) { return std::max(0.0, std::min(1.0, v)); };
    auto sigmoid = [](double x) {
        if (x > 20.0) return 1.0;
        if (x < -20.0) return 0.0;
        return 1.0 / (1.0 + std::exp(-x));
    };

    std::vector<int> fieldToLocal(nField, -1);

    for (int i = 0; i < countryCount; ++i) {
        Country& c = countries[static_cast<size_t>(i)];
        CountryControlCache& cache = m_countryControlCache[static_cast<size_t>(i)];
        const auto& owned = ownedByCountry[static_cast<size_t>(i)];
        const size_t roadCount = c.getRoads().size();
        const size_t portCount = c.getPorts().size();
        if (owned.empty() || c.getPopulation() <= 0) {
            c.setAvgControl(0.0);
            cache.fieldIndices.clear();
            cache.travelTimes.clear();
            cache.lastComputedYear = currentYear;
            cache.roadCount = roadCount;
            cache.portCount = portCount;
            continue;
        }

        const int cadence = 5 + (i % 6); // 5..10 years
        const bool transportChange = (cache.roadCount != roadCount) || (cache.portCount != portCount);
        const bool forceRecompute = m_controlCacheDirty || dtYears > 1 || cache.fieldIndices.empty() || transportChange;
        const bool cadenceRecompute = (currentYear - cache.lastComputedYear) >= cadence;
        const bool recompute = forceRecompute || cadenceRecompute;

        if (recompute) {
            cache.fieldIndices = owned;
            cache.travelTimes.assign(owned.size(), std::numeric_limits<float>::infinity());
            for (size_t li = 0; li < owned.size(); ++li) {
                const int fi = owned[li];
                if (fi >= 0 && static_cast<size_t>(fi) < nField) {
                    fieldToLocal[static_cast<size_t>(fi)] = static_cast<int>(li);
                }
            }

            // Multi-source weighted Dijkstra from capital and top cities.
            std::vector<int> sourceField;
            sourceField.reserve(8);
            const sf::Vector2i capPx = c.getCapitalLocation();
            const int capFx = std::max(0, std::min(m_fieldW - 1, capPx.x / kFieldCellSize));
            const int capFy = std::max(0, std::min(m_fieldH - 1, capPx.y / kFieldCellSize));
            const int capIdx = capFy * m_fieldW + capFx;
            if (capIdx >= 0 && static_cast<size_t>(capIdx) < nField && m_fieldOwnerId[static_cast<size_t>(capIdx)] == i) {
                sourceField.push_back(capIdx);
            }

            struct CitySeed {
                float pop = 0.0f;
                int idx = -1;
                int y = 0;
                int x = 0;
            };
            std::vector<CitySeed> seeds;
            seeds.reserve(c.getCities().size());
            for (const auto& city : c.getCities()) {
                const int fx = std::max(0, std::min(m_fieldW - 1, city.getLocation().x / kFieldCellSize));
                const int fy = std::max(0, std::min(m_fieldH - 1, city.getLocation().y / kFieldCellSize));
                const int idx = fy * m_fieldW + fx;
                if (idx < 0 || static_cast<size_t>(idx) >= nField) continue;
                if (m_fieldOwnerId[static_cast<size_t>(idx)] != i) continue;
                seeds.push_back(CitySeed{city.getPopulation(), idx, fy, fx});
            }
            std::sort(seeds.begin(), seeds.end(), [](const CitySeed& a, const CitySeed& b) {
                if (a.pop != b.pop) return a.pop > b.pop;
                if (a.y != b.y) return a.y < b.y;
                return a.x < b.x;
            });
            const int maxCitySources = 7;
            for (int s = 0; s < static_cast<int>(seeds.size()) && s < maxCitySources; ++s) {
                sourceField.push_back(seeds[static_cast<size_t>(s)].idx);
            }
            std::sort(sourceField.begin(), sourceField.end());
            sourceField.erase(std::unique(sourceField.begin(), sourceField.end()), sourceField.end());
            if (sourceField.empty()) {
                sourceField.push_back(owned.front());
            }

            struct Node {
                float dist = 0.0f;
                int field = -1;
                int local = -1;
            };
            struct NodeCmp {
                bool operator()(const Node& a, const Node& b) const {
                    if (a.dist != b.dist) return a.dist > b.dist;
                    return a.field > b.field;
                }
            };
            std::priority_queue<Node, std::vector<Node>, NodeCmp> pq;
            for (int src : sourceField) {
                const int li = (src >= 0 && static_cast<size_t>(src) < nField) ? fieldToLocal[static_cast<size_t>(src)] : -1;
                if (li < 0) continue;
                if (cache.travelTimes[static_cast<size_t>(li)] > 0.0f) {
                    cache.travelTimes[static_cast<size_t>(li)] = 0.0f;
                    pq.push(Node{0.0f, src, li});
                }
            }

            while (!pq.empty()) {
                const Node cur = pq.top();
                pq.pop();
                if (cur.local < 0 || static_cast<size_t>(cur.local) >= cache.travelTimes.size()) continue;
                if (cur.dist > cache.travelTimes[static_cast<size_t>(cur.local)] + 1e-6f) continue;

                const int fx = cur.field % m_fieldW;
                const int fy = cur.field / m_fieldW;
                const int nxs[4] = {fx + 1, fx - 1, fx, fx};
                const int nys[4] = {fy, fy, fy + 1, fy - 1};
                for (int k = 0; k < 4; ++k) {
                    const int x = nxs[k];
                    const int y = nys[k];
                    if (x < 0 || y < 0 || x >= m_fieldW || y >= m_fieldH) continue;
                    const int nf = y * m_fieldW + x;
                    if (nf < 0 || static_cast<size_t>(nf) >= nField) continue;
                    if (m_fieldOwnerId[static_cast<size_t>(nf)] != i) continue;
                    const int nli = fieldToLocal[static_cast<size_t>(nf)];
                    if (nli < 0) continue;

                    const float c0 = (static_cast<size_t>(cur.field) < m_fieldMoveCost.size()) ? m_fieldMoveCost[static_cast<size_t>(cur.field)] : 1.0f;
                    const float c1 = (static_cast<size_t>(nf) < m_fieldMoveCost.size()) ? m_fieldMoveCost[static_cast<size_t>(nf)] : 1.0f;
                    const float stepCost = std::max(0.08f, 0.5f * (c0 + c1));
                    const float nd = cur.dist + stepCost;
                    if (nd + 1e-6f < cache.travelTimes[static_cast<size_t>(nli)]) {
                        cache.travelTimes[static_cast<size_t>(nli)] = nd;
                        pq.push(Node{nd, nf, nli});
                    }
                }
            }

            for (int fi : owned) {
                if (fi >= 0 && static_cast<size_t>(fi) < nField) {
                    fieldToLocal[static_cast<size_t>(fi)] = -1;
                }
            }
            cache.lastComputedYear = currentYear;
            cache.roadCount = roadCount;
            cache.portCount = portCount;
        }

        const double commsMul =
            1.0 +
            0.45 * clamp01(c.getMacroEconomy().knowledgeStock) +
            0.30 * clamp01(c.getConnectivityIndex());

        const double reachCapacity =
            2.0 +
            42.0 *
            (0.30 * clamp01(c.getAdminSpendingShare()) +
             0.24 * clamp01(c.getInfraSpendingShare()) +
             0.18 * clamp01(c.getLogisticsReach()) +
             0.18 * clamp01(c.getInstitutionCapacity()) +
             0.10 * clamp01(c.getAvgControl())) *
            commsMul *
            (0.60 + 0.40 * clamp01(c.getLegitimacy()));
        const double softness = std::max(1.25, 5.5 - 3.0 * clamp01(c.getInstitutionCapacity()));

        double sumControl = 0.0;
        int countControl = 0;
        for (size_t k = 0; k < cache.fieldIndices.size() && k < cache.travelTimes.size(); ++k) {
            const int fi = cache.fieldIndices[k];
            if (fi < 0 || static_cast<size_t>(fi) >= m_fieldControl.size()) continue;
            const float tt = cache.travelTimes[k];
            if (!std::isfinite(tt)) continue;
            const double ctl = sigmoid((reachCapacity - static_cast<double>(tt)) / softness);
            m_fieldControl[static_cast<size_t>(fi)] = static_cast<float>(ctl);
            sumControl += ctl;
            countControl++;
        }
        c.setAvgControl((countControl > 0) ? (sumControl / static_cast<double>(countControl)) : 0.0);
    }

    m_controlCacheDirty = false;
}

void Map::initializePopulationGridFromCountries(const std::vector<Country>& countries) {
    ensureFieldGrids();
    if (m_fieldW <= 0 || m_fieldH <= 0) {
        return;
    }

    const size_t n = static_cast<size_t>(m_fieldW) * static_cast<size_t>(m_fieldH);
    m_fieldPopulation.assign(n, 0.0f);
    m_fieldAttractiveness.assign(n, 0.0f);
    m_fieldPopDelta.assign(n, 0.0f);
    m_lastPopulationUpdateYear = -9999999;

    std::uniform_int_distribution<int> radiusDist(2, 6); // field-cell radius

    for (const auto& c : countries) {
        const long long popLL = std::max<long long>(0, c.getPopulation());
        if (popLL <= 0) continue;

        const int owner = c.getCountryIndex();
        const sf::Vector2i start = c.getStartingPixel();
        const int fx0 = std::max(0, std::min(m_fieldW - 1, start.x / kFieldCellSize));
        const int fy0 = std::max(0, std::min(m_fieldH - 1, start.y / kFieldCellSize));

        const int r = radiusDist(m_ctx->worldRng);

        struct CellW {
            size_t idx = 0;
            double w = 0.0;
        };
        std::vector<CellW> cells;
        cells.reserve(static_cast<size_t>((2 * r + 1) * (2 * r + 1)));

        const auto inBounds = [&](int fx, int fy) {
            return fx >= 0 && fy >= 0 && fx < m_fieldW && fy < m_fieldH;
        };

        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                if (dx * dx + dy * dy > r * r) continue;
                const int fx = fx0 + dx;
                const int fy = fy0 + dy;
                if (!inBounds(fx, fy)) continue;
                const size_t idx = static_cast<size_t>(fy) * static_cast<size_t>(m_fieldW) + static_cast<size_t>(fx);
                if (idx >= m_fieldPopulation.size() || idx >= m_fieldFoodPotential.size()) continue;
                if (idx >= m_fieldOwnerId.size()) continue;
                if (m_fieldOwnerId[idx] != owner) continue;
                if (m_fieldFoodPotential[idx] <= 0.0f) continue;

                const float foodPot = m_fieldFoodPotential[idx];
                const float yieldMult = (idx < m_fieldFoodYieldMult.size()) ? m_fieldFoodYieldMult[idx] : 1.0f;
                const double w = std::max(0.0, static_cast<double>(foodPot) * static_cast<double>(yieldMult));
                if (w <= 0.0) continue;
                cells.push_back(CellW{ idx, w });
            }
        }

        // Fallback: ensure at least the start cell receives population.
        if (cells.empty()) {
            const size_t idx0 = static_cast<size_t>(fy0) * static_cast<size_t>(m_fieldW) + static_cast<size_t>(fx0);
            if (idx0 < m_fieldPopulation.size()) {
                m_fieldPopulation[idx0] += static_cast<float>(popLL);
            }
            continue;
        }

        double sumW = 0.0;
        for (const auto& cw : cells) sumW += cw.w;
        if (sumW <= 1e-9) {
            const size_t idx0 = static_cast<size_t>(fy0) * static_cast<size_t>(m_fieldW) + static_cast<size_t>(fx0);
            if (idx0 < m_fieldPopulation.size()) {
                m_fieldPopulation[idx0] += static_cast<float>(popLL);
            }
            continue;
        }

        // Allocate integer population across the cluster proportional to weights.
        long long remaining = popLL;
        std::vector<long long> alloc(cells.size(), 0);
        for (size_t k = 0; k < cells.size(); ++k) {
            const double share = static_cast<double>(popLL) * (cells[k].w / sumW);
            const long long a = std::max<long long>(0, static_cast<long long>(std::floor(share)));
            alloc[k] = a;
            remaining -= a;
        }
        if (remaining > 0) {
            // Distribute remainder via weighted sampling (cheap: repeat draws; remaining is small after floors).
            std::vector<double> ws;
            ws.reserve(cells.size());
            for (const auto& cw : cells) ws.push_back(std::max(0.0, cw.w));
            std::discrete_distribution<int> pick(ws.begin(), ws.end());
            while (remaining-- > 0) {
                const int k = pick(m_ctx->worldRng);
                if (k >= 0 && static_cast<size_t>(k) < alloc.size()) {
                    alloc[static_cast<size_t>(k)] += 1;
                }
            }
        } else if (remaining < 0) {
            // Numeric guard: remove extras from the largest allocations.
            long long toRemove = -remaining;
            while (toRemove-- > 0) {
                size_t bestK = 0;
                for (size_t k = 1; k < alloc.size(); ++k) {
                    if (alloc[k] > alloc[bestK]) bestK = k;
                }
                if (alloc[bestK] > 0) alloc[bestK] -= 1;
            }
        }

        for (size_t k = 0; k < cells.size(); ++k) {
            const size_t idx = cells[k].idx;
            if (idx < m_fieldPopulation.size()) {
                m_fieldPopulation[idx] += static_cast<float>(std::max<long long>(0, alloc[k]));
            }
        }
    }
}

void Map::applyPopulationTotalsToCountries(std::vector<Country>& countries) const {
    if (m_fieldPopulation.empty() || m_fieldOwnerId.empty()) {
        return;
    }
    const int countryCount = static_cast<int>(countries.size());
    std::vector<double> sum(static_cast<size_t>(countryCount), 0.0);

    const size_t n = std::min(m_fieldPopulation.size(), m_fieldOwnerId.size());
    for (size_t i = 0; i < n; ++i) {
        const int owner = m_fieldOwnerId[i];
        if (owner < 0 || owner >= countryCount) continue;
        sum[static_cast<size_t>(owner)] += static_cast<double>(std::max(0.0f, m_fieldPopulation[i]));
    }

    for (int i = 0; i < countryCount; ++i) {
        const long long pop = static_cast<long long>(std::llround(std::max(0.0, sum[static_cast<size_t>(i)])));
        countries[static_cast<size_t>(i)].setPopulation(pop);
    }
}

void Map::tickPopulationGrid(const std::vector<Country>& countries,
                             int currentYear,
                             int dtYears,
                             const std::vector<float>* tradeIntensityMatrix) {
    if (m_fieldPopulation.empty() || m_fieldFoodPotential.empty()) return;
    if (currentYear <= m_lastPopulationUpdateYear) return;
    m_lastPopulationUpdateYear = currentYear;

    const int years = std::max(1, dtYears);
    const double yearsD = static_cast<double>(years);
    const int countryCount = static_cast<int>(countries.size());
    const size_t n = m_fieldPopulation.size();
    const size_t ownerN = m_fieldOwnerId.size();

    auto clamp01 = [](double v) { return std::max(0.0, std::min(1.0, v)); };
    auto traitDistance = [](const Country& a, const Country& b) -> double {
        double sumSq = 0.0;
        for (int k = 0; k < Country::kTraits; ++k) {
            const double da = a.getTraits()[static_cast<size_t>(k)];
            const double db = b.getTraits()[static_cast<size_t>(k)];
            const double d = da - db;
            sumSq += d * d;
        }
        return std::sqrt(sumSq / static_cast<double>(Country::kTraits));
    };

    std::vector<double> refugeePush(static_cast<size_t>(countryCount), 0.0);
    for (int i = 0; i < countryCount; ++i) {
        double p = 0.0;
        if (static_cast<size_t>(i) < m_countryRefugeePush.size()) {
            p = m_countryRefugeePush[static_cast<size_t>(i)];
        } else {
            p = countries[static_cast<size_t>(i)].getMacroEconomy().refugeePush;
        }
        refugeePush[static_cast<size_t>(i)] = clamp01(p);
    }

    if (m_fieldAttractiveness.size() != n) m_fieldAttractiveness.assign(n, 0.0f);
    if (m_fieldPopDelta.size() != n) m_fieldPopDelta.assign(n, 0.0f);

    auto KFor = [&](size_t i) -> double {
        const double food = (i < m_fieldFoodPotential.size()) ? static_cast<double>(std::max(0.0f, m_fieldFoodPotential[i])) : 0.0;
        return std::max(1.0, food * 1200.0);
    };

    const int microIters = (years <= 1) ? 3 : std::max(1, years / 2);
    const float migRate = static_cast<float>(std::min(0.08, 0.010 * yearsD));

    for (int it = 0; it < microIters; ++it) {
        for (size_t i = 0; i < n; ++i) {
            const float food = (i < m_fieldFoodPotential.size()) ? m_fieldFoodPotential[i] : 0.0f;
            if (food <= 0.0f) {
                m_fieldAttractiveness[i] = -1e6f;
                continue;
            }
            const double K = KFor(i);
            const double pop = static_cast<double>(std::max(0.0f, m_fieldPopulation[i]));
            const double crowd = (K > 0.0) ? (pop / K) : 2.0;

            float a = static_cast<float>(std::log(1.0 + static_cast<double>(food)));
            a -= static_cast<float>(1.20 * crowd);

            if (i < ownerN) {
                const int owner = m_fieldOwnerId[i];
                if (owner >= 0 && owner < countryCount) {
                    const Country& c = countries[static_cast<size_t>(owner)];
                    const auto& m = c.getMacroEconomy();
                    const double push = refugeePush[static_cast<size_t>(owner)];
                    a += static_cast<float>(0.80 * clamp01(m.migrationAttractiveness));
                    a -= static_cast<float>(0.70 * clamp01(m.migrationPressureOut));
                    a -= static_cast<float>(0.55 * push);
                    a += static_cast<float>(0.35 * clamp01(m.realWage / 2.0));
                    a += static_cast<float>(0.22 * clamp01(c.getAvgControl()));
                    a += static_cast<float>(0.18 * clamp01(c.getLegitimacy()));
                    a -= static_cast<float>(0.50 * clamp01(m.diseaseBurden));
                    if (c.isAtWar()) a -= 0.35f;
                }
            }

            m_fieldAttractiveness[i] = a;
        }

        std::fill(m_fieldPopDelta.begin(), m_fieldPopDelta.end(), 0.0f);

        for (int y = 0; y < m_fieldH; ++y) {
            for (int x = 0; x < m_fieldW; ++x) {
                const size_t i = static_cast<size_t>(y) * static_cast<size_t>(m_fieldW) + static_cast<size_t>(x);
                if (i >= n) continue;
                const float pop = m_fieldPopulation[i];
                if (pop <= 1.0f) continue;
                const float a0 = m_fieldAttractiveness[i];
                if (a0 < -1e5f) continue;

                struct Nb { size_t j; float diff; };
                Nb nb[4];
                int nbCount = 0;
                float sumDiff = 0.0f;

                auto addNb = [&](int nx, int ny) {
                    if (nx < 0 || ny < 0 || nx >= m_fieldW || ny >= m_fieldH) return;
                    const size_t j = static_cast<size_t>(ny) * static_cast<size_t>(m_fieldW) + static_cast<size_t>(nx);
                    if (j >= n) return;
                    if (m_fieldFoodPotential[j] <= 0.0f) return;
                    float d = m_fieldAttractiveness[j] - a0;
                    if (d <= 0.0f) return;
                    const float cw0 = (i < m_fieldCorridorWeight.size()) ? m_fieldCorridorWeight[i] : 1.0f;
                    const float cw1 = (j < m_fieldCorridorWeight.size()) ? m_fieldCorridorWeight[j] : 1.0f;
                    d *= std::max(0.05f, 0.5f * (cw0 + cw1));
                    nb[nbCount++] = {j, d};
                    sumDiff += d;
                };

                addNb(x + 1, y);
                addNb(x - 1, y);
                addNb(x, y + 1);
                addNb(x, y - 1);
                if (nbCount == 0 || sumDiff <= 0.0f) continue;

                const float move = std::min(pop, pop * migRate);
                for (int k = 0; k < nbCount; ++k) {
                    const float f = move * (nb[k].diff / sumDiff);
                    m_fieldPopDelta[i] -= f;
                    m_fieldPopDelta[nb[k].j] += f;
                }
            }
        }

        for (size_t i = 0; i < n; ++i) {
            m_fieldPopulation[i] = std::max(0.0f, m_fieldPopulation[i] + m_fieldPopDelta[i]);
        }
    }

    // Aggregate country totals for long-hop migration.
    std::vector<double> countryTotal(static_cast<size_t>(countryCount), 0.0);
    for (size_t fi = 0; fi < n; ++fi) {
        if (fi >= ownerN) continue;
        const int owner = m_fieldOwnerId[fi];
        if (owner < 0 || owner >= countryCount) continue;
        countryTotal[static_cast<size_t>(owner)] += static_cast<double>(std::max(0.0f, m_fieldPopulation[fi]));
    }

    std::vector<double> countryDelta(static_cast<size_t>(countryCount), 0.0);
    const bool hasTradeMatrix =
        tradeIntensityMatrix &&
        tradeIntensityMatrix->size() >= static_cast<size_t>(countryCount) * static_cast<size_t>(countryCount);

    for (int i = 0; i < countryCount; ++i) {
        const Country& src = countries[static_cast<size_t>(i)];
        if (src.getPopulation() <= 0) continue;

        const auto& sm = src.getMacroEconomy();
        const double outP = clamp01(sm.migrationPressureOut + 0.65 * refugeePush[static_cast<size_t>(i)]);
        if (outP <= 1e-4) continue;

        const double migrants = std::min(countryTotal[static_cast<size_t>(i)] * 0.06, countryTotal[static_cast<size_t>(i)] * outP * (0.0018 * yearsD));
        if (migrants <= 1.0) continue;

        struct Dest { int j = -1; double score = 0.0; };
        std::vector<Dest> dest;
        dest.reserve(static_cast<size_t>(countryCount));

        for (int j = 0; j < countryCount; ++j) {
            if (j == i) continue;
            const Country& dst = countries[static_cast<size_t>(j)];
            if (dst.getPopulation() <= 0) continue;
            const auto& dm = dst.getMacroEconomy();
            const double dstPush = refugeePush[static_cast<size_t>(j)];

            double conn = 0.0;
            if (hasTradeMatrix) {
                const size_t ij = static_cast<size_t>(i) * static_cast<size_t>(countryCount) + static_cast<size_t>(j);
                const size_t ji = static_cast<size_t>(j) * static_cast<size_t>(countryCount) + static_cast<size_t>(i);
                conn = static_cast<double>((*tradeIntensityMatrix)[ij]) + 0.6 * static_cast<double>((*tradeIntensityMatrix)[ji]);
            } else if (areCountryIndicesNeighbors(i, j)) {
                conn = 0.35;
            }
            if (conn <= 1e-6 && !areCountryIndicesNeighbors(i, j)) continue;

            const double wageTerm = clamp01(dm.realWage / 2.0);
            const double safety = 0.5 * clamp01(dst.getAvgControl()) + 0.5 * clamp01(dst.getLegitimacy());
            const double disease = clamp01(dm.diseaseBurden);
            const double nutrition = clamp01(dm.foodSecurity);
            const double attract = clamp01(dm.migrationAttractiveness);
            const double culturalPreference = clamp01(m_ctx->config.migration.culturalPreference);
            const double dist = traitDistance(src, dst);
            const double culturalClose = std::exp(-std::max(0.0, m_ctx->config.tech.culturalFrictionStrength) * dist);
            const double culturalTerm = (1.0 - culturalPreference) + culturalPreference * culturalClose;
            const double refugeeSinkPenalty = 1.0 - 0.45 * dstPush;
            const double score = std::max(
                0.0,
                (0.32 * wageTerm + 0.24 * safety + 0.20 * nutrition + 0.24 * attract - 0.20 * disease) *
                (0.35 + 0.65 * clamp01(conn)) *
                culturalTerm *
                std::max(0.20, refugeeSinkPenalty));
            if (score > 1e-6) {
                dest.push_back(Dest{j, score});
            }
        }

        if (dest.empty()) continue;
        std::sort(dest.begin(), dest.end(), [](const Dest& a, const Dest& b) {
            if (a.score != b.score) return a.score > b.score;
            return a.j < b.j;
        });
        if (dest.size() > 6) dest.resize(6);

        double sumScore = 0.0;
        for (const auto& d : dest) sumScore += d.score;
        if (sumScore <= 1e-9) continue;

        countryDelta[static_cast<size_t>(i)] -= migrants;
        for (const auto& d : dest) {
            const double flow = migrants * (d.score / sumScore);
            countryDelta[static_cast<size_t>(d.j)] += flow;
        }
    }

    // Apply country-level long-hop migration as multiplicative rescaling over owned cells.
    std::vector<double> scale(static_cast<size_t>(countryCount), 1.0);
    for (int i = 0; i < countryCount; ++i) {
        const double oldPop = countryTotal[static_cast<size_t>(i)];
        if (oldPop <= 1e-9) continue;
        const double newPop = std::max(0.0, oldPop + countryDelta[static_cast<size_t>(i)]);
        scale[static_cast<size_t>(i)] = newPop / oldPop;
    }

    for (size_t fi = 0; fi < n; ++fi) {
        if (fi >= ownerN) continue;
        const int owner = m_fieldOwnerId[fi];
        if (owner < 0 || owner >= countryCount) continue;
        m_fieldPopulation[fi] = static_cast<float>(std::max(0.0, static_cast<double>(m_fieldPopulation[fi]) * scale[static_cast<size_t>(owner)]));
    }
}

void Map::updateCitiesFromPopulation(std::vector<Country>& countries, int currentYear, int createEveryNYears, News& news) {
    if (!isPopulationGridActive()) {
        return;
    }
    if (m_fieldPopulation.empty()) {
        return;
    }
    const int countryCount = static_cast<int>(countries.size());
    if (countryCount <= 0) {
        return;
    }

    auto clamp01 = [](double v) { return std::max(0.0, std::min(1.0, v)); };
    auto sigmoid = [](double x) {
        if (x >= 20.0) return 1.0;
        if (x <= -20.0) return 0.0;
        return 1.0 / (1.0 + std::exp(-x));
    };

    const size_t nField = m_fieldPopulation.size();
    if (m_fieldCrowding.size() != nField) m_fieldCrowding.assign(nField, 0.0f);
    if (m_fieldSpecialization.size() != nField) m_fieldSpecialization.assign(nField, 0.0f);
    if (m_fieldUrbanShare.size() != nField) m_fieldUrbanShare.assign(nField, 0.0f);
    if (m_fieldUrbanPop.size() != nField) m_fieldUrbanPop.assign(nField, 0.0f);

    // Country-level signals (computed once).
    std::vector<double> marketAccess(static_cast<size_t>(countryCount), 0.0);
    std::vector<double> foodSecurity(static_cast<size_t>(countryCount), 1.0);
    std::vector<double> control(static_cast<size_t>(countryCount), 0.0);
    std::vector<double> stability(static_cast<size_t>(countryCount), 1.0);
    for (int i = 0; i < countryCount; ++i) {
        const Country& c = countries[static_cast<size_t>(i)];
        marketAccess[static_cast<size_t>(i)] = clamp01(c.getMarketAccess());
        foodSecurity[static_cast<size_t>(i)] = clamp01(c.getFoodSecurity());
        control[static_cast<size_t>(i)] = clamp01(c.getAvgControl());
        stability[static_cast<size_t>(i)] = clamp01(c.getStability());
    }

    auto KFor = [&](size_t fi) -> double {
        const double food = (fi < m_fieldFoodPotential.size()) ? static_cast<double>(std::max(0.0f, m_fieldFoodPotential[fi])) : 0.0;
        return std::max(1.0, food * 1200.0);
    };

    std::vector<double> totalUrbanPop(static_cast<size_t>(countryCount), 0.0);
    std::vector<double> totalSpecialists(static_cast<size_t>(countryCount), 0.0);

    // Per-cell continuous specialization + urbanization (fast field scan).
    const size_t ownerN = m_fieldOwnerId.size();
    for (size_t fi = 0; fi < nField; ++fi) {
        const float foodPot = (fi < m_fieldFoodPotential.size()) ? m_fieldFoodPotential[fi] : 0.0f;
        const float popF = m_fieldPopulation[fi];
        if (foodPot <= 0.0f || popF <= 0.0f || fi >= ownerN) {
            m_fieldCrowding[fi] = 0.0f;
            m_fieldSpecialization[fi] = 0.0f;
            m_fieldUrbanShare[fi] = 0.0f;
            m_fieldUrbanPop[fi] = 0.0f;
            continue;
        }

        const int owner = m_fieldOwnerId[fi];
        if (owner < 0 || owner >= countryCount) {
            m_fieldCrowding[fi] = 0.0f;
            m_fieldSpecialization[fi] = 0.0f;
            m_fieldUrbanShare[fi] = 0.0f;
            m_fieldUrbanPop[fi] = 0.0f;
            continue;
        }

        const double pop = static_cast<double>(std::max(0.0f, popF));
        const double K = KFor(fi);
        const double crowd = (K > 1e-9) ? (pop / K) : 2.0;

        const double ma = marketAccess[static_cast<size_t>(owner)];
        const double fs = foodSecurity[static_cast<size_t>(owner)];
        const double ctl = control[static_cast<size_t>(owner)];
        const double st = stability[static_cast<size_t>(owner)];

        // Smooth specialization: rises with sustained land pressure (crowding) + access/security/control.
        const double x =
            4.0 * (std::min(3.0, crowd) - 1.0) +
            2.0 * (ma - 0.35) +
            1.8 * (fs - 0.80) +
            1.6 * (ctl - 0.50) +
            1.0 * (st - 0.50);
        const double spec = sigmoid(x);

        const double uShare = std::clamp(0.01 + 0.35 * spec, 0.01, 0.45);
        const double uPop = pop * uShare;
        const double specialists = uPop * (0.35 + 0.65 * spec);

        m_fieldCrowding[fi] = static_cast<float>(crowd);
        m_fieldSpecialization[fi] = static_cast<float>(spec);
        m_fieldUrbanShare[fi] = static_cast<float>(uShare);
        m_fieldUrbanPop[fi] = static_cast<float>(uPop);

        totalUrbanPop[static_cast<size_t>(owner)] += uPop;
        totalSpecialists[static_cast<size_t>(owner)] += specialists;
    }

    // Update per-country totals (continuous effects).
    for (int i = 0; i < countryCount; ++i) {
        Country& c = countries[static_cast<size_t>(i)];
        if (c.getPopulation() <= 0) {
            c.setTotalCityPopulation(0.0);
            c.setSpecialistPopulation(0.0);
            c.resetCityCandidate();
            continue;
        }
        c.setTotalCityPopulation(totalUrbanPop[static_cast<size_t>(i)]);
        c.setSpecialistPopulation(totalSpecialists[static_cast<size_t>(i)]);
    }

    // Update existing city objects using the continuous urbanization rule.
    {
        constexpr float kAdminScale = 2000.0f; // sqrt(people) -> contribution (diminishing returns)
        for (int i = 0; i < countryCount; ++i) {
            Country& c = countries[static_cast<size_t>(i)];
            auto& cities = c.getCitiesMutable();
            for (auto& city : cities) {
                const sf::Vector2i loc = city.getLocation();
                const int fx = std::max(0, std::min(m_fieldW - 1, loc.x / kFieldCellSize));
                const int fy = std::max(0, std::min(m_fieldH - 1, loc.y / kFieldCellSize));
                const size_t fi = static_cast<size_t>(fy) * static_cast<size_t>(m_fieldW) + static_cast<size_t>(fx);
                const float cityPop = (fi < m_fieldUrbanPop.size()) ? std::max(0.0f, m_fieldUrbanPop[fi]) : 0.0f;
                city.setPopulation(cityPop);
                city.setAdminContribution((kAdminScale > 1e-6f) ? (std::sqrt(cityPop) / kAdminScale) : 0.0f);
                city.setMajorCity(cityPop >= 1'000'000.0f);
            }
        }
    }

    // Create new cities on a cadence by scanning for population maxima.
    if (createEveryNYears <= 0 || (currentYear % createEveryNYears) != 0) {
        return;
    }

    const int desiredDistField = 5; // soft spacing (discourages clustering without hard gating)

    struct Best {
        double score = 0.0;
        float urbanPop = 0.0f;
        int fx = -1;
        int fy = -1;
        size_t fi = 0;
    };
    std::vector<Best> best(static_cast<size_t>(countryCount));

    // Precompute existing city positions in field coords (for spacing penalty).
    std::vector<std::vector<sf::Vector2i>> cityField(static_cast<size_t>(countryCount));
    for (int i = 0; i < countryCount; ++i) {
        const auto& cities = countries[static_cast<size_t>(i)].getCities();
        auto& out = cityField[static_cast<size_t>(i)];
        out.reserve(cities.size());
        for (const auto& city : cities) {
            out.push_back(sf::Vector2i(city.getLocation().x / kFieldCellSize, city.getLocation().y / kFieldCellSize));
        }
    }

    auto urbanPopAt = [&](int fx, int fy) -> float {
        if (fx < 0 || fy < 0 || fx >= m_fieldW || fy >= m_fieldH) return 0.0f;
        const size_t fi = static_cast<size_t>(fy) * static_cast<size_t>(m_fieldW) + static_cast<size_t>(fx);
        return (fi < m_fieldUrbanPop.size()) ? m_fieldUrbanPop[fi] : 0.0f;
    };

    for (int fy = 0; fy < m_fieldH; ++fy) {
        for (int fx = 0; fx < m_fieldW; ++fx) {
            const size_t fi = static_cast<size_t>(fy) * static_cast<size_t>(m_fieldW) + static_cast<size_t>(fx);
            if (fi >= ownerN || fi >= m_fieldUrbanPop.size()) continue;
            const int owner = m_fieldOwnerId[fi];
            if (owner < 0 || owner >= countryCount) continue;

            const float uPop = m_fieldUrbanPop[fi];
            if (uPop <= 1.0f) continue;

            // Local maximum in implied urban population (4-neighborhood).
            if (urbanPopAt(fx + 1, fy) > uPop) continue;
            if (urbanPopAt(fx - 1, fy) > uPop) continue;
            if (urbanPopAt(fx, fy + 1) > uPop) continue;
            if (urbanPopAt(fx, fy - 1) > uPop) continue;

            const double ma = marketAccess[static_cast<size_t>(owner)];
            const double fs = foodSecurity[static_cast<size_t>(owner)];
            const double ctl = control[static_cast<size_t>(owner)];

            double score = static_cast<double>(uPop) *
                           (0.5 + 0.5 * ma) *
                           (0.5 + 0.5 * fs) *
                           (0.5 + 0.5 * ctl);

            // Soft spacing: penalize cells close to existing cities (but don't hard-gate).
            const auto& cf = cityField[static_cast<size_t>(owner)];
            if (!cf.empty()) {
                int bestDist = 1'000'000;
                for (const auto& p : cf) {
                    const int d = std::abs(p.x - fx) + std::abs(p.y - fy);
                    bestDist = std::min(bestDist, d);
                }
                if (bestDist < 2) continue; // hard minimum to avoid duplicates in the same immediate area
                const double t = std::min(1.0, static_cast<double>(bestDist) / static_cast<double>(std::max(1, desiredDistField)));
                const double spacing = std::clamp(0.25 + 0.75 * t, 0.25, 1.0);
                score *= spacing;
            }

            Best& b = best[static_cast<size_t>(owner)];
            if (score > b.score) {
                b.score = score;
                b.urbanPop = uPop;
                b.fx = fx;
                b.fy = fy;
                b.fi = fi;
            }
        }
    }

    const int height = static_cast<int>(m_countryGrid.size());
    const int width = (height > 0) ? static_cast<int>(m_countryGrid[0].size()) : 0;

    for (int owner = 0; owner < countryCount; ++owner) {
        Best& b = best[static_cast<size_t>(owner)];
        if (b.fx < 0 || b.fy < 0) continue;

        Country& c = countries[static_cast<size_t>(owner)];
        if (c.getPopulation() <= 0) continue;

        const double pop = static_cast<double>(std::max<long long>(1, c.getPopulation()));
        const double requiredUrbanPop = std::max(8000.0, 0.015 * pop);
        const double crowd = (b.fi < m_fieldCrowding.size()) ? static_cast<double>(m_fieldCrowding[b.fi]) : 0.0;
        if (static_cast<double>(b.urbanPop) < requiredUrbanPop || crowd <= 1.03) {
            c.resetCityCandidate();
            continue;
        }

        // Persistence: only found a city if the same candidate persists across several checks.
        Country::CityCandidate& cand = c.getCityCandidateMutable();
        if (cand.fx == b.fx && cand.fy == b.fy) {
            cand.streak += 1;
        } else {
            cand.fx = b.fx;
            cand.fy = b.fy;
            cand.streak = 1;
        }

        const int needStreak = (createEveryNYears >= 75) ? 2 : 3;
        if (cand.streak < needStreak) {
            continue;
        }

        // Don't found if a city already exists in this field cell.
        bool already = false;
        for (const auto& city : c.getCities()) {
            const int cfx = city.getLocation().x / kFieldCellSize;
            const int cfy = city.getLocation().y / kFieldCellSize;
            if (cfx == b.fx && cfy == b.fy) {
                already = true;
                break;
            }
        }
        if (already) {
            c.resetCityCandidate();
            continue;
        }

        // Pick a concrete pixel within this field cell that is owned land (fallback to center).
        sf::Vector2i loc(b.fx * kFieldCellSize + kFieldCellSize / 2, b.fy * kFieldCellSize + kFieldCellSize / 2);
        if (width > 0 && height > 0) {
            const int x0 = b.fx * kFieldCellSize;
            const int y0 = b.fy * kFieldCellSize;
            const int x1 = std::min(width, x0 + kFieldCellSize);
            const int y1 = std::min(height, y0 + kFieldCellSize);
            for (int y = y0; y < y1; ++y) {
                for (int x = x0; x < x1; ++x) {
                    if (!m_isLandGrid[static_cast<size_t>(y)][static_cast<size_t>(x)]) continue;
                    if (m_countryGrid[static_cast<size_t>(y)][static_cast<size_t>(x)] != owner) continue;
                    loc = sf::Vector2i(x, y);
                    y = y1; // break outer
                    break;
                }
            }
        }

        c.foundCity(loc, news);
        c.resetCityCandidate();
    }
}

void Map::ensureCountryAggregateCapacityForIndex(int idx) {
    if (idx < 0) {
        return;
    }
    const size_t need = static_cast<size_t>(idx) + 1u;
    if (m_countryLandCellCount.size() < need) {
        m_countryLandCellCount.resize(need, 0);
        m_countryFoodPotential.resize(need, 0.0);
        m_countryForagingPotential.resize(need, 0.0);
        m_countryFarmingPotential.resize(need, 0.0);
        m_countryOrePotential.resize(need, 0.0);
        m_countryEnergyPotential.resize(need, 0.0);
        m_countryConstructionPotential.resize(need, 0.0);
        m_countryNonFoodPotential.resize(need, 0.0);
    }
}

void Map::rebuildCountryPotentials(int countryCount) {
    if (countryCount <= 0) {
        m_countryLandCellCount.clear();
        m_countryFoodPotential.clear();
        m_countryForagingPotential.clear();
        m_countryFarmingPotential.clear();
        m_countryOrePotential.clear();
        m_countryEnergyPotential.clear();
        m_countryConstructionPotential.clear();
        m_countryNonFoodPotential.clear();
        return;
    }

    m_countryLandCellCount.assign(static_cast<size_t>(countryCount), 0);
    m_countryFoodPotential.assign(static_cast<size_t>(countryCount), 0.0);
    m_countryForagingPotential.assign(static_cast<size_t>(countryCount), 0.0);
    m_countryFarmingPotential.assign(static_cast<size_t>(countryCount), 0.0);
    m_countryOrePotential.assign(static_cast<size_t>(countryCount), 0.0);
    m_countryEnergyPotential.assign(static_cast<size_t>(countryCount), 0.0);
    m_countryConstructionPotential.assign(static_cast<size_t>(countryCount), 0.0);
    m_countryNonFoodPotential.assign(static_cast<size_t>(countryCount), 0.0);

    const int height = static_cast<int>(m_countryGrid.size());
    const int width = (height > 0) ? static_cast<int>(m_countryGrid[0].size()) : 0;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int owner = m_countryGrid[static_cast<size_t>(y)][static_cast<size_t>(x)];
            if (owner < 0 || owner >= countryCount) {
                continue;
            }
            const size_t idx = static_cast<size_t>(y * width + x);
            m_countryLandCellCount[static_cast<size_t>(owner)] += 1;
            if (idx < m_cellFood.size()) {
                m_countryFoodPotential[static_cast<size_t>(owner)] += m_cellFood[idx];
            }
            if (idx < m_cellForaging.size()) {
                m_countryForagingPotential[static_cast<size_t>(owner)] += m_cellForaging[idx];
            }
            if (idx < m_cellFarming.size()) {
                m_countryFarmingPotential[static_cast<size_t>(owner)] += m_cellFarming[idx];
            }
            if (idx < m_cellOre.size()) {
                m_countryOrePotential[static_cast<size_t>(owner)] += m_cellOre[idx];
            }
            if (idx < m_cellEnergy.size()) {
                m_countryEnergyPotential[static_cast<size_t>(owner)] += m_cellEnergy[idx];
            }
            if (idx < m_cellConstruction.size()) {
                m_countryConstructionPotential[static_cast<size_t>(owner)] += m_cellConstruction[idx];
            }
            if (idx < m_cellNonFood.size()) {
                m_countryNonFoodPotential[static_cast<size_t>(owner)] += m_cellNonFood[idx];
            }
        }
    }
}

double Map::getCellFood(int x, int y) const {
    const int height = static_cast<int>(m_countryGrid.size());
    if (y < 0 || y >= height) {
        return 0.0;
    }
    const int width = height > 0 ? static_cast<int>(m_countryGrid[0].size()) : 0;
    if (x < 0 || x >= width) {
        return 0.0;
    }
    const size_t idx = static_cast<size_t>(y * width + x);
    if (idx >= m_cellFood.size()) {
        return 0.0;
    }
    return m_cellFood[idx];
}

int Map::getCellOwner(int x, int y) const {
    const int height = static_cast<int>(m_countryGrid.size());
    if (y < 0 || y >= height) {
        return -1;
    }
    const int width = height > 0 ? static_cast<int>(m_countryGrid[0].size()) : 0;
    if (x < 0 || x >= width) {
        return -1;
    }
    return m_countryGrid[static_cast<size_t>(y)][static_cast<size_t>(x)];
}

double Map::getCountryFoodSum(int countryIndex) const {
    if (countryIndex < 0) {
        return 0.0;
    }
    const size_t idx = static_cast<size_t>(countryIndex);
    if (idx >= m_countryFoodPotential.size()) {
        return 0.0;
    }
    return m_countryFoodPotential[idx];
}

double Map::getCountryForagingPotential(int countryIndex) const {
    if (countryIndex < 0) {
        return 0.0;
    }
    const size_t idx = static_cast<size_t>(countryIndex);
    if (idx >= m_countryForagingPotential.size()) {
        return 0.0;
    }
    return m_countryForagingPotential[idx];
}

double Map::getCountryFarmingPotential(int countryIndex) const {
    if (countryIndex < 0) {
        return 0.0;
    }
    const size_t idx = static_cast<size_t>(countryIndex);
    if (idx >= m_countryFarmingPotential.size()) {
        return 0.0;
    }
    return m_countryFarmingPotential[idx];
}

double Map::getCountryNonFoodPotential(int countryIndex) const {
    if (countryIndex < 0) {
        return 0.0;
    }
    const size_t idx = static_cast<size_t>(countryIndex);
    if (idx >= m_countryNonFoodPotential.size()) {
        return 0.0;
    }
    return m_countryNonFoodPotential[idx];
}

double Map::getCountryOrePotential(int countryIndex) const {
    if (countryIndex < 0) {
        return 0.0;
    }
    const size_t idx = static_cast<size_t>(countryIndex);
    if (idx >= m_countryOrePotential.size()) {
        return 0.0;
    }
    return m_countryOrePotential[idx];
}

double Map::getCountryEnergyPotential(int countryIndex) const {
    if (countryIndex < 0) {
        return 0.0;
    }
    const size_t idx = static_cast<size_t>(countryIndex);
    if (idx >= m_countryEnergyPotential.size()) {
        return 0.0;
    }
    return m_countryEnergyPotential[idx];
}

double Map::getCountryConstructionPotential(int countryIndex) const {
    if (countryIndex < 0) {
        return 0.0;
    }
    const size_t idx = static_cast<size_t>(countryIndex);
    if (idx >= m_countryConstructionPotential.size()) {
        return 0.0;
    }
    return m_countryConstructionPotential[idx];
}

int Map::getCountryLandCellCount(int countryIndex) const {
    if (countryIndex < 0) {
        return 0;
    }
    const size_t idx = static_cast<size_t>(countryIndex);
    if (idx >= m_countryLandCellCount.size()) {
        return 0;
    }
    return m_countryLandCellCount[idx];
}

// Place the function definition in the .cpp file:
std::string generate_country_name(std::mt19937_64& rng) {
    std::vector<std::string> prefixes = { "", "New ", "Old ", "Great ", "North ", "South " };
    std::vector<std::string> syllables = { "na", "mar", "sol", "lin", "ter", "gar", "bel", "kin", "ran", "dus", "zen", "rom", "lor", "via", "qui" };
    std::vector<std::string> suffixes = { "", "ia", "land", "stan", "grad" };

    std::uniform_int_distribution<> numSyllablesDist(2, 3);
    std::uniform_int_distribution<> syllableIndexDist(0, static_cast<int>(syllables.size()) - 1);
    std::uniform_int_distribution<> prefixIndexDist(0, static_cast<int>(prefixes.size()) - 1);
    std::uniform_int_distribution<> suffixIndexDist(0, static_cast<int>(suffixes.size()) - 1);

    std::string name = prefixes[prefixIndexDist(rng)];
    int numSyllables = numSyllablesDist(rng);
    for (int i = 0; i < numSyllables; ++i) {
        name += syllables[syllableIndexDist(rng)];
    }
    name += suffixes[suffixIndexDist(rng)];

    if (!name.empty()) {
        name[0] = std::toupper(name[0]);
    }

    return name; // Now returns only the generated name
}

// 🛣️ ROAD BUILDING SUPPORT - Check if a pixel is valid for road construction
bool Map::isValidRoadPixel(int x, int y) const {
    // Check bounds
    if (x < 0 || x >= static_cast<int>(m_isLandGrid[0].size()) || 
        y < 0 || y >= static_cast<int>(m_isLandGrid.size())) {
        return false;
    }
    
    // Roads can be built on land
    return m_isLandGrid[y][x];
}

bool Map::loadSpawnZones(const std::string& filename) {
    if (!m_spawnZoneImage.loadFromFile(filename)) {
        std::cerr << "Error: Could not load spawn zone image: " << filename << std::endl;
        return false;
    }
    return true;
}

sf::Vector2i Map::getRandomCellInPreferredZones(std::mt19937_64& gen) {
    std::uniform_int_distribution<> xDist(0, m_spawnZoneImage.getSize().x - 1);
    std::uniform_int_distribution<> yDist(0, m_spawnZoneImage.getSize().y - 1);

    while (true) {
        int x = xDist(gen);
        int y = yDist(gen);

        if (m_spawnZoneImage.getPixel(x, y) == m_spawnZoneColor && m_isLandGrid[y][x]) {
            return sf::Vector2i(x, y);
        }
    }
}

void Map::initializeCountries(std::vector<Country>& countries, int numCountries) {
    attachCountriesForOwnershipSync(&countries);
    std::mt19937_64& rng = m_ctx->worldRng;
    std::uniform_int_distribution<> colorDist(50, 255);
    std::uniform_real_distribution<> growthRateDist(0.0003, 0.001); // Legacy - not used in logistic system
    std::uniform_real_distribution<> spawnDist(0.0, 1.0);

    std::uniform_int_distribution<> typeDist(0, 2);
    const int gridH = static_cast<int>(m_isLandGrid.size());
    const int gridW = (gridH > 0) ? static_cast<int>(m_isLandGrid[0].size()) : 0;
    if (gridW <= 0 || gridH <= 0) {
        return;
    }

    // Build deterministic, unique spawn pools (preferred-zone land + all land).
    std::vector<int> preferredLandCells;
    std::vector<int> allLandCells;
    preferredLandCells.reserve(static_cast<size_t>(gridW * gridH / 8));
    allLandCells.reserve(static_cast<size_t>(gridW * gridH / 2));
    const bool spawnZoneMatchesGrid =
        (static_cast<int>(m_spawnZoneImage.getSize().x) == gridW &&
         static_cast<int>(m_spawnZoneImage.getSize().y) == gridH);
    for (int y = 0; y < gridH; ++y) {
        for (int x = 0; x < gridW; ++x) {
            if (!m_isLandGrid[static_cast<size_t>(y)][static_cast<size_t>(x)]) {
                continue;
            }
            const int packed = y * gridW + x;
            allLandCells.push_back(packed);
            if (spawnZoneMatchesGrid && m_spawnZoneImage.getPixel(static_cast<unsigned int>(x), static_cast<unsigned int>(y)) == m_spawnZoneColor) {
                preferredLandCells.push_back(packed);
            }
        }
    }
    std::shuffle(preferredLandCells.begin(), preferredLandCells.end(), rng);
    std::shuffle(allLandCells.begin(), allLandCells.end(), rng);
    std::vector<uint8_t> spawnTaken(static_cast<size_t>(gridW) * static_cast<size_t>(gridH), 0u);
    size_t prefCursor = 0;
    size_t allCursor = 0;
    auto claimFromPool = [&](const std::vector<int>& pool, size_t& cursor, sf::Vector2i& out) -> bool {
        while (cursor < pool.size()) {
            const int packed = pool[cursor++];
            if (packed < 0) continue;
            const size_t idx = static_cast<size_t>(packed);
            if (idx >= spawnTaken.size()) continue;
            if (spawnTaken[idx] != 0u) continue;
            spawnTaken[idx] = 1u;
            out.x = packed % gridW;
            out.y = packed / gridW;
            return true;
        }
        return false;
    };

    // ============================================================
    // Phase 0: realistic 5000 BCE global population (heavy tail)
    // ============================================================
    const long long worldPopMin = 5'000'000;
    const long long worldPopMax = 20'000'000;
    std::uniform_int_distribution<long long> worldPopDist(worldPopMin, worldPopMax);
    const long long worldPopTarget = worldPopDist(rng);
    std::cout << "World start population target: " << worldPopTarget << " (seed " << m_ctx->worldSeed << ")" << std::endl;

    const long long minPop = 1'000;
    const long long maxPop = 300'000;
    const int nC = std::max(1, numCountries);

    std::normal_distribution<double> normal01(0.0, 1.0);
    std::vector<double> weights(static_cast<size_t>(nC), 1.0);
    double sumW = 0.0;
    for (int i = 0; i < nC; ++i) {
        const double w = std::exp(normal01(rng)); // lognormal heavy tail
        weights[static_cast<size_t>(i)] = w;
        sumW += w;
    }
    if (sumW <= 1e-9) sumW = 1.0;

    std::vector<long long> startPop(static_cast<size_t>(nC), minPop);
    long long assigned = 0;
    for (int i = 0; i < nC; ++i) {
        const double share = static_cast<double>(worldPopTarget) * (weights[static_cast<size_t>(i)] / sumW);
        long long p = static_cast<long long>(std::llround(share));
        p = std::max(minPop, std::min(maxPop, p));
        startPop[static_cast<size_t>(i)] = p;
        assigned += p;
    }

    long long diff = worldPopTarget - assigned;
    std::vector<int> order(static_cast<size_t>(nC), 0);
    for (int i = 0; i < nC; ++i) order[static_cast<size_t>(i)] = i;
    std::shuffle(order.begin(), order.end(), rng);
    if (diff > 0) {
        for (int idx : order) {
            if (diff <= 0) break;
            long long& p = startPop[static_cast<size_t>(idx)];
            const long long room = maxPop - p;
            if (room <= 0) continue;
            const long long add = std::min(room, diff);
            p += add;
            diff -= add;
        }
    } else if (diff < 0) {
        for (int idx : order) {
            if (diff >= 0) break;
            long long& p = startPop[static_cast<size_t>(idx)];
            const long long room = p - minPop;
            if (room <= 0) continue;
            const long long sub = std::min(room, -diff);
            p -= sub;
            diff += sub;
        }
    }
    // Final safety: if any remainder persists (should be rare), resolve with small random nudges.
    if (diff != 0) {
        std::uniform_int_distribution<int> pick(0, nC - 1);
        int guard = 0;
        while (diff != 0 && guard++ < 5'000'000) {
            const int idx = pick(rng);
            long long& p = startPop[static_cast<size_t>(idx)];
            if (diff > 0 && p < maxPop) {
                p += 1;
                diff -= 1;
            } else if (diff < 0 && p > minPop) {
                p -= 1;
                diff += 1;
            }
        }
    }

    std::uniform_int_distribution<int> settlementSeedDist(2, 5);

    auto getFieldYieldAtCell = [&](int x, int y) -> double {
        if (m_fieldW <= 0 || m_fieldH <= 0 || m_fieldFoodYieldMult.empty()) {
            return 1.0;
        }
        const int fx = std::max(0, std::min(m_fieldW - 1, x / kFieldCellSize));
        const int fy = std::max(0, std::min(m_fieldH - 1, y / kFieldCellSize));
        const size_t fi = static_cast<size_t>(fy) * static_cast<size_t>(m_fieldW) + static_cast<size_t>(fx);
        if (fi >= m_fieldFoodYieldMult.size()) {
            return 1.0;
        }
        return std::clamp(static_cast<double>(m_fieldFoodYieldMult[fi]), 0.20, 1.80);
    };
    auto getFoodAtCell = [&](int x, int y) -> double {
        const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(gridW) + static_cast<size_t>(x);
        if (idx >= m_cellFood.size()) {
            return 0.0;
        }
        return std::max(0.0, m_cellFood[idx]);
    };
    auto computeCellSuitability = [&](int x, int y, int frontierDist) -> double {
        if (x < 0 || y < 0 || x >= gridW || y >= gridH) return -1e9;
        if (!m_isLandGrid[static_cast<size_t>(y)][static_cast<size_t>(x)]) return -1e9;

        const double climateYield = getFieldYieldAtCell(x, y);
        const double food = getFoodAtCell(x, y);
        const double foodNorm = std::clamp((food * climateYield) / 130.0, 0.0, 1.35);
        const double climateNorm = std::clamp((climateYield - 0.35) / 1.20, 0.0, 1.25);
        const double riverCoastProxy = std::clamp((food - 45.0) / 70.0, 0.0, 1.0);

        int waterAdj = 0;
        int nAdj = 0;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                const int nx = x + dx;
                const int ny = y + dy;
                ++nAdj;
                if (nx < 0 || ny < 0 || nx >= gridW || ny >= gridH ||
                    !m_isLandGrid[static_cast<size_t>(ny)][static_cast<size_t>(nx)]) {
                    waterAdj++;
                }
            }
        }
        const double coastNorm = (nAdj > 0) ? (static_cast<double>(waterAdj) / static_cast<double>(nAdj)) : 0.0;
        const double distancePenalty = 0.012 * static_cast<double>(std::max(0, frontierDist - 1));

        return (0.55 * foodNorm) + (0.25 * coastNorm) + (0.20 * climateNorm) + (0.10 * riverCoastProxy) - distancePenalty;
    };

    struct SpawnFrontierNode {
        double score = 0.0;
        int packed = -1;
        int seedId = -1;
        int dist = 0;
    };
    struct SpawnFrontierCmp {
        bool operator()(const SpawnFrontierNode& a, const SpawnFrontierNode& b) const {
            if (a.score != b.score) return a.score < b.score; // max score first
            if (a.dist != b.dist) return a.dist > b.dist;     // then closer first
            if (a.seedId != b.seedId) return a.seedId > b.seedId;
            return a.packed > b.packed;
        }
    };

    const int regionsPerRow = (m_regionSize > 0) ? (gridW / m_regionSize) : 0;

    const int targetCountries = std::min(numCountries, static_cast<int>(allLandCells.size()));
    if (targetCountries < numCountries) {
        std::cout << "Warning: requested " << numCountries << " countries but only " << targetCountries
                  << " unique land cells are available for spawning." << std::endl;
    }

    for (int i = 0; i < targetCountries; ++i) {
        sf::Vector2i startCell;
        double spawnRoll = spawnDist(rng);
        bool gotCell = false;
        if (spawnRoll < 0.75 && !preferredLandCells.empty()) {
            gotCell = claimFromPool(preferredLandCells, prefCursor, startCell);
        }
        if (!gotCell) {
            gotCell = claimFromPool(allLandCells, allCursor, startCell);
        }
        if (!gotCell) {
            break;
        }

        sf::Color countryColor(colorDist(rng), colorDist(rng), colorDist(rng));
        const long long initialPopulation = startPop[static_cast<size_t>(i)];
        double growthRate = growthRateDist(rng);

        std::string countryName = generate_country_name(rng);
        while (isNameTaken(countries, countryName)) {
            countryName = generate_country_name(rng);
        }

        countryName += " Tribe";

        Country::Type countryType = static_cast<Country::Type>(typeDist(rng));
        countries.emplace_back(i,
                               countryColor,
                               startCell,
                               initialPopulation,
                               growthRate,
                               countryName,
                               countryType,
                               m_ctx->seedForCountry(i));

        // Scale initial claimed area by population and local carrying potential.
        double localFoodPotential = 0.0;
        double localYield = 0.0;
        int localSamples = 0;
        const int localSampleRadius = 4;
        for (int dy = -localSampleRadius; dy <= localSampleRadius; ++dy) {
            for (int dx = -localSampleRadius; dx <= localSampleRadius; ++dx) {
                const int x = startCell.x + dx;
                const int y = startCell.y + dy;
                if (x < 0 || y < 0 || x >= gridW || y >= gridH) continue;
                if (!m_isLandGrid[static_cast<size_t>(y)][static_cast<size_t>(x)]) continue;
                localFoodPotential += getFoodAtCell(x, y);
                localYield += getFieldYieldAtCell(x, y);
                localSamples++;
            }
        }
        if (localSamples <= 0) {
            localFoodPotential = std::max(1.0, getFoodAtCell(startCell.x, startCell.y));
            localYield = getFieldYieldAtCell(startCell.x, startCell.y);
            localSamples = 1;
        }
        localFoodPotential /= static_cast<double>(localSamples);
        localYield /= static_cast<double>(localSamples);

        const int requestedSeedCount = settlementSeedDist(rng);
        const double localCarrying = std::max(5.0, localFoodPotential * std::clamp(localYield, 0.35, 1.80));
        const double targetDensity = std::clamp(220.0 + 8.5 * localCarrying, 240.0, 1900.0);
        int requiredAreaCells = static_cast<int>(std::ceil(static_cast<double>(initialPopulation) / targetDensity));
        requiredAreaCells = std::max(requiredAreaCells, requestedSeedCount * 3);
        requiredAreaCells = std::clamp(requiredAreaCells, requestedSeedCount * 3, 1200);

        // Pick 2-5 nearby settlement seeds, then grow each and finally fill by best-suitability frontier.
        const int seedRadius = std::clamp(6 + static_cast<int>(std::sqrt(static_cast<double>(requiredAreaCells))), 8, 20);
        const int minSeedSpacing = 4;
        const int startPacked = startCell.y * gridW + startCell.x;
        std::vector<int> seedPacked;
        seedPacked.reserve(static_cast<size_t>(requestedSeedCount));
        seedPacked.push_back(startPacked);

        struct SeedCandidate {
            int packed = -1;
            double score = 0.0;
        };
        std::vector<SeedCandidate> seedCandidates;
        seedCandidates.reserve(static_cast<size_t>((2 * seedRadius + 1) * (2 * seedRadius + 1)));
        for (int y = std::max(0, startCell.y - seedRadius); y <= std::min(gridH - 1, startCell.y + seedRadius); ++y) {
            for (int x = std::max(0, startCell.x - seedRadius); x <= std::min(gridW - 1, startCell.x + seedRadius); ++x) {
                const int dx = x - startCell.x;
                const int dy = y - startCell.y;
                if (dx * dx + dy * dy > seedRadius * seedRadius) continue;
                if (!m_isLandGrid[static_cast<size_t>(y)][static_cast<size_t>(x)]) continue;
                if (m_countryGrid[static_cast<size_t>(y)][static_cast<size_t>(x)] != -1) continue;
                const int packed = y * gridW + x;
                const double score = computeCellSuitability(x, y, /*frontierDist*/1) - 0.008 * std::sqrt(static_cast<double>(dx * dx + dy * dy));
                seedCandidates.push_back(SeedCandidate{packed, score});
            }
        }
        std::sort(seedCandidates.begin(), seedCandidates.end(), [](const SeedCandidate& a, const SeedCandidate& b) {
            if (a.score != b.score) return a.score > b.score;
            return a.packed < b.packed;
        });

        for (const SeedCandidate& c : seedCandidates) {
            if (static_cast<int>(seedPacked.size()) >= requestedSeedCount) break;
            if (c.packed == startPacked) continue;
            const int cx = c.packed % gridW;
            const int cy = c.packed / gridW;
            bool spaced = true;
            for (int sPacked : seedPacked) {
                const int sx = sPacked % gridW;
                const int sy = sPacked / gridW;
                if (std::abs(cx - sx) + std::abs(cy - sy) < minSeedSpacing) {
                    spaced = false;
                    break;
                }
            }
            if (spaced) {
                seedPacked.push_back(c.packed);
            }
        }
        // Fallback fill if spacing constraint was too strict (tiny islands/coasts).
        for (const SeedCandidate& c : seedCandidates) {
            if (static_cast<int>(seedPacked.size()) >= requestedSeedCount) break;
            if (std::find(seedPacked.begin(), seedPacked.end(), c.packed) != seedPacked.end()) continue;
            seedPacked.push_back(c.packed);
        }

        std::vector<int> claimedPacked;
        claimedPacked.reserve(static_cast<size_t>(requiredAreaCells) + 32u);
        std::vector<int> activeSeedPacked;
        activeSeedPacked.reserve(seedPacked.size());

        auto claimPackedAssumingLocked = [&](int packed) -> bool {
            if (packed < 0) return false;
            const int x = packed % gridW;
            const int y = packed / gridW;
            if (x < 0 || y < 0 || x >= gridW || y >= gridH) return false;
            if (!m_isLandGrid[static_cast<size_t>(y)][static_cast<size_t>(x)]) return false;
            if (m_countryGrid[static_cast<size_t>(y)][static_cast<size_t>(x)] != -1) return false;
            if (!setCountryOwnerAssumingLockedImpl(x, y, i)) return false;
            claimedPacked.push_back(packed);
            return true;
        };

        auto pushNeighbors = [&](int packed,
                                 int seedId,
                                 int nextDist,
                                 std::priority_queue<SpawnFrontierNode, std::vector<SpawnFrontierNode>, SpawnFrontierCmp>& frontier,
                                 std::unordered_set<int>& queued) {
            static const int ndx[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
            static const int ndy[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
            const int x = packed % gridW;
            const int y = packed / gridW;
            for (int k = 0; k < 8; ++k) {
                const int nx = x + ndx[k];
                const int ny = y + ndy[k];
                if (nx < 0 || ny < 0 || nx >= gridW || ny >= gridH) continue;
                if (!m_isLandGrid[static_cast<size_t>(ny)][static_cast<size_t>(nx)]) continue;
                if (m_countryGrid[static_cast<size_t>(ny)][static_cast<size_t>(nx)] != -1) continue;
                const int npacked = ny * gridW + nx;
                if (!queued.insert(npacked).second) continue;
                frontier.push(SpawnFrontierNode{
                    computeCellSuitability(nx, ny, nextDist),
                    npacked,
                    seedId,
                    nextDist
                });
            }
        };

        {
            std::lock_guard<std::mutex> lock(m_gridMutex);
            for (int packed : seedPacked) {
                if (static_cast<int>(claimedPacked.size()) >= requiredAreaCells) break;
                if (claimPackedAssumingLocked(packed)) {
                    activeSeedPacked.push_back(packed);
                }
            }
            if (activeSeedPacked.empty()) {
                claimPackedAssumingLocked(startPacked);
                activeSeedPacked.push_back(startPacked);
            }

            const int activeSeeds = std::max(1, static_cast<int>(activeSeedPacked.size()));
            const int localBurstTarget = std::max(2, requiredAreaCells / std::max(2, activeSeeds * 3));

            for (int s = 0; s < static_cast<int>(activeSeedPacked.size()) &&
                            static_cast<int>(claimedPacked.size()) < requiredAreaCells; ++s) {
                std::priority_queue<SpawnFrontierNode, std::vector<SpawnFrontierNode>, SpawnFrontierCmp> localFrontier;
                std::unordered_set<int> localQueued;
                localQueued.reserve(256u);
                int grown = 1;
                pushNeighbors(activeSeedPacked[static_cast<size_t>(s)], s, 1, localFrontier, localQueued);
                while (static_cast<int>(claimedPacked.size()) < requiredAreaCells &&
                       grown < localBurstTarget &&
                       !localFrontier.empty()) {
                    const SpawnFrontierNode node = localFrontier.top();
                    localFrontier.pop();
                    if (!claimPackedAssumingLocked(node.packed)) continue;
                    grown++;
                    pushNeighbors(node.packed, s, node.dist + 1, localFrontier, localQueued);
                }
            }

            std::priority_queue<SpawnFrontierNode, std::vector<SpawnFrontierNode>, SpawnFrontierCmp> frontier;
            std::unordered_set<int> queued;
            queued.reserve(std::max(512, requiredAreaCells * 4));
            for (size_t cIdx = 0; cIdx < claimedPacked.size(); ++cIdx) {
                pushNeighbors(claimedPacked[cIdx], static_cast<int>(cIdx % static_cast<size_t>(std::max(1, activeSeeds))), 1, frontier, queued);
            }

            while (static_cast<int>(claimedPacked.size()) < requiredAreaCells && !frontier.empty()) {
                const SpawnFrontierNode node = frontier.top();
                frontier.pop();
                if (!claimPackedAssumingLocked(node.packed)) continue;
                pushNeighbors(node.packed, node.seedId, node.dist + 1, frontier, queued);
            }
        }

        if (regionsPerRow > 0) {
            for (int packed : claimedPacked) {
                const int x = packed % gridW;
                const int y = packed / gridW;
                const int regionX = x / m_regionSize;
                const int regionY = y / m_regionSize;
                m_dirtyRegions.insert(regionY * regionsPerRow + regionX);
            }
        } else {
            m_dirtyRegions.insert(0);
        }
    }

    // Build the initial adjacency/contact counts from the completed grid. From this point forward,
    // territory changes should go through `setCountryOwner*()` so adjacency stays correct incrementally.
    rebuildCountryPotentials(static_cast<int>(countries.size()));
    rebuildAdjacency(countries);
    updateControlGrid(countries, /*year*/-5000, /*dtYears*/1);
    initializePopulationGridFromCountries(countries);
    applyPopulationTotalsToCountries(countries);
}

void Map::attachCountriesForOwnershipSync(std::vector<Country>* countries) {
    m_ownershipSyncCountries = countries;
}

// Add a helper function to check for name uniqueness
bool isNameTaken(const std::vector<Country>& countries, const std::string& name) {
    for (const auto& country : countries) {
        if (country.getName() == name) {
            return true;
        }
    }
    return false;
}

void Map::updateCountries(std::vector<Country>& countries, int currentYear, News& news, TechnologyManager& technologyManager) {
    attachCountriesForOwnershipSync(&countries);
    m_dirtyRegions.clear();

    // Trigger the plague in the year 4950
    if (currentYear == m_nextPlagueYear) {
        startPlague(currentYear, news);
        initializePlagueCluster(countries); // Initialize geographic cluster
    }

    if (m_plagueActive && currentYear > m_plagueStartYear) {
        updatePlagueSpread(countries);
    }

    // Check if the plague should end
    if (m_plagueActive && (currentYear == m_plagueStartYear + 3)) {
        endPlague(news);
    }

    // No need for tempGrid here anymore - tempGrid is handled within Country::update
    // std::vector<std::vector<int>> tempGrid = m_countryGrid; // REMOVE THIS LINE

		    // 🛡️ PERFORMANCE FIX: Remove OpenMP to prevent mutex contention and thread blocking
		    for (int i = 0; i < countries.size(); ++i) {
                Country& c = countries[static_cast<size_t>(i)];
                auto& macro = c.getMacroEconomyMutable();
                auto& sdbg = macro.stabilityDebug;
                auto& ldbg = macro.legitimacyDebug;
                sdbg = Country::MacroEconomyState::StabilityDebug{};
                ldbg = Country::MacroEconomyState::LegitimacyDebug{};
                sdbg.dbg_stab_start_year = std::clamp(c.getStability(), 0.0, 1.0);
                sdbg.dbg_stab_after_country_update = sdbg.dbg_stab_start_year;
                sdbg.dbg_stab_after_budget = sdbg.dbg_stab_start_year;
                sdbg.dbg_stab_after_demography = sdbg.dbg_stab_start_year;
                sdbg.dbg_pop_country_before_update = static_cast<double>(std::max<long long>(0, c.getPopulation()));
                sdbg.dbg_gold = std::max(0.0, c.getGold());
                sdbg.dbg_debt = std::max(0.0, c.getDebt());
                sdbg.dbg_avgControl = std::clamp(c.getAvgControl(), 0.0, 1.0);
                ldbg.dbg_legit_start = std::clamp(c.getLegitimacy(), 0.0, 1.0);
                ldbg.dbg_legit_after_economy = ldbg.dbg_legit_start;
                ldbg.dbg_legit_after_budget = ldbg.dbg_legit_start;
                ldbg.dbg_legit_after_demog = ldbg.dbg_legit_start;
                ldbg.dbg_legit_after_culture = ldbg.dbg_legit_start;
                ldbg.dbg_legit_end = ldbg.dbg_legit_start;
		        countries[i].update(m_isLandGrid, m_countryGrid, m_gridMutex, m_gridCellSize, m_regionSize, m_dirtyRegions, currentYear, m_resourceGrid, news, m_plagueActive, m_plagueDeathToll, *this, technologyManager, countries);
		        countries[i].attemptTechnologySharing(currentYear, countries, technologyManager, *this, news);
		    }

    // Clean up extinct countries without erasing (keeps country indices stable).
    for (int i = 0; i < static_cast<int>(countries.size()); ++i) {
        const long long pop = countries[i].getPopulation();
        const bool hasTerritory = !countries[i].getBoundaryPixels().empty();
        const bool hasCities = !countries[i].getCities().empty();
        const size_t territoryCells = countries[i].getBoundaryPixels().size();
        const bool strandedMicroPolity = hasTerritory && !hasCities && territoryCells <= 1u && pop > 0 && pop < 2000;
        if (pop <= 0 || !hasTerritory || strandedMicroPolity) {
            markCountryExtinct(countries, i, currentYear, news);
        }
    }

    // Phase 2: update coarse control field after territorial/policy changes.
    updateControlGrid(countries, currentYear, 1);

    // REMOVE THIS ENTIRE BLOCK - Grid update is now handled within Country::update
    /*
    // Update m_countryGrid from tempGrid after processing all countries
    {
        std::lock_guard<std::mutex> lock(m_gridMutex);
        m_countryGrid = tempGrid;
    }
    */
}

void Map::tickDemographyAndCities(std::vector<Country>& countries,
                                  int currentYear,
                                  int dtYears,
                                  News& news,
                                  const std::vector<float>* tradeIntensityMatrix) {
    attachCountriesForOwnershipSync(&countries);
    const int years = std::max(1, dtYears);
    const double yearsD = static_cast<double>(years);
    const int countryCount = static_cast<int>(countries.size());
    if (countryCount <= 0 || m_fieldPopulation.empty() || m_fieldOwnerId.empty()) {
        return;
    }
    if (m_countryRefugeePush.size() != static_cast<size_t>(countryCount)) {
        m_countryRefugeePush.assign(static_cast<size_t>(countryCount), 0.0);
    }

    auto clamp01 = [](double v) { return std::max(0.0, std::min(1.0, v)); };
    auto sigmoid = [](double x) {
        if (x > 20.0) return 1.0;
        if (x < -20.0) return 0.0;
        return 1.0 / (1.0 + std::exp(-x));
    };

    tickPopulationGrid(countries, currentYear, years, tradeIntensityMatrix);

    // Aggregate owner totals after migration (before births/deaths).
    std::vector<double> oldTotals(static_cast<size_t>(countryCount), 0.0);
    const size_t nField = std::min(m_fieldPopulation.size(), m_fieldOwnerId.size());
    for (size_t fi = 0; fi < nField; ++fi) {
        const int owner = m_fieldOwnerId[fi];
        if (owner < 0 || owner >= countryCount) continue;
        oldTotals[static_cast<size_t>(owner)] += static_cast<double>(std::max(0.0f, m_fieldPopulation[fi]));
    }

    prepareCountryClimateCaches(countryCount);

    // Use previous-year infection state as seed base for deterministic country-order independence.
    std::vector<double> prevI(static_cast<size_t>(countryCount), 0.0);
    for (int i = 0; i < countryCount; ++i) {
        prevI[static_cast<size_t>(i)] = clamp01(countries[static_cast<size_t>(i)].getEpidemicState().i);
    }

    const bool hasTradeMatrix =
        tradeIntensityMatrix &&
        tradeIntensityMatrix->size() >= static_cast<size_t>(countryCount) * static_cast<size_t>(countryCount);

    std::vector<double> newTotals(static_cast<size_t>(countryCount), 0.0);

    for (int i = 0; i < countryCount; ++i) {
        Country& c = countries[static_cast<size_t>(i)];
        Country::MacroEconomyState& m = c.getMacroEconomyMutable();
        auto& sdbg = m.stabilityDebug;
        auto& ldbg = m.legitimacyDebug;
        const double oldPop = std::max(0.0, oldTotals[static_cast<size_t>(i)]);
        sdbg.dbg_pop_grid_oldTotals = oldPop;
        const double popBeforeUpdate = std::max(1.0, sdbg.dbg_pop_country_before_update);
        sdbg.dbg_pop_mismatch_ratio = oldPop / popBeforeUpdate;
        if (oldPop <= 1e-9) {
            c.setPopulation(0);
            c.getPopulationCohortsMutable().fill(0.0);
            auto& epi = c.getEpidemicStateMutable();
            epi.s = 1.0;
            epi.i = 0.0;
            epi.r = 0.0;
            m.lastBirths = 0.0;
            m.lastDeathsBase = 0.0;
            m.lastDeathsFamine = 0.0;
            m.lastDeathsEpi = 0.0;
            m.lastAvgNutrition = 1.0;
            m.refugeePush = 0.0;
            m_countryRefugeePush[static_cast<size_t>(i)] = 0.0;
            sdbg.dbg_shortageRatio = 0.0;
            sdbg.dbg_diseaseBurden = 0.0;
            sdbg.dbg_delta_demog_stress = 0.0;
            sdbg.dbg_stab_after_demography = clamp01(c.getStability());
            sdbg.dbg_stab_delta_demog = sdbg.dbg_stab_after_demography - sdbg.dbg_stab_after_budget;
            sdbg.dbg_stab_delta_total = sdbg.dbg_stab_after_demography - sdbg.dbg_stab_start_year;
            ldbg.dbg_legit_demog_shortageRatio = 0.0;
            ldbg.dbg_legit_demog_diseaseBurden = 0.0;
            ldbg.dbg_legit_delta_demog_stress = 0.0;
            ldbg.dbg_legit_after_demog = clamp01(c.getLegitimacy());
            ldbg.dbg_legit_delta_demog = ldbg.dbg_legit_after_demog - ldbg.dbg_legit_after_budget;
            continue;
        }

        c.setPopulation(static_cast<long long>(std::llround(oldPop)));
        c.renormalizePopulationCohortsToTotal();
        auto& cohorts = c.getPopulationCohortsMutable();
        auto& epi = c.getEpidemicStateMutable();

        // Infection import seeding from trade and borders.
        double importedI = 0.0;
        double importW = 0.0;
        if (hasTradeMatrix) {
            for (int j = 0; j < countryCount; ++j) {
                if (j == i) continue;
                const size_t ij = static_cast<size_t>(i) * static_cast<size_t>(countryCount) + static_cast<size_t>(j);
                const size_t ji = static_cast<size_t>(j) * static_cast<size_t>(countryCount) + static_cast<size_t>(i);
                const double w = static_cast<double>((*tradeIntensityMatrix)[ij]) + 0.4 * static_cast<double>((*tradeIntensityMatrix)[ji]);
                if (w <= 1e-9) continue;
                importedI += w * prevI[static_cast<size_t>(j)];
                importW += w;
            }
        }
        for (int j : getAdjacentCountryIndicesPublic(i)) {
            if (j < 0 || j >= countryCount || j == i) continue;
            importedI += 0.15 * prevI[static_cast<size_t>(j)];
            importW += 0.15;
        }
        const double importSeed = (importW > 1e-9) ? (importedI / importW) : 0.0;

        const double climateMult = std::max(0.05, static_cast<double>(getCountryClimateFoodMultiplier(i)));
        const double humidityProxy = clamp01(0.55 + 0.35 * (1.0 - climateMult) + 0.25 * ((i < static_cast<int>(m_countryPrecipAnomMean.size())) ? static_cast<double>(m_countryPrecipAnomMean[static_cast<size_t>(i)]) : 0.0));
        const double urban = (oldPop > 1.0) ? clamp01(c.getTotalCityPopulation() / oldPop) : 0.0;
        const double control = clamp01(c.getAvgControl());
        const double institution = clamp01(m.institutionCapacity);
        const double healthSpend = clamp01(c.getHealthSpendingShare());
        const double legitimacy = clamp01(c.getLegitimacy());
        const bool war = c.isAtWar();

        // Beta/Gamma/Mu are yearly rates; we integrate in yearly substeps for dtYears stability.
        const double beta = std::clamp(
            0.55 *
            (0.35 + 0.65 * urban) *
            (0.45 + 0.55 * humidityProxy) *
            (0.25 + 0.75 * m.connectivityIndex) *
            (0.40 + 0.60 * (1.0 - institution)) *
            (0.70 + 0.30 * (1.0 - healthSpend)),
            0.03, 2.8);
        const double gamma = std::clamp(0.22 + 0.30 * healthSpend + 0.20 * institution, 0.08, 0.85);
        const double mu = std::clamp(0.010 + 0.025 * (1.0 - healthSpend) + 0.020 * (1.0 - institution), 0.001, 0.12);
        const double waning = 0.02;

        double popNow = oldPop;
        int substeps = std::max(1, years);
        const double subDt = yearsD / static_cast<double>(substeps);
        double foodStock = std::max(0.0, m.foodStock);
        double cumulativeShortage = 0.0;
        double cumulativeRequired = 0.0;
        double cumulativeBirths = 0.0;
        double cumulativeDeathsBase = 0.0;
        double cumulativeDeathsFamine = 0.0;
        double cumulativeDeathsEpi = 0.0;
        double nutritionPopWeighted = 0.0;
        double nutritionPopWeight = 0.0;

        for (int step = 0; step < substeps; ++step) {
            const double requiredStep =
                (cohorts[0] * 0.00085 +
                 cohorts[1] * 0.00100 +
                 cohorts[2] * 0.00120 +
                 cohorts[3] * 0.00110 +
                 cohorts[4] * 0.00095) * subDt;
            cumulativeRequired += requiredStep;

            const double prodStep = std::max(0.0, m.lastFoodOutput) * subDt;
            const double impQtyAnnual = (m.priceFood > 1e-9) ? (m.importsValue / m.priceFood) : 0.0;
            const double impStep = std::max(0.0, impQtyAnnual) * subDt;
            const double spoilStep = foodStock * (1.0 - std::pow(std::max(0.0, 1.0 - std::clamp(m.spoilageRate, 0.0, 0.95)), subDt));
            foodStock = std::max(0.0, foodStock - spoilStep);

            const double baseAvail = prodStep + impStep;
            const double draw = std::min(foodStock, std::max(0.0, requiredStep - baseAvail));
            const double avail = baseAvail + draw;
            foodStock = std::max(0.0, foodStock - draw);
            if (avail > requiredStep) {
                foodStock = std::min(std::max(1.0, m.foodStockCap), foodStock + (avail - requiredStep));
            }

            const double shortage = std::max(0.0, requiredStep - avail);
            cumulativeShortage += shortage;
            const double nutrition = clamp01((requiredStep > 1e-9) ? (avail / requiredStep) : 1.0);
            const double famine = 1.0 - nutrition;
            nutritionPopWeighted += nutrition * popNow;
            nutritionPopWeight += popNow;

            // SIR dynamics.
            const double externalI = 0.12 * importSeed;
            const double forceI = clamp01(epi.i + externalI);
            const double newInf = std::min(epi.s, beta * epi.s * forceI * subDt);
            const double rec = std::min(epi.i, gamma * epi.i * subDt);
            const double infDeathsFrac = std::min(epi.i - rec + newInf, mu * epi.i * subDt);
            const double wane = std::min(epi.r, waning * epi.r * subDt);
            epi.s = clamp01(epi.s - newInf + wane);
            epi.i = clamp01(epi.i + newInf - rec - infDeathsFrac);
            epi.r = clamp01(epi.r + rec - wane);
            const double sirNorm = epi.s + epi.i + epi.r;
            if (sirNorm > 1e-9) {
                epi.s /= sirNorm;
                epi.i /= sirNorm;
                epi.r /= sirNorm;
            } else {
                epi.s = 1.0;
                epi.i = 0.0;
                epi.r = 0.0;
            }

            const double infDeathsCount = popNow * infDeathsFrac;

            // February 5, 2026: removed stability multiplier from fertility due to a stability bug suppressing births.
            const double fertilityFemaleRate =
                0.20 *
                (0.25 + 0.75 * nutrition) *
                (0.40 + 0.60 * clamp01(m.realWage / 2.0)) *
                (1.0 - 0.50 * epi.i) *
                (war ? 0.88 : 1.0);
            const double births = std::max(0.0, cohorts[2] * 0.5 * fertilityFemaleRate * subDt);
            cumulativeBirths += births;

            std::array<double, 5> baseDeath = {0.012, 0.002, 0.004, 0.012, 0.050};
            std::array<double, 5> famineAdd = {0.080, 0.020, 0.022, 0.040, 0.090};
            std::array<double, 5> diseaseMult = {
                1.0 + 1.4 * epi.i,
                1.0 + 0.8 * epi.i,
                1.0 + 1.0 * epi.i,
                1.0 + 1.4 * epi.i,
                1.0 + 2.0 * epi.i};

            for (int k = 0; k < 5; ++k) {
                const double cohortK = cohorts[static_cast<size_t>(k)];
                const double baseDeadRaw = cohortK * baseDeath[static_cast<size_t>(k)] * subDt;
                const double famineDeadRaw = cohortK * (famine * famineAdd[static_cast<size_t>(k)]) * subDt;
                const double epiAmplifierRaw = (baseDeadRaw + famineDeadRaw) * std::max(0.0, diseaseMult[static_cast<size_t>(k)] - 1.0);
                const double totalRaw = baseDeadRaw + famineDeadRaw + epiAmplifierRaw;
                const double dead = std::min(cohortK, totalRaw);
                cohorts[static_cast<size_t>(k)] = std::max(0.0, cohorts[static_cast<size_t>(k)] - dead);
                const double scale = (totalRaw > 1e-12) ? (dead / totalRaw) : 0.0;
                cumulativeDeathsBase += baseDeadRaw * scale;
                cumulativeDeathsFamine += famineDeadRaw * scale;
                cumulativeDeathsEpi += epiAmplifierRaw * scale;
            }

            // Apply direct epidemic deaths with age weighting.
            std::array<double, 5> infAgeW = {1.8, 0.9, 1.0, 1.4, 2.2};
            double wsum = 0.0;
            for (int k = 0; k < 5; ++k) {
                wsum += infAgeW[static_cast<size_t>(k)] * cohorts[static_cast<size_t>(k)];
            }
            if (wsum > 1e-9 && infDeathsCount > 0.0) {
                for (int k = 0; k < 5; ++k) {
                    const double part = infDeathsCount * (infAgeW[static_cast<size_t>(k)] * cohorts[static_cast<size_t>(k)] / wsum);
                    const double removed = std::min(cohorts[static_cast<size_t>(k)], part);
                    cohorts[static_cast<size_t>(k)] = std::max(0.0, cohorts[static_cast<size_t>(k)] - removed);
                    cumulativeDeathsEpi += removed;
                }
            }

            // Aging transitions.
            const double a01 = std::min(0.95, subDt / 5.0);
            const double a12 = std::min(0.95, subDt / 10.0);
            const double a23 = std::min(0.95, subDt / 35.0);
            const double a34 = std::min(0.95, subDt / 15.0);
            const double t01 = cohorts[0] * a01;
            const double t12 = cohorts[1] * a12;
            const double t23 = cohorts[2] * a23;
            const double t34 = cohorts[3] * a34;
            cohorts[0] = std::max(0.0, cohorts[0] - t01 + births);
            cohorts[1] = std::max(0.0, cohorts[1] - t12 + t01);
            cohorts[2] = std::max(0.0, cohorts[2] - t23 + t12);
            cohorts[3] = std::max(0.0, cohorts[3] - t34 + t23);
            cohorts[4] = std::max(0.0, cohorts[4] + t34);

            popNow = cohorts[0] + cohorts[1] + cohorts[2] + cohorts[3] + cohorts[4];
            if (popNow <= 1.0) {
                cohorts.fill(0.0);
                epi.s = 1.0;
                epi.i = 0.0;
                epi.r = 0.0;
                popNow = 0.0;
                break;
            }
        }

        const double shortageRatio = (cumulativeRequired > 1e-9) ? clamp01(cumulativeShortage / cumulativeRequired) : 0.0;
        m.famineSeverity = shortageRatio;
        m.foodSecurity = clamp01(1.0 - shortageRatio);
        m.foodStock = foodStock;
        m.diseaseBurden = clamp01(epi.i);
        m.lastBirths = std::max(0.0, cumulativeBirths);
        m.lastDeathsBase = std::max(0.0, cumulativeDeathsBase);
        m.lastDeathsFamine = std::max(0.0, cumulativeDeathsFamine);
        m.lastDeathsEpi = std::max(0.0, cumulativeDeathsEpi);
        m.lastAvgNutrition = (nutritionPopWeight > 1e-9) ? clamp01(nutritionPopWeighted / nutritionPopWeight) : 1.0;
        m.migrationPressureOut = clamp01(
            0.45 * m.famineSeverity +
            0.25 * m.diseaseBurden +
            0.12 * (war ? 1.0 : 0.0) +
            0.10 * clamp01(m.inequality) +
            0.08 * (1.0 - control));
        m.migrationAttractiveness = clamp01(
            0.30 * clamp01(m.realWage / 2.0) +
            0.25 * m.foodSecurity +
            0.20 * (1.0 - m.diseaseBurden) +
            0.15 * institution +
            0.10 * legitimacy);

        // Shock-driven refugee pressure with exponential half-life decay.
        const double halfLife = std::max(0.5, m_ctx->config.migration.refugeeHalfLifeYears);
        const double decay = std::exp(-std::log(2.0) * yearsD / halfLife);
        const double famineShock = std::max(0.0, m.famineSeverity - m_ctx->config.migration.famineShockThreshold);
        const double epiShock = std::max(0.0, m.diseaseBurden - m_ctx->config.migration.epidemicShockThreshold);
        const double warShock = std::max(0.0, c.getWarExhaustion() - m_ctx->config.migration.warShockThreshold);
        const double shockAdd = clamp01(
            famineShock * std::max(0.0, m_ctx->config.migration.famineShockMultiplier) +
            epiShock * std::max(0.0, m_ctx->config.migration.epidemicShockMultiplier) +
            warShock * std::max(0.0, m_ctx->config.migration.warShockMultiplier));
        m.refugeePush = clamp01(m.refugeePush * decay + shockAdd);
        m_countryRefugeePush[static_cast<size_t>(i)] = m.refugeePush;
        m.migrationPressureOut = clamp01(m.migrationPressureOut + 0.55 * m.refugeePush);

        // Autonomy pressure state (used by fragmentation logic).
        const double autonomyUp =
            0.35 * (1.0 - control) +
            0.20 * clamp01(m.inequality) +
            0.18 * (1.0 - legitimacy) +
            0.15 * m.famineSeverity +
            0.12 * (war ? 1.0 : 0.0);
        const double autonomyDown =
            0.34 * c.getAdminSpendingShare() +
            0.26 * c.getInfraSpendingShare() +
            0.20 * clamp01(m.realWage / 2.0) +
            0.20 * m.humanCapital;
        const double autonomy = clamp01(c.getAutonomyPressure() + yearsD * (0.06 * autonomyUp - 0.05 * autonomyDown));
        c.setAutonomyPressure(autonomy);
        if (autonomy > 0.72) {
            c.setAutonomyOverThresholdYears(c.getAutonomyOverThresholdYears() + years);
        } else {
            c.setAutonomyOverThresholdYears(std::max(0, c.getAutonomyOverThresholdYears() - years));
        }

        const double newPop = std::max(0.0, cohorts[0] + cohorts[1] + cohorts[2] + cohorts[3] + cohorts[4]);
        c.setPopulation(static_cast<long long>(std::llround(newPop)));
        c.renormalizePopulationCohortsToTotal();
        newTotals[static_cast<size_t>(i)] = static_cast<double>(std::max<long long>(0, c.getPopulation()));

        // Additional stability/legitimacy feedback from severe stress.
        const double demogStressDelta = -yearsD * (0.03 * shortageRatio + 0.02 * m.diseaseBurden);
        c.setStability(c.getStability() + demogStressDelta);
        const double legitDemogDelta = -yearsD * (0.025 * shortageRatio + 0.015 * m.diseaseBurden);
        const double legitBeforeDemog = clamp01(c.getLegitimacy());
        if ((legitBeforeDemog + legitDemogDelta) < 0.0 && legitBeforeDemog > 0.0) {
            ldbg.dbg_legit_clamp_to_zero_demog++;
        }
        c.setLegitimacy(legitBeforeDemog + legitDemogDelta);
        sdbg.dbg_shortageRatio = shortageRatio;
        sdbg.dbg_diseaseBurden = m.diseaseBurden;
        sdbg.dbg_delta_demog_stress = demogStressDelta;
        sdbg.dbg_stab_after_demography = clamp01(c.getStability());
        sdbg.dbg_stab_delta_demog = sdbg.dbg_stab_after_demography - sdbg.dbg_stab_after_budget;
        sdbg.dbg_stab_delta_total = sdbg.dbg_stab_after_demography - sdbg.dbg_stab_start_year;
        ldbg.dbg_legit_demog_shortageRatio = shortageRatio;
        ldbg.dbg_legit_demog_diseaseBurden = m.diseaseBurden;
        ldbg.dbg_legit_delta_demog_stress = legitDemogDelta;
        ldbg.dbg_legit_after_demog = clamp01(c.getLegitimacy());
        ldbg.dbg_legit_delta_demog = ldbg.dbg_legit_after_demog - ldbg.dbg_legit_after_budget;
    }

    // Reconcile country-level births/deaths onto field population grid via multiplicative owner scaling.
    std::vector<double> ownerScale(static_cast<size_t>(countryCount), 1.0);
    for (int i = 0; i < countryCount; ++i) {
        const double oldPop = std::max(0.0, oldTotals[static_cast<size_t>(i)]);
        const double newPop = std::max(0.0, newTotals[static_cast<size_t>(i)]);
        if (oldPop > 1e-9) {
            ownerScale[static_cast<size_t>(i)] = newPop / oldPop;
        } else if (newPop <= 1e-9) {
            ownerScale[static_cast<size_t>(i)] = 0.0;
        } else {
            ownerScale[static_cast<size_t>(i)] = 1.0;
        }
    }

    for (size_t fi = 0; fi < nField; ++fi) {
        const int owner = m_fieldOwnerId[fi];
        if (owner < 0 || owner >= countryCount) continue;
        m_fieldPopulation[fi] = static_cast<float>(std::max(0.0, static_cast<double>(m_fieldPopulation[fi]) * ownerScale[static_cast<size_t>(owner)]));
    }

    applyPopulationTotalsToCountries(countries);
    const int createEveryNYears = (dtYears <= 1) ? 10 : 50;
    updateCitiesFromPopulation(countries, currentYear, createEveryNYears, news);
}

void Map::markCountryExtinct(std::vector<Country>& countries, int countryIndex, int currentYear, News& news) {
    if (countryIndex < 0 || countryIndex >= static_cast<int>(countries.size())) {
        return;
    }

    Country& extinct = countries[static_cast<size_t>(countryIndex)];
    if (extinct.getPopulation() <= 0 && extinct.getBoundaryPixels().empty() && !extinct.isAtWar() && extinct.getEnemies().empty()) {
        return; // Already processed
    }
    if (extinct.getPopulation() > 0 && !extinct.getBoundaryPixels().empty()) {
        const bool hasCities = !extinct.getCities().empty();
        if (hasCities || extinct.getPopulation() >= 2000) {
            return;
        }
    }

    const int extinctId = extinct.getCountryIndex();
    if (extinctId < 0) {
        return;
    }

    const std::vector<sf::Vector2i> territory = extinct.getTerritoryVec();
    if (!territory.empty()) {
        std::lock_guard<std::mutex> lock(m_gridMutex);
        const int height = static_cast<int>(m_countryGrid.size());
        const int width = height > 0 ? static_cast<int>(m_countryGrid[0].size()) : 0;
        const int regionsPerRow = m_regionSize > 0 ? (width / m_regionSize) : 0;

        for (const auto& cell : territory) {
            if (cell.x < 0 || cell.y < 0 || cell.x >= width || cell.y >= height) {
                continue;
            }
            if (m_countryGrid[cell.y][cell.x] != extinctId) {
                continue;
            }

            setCountryOwnerAssumingLockedImpl(cell.x, cell.y, -1);
            if (regionsPerRow > 0) {
                int regionIndex = (cell.y / m_regionSize) * regionsPerRow + (cell.x / m_regionSize);
                m_dirtyRegions.insert(regionIndex);
            }
        }
    }

    // Remove from wars/enemy lists without invalidating pointers.
    for (auto& other : countries) {
        if (&other == &extinct) {
            continue;
        }
        if (!other.getEnemies().empty()) {
            other.removeEnemy(&extinct);
            if (other.isAtWar() && other.getEnemies().empty()) {
                other.clearWarState();
            }
        }
    }

    // Clear local state.
    extinct.clearWarState();
    extinct.clearEnemies();
    extinct.setTerritory(std::unordered_set<sf::Vector2i>{});
    extinct.setCities(std::vector<City>{});
    extinct.clearRoadNetwork();
    extinct.setGold(0.0);
    extinct.setSciencePoints(0.0);
    extinct.setPopulation(0);

    std::string event = "💀 " + extinct.getName() + " collapses and becomes extinct in " + std::to_string(currentYear);
    if (currentYear < 0) event += " BCE";
    else event += " CE";
    news.addEvent(event);
}

void Map::processPoliticalEvents(std::vector<Country>& countries,
                                 TradeManager& tradeManager,
                                 int currentYear,
                                 News& news,
                                 TechnologyManager& techManager,
                                 CultureManager& cultureManager,
                                 int dtYears) {
    const int years = std::max(1, dtYears);
    if (years > 1) {
        const int startYear = currentYear - years + 1;
        for (int y = startYear; y <= currentYear; ++y) {
            processPoliticalEvents(countries, tradeManager, y, news, techManager, cultureManager, 1);
        }
        return;
    }

    if (countries.empty()) {
        return;
    }

    // Phase 2: rule-driven fragmentation + tag replacement (pressure/control driven).
    std::mt19937_64& rng = m_ctx->worldRng;

    auto clamp01 = [](double v) {
        return std::max(0.0, std::min(1.0, v));
    };
    auto recordLegitimacyEventDelta = [&](Country& c, double beforeLegitimacy, int splitInc, int tagReplacementInc) {
        auto& ldbg = c.getMacroEconomyMutable().legitimacyDebug;
        const double afterLegitimacy = clamp01(c.getLegitimacy());
        ldbg.dbg_legit_delta_events += (afterLegitimacy - beforeLegitimacy);
        if (splitInc > 0) {
            ldbg.dbg_legit_event_splits += splitInc;
        }
        if (tagReplacementInc > 0) {
            ldbg.dbg_legit_event_tag_replacements += tagReplacementInc;
        }
        if (afterLegitimacy <= 0.0 && beforeLegitimacy > 0.0) {
            ldbg.dbg_legit_clamp_to_zero_events++;
        }
    };

    auto endsWith = [](const std::string& s, const std::string& suffix) -> bool {
        return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
    };

    auto stripSuffix = [&](const std::string& s, const std::string& suffix) -> std::string {
        if (!endsWith(s, suffix)) return s;
        return s.substr(0, s.size() - suffix.size());
    };

    const int maxCountries = 450;
    const int minTerritoryPixels = 240;
    const long long minPopulation = 30000;
    const int fragmentationCooldownYears = 220;
    const int autonomyBreakYears = 35;
    const int localCenterMax = 8;
    int autonomyDt = 1;
    if (m_lastLocalAutonomyUpdateYear > -9'999'000) {
        autonomyDt = std::max(1, currentYear - m_lastLocalAutonomyUpdateYear);
    }
    m_lastLocalAutonomyUpdateYear = currentYear;

    auto famineStress = [&](int idx) -> double {
        if (idx < 0) return 0.0;
        const double foodSum = std::max(0.0, getCountryFoodSum(idx));
        const double K = foodSum * 1200.0;
        const double pop = std::max(0.0, static_cast<double>(countries[static_cast<size_t>(idx)].getPopulation()));
        if (K <= 1.0) return 1.0;
        return clamp01((pop - K) / K);
    };

    auto revoltRisk = [&](const Country& c, int idx) -> double {
        const double control = clamp01(c.getAvgControl());
        const double legit = clamp01(c.getLegitimacy());
        const double taxes = clamp01(c.getTaxRate());
        const double famine = famineStress(idx);
        const double war = c.isAtWar() ? 1.0 : 0.0;

        double r = 0.0;
        r += (1.0 - control) * 0.45;
        r += (1.0 - legit) * 0.30;
        r += std::max(0.0, taxes - 0.14) * 0.55;
        r += famine * 0.25;
        r += war * 0.10;
        return clamp01(r);
    };

    auto pickSeedAField = [&](int countryIndex, int capFx, int capFy) -> sf::Vector2i {
        sf::Vector2i best(capFx, capFy);
        float bestC = -1.0f;
        const int R = 3;
        for (int dy = -R; dy <= R; ++dy) {
            for (int dx = -R; dx <= R; ++dx) {
                const int fx = capFx + dx;
                const int fy = capFy + dy;
                if (fx < 0 || fy < 0 || fx >= m_fieldW || fy >= m_fieldH) continue;
                const size_t idx = static_cast<size_t>(fy) * static_cast<size_t>(m_fieldW) + static_cast<size_t>(fx);
                if (idx >= m_fieldOwnerId.size() || idx >= m_fieldControl.size()) continue;
                if (m_fieldOwnerId[idx] != countryIndex) continue;
                const float c = m_fieldControl[idx];
                if (c > bestC) {
                    bestC = c;
                    best = sf::Vector2i(fx, fy);
                }
            }
        }
        return best;
    };

    auto pickBestCellByControl = [&](int countryIndex, const std::unordered_set<sf::Vector2i>& group) -> sf::Vector2i {
        sf::Vector2i best(-1, -1);
        float bestC = -1.0f;
        for (const auto& cell : group) {
            const int fx = std::max(0, std::min(m_fieldW - 1, cell.x / kFieldCellSize));
            const int fy = std::max(0, std::min(m_fieldH - 1, cell.y / kFieldCellSize));
            const size_t idx = static_cast<size_t>(fy) * static_cast<size_t>(m_fieldW) + static_cast<size_t>(fx);
            if (idx >= m_fieldOwnerId.size() || idx >= m_fieldControl.size()) {
                continue;
            }
            if (m_fieldOwnerId[idx] != countryIndex) {
                continue;
            }
            const float c = m_fieldControl[idx];
            if (c > bestC) {
                bestC = c;
                best = cell;
            } else if (c == bestC && best.x >= 0) {
                if (cell.y < best.y || (cell.y == best.y && cell.x < best.x)) {
                    best = cell;
                }
            }
        }
        if (best.x >= 0) {
            return best;
        }
        // Fallback: deterministic coordinate order, independent of unordered_set iteration.
        for (const auto& cell : group) {
            if (best.x < 0 || cell.y < best.y || (cell.y == best.y && cell.x < best.x)) {
                best = cell;
            }
        }
        return best;
    };

    auto centerKey = [](int countryIndex, int fieldIdx) -> std::uint64_t {
        const std::uint64_t hi = static_cast<std::uint64_t>(static_cast<std::uint32_t>(countryIndex + 1));
        const std::uint64_t lo = static_cast<std::uint64_t>(static_cast<std::uint32_t>(fieldIdx + 1));
        return (hi << 32) ^ lo;
    };

    auto lookupTravelTime = [&](int countryIndex, int fieldIdx) -> double {
        if (countryIndex < 0 || countryIndex >= static_cast<int>(m_countryControlCache.size())) {
            return std::numeric_limits<double>::infinity();
        }
        const CountryControlCache& cache = m_countryControlCache[static_cast<size_t>(countryIndex)];
        const size_t sz = std::min(cache.fieldIndices.size(), cache.travelTimes.size());
        for (size_t k = 0; k < sz; ++k) {
            if (cache.fieldIndices[k] == fieldIdx) {
                return static_cast<double>(cache.travelTimes[k]);
            }
        }
        return std::numeric_limits<double>::infinity();
    };

    struct LocalCenterCandidate {
        sf::Vector2i seedField = sf::Vector2i(-1, -1);
        double pressure = 0.0;
        int overYears = 0;
    };

    std::unordered_set<std::uint64_t> seenLocalCenters;
    seenLocalCenters.reserve(countries.size() * 4u);

    auto scoreAndTrackLocalCenters = [&](int countryIndex, LocalCenterCandidate& bestOut) {
        bestOut = LocalCenterCandidate{};
        if (countryIndex < 0 || countryIndex >= static_cast<int>(countries.size())) return;
        const Country& c = countries[static_cast<size_t>(countryIndex)];
        if (c.getPopulation() <= 0) return;

        struct CenterSeed {
            double pop = 0.0;
            int field = -1;
            int y = 0;
            int x = 0;
        };
        std::vector<CenterSeed> seeds;
        seeds.reserve(c.getCities().size() + 2);
        for (const auto& city : c.getCities()) {
            const int fx = std::max(0, std::min(m_fieldW - 1, city.getLocation().x / kFieldCellSize));
            const int fy = std::max(0, std::min(m_fieldH - 1, city.getLocation().y / kFieldCellSize));
            const int fi = fy * m_fieldW + fx;
            if (fi < 0 || static_cast<size_t>(fi) >= m_fieldOwnerId.size()) continue;
            if (m_fieldOwnerId[static_cast<size_t>(fi)] != countryIndex) continue;
            seeds.push_back(CenterSeed{city.getPopulation(), fi, fy, fx});
        }

        if (seeds.empty() && countryIndex >= 0 && countryIndex < static_cast<int>(m_countryControlCache.size())) {
            const CountryControlCache& cache = m_countryControlCache[static_cast<size_t>(countryIndex)];
            const size_t sz = std::min(cache.fieldIndices.size(), cache.travelTimes.size());
            float bestT = -1.0f;
            int bestField = -1;
            for (size_t k = 0; k < sz; ++k) {
                const int fi = cache.fieldIndices[k];
                if (fi < 0 || static_cast<size_t>(fi) >= m_fieldOwnerId.size()) continue;
                if (m_fieldOwnerId[static_cast<size_t>(fi)] != countryIndex) continue;
                const float tt = cache.travelTimes[k];
                if (!std::isfinite(tt)) continue;
                if (tt > bestT) {
                    bestT = tt;
                    bestField = fi;
                }
            }
            if (bestField >= 0) {
                const int fx = bestField % m_fieldW;
                const int fy = bestField / m_fieldW;
                seeds.push_back(CenterSeed{0.0, bestField, fy, fx});
            }
        }
        if (seeds.empty()) return;

        std::sort(seeds.begin(), seeds.end(), [](const CenterSeed& a, const CenterSeed& b) {
            if (a.pop != b.pop) return a.pop > b.pop;
            if (a.y != b.y) return a.y < b.y;
            return a.x < b.x;
        });
        seeds.erase(std::unique(seeds.begin(), seeds.end(), [](const CenterSeed& a, const CenterSeed& b) {
            return a.field == b.field;
        }), seeds.end());
        if (static_cast<int>(seeds.size()) > localCenterMax) {
            seeds.resize(static_cast<size_t>(localCenterMax));
        }

        const sf::Vector2i capPx = c.getCapitalLocation();
        const int capFx = std::max(0, std::min(m_fieldW - 1, capPx.x / kFieldCellSize));
        const int capFy = std::max(0, std::min(m_fieldH - 1, capPx.y / kFieldCellSize));
        const double legitimacy = clamp01(c.getLegitimacy());
        const double inequality = clamp01(c.getInequality());
        const double stability = clamp01(c.getStability());
        const double realWage = clamp01(c.getRealWage() / 2.0);
        const double adminShare = clamp01(c.getAdminSpendingShare());
        const double infraShare = clamp01(c.getInfraSpendingShare());
        const double humanCapital = clamp01(c.getMacroEconomy().humanCapital);
        const double extraction = clamp01(c.getTaxRate() * (0.60 + 0.40 * inequality));

        double bestScore = -1.0;
        for (const CenterSeed& s : seeds) {
            const std::uint64_t key = centerKey(countryIndex, s.field);
            seenLocalCenters.insert(key);
            LocalAutonomyState& state = m_localAutonomyByCenter[key];

            const double localControl = clamp01((static_cast<size_t>(s.field) < m_fieldControl.size()) ? m_fieldControl[static_cast<size_t>(s.field)] : c.getAvgControl());
            const double travelTime = lookupTravelTime(countryIndex, s.field);
            const double travelNorm = std::isfinite(travelTime) ? clamp01(travelTime / 28.0) : 1.0;
            const int capDist = std::abs(s.x - capFx) + std::abs(s.y - capFy);
            const double capDistNorm = clamp01(static_cast<double>(capDist) / 24.0);

            const double up =
                0.30 * travelNorm +
                0.16 * extraction +
                0.16 * (1.0 - legitimacy) +
                0.14 * inequality +
                0.11 * (1.0 - localControl) +
                0.08 * (1.0 - stability) +
                0.05 * (c.isAtWar() ? 1.0 : 0.0);
            const double down =
                0.33 * adminShare +
                0.24 * infraShare +
                0.20 * realWage +
                0.15 * humanCapital +
                0.08 * stability;
            state.pressure = clamp01(state.pressure + static_cast<double>(autonomyDt) * (0.080 * up - 0.055 * down));
            if (state.pressure > 0.74) {
                state.overYears += autonomyDt;
            } else {
                state.overYears = std::max(0, state.overYears - autonomyDt);
            }

            const double score =
                state.pressure *
                (0.30 + 0.70 * capDistNorm) *
                (0.40 + 0.60 * travelNorm) *
                (0.45 + 0.55 * (1.0 - localControl));
            if (score > bestScore ||
                (score == bestScore &&
                 (s.y < bestOut.seedField.y || (s.y == bestOut.seedField.y && s.x < bestOut.seedField.x)))) {
                bestScore = score;
                bestOut.seedField = sf::Vector2i(s.x, s.y);
                bestOut.pressure = state.pressure;
                bestOut.overYears = state.overYears;
            }
        }
    };

    auto pickSeedBField = [&](int countryIndex, int capFx, int capFy, int preferredField) -> sf::Vector2i {
        if (preferredField >= 0 && static_cast<size_t>(preferredField) < m_fieldOwnerId.size() &&
            m_fieldOwnerId[static_cast<size_t>(preferredField)] == countryIndex) {
            return sf::Vector2i(preferredField % m_fieldW, preferredField / m_fieldW);
        }

        if (countryIndex >= 0 && countryIndex < static_cast<int>(m_countryControlCache.size())) {
            const CountryControlCache& cache = m_countryControlCache[static_cast<size_t>(countryIndex)];
            const size_t sz = std::min(cache.fieldIndices.size(), cache.travelTimes.size());
            int bestField = -1;
            double bestScore = -1.0;
            for (size_t k = 0; k < sz; ++k) {
                const int fi = cache.fieldIndices[k];
                if (fi < 0 || static_cast<size_t>(fi) >= m_fieldOwnerId.size() || static_cast<size_t>(fi) >= m_fieldControl.size()) continue;
                if (m_fieldOwnerId[static_cast<size_t>(fi)] != countryIndex) continue;
                const double travel = static_cast<double>(cache.travelTimes[k]);
                if (!std::isfinite(travel)) continue;
                const double control = clamp01(m_fieldControl[static_cast<size_t>(fi)]);
                const double score = travel * (1.25 - 0.85 * control);
                if (score > bestScore) {
                    bestScore = score;
                    bestField = fi;
                }
            }
            if (bestField >= 0) {
                return sf::Vector2i(bestField % m_fieldW, bestField / m_fieldW);
            }
        }

        sf::Vector2i best(-1, -1);
        double bestScore = -1.0;
        for (int fy = 0; fy < m_fieldH; ++fy) {
            for (int fx = 0; fx < m_fieldW; ++fx) {
                const size_t idx = static_cast<size_t>(fy) * static_cast<size_t>(m_fieldW) + static_cast<size_t>(fx);
                if (idx >= m_fieldOwnerId.size() || idx >= m_fieldControl.size()) continue;
                if (m_fieldOwnerId[idx] != countryIndex) continue;
                const int dist = std::abs(fx - capFx) + std::abs(fy - capFy);
                const double c = clamp01(static_cast<double>(m_fieldControl[idx]));
                const double score = static_cast<double>(dist) * (1.0 - c);
                if (score > bestScore) {
                    bestScore = score;
                    best = sf::Vector2i(fx, fy);
                }
            }
        }
        if (best.x < 0) best = sf::Vector2i(capFx, capFy);
        return best;
    };

    auto trySplitCountry = [&](int countryIndex, double rRisk, const LocalCenterCandidate& localCenter) -> bool {
        if (countryIndex < 0 || countryIndex >= static_cast<int>(countries.size())) return false;
        if (static_cast<int>(countries.size()) >= maxCountries) return false;
        if (countries.size() + 1 > countries.capacity()) return false; // prevent pointer invalidation

        const Country& country = countries[static_cast<size_t>(countryIndex)];
        if (country.getPopulation() < minPopulation) return false;

        const auto& territorySet = country.getBoundaryPixels();
        if (territorySet.size() < static_cast<size_t>(minTerritoryPixels)) return false;

        if (m_fieldW <= 0 || m_fieldH <= 0 || m_fieldOwnerId.empty() || m_fieldControl.empty()) return false;

        const sf::Vector2i capPx = country.getCapitalLocation();
        const int capFx = std::max(0, std::min(m_fieldW - 1, capPx.x / kFieldCellSize));
        const int capFy = std::max(0, std::min(m_fieldH - 1, capPx.y / kFieldCellSize));

        const sf::Vector2i seedA = pickSeedAField(countryIndex, capFx, capFy);
        int preferredSeedField =
            (localCenter.seedField.x >= 0 && localCenter.seedField.y >= 0)
            ? (localCenter.seedField.y * m_fieldW + localCenter.seedField.x)
            : -1;
        if (preferredSeedField == (seedA.y * m_fieldW + seedA.x)) {
            preferredSeedField = -1;
        }
        const sf::Vector2i seedB = pickSeedBField(countryIndex, capFx, capFy, preferredSeedField);
        if (seedA == seedB) return false;

        std::vector<int> ownedFields;
        ownedFields.reserve(static_cast<size_t>(territorySet.size() / std::max(1, kFieldCellSize)) + 64u);
        for (size_t fi = 0; fi < m_fieldOwnerId.size(); ++fi) {
            if (m_fieldOwnerId[fi] == countryIndex) {
                ownedFields.push_back(static_cast<int>(fi));
            }
        }
        if (ownedFields.empty()) return false;

        std::unordered_map<int, int> localByField;
        localByField.reserve(ownedFields.size() * 2u);
        for (size_t li = 0; li < ownedFields.size(); ++li) {
            localByField[ownedFields[li]] = static_cast<int>(li);
        }

        auto runDijkstra = [&](int seedField, std::vector<float>& distOut) -> bool {
            auto itSeed = localByField.find(seedField);
            if (itSeed == localByField.end()) return false;
            distOut.assign(ownedFields.size(), std::numeric_limits<float>::infinity());
            struct Node {
                float dist = 0.0f;
                int field = -1;
            };
            struct NodeCmp {
                bool operator()(const Node& a, const Node& b) const {
                    if (a.dist != b.dist) return a.dist > b.dist;
                    return a.field > b.field;
                }
            };
            std::priority_queue<Node, std::vector<Node>, NodeCmp> pq;
            distOut[static_cast<size_t>(itSeed->second)] = 0.0f;
            pq.push(Node{0.0f, seedField});

            while (!pq.empty()) {
                const Node cur = pq.top();
                pq.pop();
                const auto itCur = localByField.find(cur.field);
                if (itCur == localByField.end()) continue;
                const int curLocal = itCur->second;
                if (curLocal < 0 || static_cast<size_t>(curLocal) >= distOut.size()) continue;
                if (cur.dist > distOut[static_cast<size_t>(curLocal)] + 1e-6f) continue;

                const int fx = cur.field % m_fieldW;
                const int fy = cur.field / m_fieldW;
                const int nxs[4] = {fx + 1, fx - 1, fx, fx};
                const int nys[4] = {fy, fy, fy + 1, fy - 1};
                for (int k = 0; k < 4; ++k) {
                    const int x = nxs[k];
                    const int y = nys[k];
                    if (x < 0 || y < 0 || x >= m_fieldW || y >= m_fieldH) continue;
                    const int nf = y * m_fieldW + x;
                    const auto itNext = localByField.find(nf);
                    if (itNext == localByField.end()) continue;

                    const float c0 = (static_cast<size_t>(cur.field) < m_fieldMoveCost.size()) ? m_fieldMoveCost[static_cast<size_t>(cur.field)] : 1.0f;
                    const float c1 = (static_cast<size_t>(nf) < m_fieldMoveCost.size()) ? m_fieldMoveCost[static_cast<size_t>(nf)] : 1.0f;
                    const float stepCost = std::max(0.08f, 0.5f * (c0 + c1));
                    const float nd = cur.dist + stepCost;
                    const int nextLocal = itNext->second;
                    if (nd + 1e-6f < distOut[static_cast<size_t>(nextLocal)]) {
                        distOut[static_cast<size_t>(nextLocal)] = nd;
                        pq.push(Node{nd, nf});
                    }
                }
            }
            return true;
        };

        const int seedFieldA = seedA.y * m_fieldW + seedA.x;
        const int seedFieldB = seedB.y * m_fieldW + seedB.x;
        std::vector<float> distA;
        std::vector<float> distB;
        if (!runDijkstra(seedFieldA, distA)) return false;
        if (!runDijkstra(seedFieldB, distB)) return false;

        std::unordered_set<sf::Vector2i> groupA;
        std::unordered_set<sf::Vector2i> groupB;
        groupA.reserve(territorySet.size());
        groupB.reserve(territorySet.size() / 2);
        const float rebelBias = static_cast<float>(-0.08 + 0.30 * clamp01(localCenter.pressure));

        for (const auto& cell : territorySet) {
            const int fx = std::max(0, std::min(m_fieldW - 1, cell.x / kFieldCellSize));
            const int fy = std::max(0, std::min(m_fieldH - 1, cell.y / kFieldCellSize));
            const int fi = fy * m_fieldW + fx;
            const auto it = localByField.find(fi);
            if (it == localByField.end()) {
                groupA.insert(cell);
                continue;
            }
            const int li = it->second;
            const float da = distA[static_cast<size_t>(li)];
            const float db = distB[static_cast<size_t>(li)];
            if (std::isfinite(db) && (!std::isfinite(da) || db <= da + rebelBias)) {
                groupB.insert(cell);
            } else {
                groupA.insert(cell);
            }
        }

        const size_t total = groupA.size() + groupB.size();
        if (total == 0 || groupA.empty() || groupB.empty()) return false;
        double ratioB = static_cast<double>(groupB.size()) / static_cast<double>(total);
        if (ratioB < 0.18 || ratioB > 0.82) return false;

        if (groupB.count(capPx) > 0) {
            std::swap(groupA, groupB);
            ratioB = static_cast<double>(groupB.size()) / static_cast<double>(total);
        }
        if (ratioB < 0.18 || ratioB > 0.82) return false;

        const double stress = clamp01(0.60 * rRisk + 0.40 * localCenter.pressure);
        const double lossFrac = std::clamp(0.05 + 0.12 * stress, 0.04, 0.24);
        const long long totalPop = country.getPopulation();
        const long long remainingPop = std::max(0LL, static_cast<long long>(static_cast<double>(totalPop) * (1.0 - lossFrac)));
        const long long newPop = static_cast<long long>(static_cast<double>(remainingPop) * ratioB);
        const long long oldPop = remainingPop - newPop;

        const double totalGold = country.getGold();
        const double remainingGold = std::max(0.0, totalGold * (1.0 - lossFrac));
        const double newGold = remainingGold * ratioB;
        const double oldGold = remainingGold - newGold;

        // Phase 5: science/culture point currencies removed (knowledge + traits/institutions instead).

        std::vector<City> oldCities;
        std::vector<City> newCities;
        for (const auto& city : country.getCities()) {
            if (groupB.count(city.getLocation()) > 0) newCities.push_back(city);
            else oldCities.push_back(city);
        }
        if (newCities.empty() && !groupB.empty()) newCities.emplace_back(pickBestCellByControl(countryIndex, groupB));
        if (oldCities.empty() && !groupA.empty()) oldCities.emplace_back(pickBestCellByControl(countryIndex, groupA));

        const sf::Vector2i newStart = newCities.empty() ? pickBestCellByControl(countryIndex, groupB) : newCities.front().getLocation();
        const sf::Vector2i oldStart = oldCities.empty() ? pickBestCellByControl(countryIndex, groupA) : oldCities.front().getLocation();

        std::vector<sf::Vector2i> oldRoads;
        std::vector<sf::Vector2i> newRoads;
        for (const auto& road : country.getRoads()) {
            if (groupB.count(road) > 0) newRoads.push_back(road);
            else if (groupA.count(road) > 0) oldRoads.push_back(road);
        }

        std::vector<sf::Vector2i> oldFactories;
        std::vector<sf::Vector2i> newFactories;
        for (const auto& factory : country.getFactories()) {
            if (groupB.count(factory) > 0) newFactories.push_back(factory);
            else if (groupA.count(factory) > 0) oldFactories.push_back(factory);
        }

        std::uniform_int_distribution<> colorDist(50, 255);
        const sf::Color newColor(colorDist(rng), colorDist(rng), colorDist(rng));
        std::uniform_real_distribution<> growthRateDist(0.0003, 0.001);
        const double growthRate = growthRateDist(rng);

	        const std::string suffix = country.getCities().empty() ? " Tribe" : " Kingdom";
	        std::string newName;
	        do {
	            newName = generate_country_name(rng) + suffix;
	        } while (isNameTaken(countries, newName));

        const int newIndex = static_cast<int>(countries.size());
        Country newCountry(newIndex, newColor, newStart, newPop, growthRate, newName, country.getType(),
                           m_ctx->seedForCountry(newIndex));
        newCountry.setIdeology(country.getIdeology());
        const double newCountryLegitBefore = clamp01(newCountry.getLegitimacy());
        {
            auto& nldbg = newCountry.getMacroEconomyMutable().legitimacyDebug;
            nldbg.dbg_legit_start = newCountryLegitBefore;
            nldbg.dbg_legit_after_economy = newCountryLegitBefore;
            nldbg.dbg_legit_after_budget = newCountryLegitBefore;
            nldbg.dbg_legit_after_demog = newCountryLegitBefore;
            nldbg.dbg_legit_after_culture = newCountryLegitBefore;
            nldbg.dbg_legit_end = newCountryLegitBefore;
        }
        newCountry.setLegitimacy(std::clamp(0.20 + 0.35 * (1.0 - stress), 0.20, 0.55));
        recordLegitimacyEventDelta(newCountry, newCountryLegitBefore, 1, 0);
        newCountry.setStability(std::clamp(0.28 + 0.35 * (1.0 - stress), 0.20, 0.60));
        newCountry.setAutonomyPressure(std::max(0.30, localCenter.pressure));
        newCountry.setAutonomyOverThresholdYears(std::max(0, localCenter.overYears / 2));
        newCountry.setFragmentationCooldown(fragmentationCooldownYears);
        newCountry.setYearsSinceWar(0);
        newCountry.resetStagnation();
        newCountry.setTerritory(groupB);
        newCountry.setCities(newCities);
        newCountry.setRoads(newRoads);
        newCountry.setFactories(newFactories);
        newCountry.setGold(newGold);
        newCountry.initializeTechSharingTimer(currentYear);

        // Breakaway inherits science state with turmoil-scaled deployment losses.
        const double turmoil = clamp01(0.65 * stress + 0.35 * country.getAutonomyPressure());
        const double knowledgeKeep = std::clamp(0.98 - 0.13 * turmoil, 0.85, 0.98);
        const double infraKeep = std::clamp(0.90 - 0.30 * turmoil, 0.60, 0.90);

        const Country::KnowledgeVec& parentKnowledge = country.getKnowledge();
        Country::KnowledgeVec& childKnowledge = newCountry.getKnowledgeMutable();
        for (size_t d = 0; d < Country::kDomains; ++d) {
            childKnowledge[d] = std::max(0.0, parentKnowledge[d] * knowledgeKeep);
        }
        newCountry.setKnowledgeInfra(country.getKnowledgeInfra() * infraKeep);

        // Use manager assignment so child bonuses are rebuilt correctly.
        techManager.setUnlockedTechnologiesForEditor(newCountry, techManager.getUnlockedTechnologies(country), /*includePrerequisites*/false);

        const Country::MacroEconomyState parentMacroBefore = country.getMacroEconomy();
        const std::array<double, 5> parentCohortsBefore = country.getPopulationCohorts();
        const Country::EpidemicState parentEpiBefore = country.getEpidemicState();

        countries[static_cast<size_t>(countryIndex)].setStartingPixel(oldStart);
        countries[static_cast<size_t>(countryIndex)].setPopulation(oldPop);
        countries[static_cast<size_t>(countryIndex)].setGold(oldGold);
        const double parentLegitBefore = clamp01(countries[static_cast<size_t>(countryIndex)].getLegitimacy());
        countries[static_cast<size_t>(countryIndex)].setLegitimacy(std::max(0.18, countries[static_cast<size_t>(countryIndex)].getLegitimacy() * (0.62 + 0.20 * (1.0 - stress))));
        recordLegitimacyEventDelta(countries[static_cast<size_t>(countryIndex)], parentLegitBefore, 1, 0);
        countries[static_cast<size_t>(countryIndex)].setStability(std::max(0.22, countries[static_cast<size_t>(countryIndex)].getStability() * (0.66 + 0.18 * (1.0 - stress))));
        countries[static_cast<size_t>(countryIndex)].setAutonomyPressure(std::max(0.0, countries[static_cast<size_t>(countryIndex)].getAutonomyPressure() * 0.52));
        countries[static_cast<size_t>(countryIndex)].setAutonomyOverThresholdYears(0);
        countries[static_cast<size_t>(countryIndex)].setFragmentationCooldown(fragmentationCooldownYears);
        countries[static_cast<size_t>(countryIndex)].setYearsSinceWar(0);
        countries[static_cast<size_t>(countryIndex)].resetStagnation();
        countries[static_cast<size_t>(countryIndex)].setTerritory(groupA);
        countries[static_cast<size_t>(countryIndex)].setCities(oldCities);
        countries[static_cast<size_t>(countryIndex)].setRoads(oldRoads);
        countries[static_cast<size_t>(countryIndex)].setFactories(oldFactories);
        countries[static_cast<size_t>(countryIndex)].clearWarState();
        countries[static_cast<size_t>(countryIndex)].clearEnemies();

        countries.push_back(newCountry);

        // Split resources proportionally.
        const double ratio = ratioB;
        for (Resource::Type type : Resource::kAllTypes) {
            const double amount = countries[static_cast<size_t>(countryIndex)].getResourceManager().getResourceAmount(type);
            if (amount <= 0.0) continue;
            const double moved = amount * ratio;
            if (moved <= 0.0) continue;
            const_cast<ResourceManager&>(countries[static_cast<size_t>(countryIndex)].getResourceManager()).consumeResource(type, moved);
            const_cast<ResourceManager&>(countries[static_cast<size_t>(newIndex)].getResourceManager()).addResource(type, moved);
        }

        auto splitStock = [&](double totalValue, double& oldV, double& newV) {
            const double clamped = std::max(0.0, totalValue);
            newV = clamped * ratioB;
            oldV = std::max(0.0, clamped - newV);
        };

        Country::MacroEconomyState oldMacro = parentMacroBefore;
        Country::MacroEconomyState newMacro = parentMacroBefore;
        splitStock(parentMacroBefore.foodStock, oldMacro.foodStock, newMacro.foodStock);
        splitStock(parentMacroBefore.foodStockCap, oldMacro.foodStockCap, newMacro.foodStockCap);
        splitStock(parentMacroBefore.nonFoodStock, oldMacro.nonFoodStock, newMacro.nonFoodStock);
        splitStock(parentMacroBefore.capitalStock, oldMacro.capitalStock, newMacro.capitalStock);
        splitStock(parentMacroBefore.infraStock, oldMacro.infraStock, newMacro.infraStock);
        splitStock(parentMacroBefore.servicesStock, oldMacro.servicesStock, newMacro.servicesStock);
        splitStock(parentMacroBefore.militarySupplyStock, oldMacro.militarySupplyStock, newMacro.militarySupplyStock);
        splitStock(parentMacroBefore.netRevenue, oldMacro.netRevenue, newMacro.netRevenue);

        newMacro.marketAccess *= 0.82;
        newMacro.connectivityIndex *= 0.78;
        newMacro.institutionCapacity = clamp01(parentMacroBefore.institutionCapacity * (0.55 + 0.35 * clamp01(localCenter.pressure)));
        newMacro.compliance = clamp01(parentMacroBefore.compliance * 0.86);
        newMacro.leakageRate = std::clamp(parentMacroBefore.leakageRate + 0.10 + 0.15 * clamp01(localCenter.pressure), 0.02, 0.92);
        newMacro.educationInvestment = clamp01(countries[static_cast<size_t>(newIndex)].getEducationSpendingShare());
        newMacro.rndInvestment = clamp01(countries[static_cast<size_t>(newIndex)].getRndSpendingShare());

        oldMacro.institutionCapacity = clamp01(parentMacroBefore.institutionCapacity * (0.90 - 0.08 * stress));
        oldMacro.compliance = clamp01(parentMacroBefore.compliance * (0.92 - 0.10 * stress));
        oldMacro.leakageRate = std::clamp(parentMacroBefore.leakageRate + 0.06 * stress, 0.02, 0.92);
        oldMacro.educationInvestment = clamp01(countries[static_cast<size_t>(countryIndex)].getEducationSpendingShare());
        oldMacro.rndInvestment = clamp01(countries[static_cast<size_t>(countryIndex)].getRndSpendingShare());

        countries[static_cast<size_t>(countryIndex)].getMacroEconomyMutable() = oldMacro;
        countries[static_cast<size_t>(newIndex)].getMacroEconomyMutable() = newMacro;

        std::array<double, 5> oldCohorts{};
        std::array<double, 5> newCohorts{};
        for (size_t k = 0; k < oldCohorts.size(); ++k) {
            const double v = std::max(0.0, parentCohortsBefore[k]);
            const double moved = v * ratioB;
            newCohorts[k] = moved;
            oldCohorts[k] = std::max(0.0, v - moved);
        }
        countries[static_cast<size_t>(countryIndex)].getPopulationCohortsMutable() = oldCohorts;
        countries[static_cast<size_t>(newIndex)].getPopulationCohortsMutable() = newCohorts;
        countries[static_cast<size_t>(countryIndex)].renormalizePopulationCohortsToTotal();
        countries[static_cast<size_t>(newIndex)].renormalizePopulationCohortsToTotal();
        countries[static_cast<size_t>(countryIndex)].getEpidemicStateMutable() = parentEpiBefore;
        countries[static_cast<size_t>(newIndex)].getEpidemicStateMutable() = parentEpiBefore;

        const int regionsPerRow = static_cast<int>(m_baseImage.getSize().x) / (m_gridCellSize * m_regionSize);
        {
            std::lock_guard<std::mutex> lock(m_gridMutex);
            for (const auto& cell : groupB) {
                setCountryOwnerAssumingLockedImpl(cell.x, cell.y, newIndex);
                if (regionsPerRow > 0) {
                    const int regionIndex = static_cast<int>((cell.y / m_regionSize) * regionsPerRow + (cell.x / m_regionSize));
                    m_dirtyRegions.insert(regionIndex);
                }
            }
        }

        auto clearLocalStatesForCountry = [&](int idx) {
            const std::uint64_t hi = static_cast<std::uint64_t>(static_cast<std::uint32_t>(idx + 1));
            for (auto it = m_localAutonomyByCenter.begin(); it != m_localAutonomyByCenter.end(); ) {
                if ((it->first >> 32) == hi) {
                    it = m_localAutonomyByCenter.erase(it);
                } else {
                    ++it;
                }
            }
        };
        clearLocalStatesForCountry(countryIndex);
        clearLocalStatesForCountry(newIndex);

        news.addEvent("Civil war fractures " + countries[static_cast<size_t>(countryIndex)].getName() + " into a new rival state: " + newName + "!");
        return true;
    };

    bool changedTerritory = false;
    bool controlUpToDate = true; // updateControlGrid was called in updateCountries earlier this year

    auto ensureControlUpToDate = [&]() {
        if (controlUpToDate) {
            return;
        }
        updateControlGrid(countries, currentYear, 1);
        applyPopulationTotalsToCountries(countries);
        controlUpToDate = true;
    };

    std::vector<LocalCenterCandidate> localCenterByCountry(countries.size());
    for (int i = 0; i < static_cast<int>(countries.size()); ++i) {
        scoreAndTrackLocalCenters(i, localCenterByCountry[static_cast<size_t>(i)]);
    }
    for (auto it = m_localAutonomyByCenter.begin(); it != m_localAutonomyByCenter.end(); ) {
        if (seenLocalCenters.count(it->first) == 0) {
            it = m_localAutonomyByCenter.erase(it);
        } else {
            ++it;
        }
    }

    if (currentYear % 5 == 0) {
        struct Candidate {
            int idx = -1;
            double risk = 0.0;
            LocalCenterCandidate localCenter{};
        };
        std::vector<Candidate> cand;
        cand.reserve(countries.size());

        const int n = static_cast<int>(countries.size());
        for (int i = 0; i < n; ++i) {
            const Country& c = countries[static_cast<size_t>(i)];
            if (c.getPopulation() <= 0) continue;
            if (c.getFragmentationCooldown() > 0) continue;
            if (c.getBoundaryPixels().size() < static_cast<size_t>(minTerritoryPixels)) continue;

            const double revolt = revoltRisk(c, i);
            const double autonomy = clamp01(c.getAutonomyPressure());
            const LocalCenterCandidate local = localCenterByCountry[static_cast<size_t>(i)];
            const double r = clamp01(0.45 * revolt + 0.25 * autonomy + 0.30 * local.pressure);
            const bool sustainedAutonomy = c.getAutonomyOverThresholdYears() >= autonomyBreakYears;
            const bool sustainedLocalAutonomy = local.overYears >= autonomyBreakYears;
            if (r < 0.62 && !sustainedAutonomy && !sustainedLocalAutonomy) continue;
            if (c.getAvgControl() > 0.70 && local.pressure < 0.82) continue;
            cand.push_back(Candidate{i, r, local});
        }

        std::sort(cand.begin(), cand.end(), [](const Candidate& a, const Candidate& b) {
            if (a.risk != b.risk) return a.risk > b.risk;
            return a.idx < b.idx;
        });
        int splits = 0;
        for (const auto& c : cand) {
            if (splits >= 2) break;
            if (trySplitCountry(c.idx, c.risk, c.localCenter)) {
                changedTerritory = true;
                controlUpToDate = false;
                splits++;
            }
        }
    }

    if (currentYear % 10 == 0) {
        for (auto& c : countries) {
            if (c.getPopulation() <= 0) continue;
            if (c.isAtWar()) continue;

            const double control = clamp01(c.getAvgControl());
            const double legit = clamp01(c.getLegitimacy());
            if (control < 0.55 || legit > 0.18) continue;

            std::string base = c.getName();
            base = stripSuffix(base, " Tribe");
            base = stripSuffix(base, " Kingdom");
            base = stripSuffix(base, " Empire");
            base = stripSuffix(base, " Republic");

            const std::string suffix = (c.getCities().size() >= 2) ? " Republic" : " Kingdom";
            const std::string next = base + suffix;
            if (next == c.getName()) continue;
            if (isNameTaken(countries, next)) continue;

            c.setName(next);
            const double legitBefore = clamp01(c.getLegitimacy());
            c.setLegitimacy(0.45);
            recordLegitimacyEventDelta(c, legitBefore, 0, 1);
            c.setStability(std::max(0.45, c.getStability()));
            c.setFragmentationCooldown(120);
            if (suffix == " Republic") c.setIdeology(Country::Ideology::Republic);
            else c.setIdeology(Country::Ideology::Kingdom);

            news.addEvent("Regime change: " + base + " undergoes tag replacement and emerges as " + next + ".");
        }
    }

	    // ============================================================
	    // Phase 7: exploration + colonization (bounded sampling, CPU)
	    // ============================================================
	    if (currentYear % 10 == 0 && !m_fieldCoastalLandCandidates.empty()) {
	        attachCountriesForOwnershipSync(&countries);
	        tradeManager.ensureSeaNavPublic(*this);

	        const int height = static_cast<int>(m_countryGrid.size());
	        const int width = (height > 0) ? static_cast<int>(m_countryGrid[0].size()) : 0;

	        auto isCoastalLandPixel = [&](int x, int y) -> bool {
	            if (x < 0 || y < 0 || y >= height || x >= width) return false;
	            if (!m_isLandGrid[static_cast<size_t>(y)][static_cast<size_t>(x)]) return false;
	            static const int dxs[8] = {1, 1, 0, -1, -1, -1, 0, 1};
	            static const int dys[8] = {0, 1, 1, 1, 0, -1, -1, -1};
	            for (int k = 0; k < 8; ++k) {
	                const int nx = x + dxs[k];
	                const int ny = y + dys[k];
	                if (nx < 0 || ny < 0 || ny >= height || nx >= width) continue;
	                if (!m_isLandGrid[static_cast<size_t>(ny)][static_cast<size_t>(nx)]) {
	                    return true;
	                }
	            }
	            return false;
	        };

	        std::uniform_int_distribution<int> pickCandidate(0, static_cast<int>(m_fieldCoastalLandCandidates.size()) - 1);

	        for (int i = 0; i < static_cast<int>(countries.size()); ++i) {
	            Country& c = countries[static_cast<size_t>(i)];
	            if (c.getPopulation() <= 0) continue;

	            auto& ex = c.getExplorationMutable();

	            const double foodSum = std::max(0.0, getCountryFoodSum(i));
	            const double K = std::max(1.0, foodSum * 1200.0);
	            const double pop = static_cast<double>(std::max<long long>(1, c.getPopulation()));
	            const double landPressure = pop / K;

	            const float pressureNow = c.computeColonizationPressure(cultureManager, c.getMarketAccess(), landPressure);
	            ex.explorationDrive = 0.88f * ex.explorationDrive + 0.12f * pressureNow;
	            ex.explorationDrive = std::max(0.0f, std::min(1.0f, ex.explorationDrive));

	            if (!c.canAttemptColonization(techManager, cultureManager)) continue;
	            if (ex.explorationDrive < 0.40f) continue;

	            const int cooldown = std::max(25, std::min(90, static_cast<int>(std::llround(80.0 - 55.0 * ex.explorationDrive))));
	            if (currentYear - ex.lastColonizationYear < cooldown) continue;

	            const auto& ports = c.getPorts();
	            if (ports.empty()) continue;
	            const sf::Vector2i fromPort = ports.front();

	            const double navalRangePx = std::max(120.0, c.computeNavalRangePx(techManager, cultureManager));

	            // Require some fiscal capacity (prevents "infinite free colonization").
	            const double minTreasure = 18.0 + 0.16 * navalRangePx * 0.01;
	            if (c.getGold() < minTreasure) continue;

	            const int samples = std::max(30, std::min(80, 30 + static_cast<int>(std::llround(60.0 * ex.explorationDrive))));
	            double bestScore = -1.0;
	            sf::Vector2i bestPx(-1, -1);
	            float bestSeaLen = 0.0f;

	            for (int s = 0; s < samples; ++s) {
	                const int fidx = m_fieldCoastalLandCandidates[static_cast<size_t>(pickCandidate(rng))];
	                if (fidx < 0) continue;
	                const int fx = fidx % m_fieldW;
	                const int fy = fidx / m_fieldW;
	                if (fx < 0 || fy < 0 || fx >= m_fieldW || fy >= m_fieldH) continue;

	                const size_t fi = static_cast<size_t>(fidx);
	                if (fi >= m_fieldLandMask.size() || m_fieldLandMask[fi] == 0u) continue;
	                if (fi >= m_fieldBiome.size() || fi >= m_fieldFoodYieldMult.size() || fi >= m_fieldFoodPotential.size()) continue;

	                const uint8_t biome = m_fieldBiome[fi];
	                if (biome == 0u || biome == 255u) continue; // Ice/water
	                if (biome == 5u && m_fieldFoodYieldMult[fi] < 0.55f) continue; // deep desert
	                if (m_fieldFoodYieldMult[fi] < 0.40f) continue;
	                if (m_fieldFoodPotential[fi] < 700.0f) continue;

	                // Reject if any land pixel in this field block is already claimed.
	                const int x0 = fx * kFieldCellSize;
	                const int y0 = fy * kFieldCellSize;
	                const int x1 = std::min(width, x0 + kFieldCellSize);
	                const int y1 = std::min(height, y0 + kFieldCellSize);
	                bool anyClaimed = false;
	                for (int y = y0; y < y1 && !anyClaimed; ++y) {
	                    const auto& landRow = m_isLandGrid[static_cast<size_t>(y)];
	                    const auto& ownerRow = m_countryGrid[static_cast<size_t>(y)];
	                    for (int x = x0; x < x1; ++x) {
	                        if (!landRow[static_cast<size_t>(x)]) continue;
	                        if (ownerRow[static_cast<size_t>(x)] != -1) {
	                            anyClaimed = true;
	                            break;
	                        }
	                    }
	                }
	                if (anyClaimed) continue;

	                sf::Vector2i coastPx(-1, -1);
	                for (int y = y0; y < y1 && coastPx.x < 0; ++y) {
	                    const auto& landRow = m_isLandGrid[static_cast<size_t>(y)];
	                    for (int x = x0; x < x1; ++x) {
	                        if (!landRow[static_cast<size_t>(x)]) continue;
	                        if (m_countryGrid[static_cast<size_t>(y)][static_cast<size_t>(x)] != -1) continue;
	                        if (!isCoastalLandPixel(x, y)) continue;
	                        coastPx = sf::Vector2i(x, y);
	                        break;
	                    }
	                }
	                if (coastPx.x < 0) continue;

	                const double dx = static_cast<double>(fromPort.x - coastPx.x);
	                const double dy = static_cast<double>(fromPort.y - coastPx.y);
	                const double heuristic = std::sqrt(dx * dx + dy * dy);
	                if (heuristic > navalRangePx * 1.35) continue;

	                float seaLenPx = 0.0f;
	                if (!tradeManager.findSeaPathLenPx(*this, fromPort, coastPx, seaLenPx)) {
	                    continue;
	                }
	                if (seaLenPx <= 0.0f || static_cast<double>(seaLenPx) > navalRangePx) {
	                    continue;
	                }

	                const double foodTerm = static_cast<double>(m_fieldFoodPotential[fi]) * static_cast<double>(m_fieldFoodYieldMult[fi]);
	                const double seaCost = 1.0 + 0.0045 * static_cast<double>(seaLenPx);
	                const double overstretch = std::max(0.0, std::min(1.0, static_cast<double>(ex.colonialOverstretch)));
	                const double stretchCost = 1.0 + 1.35 * overstretch;
	                const double score = foodTerm / (seaCost * stretchCost);

	                if (score > bestScore) {
	                    bestScore = score;
	                    bestPx = coastPx;
	                    bestSeaLen = seaLenPx;
	                }
	            }

	            if (bestPx.x < 0 || bestScore < 250.0) {
	                continue;
	            }

	            // Costs scale with distance and overstretch; must be affordable.
	            const double overstretch = std::max(0.0, std::min(1.0, static_cast<double>(ex.colonialOverstretch)));
	            const double goldCost = (35.0 + 0.06 * static_cast<double>(bestSeaLen)) * (1.0 + 0.85 * overstretch);
	            if (c.getGold() < goldCost) {
	                continue;
	            }

	            std::vector<int> affected;
	            const int radius = std::max(10, std::min(25, 10 + static_cast<int>(std::llround(12.0 * ex.explorationDrive))));
	            if (!paintCells(i, bestPx, radius, /*erase*/false, /*allowOverwrite*/false, affected)) {
	                continue;
	            }

	            changedTerritory = true;
	            controlUpToDate = false;

	            // Found a foothold city + a port.
	            c.foundCity(bestPx, news);
	            c.forceAddPort(*this, bestPx);

	            // Small colonist transfer (only meaningful in PopulationGrid mode).
	            if (isPopulationGridActive() && !m_fieldPopulation.empty()) {
	                const sf::Vector2i capPx = c.getCapitalLocation();
	                const int capFx = std::max(0, std::min(m_fieldW - 1, capPx.x / kFieldCellSize));
	                const int capFy = std::max(0, std::min(m_fieldH - 1, capPx.y / kFieldCellSize));
	                const int colFx = std::max(0, std::min(m_fieldW - 1, bestPx.x / kFieldCellSize));
	                const int colFy = std::max(0, std::min(m_fieldH - 1, bestPx.y / kFieldCellSize));
	                const size_t capIdx = static_cast<size_t>(capFy) * static_cast<size_t>(m_fieldW) + static_cast<size_t>(capFx);
	                const size_t colIdx = static_cast<size_t>(colFy) * static_cast<size_t>(m_fieldW) + static_cast<size_t>(colFx);

	                const float baseColonists = 1800.0f + 0.0012f * static_cast<float>(std::min<double>(30'000'000.0, pop));
	                const float colonists = std::max(600.0f, std::min(15'000.0f, baseColonists));

	                if (capIdx < m_fieldPopulation.size() && colIdx < m_fieldPopulation.size()) {
	                    const float moved = std::min(colonists, std::max(0.0f, m_fieldPopulation[capIdx] * 0.08f));
	                    m_fieldPopulation[capIdx] = std::max(0.0f, m_fieldPopulation[capIdx] - moved);
	                    m_fieldPopulation[colIdx] += moved;
	                }
	            }

	            // Apply costs and state strain.
	            c.subtractGold(goldCost);
	            const double distFrac = std::min(1.0, static_cast<double>(bestSeaLen) / std::max(1.0, navalRangePx));
	            const double stabHit = 0.008 + 0.020 * distFrac + 0.020 * overstretch;
	            c.setStability(std::max(0.0, c.getStability() - stabHit));

	            ex.lastColonizationYear = currentYear;
	            ex.colonialOverstretch = std::min(1.0f, ex.colonialOverstretch + static_cast<float>(0.06 + 0.12 * distFrac));

	            news.addEvent("🧭 " + c.getName() + " establishes an overseas colony (sea distance: " + std::to_string(static_cast<int>(std::llround(bestSeaLen))) + ").");
	        }
	    }

	    // ============================================================
	    // Phase 7: overseas control penalty + colonial breakaway
	    // ============================================================
	    if (currentYear % 20 == 0 && m_fieldW > 0 && m_fieldH > 0 && !m_fieldOwnerId.empty()) {
	        ensureControlUpToDate();

	        const int fieldN = m_fieldW * m_fieldH;
	        std::vector<uint8_t> visited(static_cast<size_t>(fieldN), 0u);
	        m_fieldOverseasMask.assign(static_cast<size_t>(fieldN), 0u);
	        m_lastOverseasMaskYear = currentYear;

	        std::vector<int> capFx(static_cast<size_t>(countries.size()), 0);
	        std::vector<int> capFy(static_cast<size_t>(countries.size()), 0);
	        for (size_t i = 0; i < countries.size(); ++i) {
	            const sf::Vector2i capPx = countries[i].getCapitalLocation();
	            capFx[i] = std::max(0, std::min(m_fieldW - 1, capPx.x / kFieldCellSize));
	            capFy[i] = std::max(0, std::min(m_fieldH - 1, capPx.y / kFieldCellSize));
	        }

	        std::vector<int> totalOwned(static_cast<size_t>(countries.size()), 0);
	        std::vector<int> overseasOwned(static_cast<size_t>(countries.size()), 0);
	        std::vector<double> overseasControlSum(static_cast<size_t>(countries.size()), 0.0);
	        std::vector<int> largestOverseasStart(static_cast<size_t>(countries.size()), -1);
	        std::vector<int> largestOverseasSize(static_cast<size_t>(countries.size()), 0);

	        std::vector<int> q;
	        q.reserve(4096);

	        auto push = [&](int idx) {
	            visited[static_cast<size_t>(idx)] = 1u;
	            q.push_back(idx);
	        };

	        for (int start = 0; start < fieldN; ++start) {
	            if (visited[static_cast<size_t>(start)] != 0u) continue;
	            const int owner = m_fieldOwnerId[static_cast<size_t>(start)];
	            if (owner < 0 || owner >= static_cast<int>(countries.size())) {
	                continue;
	            }
	            if (countries[static_cast<size_t>(owner)].getPopulation() <= 0) {
	                continue;
	            }

	            q.clear();
	            push(start);

	            int compSize = 0;
	            double compControl = 0.0;
	            bool containsCap = false;
	            std::vector<int> compCells;
	            compCells.reserve(256);

	            for (size_t qi = 0; qi < q.size(); ++qi) {
	                const int cur = q[qi];
	                const int cx = cur % m_fieldW;
	                const int cy = cur / m_fieldW;
	                compCells.push_back(cur);
	                compSize++;
	                if (static_cast<size_t>(cur) < m_fieldControl.size()) {
	                    compControl += static_cast<double>(m_fieldControl[static_cast<size_t>(cur)]);
	                }
	                if (cx == capFx[static_cast<size_t>(owner)] && cy == capFy[static_cast<size_t>(owner)]) {
	                    containsCap = true;
	                }

	                const int nx[4] = {cx + 1, cx - 1, cx, cx};
	                const int ny[4] = {cy, cy, cy + 1, cy - 1};
	                for (int k = 0; k < 4; ++k) {
	                    const int x = nx[k];
	                    const int y = ny[k];
	                    if (x < 0 || y < 0 || x >= m_fieldW || y >= m_fieldH) continue;
	                    const int nidx = y * m_fieldW + x;
	                    if (visited[static_cast<size_t>(nidx)] != 0u) continue;
	                    if (m_fieldOwnerId[static_cast<size_t>(nidx)] != owner) continue;
	                    push(nidx);
	                }
	            }

	            totalOwned[static_cast<size_t>(owner)] += compSize;

	            if (!containsCap) {
	                overseasOwned[static_cast<size_t>(owner)] += compSize;
	                overseasControlSum[static_cast<size_t>(owner)] += compControl;
	                for (int cell : compCells) {
	                    m_fieldOverseasMask[static_cast<size_t>(cell)] = 1u;
	                }
	                if (compSize > largestOverseasSize[static_cast<size_t>(owner)]) {
	                    largestOverseasSize[static_cast<size_t>(owner)] = compSize;
	                    largestOverseasStart[static_cast<size_t>(owner)] = start;
	                }
	            }
	        }

	        auto clamp01d = [](double v) { return std::max(0.0, std::min(1.0, v)); };

	        for (int i = 0; i < static_cast<int>(countries.size()); ++i) {
	            Country& c = countries[static_cast<size_t>(i)];
	            if (c.getPopulation() <= 0) continue;

	            auto& ex = c.getExplorationMutable();
	            const int tot = totalOwned[static_cast<size_t>(i)];
	            const int over = overseasOwned[static_cast<size_t>(i)];
	            if (tot <= 0 || over <= 0) {
	                ex.colonialOverstretch = 0.92f * ex.colonialOverstretch;
	                ex.overseasLowControlYears = std::max(0, ex.overseasLowControlYears - 20);
	                continue;
	            }

	            const double frac = clamp01d(static_cast<double>(over) / static_cast<double>(tot));
	            const double meanControl = overseasControlSum[static_cast<size_t>(i)] / static_cast<double>(std::max(1, over));

	            ex.colonialOverstretch = 0.85f * ex.colonialOverstretch + 0.15f * static_cast<float>(std::min(1.0, frac * 1.25));

	            if (meanControl < 0.22) {
	                ex.overseasLowControlYears += 20;
	            } else {
	                ex.overseasLowControlYears = std::max(0, ex.overseasLowControlYears - 20);
	            }

	            if (frac > 0.12) {
	                const double admin = clamp01d(c.getAdminCapacity());
	                const double debtRatio = c.getDebt() / (std::max(1.0, c.getLastTaxTake()) + 1.0);
	                const double debtPenalty = clamp01d((debtRatio - 1.5) / 4.0);
	                const double stabHit = 0.010 + 0.040 * frac * (1.0 - admin) + 0.015 * debtPenalty;
	                c.setStability(std::max(0.0, c.getStability() - stabHit));
	            }

	            // Breakaway: sustained low-control overseas component in an overstretched state.
	            const bool canSpawn = (largestOverseasStart[static_cast<size_t>(i)] >= 0) &&
	                                  (largestOverseasSize[static_cast<size_t>(i)] >= 14) &&
	                                  (ex.overseasLowControlYears >= 120) &&
	                                  (frac >= 0.18) &&
	                                  !c.isAtWar();
	            if (!canSpawn) continue;
	            if (static_cast<int>(countries.size()) >= maxCountries) continue;
	            if (countries.size() + 1 > countries.capacity()) continue; // avoid pointer invalidation

	            // Rebuild the largest overseas component cells (field indices).
	            const int start = largestOverseasStart[static_cast<size_t>(i)];
	            if (start < 0 || start >= fieldN) continue;
	            if (m_fieldOwnerId[static_cast<size_t>(start)] != i) continue;

	            std::vector<uint8_t> compMark(static_cast<size_t>(fieldN), 0u);
	            std::vector<int> comp;
	            comp.reserve(static_cast<size_t>(largestOverseasSize[static_cast<size_t>(i)]));
	            std::vector<int> qq;
	            qq.reserve(1024);
	            qq.push_back(start);
	            compMark[static_cast<size_t>(start)] = 1u;
	            for (size_t qi = 0; qi < qq.size(); ++qi) {
	                const int cur = qq[qi];
	                comp.push_back(cur);
	                const int cx = cur % m_fieldW;
	                const int cy = cur / m_fieldW;
	                const int nx[4] = {cx + 1, cx - 1, cx, cx};
	                const int ny[4] = {cy, cy, cy + 1, cy - 1};
	                for (int k = 0; k < 4; ++k) {
	                    const int x = nx[k];
	                    const int y = ny[k];
	                    if (x < 0 || y < 0 || x >= m_fieldW || y >= m_fieldH) continue;
	                    const int nidx = y * m_fieldW + x;
	                    if (compMark[static_cast<size_t>(nidx)] != 0u) continue;
	                    if (m_fieldOwnerId[static_cast<size_t>(nidx)] != i) continue;
	                    // Exclude the capital component defensively.
	                    if (x == capFx[static_cast<size_t>(i)] && y == capFy[static_cast<size_t>(i)]) continue;
	                    compMark[static_cast<size_t>(nidx)] = 1u;
	                    qq.push_back(nidx);
	                }
	            }
	            if (static_cast<int>(comp.size()) < 12) {
	                continue;
	            }

	            // Choose a start pixel within this component.
	            const int compFx = comp.front() % m_fieldW;
	            const int compFy = comp.front() / m_fieldW;
	            const int height = static_cast<int>(m_countryGrid.size());
	            const int width = (height > 0) ? static_cast<int>(m_countryGrid[0].size()) : 0;
	            sf::Vector2i newStart(compFx * kFieldCellSize + kFieldCellSize / 2, compFy * kFieldCellSize + kFieldCellSize / 2);
	            if (width > 0 && height > 0) {
	                const int x0 = compFx * kFieldCellSize;
	                const int y0 = compFy * kFieldCellSize;
	                const int x1 = std::min(width, x0 + kFieldCellSize);
	                const int y1 = std::min(height, y0 + kFieldCellSize);
	                for (int y = y0; y < y1; ++y) {
	                    for (int x = x0; x < x1; ++x) {
	                        if (!m_isLandGrid[static_cast<size_t>(y)][static_cast<size_t>(x)]) continue;
	                        if (m_countryGrid[static_cast<size_t>(y)][static_cast<size_t>(x)] != i) continue;
	                        newStart = sf::Vector2i(x, y);
	                        y = y1;
	                        break;
	                    }
	                }
	            }

	            std::uniform_int_distribution<> colorDist(50, 255);
	            const sf::Color newColor(colorDist(rng), colorDist(rng), colorDist(rng));

	            std::string newName;
	            do {
	                newName = generate_country_name(rng) + " Colony";
	            } while (isNameTaken(countries, newName));

	            const int newIndex = static_cast<int>(countries.size());
	            Country newCountry(newIndex, newColor, newStart, /*initialPop*/50000, /*growth*/0.0005, newName,
	                               c.getType(), m_ctx->seedForCountry(newIndex));
	            newCountry.setIdeology(c.getIdeology());
	            newCountry.setStability(std::max(0.30, std::min(0.60, c.getStability())));
	            const double colonyLegitBefore = clamp01(newCountry.getLegitimacy());
	            {
	                auto& nldbg = newCountry.getMacroEconomyMutable().legitimacyDebug;
	                nldbg.dbg_legit_start = colonyLegitBefore;
	                nldbg.dbg_legit_after_economy = colonyLegitBefore;
	                nldbg.dbg_legit_after_budget = colonyLegitBefore;
	                nldbg.dbg_legit_after_demog = colonyLegitBefore;
	                nldbg.dbg_legit_after_culture = colonyLegitBefore;
	                nldbg.dbg_legit_end = colonyLegitBefore;
	            }
	            newCountry.setLegitimacy(std::max(0.20, std::min(0.55, c.getLegitimacy() * 0.90)));
	            recordLegitimacyEventDelta(newCountry, colonyLegitBefore, 0, 0);
	            newCountry.setFragmentationCooldown(180);

	            // Inherit cultural traits; scale knowledge continuity by breakaway turmoil.
	            newCountry.getTraitsMutable() = c.getTraits();
	            const double turmoil = clamp01d(
	                0.40 * static_cast<double>(ex.colonialOverstretch) +
	                0.30 * frac +
	                0.20 * (1.0 - meanControl) +
	                0.10 * clamp01d(c.getAutonomyPressure()));
	            const double knowledgeKeep = std::clamp(0.98 - 0.13 * turmoil, 0.85, 0.98);
	            const double infraKeep = std::clamp(0.90 - 0.30 * turmoil, 0.60, 0.90);
	            const Country::KnowledgeVec& parentKnowledge = c.getKnowledge();
	            Country::KnowledgeVec& childKnowledge = newCountry.getKnowledgeMutable();
	            for (size_t d = 0; d < Country::kDomains; ++d) {
	                childKnowledge[d] = std::max(0.0, parentKnowledge[d] * knowledgeKeep);
	            }
	            newCountry.setKnowledgeInfra(c.getKnowledgeInfra() * infraKeep);

	            // Inherit unlocked techs to avoid instant collapse; keep prereqs already present.
	            techManager.setUnlockedTechnologiesForEditor(newCountry, techManager.getUnlockedTechnologies(c), /*includePrerequisites*/false);

	            // Split cities/ports by field membership.
	            std::vector<City> keptCities;
	            std::vector<City> movedCities;
	            for (const auto& city : c.getCities()) {
	                const int fx = std::max(0, std::min(m_fieldW - 1, city.getLocation().x / kFieldCellSize));
	                const int fy = std::max(0, std::min(m_fieldH - 1, city.getLocation().y / kFieldCellSize));
	                const int idx = fy * m_fieldW + fx;
	                if (idx >= 0 && idx < fieldN && compMark[static_cast<size_t>(idx)] != 0u) {
	                    movedCities.push_back(city);
	                } else {
	                    keptCities.push_back(city);
	                }
	            }
	            if (movedCities.empty()) {
	                movedCities.emplace_back(newStart);
	            }
	            newCountry.setCities(movedCities);
	            c.setCities(keptCities);

	            std::vector<sf::Vector2i> keptPorts;
	            std::vector<sf::Vector2i> movedPorts;
	            for (const auto& p : c.getPorts()) {
	                const int fx = std::max(0, std::min(m_fieldW - 1, p.x / kFieldCellSize));
	                const int fy = std::max(0, std::min(m_fieldH - 1, p.y / kFieldCellSize));
	                const int idx = fy * m_fieldW + fx;
	                if (idx >= 0 && idx < fieldN && compMark[static_cast<size_t>(idx)] != 0u) {
	                    movedPorts.push_back(p);
	                } else {
	                    keptPorts.push_back(p);
	                }
	            }
	            newCountry.setPorts(movedPorts);
	            c.setPorts(keptPorts);

	            countries.push_back(newCountry);

	            // Transfer territory ownership (grid-resolution), bounded to the old country's cells.
	            const int regionsPerRow = static_cast<int>(m_baseImage.getSize().x) / (m_gridCellSize * m_regionSize);
	            const std::vector<sf::Vector2i> territory = c.getTerritoryVec(); // copy (mutated by ownership sync during transfer)
	            {
	                std::lock_guard<std::mutex> lock(m_gridMutex);
	                for (const auto& cell : territory) {
	                    const int fx = cell.x / kFieldCellSize;
	                    const int fy = cell.y / kFieldCellSize;
	                    const int idx = fy * m_fieldW + fx;
	                    if (idx < 0 || idx >= fieldN) continue;
	                    if (compMark[static_cast<size_t>(idx)] == 0u) continue;
	                    setCountryOwnerAssumingLockedImpl(cell.x, cell.y, newIndex);
	                    if (regionsPerRow > 0) {
	                        const int regionIndex = static_cast<int>((cell.y / m_regionSize) * regionsPerRow + (cell.x / m_regionSize));
	                        m_dirtyRegions.insert(regionIndex);
	                    }
	                }
	            }

	            news.addEvent("🏴 Breakaway: an overseas territory of " + c.getName() + " declares independence as " + newName + ".");
	            changedTerritory = true;
	            controlUpToDate = false;
	            ex.overseasLowControlYears = 0;
	        }
	    }

	    if (changedTerritory) {
	        ensureControlUpToDate();
	    }

    for (Country& c : countries) {
        auto& ldbg = c.getMacroEconomyMutable().legitimacyDebug;
        ldbg.dbg_legit_end = clamp01(c.getLegitimacy());
        ldbg.dbg_legit_delta_total = ldbg.dbg_legit_end - ldbg.dbg_legit_start;
    }

#if 0
    std::mt19937_64& gen = m_ctx->worldRng;
    std::uniform_real_distribution<> chanceDist(0.0, 1.0);

    const int maxCountries = 400;
    const int minTerritoryCells = 180;
    const long long minPopulation = 40000;
    const int fragmentationCooldown = 150;

    auto trySplitCountry = [&](int countryIndex) -> bool {
        if (countryIndex < 0 || countryIndex >= static_cast<int>(countries.size())) {
            return false;
        }
        if (static_cast<int>(countries.size()) >= maxCountries) {
            return false;
        }
        if (countries.size() + 1 > countries.capacity()) {
            return false;
        }

        const Country& country = countries[countryIndex];
        if (country.getPopulation() < minPopulation) {
            return false;
        }

        const auto& territorySet = country.getBoundaryPixels();
        if (territorySet.size() < static_cast<size_t>(minTerritoryCells)) {
            return false;
        }

        std::vector<sf::Vector2i> territory(territorySet.begin(), territorySet.end());
        std::unordered_set<sf::Vector2i> groupA;
        std::unordered_set<sf::Vector2i> groupB;

        auto attemptSplit = [&](const sf::Vector2i& seedA, const sf::Vector2i& seedB) -> bool {
            std::unordered_map<sf::Vector2i, int> assignment;
            std::queue<sf::Vector2i> frontier;

            assignment[seedA] = 0;
            assignment[seedB] = 1;
            frontier.push(seedA);
            frontier.push(seedB);

            const int dirs[4][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
            while (!frontier.empty()) {
                sf::Vector2i current = frontier.front();
                frontier.pop();
                int label = assignment[current];

                for (const auto& dir : dirs) {
                    sf::Vector2i next(current.x + dir[0], current.y + dir[1]);
                    if (territorySet.count(next) == 0) {
                        continue;
                    }
                    if (assignment.find(next) != assignment.end()) {
                        continue;
                    }
                    assignment[next] = label;
                    frontier.push(next);
                }
            }

            for (const auto& cell : territorySet) {
                if (assignment.find(cell) != assignment.end()) {
                    continue;
                }
                int distA = std::abs(cell.x - seedA.x) + std::abs(cell.y - seedA.y);
                int distB = std::abs(cell.x - seedB.x) + std::abs(cell.y - seedB.y);
                assignment[cell] = (distA <= distB) ? 0 : 1;
            }

            groupA.clear();
            groupB.clear();
            for (const auto& entry : assignment) {
                if (entry.second == 0) {
                    groupA.insert(entry.first);
                } else {
                    groupB.insert(entry.first);
                }
            }

            size_t total = groupA.size() + groupB.size();
            if (total == 0) {
                return false;
            }
            double ratio = static_cast<double>(groupA.size()) / static_cast<double>(total);
            return ratio >= 0.3 && ratio <= 0.7;
        };

        bool splitBuilt = false;
        for (int attempt = 0; attempt < 4 && !splitBuilt; ++attempt) {
            std::uniform_int_distribution<size_t> seedDist(0, territory.size() - 1);
            sf::Vector2i seedA = territory[seedDist(gen)];
            sf::Vector2i seedB = seedA;
            int bestDist = -1;
            for (const auto& cell : territory) {
                int dist = (cell.x - seedA.x) * (cell.x - seedA.x) + (cell.y - seedA.y) * (cell.y - seedA.y);
                if (dist > bestDist) {
                    bestDist = dist;
                    seedB = cell;
                }
            }
            if (seedA == seedB) {
                continue;
            }
            splitBuilt = attemptSplit(seedA, seedB);
        }

        if (!splitBuilt || groupA.empty() || groupB.empty()) {
            return false;
        }

        sf::Vector2i capital = country.getCapitalLocation();
        if (groupB.count(capital) > 0) {
            std::swap(groupA, groupB);
        }

        double ratio = static_cast<double>(groupB.size()) / static_cast<double>(groupA.size() + groupB.size());
        long long totalPop = country.getPopulation();
        long long populationLoss = static_cast<long long>(totalPop * 0.05);
        long long remainingPop = std::max(0LL, totalPop - populationLoss);
        long long newPop = static_cast<long long>(remainingPop * ratio);
        long long oldPop = remainingPop - newPop;

        double totalGold = country.getGold();
        double goldLoss = totalGold * 0.1;
        double remainingGold = std::max(0.0, totalGold - goldLoss);
        double newGold = remainingGold * ratio;
        double oldGold = remainingGold - newGold;

        // Phase 5: science/culture point currencies removed (knowledge + traits/institutions instead).

        std::vector<City> oldCities;
        std::vector<City> newCities;
        for (const auto& city : country.getCities()) {
            if (groupB.count(city.getLocation()) > 0) {
                newCities.push_back(city);
            } else {
                oldCities.push_back(city);
            }
        }

        if (newCities.empty() && !groupB.empty()) {
            newCities.emplace_back(pickBestCellByControl(countryIndex, groupB));
        }
        if (oldCities.empty() && !groupA.empty()) {
            oldCities.emplace_back(pickBestCellByControl(countryIndex, groupA));
        }

        sf::Vector2i newStart = newCities.empty() ? pickBestCellByControl(countryIndex, groupB) : newCities.front().getLocation();
        sf::Vector2i oldStart = oldCities.empty() ? pickBestCellByControl(countryIndex, groupA) : oldCities.front().getLocation();

        std::vector<sf::Vector2i> oldRoads;
        std::vector<sf::Vector2i> newRoads;
        for (const auto& road : country.getRoads()) {
            if (groupB.count(road) > 0) {
                newRoads.push_back(road);
            } else if (groupA.count(road) > 0) {
                oldRoads.push_back(road);
            }
        }

        std::vector<sf::Vector2i> oldFactories;
        std::vector<sf::Vector2i> newFactories;
        for (const auto& factory : country.getFactories()) {
            if (groupB.count(factory) > 0) {
                newFactories.push_back(factory);
            } else if (groupA.count(factory) > 0) {
                oldFactories.push_back(factory);
            }
        }

        std::uniform_int_distribution<> colorDist(50, 255);
        sf::Color newColor(colorDist(gen), colorDist(gen), colorDist(gen));

        std::uniform_real_distribution<> growthRateDist(0.0003, 0.001);
        double growthRate = growthRateDist(gen);

        std::string newName = generate_country_name(gen);
        while (isNameTaken(countries, newName)) {
            newName = generate_country_name(gen);
        }
        newName += " Republic";

        int newIndex = static_cast<int>(countries.size());
        Country newCountry(newIndex, newColor, newStart, newPop, growthRate, newName, country.getType(),
                           m_ctx->seedForCountry(newIndex));
        newCountry.setIdeology(country.getIdeology());
        newCountry.setStability(0.45);
        newCountry.setFragmentationCooldown(fragmentationCooldown);
        newCountry.setYearsSinceWar(0);
        newCountry.resetStagnation();
        newCountry.setTerritory(groupB);
        newCountry.setCities(newCities);
        newCountry.setRoads(newRoads);
        newCountry.setFactories(newFactories);
        newCountry.setGold(newGold);
        newCountry.setStartingPixel(newStart);

        countries.push_back(newCountry);

        Country& updatedCountry = countries[countryIndex];
        updatedCountry.setPopulation(oldPop);
        updatedCountry.setGold(oldGold);
        updatedCountry.setStability(0.45);
        updatedCountry.setFragmentationCooldown(fragmentationCooldown);
        updatedCountry.setYearsSinceWar(0);
        updatedCountry.resetStagnation();
        updatedCountry.setTerritory(groupA);
        updatedCountry.setCities(oldCities);
        updatedCountry.setRoads(oldRoads);
        updatedCountry.setFactories(oldFactories);
        updatedCountry.setStartingPixel(oldStart);

        const auto& resources = updatedCountry.getResourceManager().getResources();
        for (const auto& entry : resources) {
            Resource::Type type = entry.first;
            double amount = updatedCountry.getResourceManager().getResourceAmount(type);
            double movedAmount = amount * ratio;
            if (movedAmount > 0.0) {
                const_cast<ResourceManager&>(updatedCountry.getResourceManager()).consumeResource(type, movedAmount);
                const_cast<ResourceManager&>(countries[newIndex].getResourceManager()).addResource(type, movedAmount);
            }
        }

        int regionsPerRow = static_cast<int>(m_baseImage.getSize().x) / (m_gridCellSize * m_regionSize);
        {
            std::lock_guard<std::mutex> lock(m_gridMutex);
            for (const auto& cell : groupB) {
                setCountryOwnerAssumingLockedImpl(cell.x, cell.y, newIndex);
                int regionIndex = static_cast<int>((cell.y / m_regionSize) * regionsPerRow + (cell.x / m_regionSize));
                m_dirtyRegions.insert(regionIndex);
            }
        }

        news.addEvent("Civil war fractures " + updatedCountry.getName() + " into a new rival state: " + newName + "!");
        return true;
    };

    if (currentYear % 5 == 0) {
        int candidateCount = static_cast<int>(countries.size());
        int splits = 0;
        for (int i = 0; i < candidateCount && splits < 2; ++i) {
            Country& country = countries[i];
            if (country.getPopulation() <= 0) {
                continue;
            }
            if (!country.isFragmentationReady()) {
                continue;
            }
            bool underStress = country.isAtWar() || (m_plagueActive && isCountryAffectedByPlague(i));
            if (!underStress && country.getStability() > 0.15) {
                continue;
            }
            if (chanceDist(gen) < 0.35 && trySplitCountry(i)) {
                splits++;
            }
        }
    }

    if (currentYear % 50 == 0) {
        std::unordered_set<int> unified;
        int unifications = 0;
        int candidateCount = static_cast<int>(countries.size());
        for (int i = 0; i < candidateCount; ++i) {
            if (unified.count(i) > 0) {
                continue;
            }
            Country& a = countries[i];
            if (a.getPopulation() <= 0 || a.isAtWar() || a.getYearsSinceWar() < 50 || a.getStability() < 0.6) {
                continue;
            }

	            for (int j : getAdjacentCountryIndices(a.getCountryIndex())) {
	                if (j <= i || j < 0 || j >= candidateCount) {
	                    continue;
	                }
	                if (countries[static_cast<size_t>(j)].getCountryIndex() != j) {
	                    continue;
	                }
	                if (unified.count(j) > 0) {
	                    continue;
	                }
	                Country& b = countries[static_cast<size_t>(j)];
	                if (b.getPopulation() <= 0 || b.isAtWar() || b.getYearsSinceWar() < 50 || b.getStability() < 0.6) {
	                    continue;
	                }
	                const int contact = std::max(1, getBorderContactCount(a.getCountryIndex(), b.getCountryIndex()));
	                const double border = std::min(1.0, std::sqrt(static_cast<double>(contact)) / 10.0);
	                const double access = 0.5 * (a.getMarketAccess() + b.getMarketAccess());
	                const double stability = 0.5 * (a.getStability() + b.getStability());
	                const double tradeScore = 10.0 * access + 5.0 * border + 3.0 * stability;
	                if (tradeScore < 6.0) {
	                    continue;
	                }
                if (chanceDist(gen) > 0.25) {
                    continue;
                }

                int leaderIndex = (a.getPopulation() >= b.getPopulation()) ? i : j;
                int absorbedIndex = (leaderIndex == i) ? j : i;
                Country& leader = countries[leaderIndex];
                Country& absorbed = countries[absorbedIndex];

                const std::vector<sf::Vector2i> absorbedTerritory = absorbed.getTerritoryVec();
                std::unordered_set<sf::Vector2i> mergedTerritory = leader.getBoundaryPixels();
                for (const auto& cell : absorbedTerritory) {
                    mergedTerritory.insert(cell);
                }

                std::vector<City> mergedCities = leader.getCities();
                for (const auto& city : absorbed.getCities()) {
                    mergedCities.push_back(city);
                }

                std::vector<sf::Vector2i> mergedRoads = leader.getRoads();
                mergedRoads.insert(mergedRoads.end(), absorbed.getRoads().begin(), absorbed.getRoads().end());

                std::vector<sf::Vector2i> mergedFactories = leader.getFactories();
                mergedFactories.insert(mergedFactories.end(), absorbed.getFactories().begin(), absorbed.getFactories().end());

                leader.setTerritory(mergedTerritory);
                leader.setCities(mergedCities);
                leader.setRoads(mergedRoads);
                leader.setFactories(mergedFactories);

                leader.setPopulation(leader.getPopulation() + absorbed.getPopulation());
                leader.setGold(leader.getGold() + absorbed.getGold());
                leader.setStability(std::max(leader.getStability(), 0.7));
                leader.setFragmentationCooldown(fragmentationCooldown);

                for (Resource::Type type : Resource::kAllTypes) {
                    double amount = absorbed.getResourceManager().getResourceAmount(type);
                    if (amount > 0.0) {
                        const_cast<ResourceManager&>(leader.getResourceManager()).addResource(type, amount);
                        const_cast<ResourceManager&>(absorbed.getResourceManager()).consumeResource(type, amount);
                    }
                }

                int regionsPerRow = static_cast<int>(m_baseImage.getSize().x) / (m_gridCellSize * m_regionSize);
                {
                    std::lock_guard<std::mutex> lock(m_gridMutex);
                    for (const auto& cell : absorbedTerritory) {
                        setCountryOwnerAssumingLockedImpl(cell.x, cell.y, leader.getCountryIndex());
                        int regionIndex = static_cast<int>((cell.y / m_regionSize) * regionsPerRow + (cell.x / m_regionSize));
                        m_dirtyRegions.insert(regionIndex);
                    }
                }

                absorbed.setPopulation(0);
                absorbed.setGold(0.0);
                absorbed.setSciencePoints(0.0);
                absorbed.setCulturePoints(0.0);
                absorbed.setStability(0.0);
                absorbed.setTerritory({});
                absorbed.setCities({});
                absorbed.clearRoadNetwork();
                absorbed.setFactories({});

                news.addEvent("Unification: " + leader.getName() + " peacefully integrates " + absorbed.getName() + ".");

                unified.insert(leaderIndex);
                unified.insert(absorbedIndex);
                unifications++;
                if (unifications >= 1) {
                    return;
                }
            }
        }
    }
#endif
}

void Map::startPlague(int year, News& news) {
    m_plagueActive = true;
    m_plagueStartYear = year;
    m_plagueDeathToll = 0;
    m_plagueAffectedCountries.clear(); // Clear previous plague
    
    news.addEvent("The Great Plague of " + std::to_string(year) + " has started!");

    // Determine the next plague year
    std::uniform_int_distribution<> plagueIntervalDist(600, 700);
    m_plagueInterval = plagueIntervalDist(m_ctx->worldRng);
    m_nextPlagueYear = year + m_plagueInterval;
}

void Map::endPlague(News& news) {
    m_plagueActive = false;
    m_plagueAffectedCountries.clear(); // Clear affected countries
    news.addEvent("The Great Plague has ended. Total deaths: " + std::to_string(m_plagueDeathToll));
}

void Map::rebuildCountryAdjacency(const std::vector<Country>& countries) {
    // Full rebuild (slow path):
    // Scans the entire grid and rebuilds:
    // - `m_countryBorderContactCounts` (symmetric border-contact counts)
    // - `m_countryAdjacencyBits` / `m_countryAdjacency` (derived neighbor sets)
    //
    // The incremental path updates these via `setCountryOwner*()` as territory changes.
    int maxCountryIndex = -1;
    for (const auto& country : countries) {
        maxCountryIndex = std::max(maxCountryIndex, country.getCountryIndex());
    }

    int newSize = maxCountryIndex + 1;
    if (newSize <= 0) {
        m_countryAdjacencySize = 0;
        m_countryAdjacency.clear();
        m_countryAdjacencyBits.clear();
        m_countryBorderContactCounts.clear();
        return;
    }

    m_countryAdjacencySize = newSize;
    m_countryAdjacency.assign(static_cast<size_t>(m_countryAdjacencySize), {});

    const int height = static_cast<int>(m_countryGrid.size());
    if (height <= 0) {
        return;
    }
    const int width = static_cast<int>(m_countryGrid[0].size());
    if (width <= 0) {
        return;
    }

    const int wordCount = (m_countryAdjacencySize + 63) / 64;
    m_countryAdjacencyBits.assign(
        static_cast<size_t>(m_countryAdjacencySize),
        std::vector<std::uint64_t>(static_cast<size_t>(wordCount), 0));
    m_countryBorderContactCounts.assign(
        static_cast<size_t>(m_countryAdjacencySize),
        std::vector<int>(static_cast<size_t>(m_countryAdjacencySize), 0));

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int owner = m_countryGrid[y][x];
            if (owner < 0 || owner >= m_countryAdjacencySize) {
                continue;
            }

            if (x + 1 < width) {
                addBorderContact(owner, m_countryGrid[y][x + 1]);
            }
            if (y + 1 < height) {
                addBorderContact(owner, m_countryGrid[y + 1][x]);
            }
            if (x + 1 < width && y + 1 < height) {
                addBorderContact(owner, m_countryGrid[y + 1][x + 1]);
            }
            if (x - 1 >= 0 && y + 1 < height) {
                addBorderContact(owner, m_countryGrid[y + 1][x - 1]);
            }
        }
    }
}

const std::vector<int>& Map::getAdjacentCountryIndices(int countryIndex) const {
    static const std::vector<int> empty;
    if (countryIndex < 0 || countryIndex >= m_countryAdjacencySize) {
        return empty;
    }
    return m_countryAdjacency[static_cast<size_t>(countryIndex)];
}

const std::vector<int>& Map::getAdjacentCountryIndicesPublic(int countryIndex) const {
    return getAdjacentCountryIndices(countryIndex);
}

int Map::getBorderContactCount(int a, int b) const {
    if (a < 0 || b < 0 || a == b) {
        return 0;
    }
    if (a >= m_countryAdjacencySize || b >= m_countryAdjacencySize) {
        return 0;
    }
    if (static_cast<size_t>(a) >= m_countryBorderContactCounts.size()) {
        return 0;
    }
    if (static_cast<size_t>(b) >= m_countryBorderContactCounts[static_cast<size_t>(a)].size()) {
        return 0;
    }
    return std::max(0, m_countryBorderContactCounts[static_cast<size_t>(a)][static_cast<size_t>(b)]);
}

void Map::ensureAdjacencyStorageForIndex(int countryIndex) {
    if (countryIndex < 0) {
        return;
    }
    if (countryIndex < m_countryAdjacencySize) {
        return;
    }

    const int oldSize = m_countryAdjacencySize;
    const int newSize = countryIndex + 1;
    m_countryAdjacencySize = newSize;

    const int newWordCount = (newSize + 63) / 64;

    m_countryAdjacency.resize(static_cast<size_t>(newSize));

    if (m_countryAdjacencyBits.empty()) {
        m_countryAdjacencyBits.assign(
            static_cast<size_t>(newSize),
            std::vector<std::uint64_t>(static_cast<size_t>(newWordCount), 0));
    } else {
        for (auto& row : m_countryAdjacencyBits) {
            row.resize(static_cast<size_t>(newWordCount), 0);
        }
        m_countryAdjacencyBits.resize(
            static_cast<size_t>(newSize),
            std::vector<std::uint64_t>(static_cast<size_t>(newWordCount), 0));
    }

    if (m_countryBorderContactCounts.empty()) {
        m_countryBorderContactCounts.assign(
            static_cast<size_t>(newSize),
            std::vector<int>(static_cast<size_t>(newSize), 0));
    } else {
        for (auto& row : m_countryBorderContactCounts) {
            row.resize(static_cast<size_t>(newSize), 0);
        }
        m_countryBorderContactCounts.resize(
            static_cast<size_t>(newSize),
            std::vector<int>(static_cast<size_t>(newSize), 0));
    }

    // Existing adjacency lists/bits remain valid; new countries start empty.
    (void)oldSize;
}

void Map::setAdjacencyEdge(int a, int b, bool isNeighbor) {
    if (a < 0 || b < 0 || a == b) {
        return;
    }
    ensureAdjacencyStorageForIndex(std::max(a, b));

    const size_t wordB = static_cast<size_t>(b >> 6);
    const std::uint64_t maskB = 1ull << (b & 63);
    const bool currentlyNeighbor = (m_countryAdjacencyBits[static_cast<size_t>(a)][wordB] & maskB) != 0;

    if (isNeighbor) {
        if (currentlyNeighbor) {
            return;
        }
        m_countryAdjacencyBits[static_cast<size_t>(a)][wordB] |= maskB;

        const size_t wordA = static_cast<size_t>(a >> 6);
        const std::uint64_t maskA = 1ull << (a & 63);
        m_countryAdjacencyBits[static_cast<size_t>(b)][wordA] |= maskA;

        m_countryAdjacency[static_cast<size_t>(a)].push_back(b);
        m_countryAdjacency[static_cast<size_t>(b)].push_back(a);
        return;
    }

    if (!currentlyNeighbor) {
        return;
    }
    m_countryAdjacencyBits[static_cast<size_t>(a)][wordB] &= ~maskB;

    const size_t wordA = static_cast<size_t>(a >> 6);
    const std::uint64_t maskA = 1ull << (a & 63);
    m_countryAdjacencyBits[static_cast<size_t>(b)][wordA] &= ~maskA;

    auto& aNeighbors = m_countryAdjacency[static_cast<size_t>(a)];
    aNeighbors.erase(std::remove(aNeighbors.begin(), aNeighbors.end(), b), aNeighbors.end());

    auto& bNeighbors = m_countryAdjacency[static_cast<size_t>(b)];
    bNeighbors.erase(std::remove(bNeighbors.begin(), bNeighbors.end(), a), bNeighbors.end());
}

void Map::addBorderContact(int a, int b) {
    if (a < 0 || b < 0 || a == b) {
        return;
    }
    ensureAdjacencyStorageForIndex(std::max(a, b));

    int& countAB = m_countryBorderContactCounts[static_cast<size_t>(a)][static_cast<size_t>(b)];
    const int before = countAB;
    countAB = before + 1;
    m_countryBorderContactCounts[static_cast<size_t>(b)][static_cast<size_t>(a)] = countAB;

    if (before == 0) {
        setAdjacencyEdge(a, b, true);
    }
}

void Map::removeBorderContact(int a, int b) {
    if (a < 0 || b < 0 || a == b) {
        return;
    }
    if (a >= m_countryAdjacencySize || b >= m_countryAdjacencySize) {
        return;
    }

    int& countAB = m_countryBorderContactCounts[static_cast<size_t>(a)][static_cast<size_t>(b)];
    if (countAB <= 0) {
        return;
    }
    countAB -= 1;
    m_countryBorderContactCounts[static_cast<size_t>(b)][static_cast<size_t>(a)] = countAB;

    if (countAB == 0) {
        setAdjacencyEdge(a, b, false);
    }
}

void Map::initializePlagueCluster(const std::vector<Country>& countries) {
    if (countries.empty()) return;

    // Adjacency is maintained incrementally via `setCountryOwner*()`; no need to full-scan rebuild here.
    int maxCountryIndex = -1;
    for (const auto& country : countries) {
        maxCountryIndex = std::max(maxCountryIndex, country.getCountryIndex());
    }
    ensureAdjacencyStorageForIndex(maxCountryIndex);

    std::vector<int> countryIndexToVectorIndex(static_cast<size_t>(m_countryAdjacencySize), -1);
    for (size_t i = 0; i < countries.size(); ++i) {
        int idx = countries[i].getCountryIndex();
        if (idx >= 0 && idx < m_countryAdjacencySize) {
            countryIndexToVectorIndex[static_cast<size_t>(idx)] = static_cast<int>(i);
        }
    }

	// Select a random country with neighbors as starting point
	std::mt19937_64& gen = m_ctx->worldRng;
	std::vector<int> potentialStarters;
    
    // Find countries that have neighbors (to avoid isolated countries)
    for (size_t i = 0; i < countries.size(); ++i) {
        if (countries[i].getPopulation() <= 0) continue; // Skip dead countries
        
        // Check if this country has any living neighbor
        bool hasNeighbors = false;
        int countryIndex = countries[i].getCountryIndex();
        for (int neighborCountryIndex : getAdjacentCountryIndices(countryIndex)) {
            if (neighborCountryIndex < 0 || neighborCountryIndex >= m_countryAdjacencySize) {
                continue;
            }
            int neighborVecIndex = countryIndexToVectorIndex[static_cast<size_t>(neighborCountryIndex)];
            if (neighborVecIndex >= 0 && neighborVecIndex < static_cast<int>(countries.size()) &&
                countries[static_cast<size_t>(neighborVecIndex)].getPopulation() > 0) {
                hasNeighbors = true;
                break;
            }
        }
        if (hasNeighbors) {
            potentialStarters.push_back(static_cast<int>(i));
        }
    }
    
    if (potentialStarters.empty()) return; // No connected countries
    
    // Select random starting country
    std::uniform_int_distribution<> starterDist(0, static_cast<int>(potentialStarters.size() - 1));
    int startCountry = potentialStarters[starterDist(gen)];
    
    // Start BFS to build connected cluster
    std::queue<int> toProcess;
    std::unordered_set<int> visited;
    
    toProcess.push(startCountry);
    visited.insert(startCountry);
    m_plagueAffectedCountries.insert(startCountry);
    
    // Spread to neighbors with 70% probability each
    std::uniform_real_distribution<> spreadDist(0.0, 1.0);
    
    while (!toProcess.empty()) {
        int currentCountry = toProcess.front();
        toProcess.pop();
        
        int currentCountryIndex = countries[static_cast<size_t>(currentCountry)].getCountryIndex();
        for (int neighborCountryIndex : getAdjacentCountryIndices(currentCountryIndex)) {
            if (neighborCountryIndex < 0 || neighborCountryIndex >= m_countryAdjacencySize) {
                continue;
            }

            int neighborVecIndex = countryIndexToVectorIndex[static_cast<size_t>(neighborCountryIndex)];
            if (neighborVecIndex < 0 || neighborVecIndex >= static_cast<int>(countries.size())) {
                continue;
            }
            if (visited.count(neighborVecIndex) || countries[static_cast<size_t>(neighborVecIndex)].getPopulation() <= 0) {
                continue;
            }

            if (spreadDist(gen) < 0.7) {
                visited.insert(neighborVecIndex);
                m_plagueAffectedCountries.insert(neighborVecIndex);
                toProcess.push(neighborVecIndex);
            }
        }
    }
}

void Map::updatePlagueSpread(const std::vector<Country>& countries) {
    if (!m_plagueActive || m_plagueAffectedCountries.empty()) {
        return;
    }

    rebuildCountryAdjacency(countries);

    std::vector<int> countryIndexToVectorIndex(static_cast<size_t>(m_countryAdjacencySize), -1);
    for (size_t i = 0; i < countries.size(); ++i) {
        int idx = countries[i].getCountryIndex();
        if (idx >= 0 && idx < m_countryAdjacencySize) {
            countryIndexToVectorIndex[static_cast<size_t>(idx)] = static_cast<int>(i);
        }
    }

	std::mt19937_64& gen = m_ctx->worldRng;
	std::uniform_real_distribution<> spreadDist(0.0, 1.0);
	std::uniform_real_distribution<> recoveryDist(0.0, 1.0);

    std::unordered_set<int> nextAffected = m_plagueAffectedCountries;

    for (int countryIndex : m_plagueAffectedCountries) {
        if (countryIndex < 0 || countryIndex >= static_cast<int>(countries.size())) {
            continue;
        }

        if (recoveryDist(gen) < 0.15) {
            nextAffected.erase(countryIndex);
            continue;
        }

        int sourceCountryIndex = countries[static_cast<size_t>(countryIndex)].getCountryIndex();
        for (int neighborCountryIndex : getAdjacentCountryIndices(sourceCountryIndex)) {
            if (neighborCountryIndex < 0 || neighborCountryIndex >= m_countryAdjacencySize) {
                continue;
            }

            int neighborVecIndex = countryIndexToVectorIndex[static_cast<size_t>(neighborCountryIndex)];
            if (neighborVecIndex < 0 || neighborVecIndex >= static_cast<int>(countries.size())) {
                continue;
            }

            if (countries[static_cast<size_t>(neighborVecIndex)].getPopulation() <= 0) {
                continue;
            }
            if (nextAffected.count(neighborVecIndex) > 0) {
                continue;
            }

            if (spreadDist(gen) < 0.35) {
                nextAffected.insert(neighborVecIndex);
            }
        }
    }

    if (!nextAffected.empty()) {
        m_plagueAffectedCountries = std::move(nextAffected);
    }
}

bool Map::isCountryAffectedByPlague(int countryIndex) const {
    return m_plagueAffectedCountries.count(countryIndex) > 0;
}

bool Map::isPlagueActive() const {
    return m_plagueActive;
}

bool Map::areCountryIndicesNeighbors(int countryIndex1, int countryIndex2) const {
    if (countryIndex1 < 0 || countryIndex2 < 0 || countryIndex1 == countryIndex2) {
        return false;
    }
    if (countryIndex1 >= m_countryAdjacencySize || countryIndex2 >= m_countryAdjacencySize) {
        return false;
    }
    if (m_countryAdjacencyBits.empty()) {
        return false;
    }

    const size_t word = static_cast<size_t>(countryIndex2 >> 6);
    const std::uint64_t mask = 1ull << (countryIndex2 & 63);
    return (m_countryAdjacencyBits[static_cast<size_t>(countryIndex1)][word] & mask) != 0;
}

bool Map::areNeighbors(const Country& country1, const Country& country2) const {
    return areCountryIndicesNeighbors(country1.getCountryIndex(), country2.getCountryIndex());
}

int Map::getPlagueStartYear() const {
    return m_plagueStartYear;
}

// MEGA TIME JUMP - SIMULATE THOUSANDS OF YEARS OF HISTORY
bool Map::megaTimeJump(std::vector<Country>& countries, int& currentYear, int targetYear, News& news,
                       TechnologyManager& techManager, CultureManager& cultureManager,
                       EconomyModelCPU& macroEconomy,
                       TradeManager& tradeManager,
                       GreatPeopleManager& greatPeopleManager,
                       std::function<void(int, int, float)> progressCallback,
                       std::function<void(int, int)> chunkCompletedCallback,
                       const std::atomic<bool>* cancelRequested,
                       bool enablePopulationDebugLog,
                       const std::string& populationDebugLogPath) {
    std::cout << "\nBEGINNING MEGA SIMULATION OF HUMAN HISTORY (EXACT YEARLY KERNEL)..." << std::endl;

    const int totalYears = targetYear - currentYear;
    const int startYear = currentYear;
    if (totalYears <= 0) {
        return true;
    }

    bool canceled = false;
    int totalPlagues = 0;
    int totalWarStarts = 0;
    int totalTechBreakthroughs = 0;

    auto startTime = std::chrono::high_resolution_clock::now();
    constexpr int worldSnapshotCadenceYears = 50;

    std::ofstream populationDebugLog;
    if (enablePopulationDebugLog) {
        std::string outPath = populationDebugLogPath.empty()
            ? std::string("mega_time_jump_population_debug.csv")
            : populationDebugLogPath;
        populationDebugLog.open(outPath, std::ios::out | std::ios::trunc);
        if (populationDebugLog.is_open()) {
            populationDebugLog << "year,world_pop_owned,world_food_need_annual,world_food_prod_annual,world_food_imports_qty_annual,"
                                  "world_food_stock,world_food_stock_cap,world_shortage_annual,mean_food_security_popw,"
                                  "mean_nutrition_popw,pop_share_under_0_9,pop_share_under_0_7,pop_share_under_0_5,"
                                  "births_total_annual,deaths_base_total_annual,deaths_famine_total_annual,deaths_epi_total_annual,"
                                  "sum_field_population_all,sum_field_population_owned,sum_field_population_unowned\n";
            populationDebugLog << std::fixed << std::setprecision(3);
            std::cout << "MEGA DEBUG: writing world snapshots to " << outPath << std::endl;
        } else {
            std::cout << "MEGA DEBUG: failed to open log file: " << outPath << std::endl;
        }
    }

    struct WorldFoodSnapshot {
        double worldPopOwned = 0.0;
        double worldFoodNeed = 0.0;
        double worldFoodProd = 0.0;
        double worldFoodImportsQty = 0.0;
        double worldFoodStock = 0.0;
        double worldFoodStockCap = 0.0;
        double worldShortage = 0.0;
        double meanFoodSecurityPopWeighted = 1.0;
        double meanNutritionPopWeighted = 1.0;
        double popShareUnder09 = 0.0;
        double popShareUnder07 = 0.0;
        double popShareUnder05 = 0.0;
        double birthsTotal = 0.0;
        double deathsBaseTotal = 0.0;
        double deathsFamineTotal = 0.0;
        double deathsEpiTotal = 0.0;
        double fieldPopAll = 0.0;
        double fieldPopOwned = 0.0;
        double fieldPopUnowned = 0.0;
    };

    auto clamp01d = [](double v) { return std::max(0.0, std::min(1.0, v)); };

    auto computeWorldFoodSnapshot = [&]() -> WorldFoodSnapshot {
        WorldFoodSnapshot snap;
        double foodSecurityWeighted = 0.0;
        double nutritionWeighted = 0.0;
        double popUnder09 = 0.0;
        double popUnder07 = 0.0;
        double popUnder05 = 0.0;

        for (const Country& c : countries) {
            const long long popLL = c.getPopulation();
            if (popLL <= 0) {
                continue;
            }
            const double pop = static_cast<double>(popLL);
            const auto& cohorts = c.getPopulationCohorts();
            const Country::MacroEconomyState& m = c.getMacroEconomy();

            const double subsistenceFoodNeedAnnual =
                cohorts[0] * 0.00085 +
                cohorts[1] * 0.00100 +
                cohorts[2] * 0.00120 +
                cohorts[3] * 0.00110 +
                cohorts[4] * 0.00095;

            snap.worldPopOwned += pop;
            snap.worldFoodNeed += subsistenceFoodNeedAnnual;
            snap.worldFoodProd += std::max(0.0, m.lastFoodOutput);
            snap.worldFoodImportsQty += std::max(0.0, m.importsValue / std::max(1e-9, m.priceFood));
            snap.worldFoodStock += std::max(0.0, m.foodStock);
            snap.worldFoodStockCap += std::max(0.0, m.foodStockCap);
            snap.worldShortage += std::max(0.0, m.famineSeverity) * subsistenceFoodNeedAnnual;
            snap.birthsTotal += std::max(0.0, m.lastBirths);
            snap.deathsBaseTotal += std::max(0.0, m.lastDeathsBase);
            snap.deathsFamineTotal += std::max(0.0, m.lastDeathsFamine);
            snap.deathsEpiTotal += std::max(0.0, m.lastDeathsEpi);

            const double fs = clamp01d(m.foodSecurity);
            const double nutrition = clamp01d(m.lastAvgNutrition);
            foodSecurityWeighted += fs * pop;
            nutritionWeighted += nutrition * pop;
            if (fs < 0.9) popUnder09 += pop;
            if (fs < 0.7) popUnder07 += pop;
            if (fs < 0.5) popUnder05 += pop;
        }

        const double popDenom = std::max(1.0, snap.worldPopOwned);
        snap.meanFoodSecurityPopWeighted = foodSecurityWeighted / popDenom;
        snap.meanNutritionPopWeighted = nutritionWeighted / popDenom;
        snap.popShareUnder09 = popUnder09 / popDenom;
        snap.popShareUnder07 = popUnder07 / popDenom;
        snap.popShareUnder05 = popUnder05 / popDenom;

        const size_t popSize = m_fieldPopulation.size();
        const size_t ownerSize = m_fieldOwnerId.size();
        for (size_t idx = 0; idx < popSize; ++idx) {
            const double p = std::max(0.0, static_cast<double>(m_fieldPopulation[idx]));
            snap.fieldPopAll += p;
            const int owner = (idx < ownerSize) ? m_fieldOwnerId[idx] : -1;
            if (owner >= 0) {
                snap.fieldPopOwned += p;
            } else {
                snap.fieldPopUnowned += p;
            }
        }

        return snap;
    };

    auto maybeEmitWorldFoodSnapshot = [&](int simYear) {
        if ((simYear % worldSnapshotCadenceYears) != 0) {
            return;
        }

        const WorldFoodSnapshot snap = computeWorldFoodSnapshot();

        std::cout << "[FOOD SNAPSHOT] year=" << simYear
                  << " pop=" << static_cast<long long>(std::llround(snap.worldPopOwned))
                  << " need=" << snap.worldFoodNeed
                  << " prod=" << snap.worldFoodProd
                  << " importsQty=" << snap.worldFoodImportsQty
                  << " stock=" << snap.worldFoodStock
                  << " stockCap=" << snap.worldFoodStockCap
                  << " shortage=" << snap.worldShortage
                  << " fsMean=" << snap.meanFoodSecurityPopWeighted
                  << " nutrMean=" << snap.meanNutritionPopWeighted
                  << " pop<0.9=" << (100.0 * snap.popShareUnder09) << "%"
                  << " pop<0.7=" << (100.0 * snap.popShareUnder07) << "%"
                  << " pop<0.5=" << (100.0 * snap.popShareUnder05) << "%"
                  << std::endl;
        std::cout << "[DEMOGRAPHY SNAPSHOT] year=" << simYear
                  << " births=" << snap.birthsTotal
                  << " deathsBase=" << snap.deathsBaseTotal
                  << " deathsFamine=" << snap.deathsFamineTotal
                  << " deathsEpi=" << snap.deathsEpiTotal
                  << std::endl;

        struct WorstFoodCountry {
            int index = -1;
            double score = 0.0;
            double pop = 0.0;
            double foodSecurity = 1.0;
            double famineSeverity = 0.0;
            double subsistenceFoodNeedAnnual = 0.0;
            double lastFoodOutput = 0.0;
            double importsQtyAnnual = 0.0;
            double foodStock = 0.0;
            double foodStockCap = 0.0;
            double laborFoodShare = 0.0;
            double realWage = 0.0;
            double stability = 1.0;
            double control = 1.0;
        };

        std::vector<WorstFoodCountry> worstCountries;
        worstCountries.reserve(countries.size());
        for (size_t i = 0; i < countries.size(); ++i) {
            const Country& c = countries[i];
            const long long popLL = c.getPopulation();
            if (popLL <= 0) {
                continue;
            }

            const double pop = static_cast<double>(popLL);
            const auto& cohorts = c.getPopulationCohorts();
            const Country::MacroEconomyState& m = c.getMacroEconomy();
            const double subsistenceFoodNeedAnnual =
                cohorts[0] * 0.00085 +
                cohorts[1] * 0.00100 +
                cohorts[2] * 0.00120 +
                cohorts[3] * 0.00110 +
                cohorts[4] * 0.00095;

            WorstFoodCountry row;
            row.index = static_cast<int>(i);
            row.pop = pop;
            row.famineSeverity = std::max(0.0, m.famineSeverity);
            row.score = row.pop * row.famineSeverity;
            row.foodSecurity = clamp01d(m.foodSecurity);
            row.subsistenceFoodNeedAnnual = subsistenceFoodNeedAnnual;
            row.lastFoodOutput = std::max(0.0, m.lastFoodOutput);
            row.importsQtyAnnual = std::max(0.0, m.importsValue / std::max(1e-9, m.priceFood));
            row.foodStock = std::max(0.0, m.foodStock);
            row.foodStockCap = std::max(0.0, m.foodStockCap);
            row.laborFoodShare = clamp01d(m.lastLaborFoodShare);
            row.realWage = m.realWage;
            row.stability = clamp01d(c.getStability());
            row.control = clamp01d(c.getAvgControl());
            worstCountries.push_back(row);
        }

        std::sort(worstCountries.begin(), worstCountries.end(),
                  [](const WorstFoodCountry& a, const WorstFoodCountry& b) {
                      if (a.score != b.score) return a.score > b.score;
                      if (a.pop != b.pop) return a.pop > b.pop;
                      return a.index < b.index;
                  });

        const size_t worstCount = std::min<size_t>(5, worstCountries.size());
        std::cout << "[FOOD WORST5] year=" << simYear << " count=" << worstCount << std::endl;
        for (size_t rank = 0; rank < worstCount; ++rank) {
            const WorstFoodCountry& row = worstCountries[rank];
            const Country& c = countries[static_cast<size_t>(row.index)];
            std::cout << "  #" << (rank + 1)
                      << " " << c.getName()
                      << " (id=" << c.getCountryIndex() << ")"
                      << " pop=" << static_cast<long long>(std::llround(row.pop))
                      << " foodSecurity=" << row.foodSecurity
                      << " famineSeverity=" << row.famineSeverity
                      << " subsistenceNeed=" << row.subsistenceFoodNeedAnnual
                      << " foodOutput=" << row.lastFoodOutput
                      << " importsQty=" << row.importsQtyAnnual
                      << " foodStock=" << row.foodStock
                      << " foodStockCap=" << row.foodStockCap
                      << " laborFoodShare=" << row.laborFoodShare
                      << " realWage=" << row.realWage
                      << " stability=" << row.stability
                      << " control=" << row.control
                      << std::endl;
        }

        struct WorstDemographyCountry {
            int index = -1;
            double score = 0.0;
            double pop = 0.0;
            double c0SharePct = 0.0;
            double c1SharePct = 0.0;
            double c2SharePct = 0.0;
            double c3SharePct = 0.0;
            double c4SharePct = 0.0;
            double fertility = 0.0;
            double nutritionMult = 1.0;
            double stabilityMult = 1.0;
            double wageMult = 1.0;
            double crudeBirthRate = 0.0;
            double crudeDeathRate = 0.0;
            int stagnationYears = 0;
            double stability = 1.0;
        };

        std::vector<WorstDemographyCountry> worstDemography;
        worstDemography.reserve(countries.size());
        for (size_t i = 0; i < countries.size(); ++i) {
            const Country& c = countries[i];
            const long long popLL = c.getPopulation();
            if (popLL <= 0) {
                continue;
            }

            const double pop = static_cast<double>(popLL);
            const Country::MacroEconomyState& m = c.getMacroEconomy();
            const auto& cohorts = c.getPopulationCohorts();
            const double popDen = std::max(1.0, pop);
            const double nutrition = clamp01d(m.lastAvgNutrition);
            const double stability = clamp01d(c.getStability());
            const double wageNorm = clamp01d(m.realWage / 2.0);
            const double nutritionMult = 0.25 + 0.75 * nutrition;
            const double stabilityMult = 0.35 + 0.65 * stability;
            const double wageMult = 0.40 + 0.60 * wageNorm;
            const double fertilityFemaleRate =
                0.20 *
                nutritionMult *
                wageMult *
                (1.0 - 0.50 * clamp01d(m.diseaseBurden)) *
                (c.isAtWar() ? 0.88 : 1.0);

            const double births = std::max(0.0, m.lastBirths);
            const double deathsThisYear = std::max(0.0, m.lastDeathsBase + m.lastDeathsFamine + m.lastDeathsEpi);
            const double crudeBirthRate = births / popDen;
            const double crudeDeathRate = deathsThisYear / popDen;

            WorstDemographyCountry row;
            row.index = static_cast<int>(i);
            row.pop = pop;
            row.c0SharePct = 100.0 * std::max(0.0, cohorts[0]) / popDen;
            row.c1SharePct = 100.0 * std::max(0.0, cohorts[1]) / popDen;
            row.c2SharePct = 100.0 * std::max(0.0, cohorts[2]) / popDen;
            row.c3SharePct = 100.0 * std::max(0.0, cohorts[3]) / popDen;
            row.c4SharePct = 100.0 * std::max(0.0, cohorts[4]) / popDen;
            row.fertility = fertilityFemaleRate;
            row.nutritionMult = nutritionMult;
            row.stabilityMult = stabilityMult;
            row.wageMult = wageMult;
            row.crudeBirthRate = crudeBirthRate;
            row.crudeDeathRate = crudeDeathRate;
            row.stagnationYears = c.getStagnationYears();
            row.stability = stability;
            row.score = pop * std::max(0.0, crudeDeathRate - crudeBirthRate);
            worstDemography.push_back(row);
        }

        std::sort(worstDemography.begin(), worstDemography.end(),
                  [](const WorstDemographyCountry& a, const WorstDemographyCountry& b) {
                      if (a.score != b.score) return a.score > b.score;
                      const double aGap = a.crudeDeathRate - a.crudeBirthRate;
                      const double bGap = b.crudeDeathRate - b.crudeBirthRate;
                      if (aGap != bGap) return aGap > bGap;
                      if (a.pop != b.pop) return a.pop > b.pop;
                      return a.index < b.index;
                  });

        const size_t worstDemoCount = std::min<size_t>(5, worstDemography.size());
        std::cout << "[DEMOGRAPHY WORST5] year=" << simYear << " count=" << worstDemoCount << std::endl;
        for (size_t rank = 0; rank < worstDemoCount; ++rank) {
            const WorstDemographyCountry& row = worstDemography[rank];
            const Country& c = countries[static_cast<size_t>(row.index)];
            std::cout << "  #" << (rank + 1)
                      << " " << c.getName()
                      << " (id=" << c.getCountryIndex() << ")"
                      << " pop=" << static_cast<long long>(std::llround(row.pop))
                      << " c0=" << row.c0SharePct << "%"
                      << " c1=" << row.c1SharePct << "%"
                      << " c2=" << row.c2SharePct << "%"
                      << " c3=" << row.c3SharePct << "%"
                      << " c4=" << row.c4SharePct << "%"
                      << " fertility=" << row.fertility
                      << " nutritionMult=" << row.nutritionMult
                      << " stabilityMult=" << row.stabilityMult
                      << " wageMult=" << row.wageMult
                      << " crudeBirthRate=" << row.crudeBirthRate
                      << " crudeDeathRate=" << row.crudeDeathRate
                      << " stagnationYears=" << row.stagnationYears
                      << " stability=" << row.stability
                      << std::endl;
        }

        struct StabilitySnapshot {
            double popOwned = 0.0;
            double stabMean = 1.0;
            double stabP10 = 1.0;
            double popShareUnder02 = 0.0;
            double popShareUnder04 = 0.0;
            double popShareUnder06 = 0.0;
            double meanDeltaUpdate = 0.0;
            double meanDeltaBudget = 0.0;
            double meanDeltaDemog = 0.0;
            double meanDeltaTotal = 0.0;
            int countStagnationGt20 = 0;
            double meanGrowthRatioUsed = 0.0;
        };

        auto computeStabilitySnapshot = [&]() -> StabilitySnapshot {
            StabilitySnapshot out;
            double wStability = 0.0;
            double wDeltaUpdate = 0.0;
            double wDeltaBudget = 0.0;
            double wDeltaDemog = 0.0;
            double wDeltaTotal = 0.0;
            double wGrowthRatio = 0.0;
            double popUnder02 = 0.0;
            double popUnder04 = 0.0;
            double popUnder06 = 0.0;
            std::vector<std::pair<double, double>> stabPop;
            stabPop.reserve(countries.size());

            for (const Country& c : countries) {
                const long long popLL = c.getPopulation();
                if (popLL <= 0) {
                    continue;
                }
                const double pop = static_cast<double>(popLL);
                const double stab = clamp01d(c.getStability());
                const auto& sd = c.getMacroEconomy().stabilityDebug;

                out.popOwned += pop;
                wStability += stab * pop;
                wDeltaUpdate += sd.dbg_stab_delta_update * pop;
                wDeltaBudget += sd.dbg_stab_delta_budget * pop;
                wDeltaDemog += sd.dbg_stab_delta_demog * pop;
                wDeltaTotal += sd.dbg_stab_delta_total * pop;
                wGrowthRatio += sd.dbg_growthRatio_used * pop;
                if (stab < 0.2) popUnder02 += pop;
                if (stab < 0.4) popUnder04 += pop;
                if (stab < 0.6) popUnder06 += pop;
                if (sd.dbg_stagnationYears > 20) {
                    out.countStagnationGt20++;
                }
                stabPop.emplace_back(stab, pop);
            }

            const double popDen = std::max(1.0, out.popOwned);
            out.stabMean = wStability / popDen;
            out.popShareUnder02 = popUnder02 / popDen;
            out.popShareUnder04 = popUnder04 / popDen;
            out.popShareUnder06 = popUnder06 / popDen;
            out.meanDeltaUpdate = wDeltaUpdate / popDen;
            out.meanDeltaBudget = wDeltaBudget / popDen;
            out.meanDeltaDemog = wDeltaDemog / popDen;
            out.meanDeltaTotal = wDeltaTotal / popDen;
            out.meanGrowthRatioUsed = wGrowthRatio / popDen;

            std::sort(stabPop.begin(), stabPop.end(),
                      [](const std::pair<double, double>& a, const std::pair<double, double>& b) {
                          if (a.first != b.first) return a.first < b.first;
                          return a.second > b.second;
                      });
            double acc = 0.0;
            const double target = 0.10 * out.popOwned;
            out.stabP10 = 1.0;
            for (const auto& v : stabPop) {
                acc += v.second;
                out.stabP10 = v.first;
                if (acc >= target) {
                    break;
                }
            }
            return out;
        };

        const StabilitySnapshot stabilitySnapshot = computeStabilitySnapshot();
        std::cout << "[STABILITY SNAPSHOT] year=" << simYear
                  << " popOwned=" << static_cast<long long>(std::llround(stabilitySnapshot.popOwned))
                  << " stabMean=" << stabilitySnapshot.stabMean
                  << " stabP10=" << stabilitySnapshot.stabP10
                  << " pop<0.2=" << (100.0 * stabilitySnapshot.popShareUnder02) << "%"
                  << " pop<0.4=" << (100.0 * stabilitySnapshot.popShareUnder04) << "%"
                  << " pop<0.6=" << (100.0 * stabilitySnapshot.popShareUnder06) << "%"
                  << " meanDeltaTotal=" << stabilitySnapshot.meanDeltaTotal
                  << " meanDeltaUpdate=" << stabilitySnapshot.meanDeltaUpdate
                  << " meanDeltaBudget=" << stabilitySnapshot.meanDeltaBudget
                  << " meanDeltaDemog=" << stabilitySnapshot.meanDeltaDemog
                  << " countStagn>20=" << stabilitySnapshot.countStagnationGt20
                  << " meanGrowthRatio=" << stabilitySnapshot.meanGrowthRatioUsed
                  << std::endl;

        struct WorstStabilityCountry {
            int index = -1;
            double score = 0.0;
            double pop = 0.0;
            Country::MacroEconomyState::StabilityDebug dbg{};
        };

        std::vector<WorstStabilityCountry> worstStability;
        worstStability.reserve(countries.size());
        for (size_t i = 0; i < countries.size(); ++i) {
            const Country& c = countries[i];
            const long long popLL = c.getPopulation();
            if (popLL <= 0) {
                continue;
            }
            WorstStabilityCountry row;
            row.index = static_cast<int>(i);
            row.pop = static_cast<double>(popLL);
            row.dbg = c.getMacroEconomy().stabilityDebug;
            row.score = row.dbg.dbg_stab_delta_total;
            worstStability.push_back(row);
        }

        std::sort(worstStability.begin(), worstStability.end(),
                  [](const WorstStabilityCountry& a, const WorstStabilityCountry& b) {
                      if (a.score != b.score) return a.score < b.score; // more negative is worse
                      if (a.dbg.dbg_stab_after_demography != b.dbg.dbg_stab_after_demography) {
                          return a.dbg.dbg_stab_after_demography < b.dbg.dbg_stab_after_demography;
                      }
                      if (a.pop != b.pop) return a.pop > b.pop;
                      return a.index < b.index;
                  });

        const size_t worstStabilityCount = std::min<size_t>(5, worstStability.size());
        std::cout << "[STABILITY WORST5] year=" << simYear << " count=" << worstStabilityCount << std::endl;
        for (size_t rank = 0; rank < worstStabilityCount; ++rank) {
            const WorstStabilityCountry& row = worstStability[rank];
            const Country& c = countries[static_cast<size_t>(row.index)];
            const auto& sd = row.dbg;
            std::cout << "  #" << (rank + 1)
                      << " " << c.getName()
                      << " (id=" << c.getCountryIndex() << ")"
                      << " pop=" << static_cast<long long>(std::llround(row.pop))
                      << " stabStart=" << sd.dbg_stab_start_year
                      << " stabAfterUpdate=" << sd.dbg_stab_after_country_update
                      << " stabAfterBudget=" << sd.dbg_stab_after_budget
                      << " stabAfterDemog=" << sd.dbg_stab_after_demography
                      << " deltas(update=" << sd.dbg_stab_delta_update
                      << ", budget=" << sd.dbg_stab_delta_budget
                      << ", demog=" << sd.dbg_stab_delta_demog
                      << ", total=" << sd.dbg_stab_delta_total << ")"
                      << " growthRatio=" << sd.dbg_growthRatio_used
                      << " stagnYears=" << sd.dbg_stagnationYears
                      << " war=" << (sd.dbg_isAtWar ? 1 : 0)
                      << " plague=" << (sd.dbg_plagueAffected ? 1 : 0)
                      << " deltas(war=" << sd.dbg_delta_war
                      << ", plague=" << sd.dbg_delta_plague
                      << ", stagn=" << sd.dbg_delta_stagnation
                      << ", peaceRec=" << sd.dbg_delta_peace_recover << ")"
                      << " deltas(debt=" << sd.dbg_delta_debt_crisis
                      << ", control=" << sd.dbg_delta_control_decay
                      << ", demogStress=" << sd.dbg_delta_demog_stress << ")"
                      << " control=" << sd.dbg_avgControl
                      << " shortage=" << sd.dbg_shortageRatio
                      << " disease=" << sd.dbg_diseaseBurden
                      << " popCountryBeforeUpdate=" << sd.dbg_pop_country_before_update
                      << " popGridOld=" << sd.dbg_pop_grid_oldTotals
                      << " mismatch=" << sd.dbg_pop_mismatch_ratio
                      << std::endl;
        }

        struct LegitimacySnapshot {
            double popOwned = 0.0;
            double legitMean = 1.0;
            double legitP10 = 1.0;
            double popShareUnder02 = 0.0;
            double popShareUnder04 = 0.0;
            double popShareUnder06 = 0.0;
            double meanDeltaEconomy = 0.0;
            double meanDeltaBudget = 0.0;
            double meanDeltaDemog = 0.0;
            double meanDeltaCulture = 0.0;
            double meanDeltaEvents = 0.0;
            double meanDeltaTotal = 0.0;
            int clamp0Economy = 0;
            int clamp0Budget = 0;
            int clamp0Demog = 0;
            int clamp0Events = 0;
        };

        auto computeLegitimacySnapshot = [&]() -> LegitimacySnapshot {
            LegitimacySnapshot out;
            double wLegit = 0.0;
            double wDeltaEconomy = 0.0;
            double wDeltaBudget = 0.0;
            double wDeltaDemog = 0.0;
            double wDeltaCulture = 0.0;
            double wDeltaEvents = 0.0;
            double wDeltaTotal = 0.0;
            double popUnder02 = 0.0;
            double popUnder04 = 0.0;
            double popUnder06 = 0.0;
            std::vector<std::pair<double, double>> legitPop;
            legitPop.reserve(countries.size());

            for (const Country& c : countries) {
                const long long popLL = c.getPopulation();
                if (popLL <= 0) {
                    continue;
                }
                const double pop = static_cast<double>(popLL);
                const auto& ld = c.getMacroEconomy().legitimacyDebug;
                const double legit = clamp01d(ld.dbg_legit_end);

                out.popOwned += pop;
                wLegit += legit * pop;
                wDeltaEconomy += ld.dbg_legit_delta_economy * pop;
                wDeltaBudget += ld.dbg_legit_delta_budget * pop;
                wDeltaDemog += ld.dbg_legit_delta_demog * pop;
                wDeltaCulture += ld.dbg_legit_delta_culture * pop;
                wDeltaEvents += ld.dbg_legit_delta_events * pop;
                wDeltaTotal += ld.dbg_legit_delta_total * pop;
                if (legit < 0.2) popUnder02 += pop;
                if (legit < 0.4) popUnder04 += pop;
                if (legit < 0.6) popUnder06 += pop;
                out.clamp0Economy += ld.dbg_legit_clamp_to_zero_economy;
                out.clamp0Budget += ld.dbg_legit_clamp_to_zero_budget;
                out.clamp0Demog += ld.dbg_legit_clamp_to_zero_demog;
                out.clamp0Events += ld.dbg_legit_clamp_to_zero_events;
                legitPop.emplace_back(legit, pop);
            }

            const double popDen = std::max(1.0, out.popOwned);
            out.legitMean = wLegit / popDen;
            out.popShareUnder02 = popUnder02 / popDen;
            out.popShareUnder04 = popUnder04 / popDen;
            out.popShareUnder06 = popUnder06 / popDen;
            out.meanDeltaEconomy = wDeltaEconomy / popDen;
            out.meanDeltaBudget = wDeltaBudget / popDen;
            out.meanDeltaDemog = wDeltaDemog / popDen;
            out.meanDeltaCulture = wDeltaCulture / popDen;
            out.meanDeltaEvents = wDeltaEvents / popDen;
            out.meanDeltaTotal = wDeltaTotal / popDen;

            std::sort(legitPop.begin(), legitPop.end(),
                      [](const std::pair<double, double>& a, const std::pair<double, double>& b) {
                          if (a.first != b.first) return a.first < b.first;
                          return a.second > b.second;
                      });
            double acc = 0.0;
            const double target = 0.10 * out.popOwned;
            out.legitP10 = 1.0;
            for (const auto& v : legitPop) {
                acc += v.second;
                out.legitP10 = v.first;
                if (acc >= target) {
                    break;
                }
            }
            return out;
        };

        const LegitimacySnapshot legitimacySnapshot = computeLegitimacySnapshot();
        std::cout << "[LEGITIMACY SNAPSHOT] year=" << simYear
                  << " popOwned=" << static_cast<long long>(std::llround(legitimacySnapshot.popOwned))
                  << " legitMean=" << legitimacySnapshot.legitMean
                  << " legitP10=" << legitimacySnapshot.legitP10
                  << " pop<0.2=" << (100.0 * legitimacySnapshot.popShareUnder02) << "%"
                  << " pop<0.4=" << (100.0 * legitimacySnapshot.popShareUnder04) << "%"
                  << " pop<0.6=" << (100.0 * legitimacySnapshot.popShareUnder06) << "%"
                  << " meanDeltaEconomy=" << legitimacySnapshot.meanDeltaEconomy
                  << " meanDeltaBudget=" << legitimacySnapshot.meanDeltaBudget
                  << " meanDeltaDemog=" << legitimacySnapshot.meanDeltaDemog
                  << " meanDeltaCulture=" << legitimacySnapshot.meanDeltaCulture
                  << " meanDeltaEvents=" << legitimacySnapshot.meanDeltaEvents
                  << " meanDeltaTotal=" << legitimacySnapshot.meanDeltaTotal
                  << " clamp0_economy=" << legitimacySnapshot.clamp0Economy
                  << " clamp0_budget=" << legitimacySnapshot.clamp0Budget
                  << " clamp0_demog=" << legitimacySnapshot.clamp0Demog
                  << " clamp0_events=" << legitimacySnapshot.clamp0Events
                  << std::endl;

        struct WorstLegitimacyCountry {
            int index = -1;
            double pop = 0.0;
            Country::MacroEconomyState::LegitimacyDebug dbg{};
        };

        std::vector<WorstLegitimacyCountry> worstLegitimacy;
        worstLegitimacy.reserve(countries.size());
        for (size_t i = 0; i < countries.size(); ++i) {
            const Country& c = countries[i];
            const long long popLL = c.getPopulation();
            if (popLL <= 0) {
                continue;
            }
            WorstLegitimacyCountry row;
            row.index = static_cast<int>(i);
            row.pop = static_cast<double>(popLL);
            row.dbg = c.getMacroEconomy().legitimacyDebug;
            worstLegitimacy.push_back(row);
        }

        std::sort(worstLegitimacy.begin(), worstLegitimacy.end(),
                  [](const WorstLegitimacyCountry& a, const WorstLegitimacyCountry& b) {
                      const double aEnd = a.dbg.dbg_legit_end;
                      const double bEnd = b.dbg.dbg_legit_end;
                      if (aEnd != bEnd) return aEnd < bEnd;
                      if (a.dbg.dbg_legit_delta_total != b.dbg.dbg_legit_delta_total) {
                          return a.dbg.dbg_legit_delta_total < b.dbg.dbg_legit_delta_total;
                      }
                      if (a.pop != b.pop) return a.pop > b.pop;
                      return a.index < b.index;
                  });

        const size_t worstLegitimacyCount = std::min<size_t>(5, worstLegitimacy.size());
        std::cout << "[LEGITIMACY WORST5] year=" << simYear << " count=" << worstLegitimacyCount << std::endl;
        for (size_t rank = 0; rank < worstLegitimacyCount; ++rank) {
            const WorstLegitimacyCountry& row = worstLegitimacy[rank];
            const Country& c = countries[static_cast<size_t>(row.index)];
            const auto& ld = row.dbg;
            std::cout << "  #" << (rank + 1)
                      << " " << c.getName()
                      << " (id=" << c.getCountryIndex() << ")"
                      << " pop=" << static_cast<long long>(std::llround(row.pop))
                      << " legitStart=" << ld.dbg_legit_start
                      << " afterEconomy=" << ld.dbg_legit_after_economy
                      << " afterBudget=" << ld.dbg_legit_after_budget
                      << " afterDemog=" << ld.dbg_legit_after_demog
                      << " afterCulture=" << ld.dbg_legit_after_culture
                      << " end=" << ld.dbg_legit_end
                      << " deltas(economy=" << ld.dbg_legit_delta_economy
                      << ", budget=" << ld.dbg_legit_delta_budget
                      << ", demog=" << ld.dbg_legit_delta_demog
                      << ", culture=" << ld.dbg_legit_delta_culture
                      << ", events=" << ld.dbg_legit_delta_events
                      << ", total=" << ld.dbg_legit_delta_total << ")"
                      << " budget(incomeAnnual=" << ld.dbg_legit_budget_incomeAnnual
                      << ", incomeSafe=" << ld.dbg_legit_budget_incomeSafe
                      << ", desired=" << ld.dbg_legit_budget_desiredBlock
                      << ", actual=" << ld.dbg_legit_budget_actualSpending
                      << ", shortfallStress=" << ld.dbg_legit_budget_shortfallStress
                      << ", taxRateTarget=" << ld.dbg_legit_budget_taxRateTarget
                      << ", taxRateBefore=" << ld.dbg_legit_budget_taxRateBefore
                      << ", taxRateAfter=" << ld.dbg_legit_budget_taxRateAfter
                      << ", taxRateSource=" << ld.dbg_legit_budget_taxRateSource
                      << ", debtToIncome=" << ld.dbg_legit_budget_debtToIncome
                      << ", debtToIncomeRaw=" << ld.dbg_legit_budget_debtToIncomeRaw
                      << ", serviceToIncome=" << ld.dbg_legit_budget_serviceToIncome
                      << ", serviceToIncomeRaw=" << ld.dbg_legit_budget_serviceToIncomeRaw
                      << ", debtRaw=" << ld.dbg_legit_budget_debtEnd
                      << ", debtServiceRaw=" << ld.dbg_legit_budget_debtServiceAnnual
                      << ", ratioOver5=" << (ld.dbg_legit_budget_ratioOver5 ? 1 : 0)
                      << ", taxRate=" << ld.dbg_legit_budget_taxRate
                      << ", control=" << ld.dbg_legit_budget_avgControl
                      << ", stability=" << ld.dbg_legit_budget_stability << ")"
                      << " economy(instCap=" << ld.dbg_legit_econ_instCap
                      << ", wageGain=" << ld.dbg_legit_econ_wageGain
                      << ", famine=" << ld.dbg_legit_econ_famineSeverity
                      << ", ineq=" << ld.dbg_legit_econ_ineq
                      << ", disease=" << ld.dbg_legit_econ_disease << ")"
                      << " budgetLegit(shortfallDirect=" << ld.dbg_legit_budget_shortfall_direct
                      << ", burdenPenalty=" << ld.dbg_legit_budget_burden_penalty
                      << ", debtStress=" << ld.dbg_legit_budget_debtStress
                      << ", serviceStress=" << ld.dbg_legit_budget_serviceStress
                      << ", drift_tax=" << ld.dbg_legit_budget_drift_tax
                      << ", drift_control=" << ld.dbg_legit_budget_drift_control
                      << ", drift_debt=" << ld.dbg_legit_budget_drift_debt
                      << ", drift_service=" << ld.dbg_legit_budget_drift_service
                      << ", drift_shortfall=" << ld.dbg_legit_budget_drift_shortfall
                      << ", drift_plague=" << ld.dbg_legit_budget_drift_plague
                      << ", drift_war=" << ld.dbg_legit_budget_drift_war
                      << ", drift_stability=" << ld.dbg_legit_budget_drift_stability << ")"
                      << " economyLegit(inst=" << ld.dbg_legit_econ_up_inst
                      << ", wage=" << ld.dbg_legit_econ_up_wage
                      << ", famine=" << ld.dbg_legit_econ_down_famine
                      << ", inequality=" << ld.dbg_legit_econ_down_ineq
                      << ", disease=" << ld.dbg_legit_econ_down_disease << ")"
                      << " events(splits=" << ld.dbg_legit_event_splits
                      << ", tagReplacements=" << ld.dbg_legit_event_tag_replacements << ")"
                      << " clamp0(economy=" << ld.dbg_legit_clamp_to_zero_economy
                      << ", budget=" << ld.dbg_legit_clamp_to_zero_budget
                      << ", demog=" << ld.dbg_legit_clamp_to_zero_demog
                      << ", events=" << ld.dbg_legit_clamp_to_zero_events << ")"
                      << std::endl;
        }

        if (populationDebugLog.is_open()) {
            populationDebugLog << simYear
                               << "," << snap.worldPopOwned
                               << "," << snap.worldFoodNeed
                               << "," << snap.worldFoodProd
                               << "," << snap.worldFoodImportsQty
                               << "," << snap.worldFoodStock
                               << "," << snap.worldFoodStockCap
                               << "," << snap.worldShortage
                               << "," << snap.meanFoodSecurityPopWeighted
                               << "," << snap.meanNutritionPopWeighted
                               << "," << snap.popShareUnder09
                               << "," << snap.popShareUnder07
                               << "," << snap.popShareUnder05
                               << "," << snap.birthsTotal
                               << "," << snap.deathsBaseTotal
                               << "," << snap.deathsFamineTotal
                               << "," << snap.deathsEpiTotal
                               << "," << snap.fieldPopAll
                               << "," << snap.fieldPopOwned
                               << "," << snap.fieldPopUnowned
                               << "\n";
        }
    };

    maybeEmitWorldFoodSnapshot(startYear);

    // Chunking is for progress/cancel responsiveness only; simulation itself remains yearly-exact.
    const int megaChunkSize = 100;
    const int totalChunks = (totalYears + megaChunkSize - 1) / megaChunkSize;

    const auto progressInterval = std::chrono::milliseconds(200);
    auto lastProgressReport = std::chrono::steady_clock::now();
    bool reportedProgressOnce = false;

    auto maybeReportProgress = [&](bool force) {
        if (!progressCallback) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (!force && reportedProgressOnce && (now - lastProgressReport) < progressInterval) {
            return;
        }

        float etaSeconds = -1.0f;
        const int doneYears = currentYear - startYear;
        if (totalYears > 0) {
            const float frac = std::clamp(static_cast<float>(doneYears) / static_cast<float>(totalYears), 0.0f, 1.0f);
            if (frac > 0.0001f) {
                const auto nowHr = std::chrono::high_resolution_clock::now();
                const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(nowHr - startTime);
                const double elapsedSeconds = elapsedMs.count() / 1000.0;
                etaSeconds = static_cast<float>(elapsedSeconds * (1.0 / frac - 1.0));
            }
        }

        progressCallback(currentYear, targetYear, etaSeconds);
        lastProgressReport = now;
        reportedProgressOnce = true;
    };

    std::vector<size_t> lastTechCountPerCountry(countries.size(), 0u);
    std::vector<uint8_t> wasAtWar(countries.size(), 0u);
    auto syncPerCountryTracking = [&]() {
        if (lastTechCountPerCountry.size() < countries.size()) {
            const size_t oldSize = lastTechCountPerCountry.size();
            lastTechCountPerCountry.resize(countries.size(), 0u);
            for (size_t i = oldSize; i < countries.size(); ++i) {
                lastTechCountPerCountry[i] = techManager.getUnlockedTechnologies(countries[i]).size();
            }
        }
        if (wasAtWar.size() < countries.size()) {
            const size_t oldSize = wasAtWar.size();
            wasAtWar.resize(countries.size(), 0u);
            for (size_t i = oldSize; i < countries.size(); ++i) {
                wasAtWar[i] = countries[i].isAtWar() ? 1u : 0u;
            }
        }
    };
    for (size_t i = 0; i < countries.size(); ++i) {
        lastTechCountPerCountry[i] = techManager.getUnlockedTechnologies(countries[i]).size();
        wasAtWar[i] = countries[i].isAtWar() ? 1u : 0u;
    }

    std::cout << "MEGA JUMP: " << totalYears << " years in " << totalChunks
              << " progress chunks (" << megaChunkSize << "y each)." << std::endl;

    // Adaptive "quiet period" windows: keep yearly simulation exact, but reduce
    // coordination/progress overhead when the world is calm.
    auto pickAdaptiveWindowYears = [&]() -> int {
        if (m_plagueActive) {
            return 1;
        }

        int activeCountries = 0;
        int atWar = 0;
        int lowStability = 0;
        int autonomyStress = 0;

        for (const Country& c : countries) {
            if (c.getPopulation() <= 0) {
                continue;
            }
            ++activeCountries;
            if (c.isAtWar()) {
                ++atWar;
            }
            if (c.getStability() < 0.35) {
                ++lowStability;
            }
            if (c.getAutonomyPressure() > 0.68 || c.getAutonomyOverThresholdYears() >= 25) {
                ++autonomyStress;
            }
        }

        if (atWar > 0) {
            return 1;
        }
        if (activeCountries <= 0) {
            return 10;
        }
        if ((lowStability * 4) >= activeCountries || (autonomyStress * 5) >= activeCountries) {
            return 5;
        }
        return 10;
    };

    constexpr int progressPollStrideYears = 5;
    int yearsSinceProgressPoll = 0;

    for (int chunkStart = 0; chunkStart < totalYears; chunkStart += megaChunkSize) {
        if (cancelRequested && cancelRequested->load(std::memory_order_relaxed)) {
            canceled = true;
            break;
        }

        const int chunkYears = std::min(megaChunkSize, totalYears - chunkStart);
        maybeReportProgress(false);

        int chunkSimulatedYears = 0;
        while (chunkSimulatedYears < chunkYears) {
            if (cancelRequested && cancelRequested->load(std::memory_order_relaxed)) {
                canceled = true;
                break;
            }

            const int windowYears = std::min(pickAdaptiveWindowYears(), chunkYears - chunkSimulatedYears);
            for (int step = 0; step < windowYears; ++step) {
                if (cancelRequested && cancelRequested->load(std::memory_order_relaxed)) {
                    canceled = true;
                    break;
                }

                currentYear++;
                if (currentYear == 0) currentYear = 1; // skip year 0

                const bool plagueBefore = m_plagueActive;

                // Exact yearly order from normal simulation loop (headless).
                updateCountries(countries, currentYear, news, techManager);
                if (!plagueBefore && m_plagueActive) {
                    totalPlagues++;
                }

                tickWeather(currentYear, 1);
                macroEconomy.tickYear(currentYear, 1, *this, countries, techManager, tradeManager, news);
                tickDemographyAndCities(countries, currentYear, 1, news, &macroEconomy.getLastTradeIntensity());
                techManager.tickYear(countries, *this, &macroEconomy.getLastTradeIntensity(), currentYear, 1);
                cultureManager.tickYear(countries, *this, techManager, &macroEconomy.getLastTradeIntensity(), currentYear, 1, news);
                greatPeopleManager.updateEffects(currentYear, countries, news, 1);
                processPoliticalEvents(countries, tradeManager, currentYear, news, techManager, cultureManager, 1);
                maybeEmitWorldFoodSnapshot(currentYear);

                syncPerCountryTracking();
                for (size_t i = 0; i < countries.size(); ++i) {
                    const size_t currentTechCount = techManager.getUnlockedTechnologies(countries[i]).size();
                    if (currentTechCount > lastTechCountPerCountry[i]) {
                        totalTechBreakthroughs += static_cast<int>(currentTechCount - lastTechCountPerCountry[i]);
                    }
                    lastTechCountPerCountry[i] = currentTechCount;

                    const uint8_t nowAtWar = countries[i].isAtWar() ? 1u : 0u;
                    if (nowAtWar != 0u && wasAtWar[i] == 0u) {
                        totalWarStarts++;
                    }
                    wasAtWar[i] = nowAtWar;
                }

                ++yearsSinceProgressPoll;
                if (yearsSinceProgressPoll >= progressPollStrideYears) {
                    maybeReportProgress(false);
                    yearsSinceProgressPoll = 0;
                }
            }

            if (canceled) {
                break;
            }
            chunkSimulatedYears += windowYears;
            maybeReportProgress(false);
        }

        if (canceled) {
            break;
        }

        if (chunkCompletedCallback) {
            chunkCompletedCallback(currentYear, chunkSimulatedYears);
        }
    }

    maybeReportProgress(true);

    if (populationDebugLog.is_open()) {
        populationDebugLog.flush();
        populationDebugLog.close();
    }

    if (canceled) {
        std::cout << "\nMEGA TIME JUMP CANCELED at year " << currentYear << std::endl;
        return false;
    }

    const auto endTime = std::chrono::high_resolution_clock::now();
    const auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    const double seconds = std::max(0.001, totalDuration.count() / 1000.0);

    long long finalWorldPopulation = 0;
    int survivingCountries = 0;
    for (const auto& country : countries) {
        finalWorldPopulation += country.getPopulation();
        if (country.getPopulation() > 0 && !country.getBoundaryPixels().empty()) {
            survivingCountries++;
        }
    }

    std::cout << "\nMEGA TIME JUMP COMPLETE (exact yearly kernel)." << std::endl;
    std::cout << "  Years simulated: " << totalYears << std::endl;
    std::cout << "  Wall time: " << seconds << " s" << std::endl;
    std::cout << "  Throughput: " << (static_cast<double>(totalYears) / seconds) << " years/s" << std::endl;
    std::cout << "  Surviving countries: " << survivingCountries << std::endl;
    std::cout << "  Final world population: " << finalWorldPopulation << std::endl;
    std::cout << "  War starts (country-side count): " << totalWarStarts << std::endl;
    std::cout << "  Plague outbreaks: " << totalPlagues << std::endl;
    std::cout << "  Plague deaths: " << m_plagueDeathToll << std::endl;
    std::cout << "  Tech breakthroughs: " << totalTechBreakthroughs << std::endl;

    return true;
}

void Map::updatePlagueDeaths(long long deaths) {
    m_plagueDeathToll += deaths;
}

const std::vector<std::vector<bool>>& Map::getIsLandGrid() const {
    return m_isLandGrid;
}

sf::Vector2i Map::pixelToGrid(const sf::Vector2f& pixel) const {
    return sf::Vector2i(static_cast<int>(pixel.x / m_gridCellSize), static_cast<int>(pixel.y / m_gridCellSize));
}

int Map::getGridCellSize() const {
    return m_gridCellSize;
}

std::mutex& Map::getGridMutex() {
    return m_gridMutex;
}

const sf::Image& Map::getBaseImage() const {
    return m_baseImage;
}

int Map::getRegionSize() const {
    return m_regionSize;
}

const std::unordered_set<int>& Map::getDirtyRegions() const {
    return m_dirtyRegions;
}

const std::vector<std::vector<int>>& Map::getCountryGrid() const {
    return m_countryGrid;
}

const std::vector<std::vector<std::unordered_map<Resource::Type, double>>>& Map::getResourceGrid() const {
    return m_resourceGrid;
}

const SimulationConfig& Map::getConfig() const {
    return m_ctx->config;
}

// Getter implementation (in country.cpp)
int Country::getNextWarCheckYear() const {
    return m_nextWarCheckYear;
}

// Setter implementation (in country.cpp)
void Country::setNextWarCheckYear(int year) {
    m_nextWarCheckYear = year;
}

void Map::setCountryGridValue(int x, int y, int value) {
    setCountryOwner(x, y, value);
}

void Map::insertDirtyRegion(int regionIndex) {
    m_dirtyRegions.insert(regionIndex);
}

bool Map::setCountryOwner(int x, int y, int newOwner) {
    std::lock_guard<std::mutex> lock(m_gridMutex);
    return setCountryOwnerAssumingLockedImpl(x, y, newOwner);
}

bool Map::setCountryOwnerAssumingLocked(int x, int y, int newOwner) {
    return setCountryOwnerAssumingLockedImpl(x, y, newOwner);
}

bool Map::setCountryOwnerAssumingLockedImpl(int x, int y, int newOwner) {
    // Incremental adjacency maintenance:
    // When (x,y) changes owner, only the 8 edges incident to that cell can change. We update a symmetric
    // border-contact count matrix for (oldOwner, neighborOwner) and (newOwner, neighborOwner) for each
    // of the 8 neighboring cells. Adjacency exists iff the contact count between two countries is > 0.
    //
    // This makes neighbor checks O(1) and avoids repeated full-grid scans.
    const int height = static_cast<int>(m_countryGrid.size());
    if (y < 0 || y >= height) {
        return false;
    }
    const int width = static_cast<int>(m_countryGrid[0].size());
    if (x < 0 || x >= width) {
        return false;
    }

    const int oldOwner = m_countryGrid[y][x];
    if (oldOwner == newOwner) {
        return false;
    }

    // Incremental per-country aggregates (food/typed-nonfood potential and land cell count).
    // These are used by fast-forward / mega simulation to compute carrying capacity cheaply.
    const size_t cellIdx = static_cast<size_t>(y * width + x);
    const double cellFood = (cellIdx < m_cellFood.size()) ? m_cellFood[cellIdx] : 0.0;
    const double cellForaging = (cellIdx < m_cellForaging.size()) ? m_cellForaging[cellIdx] : 0.0;
    const double cellFarming = (cellIdx < m_cellFarming.size()) ? m_cellFarming[cellIdx] : 0.0;
    const double cellOre = (cellIdx < m_cellOre.size()) ? m_cellOre[cellIdx] : 0.0;
    const double cellEnergy = (cellIdx < m_cellEnergy.size()) ? m_cellEnergy[cellIdx] : 0.0;
    const double cellConstruction = (cellIdx < m_cellConstruction.size()) ? m_cellConstruction[cellIdx] : 0.0;
    const double cellNonFood = (cellIdx < m_cellNonFood.size()) ? m_cellNonFood[cellIdx] : 0.0;

    if (oldOwner >= 0) {
        ensureCountryAggregateCapacityForIndex(oldOwner);
        m_countryLandCellCount[static_cast<size_t>(oldOwner)] -= 1;
        m_countryFoodPotential[static_cast<size_t>(oldOwner)] -= cellFood;
        m_countryForagingPotential[static_cast<size_t>(oldOwner)] -= cellForaging;
        m_countryFarmingPotential[static_cast<size_t>(oldOwner)] -= cellFarming;
        m_countryOrePotential[static_cast<size_t>(oldOwner)] -= cellOre;
        m_countryEnergyPotential[static_cast<size_t>(oldOwner)] -= cellEnergy;
        m_countryConstructionPotential[static_cast<size_t>(oldOwner)] -= cellConstruction;
        m_countryNonFoodPotential[static_cast<size_t>(oldOwner)] -= cellNonFood;
        if (m_countryLandCellCount[static_cast<size_t>(oldOwner)] < 0) {
            m_countryLandCellCount[static_cast<size_t>(oldOwner)] = 0;
        }
        if (m_countryFoodPotential[static_cast<size_t>(oldOwner)] < 0.0) {
            // Guard against numeric drift.
            m_countryFoodPotential[static_cast<size_t>(oldOwner)] = 0.0;
        }
        if (m_countryNonFoodPotential[static_cast<size_t>(oldOwner)] < 0.0) {
            m_countryNonFoodPotential[static_cast<size_t>(oldOwner)] = 0.0;
        }
        if (m_countryForagingPotential[static_cast<size_t>(oldOwner)] < 0.0) {
            m_countryForagingPotential[static_cast<size_t>(oldOwner)] = 0.0;
        }
        if (m_countryFarmingPotential[static_cast<size_t>(oldOwner)] < 0.0) {
            m_countryFarmingPotential[static_cast<size_t>(oldOwner)] = 0.0;
        }
        if (m_countryOrePotential[static_cast<size_t>(oldOwner)] < 0.0) {
            m_countryOrePotential[static_cast<size_t>(oldOwner)] = 0.0;
        }
        if (m_countryEnergyPotential[static_cast<size_t>(oldOwner)] < 0.0) {
            m_countryEnergyPotential[static_cast<size_t>(oldOwner)] = 0.0;
        }
        if (m_countryConstructionPotential[static_cast<size_t>(oldOwner)] < 0.0) {
            m_countryConstructionPotential[static_cast<size_t>(oldOwner)] = 0.0;
        }
    }
    if (newOwner >= 0) {
        ensureCountryAggregateCapacityForIndex(newOwner);
        m_countryLandCellCount[static_cast<size_t>(newOwner)] += 1;
        m_countryFoodPotential[static_cast<size_t>(newOwner)] += cellFood;
        m_countryForagingPotential[static_cast<size_t>(newOwner)] += cellForaging;
        m_countryFarmingPotential[static_cast<size_t>(newOwner)] += cellFarming;
        m_countryOrePotential[static_cast<size_t>(newOwner)] += cellOre;
        m_countryEnergyPotential[static_cast<size_t>(newOwner)] += cellEnergy;
        m_countryConstructionPotential[static_cast<size_t>(newOwner)] += cellConstruction;
        m_countryNonFoodPotential[static_cast<size_t>(newOwner)] += cellNonFood;
    }

    // Ensure the tracking structures can represent any index we touch.
    ensureAdjacencyStorageForIndex(std::max({ oldOwner, newOwner, 0 }));

    // Update border-contact counts for the 8 edges incident to this cell.
    // Each edge contributes to a symmetric contact count between the two owners.
    static const int dxs[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };
    static const int dys[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };

    for (int k = 0; k < 8; ++k) {
        const int nx = x + dxs[k];
        const int ny = y + dys[k];
        if (nx < 0 || ny < 0 || ny >= height || nx >= width) {
            continue;
        }
        const int neighborOwner = m_countryGrid[ny][nx];

        removeBorderContact(oldOwner, neighborOwner);
        addBorderContact(newOwner, neighborOwner);
    }

    // Keep Country territory containers consistent with the authoritative grid.
    if (m_ownershipSyncCountries) {
        auto& countries = *m_ownershipSyncCountries;
        const sf::Vector2i cell(x, y);
        if (oldOwner >= 0 && oldOwner < static_cast<int>(countries.size()) &&
            countries[static_cast<size_t>(oldOwner)].getCountryIndex() == oldOwner) {
            countries[static_cast<size_t>(oldOwner)].removeTerritoryCell(cell);
        }
        if (newOwner >= 0 && newOwner < static_cast<int>(countries.size()) &&
            countries[static_cast<size_t>(newOwner)].getCountryIndex() == newOwner) {
            countries[static_cast<size_t>(newOwner)].addTerritoryCell(cell);
        }
    }

    m_countryGrid[y][x] = newOwner;
    m_controlCacheDirty = true;
    return true;
}

bool Map::paintCells(int countryIndex,
                     const sf::Vector2i& center,
                     int radius,
                     bool erase,
                     bool allowOverwrite,
                     std::vector<int>& affectedCountries) {
    if (radius < 0) {
        radius = 0;
    }

    const int height = static_cast<int>(m_countryGrid.size());
    if (height <= 0) {
        return false;
    }
    const int width = static_cast<int>(m_countryGrid[0].size());
    if (width <= 0) {
        return false;
    }

    const int regionsPerRow = m_regionSize > 0 ? (width / m_regionSize) : 0;
    if (regionsPerRow <= 0) {
        return false;
    }

    const int minX = std::max(0, center.x - radius);
    const int maxX = std::min(width - 1, center.x + radius);
    const int minY = std::max(0, center.y - radius);
    const int maxY = std::min(height - 1, center.y + radius);

    const int r2 = radius * radius;
    bool anyChanged = false;

    std::lock_guard<std::mutex> lock(m_gridMutex);
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const int dx = x - center.x;
            const int dy = y - center.y;
            if (dx * dx + dy * dy > r2) {
                continue;
            }
            if (!m_isLandGrid[y][x]) {
                continue;
            }

            const int prevOwner = m_countryGrid[y][x];
            int nextOwner = prevOwner;
            if (erase) {
                nextOwner = -1;
            } else {
                if (countryIndex < 0) {
                    continue;
                }
                if (prevOwner == -1 || prevOwner == countryIndex) {
                    nextOwner = countryIndex;
                } else if (allowOverwrite) {
                    nextOwner = countryIndex;
                } else {
                    continue;
                }
            }

            if (nextOwner == prevOwner) {
                continue;
            }

            setCountryOwnerAssumingLockedImpl(x, y, nextOwner);
            anyChanged = true;

            if (prevOwner >= 0) {
                affectedCountries.push_back(prevOwner);
            }
            if (nextOwner >= 0) {
                affectedCountries.push_back(nextOwner);
            }

            const int regionX = x / m_regionSize;
            const int regionY = y / m_regionSize;
            m_dirtyRegions.insert(regionY * regionsPerRow + regionX);
        }
    }

    return anyChanged;
}

void Map::rebuildCountryBoundary(Country& country) {
    const int idx = country.getCountryIndex();
    if (idx < 0) {
        country.setTerritory(std::unordered_set<sf::Vector2i>{});
        return;
    }

    std::unordered_set<sf::Vector2i> territory;
    const int height = static_cast<int>(m_countryGrid.size());
    const int width = height > 0 ? static_cast<int>(m_countryGrid[0].size()) : 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (m_countryGrid[y][x] == idx) {
                territory.insert(sf::Vector2i(x, y));
            }
        }
    }
    country.setTerritory(territory);
}

void Map::rebuildBoundariesForCountries(std::vector<Country>& countries, const std::vector<int>& countryIndices) {
    if (countries.empty() || countryIndices.empty()) {
        return;
    }

    std::vector<int> unique = countryIndices;
    std::sort(unique.begin(), unique.end());
    unique.erase(std::unique(unique.begin(), unique.end()), unique.end());

    std::vector<int> valid;
    valid.reserve(unique.size());
    for (int idx : unique) {
        if (idx >= 0 && idx < static_cast<int>(countries.size())) {
            valid.push_back(idx);
        }
    }
    if (valid.empty()) {
        return;
    }

    std::vector<int> indexToSlot(countries.size(), -1);
    for (size_t slot = 0; slot < valid.size(); ++slot) {
        indexToSlot[static_cast<size_t>(valid[slot])] = static_cast<int>(slot);
    }

    std::vector<std::unordered_set<sf::Vector2i>> territories(valid.size());

    const int height = static_cast<int>(m_countryGrid.size());
    const int width = static_cast<int>(m_countryGrid[0].size());
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int owner = m_countryGrid[y][x];
            if (owner < 0 || owner >= static_cast<int>(indexToSlot.size())) {
                continue;
            }
            int slot = indexToSlot[static_cast<size_t>(owner)];
            if (slot < 0) {
                continue;
            }
            territories[static_cast<size_t>(slot)].insert(sf::Vector2i(x, y));
        }
    }

    for (size_t slot = 0; slot < valid.size(); ++slot) {
        countries[static_cast<size_t>(valid[slot])].setTerritory(territories[slot]);
    }
}

void Map::rebuildAdjacency(const std::vector<Country>& countries) {
    rebuildCountryAdjacency(countries);
}

std::vector<std::vector<int>>& Map::getCountryGrid() {
    return m_countryGrid;
}

std::unordered_set<int>& Map::getDirtyRegions() {
    return m_dirtyRegions;
}

// map.cpp (Modified section)

void Map::triggerPlague(int year, News& news) {
    startPlague(year, news); // Reuse the existing startPlague logic

    // Immediately reset the next plague year for "spamming"
    std::uniform_int_distribution<> plagueIntervalDist(600, 700);
    m_plagueInterval = plagueIntervalDist(m_ctx->worldRng);
    m_nextPlagueYear = year + m_plagueInterval;
}

// FAST FORWARD MODE: Optimized simulation for 100 years in 2 seconds
void Map::fastForwardSimulation(std::vector<Country>& countries, int& currentYear, int targetYears, News& news, TechnologyManager& technologyManager) {
    std::mt19937_64 gen = m_ctx->makeRng(0x46465349ull ^ (static_cast<std::uint64_t>(currentYear) * 0x9E3779B97F4A7C15ull));
    
    // Clear dirty regions to start fresh
    m_dirtyRegions.clear();
    
    // Mark all regions as dirty for full update at the end
    int totalRegions = (m_baseImage.getSize().x / m_gridCellSize / m_regionSize) * 
                      (m_baseImage.getSize().y / m_gridCellSize / m_regionSize);
    for (int i = 0; i < totalRegions; ++i) {
        m_dirtyRegions.insert(i);
    }
    
    for (int year = 0; year < targetYears; ++year) {
        currentYear++;
        if (currentYear == 0) currentYear = 1;
        
        // Randomized plague logic - every 600-700 years (same as normal mode)
        if (currentYear == m_nextPlagueYear && !m_plagueActive) {
            startPlague(currentYear, news);
            initializePlagueCluster(countries); // Initialize geographic cluster
        }
        if (m_plagueActive && (currentYear == m_plagueStartYear + 3)) {
            endPlague(news);
        }
        
        // Batch update countries with simplified logic
        for (size_t i = 0; i < countries.size(); ++i) {
            // Simplified population growth and expansion
            if (!m_plagueActive) {
                countries[i].fastForwardGrowth(year, currentYear, m_isLandGrid, m_countryGrid, m_resourceGrid, news, *this, technologyManager, gen, false);
	            } else if (!isPopulationGridActive() && isCountryAffectedByPlague(static_cast<int>(i))) {
	                // NEW TECH-DEPENDENT PLAGUE SYSTEM - Only affect countries in plague cluster
	                double baseDeathRate = 0.05; // 5% typical country hit
	                long long deaths = static_cast<long long>(std::llround(countries[i].getPopulation() * baseDeathRate * countries[i].getPlagueMortalityMultiplier(technologyManager)));
	                deaths = std::min(deaths, countries[i].getPopulation()); // Clamp deaths to population
	                countries[i].applyPlagueDeaths(deaths);
	                m_plagueDeathToll += deaths;
	            }
            
            // TECHNOLOGY SHARING - Trader countries attempt to share technology
            countries[i].attemptTechnologySharing(currentYear, countries, technologyManager, *this, news);
        }
        
        // Simplified war logic - only every 10 years for performance
        if (year % 10 == 0) {
            for (size_t i = 0; i < countries.size(); ++i) {
                if (countries[i].getType() == Country::Type::Warmonger && 
                    countries[i].canDeclareWar() && !countries[i].isAtWar()) {
                    
                    // Find potential targets (simplified)
                    std::vector<size_t> potentialTargets;
                    for (size_t j = 0; j < countries.size(); ++j) {
                        if (i != j && countries[i].getMilitaryStrength() > countries[j].getMilitaryStrength()) {
                            potentialTargets.push_back(j);
                        }
                    }
                    
                    // 15% chance to declare war (reduced for fast forward)
                    if (!potentialTargets.empty() && gen() % 100 < 15) {
                        size_t targetIndex = potentialTargets[gen() % potentialTargets.size()];
                        countries[i].startWar(countries[targetIndex], news);
                    }
                }
                
                // Update war status
                if (countries[i].isAtWar()) {
                    countries[i].decrementWarDuration();
                    if (countries[i].getWarDuration() <= 0) {
                        countries[i].endWar(currentYear);
                    }
                }
                
                // TECHNOLOGY SHARING - Trader countries attempt to share technology
                countries[i].attemptTechnologySharing(currentYear, countries, technologyManager, *this, news);
            }
        }
    }
}
