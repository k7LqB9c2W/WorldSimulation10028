#include "map.h"
#include "technology.h"
#include "culture.h"
#include "great_people.h"
#include "trade.h"
#include "economy.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <limits>
#include <queue>
#include <unordered_set>
#include <cmath>
#include <algorithm>

Map::Map(const sf::Image& baseImage, const sf::Image& resourceImage, int gridCellSize, const sf::Color& landColor, const sf::Color& waterColor, int regionSize, SimulationContext& ctx) :
    m_ctx(&ctx),
    m_gridCellSize(gridCellSize),
    m_regionSize(regionSize),
    m_landColor(landColor),
    m_waterColor(waterColor),
    m_baseImage(baseImage),
    m_resourceImage(resourceImage),
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
        {sf::Color(127, 0, 55), Resource::Type::HORSES}
    };

	    initializeResourceGrid();
		    rebuildCellFoodCache();
		    rebuildCellNonFoodCache();
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

    // OPTIMIZATION 1: Use OpenMP parallel processing
    #pragma omp parallel for
    for (int y = 0; y < static_cast<int>(m_isLandGrid.size()); ++y) {
        for (size_t x = 0; x < m_isLandGrid[y].size(); ++x) {
            if (m_isLandGrid[y][x]) {
                // OPTIMIZATION 2: Simplified food calculation (no nested loops)
                // Check only immediate neighbors for water (much faster)
                double foodAmount = 51.2; // Inland food (supports 61,440 people)
                
                // Quick water proximity check (only 8 directions, not 49 pixels!)
                const int dx[] = {-1, -1, -1, 0, 0, 1, 1, 1};
                const int dy[] = {-1, 0, 1, -1, 1, -1, 0, 1};
                
                for (int i = 0; i < 8; ++i) {
                    int nx = static_cast<int>(x) + dx[i];
                    int ny = y + dy[i];
                    if (nx >= 0 && nx < static_cast<int>(m_isLandGrid[0].size()) && 
                        ny >= 0 && ny < static_cast<int>(m_isLandGrid.size())) {
                        if (!m_isLandGrid[ny][nx]) { // Found water
                            foodAmount = 102.4; // Coastal bonus (supports 122,880 people)
                            break;
                        }
                    }
                }
                
                m_resourceGrid[y][x][Resource::Type::FOOD] = foodAmount;

                // OPTIMIZATION 3: Batch process special resources
                sf::Vector2u pixelPos(static_cast<unsigned int>(x * m_gridCellSize), static_cast<unsigned int>(y * m_gridCellSize));
                sf::Color resourcePixelColor = m_resourceImage.getPixel(pixelPos.x, pixelPos.y);

                // Only process if the pixel is not fully transparent
                if (resourcePixelColor.a > 0) {
                    for (const auto& [color, type] : m_resourceColors) {
                        if (resourcePixelColor == color) {
                            const std::uint64_t coord = (static_cast<std::uint64_t>(x) << 32) ^ static_cast<std::uint64_t>(y);
                            const std::uint64_t saltA = 0xA8F1B4D5E6C70123ull ^ static_cast<std::uint64_t>(type);
                            const std::uint64_t saltB = 0x3D2C1B0A99887766ull ^ (static_cast<std::uint64_t>(type) << 32);
                            const double u1 = SimulationContext::u01FromU64(SimulationContext::mix64(m_ctx->worldSeed ^ coord ^ saltA));
                            const double u2 = SimulationContext::u01FromU64(SimulationContext::mix64(m_ctx->worldSeed ^ coord ^ saltB));
                            const double baseAmount = 0.2 + u1 * (2.0 - 0.2);
                            const double hotspot = 2.0 + u2 * (6.0 - 2.0);
                            m_resourceGrid[y][x][type] = baseAmount * hotspot;
                            break;
                        }
                    }
                }
            }
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "✅ RESOURCES INITIALIZED in " << duration.count() << " ms" << std::endl;
}

void Map::rebuildCellFoodCache() {
    const int height = static_cast<int>(m_resourceGrid.size());
    const int width = (height > 0) ? static_cast<int>(m_resourceGrid[0].size()) : 0;
    m_cellFood.assign(static_cast<size_t>(height * width), 0.0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (!m_isLandGrid[static_cast<size_t>(y)][static_cast<size_t>(x)]) {
                continue;
            }
            const auto& cell = m_resourceGrid[static_cast<size_t>(y)][static_cast<size_t>(x)];
            auto it = cell.find(Resource::Type::FOOD);
            if (it != cell.end()) {
                m_cellFood[static_cast<size_t>(y * width + x)] = it->second;
            }
        }
    }
}

void Map::rebuildCellNonFoodCache() {
    const int height = static_cast<int>(m_resourceGrid.size());
    const int width = (height > 0) ? static_cast<int>(m_resourceGrid[0].size()) : 0;
    m_cellNonFood.assign(static_cast<size_t>(height * width), 0.0);

    auto getAmount = [&](const std::unordered_map<Resource::Type, double>& cell, Resource::Type t) -> double {
        auto it = cell.find(t);
        return (it != cell.end()) ? it->second : 0.0;
    };

    // Weights are tuned for "macro" extraction potential (relative, not a stockpile).
    constexpr double wIron = 55.0;
    constexpr double wCoal = 55.0;
    constexpr double wSalt = 30.0;
    constexpr double wHorses = 20.0;
    constexpr double wGold = 65.0;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (!m_isLandGrid[static_cast<size_t>(y)][static_cast<size_t>(x)]) {
                continue;
            }
            const auto& cell = m_resourceGrid[static_cast<size_t>(y)][static_cast<size_t>(x)];
            const double iron = getAmount(cell, Resource::Type::IRON);
            const double coal = getAmount(cell, Resource::Type::COAL);
            const double salt = getAmount(cell, Resource::Type::SALT);
            const double horses = getAmount(cell, Resource::Type::HORSES);
            const double gold = getAmount(cell, Resource::Type::GOLD);
            const double pot = iron * wIron + coal * wCoal + salt * wSalt + horses * wHorses + gold * wGold;
            m_cellNonFood[static_cast<size_t>(y * width + x)] = std::max(0.0, pot);
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
    m_fieldFoodPotential.assign(n, 0.0f);

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

void Map::updateControlGrid(std::vector<Country>& countries, int currentYear, int dtYears) {
    (void)currentYear;
    (void)dtYears;

    ensureFieldGrids();
    if (m_fieldW <= 0 || m_fieldH <= 0) {
        return;
    }

    const int countryCount = static_cast<int>(countries.size());
    {
        std::lock_guard<std::mutex> lock(m_gridMutex);
        rebuildFieldOwnerIdAssumingLocked(countryCount);
    }

    auto clamp01f = [](float v) {
        return std::max(0.0f, std::min(1.0f, v));
    };
    auto clamp01d = [](double v) {
        return std::max(0.0, std::min(1.0, v));
    };

    std::vector<int> capX(static_cast<size_t>(countryCount), 0);
    std::vector<int> capY(static_cast<size_t>(countryCount), 0);
    std::vector<float> range(static_cast<size_t>(countryCount), 1.0f);

    const float baseRangeCells = 60.0f;
    for (int i = 0; i < countryCount; ++i) {
        const sf::Vector2i cap = countries[static_cast<size_t>(i)].getCapitalLocation();
        capX[static_cast<size_t>(i)] = std::max(0, std::min(m_fieldW - 1, cap.x / kFieldCellSize));
        capY[static_cast<size_t>(i)] = std::max(0, std::min(m_fieldH - 1, cap.y / kFieldCellSize));

        const double admin = clamp01d(countries[static_cast<size_t>(i)].getAdminCapacity());
        const double logi = clamp01d(countries[static_cast<size_t>(i)].getLogisticsReach());
        const double r = static_cast<double>(baseRangeCells) * (0.25 + 0.75 * logi) * (0.25 + 0.75 * admin);
        range[static_cast<size_t>(i)] = std::max(2.0f, static_cast<float>(r));
    }

    std::vector<double> sumControl(static_cast<size_t>(countryCount), 0.0);
    std::vector<int> countOwned(static_cast<size_t>(countryCount), 0);

    for (int fy = 0; fy < m_fieldH; ++fy) {
        for (int fx = 0; fx < m_fieldW; ++fx) {
            const size_t idx = static_cast<size_t>(fy) * static_cast<size_t>(m_fieldW) + static_cast<size_t>(fx);
            const int owner = (idx < m_fieldOwnerId.size()) ? m_fieldOwnerId[idx] : -1;
            float c = 0.0f;
            if (owner >= 0 && owner < countryCount) {
                const int dist = std::abs(fx - capX[static_cast<size_t>(owner)]) + std::abs(fy - capY[static_cast<size_t>(owner)]);
                const float r = range[static_cast<size_t>(owner)];
                c = clamp01f(1.0f - static_cast<float>(dist) / r);
                sumControl[static_cast<size_t>(owner)] += static_cast<double>(c);
                countOwned[static_cast<size_t>(owner)]++;
            }
            if (idx < m_fieldControl.size()) {
                m_fieldControl[idx] = c;
            }
        }
    }

    for (int i = 0; i < countryCount; ++i) {
        double avg = 0.0;
        const int n = countOwned[static_cast<size_t>(i)];
        if (n > 0) {
            avg = sumControl[static_cast<size_t>(i)] / static_cast<double>(n);
        }
        countries[static_cast<size_t>(i)].setAvgControl(avg);
    }
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

void Map::tickPopulationGrid(const std::vector<Country>& countries, int currentYear, int dtYears) {
    if (m_fieldPopulation.empty() || m_fieldFoodPotential.empty()) {
        return;
    }
    if (currentYear <= m_lastPopulationUpdateYear) {
        return;
    }
    m_lastPopulationUpdateYear = currentYear;

    auto clamp01 = [](double v) {
        return std::max(0.0, std::min(1.0, v));
    };

    const double years = static_cast<double>(std::max(1, dtYears));
    const float yearsF = static_cast<float>(years);

    const double baseBirth = 0.045; // crude, pre-modern baseline
    const double baseDeath = 0.036;

    const int countryCount = static_cast<int>(countries.size());
    const size_t n = m_fieldPopulation.size();
    const size_t ownerN = m_fieldOwnerId.size();
    prepareCountryClimateCaches(countryCount);

    auto KFor = [&](size_t i) -> double {
        const double food = (i < m_fieldFoodPotential.size()) ? static_cast<double>(std::max(0.0f, m_fieldFoodPotential[i])) : 0.0;
        return std::max(1.0, food * 1200.0);
    };

    // Demography: births/deaths with famine pressure when pop > K.
    for (size_t i = 0; i < n; ++i) {
        const float food = (i < m_fieldFoodPotential.size()) ? m_fieldFoodPotential[i] : 0.0f;
        if (food <= 0.0f) {
            continue;
        }

        const double K = KFor(i);
        const double pop = static_cast<double>(std::max(0.0f, m_fieldPopulation[i]));
        if (pop <= 0.0) {
            continue;
        }

        const double crowd = pop / K;
        double birthFactor = std::max(0.1, 1.15 - 0.75 * std::min(1.5, crowd));
        double deathFactor = 0.85 + 2.8 * std::max(0.0, crowd - 1.0);

        // Phase 4: macro food security feeds back into births/deaths (shortages bite).
        if (i < ownerN) {
            const int owner = m_fieldOwnerId[i];
            if (owner >= 0 && owner < countryCount) {
                const double fs = clamp01(countries[static_cast<size_t>(owner)].getFoodSecurity());
                birthFactor *= (0.20 + 0.80 * fs);
                deathFactor *= (1.00 + (1.0 - fs) * 1.60);

                // Phase 6: drought years add a mortality bump (country-level precip anomaly mean).
                if (owner >= 0 && owner < static_cast<int>(m_countryPrecipAnomMean.size())) {
                    const double pa = static_cast<double>(m_countryPrecipAnomMean[static_cast<size_t>(owner)]);
                    if (pa < -0.10) {
                        const double drought = clamp01((-pa - 0.10) / 0.20);
                        deathFactor *= (1.0 + 0.35 * drought);
                        birthFactor *= (1.0 - 0.15 * drought);
                    }
                }
            }
        }

        // Density disease pressure: scales smoothly with crowding and humidity.
        {
            const float pm = (i < m_fieldPrecipMean.size()) ? m_fieldPrecipMean[i] : 0.0f; // 0..1 baseline humidity proxy
            const double humid = clamp01(static_cast<double>(pm));
            deathFactor *= (1.0 + 0.28 * humid * std::max(0.0, crowd - 1.0));
        }

        // Plague: density-agnostic multiplier for now (density-aware later).
        if (m_plagueActive && i < ownerN) {
            const int owner = m_fieldOwnerId[i];
            if (owner >= 0 && owner < countryCount && isCountryAffectedByPlague(owner)) {
                deathFactor *= 2.2;
            }
        }

        const double births = pop * baseBirth * birthFactor * years;
        const double deaths = pop * baseDeath * deathFactor * years;

        const double next = std::max(0.0, pop + births - deaths);
        m_fieldPopulation[i] = static_cast<float>(next);
    }

    // Migration: cheap diffusion on attractiveness gradient.
    const int microIters = (dtYears <= 1) ? 3 : 1;
    const float migRate = (dtYears <= 1) ? 0.010f : (0.020f * yearsF);

    for (int it = 0; it < microIters; ++it) {
        // Attractiveness
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
            a -= static_cast<float>(1.35 * crowd);

            if (i < ownerN) {
                const int owner = m_fieldOwnerId[i];
                if (owner >= 0 && owner < countryCount) {
                    const Country& c = countries[static_cast<size_t>(owner)];
                    a -= static_cast<float>(0.55 * clamp01(c.getTaxRate()));
                    a -= static_cast<float>(0.45 * (1.0 - clamp01(c.getAvgControl())));
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
                    if (m_fieldFoodPotential[j] <= 0.0f) return; // water
                    const float d = m_fieldAttractiveness[j] - a0;
                    if (d <= 0.0f) return;
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
        m_countryNonFoodPotential.resize(need, 0.0);
    }
}

void Map::rebuildCountryPotentials(int countryCount) {
    if (countryCount <= 0) {
        m_countryLandCellCount.clear();
        m_countryFoodPotential.clear();
        m_countryNonFoodPotential.clear();
        return;
    }

    m_countryLandCellCount.assign(static_cast<size_t>(countryCount), 0);
    m_countryFoodPotential.assign(static_cast<size_t>(countryCount), 0.0);
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
    std::uniform_int_distribution<> xDist(0, static_cast<int>(m_isLandGrid[0].size() - 1));
    std::uniform_int_distribution<> yDist(0, static_cast<int>(m_isLandGrid.size() - 1));
    std::uniform_int_distribution<> colorDist(50, 255);
    std::uniform_real_distribution<> growthRateDist(0.0003, 0.001); // Legacy - not used in logistic system
    std::uniform_real_distribution<> spawnDist(0.0, 1.0);

    std::uniform_int_distribution<> typeDist(0, 2);
    std::discrete_distribution<> scienceTypeDist({ 50, 40, 10 });
    std::discrete_distribution<> cultureTypeDist({ 40, 40, 20 }); // 40% NC, 40% LC, 20% MC

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

    // Small initial territory clusters (prevents single-cell artifacts and keeps pop counted to owners).
    std::uniform_int_distribution<int> territoryRadiusFieldDist(2, 6);

    for (int i = 0; i < numCountries; ++i) {
        sf::Vector2i startCell;
        double spawnRoll = spawnDist(rng);

        if (spawnRoll < 0.75) {
            // Attempt to spawn in a preferred zone
            startCell = getRandomCellInPreferredZones(rng);
        }
        else {
            // 25% chance: Random spawn anywhere on land
            do {
                startCell.x = xDist(rng);
                startCell.y = yDist(rng);
            } while (!m_isLandGrid[startCell.y][startCell.x]);
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
        Country::ScienceType scienceType = static_cast<Country::ScienceType>(scienceTypeDist(rng));
        Country::CultureType cultureType = static_cast<Country::CultureType>(cultureTypeDist(rng));
        countries.emplace_back(i,
                               countryColor,
                               startCell,
                               initialPopulation,
                               growthRate,
                               countryName,
                               countryType,
                               scienceType,
                               cultureType,
                               m_ctx->seedForCountry(i));
        setCountryOwnerAssumingLockedImpl(startCell.x, startCell.y, i);

        // Paint a small initial territory blob so population can be distributed over multiple field cells.
        {
            const int rField = territoryRadiusFieldDist(rng);
            const int rPx = std::max(1, rField * kFieldCellSize);
            std::vector<int> affected;
            affected.reserve(64);
            paintCells(i, startCell, rPx, /*erase*/false, /*allowOverwrite*/false, affected);
        }

        int regionIndex = static_cast<int>(startCell.y / m_regionSize * (m_baseImage.getSize().x / m_gridCellSize / m_regionSize) + startCell.x / m_regionSize);
        m_dirtyRegions.insert(regionIndex);
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
	        countries[i].update(m_isLandGrid, m_countryGrid, m_gridMutex, m_gridCellSize, m_regionSize, m_dirtyRegions, currentYear, m_resourceGrid, news, m_plagueActive, m_plagueDeathToll, *this, technologyManager, countries);
	        countries[i].attemptTechnologySharing(currentYear, countries, technologyManager, *this, news);
	    }

    // Clean up extinct countries without erasing (keeps country indices stable).
    for (int i = 0; i < static_cast<int>(countries.size()); ++i) {
        const long long pop = countries[i].getPopulation();
        const bool hasTerritory = !countries[i].getBoundaryPixels().empty();
        const bool hasCities = !countries[i].getCities().empty();
        const bool strandedMicroPolity = hasTerritory && !hasCities && pop > 0 && pop < 2000;
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

void Map::tickDemographyAndCities(std::vector<Country>& countries, int currentYear, int dtYears, News& news) {
    attachCountriesForOwnershipSync(&countries);
    tickPopulationGrid(countries, currentYear, dtYears);
    applyPopulationTotalsToCountries(countries);
    // City placement cadence (calendar-based) keeps artifacts low in fast-forward.
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
                                 CultureManager& cultureManager) {
    if (countries.empty()) {
        return;
    }

    // Phase 2: rule-driven fragmentation + tag replacement (pressure/control driven).
    std::mt19937_64& rng = m_ctx->worldRng;

    auto clamp01 = [](double v) {
        return std::max(0.0, std::min(1.0, v));
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

    auto pickSeedBField = [&](int countryIndex, int capFx, int capFy) -> sf::Vector2i {
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

    auto trySplitCountry = [&](int countryIndex, double rRisk) -> bool {
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
        const sf::Vector2i seedB = pickSeedBField(countryIndex, capFx, capFy);
        if (seedA == seedB) return false;

        std::unordered_set<sf::Vector2i> groupA;
        std::unordered_set<sf::Vector2i> groupB;
        groupA.reserve(territorySet.size());
        groupB.reserve(territorySet.size() / 2);

        for (const auto& cell : territorySet) {
            const int fx = cell.x / kFieldCellSize;
            const int fy = cell.y / kFieldCellSize;
            const int dA = std::abs(fx - seedA.x) + std::abs(fy - seedA.y);
            const int dB = std::abs(fx - seedB.x) + std::abs(fy - seedB.y);
            if (dB < dA) groupB.insert(cell);
            else groupA.insert(cell);
        }

	        const size_t total = groupA.size() + groupB.size();
	        if (total == 0 || groupA.empty() || groupB.empty()) return false;
	        double ratioB = static_cast<double>(groupB.size()) / static_cast<double>(total);
	        if (ratioB < 0.22 || ratioB > 0.78) return false;

	        if (groupB.count(capPx) > 0) {
	            std::swap(groupA, groupB);
	            ratioB = static_cast<double>(groupB.size()) / static_cast<double>(total);
	        }

        const double lossFrac = std::clamp(0.06 + 0.10 * rRisk, 0.04, 0.20);
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
                           country.getScienceType(), country.getCultureType(), m_ctx->seedForCountry(newIndex));
        newCountry.setIdeology(country.getIdeology());
        newCountry.setLegitimacy(0.35);
        newCountry.setStability(0.42);
        newCountry.setFragmentationCooldown(fragmentationCooldownYears);
        newCountry.setYearsSinceWar(0);
        newCountry.resetStagnation();
        newCountry.setTerritory(groupB);
        newCountry.setCities(newCities);
        newCountry.setRoads(newRoads);
        newCountry.setFactories(newFactories);
        newCountry.setGold(newGold);
        newCountry.initializeTechSharingTimer(currentYear);

        countries[static_cast<size_t>(countryIndex)].setStartingPixel(oldStart);
        countries[static_cast<size_t>(countryIndex)].setPopulation(oldPop);
        countries[static_cast<size_t>(countryIndex)].setGold(oldGold);
        countries[static_cast<size_t>(countryIndex)].setLegitimacy(std::max(0.20, countries[static_cast<size_t>(countryIndex)].getLegitimacy() * 0.65));
        countries[static_cast<size_t>(countryIndex)].setStability(std::max(0.25, countries[static_cast<size_t>(countryIndex)].getStability() * 0.70));
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
        for (int r = 0; r < 6; ++r) {
            const Resource::Type type = static_cast<Resource::Type>(r);
            const double amount = countries[static_cast<size_t>(countryIndex)].getResourceManager().getResourceAmount(type);
            if (amount <= 0.0) continue;
            const double moved = amount * ratio;
            if (moved <= 0.0) continue;
            const_cast<ResourceManager&>(countries[static_cast<size_t>(countryIndex)].getResourceManager()).consumeResource(type, moved);
            const_cast<ResourceManager&>(countries[static_cast<size_t>(newIndex)].getResourceManager()).addResource(type, moved);
        }

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

    if (currentYear % 5 == 0) {
        struct Candidate { int idx; double risk; };
        std::vector<Candidate> cand;
        cand.reserve(countries.size());

        const int n = static_cast<int>(countries.size());
        for (int i = 0; i < n; ++i) {
            const Country& c = countries[static_cast<size_t>(i)];
            if (c.getPopulation() <= 0) continue;
            if (c.getFragmentationCooldown() > 0) continue;
            if (c.getBoundaryPixels().size() < static_cast<size_t>(minTerritoryPixels)) continue;

            const double r = revoltRisk(c, i);
            if (r < 0.62) continue;
            if (c.getAvgControl() > 0.70) continue;
            cand.push_back({i, r});
        }

        std::sort(cand.begin(), cand.end(), [](const Candidate& a, const Candidate& b) { return a.risk > b.risk; });
        int splits = 0;
        for (const auto& c : cand) {
            if (splits >= 2) break;
            if (trySplitCountry(c.idx, c.risk)) {
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
            c.setLegitimacy(0.45);
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
	                               c.getType(), c.getScienceType(), c.getCultureType(), m_ctx->seedForCountry(newIndex));
	            newCountry.setIdeology(c.getIdeology());
	            newCountry.setStability(std::max(0.30, std::min(0.60, c.getStability())));
	            newCountry.setLegitimacy(std::max(0.20, std::min(0.55, c.getLegitimacy() * 0.90)));
	            newCountry.setFragmentationCooldown(180);

	            // Inherit cultural traits and knowledge stocks (but not the full polity apparatus).
	            newCountry.getTraitsMutable() = c.getTraits();
	            newCountry.getKnowledgeMutable() = c.getKnowledge();

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
                           country.getScienceType(), country.getCultureType(), m_ctx->seedForCountry(newIndex));
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

                for (int r = 0; r < 6; ++r) {
                    Resource::Type type = static_cast<Resource::Type>(r);
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
                       GreatPeopleManager& greatPeopleManager,
                       std::function<void(int, int, float)> progressCallback,
                       std::function<void(int, int)> chunkCompletedCallback,
                       const std::atomic<bool>* cancelRequested) {
    
    std::cout << "\nBEGINNING MEGA SIMULATION OF HUMAN HISTORY!" << std::endl;

    std::mt19937_64 gen = m_ctx->makeRng(
        0x4D454741ull ^
        (static_cast<std::uint64_t>(currentYear) * 0x9E3779B97F4A7C15ull) ^
        (static_cast<std::uint64_t>(targetYear) * 0xBF58476D1CE4E5B9ull));

    // Phase 4: run a local macro economy + directed trade inside the mega-jump loop so
    // shortages and logistics constraints continue to matter in fast-forward history.
    TradeManager localTrade(*m_ctx);
    EconomyModelCPU localEconomy(*m_ctx);
    
    int totalYears = targetYear - currentYear;
    int startYear = currentYear;
    bool canceled = false;
    
    // Track major historical events
    std::vector<std::string> majorEvents;
    std::vector<std::string> extinctCountries;
    std::vector<std::string> superPowers;
    int totalWars = 0;
    int totalPlagues = 0;
    int totalTechBreakthroughs = 0;
    long long lastMilestone = 0;
    
    std::cout << "SIMULATION PARAMETERS:" << std::endl;
    std::cout << "   Years to simulate: " << totalYears << std::endl;
    std::cout << "   Starting countries: " << countries.size() << std::endl;
    std::cout << "   Optimization level: MAXIMUM" << std::endl;
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Clear dirty regions and mark all for update
    m_dirtyRegions.clear();
    int totalRegions = (m_baseImage.getSize().x / m_gridCellSize / m_regionSize) * 
                      (m_baseImage.getSize().y / m_gridCellSize / m_regionSize);
    for (int i = 0; i < totalRegions; ++i) {
        m_dirtyRegions.insert(i);
    }
    
    // 🚀 SUPER OPTIMIZATION: Process in large chunks for maximum speed
    const int megaChunkSize = 50; // Process 50 years at a time
    int chunksProcessed = 0;
    int totalChunks = (totalYears + megaChunkSize - 1) / megaChunkSize;
    
    std::cout << "\nBEGINNING MEGA CHUNKS (" << totalChunks << " chunks of " << megaChunkSize << " years each)..." << std::endl;
    
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
    for (size_t i = 0; i < countries.size(); ++i) {
        lastTechCountPerCountry[i] = techManager.getUnlockedTechnologies(countries[i]).size();
    }

    for (int chunkStart = 0; chunkStart < totalYears; chunkStart += megaChunkSize) {
        if (cancelRequested && cancelRequested->load(std::memory_order_relaxed)) {
            canceled = true;
            break;
        }
        int chunkYears = std::min(megaChunkSize, totalYears - chunkStart);
        chunksProcessed++;
        
        maybeReportProgress(false);

        const float progressPercent = (totalYears > 0)
            ? (static_cast<float>(chunkStart) / static_cast<float>(totalYears) * 100.0f)
            : 100.0f;
        
        std::cout << "MEGA CHUNK " << chunksProcessed << "/" << totalChunks 
                  << " (" << std::fixed << std::setprecision(1) << progressPercent << "%) - "
                  << "Years " << currentYear << " to " << (currentYear + chunkYears)
                  << std::endl;
        
        // Process this chunk
        for (int year = 0; year < chunkYears; ++year) {
            if (cancelRequested && cancelRequested->load(std::memory_order_relaxed)) {
                canceled = true;
                break;
            }
            currentYear++;
            if (currentYear == 0) currentYear = 1; // Skip year 0

            maybeReportProgress(false);
            
            // 🦠 CONSISTENT PLAGUE TIMING - Same as normal mode (600-700 years)
            if (currentYear == m_nextPlagueYear && !m_plagueActive) {
                startPlague(currentYear, news);
                initializePlagueCluster(countries); // Initialize geographic cluster
                totalPlagues++;
                
                std::string plagueEvent = "🦠 MEGA PLAGUE ravages the world in " + std::to_string(currentYear);
                if (currentYear < 0) plagueEvent += " BCE";
                else plagueEvent += " CE";
                majorEvents.push_back(plagueEvent);
            }
	            if (m_plagueActive && (currentYear == m_plagueStartYear + 3)) { // 3 year plagues
	                endPlague(news);
	            }

		            // Phase 3 mega-jump support: update coarse fields in larger steps.
		            if (isPopulationGridActive() && (currentYear % 10 == 0)) {
		                updateControlGrid(countries, currentYear, 10);
                        tickWeather(currentYear, 10);
		                localEconomy.tickYear(currentYear, 10, *this, countries, techManager, localTrade, news);
		                tickPopulationGrid(countries, currentYear, 10);
		                applyPopulationTotalsToCountries(countries);
		                updateCitiesFromPopulation(countries, currentYear, 50, news);
		                processPoliticalEvents(countries, localTrade, currentYear, news, techManager, cultureManager);
		            }
	            
	            // MEGA COUNTRY UPDATES - calendar-based cadence (avoids chunk-boundary artifacts).
	            const int simYearIndex = currentYear - startYear;

            for (size_t i = 0; i < countries.size(); ++i) {
                if ((i & 63u) == 0u) {
                    if (cancelRequested && cancelRequested->load(std::memory_order_relaxed)) {
                        canceled = true;
                        break;
                    }
                }
                if (countries[i].getPopulation() <= 0) continue; // Skip dead countries
                
                const bool plagueAffected = m_plagueActive && isCountryAffectedByPlague(static_cast<int>(i));

                // Population growth, science/culture generation, and expansion (fast but calendar-aligned).
                countries[i].fastForwardGrowth(simYearIndex, currentYear, m_isLandGrid, m_countryGrid, m_resourceGrid,
                                               news, *this, techManager, gen, plagueAffected);

                if (plagueAffected && !isPopulationGridActive()) {
	                    // Plague effects: mortality hit (mirrors normal-mode magnitude).
	                    const long long currentPop = countries[i].getPopulation();
	                    double baseDeathRate = 0.05; // 5% typical country hit
	                    long long deaths = static_cast<long long>(std::llround(static_cast<double>(currentPop) * baseDeathRate *
	                                                                          countries[i].getPlagueMortalityMultiplier(techManager)));
	                    deaths = std::min(deaths, currentPop);
	                    countries[i].applyPlagueDeaths(deaths);
	                    updatePlagueDeaths(deaths);
	                }
            }

            // Tech/culture progression: tick in multi-year steps (scales with dtYears).
            if (currentYear % 5 == 0) {
                techManager.tickYear(countries, *this, &localEconomy.getLastTradeIntensity(), currentYear, 5);
                cultureManager.tickYear(countries, *this, techManager, &localEconomy.getLastTradeIntensity(), currentYear, 5, news);
                for (size_t i = 0; i < countries.size(); ++i) {
                    const size_t currentTechCount = techManager.getUnlockedTechnologies(countries[i]).size();
                    if (currentTechCount > lastTechCountPerCountry[i]) {
                        totalTechBreakthroughs += static_cast<int>(currentTechCount - lastTechCountPerCountry[i]);
                        lastTechCountPerCountry[i] = currentTechCount;
                    }
                }
            }
            
            // 🗡️ MEGA WAR SIMULATION - Epic conflicts every 50 years instead of 25 (OPTIMIZATION)
            if (currentYear % 50 == 0) {
                for (size_t i = 0; i < countries.size(); ++i) {
                    if (countries[i].getType() == Country::Type::Warmonger && 
                        !countries[i].isAtWar() && countries[i].getPopulation() > 1000) {
                        
	                        // 💀 FIRST: Check for annihilation opportunities (weak neighbors)
	                        Country* weakestNeighbor = nullptr;
	                        double weakestMilitaryStrength = std::numeric_limits<double>::max();
	                        
	                        for (int neighborIndex : getAdjacentCountryIndices(countries[i].getCountryIndex())) {
	                            if (neighborIndex < 0 || neighborIndex >= static_cast<int>(countries.size())) {
	                                continue;
	                            }
	                            if (static_cast<int>(i) == neighborIndex) {
	                                continue;
	                            }
	                            if (countries[static_cast<size_t>(neighborIndex)].getCountryIndex() != neighborIndex) {
	                                continue;
	                            }
	                            if (countries[static_cast<size_t>(neighborIndex)].getPopulation() <= 0) {
	                                continue;
	                            }

	                            double targetStrength = countries[static_cast<size_t>(neighborIndex)].getMilitaryStrength();
	                            if (targetStrength < weakestMilitaryStrength &&
	                                countries[i].canAnnihilateCountry(countries[static_cast<size_t>(neighborIndex)])) {
	                                weakestMilitaryStrength = targetStrength;
	                                weakestNeighbor = &countries[static_cast<size_t>(neighborIndex)];
	                            }
	                        }
                        
                        if (weakestNeighbor) {
                            // 💀 ANNIHILATION ATTACK - Complete absorption
                            countries[i].startWar(*weakestNeighbor, news);
                            countries[i].absorbCountry(*weakestNeighbor, *this, news);
                            totalWars++;
                            
                            std::string annihilationEvent = "💀 ANNIHILATION: " + countries[i].getName() + 
                                                           " completely destroys " + weakestNeighbor->getName() + " in " + std::to_string(currentYear);
                            if (currentYear < 0) annihilationEvent += " BCE";
                            else annihilationEvent += " CE";
                            majorEvents.push_back(annihilationEvent);
                        } else {
	                            // 🗡️ NORMAL WAR - Find the largest neighbor to attack
	                            Country* largestNeighbor = nullptr;
	                            long long largestPop = 0;
	                            
	                            for (int neighborIndex : getAdjacentCountryIndices(countries[i].getCountryIndex())) {
	                                if (neighborIndex < 0 || neighborIndex >= static_cast<int>(countries.size())) {
	                                    continue;
	                                }
	                                if (static_cast<int>(i) == neighborIndex) {
	                                    continue;
	                                }
	                                if (countries[static_cast<size_t>(neighborIndex)].getCountryIndex() != neighborIndex) {
	                                    continue;
	                                }
	                                long long pop = countries[static_cast<size_t>(neighborIndex)].getPopulation();
	                                if (pop > largestPop) {
	                                    largestPop = pop;
	                                    largestNeighbor = &countries[static_cast<size_t>(neighborIndex)];
	                                }
	                            }
                            
                            if (largestNeighbor) {
                                countries[i].startWar(*largestNeighbor, news);
                                totalWars++;
                                
                                std::string warEvent = "⚔️ " + countries[i].getName() + " attacks " + largestNeighbor->getName() + " in " + std::to_string(currentYear);
                                if (currentYear < 0) warEvent += " BCE";
                                else warEvent += " CE";
                                majorEvents.push_back(warEvent);
                            }
                        }
                    }
                }
            }
            if (canceled) {
                break;
            }
            
            // MEGA GREAT PEOPLE - closer to normal cadence, still amortized.
            if (currentYear % 5 == 0) {
                greatPeopleManager.updateEffects(currentYear, countries, news);
            }
        }
        if (canceled) {
            break;
        }
        
        // 📊 Mark extinct countries and track superpowers (keep indices stable by never erasing).
        long long totalWorldPopulation = 0;
        for (const auto& country : countries) {
            totalWorldPopulation += country.getPopulation();
        }

        long long superpowerThreshold = std::max(100000LL, totalWorldPopulation / 20); // Top 5% or min 100k

        for (int i = 0; i < static_cast<int>(countries.size()); ++i) {
            Country& country = countries[static_cast<size_t>(i)];

            const bool alreadyExtinct = (country.getPopulation() <= 0) && country.getBoundaryPixels().empty();
            const bool isExtinct = (country.getPopulation() <= 50) || country.getBoundaryPixels().empty();

            if (!alreadyExtinct && isExtinct) {
                extinctCountries.push_back(country.getName() + " (extinct in " + std::to_string(currentYear) +
                                           " - Pop: " + std::to_string(country.getPopulation()) +
                                           ", Territory: " + std::to_string(country.getBoundaryPixels().size()) + " pixels)");
                markCountryExtinct(countries, i, currentYear, news);
                continue;
            }

            if (!isExtinct && country.getPopulation() > superpowerThreshold) {
                bool alreadyTracked = false;
                for (const auto& power : superPowers) {
                    if (power.find(country.getName()) != std::string::npos) {
                        alreadyTracked = true;
                        break;
                    }
                }
                if (!alreadyTracked) {
                    superPowers.push_back("🏛️ " + country.getName() + " becomes a superpower (" + std::to_string(country.getPopulation()) + " people in " + std::to_string(currentYear) + ")");
                }
            }
        }
        
        // Track world population milestones naturally
        // Only track significant natural milestones (powers of 10)
        long long currentMilestone = 1;
        while (currentMilestone <= totalWorldPopulation) {
            currentMilestone *= 10;
        }
        currentMilestone /= 10; // Get the largest power of 10 below current population
        
        if (currentMilestone > lastMilestone && currentMilestone >= 1000000) { // Only track millions and above
            majorEvents.push_back("🌍 World population reaches " + std::to_string(currentMilestone) + " in " + std::to_string(currentYear));
            lastMilestone = currentMilestone;
        }

        if (chunkCompletedCallback) {
            chunkCompletedCallback(currentYear, chunkYears);
        }
    }

    maybeReportProgress(true);

    if (canceled) {
        std::cout << "\n🛑🛑🛑 MEGA TIME JUMP CANCELED 🛑🛑🛑" << std::endl;
        std::cout << "⏱️ Stopped at year " << currentYear << std::endl;
        return false;
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    std::cout << "\n🎉🎉🎉 MEGA TIME JUMP COMPLETE! 🎉🎉🎉" << std::endl;
    std::cout << "⏱️ Simulated " << totalYears << " years in " << (totalDuration.count() / 1000.0) << " seconds!" << std::endl;
    std::cout << "⚡ Performance: " << (totalYears / (totalDuration.count() / 1000.0)) << " years/second" << std::endl;
    
    // Calculate final world population
    long long finalWorldPopulation = 0;
    int survivingCountries = 0;
    for (const auto& country : countries) {
        finalWorldPopulation += country.getPopulation();
        if (country.getPopulation() > 0 && !country.getBoundaryPixels().empty()) {
            survivingCountries++;
        }
    }
    
    std::cout << "\n📈 MEGA STATISTICS:" << std::endl;
    std::cout << "   🏛️ Surviving countries: " << survivingCountries << std::endl;
    std::cout << "   💀 Extinct countries: " << extinctCountries.size() << std::endl;
    std::cout << "   🌍 Final world population: " << finalWorldPopulation << std::endl;
    std::cout << "   ⚔️ Total wars: " << totalWars << std::endl;
    std::cout << "   🦠 Total plagues: " << totalPlagues << std::endl;
    std::cout << "   💀 Total plague deaths: " << m_plagueDeathToll << std::endl;
    std::cout << "   🧠 Tech breakthroughs: " << totalTechBreakthroughs << std::endl;
    
    std::cout << "\n🌟 MAJOR HISTORICAL EVENTS:" << std::endl;
    int eventCount = 0;
    for (const auto& event : majorEvents) {
        if (eventCount++ >= 10) break; // Show top 10 events
        std::cout << "   " << event << std::endl;
    }
    
    std::cout << "\n💀 EXTINCT CIVILIZATIONS:" << std::endl;
    for (size_t i = 0; i < std::min(extinctCountries.size(), size_t(5)); ++i) {
        std::cout << "   " << extinctCountries[i] << std::endl;
    }
    
    std::cout << "\n🏛️ SUPERPOWERS EMERGED:" << std::endl;
    for (const auto& power : superPowers) {
        std::cout << "   " << power << std::endl;
    }
    
    std::cout << "\n🌍 Welcome to " << currentYear;
    if (currentYear < 0) std::cout << " BCE";
    else std::cout << " CE";
    std::cout << "! The world has changed dramatically!" << std::endl;
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

    // Incremental per-country aggregates (food/non-food potential and land cell count).
    // These are used by fast-forward / mega simulation to compute carrying capacity cheaply.
    const size_t cellIdx = static_cast<size_t>(y * width + x);
    const double cellFood = (cellIdx < m_cellFood.size()) ? m_cellFood[cellIdx] : 0.0;
    const double cellNonFood = (cellIdx < m_cellNonFood.size()) ? m_cellNonFood[cellIdx] : 0.0;

    if (oldOwner >= 0) {
        ensureCountryAggregateCapacityForIndex(oldOwner);
        m_countryLandCellCount[static_cast<size_t>(oldOwner)] -= 1;
        m_countryFoodPotential[static_cast<size_t>(oldOwner)] -= cellFood;
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
    }
    if (newOwner >= 0) {
        ensureCountryAggregateCapacityForIndex(newOwner);
        m_countryLandCellCount[static_cast<size_t>(newOwner)] += 1;
        m_countryFoodPotential[static_cast<size_t>(newOwner)] += cellFood;
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
