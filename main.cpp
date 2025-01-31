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
        return -1; // Or handle the error in some other way
    }

    map.initializeCountries(countries, numCountries);

    // Initialize the TechnologyManager
    TechnologyManager technologyManager;

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
                else if (event.key.code == sf::Keyboard::Num4) { // Check for "4" key press
                    renderer.toggleWarmongerHighlights();
                }
                else if (event.key.code == sf::Keyboard::Num3) { // Check for "3" key press
                    enableZoom = !enableZoom; // Toggle zoom mode
                    if (enableZoom) {
                        // Initialize zoomed view
                        zoomedView = window.getView();
                    }
                    else {
                        // Reset to default view
                        window.setView(defaultView);
                        zoomLevel = 1.0f;
                    }
                }
                else if (event.key.code == sf::Keyboard::Num6) {
                    renderer.toggleWarHighlights();
                }
            }
            else if (event.type == sf::Event::MouseWheelScrolled) {
                if (showCountryInfo) { // Only allow scrolling if the country info window is visible
                    // Adjust the scroll offset based on the mouse wheel delta
                    int newOffset = renderer.getTechScrollOffset() - static_cast<int>(event.mouseWheelScroll.delta * 10);

                    // Assuming a maximum offset of 300 and a minimum of 0:
                    newOffset = std::max(0, std::min(newOffset, renderer.getMaxTechScrollOffset()));

                    renderer.setTechScrollOffset(newOffset);
                }
                else if (enableZoom)
                {
                    if (event.mouseWheelScroll.delta > 0) {
                        zoomLevel *= 0.9f; // Zoom in
                    }
                    else {
                        zoomLevel *= 1.1f; // Zoom out
                    }
                    zoomLevel = std::max(0.5f, std::min(zoomLevel, 3.0f)); // Limit zoom
                    zoomedView.setSize(defaultView.getSize().x * zoomLevel, defaultView.getSize().y * zoomLevel);
                }
            }
            else if (event.type == sf::Event::MouseButtonPressed && event.mouseButton.button == sf::Mouse::Left) {
                if (enableZoom) {
                    isDragging = true;
                    lastMousePos = window.mapPixelToCoords(sf::Mouse::getPosition(window));
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

        // Update countries and technology
        map.updateCountries(countries, currentYear, news);
        for (auto& country : countries) {
            technologyManager.updateCountry(country);
        }

        if (yearClock.getElapsedTime() >= yearDuration) {
            currentYear++;
            if (currentYear == 0) {
                currentYear = 1;
            }
            renderer.updateYearText(currentYear);
            yearClock.restart();
            renderer.setNeedsUpdate(true);
        }

        if (enableZoom) {
            window.setView(zoomedView);
        }

        renderer.render(countries, map, news, technologyManager, selectedCountry, showCountryInfo);
    }

    return 0;
}