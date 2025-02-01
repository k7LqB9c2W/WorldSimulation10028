// main.cpp

#include <SFML/Graphics.hpp>
#include <vector>
#include <iostream>
#include <omp.h>
#include "country.h"
#include "renderer.h"
#include "map.h"
#include "news.h"
#include "technology.h"
#include "great_people.h"


int main() {
    sf::VideoMode videoMode(1920, 1080);

    if (std::find(sf::VideoMode::getFullscreenModes().begin(), sf::VideoMode::getFullscreenModes().end(), videoMode) == sf::VideoMode::getFullscreenModes().end()) {
        std::cerr << "Error: 1920x1080 fullscreen mode not available." << std::endl;
        return -1;
    }

    sf::RenderWindow window(videoMode, "Country Simulator", sf::Style::Fullscreen);

    sf::Image baseImage;
    if (!baseImage.loadFromFile("map.png")) {
        std::cerr << "Error: Could not load map image." << std::endl;
        return -1;
    }

    sf::Image resourceImage;
    if (!resourceImage.loadFromFile("resource.png")) {
        std::cerr << "Error: Could not load resource image." << std::endl;
        return -1;
    }

    sf::Color landColor(0, 58, 0);
    sf::Color waterColor(44, 90, 244);

    const int gridCellSize = 1;
    const int regionSize = 32;
    Map map(baseImage, resourceImage, gridCellSize, landColor, waterColor, regionSize);

    std::vector<Country> countries;
    const int numCountries = 100;

    // Show loading screen before initialization
    Renderer tempRenderer(window, map, waterColor);
    tempRenderer.showLoadingScreen();

    // Load spawn zones before initializing countries
    if (!map.loadSpawnZones("spawn.png")) {
        return -1;
    }

    map.initializeCountries(countries, numCountries);

    // Initialize the TechnologyManager
    TechnologyManager technologyManager;

    // Initialize the Great People Manager
    GreatPeopleManager greatPeopleManager;

    Renderer renderer(window, map, waterColor);
    News news;

    int currentYear = -5000;
    sf::Clock yearClock;
    sf::Time yearDuration = sf::seconds(1.0f);

    window.setVerticalSyncEnabled(true);

    omp_set_num_threads(omp_get_max_threads());

    // Zoom and panning variables
    bool enableZoom = false;
    float zoomLevel = 1.0f;
    sf::View defaultView = window.getDefaultView();
    sf::View zoomedView = defaultView;
    sf::Vector2f lastMousePos;
    bool isDragging = false;

    // Country info window variables
    const Country* selectedCountry = nullptr;
    bool showCountryInfo = false;

    // Country add mode variables
    bool countryAddMode = false;
    sf::Font m_font; // Font loading moved outside the loop
    if (!m_font.loadFromFile("arial.ttf")) {
        std::cerr << "Error: Could not load font file." << std::endl;
        return -1;
    }
    sf::Text countryAddModeText;
    countryAddModeText.setFont(m_font);
    countryAddModeText.setCharacterSize(24);
    countryAddModeText.setFillColor(sf::Color::White);
    countryAddModeText.setPosition(10, 10);
    countryAddModeText.setString("Country Add Mode");

    while (window.isOpen()) {
        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed) {
                window.close();
            }
            else if (event.type == sf::Event::KeyPressed) {
                if (event.key.code == sf::Keyboard::Num5) {
                    news.toggleWindow();
                }
                else if (event.key.code == sf::Keyboard::Num4) {
                    renderer.toggleWarmongerHighlights();
                }
                else if (event.key.code == sf::Keyboard::Num9) {
                    countryAddMode = !countryAddMode;
                    renderer.setShowCountryAddModeText(countryAddMode); // Update the flag in Renderer
                }
                else if (event.key.code == sf::Keyboard::Num3) {
                    enableZoom = !enableZoom;
                    if (enableZoom) {
                        zoomedView = window.getView();
                    }
                    else {
                        window.setView(defaultView);
                        zoomLevel = 1.0f;
                    }
                }
                else if (event.key.code == sf::Keyboard::Num6) {
                    renderer.toggleWarHighlights();
                }
            }
            else if (event.type == sf::Event::MouseWheelScrolled) {
                if (showCountryInfo) {
                    int newOffset = renderer.getTechScrollOffset() - static_cast<int>(event.mouseWheelScroll.delta * 10);
                    newOffset = std::max(0, std::min(newOffset, renderer.getMaxTechScrollOffset()));
                    renderer.setTechScrollOffset(newOffset);
                }
                else if (enableZoom) {
                    if (event.mouseWheelScroll.delta > 0) {
                        zoomLevel *= 0.9f;
                    }
                    else {
                        zoomLevel *= 1.1f;
                    }
                    zoomLevel = std::max(0.5f, std::min(zoomLevel, 3.0f));
                    zoomedView.setSize(defaultView.getSize().x * zoomLevel, defaultView.getSize().y * zoomLevel);
                }
            }
            else if (event.type == sf::Event::MouseButtonPressed && event.mouseButton.button == sf::Mouse::Left) {
                if (enableZoom) {
                    isDragging = true;
                    lastMousePos = window.mapPixelToCoords(sf::Mouse::getPosition(window));
                }
                else if (countryAddMode) {
                    // Add new country
                    sf::Vector2i mousePos = sf::Mouse::getPosition(window);
                    sf::Vector2f worldPos = window.mapPixelToCoords(mousePos);
                    sf::Vector2i gridPos = map.pixelToGrid(worldPos);

                    if (gridPos.x >= 0 && gridPos.x < map.getIsLandGrid()[0].size() &&
                        gridPos.y >= 0 && gridPos.y < map.getIsLandGrid().size() &&
                        map.getIsLandGrid()[gridPos.y][gridPos.x] && map.getCountryGrid()[gridPos.y][gridPos.x] == -1) {

                        std::random_device rd;
                        std::mt19937 gen(rd());
                        std::uniform_int_distribution<> colorDist(50, 255);
                        std::uniform_int_distribution<> popDist(1000, 10000);
                        std::uniform_real_distribution<> growthRateDist(0.0003, 0.001);
                        std::uniform_int_distribution<> typeDist(0, 2);
                        std::discrete_distribution<> scienceTypeDist({ 50, 40, 10 });
                        std::discrete_distribution<> cultureTypeDist({ 40, 40, 20 });

                        sf::Color countryColor(colorDist(gen), colorDist(gen), colorDist(gen));
                        long long initialPopulation = popDist(gen);
                        double growthRate = growthRateDist(gen);
                        std::string countryName = generate_country_name();
                        while (isNameTaken(countries, countryName)) {
                            countryName = generate_country_name();
                        }
                        countryName += " Tribe";
                        Country::Type countryType = static_cast<Country::Type>(typeDist(gen));
                        Country::ScienceType scienceType = static_cast<Country::ScienceType>(scienceTypeDist(gen));
                        Country::CultureType cultureType = static_cast<Country::CultureType>(cultureTypeDist(gen));

                        int newCountryIndex = countries.size();
                        countries.emplace_back(newCountryIndex, countryColor, gridPos, initialPopulation, growthRate, countryName, countryType, scienceType, cultureType);

                        // Use Map's methods to update the grid and dirty regions
                        map.setCountryGridValue(gridPos.x, gridPos.y, newCountryIndex);

                        int regionIndex = static_cast<int>((gridPos.y / map.getRegionSize()) * (map.getBaseImage().getSize().x / map.getGridCellSize() / map.getRegionSize()) + (gridPos.x / map.getRegionSize()));
                        map.insertDirtyRegion(regionIndex);

                        renderer.setNeedsUpdate(true);
                    }
                }
                else {
                    // Check if a country is clicked
                    sf::Vector2i mousePos = sf::Mouse::getPosition(window);
                    sf::Vector2f worldPos = window.mapPixelToCoords(mousePos); // Get coordinates in the world
                    sf::Vector2i gridPos = map.pixelToGrid(worldPos);

                    if (gridPos.x >= 0 && gridPos.x < map.getCountryGrid()[0].size() &&
                        gridPos.y >= 0 && gridPos.y < map.getCountryGrid().size()) {
                        int countryIndex = map.getCountryGrid()[gridPos.y][gridPos.x];
                        if (countryIndex != -1) {
                            selectedCountry = &countries[countryIndex];
                            showCountryInfo = true;
                        }
                        else {
                            showCountryInfo = false;
                        }
                    }
                }
            }
            else if (event.type == sf::Event::MouseButtonReleased && event.mouseButton.button == sf::Mouse::Left) {
                isDragging = false;
            }
            else if (event.type == sf::Event::MouseMoved && isDragging && enableZoom) {
                sf::Vector2f currentMousePos = window.mapPixelToCoords(sf::Mouse::getPosition(window));
                sf::Vector2f delta = lastMousePos - currentMousePos;
                zoomedView.move(delta);
                lastMousePos = currentMousePos;
            }
        }

        map.updateCountries(countries, currentYear, news);
        for (auto& country : countries) {
            technologyManager.updateCountry(country);
        }

        if (yearClock.getElapsedTime() >= yearDuration) {
            currentYear++;
            if (currentYear == 0) {
                currentYear = 1;
            }

            // Update the great people system
            greatPeopleManager.updateEffects(currentYear, countries, news);

            // Update the year text and flag a redraw

            renderer.updateYearText(currentYear);
            yearClock.restart();
            renderer.setNeedsUpdate(true);
        }

        if (enableZoom) {
            window.setView(zoomedView);
        }

        renderer.render(countries, map, news, technologyManager, selectedCountry, showCountryInfo);
        //if (countryAddMode) {
          //  window.draw(countryAddModeText);
        //}
    }

    return 0;
}