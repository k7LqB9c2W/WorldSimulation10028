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

TradeManager::TradeManager() : m_rng(std::random_device{}()) {
    std::cout << "🏪 Trade & Economic Exchange Framework initialized!" << std::endl;
}

// 🔥 MAIN TRADE UPDATE - Highly optimized for all game modes
void TradeManager::updateTrade(std::vector<Country>& countries, int currentYear, const Map& map, 
                              const TechnologyManager& techManager, News& news) {
    
    // 🚀 OPTIMIZATION: Only process trades every few years for performance
    static int lastTradeYear = -5000;
    if (currentYear - lastTradeYear < 2) return; // Process every 2 years
    lastTradeYear = currentYear;
    
    // Stage 1: Basic Barter (always available)
    processBarter(countries, currentYear, map, news);
    
    // Stage 2: Currency trades (requires Currency tech)
    processCurrencyTrades(countries, currentYear, techManager, map, news);
    
    // Stage 3: Markets (requires Markets tech)
    updateMarkets(countries, currentYear, techManager, map, news);
    
    // Stage 4: Trade routes (requires Navigation tech)
    establishTradeRoutes(countries, currentYear, techManager, map);
    processTradeRoutes(countries, news);
    
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
            processTradeRoutes(countries, news);
            updateBanking(countries, year, techManager, news);
        }
        
        applyTraderBonuses(countries, techManager);
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
    executeTradeOffers(countries, news);
}

void TradeManager::generateTradeOffers(std::vector<Country>& countries, int currentYear, const Map& map) {
    
    // Limit offer generation frequency for performance
    if (currentYear - m_lastBarterYear < 3) return;
    m_lastBarterYear = currentYear;
    
    std::uniform_real_distribution<> chanceDist(0.0, 1.0);
    std::uniform_int_distribution<> resourceDist(0, 5); // 6 resource types
    std::uniform_real_distribution<> amountDist(5.0, 25.0);
    std::uniform_int_distribution<> validityDist(5, 15); // Valid for 5-15 years
    
    for (size_t i = 0; i < countries.size(); ++i) {
        Country& country1 = countries[i];
        if (country1.getPopulation() <= 0) continue;
        
        // Countries make trade offers based on their resources
        if (chanceDist(m_rng) < 0.3) { // 30% chance per country per trade cycle
            
            // Find potential trading partners (neighbors only for basic barter)
            for (size_t j = 0; j < countries.size(); ++j) {
                if (i == j) continue;
                Country& country2 = countries[j];
                if (country2.getPopulation() <= 0) continue;
                
                // Must be neighbors for basic barter
                if (!areCountriesNeighbors(country1, country2, map)) continue;
                
                // Cannot trade with enemies
                if (country1.isAtWar() && std::find(country1.getEnemies().begin(), 
                    country1.getEnemies().end(), &country2) != country1.getEnemies().end()) continue;
                
                // Generate realistic trade offer
                Resource::Type offeredResource = static_cast<Resource::Type>(resourceDist(m_rng));
                Resource::Type requestedResource = static_cast<Resource::Type>(resourceDist(m_rng));
                
                // Don't trade the same resource
                if (offeredResource == requestedResource) continue;
                
                double offeredAmount = amountDist(m_rng);
                double requestedAmount = offeredAmount * calculateBarterhRatio(offeredResource, requestedResource);
                
                // Check if country has the resources to offer
                if (country1.getResourceManager().getResourceAmount(offeredResource) >= offeredAmount) {
                    
                    TradeOffer offer(static_cast<int>(i), static_cast<int>(j), offeredResource, offeredAmount,
                                   requestedResource, requestedAmount, currentYear + validityDist(m_rng), m_nextOfferId++);
                    
                    m_activeOffers.push_back(offer);
                    
                    // Limit offers per country to prevent spam
                    if (m_activeOffers.size() > 100) break;
                }
            }
        }
    }
}

void TradeManager::executeTradeOffers(std::vector<Country>& countries, News& news) {
    
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
        
        if (acceptanceDist(m_rng) < acceptanceChance) {
            executeTradeOffer(offer, countries, news);
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
                for (int r = 0; r < 6; ++r) {
                    Resource::Type resource = static_cast<Resource::Type>(r);
                    double supply = calculateResourceSupply(resource, seller);
                    
                    if (supply > 1.2) { // Has surplus
                        double demand = calculateResourceDemand(resource, buyer);
                        
                        if (demand > 0.8 && buyer.getGold() > 5.0) { // Buyer needs it and has gold
                            double tradeAmount = std::min(10.0, supply - 1.0); // Trade surplus
                            double price = getResourcePrice(resource, seller) * priceDist(m_rng);
                            double totalCost = tradeAmount * price;
                            
                            if (buyer.getGold() >= totalCost && 
                                seller.getResourceManager().getResourceAmount(resource) >= tradeAmount) {
                                
                                // Execute currency trade
                                const_cast<ResourceManager&>(seller.getResourceManager()).consumeResource(resource, tradeAmount);
                                const_cast<ResourceManager&>(buyer.getResourceManager()).addResource(resource, tradeAmount);
                                
                                // Transfer gold
                                const_cast<Country&>(buyer).subtractGold(totalCost);
                                const_cast<Country&>(seller).addGold(totalCost);
                                
                                m_totalTradesCompleted++;
                                m_totalTradeValue += totalCost;
                                
                                // Add news event
                                std::string resourceName = [resource]() {
                                    switch(resource) {
                                        case Resource::Type::FOOD: return "food";
                                        case Resource::Type::HORSES: return "horses";
                                        case Resource::Type::SALT: return "salt";
                                        case Resource::Type::IRON: return "iron";
                                        case Resource::Type::COAL: return "coal";
                                        case Resource::Type::GOLD: return "gold";
                                        default: return "goods";
                                    }
                                }();
                                
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
                // Check if there's already a market nearby
                bool hasNearbyMarket = false;
                for (const Market& market : m_markets) {
                    double distance = std::sqrt(std::pow(market.location.x - country.getBoundaryPixels().begin()->x, 2) +
                                              std::pow(market.location.y - country.getBoundaryPixels().begin()->y, 2));
                    if (distance < 100) { // Within 100 pixels
                        hasNearbyMarket = true;
                        break;
                    }
                }
                
                if (!hasNearbyMarket) {
                    sf::Vector2i boundaryPixel = *country.getBoundaryPixels().begin();
                    Vector2i marketLocation(boundaryPixel.x, boundaryPixel.y);
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

void TradeManager::processTradeRoutes(std::vector<Country>& countries, News& news) {
    
    std::uniform_real_distribution<> tradeDist(0.0, 1.0);
    
    for (const TradeRoute& route : m_tradeRoutes) {
        if (!route.isActive) continue;
        
        if (route.fromCountryIndex >= static_cast<int>(countries.size()) || 
            route.toCountryIndex >= static_cast<int>(countries.size())) continue;
        
        Country& country1 = countries[route.fromCountryIndex];
        Country& country2 = countries[route.toCountryIndex];
        
        if (country1.getPopulation() <= 0 || country2.getPopulation() <= 0) continue;
        
        // Trade along route based on capacity and efficiency
        if (tradeDist(m_rng) < 0.4) { // 40% chance per route per cycle
            
            // Find what each country can export
            for (int r = 0; r < 6; ++r) {
                Resource::Type resource = static_cast<Resource::Type>(r);
                
                double supply1 = calculateResourceSupply(resource, country1);
                double demand2 = calculateResourceDemand(resource, country2);
                
                if (supply1 > 1.2 && demand2 > 0.8) {
                    double tradeAmount = std::min(route.capacity * route.efficiency, 
                                                std::min(supply1 - 1.0, demand2 * 0.5));
                    
                    if (tradeAmount > 0.5 && 
                        country1.getResourceManager().getResourceAmount(resource) >= tradeAmount) {
                        
                        const_cast<ResourceManager&>(country1.getResourceManager()).consumeResource(resource, tradeAmount);
                        const_cast<ResourceManager&>(country2.getResourceManager()).addResource(resource, tradeAmount);
                        
                        m_totalTradesCompleted++;
                        m_totalTradeValue += tradeAmount * getResourcePrice(resource, country1);
                    }
                }
            }
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
    
    sf::Vector2i fromBoundary = *from.getBoundaryPixels().begin();
    sf::Vector2i toBoundary = *to.getBoundaryPixels().begin();
    Vector2i fromCenter(fromBoundary.x, fromBoundary.y);
    Vector2i toCenter(toBoundary.x, toBoundary.y);
    
    return std::sqrt(std::pow(fromCenter.x - toCenter.x, 2) + std::pow(fromCenter.y - toCenter.y, 2));
}

double TradeManager::getResourcePrice(Resource::Type resource, const Country& country) const {
    
    // Base prices with supply/demand modifiers
    double basePrice = 1.0;
    switch(resource) {
        case Resource::Type::FOOD: basePrice = 1.0; break;
        case Resource::Type::HORSES: basePrice = 5.0; break;
        case Resource::Type::SALT: basePrice = 3.0; break;
        case Resource::Type::IRON: basePrice = 4.0; break;
        case Resource::Type::COAL: basePrice = 2.0; break;
        case Resource::Type::GOLD: basePrice = 10.0; break;
    }
    
    // Adjust based on country's supply/demand
    double supply = calculateResourceSupply(resource, country);
    double demand = calculateResourceDemand(resource, country);
    
    if (supply > demand) basePrice *= 0.8; // Surplus = lower price
    else if (demand > supply) basePrice *= 1.3; // Shortage = higher price
    
    return basePrice;
}

// 🧠 TECHNOLOGY CHECKS

bool TradeManager::hasCurrency(const Country& country, const TechnologyManager& techManager) const {
    // Check for Currency technology (ID 15)
    return TechnologyManager::hasTech(techManager, country, 15);
}

bool TradeManager::hasMarkets(const Country& country, const TechnologyManager& techManager) const {
    // Check for Markets technology (ID 35)
    return TechnologyManager::hasTech(techManager, country, 35);
}

bool TradeManager::hasNavigation(const Country& country, const TechnologyManager& techManager) const {
    // Check for Navigation technology (ID 43)
    return TechnologyManager::hasTech(techManager, country, 43);
}

bool TradeManager::hasBanking(const Country& country, const TechnologyManager& techManager) const {
    // Check for Banking technology (ID 34)
    return TechnologyManager::hasTech(techManager, country, 34);
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
        {Resource::Type::GOLD, 10.0}
    };
    
    return resourceValues[to] / resourceValues[from];
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
    
    m_totalTradesCompleted++;
    m_totalTradeValue += offer.offeredAmount + offer.requestedAmount;
    
    // Add news event
    std::string offeredName = [&offer]() {
        switch(offer.offeredResource) {
            case Resource::Type::FOOD: return "food";
            case Resource::Type::HORSES: return "horses";
            case Resource::Type::SALT: return "salt";
            case Resource::Type::IRON: return "iron";
            case Resource::Type::COAL: return "coal";
            case Resource::Type::GOLD: return "gold";
            default: return "goods";
        }
    }();
    
    std::string requestedName = [&offer]() {
        switch(offer.requestedResource) {
            case Resource::Type::FOOD: return "food";
            case Resource::Type::HORSES: return "horses";
            case Resource::Type::SALT: return "salt";
            case Resource::Type::IRON: return "iron";
            case Resource::Type::COAL: return "coal";
            case Resource::Type::GOLD: return "gold";
            default: return "goods";
        }
    }();
    
    news.addEvent("📦 TRADE: " + fromCountry.getName() + " trades " + 
                 std::to_string(static_cast<int>(offer.offeredAmount)) + " " + offeredName + 
                 " for " + std::to_string(static_cast<int>(offer.requestedAmount)) + " " + requestedName + 
                 " with " + toCountry.getName() + "!");
}

double TradeManager::calculateResourceDemand(Resource::Type resource, const Country& country) const {
    
    // Base demand based on population and country type
    double baseDemand = static_cast<double>(country.getPopulation()) / 100000.0; // Scale to reasonable numbers
    
    // Adjust based on resource type and country needs
    switch(resource) {
        case Resource::Type::FOOD:
            baseDemand *= 2.0; // Everyone needs food
            break;
        case Resource::Type::HORSES:
            if (country.getType() == Country::Type::Warmonger) baseDemand *= 1.5;
            break;
        case Resource::Type::IRON:
            if (country.getType() == Country::Type::Warmonger) baseDemand *= 2.0;
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
        
        for (int r = 0; r < 6; ++r) {
            Resource::Type resource = static_cast<Resource::Type>(r);
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
        
        for (int r = 0; r < 6; ++r) {
            Resource::Type resource = static_cast<Resource::Type>(r);
            
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
