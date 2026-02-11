// trade.cpp

#include "trade.h"
#include "country.h"
#include "map.h"
#include "technology.h"
#include "news.h"
#include <algorithm>
#include <iostream>
#include <cmath>
#include <SFML/Graphics.hpp>
#include <queue>
#include <limits>

namespace {
constexpr int kDefaultSeaNavStep = 6;             // Downsample factor for sea navigation grid
constexpr int kMaxDockSearchRadiusPx = 18;        // How far (in pixels) to search for a navigable water nav-cell near a port
constexpr int kMaxDockCandidates = 6;             // Per-port dock candidates
constexpr int kMaxShippingPartnerAttempts = 45;   // Random partner attempts per country per establish tick
constexpr int kMaxAStarNodeExpansions = 220000;   // Hard cap to prevent pathological spikes

inline int clampi(int v, int lo, int hi) {
    return (v < lo) ? lo : (v > hi ? hi : v);
}

const char* resourceTypeName(Resource::Type resource) {
    switch (resource) {
        case Resource::Type::FOOD: return "food";
        case Resource::Type::HORSES: return "horses";
        case Resource::Type::SALT: return "salt";
        case Resource::Type::IRON: return "iron";
        case Resource::Type::COAL: return "coal";
        case Resource::Type::GOLD: return "gold";
        case Resource::Type::COPPER: return "copper";
        case Resource::Type::TIN: return "tin";
        case Resource::Type::CLAY: return "clay";
        default: return "goods";
    }
}

double resourceBasePrice(Resource::Type resource) {
    switch (resource) {
        case Resource::Type::FOOD: return 1.0;
        case Resource::Type::HORSES: return 5.0;
        case Resource::Type::SALT: return 3.0;
        case Resource::Type::IRON: return 4.0;
        case Resource::Type::COAL: return 2.0;
        case Resource::Type::GOLD: return 10.0;
        case Resource::Type::COPPER: return 4.5;
        case Resource::Type::TIN: return 8.0;
        case Resource::Type::CLAY: return 2.0;
        default: return 1.0;
    }
}
} // namespace

TradeManager::TradeManager(SimulationContext& ctx) : m_rng(ctx.makeRng(0x5452414445ull)) {
    std::cout << "🏪 Trade & Economic Exchange Framework initialized!" << std::endl;
}

void TradeManager::ensureSeaNavPublic(const Map& map) {
    ensureSeaNavGrid(map);
}

bool TradeManager::findSeaPathLenPx(const Map& map,
                                   const sf::Vector2i& fromPortCell,
                                   const sf::Vector2i& toCoastCell,
                                   float& outLenPx) {
    outLenPx = 0.0f;
    ensureSeaNavGrid(map);
    if (!m_seaNav.ready || m_seaNav.width <= 0 || m_seaNav.height <= 0) {
        return false;
    }

    const Vector2i from(fromPortCell.x, fromPortCell.y);
    const Vector2i to(toCoastCell.x, toCoastCell.y);

    const std::vector<Vector2i> docksA = findDockCandidates(from, map);
    const std::vector<Vector2i> docksB = findDockCandidates(to, map);
    if (docksA.empty() || docksB.empty()) {
        return false;
    }

    // Pick the closest dock-pair within the same sea component (cheap pre-filter).
    Vector2i bestStartNav;
    Vector2i bestGoalNav;
    bool foundDockPair = false;
    int bestScore = std::numeric_limits<int>::max();
    for (const auto& da : docksA) {
        const int aNavIdx = da.y * m_seaNav.width + da.x;
        if (aNavIdx < 0 || aNavIdx >= static_cast<int>(m_seaNav.componentId.size())) continue;
        const int aComp = m_seaNav.componentId[static_cast<size_t>(aNavIdx)];
        if (aComp < 0) continue;

        for (const auto& db : docksB) {
            const int bNavIdx = db.y * m_seaNav.width + db.x;
            if (bNavIdx < 0 || bNavIdx >= static_cast<int>(m_seaNav.componentId.size())) continue;
            const int bComp = m_seaNav.componentId[static_cast<size_t>(bNavIdx)];
            if (bComp != aComp) continue;

            const int dx = da.x - db.x;
            const int dy = da.y - db.y;
            const int d2 = dx * dx + dy * dy;
            if (d2 < bestScore) {
                bestScore = d2;
                bestStartNav = da;
                bestGoalNav = db;
                foundDockPair = true;
            }
        }
    }
    if (!foundDockPair) {
        return false;
    }

    std::vector<Vector2i> navPath;
    if (!findSeaPathCached(bestStartNav, bestGoalNav, navPath)) {
        return false;
    }
    if (navPath.size() < 2) {
        return false;
    }

    float total = 0.0f;
    for (size_t i = 1; i < navPath.size(); ++i) {
        const int dx = navPath[i].x - navPath[i - 1].x;
        const int dy = navPath[i].y - navPath[i - 1].y;
        total += std::sqrt(static_cast<float>(dx * dx + dy * dy)) * static_cast<float>(m_seaNav.step);
    }
    outLenPx = total;
    return true;
}

void TradeManager::beginExportsYear(int year, size_t countryCount) {
    m_lastCountryExportsYear = year;
    m_countryExportsValue.assign(countryCount, 0.0);
}

void TradeManager::addExportValue(int exporterIndex, double value) {
    if (exporterIndex < 0) {
        return;
    }
    if (value <= 0.0) {
        return;
    }
    if (static_cast<size_t>(exporterIndex) >= m_countryExportsValue.size()) {
        return;
    }
    m_countryExportsValue[static_cast<size_t>(exporterIndex)] += value;
}

// 🔥 MAIN TRADE UPDATE - Highly optimized for all game modes
void TradeManager::updateTrade(std::vector<Country>& countries, int currentYear, const Map& map, 
                              const TechnologyManager& techManager, News& news) {
    
    // 🚀 OPTIMIZATION: Only process trades every few years for performance
    if (currentYear - m_lastTradeYear < 2) return; // Process every 2 years
    m_lastTradeYear = currentYear;
    
    beginExportsYear(currentYear, countries.size());

    // Stage 1: Basic Barter (always available)
    processBarter(countries, currentYear, map, news);
    
    // Stage 2: Currency trades (requires Currency tech)
    processCurrencyTrades(countries, currentYear, techManager, map, news);
    
    // Stage 3: Markets (requires Markets tech)
    updateMarkets(countries, currentYear, techManager, map, news);
    
    // Stage 4: Trade routes (requires Navigation tech)
    establishTradeRoutes(countries, currentYear, techManager, map);
    establishShippingRoutes(countries, currentYear, techManager, map, news);
    processTradeRoutes(countries, currentYear, news);
    
    // Stage 5: Banking (requires Banking tech)
    updateBanking(countries, currentYear, techManager, news);
    
    // Apply trader specialization bonuses
    applyTraderBonuses(countries, techManager);
}

// 🚀 FAST FORWARD OPTIMIZATION - Batch process trades for speed
void TradeManager::fastForwardTrade(std::vector<Country>& countries, int startYear, int endYear, 
                                   const Map& map, const TechnologyManager& techManager, News& news) {
    
    // Process trades in 10-year chunks for optimal performance
    for (int year = startYear; year < endYear; year += 10) {
        beginExportsYear(year, countries.size());
        // Accelerated barter processing
        if (year % 5 == 0) { // Every 5 years during fast forward
            processBarter(countries, year, map, news);
        }
        
        // Accelerated currency and market processing
        if (year % 8 == 0) { // Every 8 years during fast forward
            processCurrencyTrades(countries, year, techManager, map, news);
            updateMarkets(countries, year, techManager, map, news);
        }
        
        // Trade routes and banking every 10 years
        if (year % 10 == 0) {
            establishTradeRoutes(countries, year, techManager, map);
            establishShippingRoutes(countries, year, techManager, map, news);
            processTradeRoutes(countries, year, news);
            updateBanking(countries, year, techManager, news);
        }
        
        applyTraderBonuses(countries, techManager);
    }
}

std::uint64_t TradeManager::makeU64PairKey(int a, int b) const {
    std::uint32_t lo = static_cast<std::uint32_t>(std::min(a, b));
    std::uint32_t hi = static_cast<std::uint32_t>(std::max(a, b));
    return (static_cast<std::uint64_t>(lo) << 32) | static_cast<std::uint64_t>(hi);
}

bool TradeManager::hasShippingRoute(int a, int b) const {
    if (a < 0 || b < 0) {
        return false;
    }
    if (a == b) {
        return false;
    }
    return (m_shippingRouteKeys.find(makeU64PairKey(a, b)) != m_shippingRouteKeys.end());
}

void TradeManager::ensureSeaNavGrid(const Map& map) {
    if (m_seaNav.ready) {
        return;
    }

    const auto& isLandGrid = map.getIsLandGrid();
    if (isLandGrid.empty() || isLandGrid[0].empty()) {
        return;
    }

    m_seaNav.step = kDefaultSeaNavStep;
    const int srcH = static_cast<int>(isLandGrid.size());
    const int srcW = static_cast<int>(isLandGrid[0].size());
    m_seaNav.width = (srcW + m_seaNav.step - 1) / m_seaNav.step;
    m_seaNav.height = (srcH + m_seaNav.step - 1) / m_seaNav.step;

    const int navW = m_seaNav.width;
    const int navH = m_seaNav.height;
    const int navN = navW * navH;
    m_seaNav.water.assign(static_cast<size_t>(navN), 0u);
    m_seaNav.componentId.assign(static_cast<size_t>(navN), -1);

    // Build strict water-only nav cells: a nav cell is navigable only if the entire underlying block is water.
    for (int ny = 0; ny < navH; ++ny) {
        const int y0 = ny * m_seaNav.step;
        const int y1 = std::min(srcH, (ny + 1) * m_seaNav.step);
        for (int nx = 0; nx < navW; ++nx) {
            const int x0 = nx * m_seaNav.step;
            const int x1 = std::min(srcW, (nx + 1) * m_seaNav.step);

            bool allWater = true;
            for (int y = y0; y < y1 && allWater; ++y) {
                const auto& row = isLandGrid[static_cast<size_t>(y)];
                for (int x = x0; x < x1; ++x) {
                    if (row[static_cast<size_t>(x)]) {
                        allWater = false;
                        break;
                    }
                }
            }
            if (allWater) {
                m_seaNav.water[static_cast<size_t>(ny * navW + nx)] = 1u;
            }
        }
    }

    // Connected components for fast "no route possible" rejection.
    int nextComp = 0;
    std::vector<int> q;
    q.reserve(4096);

    for (int idx = 0; idx < navN; ++idx) {
        if (m_seaNav.water[static_cast<size_t>(idx)] == 0u) {
            continue;
        }
        if (m_seaNav.componentId[static_cast<size_t>(idx)] != -1) {
            continue;
        }

        const int compId = nextComp++;
        m_seaNav.componentId[static_cast<size_t>(idx)] = compId;
        q.clear();
        q.push_back(idx);

        for (size_t qi = 0; qi < q.size(); ++qi) {
            const int cur = q[qi];
            const int cx = cur % navW;
            const int cy = cur / navW;

            const int nx[4] = { cx + 1, cx - 1, cx, cx };
            const int ny[4] = { cy, cy, cy + 1, cy - 1 };
            for (int k = 0; k < 4; ++k) {
                const int x = nx[k];
                const int y = ny[k];
                if (x < 0 || x >= navW || y < 0 || y >= navH) {
                    continue;
                }
                const int nidx = y * navW + x;
                if (m_seaNav.water[static_cast<size_t>(nidx)] == 0u) {
                    continue;
                }
                if (m_seaNav.componentId[static_cast<size_t>(nidx)] != -1) {
                    continue;
                }
                m_seaNav.componentId[static_cast<size_t>(nidx)] = compId;
                q.push_back(nidx);
            }
        }
    }

    m_seaNav.ready = true;
}

std::vector<Vector2i> TradeManager::findDockCandidates(const Vector2i& portCell, const Map& map) const {
    std::vector<Vector2i> docks;
    if (!m_seaNav.ready || m_seaNav.width <= 0 || m_seaNav.height <= 0) {
        return docks;
    }

    const auto& isLandGrid = map.getIsLandGrid();
    if (isLandGrid.empty() || isLandGrid[0].empty()) {
        return docks;
    }

    const int srcH = static_cast<int>(isLandGrid.size());
    const int srcW = static_cast<int>(isLandGrid[0].size());

    const int px = portCell.x;
    const int py = portCell.y;
    if (px < 0 || px >= srcW || py < 0 || py >= srcH) {
        return docks;
    }

    struct Candidate {
        int dist2;
        Vector2i nav;
    };
    std::vector<Candidate> cand;
    cand.reserve(64);

    const int R = kMaxDockSearchRadiusPx;
    const int r2 = R * R;
    const int y0 = clampi(py - R, 0, srcH - 1);
    const int y1 = clampi(py + R, 0, srcH - 1);
    const int x0 = clampi(px - R, 0, srcW - 1);
    const int x1 = clampi(px + R, 0, srcW - 1);

    std::unordered_set<int> seen;
    seen.reserve(static_cast<size_t>(kMaxDockCandidates) * 2u);

    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const int dx = x - px;
            const int dy = y - py;
            const int d2 = dx * dx + dy * dy;
            if (d2 > r2) {
                continue;
            }
            if (isLandGrid[static_cast<size_t>(y)][static_cast<size_t>(x)]) {
                continue;
            }

            const int navX = x / m_seaNav.step;
            const int navY = y / m_seaNav.step;
            if (navX < 0 || navX >= m_seaNav.width || navY < 0 || navY >= m_seaNav.height) {
                continue;
            }
            const int idx = navY * m_seaNav.width + navX;
            if (m_seaNav.water[static_cast<size_t>(idx)] == 0u) {
                continue;
            }
            if (!seen.insert(idx).second) {
                continue;
            }
            cand.push_back({d2, Vector2i(navX, navY)});
        }
    }

    if (cand.empty()) {
        return docks;
    }

    std::sort(cand.begin(), cand.end(), [](const Candidate& a, const Candidate& b) {
        return a.dist2 < b.dist2;
    });

    const int take = std::min(static_cast<int>(cand.size()), kMaxDockCandidates);
    docks.reserve(static_cast<size_t>(take));
    for (int i = 0; i < take; ++i) {
        docks.push_back(cand[static_cast<size_t>(i)].nav);
    }
    return docks;
}

bool TradeManager::findSeaPathCached(const Vector2i& startNav, const Vector2i& goalNav, std::vector<Vector2i>& outPath) {
    if (!m_seaNav.ready || m_seaNav.width <= 0 || m_seaNav.height <= 0) {
        return false;
    }

    const int navW = m_seaNav.width;
    const int aIdx = startNav.y * navW + startNav.x;
    const int bIdx = goalNav.y * navW + goalNav.x;
    if (aIdx < 0 || bIdx < 0) {
        return false;
    }

    const std::uint32_t lo = static_cast<std::uint32_t>(std::min(aIdx, bIdx));
    const std::uint32_t hi = static_cast<std::uint32_t>(std::max(aIdx, bIdx));
    const std::uint64_t key = (static_cast<std::uint64_t>(lo) << 32) | static_cast<std::uint64_t>(hi);

    auto it = m_seaPathCache.find(key);
    if (it != m_seaPathCache.end()) {
        outPath = it->second;
        if (aIdx != static_cast<int>(lo)) {
            std::reverse(outPath.begin(), outPath.end());
        }
        return !outPath.empty();
    }

    std::vector<Vector2i> path;
    if (!aStarSea(startNav, goalNav, path)) {
        return false;
    }

    m_seaPathCache.emplace(key, (aIdx == static_cast<int>(lo)) ? path : std::vector<Vector2i>(path.rbegin(), path.rend()));
    outPath = std::move(path);
    return !outPath.empty();
}

bool TradeManager::aStarSea(const Vector2i& startNav, const Vector2i& goalNav, std::vector<Vector2i>& outPath) {
    outPath.clear();
    if (!m_seaNav.ready) {
        return false;
    }

    const int navW = m_seaNav.width;
    const int navH = m_seaNav.height;
    if (startNav.x < 0 || startNav.x >= navW || startNav.y < 0 || startNav.y >= navH) {
        return false;
    }
    if (goalNav.x < 0 || goalNav.x >= navW || goalNav.y < 0 || goalNav.y >= navH) {
        return false;
    }

    const int startIdx = startNav.y * navW + startNav.x;
    const int goalIdx = goalNav.y * navW + goalNav.x;
    if (startIdx == goalIdx) {
        outPath.push_back(startNav);
        return true;
    }
    if (m_seaNav.water[static_cast<size_t>(startIdx)] == 0u || m_seaNav.water[static_cast<size_t>(goalIdx)] == 0u) {
        return false;
    }

    const int compA = m_seaNav.componentId[static_cast<size_t>(startIdx)];
    const int compB = m_seaNav.componentId[static_cast<size_t>(goalIdx)];
    if (compA < 0 || compB < 0 || compA != compB) {
        return false;
    }

    const int N = navW * navH;
    if (static_cast<int>(m_astarParent.size()) != N) {
        m_astarParent.assign(static_cast<size_t>(N), -1);
        m_astarG.assign(static_cast<size_t>(N), 0);
        m_astarStamp.assign(static_cast<size_t>(N), 0);
        m_astarCurStamp = 1;
    }

    // Stamp overflow guard.
    if (m_astarCurStamp == std::numeric_limits<int>::max()) {
        std::fill(m_astarStamp.begin(), m_astarStamp.end(), 0);
        m_astarCurStamp = 1;
    } else {
        ++m_astarCurStamp;
    }
    const int stamp = m_astarCurStamp;

    struct Node {
        int idx;
        int f;
        int g;
    };
    struct Cmp {
        bool operator()(const Node& a, const Node& b) const { return a.f > b.f; }
    };
    std::priority_queue<Node, std::vector<Node>, Cmp> open;

    auto heuristic = [&](int idx) -> int {
        const int x = idx % navW;
        const int y = idx / navW;
        return std::abs(x - goalNav.x) + std::abs(y - goalNav.y);
    };

    m_astarStamp[static_cast<size_t>(startIdx)] = stamp;
    m_astarParent[static_cast<size_t>(startIdx)] = -1;
    m_astarG[static_cast<size_t>(startIdx)] = 0;
    open.push({startIdx, heuristic(startIdx), 0});

    int expansions = 0;
    bool found = false;

    while (!open.empty()) {
        Node cur = open.top();
        open.pop();

        if (cur.idx == goalIdx) {
            found = true;
            break;
        }

        if (++expansions > kMaxAStarNodeExpansions) {
            return false;
        }

        const int cx = cur.idx % navW;
        const int cy = cur.idx / navW;

        const int nx[4] = { cx + 1, cx - 1, cx, cx };
        const int ny[4] = { cy, cy, cy + 1, cy - 1 };

        for (int k = 0; k < 4; ++k) {
            const int x = nx[k];
            const int y = ny[k];
            if (x < 0 || x >= navW || y < 0 || y >= navH) {
                continue;
            }
            const int nidx = y * navW + x;
            if (m_seaNav.water[static_cast<size_t>(nidx)] == 0u) {
                continue;
            }

            const int newG = cur.g + 1;
            if (m_astarStamp[static_cast<size_t>(nidx)] != stamp) {
                m_astarStamp[static_cast<size_t>(nidx)] = stamp;
                m_astarG[static_cast<size_t>(nidx)] = newG;
                m_astarParent[static_cast<size_t>(nidx)] = cur.idx;
                open.push({nidx, newG + heuristic(nidx), newG});
            } else if (newG < m_astarG[static_cast<size_t>(nidx)]) {
                m_astarG[static_cast<size_t>(nidx)] = newG;
                m_astarParent[static_cast<size_t>(nidx)] = cur.idx;
                open.push({nidx, newG + heuristic(nidx), newG});
            }
        }
    }

    if (!found) {
        return false;
    }

    // Reconstruct.
    std::vector<int> rev;
    rev.reserve(512);
    for (int at = goalIdx; at != -1; at = m_astarParent[static_cast<size_t>(at)]) {
        rev.push_back(at);
        if (at == startIdx) {
            break;
        }
    }
    if (rev.empty() || rev.back() != startIdx) {
        return false;
    }
    std::reverse(rev.begin(), rev.end());

    outPath.reserve(rev.size());
    for (int idx : rev) {
        outPath.emplace_back(idx % navW, idx / navW);
    }
    return !outPath.empty();
}

void TradeManager::fillRouteLengths(ShippingRoute& route) const {
    route.cumulativeLen.clear();
    route.totalLen = 0.0f;
    if (route.navPath.empty()) {
        return;
    }
    route.cumulativeLen.resize(route.navPath.size(), 0.0f);
    for (size_t i = 1; i < route.navPath.size(); ++i) {
        const int dx = route.navPath[i].x - route.navPath[i - 1].x;
        const int dy = route.navPath[i].y - route.navPath[i - 1].y;
        const float seg = std::sqrt(static_cast<float>(dx * dx + dy * dy)) * static_cast<float>(route.navStep);
        route.totalLen += seg;
        route.cumulativeLen[i] = route.totalLen;
    }
}

// 💱 BASIC BARTER SYSTEM - Foundation of all trade
void TradeManager::processBarter(std::vector<Country>& countries, int currentYear, const Map& map, News& news) {
    
    // Clean up expired offers
    m_activeOffers.erase(
        std::remove_if(m_activeOffers.begin(), m_activeOffers.end(),
            [currentYear](const TradeOffer& offer) { return currentYear > offer.validUntilYear; }),
        m_activeOffers.end()
    );
    
    // Generate new trade offers
    generateTradeOffers(countries, currentYear, map);
    
    // Execute valid trade offers
    executeTradeOffers(countries, currentYear, news);
}

void TradeManager::generateTradeOffers(std::vector<Country>& countries, int currentYear, const Map& map) {
    
    // Limit offer generation frequency for performance
    if (currentYear - m_lastBarterYear < 3) return;
    m_lastBarterYear = currentYear;
    
    std::uniform_real_distribution<> chanceDist(0.0, 1.0);
    std::uniform_int_distribution<> resourceDist(0, Resource::kTypeCount - 1);
    std::uniform_real_distribution<> amountDist(5.0, 25.0);
    std::uniform_int_distribution<> validityDist(5, 15); // Valid for 5-15 years
    
    for (size_t i = 0; i < countries.size(); ++i) {
        Country& country1 = countries[i];
        if (country1.getPopulation() <= 0) continue;
        
        // Countries make trade offers based on their resources
        if (chanceDist(m_rng) < 0.3) { // 30% chance per country per trade cycle
            
            // Find potential trading partners (neighbors only for basic barter)
            for (int neighborIndex : map.getAdjacentCountryIndicesPublic(country1.getCountryIndex())) {
                if (neighborIndex < 0 || neighborIndex >= static_cast<int>(countries.size())) continue;
                if (static_cast<int>(i) == neighborIndex) continue;
                Country& country2 = countries[static_cast<size_t>(neighborIndex)];
                if (country2.getCountryIndex() != neighborIndex) continue;
                if (country2.getPopulation() <= 0) continue;
                
                // Cannot trade with enemies
                if (country1.isAtWar() && std::find(country1.getEnemies().begin(), 
                    country1.getEnemies().end(), &country2) != country1.getEnemies().end()) continue;
                
                // Generate realistic trade offer
                Resource::Type offeredResource = Resource::kAllTypes[static_cast<size_t>(resourceDist(m_rng))];
                Resource::Type requestedResource = Resource::kAllTypes[static_cast<size_t>(resourceDist(m_rng))];
                
                // Don't trade the same resource
                if (offeredResource == requestedResource) continue;
                
                double offeredAmount = amountDist(m_rng);
                double requestedAmount = offeredAmount * calculateBarterhRatio(offeredResource, requestedResource);
                
                // Check if country has the resources to offer
                if (country1.getResourceManager().getResourceAmount(offeredResource) >= offeredAmount) {
                    
                    TradeOffer offer(static_cast<int>(i), neighborIndex, offeredResource, offeredAmount,
                                   requestedResource, requestedAmount, currentYear + validityDist(m_rng), m_nextOfferId++);
                    
                    m_activeOffers.push_back(offer);
                    
                    // Limit offers per country to prevent spam
                    if (m_activeOffers.size() > 100) break;
                }
            }
        }
    }
}

void TradeManager::executeTradeOffers(std::vector<Country>& countries, int currentYear, News& news) {
    
    std::uniform_real_distribution<> acceptanceDist(0.0, 1.0);
    
    for (auto it = m_activeOffers.begin(); it != m_activeOffers.end();) {
        const TradeOffer& offer = *it;
        
        if (!validateTradeOffer(offer, countries)) {
            it = m_activeOffers.erase(it);
            continue;
        }
        
        Country& fromCountry = countries[offer.fromCountryIndex];
        Country& toCountry = countries[offer.toCountryIndex];
        
        // Calculate acceptance probability based on country needs and relations
        double acceptanceChance = 0.4; // Base 40% acceptance rate
        
        // Increase chance if requesting country really needs the resource
        double demand = calculateResourceDemand(offer.requestedResource, toCountry);
        if (demand > 1.5) acceptanceChance += 0.3; // +30% if high demand
        
        // Trader countries are more likely to trade
        if (fromCountry.getType() == Country::Type::Trader) acceptanceChance += 0.2;
        if (toCountry.getType() == Country::Type::Trader) acceptanceChance += 0.2;
        
        // Pacifist countries are more cooperative
        if (fromCountry.getType() == Country::Type::Pacifist && 
            toCountry.getType() == Country::Type::Pacifist) acceptanceChance += 0.15;
        
        // Warmongers are less likely to trade
        if (fromCountry.getType() == Country::Type::Warmonger || 
            toCountry.getType() == Country::Type::Warmonger) acceptanceChance -= 0.15;

        // Cultural/language affinity affects trust and transaction friction.
        const double affinity = fromCountry.computeCulturalAffinity(toCountry);
        acceptanceChance += 0.22 * (affinity - 0.5);

        // Leaders with stronger diplomacy can sustain exchanges across borders.
        const double diplomacyBlend = 0.5 * fromCountry.getLeader().diplomacy + 0.5 * toCountry.getLeader().diplomacy;
        acceptanceChance += 0.10 * (diplomacyBlend - 0.5);

        acceptanceChance = std::clamp(acceptanceChance, 0.05, 0.95);
        
        if (acceptanceDist(m_rng) < acceptanceChance) {
            executeTradeOffer(offer, countries, news);
            recordTrade(offer.fromCountryIndex, offer.toCountryIndex, currentYear);
            it = m_activeOffers.erase(it);
        } else {
            ++it;
        }
    }
}

// 💰 CURRENCY SYSTEM - Advanced trading with gold
void TradeManager::processCurrencyTrades(std::vector<Country>& countries, int currentYear, 
                                        const TechnologyManager& techManager, const Map& map, News& news) {
    
    std::uniform_real_distribution<> chanceDist(0.0, 1.0);
    std::uniform_real_distribution<> priceDist(0.5, 3.0); // Price multiplier
    
    for (size_t i = 0; i < countries.size(); ++i) {
        Country& seller = countries[i];
        if (seller.getPopulation() <= 0) continue;
        
        // Only countries with currency tech can engage in currency trades
        if (!hasCurrency(seller, techManager)) continue;
        
        if (chanceDist(m_rng) < 0.25) { // 25% chance per country
            
            for (size_t j = 0; j < countries.size(); ++j) {
                if (i == j) continue;
                Country& buyer = countries[j];
                if (buyer.getPopulation() <= 0) continue;
                
                if (!hasCurrency(buyer, techManager)) continue;
                if (!canTradeDirectly(seller, buyer, map, techManager)) continue;
                
                // Trade surplus resources for gold
                for (Resource::Type resource : Resource::kAllTypes) {
                    double supply = calculateResourceSupply(resource, seller);
                    
                    if (supply > 1.2) { // Has surplus
                        double demand = calculateResourceDemand(resource, buyer);
                        
                        if (demand > 0.8 && buyer.getGold() > 5.0) { // Buyer needs it and has gold
                            const double affinity = seller.computeCulturalAffinity(buyer);
                            if (chanceDist(m_rng) > (0.28 + 0.72 * affinity)) {
                                continue;
                            }
                            double tradeAmount = std::min(10.0, supply - 1.0); // Trade surplus
                            double price = getResourcePrice(resource, seller) * priceDist(m_rng);
                            double totalCost = tradeAmount * price;
                            
                            if (buyer.getGold() >= totalCost && 
                                seller.getResourceManager().getResourceAmount(resource) >= tradeAmount) {
                                
                                // Execute currency trade
                                const_cast<ResourceManager&>(seller.getResourceManager()).consumeResource(resource, tradeAmount);
                                const_cast<ResourceManager&>(buyer.getResourceManager()).addResource(resource, tradeAmount);

                                addExportValue(static_cast<int>(i), totalCost);
                                
                                // Transfer gold
                                const_cast<Country&>(buyer).subtractGold(totalCost);
                                const_cast<Country&>(seller).addGold(totalCost);
                                
                                m_totalTradesCompleted++;
                                m_totalTradeValue += totalCost;
                                recordTrade(static_cast<int>(i), static_cast<int>(j), currentYear);
                                
                                // Add news event
                                const std::string resourceName = resourceTypeName(resource);
                                
                                news.addEvent("💰 CURRENCY TRADE: " + buyer.getName() + " purchases " + 
                                            std::to_string(static_cast<int>(tradeAmount)) + " " + resourceName + 
                                            " from " + seller.getName() + " for " + 
                                            std::to_string(static_cast<int>(totalCost)) + " gold!");
                                
                                break; // One trade per country pair per cycle
                            }
                        }
                    }
                }
            }
        }
    }
}

// 🏪 MARKET SYSTEM - Advanced economic hubs
void TradeManager::updateMarkets(std::vector<Country>& countries, int currentYear, 
                                const TechnologyManager& techManager, const Map& map, News& news) {
    
    // Create new markets where appropriate
    if (currentYear % 50 == 0) { // Check every 50 years
        for (const Country& country : countries) {
            if (country.getPopulation() > 50000 && hasMarkets(country, techManager)) {
                const sf::Vector2i anchor = country.deterministicTerritoryAnchor();
                // Check if there's already a market nearby
                bool hasNearbyMarket = false;
                for (const Market& market : m_markets) {
                    double distance = std::sqrt(std::pow(market.location.x - anchor.x, 2) +
                                              std::pow(market.location.y - anchor.y, 2));
                    if (distance < 100) { // Within 100 pixels
                        hasNearbyMarket = true;
                        break;
                    }
                }
                
                if (!hasNearbyMarket) {
                    Vector2i marketLocation(anchor.x, anchor.y);
                    createMarket(marketLocation, countries);
                    news.addEvent("🏪 MARKET ESTABLISHED: " + country.getName() + " establishes a major trading market!");
                }
            }
        }
    }
    
    // Update existing markets
    for (Market& market : m_markets) {
        updateMarketSupplyDemand(market, countries);
        updateMarketPrices();
        processMarketTrades(market, countries, news);
    }
}

// 🚢 TRADE ROUTES - Long-distance trade networks
void TradeManager::establishTradeRoutes(std::vector<Country>& countries, int currentYear,
                                       const TechnologyManager& techManager, const Map& map) {
    
    // Only establish new routes every 25 years for performance
    if (currentYear % 25 != 0) return;
    
    for (size_t i = 0; i < countries.size(); ++i) {
        const Country& country1 = countries[i];
        if (country1.getPopulation() <= 0) continue;
        
        // Only countries with navigation can establish long-distance routes
        if (!hasNavigation(country1, techManager)) continue;
        
        for (size_t j = i + 1; j < countries.size(); ++j) {
            const Country& country2 = countries[j];
            if (country2.getPopulation() <= 0) continue;
            
            if (!hasNavigation(country2, techManager)) continue;
            
            // Check if route already exists
            bool routeExists = false;
            for (const TradeRoute& route : m_tradeRoutes) {
                if ((route.fromCountryIndex == static_cast<int>(i) && route.toCountryIndex == static_cast<int>(j)) ||
                    (route.fromCountryIndex == static_cast<int>(j) && route.toCountryIndex == static_cast<int>(i))) {
                    routeExists = true;
                    break;
                }
            }
            
            if (!routeExists) {
                double distance = calculateTradeDistance(country1, country2);
                if (distance < 600) { // Maximum route distance (increased from 500)
                    double capacity = std::min(country1.getPopulation(), country2.getPopulation()) / 10000.0;
                    m_tradeRoutes.emplace_back(static_cast<int>(i), static_cast<int>(j), capacity, distance, currentYear);
                }
            }
        }
    }
}

void TradeManager::establishShippingRoutes(std::vector<Country>& countries, int currentYear,
                                          const TechnologyManager& techManager, const Map& map, News& news) {
    // Only establish new shipping routes occasionally for performance.
    if (currentYear % 25 != 0) {
        return;
    }

    ensureSeaNavGrid(map);
    if (!m_seaNav.ready) {
        return;
    }

    // Drop dead/invalid routes (ports can be removed when territory changes).
    if (!m_shippingRoutes.empty()) {
        for (auto& r : m_shippingRoutes) {
            if (!r.isActive) continue;
            if (r.fromCountryIndex < 0 || r.toCountryIndex < 0 ||
                r.fromCountryIndex >= static_cast<int>(countries.size()) ||
                r.toCountryIndex >= static_cast<int>(countries.size())) {
                r.isActive = false;
                continue;
            }
            const Country& a = countries[static_cast<size_t>(r.fromCountryIndex)];
            const Country& b = countries[static_cast<size_t>(r.toCountryIndex)];
            if (a.getPopulation() <= 0 || b.getPopulation() <= 0) {
                r.isActive = false;
                continue;
            }
            auto portStillExists = [](const Country& c, const Vector2i& p) {
                for (const auto& sp : c.getPorts()) {
                    if (sp.x == p.x && sp.y == p.y) {
                        return true;
                    }
                }
                return false;
            };
            if (!portStillExists(a, r.fromPortCell) || !portStillExists(b, r.toPortCell)) {
                r.isActive = false;
            }
        }
        // Rebuild membership set (keeps trade bonus correct even if many were invalidated).
        m_shippingRouteKeys.clear();
        for (const auto& r : m_shippingRoutes) {
            if (r.isActive) {
                m_shippingRouteKeys.insert(makeU64PairKey(r.fromCountryIndex, r.toCountryIndex));
            }
        }
    }

    if (countries.size() < 2) {
        return;
    }

    // Count existing active routes per country to enforce caps.
    std::vector<int> routeCounts(countries.size(), 0);
    for (const auto& r : m_shippingRoutes) {
        if (!r.isActive) continue;
        if (r.fromCountryIndex >= 0 && r.fromCountryIndex < static_cast<int>(routeCounts.size())) {
            routeCounts[static_cast<size_t>(r.fromCountryIndex)]++;
        }
        if (r.toCountryIndex >= 0 && r.toCountryIndex < static_cast<int>(routeCounts.size())) {
            routeCounts[static_cast<size_t>(r.toCountryIndex)]++;
        }
    }

    std::uniform_real_distribution<> chanceDist(0.0, 1.0);
    std::uniform_int_distribution<> pick(0, std::max(0, static_cast<int>(countries.size()) - 1));

    auto hasShipbuilding = [&](const Country& c) {
        return TechnologyManager::hasTech(techManager, c, 12); // Shipbuilding
    };
    auto hasNavigation = [&](const Country& c) {
        return TechnologyManager::hasTech(techManager, c, TechId::NAVIGATION); // Navigation
    };

    for (size_t i = 0; i < countries.size(); ++i) {
        const Country& a = countries[i];
        if (a.getPopulation() <= 0) continue;
        if (!hasNavigation(a) || !hasShipbuilding(a)) continue;
        if (a.getPorts().empty()) continue;

        // Limit attempts to spread load (not every country tries every establish tick).
        if (chanceDist(m_rng) > 0.35) {
            continue;
        }

        int majorCities = 0;
        for (const auto& city : a.getCities()) {
            if (city.isMajorCity()) majorCities++;
        }
        const int maxRoutesA = std::max(1, std::min(4, 1 + majorCities));
        if (routeCounts[i] >= maxRoutesA) {
            continue;
        }

        for (int attempt = 0; attempt < kMaxShippingPartnerAttempts; ++attempt) {
            const int j = pick(m_rng);
            if (j < 0 || j >= static_cast<int>(countries.size())) continue;
            if (j == static_cast<int>(i)) continue;

            const Country& b = countries[static_cast<size_t>(j)];
            if (b.getPopulation() <= 0) continue;
            if (!hasNavigation(b) || !hasShipbuilding(b)) continue;
            if (b.getPorts().empty()) continue;
            int majorCitiesB = 0;
            for (const auto& city : b.getCities()) {
                if (city.isMajorCity()) majorCitiesB++;
            }
            const int maxRoutesB = std::max(1, std::min(4, 1 + majorCitiesB));
            if (routeCounts[static_cast<size_t>(j)] >= maxRoutesB) continue;
            if (hasShippingRoute(static_cast<int>(i), j)) continue;

            // Choose the best port-to-port dock pair that is on the same sea component.
            Vector2i bestStartNav;
            Vector2i bestGoalNav;
            Vector2i bestPortA;
            Vector2i bestPortB;
            int bestScore = std::numeric_limits<int>::max();
            bool foundDockPair = false;

            for (const auto& pa : a.getPorts()) {
                const Vector2i portA(pa.x, pa.y);
                const auto docksA = findDockCandidates(portA, map);
                if (docksA.empty()) continue;

                for (const auto& pb : b.getPorts()) {
                    const Vector2i portB(pb.x, pb.y);
                    const auto docksB = findDockCandidates(portB, map);
                    if (docksB.empty()) continue;

                    for (const auto& da : docksA) {
                        const int aNavIdx = da.y * m_seaNav.width + da.x;
                        const int aComp = m_seaNav.componentId[static_cast<size_t>(aNavIdx)];
                        if (aComp < 0) continue;

                        for (const auto& db : docksB) {
                            const int bNavIdx = db.y * m_seaNav.width + db.x;
                            const int bComp = m_seaNav.componentId[static_cast<size_t>(bNavIdx)];
                            if (bComp != aComp) continue;

                            const int dx = da.x - db.x;
                            const int dy = da.y - db.y;
                            const int d2 = dx * dx + dy * dy;
                            if (d2 < bestScore) {
                                bestScore = d2;
                                bestStartNav = da;
                                bestGoalNav = db;
                                bestPortA = portA;
                                bestPortB = portB;
                                foundDockPair = true;
                            }
                        }
                    }
                }
            }

            if (!foundDockPair) {
                continue;
            }

            std::vector<Vector2i> navPath;
            if (!findSeaPathCached(bestStartNav, bestGoalNav, navPath)) {
                continue;
            }
            if (navPath.size() < 2) {
                continue;
            }

            ShippingRoute route;
            route.fromCountryIndex = static_cast<int>(i);
            route.toCountryIndex = j;
            route.fromPortCell = bestPortA;
            route.toPortCell = bestPortB;
            route.navStep = m_seaNav.step;
            route.navPath = std::move(navPath);
            route.establishedYear = currentYear;
            route.isActive = true;
            fillRouteLengths(route);

            m_shippingRoutes.push_back(std::move(route));
            m_shippingRouteKeys.insert(makeU64PairKey(static_cast<int>(i), j));
            routeCounts[i]++;
            routeCounts[static_cast<size_t>(j)]++;

            news.addEvent("🚢 SHIPPING ROUTE ESTABLISHED: " + a.getName() + " opens a shipping lane with " + b.getName() + ".");
            break; // Only one new route per country per establish tick.
        }
    }
}

void TradeManager::processTradeRoutes(std::vector<Country>& countries, int currentYear, News& news) {
    
    std::uniform_real_distribution<> tradeDist(0.0, 1.0);
    
    for (const TradeRoute& route : m_tradeRoutes) {
        if (!route.isActive) continue;
        
        if (route.fromCountryIndex >= static_cast<int>(countries.size()) || 
            route.toCountryIndex >= static_cast<int>(countries.size())) continue;
        
        Country& country1 = countries[route.fromCountryIndex];
        Country& country2 = countries[route.toCountryIndex];
        
        if (country1.getPopulation() <= 0 || country2.getPopulation() <= 0) continue;

        const double shipBonus = hasShippingRoute(route.fromCountryIndex, route.toCountryIndex) ? 1.25 : 1.0;

        // Trade along route based on capacity and efficiency
        bool tradeHappened = false;
        if (tradeDist(m_rng) < 0.4) { // 40% chance per route per cycle
            
            // Find what each country can export
            for (Resource::Type resource : Resource::kAllTypes) {
                
                double supply1 = calculateResourceSupply(resource, country1);
                double demand2 = calculateResourceDemand(resource, country2);
                
                if (supply1 > 1.2 && demand2 > 0.8) {
                    double tradeAmount = std::min(route.capacity * route.efficiency * shipBonus, 
                                                std::min(supply1 - 1.0, demand2 * 0.5));
                    
                    if (tradeAmount > 0.5 && 
                        country1.getResourceManager().getResourceAmount(resource) >= tradeAmount) {
                        
                        const_cast<ResourceManager&>(country1.getResourceManager()).consumeResource(resource, tradeAmount);
                        const_cast<ResourceManager&>(country2.getResourceManager()).addResource(resource, tradeAmount);
                        
                        m_totalTradesCompleted++;
                        const double value = tradeAmount * getResourcePrice(resource, country1);
                        m_totalTradeValue += value;
                        addExportValue(route.fromCountryIndex, value);
                        tradeHappened = true;
                    }
                }
            }
        }

        if (tradeHappened) {
            recordTrade(route.fromCountryIndex, route.toCountryIndex, currentYear);
        }
    }
}

// 🏦 BANKING SYSTEM - Advanced financial instruments
void TradeManager::updateBanking(std::vector<Country>& countries, int currentYear,
                                const TechnologyManager& techManager, News& news) {
    
    // Create banks in major countries
    if (currentYear % 100 == 0) { // Every 100 years
        for (size_t i = 0; i < countries.size(); ++i) {
            const Country& country = countries[i];
            if (country.getPopulation() > 100000 && hasBanking(country, techManager)) {
                
                // Check if country already has a bank
                bool hasBank = false;
                for (const Bank& bank : m_banks) {
                    if (std::find(bank.countryDeposits.begin(), bank.countryDeposits.end(), 
                                std::make_pair(static_cast<int>(i), 0.0)) != bank.countryDeposits.end()) {
                        hasBank = true;
                        break;
                    }
                }
                
                if (!hasBank) {
                    m_banks.emplace_back(currentYear);
                    news.addEvent("🏦 BANKING: " + country.getName() + " establishes a central bank!");
                }
            }
        }
    }
    
    // Process banking operations (interest, loans, etc.)
    for (Bank& bank : m_banks) {
        // Simple interest calculation
        for (auto& pair : bank.countryDeposits) {
            pair.second *= (1.0 + bank.interestRate);
        }
    }
}

// 🏪 TRADER SPECIALIZATION - Bonus for trader countries
void TradeManager::applyTraderBonuses(std::vector<Country>& countries, const TechnologyManager& techManager) {
    
    for (Country& country : countries) {
        if (country.getType() == Country::Type::Trader) {
            
            // Traders get bonus gold from markets and trade routes
            double tradeBonus = 0.0;
            
            // Bonus from nearby markets
            for (const Market& market : m_markets) {
                for (int participantIndex : market.participatingCountries) {
                    if (participantIndex == country.getCountryIndex()) {
                        tradeBonus += 2.0; // +2 gold per market per year
                        break;
                    }
                }
            }
            
            // Bonus from trade routes
            for (const TradeRoute& route : m_tradeRoutes) {
                if (route.fromCountryIndex == country.getCountryIndex() || 
                    route.toCountryIndex == country.getCountryIndex()) {
                    tradeBonus += 1.0; // +1 gold per route per year
                }
            }
            
            // Apply trader bonus gold
            const_cast<Country&>(country).addGold(tradeBonus);
            
            // Traders also get better resource generation through trade networks
            if (hasCurrency(country, techManager)) {
                // Traders with currency tech get additional gold income
                const_cast<Country&>(country).addGold(1.0); // +1 gold per year for currency-enabled traders
            }
        }
    }
}

// ⚙️ UTILITY FUNCTIONS

bool TradeManager::areCountriesNeighbors(const Country& country1, const Country& country2, const Map& map) const {
    return map.areNeighbors(country1, country2);
}

bool TradeManager::canTradeDirectly(const Country& from, const Country& to, const Map& map, 
                                   const TechnologyManager& techManager) const {
    
    // Basic barter requires neighbors
    if (areCountriesNeighbors(from, to, map)) return true;
    
    // Navigation allows long-distance trade
    if (hasNavigation(from, techManager) && hasNavigation(to, techManager)) return true;
    
    return false;
}

double TradeManager::calculateTradeDistance(const Country& from, const Country& to) const {
    
    if (from.getBoundaryPixels().empty() || to.getBoundaryPixels().empty()) return 1000.0;
    
    sf::Vector2i fromBoundary = from.deterministicTerritoryAnchor();
    sf::Vector2i toBoundary = to.deterministicTerritoryAnchor();
    Vector2i fromCenter(fromBoundary.x, fromBoundary.y);
    Vector2i toCenter(toBoundary.x, toBoundary.y);
    
    return std::sqrt(std::pow(fromCenter.x - toCenter.x, 2) + std::pow(fromCenter.y - toCenter.y, 2));
}

double TradeManager::getResourcePrice(Resource::Type resource, const Country& country) const {
    
    // Base prices with supply/demand modifiers
    double basePrice = resourceBasePrice(resource);
    
    // Adjust based on country's supply/demand
    double supply = calculateResourceSupply(resource, country);
    double demand = calculateResourceDemand(resource, country);
    
    if (supply > demand) basePrice *= 0.8; // Surplus = lower price
    else if (demand > supply) basePrice *= 1.3; // Shortage = higher price
    
    return basePrice;
}

double TradeManager::getTradeScore(int countryA, int countryB, int currentYear) const {
    if (countryA == countryB) {
        return 0.0;
    }

    long long key = makePairKey(countryA, countryB);
    auto it = m_tradeRelations.find(key);
    if (it == m_tradeRelations.end()) {
        return 0.0;
    }

    const TradeRelation& relation = it->second;
    int yearsElapsed = currentYear - relation.lastYear;
    if (yearsElapsed <= 0) {
        return relation.score;
    }

    double decay = std::pow(0.92, static_cast<double>(yearsElapsed));
    return relation.score * decay;
}

// 🧠 TECHNOLOGY CHECKS

bool TradeManager::hasCurrency(const Country& country, const TechnologyManager& techManager) const {
    // Check for Currency technology (ID 15)
    return TechnologyManager::hasTech(techManager, country, TechId::CURRENCY);
}

bool TradeManager::hasMarkets(const Country& country, const TechnologyManager& techManager) const {
    // Check for Markets technology (ID 35)
    return TechnologyManager::hasTech(techManager, country, 35);
}

bool TradeManager::hasNavigation(const Country& country, const TechnologyManager& techManager) const {
    // Check for Navigation technology (ID 43)
    return TechnologyManager::hasTech(techManager, country, TechId::NAVIGATION);
}

bool TradeManager::hasBanking(const Country& country, const TechnologyManager& techManager) const {
    // Check for Banking technology (ID 34)
    return TechnologyManager::hasTech(techManager, country, TechId::BANKING);
}

// 📊 HELPER FUNCTIONS

double TradeManager::calculateBarterhRatio(Resource::Type from, Resource::Type to) const {
    
    // Simple exchange ratios based on resource value
    std::unordered_map<Resource::Type, double> resourceValues = {
        {Resource::Type::FOOD, 1.0},
        {Resource::Type::SALT, 3.0},
        {Resource::Type::COAL, 2.0},
        {Resource::Type::IRON, 4.0},
        {Resource::Type::HORSES, 5.0},
        {Resource::Type::GOLD, 10.0},
        {Resource::Type::COPPER, 4.5},
        {Resource::Type::TIN, 8.0},
        {Resource::Type::CLAY, 2.0}
    };
    
    return resourceValues[to] / resourceValues[from];
}

long long TradeManager::makePairKey(int countryA, int countryB) const {
    if (countryA > countryB) {
        std::swap(countryA, countryB);
    }
    return (static_cast<long long>(countryA) << 32) | static_cast<unsigned int>(countryB);
}

void TradeManager::recordTrade(int countryA, int countryB, int currentYear) {
    if (countryA == countryB) {
        return;
    }

    long long key = makePairKey(countryA, countryB);
    TradeRelation& relation = m_tradeRelations[key];
    if (relation.lastYear != 0) {
        relation.score = getTradeScore(countryA, countryB, currentYear);
    }
    relation.score += 1.0;
    relation.lastYear = currentYear;
}

bool TradeManager::validateTradeOffer(const TradeOffer& offer, const std::vector<Country>& countries) const {
    
    if (offer.fromCountryIndex >= static_cast<int>(countries.size()) || 
        offer.toCountryIndex >= static_cast<int>(countries.size())) return false;
    
    const Country& fromCountry = countries[offer.fromCountryIndex];
    const Country& toCountry = countries[offer.toCountryIndex];
    
    if (fromCountry.getPopulation() <= 0 || toCountry.getPopulation() <= 0) return false;
    
    // Check if offering country still has the resources
    return fromCountry.getResourceManager().getResourceAmount(offer.offeredResource) >= offer.offeredAmount;
}

void TradeManager::executeTradeOffer(const TradeOffer& offer, std::vector<Country>& countries, News& news) {
    
    Country& fromCountry = countries[offer.fromCountryIndex];
    Country& toCountry = countries[offer.toCountryIndex];
    
    // Execute the trade
    const_cast<ResourceManager&>(fromCountry.getResourceManager()).consumeResource(offer.offeredResource, offer.offeredAmount);
    const_cast<ResourceManager&>(toCountry.getResourceManager()).addResource(offer.offeredResource, offer.offeredAmount);
    
    const_cast<ResourceManager&>(toCountry.getResourceManager()).consumeResource(offer.requestedResource, offer.requestedAmount);
    const_cast<ResourceManager&>(fromCountry.getResourceManager()).addResource(offer.requestedResource, offer.requestedAmount);

    // Count exports for both directions in this barter exchange.
    // Use each country's own price function as a local value proxy.
    addExportValue(offer.fromCountryIndex, offer.offeredAmount * getResourcePrice(offer.offeredResource, fromCountry));
    addExportValue(offer.toCountryIndex, offer.requestedAmount * getResourcePrice(offer.requestedResource, toCountry));
    
    m_totalTradesCompleted++;
    m_totalTradeValue += offer.offeredAmount + offer.requestedAmount;
    
    // Add news event
    const std::string offeredName = resourceTypeName(offer.offeredResource);
    const std::string requestedName = resourceTypeName(offer.requestedResource);
    
    news.addEvent("📦 TRADE: " + fromCountry.getName() + " trades " + 
                 std::to_string(static_cast<int>(offer.offeredAmount)) + " " + offeredName + 
                 " for " + std::to_string(static_cast<int>(offer.requestedAmount)) + " " + requestedName + 
                 " with " + toCountry.getName() + "!");
}

double TradeManager::calculateResourceDemand(Resource::Type resource, const Country& country) const {
    
    // Base demand based on population and country type
    double baseDemand = static_cast<double>(country.getPopulation()) / 100000.0; // Scale to reasonable numbers
    const double pop = std::max(1.0, static_cast<double>(country.getPopulation()));
    const double urbanizationProxy = std::clamp((std::log10(pop + 1.0) - 4.2) / 2.0, 0.0, 1.2);
    const double goodsProxy = std::clamp(country.getGDP() / (pop * 450.0), 0.0, 2.5);
    const double militaryProxy = std::clamp(country.getMilitaryStrength() / (pop * 0.0025), 0.0, 2.5);
    const double infraProxy = std::clamp(country.getInfraSpendingShare() + country.getConnectivityIndex(), 0.0, 2.0);
    const auto& macro = country.getMacroEconomy();
    const double nonFoodScarcity = std::clamp(macro.lastNonFoodShortage / (0.00025 * pop + 1.0), 0.0, 2.5);
    const double energyStress = std::clamp((1.0 - macro.foodSecurity) * 0.5 + nonFoodScarcity * 0.4, 0.0, 2.0);
    
    // Adjust based on resource type and country needs
    switch(resource) {
        case Resource::Type::FOOD:
            baseDemand *= 2.0 + 1.2 * std::clamp(1.0 - macro.foodSecurity, 0.0, 1.0); // Everyone needs food
            break;
        case Resource::Type::HORSES:
            if (country.getType() == Country::Type::Warmonger) baseDemand *= 1.5;
            break;
        case Resource::Type::IRON:
            baseDemand *= 0.9 + 0.7 * goodsProxy;
            if (country.getType() == Country::Type::Warmonger) baseDemand *= 1.8;
            break;
        case Resource::Type::COAL:
            baseDemand *= 0.9 + 0.5 * urbanizationProxy + 0.6 * goodsProxy + 0.7 * energyStress;
            break;
        case Resource::Type::COPPER:
            baseDemand *= 0.9 + 0.8 * goodsProxy + 0.45 * militaryProxy + 0.8 * nonFoodScarcity;
            break;
        case Resource::Type::TIN:
            baseDemand *= 0.65 + 0.95 * goodsProxy + 0.35 * militaryProxy + 0.9 * nonFoodScarcity;
            break;
        case Resource::Type::CLAY:
            baseDemand *= 0.8 + 0.75 * urbanizationProxy + 0.5 * infraProxy + 0.35 * nonFoodScarcity;
            break;
        case Resource::Type::GOLD:
            if (country.getType() == Country::Type::Trader) baseDemand *= 1.5;
            break;
        default:
            break;
    }
    
    // Adjust based on current resources
    double currentAmount = country.getResourceManager().getResourceAmount(resource);
    if (currentAmount < baseDemand * 0.5) baseDemand *= 1.5; // High demand if low supply
    
    return baseDemand;
}

double TradeManager::calculateResourceSupply(Resource::Type resource, const Country& country) const {
    
    double currentAmount = country.getResourceManager().getResourceAmount(resource);
    double demand = calculateResourceDemand(resource, country);
    
    return currentAmount / std::max(0.1, demand); // Ratio of supply to demand
}

std::vector<int> TradeManager::findNearbyCountries(const Country& country, const Map& map, int maxDistance) const {
    
    std::vector<int> nearbyCountries;
    // This would need to be implemented with proper map distance calculations
    // For now, return empty vector as placeholder
    return nearbyCountries;
}

void TradeManager::createMarket(Vector2i location, const std::vector<Country>& countries) {
    
    Market newMarket(location);
    
    // Add nearby countries as participants
    for (size_t i = 0; i < countries.size(); ++i) {
        const Country& country = countries[i];
        if (country.getPopulation() > 10000) {
            // Simple distance check - would need proper implementation
            newMarket.participatingCountries.push_back(static_cast<int>(i));
        }
    }
    
    m_markets.push_back(newMarket);
}

void TradeManager::updateMarketSupplyDemand(Market& market, const std::vector<Country>& countries) {
    
    // Reset supply and demand
    for (auto& pair : market.supply) pair.second = 0.0;
    for (auto& pair : market.demand) pair.second = 0.0;
    
    // Calculate total supply and demand from participating countries
    for (int countryIndex : market.participatingCountries) {
        if (countryIndex >= static_cast<int>(countries.size())) continue;
        
        const Country& country = countries[countryIndex];
        if (country.getPopulation() <= 0) continue;
        
        for (Resource::Type resource : Resource::kAllTypes) {
            market.supply[resource] += calculateResourceSupply(resource, country);
            market.demand[resource] += calculateResourceDemand(resource, country);
        }
    }
}

void TradeManager::updateMarketPrices() {
    
    for (Market& market : m_markets) {
        for (auto& pair : market.prices) {
            Resource::Type resource = pair.first;
            double& price = pair.second;
            double supply = market.supply[resource];
            double demand = market.demand[resource];
            
            if (demand > 0) {
                double ratio = supply / demand;
                if (ratio < 0.8) price *= 1.1; // Increase price if low supply
                else if (ratio > 1.5) price *= 0.95; // Decrease price if high supply
                
                // Keep prices within reasonable bounds
                price = std::max(0.1, std::min(price, 100.0));
            }
        }
    }
}

void TradeManager::processMarketTrades(Market& market, std::vector<Country>& countries, News& news) {
    
    // Process automatic trades through market mechanisms
    std::uniform_real_distribution<> tradeDist(0.0, 1.0);
    
    if (tradeDist(m_rng) < 0.3) { // 30% chance per market per cycle
        
        for (Resource::Type resource : Resource::kAllTypes) {
            
            if (market.supply[resource] > market.demand[resource] * 1.2) {
                // Market has surplus - distribute to countries with high demand
                double surplus = market.supply[resource] - market.demand[resource];
                double pricePerUnit = market.prices[resource];
                
                m_totalTradesCompleted++;
                m_totalTradeValue += surplus * pricePerUnit;
                
                // This would need more complex implementation to actually transfer resources
            }
        }
    }
}

void TradeManager::printTradeStatistics(int currentYear) const {
    
    std::cout << "📊 TRADE STATISTICS for year " << currentYear << ":" << std::endl;
    std::cout << "   Total Trades Completed: " << m_totalTradesCompleted << std::endl;
    std::cout << "   Total Trade Value: " << static_cast<int>(m_totalTradeValue) << std::endl;
    std::cout << "   Active Trade Offers: " << m_activeOffers.size() << std::endl;
    std::cout << "   Trade Routes: " << m_tradeRoutes.size() << std::endl;
    std::cout << "   Markets: " << m_markets.size() << std::endl;
    std::cout << "   Banks: " << m_banks.size() << std::endl;
}
