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

    // (Optionally, if you want to apply a bonus for science too, add similar methods.)

    void resetScienceMultiplier();
    void applyScienceMultiplier(double bonus);
    double getScienceMultiplier() const { return m_scienceMultiplier; }

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
};