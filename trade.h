// trade.h
#pragma once

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <random>
#include <cstdint>
#include "resource.h"

// Forward declarations
class Country;
class News;
class TechnologyManager;
class Map;

// Simple 2D vector structure to avoid SFML dependency in header
struct Vector2i {
    int x, y;
    Vector2i(int x = 0, int y = 0) : x(x), y(y) {}
};

// Trade offer structure for barter system
struct TradeOffer {
    int fromCountryIndex;
    int toCountryIndex;
    Resource::Type offeredResource;
    double offeredAmount;
    Resource::Type requestedResource;
    double requestedAmount;
    double goldOffered = 0.0;      // For currency-based trades
    double goldRequested = 0.0;
    int validUntilYear;
    bool isCurrencyTrade = false;  // True if involves gold
    int offerId;
    
    TradeOffer(int from, int to, Resource::Type offered, double offeredAmt, 
               Resource::Type requested, double requestedAmt, int validYear, int id)
        : fromCountryIndex(from), toCountryIndex(to), offeredResource(offered), 
          offeredAmount(offeredAmt), requestedResource(requested), 
          requestedAmount(requestedAmt), validUntilYear(validYear), offerId(id) {}
};

// Trade route structure for advanced trade system
struct TradeRoute {
    int fromCountryIndex;
    int toCountryIndex;
    double capacity;          // Max resources per year
    double distance;          // Distance penalty
    double efficiency = 1.0;  // Efficiency from infrastructure
    bool isActive = true;
    int establishedYear;
    
    TradeRoute(int from, int to, double cap, double dist, int year)
        : fromCountryIndex(from), toCountryIndex(to), capacity(cap), 
          distance(dist), establishedYear(year) {}
};

// Shipping route structure for water-only port-to-port logistics.
struct ShippingRoute {
    int fromCountryIndex = -1;
    int toCountryIndex = -1;
    Vector2i fromPortCell;
    Vector2i toPortCell;

    // Downsampled navigation grid parameters used for navPath.
    int navStep = 6;

    // Path over nav grid coordinates (water-only). Each node is a cell in the nav grid.
    std::vector<Vector2i> navPath;
    std::vector<float> cumulativeLen; // same size as navPath; cumulativeLen[0]=0; totalLen=cumulativeLen.back()
    float totalLen = 0.0f;

    bool isActive = true;
    int establishedYear = -5000;
};

// Market structure for advanced economy
struct Market {
    std::unordered_map<Resource::Type, double> supply;
    std::unordered_map<Resource::Type, double> demand;
    std::unordered_map<Resource::Type, double> prices;
    std::vector<int> participatingCountries;
    Vector2i location; // Grid location of market
    double influence = 1.0; // Market influence radius
    
    Market(Vector2i loc) : location(loc) {
        // Initialize base prices for all resources
        prices[Resource::Type::FOOD] = 1.0;
        prices[Resource::Type::HORSES] = 5.0;
        prices[Resource::Type::SALT] = 3.0;
        prices[Resource::Type::IRON] = 4.0;
        prices[Resource::Type::COAL] = 2.0;
        prices[Resource::Type::GOLD] = 10.0;
    }
};

// Financial institution for late-game economics
struct Bank {
    double totalDeposits = 0.0;
    double totalLoans = 0.0;
    double interestRate = 0.05; // 5% default
    std::unordered_map<int, double> countryDeposits;
    std::unordered_map<int, double> countryLoans;
    int establishedYear;
    
    Bank(int year) : establishedYear(year) {}
};

class TradeManager {
public:
    TradeManager();
    
    // Core trade system updates
    void updateTrade(std::vector<Country>& countries, int currentYear, const Map& map, 
                     const TechnologyManager& techManager, News& news);
    
    // Fast forward optimization
    void fastForwardTrade(std::vector<Country>& countries, int startYear, int endYear, 
                         const Map& map, const TechnologyManager& techManager, News& news);
    
    // Basic barter system (no tech requirements)
    void processBarter(std::vector<Country>& countries, int currentYear, const Map& map, News& news);
    void generateTradeOffers(std::vector<Country>& countries, int currentYear, const Map& map);
    void executeTradeOffers(std::vector<Country>& countries, int currentYear, News& news);
    
    // Currency system (requires Currency tech)
    void processCurrencyTrades(std::vector<Country>& countries, int currentYear, 
                              const TechnologyManager& techManager, const Map& map, News& news);
    
    // Market system (requires Markets tech)
    void updateMarkets(std::vector<Country>& countries, int currentYear, 
                      const TechnologyManager& techManager, const Map& map, News& news);
    void createMarket(Vector2i location, const std::vector<Country>& countries);
    void updateMarketPrices();
    
    // Trade routes (requires Navigation tech)
    void establishTradeRoutes(std::vector<Country>& countries, int currentYear,
                             const TechnologyManager& techManager, const Map& map);
    void processTradeRoutes(std::vector<Country>& countries, int currentYear, News& news);

    // Shipping routes (water-only, requires Shipbuilding + Navigation tech)
    void establishShippingRoutes(std::vector<Country>& countries, int currentYear,
                                 const TechnologyManager& techManager, const Map& map, News& news);

    // Financial institutions (requires Banking tech)
    void updateBanking(std::vector<Country>& countries, int currentYear,
                      const TechnologyManager& techManager, News& news);
    
    // Trader specialization bonuses
    void applyTraderBonuses(std::vector<Country>& countries, const TechnologyManager& techManager);
    
    // Utility functions
    bool areCountriesNeighbors(const Country& country1, const Country& country2, const Map& map) const;
    bool canTradeDirectly(const Country& from, const Country& to, const Map& map, 
                         const TechnologyManager& techManager) const;
    double calculateTradeDistance(const Country& from, const Country& to) const;
    double getResourcePrice(Resource::Type resource, const Country& country) const;
    double getTradeScore(int countryA, int countryB, int currentYear) const;
    
    // Technology checks
    bool hasCurrency(const Country& country, const TechnologyManager& techManager) const;
    bool hasMarkets(const Country& country, const TechnologyManager& techManager) const;
    bool hasNavigation(const Country& country, const TechnologyManager& techManager) const;
    bool hasBanking(const Country& country, const TechnologyManager& techManager) const;
    
    // Statistics and debugging
    void printTradeStatistics(int currentYear) const;
    int getTotalTrades() const { return m_totalTradesCompleted; }
    double getTotalTradeValue() const { return m_totalTradeValue; }
    const std::vector<TradeRoute>& getTradeRoutes() const { return m_tradeRoutes; }
    const std::vector<ShippingRoute>& getShippingRoutes() const { return m_shippingRoutes; }

    // Per-country export value proxy derived from executed TradeManager transfers (barter/currency/routes).
    // Values are only updated when trade is processed; use the year getter to see how fresh they are.
    int getLastCountryExportsYear() const { return m_lastCountryExportsYear; }
    const std::vector<double>& getLastCountryExports() const { return m_countryExportsValue; }

private:
    struct TradeRelation {
        double score = 0.0;
        int lastYear = 0;
    };

    struct SeaNavGrid {
        int step = 6;
        int width = 0;
        int height = 0;
        bool ready = false;
        std::vector<std::uint8_t> water; // 1 if navigable water
        std::vector<int> componentId;    // same size as water; -1 if not water
    };

    // Trade offers and routes
    std::vector<TradeOffer> m_activeOffers;
    std::vector<TradeRoute> m_tradeRoutes;
    std::vector<ShippingRoute> m_shippingRoutes;
    std::vector<Market> m_markets;
    std::vector<Bank> m_banks;
    
    // Trade statistics
    int m_totalTradesCompleted = 0;
    double m_totalTradeValue = 0.0;
    int m_nextOfferId = 1;
    std::unordered_map<long long, TradeRelation> m_tradeRelations;
    
    // Optimization for fast forward
    int m_lastBarterYear = -5000;
    int m_lastMarketUpdateYear = -5000;

    // Export value proxy (in "gold-equivalent" units) for the last processed trade year.
    int m_lastCountryExportsYear = -5000;
    std::vector<double> m_countryExportsValue;

    // Shipping route membership for O(1) trade bonuses.
    std::unordered_set<std::uint64_t> m_shippingRouteKeys;

    // Sea navigation (downsampled, cached).
    SeaNavGrid m_seaNav;
    std::unordered_map<std::uint64_t, std::vector<Vector2i>> m_seaPathCache;

    // A* scratch (reused to avoid allocations).
    std::vector<int> m_astarParent;
    std::vector<int> m_astarG;
    std::vector<int> m_astarStamp;
    int m_astarCurStamp = 1;

    // Random number generation
    mutable std::mt19937 m_rng;

    // Helper functions
    void beginExportsYear(int year, size_t countryCount);
    void addExportValue(int exporterIndex, double value);
    double calculateBarterhRatio(Resource::Type from, Resource::Type to) const;
    bool validateTradeOffer(const TradeOffer& offer, const std::vector<Country>& countries) const;
    void executeTradeOffer(const TradeOffer& offer, std::vector<Country>& countries, News& news);
    long long makePairKey(int countryA, int countryB) const;
    void recordTrade(int countryA, int countryB, int currentYear);
    
    // Resource demand/supply calculations
    double calculateResourceDemand(Resource::Type resource, const Country& country) const;
    double calculateResourceSupply(Resource::Type resource, const Country& country) const;
    std::vector<int> findNearbyCountries(const Country& country, const Map& map, int maxDistance = 3) const;
    
    // Market operations
    void updateMarketSupplyDemand(Market& market, const std::vector<Country>& countries);
    void processMarketTrades(Market& market, std::vector<Country>& countries, News& news);

    // Shipping helpers
    void ensureSeaNavGrid(const Map& map);
    std::vector<Vector2i> findDockCandidates(const Vector2i& portCell, const Map& map) const;
    bool findSeaPathCached(const Vector2i& startNav, const Vector2i& goalNav, std::vector<Vector2i>& outPath);
    bool aStarSea(const Vector2i& startNav, const Vector2i& goalNav, std::vector<Vector2i>& outPath);
    void fillRouteLengths(ShippingRoute& route) const;
    std::uint64_t makeU64PairKey(int a, int b) const;
    bool hasShippingRoute(int a, int b) const;
};
