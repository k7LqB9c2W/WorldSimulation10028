// country.h

#pragma once

#include <SFML/Graphics.hpp>
#include <vector>
#include <unordered_set>
#include <mutex>
#include <string>
#include "resource.h"
#include "news.h"
#include "map.h"

// Forward declaration of Map
class Map;

namespace std {
    template <>
    struct hash<sf::Vector2i> {
        size_t operator()(const sf::Vector2i& v) const {
            return (std::hash<int>()(v.x) ^ (std::hash<int>()(v.y) << 1));
        }
    };
}

class City {
public:
    City(const sf::Vector2i& location) : m_location(location) {}

    sf::Vector2i getLocation() const { return m_location; }

private:
    sf::Vector2i m_location;
};

class Country {
public:
    // Add the enum for country types
    enum class Type {
        Warmonger,
        Pacifist,
        Trader
    };
    enum class ScienceType {
        NS, // Normal Science
        LS, // Less Science
        MS  // More Science
    };
    // Add the enum for culture types
    enum class CultureType {
        NC, // Normal Culture (40% chance)
        LC, // Less Culture (40% chance)
        MC  // More Culture (20% chance)
    };
    
    enum class Ideology {
        Tribal,        // Starting ideology
        Chiefdom,      // Early organization
        Kingdom,       // Monarchical rule
        Empire,        // Large territorial state
        Republic,      // Republican government
        Democracy,     // Democratic government
        Dictatorship,  // Authoritarian rule
        Federation,    // Federal system
        Theocracy,     // Religious rule
        CityState      // Independent city-state
    };
    // Getter for m_nextWarCheckYear
    int getNextWarCheckYear() const;

    // Setter for m_nextWarCheckYear
    void setNextWarCheckYear(int year);

    Country(int countryIndex, const sf::Color& color, const sf::Vector2i& startCell, long long initialPopulation, double growthRate, const std::string& name, Type type, ScienceType scienceType, CultureType cultureType);
    void update(const std::vector<std::vector<bool>>& isLandGrid, std::vector<std::vector<int>>& countryGrid, std::mutex& gridMutex, int gridCellSize, int regionSize, std::unordered_set<int>& dirtyRegions, int currentYear, const std::vector<std::vector<std::unordered_map<Resource::Type, double>>>& resourceGrid, News& news, bool plagueActive, long long& plagueDeaths, const Map& map);
    long long getPopulation() const;
    sf::Color getColor() const;
    int getCountryIndex() const;
    void foundCity(const sf::Vector2i& location, News& news);
    const std::vector<City>& getCities() const;
    double getGold() const;
    double getMilitaryStrength() const;
    // Add a member variable to store science points
    double getSciencePoints() const;
    void addSciencePoints(double points);
    void setSciencePoints(double points);
    void addBoundaryPixel(const sf::Vector2i& cell);
    const std::unordered_set<sf::Vector2i>& getBoundaryPixels() const;
    const ResourceManager& getResourceManager() const;
    const std::string& getName() const;
    Type getType() const; // Add a getter for the country type
    ScienceType getScienceType() const;
    CultureType getCultureType() const;
    bool canFoundCity() const;
    bool canDeclareWar() const;
    void startWar(Country& target, News& news);
    void endWar();
    bool isAtWar() const;
    bool isNeighbor(const Country& other) const;
    int getWarDuration() const;
    void setWarDuration(int duration);
    void decrementWarDuration();
    bool isWarofAnnihilation() const;
    void setWarofAnnihilation(bool isannhilation);
    bool isWarofConquest() const;
    void setWarofConquest(bool isconquest);
    int getPeaceDuration() const;
    void setPeaceDuration(int duration);
    void decrementPeaceDuration();
    bool isAtPeace() const;
    void addConqueredCity(const City& city);
    const std::vector<Country*>& getEnemies() const;
    void addEnemy(Country* enemy);
    void removeEnemy(Country* enemy);
    void clearEnemies();
    void setPopulation(long long population);
    // Resets military strength to its base value (based on the country type).
    void resetMilitaryStrength();
    // Applies a bonus multiplier to the current military strength.
    void applyMilitaryBonus(double bonus);
    double getCulturePoints() const;
    void addCulturePoints(double points);
    void setCulturePoints(double points);
    double getCultureMultiplier() const { return m_cultureMultiplier; }
    void resetCultureMultiplier();
    void applyCultureMultiplier(double bonus);

    // (Optionally, if you want to apply a bonus for science too, add similar methods.)

    void resetScienceMultiplier();
    void applyScienceMultiplier(double bonus);
    double getScienceMultiplier() const { return m_scienceMultiplier; }
    
    // Fast Forward Mode methods
    void fastForwardGrowth(int yearIndex, int currentYear, const std::vector<std::vector<bool>>& isLandGrid, 
                          std::vector<std::vector<int>>& countryGrid, 
                          const std::vector<std::vector<std::unordered_map<Resource::Type, double>>>& resourceGrid,
                          News& news, const Map& map);
    void applyPlagueDeaths(long long deaths);
    
    // Technology effects
    void applyTechnologyBonus(int techId);
    double getTotalPopulationGrowthRate() const;
    double getPlagueResistance() const;
    double getMilitaryStrengthMultiplier() const;
    double getTerritoryCaptureBonusRate() const;
    double getDefensiveBonus() const;
    double getWarDurationReduction() const;
    double getMaxSizeMultiplier() const;
    int getExpansionRateBonus() const;
    int getBurstExpansionRadius() const;
    int getBurstExpansionFrequency() const;
    bool canAnnihilateCountry(const Country& target) const;
    void absorbCountry(Country& target, std::vector<std::vector<int>>& countryGrid, News& news);
    int getWarBurstConquestRadius() const;
    int getWarBurstConquestFrequency() const;
    
    // Ideology system
    Ideology getIdeology() const { return m_ideology; }
    void setIdeology(Ideology ideology) { m_ideology = ideology; }
    std::string getIdeologyString() const;
    void checkIdeologyChange(int currentYear, News& news);
    bool canChangeToIdeology(Ideology newIdeology) const;
    double getSciencePointsMultiplier() const;
    double calculateNeighborScienceBonus(const std::vector<Country>& allCountries, const class Map& map, const class TechnologyManager& techManager, int currentYear) const;

private:
    int m_countryIndex;
    sf::Color m_color;
    long long m_population;
    std::unordered_set<sf::Vector2i> m_boundaryPixels;
    double m_populationGrowthRate;
    ResourceManager m_resourceManager;
    std::string m_name;
    int m_nextWarCheckYear;
    std::vector<City> m_cities;
    bool m_hasCity;
    double m_gold;
    double m_militaryStrength;  // Add military strength member
    Type m_type; // Add a member variable to store the country type
    ScienceType m_scienceType;
    CultureType m_cultureType;
    Ideology m_ideology;
    double m_culturePoints; // For culture points
    double m_cultureMultiplier = 1.0;
    int getMaxExpansionPixels(int year) const;
    long long m_prePlaguePopulation;
    std::vector<Country*> m_enemies;
    bool m_isAtWar;
    int m_warDuration;
    bool m_isWarofAnnihilation;
    bool m_isWarofConquest;
    int m_peaceDuration;
    long long m_preWarPopulation;
    int m_warCheckCooldown;  // Cooldown period before checking for war again
    int m_warCheckDuration;  // Duration for actively seeking war
    bool m_isSeekingWar;    // Flag to indicate if currently seeking war
    double m_sciencePoints; // For science points
    double m_scienceMultiplier = 1.0;
    
    // Technology bonuses
    double m_populationGrowthBonus = 0.0;
    double m_plagueResistanceBonus = 0.0;
    double m_militaryStrengthBonus = 0.0;
    double m_territoryCaptureBonusRate = 0.0;
    double m_defensiveBonus = 0.0;
    double m_warDurationReduction = 0.0;
    double m_maxSizeMultiplier = 1.0;
    int m_expansionRateBonus = 0;
    int m_burstExpansionRadius = 1; // How many pixels outward to expand in bursts
    int m_burstExpansionFrequency = 0; // How often burst expansion occurs (0 = never)
    int m_warBurstConquestRadius = 1; // How many enemy pixels to capture in war bursts
    int m_warBurstConquestFrequency = 0; // How often war burst conquest occurs
    double m_sciencePointsBonus = 0.0; // Science research speed bonus
    
    // 🚀 PERFORMANCE: Cache neighbor science bonuses to avoid O(n²) calculations
    mutable double m_cachedNeighborScienceBonus = 0.0;
    mutable int m_neighborBonusLastUpdated = -999999; // Year when bonus was last calculated
    mutable std::vector<int> m_cachedNeighborIndices; // Cache of neighbor country indices
    mutable int m_neighborRecalculationInterval = 50; // Random interval between 20-80 years for this country
    
    // Expansion contentment system
    bool m_isContentWithSize = false; // Whether country wants to stop expanding
    int m_contentmentDuration = 0; // How many years to remain content
    int m_expansionStaggerOffset = 0; // Personal offset for burst expansion timing
};