// map.h
#pragma once

#include <SFML/Graphics.hpp>
#include <vector>
#include <mutex>
#include <unordered_set>
#include <unordered_map>
#include <random>
#include <functional>
#include "country.h"
#include "resource.h"
#include "news.h"

class Country;

namespace std {
    template <>
    struct hash<sf::Color> {
        size_t operator()(const sf::Color& color) const {
            return (static_cast<size_t>(color.r) << 24) |
                (static_cast<size_t>(color.g) << 16) |
                (static_cast<size_t>(color.b) << 8) |
                static_cast<size_t>(color.a);
        }
    };
}

std::string generate_country_name();
bool isNameTaken(const std::vector<Country>& countries, const std::string& name);

class Map {
public:
    Map(const sf::Image& baseImage, const sf::Image& resourceImage, int gridCellSize, const sf::Color& landColor, const sf::Color& waterColor, int regionSize);
    void initializeCountries(std::vector<Country>& countries, int numCountries);
    void updateCountries(std::vector<Country>& countries, int currentYear, News& news, class TechnologyManager& technologyManager);
    const std::vector<std::vector<bool>>& getIsLandGrid() const;
    sf::Vector2i pixelToGrid(const sf::Vector2f& pixel) const;
    int getGridCellSize() const;
    std::mutex& getGridMutex();
    const sf::Image& getBaseImage() const;
    int getRegionSize() const;
    void startPlague(int year, News& news);
    void endPlague(News& news);
    bool areNeighbors(const Country& country1, const Country& country2) const;
    bool isPlagueActive() const;
    void initializePlagueCluster(const std::vector<Country>& countries);
    bool isCountryAffectedByPlague(int countryIndex) const;
    int getPlagueStartYear() const;
    void updatePlagueDeaths(int deaths);
    bool loadSpawnZones(const std::string& filename);
    sf::Vector2i getRandomCellInPreferredZones(std::mt19937& gen);
    void setCountryGridValue(int x, int y, int value);
    void insertDirtyRegion(int regionIndex);
    void triggerPlague(int year, News& news); // Add this line
    
    // Fast Forward Mode - simulate multiple years quickly
    void fastForwardSimulation(std::vector<Country>& countries, int& currentYear, int targetYears, News& news, class TechnologyManager& technologyManager);
    
    // MEGA TIME JUMP - simulate thousands of years with historical tracking
    void megaTimeJump(std::vector<Country>& countries, int& currentYear, int targetYear, News& news, 
                      class TechnologyManager& techManager, class CultureManager& cultureManager, 
                      class GreatPeopleManager& greatPeopleManager, 
                      std::function<void(int, int, float)> progressCallback = nullptr);

    // Keep these for read-only access (const versions)
    const std::vector<std::vector<int>>& getCountryGrid() const;
    const std::unordered_set<int>& getDirtyRegions() const;

    // Keep these for modification access (non-const versions)
    std::vector<std::vector<int>>& getCountryGrid();
    std::unordered_set<int>& getDirtyRegions();

    const std::vector<std::vector<std::unordered_map<Resource::Type, double>>>& getResourceGrid() const;

private:
    std::vector<std::vector<int>> m_countryGrid;
    std::vector<std::vector<bool>> m_isLandGrid;
    int m_gridCellSize;
    int m_regionSize;
    sf::Color m_landColor;
    sf::Color m_waterColor;
    std::mutex m_gridMutex;
    sf::Image m_baseImage;
    sf::Image m_resourceImage;
    std::unordered_set<int> m_dirtyRegions;
    std::vector<std::vector<std::unordered_map<Resource::Type, double>>> m_resourceGrid;
    std::unordered_map<sf::Color, Resource::Type> m_resourceColors;
    void initializeResourceGrid();
    bool m_plagueActive;
    int m_plagueStartYear;
    long long m_plagueDeathToll;
    int m_plagueInterval;
    int m_nextPlagueYear;
    std::unordered_set<int> m_plagueAffectedCountries; // Track which countries are affected by current plague
    sf::Image m_spawnZoneImage;
    sf::Color m_spawnZoneColor = sf::Color(255, 132, 255);
};