#pragma once

#include <SFML/Graphics.hpp>
#include <vector>
#include <mutex>
#include <unordered_set>
#include <unordered_map>
#include "country.h"
#include "resource.h"
#include "news.h"

// Forward declaration of Country
class Country;

// Custom hash function for sf::Color
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

// Declare the function in the header file:
std::string generate_country_name();
// Declare isNameTaken as a non-member function:
bool isNameTaken(const std::vector<Country>& countries, const std::string& name);
class Map {
public:
    Map(const sf::Image& baseImage, const sf::Image& resourceImage, int gridCellSize, const sf::Color& landColor, const sf::Color& waterColor, int regionSize);
    void initializeCountries(std::vector<Country>& countries, int numCountries);
    void updateCountries(std::vector<Country>& countries, int currentYear, News& news);
    const std::vector<std::vector<bool>>& getIsLandGrid() const;
    sf::Vector2i pixelToGrid(const sf::Vector2f& pixel) const;
    int getGridCellSize() const;
    std::mutex& getGridMutex();
    const sf::Image& getBaseImage() const;
    int getRegionSize() const;
    const std::unordered_set<int>& getDirtyRegions() const;
    const std::vector<std::vector<int>>& getCountryGrid() const;
    const std::vector<std::vector<std::unordered_map<Resource::Type, double>>>& getResourceGrid() const;
    void startPlague(int year, News& news);
    void endPlague(News& news);
    bool areNeighbors(const Country& country1, const Country& country2) const;
    bool isPlagueActive() const;
    int getPlagueStartYear() const;
    void updatePlagueDeaths(int deaths);

private:
    std::vector<std::vector<int>> m_countryGrid;
    std::vector<std::vector<bool>> m_isLandGrid;
    int m_gridCellSize;
    int m_regionSize;
    sf::Color m_landColor;
    sf::Color m_waterColor;
    std::mutex m_gridMutex;
    sf::Image m_baseImage;
    sf::Image m_resourceImage; // Image for resource data
    std::unordered_set<int> m_dirtyRegions;
    std::vector<std::vector<std::unordered_map<Resource::Type, double>>> m_resourceGrid;
    std::unordered_map<sf::Color, Resource::Type> m_resourceColors;
    void initializeResourceGrid();
    bool m_plagueActive;
    int m_plagueStartYear;
    long long m_plagueDeathToll; // Changed to long long
    int m_plagueInterval;
    int m_nextPlagueYear;
};