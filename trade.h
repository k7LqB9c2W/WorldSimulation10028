// trade.h
#pragma once

#include <vector>
#include <unordered_map>
#include <string>
#include <random>
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
    void executeTradeOffers(std::vector<Country>& countries, News& news);
    
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
    void processTradeRoutes(std::vector<Country>& countries, News& news);
    
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
    
    // Technology checks
    bool hasCurrency(const Country& country, const TechnologyManager& techManager) const;
    bool hasMarkets(const Country& country, const TechnologyManager& techManager) const;
    bool hasNavigation(const Country& country, const TechnologyManager& techManager) const;
    bool hasBanking(const Country& country, const TechnologyManager& techManager) const;
    
    // Statistics and debugging
    void printTradeStatistics(int currentYear) const;
    int getTotalTrades() const { return m_totalTradesCompleted; }
    double getTotalTradeValue() const { return m_totalTradeValue; }

private:
    // Trade offers and routes
    std::vector<TradeOffer> m_activeOffers;
    std::vector<TradeRoute> m_tradeRoutes;
    std::vector<Market> m_markets;
    std::vector<Bank> m_banks;
    
    // Trade statistics
    int m_totalTradesCompleted = 0;
    double m_totalTradeValue = 0.0;
    int m_nextOfferId = 1;
    
    // Optimization for fast forward
    int m_lastBarterYear = -5000;
    int m_lastMarketUpdateYear = -5000;
    
    // Random number generation
    mutable std::mt19937 m_rng;
    
    // Helper functions
    double calculateBarterhRatio(Resource::Type from, Resource::Type to) const;
    bool validateTradeOffer(const TradeOffer& offer, const std::vector<Country>& countries) const;
    void executeTradeOffer(const TradeOffer& offer, std::vector<Country>& countries, News& news);
    
    // Resource demand/supply calculations
    double calculateResourceDemand(Resource::Type resource, const Country& country) const;
    double calculateResourceSupply(Resource::Type resource, const Country& country) const;
    std::vector<int> findNearbyCountries(const Country& country, const Map& map, int maxDistance = 3) const;
    
    // Market operations
    void updateMarketSupplyDemand(Market& market, const std::vector<Country>& countries);
    void processMarketTrades(Market& market, std::vector<Country>& countries, News& news);
};
