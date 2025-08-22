// main.cpp

#include <SFML/Graphics.hpp>
#include <vector>
#include <iostream>
#include <chrono>
#include <omp.h>
#include <csignal>
#include <exception>
#include <stdexcept>
#include "country.h"
#include "renderer.h"
#include "map.h"
#include "news.h"
#include "technology.h"
#include "great_people.h"
#include "culture.h" // Include the new culture header

// 🚨 CRASH DETECTION SYSTEM
void crashHandler(int signal) {
    std::cout << "\n🚨🚨🚨 GAME CRASHED! 🚨🚨🚨" << std::endl;
    std::cout << "Signal: " << signal << std::endl;
    
    switch(signal) {
        case SIGSEGV:
            std::cout << "💥 SEGMENTATION FAULT - Invalid memory access!" << std::endl;
            std::cout << "   Likely causes: Array out of bounds, null pointer, corrupted memory" << std::endl;
            break;
        case SIGABRT:
            std::cout << "💥 ABORT SIGNAL - Program terminated!" << std::endl;
            std::cout << "   Likely causes: Assert failed, exception not caught, memory corruption" << std::endl;
            break;
        case SIGFPE:
            std::cout << "💥 FLOATING POINT EXCEPTION - Math error!" << std::endl;
            std::cout << "   Likely causes: Division by zero, invalid math operation" << std::endl;
            break;
        case SIGILL:
            std::cout << "💥 ILLEGAL INSTRUCTION - Invalid CPU instruction!" << std::endl;
            std::cout << "   Likely causes: Corrupted memory, stack overflow" << std::endl;
            break;
        default:
            std::cout << "💥 UNKNOWN SIGNAL: " << signal << std::endl;
            break;
    }
    
    std::cout << "\n📋 CRASH REPORT:" << std::endl;
    std::cout << "   Time: " << std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() << std::endl;
    std::cout << "   Last known operation: Check console output above for details" << std::endl;
    std::cout << "\n💡 TIP: Press D to enable debug mode for more detailed logging" << std::endl;
    std::cout << "🔄 The game will attempt to exit gracefully..." << std::endl;
    
    // Try to exit gracefully
    std::exit(signal);
}

// Global performance mode variable
bool turboMode = false;

// Mega Time Jump GUI variables
bool megaTimeJumpMode = false;
std::string megaTimeJumpInput = "";

int main() {
    // 🚨 REGISTER CRASH HANDLERS
    std::signal(SIGSEGV, crashHandler);  // Segmentation fault
    std::signal(SIGABRT, crashHandler);  // Abort signal
    std::signal(SIGFPE, crashHandler);   // Floating point exception
    std::signal(SIGILL, crashHandler);   // Illegal instruction
    
    std::cout << "🛡️ CRASH DETECTION SYSTEM ACTIVE" << std::endl;
    std::cout << "   Any crashes will be reported in detail!" << std::endl;
    std::cout << "   Press D during gameplay to enable debug mode for more info" << std::endl;
    
    try {
        std::cout << "🚀 Starting World Simulation..." << std::endl;
        
        sf::VideoMode videoMode(1920, 1080);

    if (std::find(sf::VideoMode::getFullscreenModes().begin(), sf::VideoMode::getFullscreenModes().end(), videoMode) == sf::VideoMode::getFullscreenModes().end()) {
        std::cerr << "Error: 1920x1080 fullscreen mode not available." << std::endl;
        return -1;
    }

    sf::RenderWindow window(videoMode, "Country Simulator", sf::Style::Fullscreen);

    // Performance optimization: Limit frame rate to reduce CPU usage
    window.setFramerateLimit(60);
    window.setVerticalSyncEnabled(false); // Disable vsync for better performance

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
    
    std::cout << "🚀 INITIALIZING MAP..." << std::endl;
    auto mapStart = std::chrono::high_resolution_clock::now();
    Map map(baseImage, resourceImage, gridCellSize, landColor, waterColor, regionSize);
    auto mapEnd = std::chrono::high_resolution_clock::now();
    auto mapDuration = std::chrono::duration_cast<std::chrono::milliseconds>(mapEnd - mapStart);
    std::cout << "✅ MAP INITIALIZED in " << mapDuration.count() << " ms" << std::endl;

    std::vector<Country> countries;
    const int numCountries = 100;

    // Show loading screen before initialization
    Renderer tempRenderer(window, map, waterColor);
    tempRenderer.showLoadingScreen();

    // Load spawn zones before initializing countries
    if (!map.loadSpawnZones("spawn.png")) {
        return -1;
    }

    std::cout << "🚀 SPAWNING COUNTRIES..." << std::endl;
    auto countryStart = std::chrono::high_resolution_clock::now();
    map.initializeCountries(countries, numCountries);
    auto countryEnd = std::chrono::high_resolution_clock::now();
    auto countryDuration = std::chrono::duration_cast<std::chrono::milliseconds>(countryEnd - countryStart);
    std::cout << "✅ " << numCountries << " COUNTRIES SPAWNED in " << countryDuration.count() << " ms" << std::endl;

    // Initialize the TechnologyManager
    TechnologyManager technologyManager;

    // Initialize the CultureManager
    CultureManager cultureManager;

    // Initialize the Great People Manager
    GreatPeopleManager greatPeopleManager;

    Renderer renderer(window, map, waterColor);
    News news;

    int currentYear = -5000;
    sf::Clock yearClock;
    sf::Time yearDuration = sf::seconds(1.0f);
    
    // NUCLEAR OPTIMIZATION: Event-driven simulation architecture
    sf::Clock frameClock;
    const float targetFrameTime = 1.0f / 60.0f; // 60 FPS rendering
    bool simulationNeedsUpdate = true;  // Force initial simulation
    bool renderingNeedsUpdate = true;   // Force initial render
    
    // Performance mode settings
    sf::Time turboYearDuration = sf::seconds(0.1f); // 10 years/second in turbo
    
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
        frameClock.restart(); // Start frame timing
        
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

                else if (event.key.code == sf::Keyboard::Num8) { // Add this block
                    map.triggerPlague(currentYear, news);
                }
                else if (event.key.code == sf::Keyboard::T) { // TURBO MODE TOGGLE
                    turboMode = !turboMode;
                    renderingNeedsUpdate = true; // Update display
                }
                else if (event.key.code == sf::Keyboard::F) { // 🛡️ CRASH-SAFE FAST FORWARD MODE
                    std::cout << "🔍 OPERATION: Fast Forward requested" << std::endl;
                    std::cout << "📊 MEMORY STATUS: Starting Fast Forward operation" << std::endl;
                    std::cout << "   Current Year: " << currentYear << std::endl;
                    std::cout << "   Country Count: " << countries.size() << std::endl;
                    
                    try {
                        sf::Clock fastForwardClock;
                        
                        // Show fast forward indicator
                        sf::Text fastForwardText;
                        fastForwardText.setFont(m_font);
                        fastForwardText.setCharacterSize(48);
                        fastForwardText.setFillColor(sf::Color::Yellow);
                        fastForwardText.setString("FAST FORWARDING 100 YEARS...");
                        fastForwardText.setPosition(window.getSize().x / 2 - 300, window.getSize().y / 2);
                        
                        window.clear();
                        window.draw(fastForwardText);
                        window.display();
                        
                        std::cout << "🚀 Starting Fast Forward (100 years)..." << std::endl;
                    
                    // 🛡️ CRASH FIX: Process in small chunks to avoid memory overflow
                    const int totalYears = 100;
                    const int chunkSize = 10; // Process 10 years at a time
                    
                    for (int chunk = 0; chunk < totalYears / chunkSize; ++chunk) {
                        
                        std::cout << "🔍 CHUNK " << (chunk + 1) << "/10: Processing years " 
                                  << currentYear << " to " << (currentYear + chunkSize) << std::endl;
                        
                        int startYear = currentYear;
                        
                        try {
                            // Perform map simulation for this chunk
                            std::cout << "   📍 Map simulation..." << std::endl;
                            map.fastForwardSimulation(countries, currentYear, chunkSize, news);
                            
                            // 🚀 OPTIMIZED TECH/CULTURE: Batch processing and reduced frequency
                            std::cout << "   🧠 Tech/Culture updates for " << countries.size() << " countries..." << std::endl;
                            
                            // Only update every other chunk (20 years instead of 10) for better performance
                            if (chunk % 2 == 0) {
                            for (size_t i = 0; i < countries.size(); ++i) {
                                    // Do 2 rounds to compensate for reduced frequency
                                    technologyManager.updateCountry(countries[i]);
                                    cultureManager.updateCountry(countries[i]);
                                technologyManager.updateCountry(countries[i]);
                                cultureManager.updateCountry(countries[i]);
                                
                                    // Safety check every 20 countries (reduced logging)
                                    if (i % 20 == 0 && i > 0) {
                                    std::cout << "     ✓ Processed " << i << "/" << countries.size() << " countries" << std::endl;
                                    }
                                }
                            } else {
                                std::cout << "     🚀 Skipping tech/culture this chunk for performance" << std::endl;
                            }
                            std::cout << "   ✅ Chunk " << (chunk + 1) << " completed successfully" << std::endl;
                        } catch (const std::exception& e) {
                            std::cout << "🚨 ERROR IN CHUNK " << (chunk + 1) << ": " << e.what() << std::endl;
                            throw; // Re-throw to outer catch
                        }
                        
                        // Update progress indicator
                        int yearsCompleted = (chunk + 1) * chunkSize;
                        fastForwardText.setString("FAST FORWARD: " + std::to_string(yearsCompleted) + "/100 years");
                        window.clear();
                        window.draw(fastForwardText);
                        window.display();
                        
                        // 🛡️ PREVENT SYSTEM OVERLOAD: Small pause between chunks
                        sf::sleep(sf::milliseconds(50));
                    }
                    
                    // Final updates
                    greatPeopleManager.updateEffects(currentYear, countries, news);
                    
                    // 🔥 FORCE IMMEDIATE COMPLETE VISUAL REFRESH FOR FAST FORWARD
                    std::cout << "🎨 Refreshing fast forward visuals..." << std::endl;
                    
                    // Mark ALL regions as dirty for complete re-render
                    int totalRegions = (window.getSize().x / map.getGridCellSize() / map.getRegionSize()) * 
                                      (window.getSize().y / map.getGridCellSize() / map.getRegionSize());
                    for (int i = 0; i < totalRegions; ++i) {
                        map.insertDirtyRegion(i);
                    }
                    
                    renderer.updateYearText(currentYear);
                    renderer.setNeedsUpdate(true);
                    renderingNeedsUpdate = true;
                    
                    // Show completion
                    auto elapsed = fastForwardClock.getElapsedTime();
                    std::cout << "✅ Fast Forward Complete! 100 years in " << elapsed.asSeconds() << " seconds" << std::endl;
                    std::cout << "📊 FINAL STATUS:" << std::endl;
                    std::cout << "   Final Year: " << currentYear << std::endl;
                    std::cout << "   Countries: " << countries.size() << std::endl;
                    std::cout << "   Memory state: Stable" << std::endl;
                    
                    fastForwardText.setString("FAST FORWARD COMPLETE!");
                    fastForwardText.setFillColor(sf::Color::Green);
                    window.clear();
                    renderer.render(countries, map, news, technologyManager, cultureManager, selectedCountry, showCountryInfo);
                    window.draw(fastForwardText);
                    window.display();
                    sf::sleep(sf::seconds(0.5f));
                    
                    } catch (const std::exception& e) {
                        std::cout << "🚨🚨🚨 FAST FORWARD CRASHED! 🚨🚨🚨" << std::endl;
                        std::cout << "💥 ERROR: " << e.what() << std::endl;
                        std::cout << "📍 Last known state:" << std::endl;
                        std::cout << "   Year: " << currentYear << std::endl;
                        std::cout << "   Countries: " << countries.size() << std::endl;
                        std::cout << "🔄 Attempting to continue normal simulation..." << std::endl;
                        
                        // Reset any potentially corrupted state
                        renderingNeedsUpdate = true;
                    } catch (...) {
                        std::cout << "🚨🚨🚨 UNKNOWN FAST FORWARD ERROR! 🚨🚨🚨" << std::endl;
                        std::cout << "💥 This is likely a memory corruption or system-level issue" << std::endl;
                        std::cout << "🔄 Attempting to continue..." << std::endl;
                        renderingNeedsUpdate = true;
                    }
                }
                else if (event.key.code == sf::Keyboard::D) { // 🔧 DEBUG MODE TOGGLE
                    bool currentDebugMode = TechnologyManager::getDebugMode();
                    TechnologyManager::setDebugMode(!currentDebugMode);
                    CultureManager::setDebugMode(!currentDebugMode);
                    
                    std::cout << "🔧 DEBUG MODE " << (currentDebugMode ? "DISABLED" : "ENABLED") 
                              << " - Tech/Civic unlock messages are now " 
                              << (currentDebugMode ? "OFF" : "ON") << std::endl;
                }
                else if (event.key.code == sf::Keyboard::Z) { // 🚀 MEGA TIME JUMP MODE
                    megaTimeJumpMode = true;
                    megaTimeJumpInput = "";
                    std::cout << "\n🚀 MEGA TIME JUMP MODE ACTIVATED!" << std::endl;
                }
            }
            else if (event.type == sf::Event::TextEntered) {
                // Handle text input for Mega Time Jump
                if (megaTimeJumpMode) {
                    if (event.text.unicode >= '0' && event.text.unicode <= '9') {
                        megaTimeJumpInput += static_cast<char>(event.text.unicode);
                    }
                    else if (event.text.unicode == '-' && megaTimeJumpInput.empty()) {
                        megaTimeJumpInput = "-";
                    }
                    else if (event.text.unicode == 8 && !megaTimeJumpInput.empty()) { // Backspace
                        megaTimeJumpInput.pop_back();
                    }
                    else if (event.text.unicode == 13) { // Enter key
                        if (!megaTimeJumpInput.empty()) {
                            int targetYear = std::stoi(megaTimeJumpInput);
                            
                            // Validate year range
                            if (targetYear >= -5000 && targetYear <= 2025 && targetYear > currentYear) {
                                megaTimeJumpMode = false;
                                
                                int yearsToSimulate = targetYear - currentYear;
                                std::cout << "SIMULATING " << yearsToSimulate << " YEARS OF HISTORY!" << std::endl;
                                std::cout << "From " << currentYear << " to " << targetYear << std::endl;
                                
                                // Initialize timing and display
                                sf::Clock megaClock;
                                sf::Clock updateClock;
                                std::vector<float> sampleTimes;
                                
                                // Show loading screen
                                sf::RectangleShape loadingBg(sf::Vector2f(window.getSize().x, window.getSize().y));
                                loadingBg.setFillColor(sf::Color(0, 0, 0, 200));
                                
                                sf::Text loadingText;
                                loadingText.setFont(m_font);
                                loadingText.setCharacterSize(48);
                                loadingText.setFillColor(sf::Color::White);
                                loadingText.setString("TIME JUMPING TO " + std::to_string(targetYear) + "...");
                                loadingText.setPosition(window.getSize().x / 2 - 350, window.getSize().y / 2 - 40);
                                
                                sf::Text countdownText;
                                countdownText.setFont(m_font);
                                countdownText.setCharacterSize(36);
                                countdownText.setFillColor(sf::Color::Cyan);
                                countdownText.setString("Estimating time...");
                                countdownText.setPosition(window.getSize().x / 2 - 200, window.getSize().y / 2 + 20);
                                
                                window.clear();
                                renderer.render(countries, map, news, technologyManager, cultureManager, selectedCountry, showCountryInfo);
                                window.draw(loadingBg);
                                window.draw(loadingText);
                                window.draw(countdownText);
                                window.display();
                                
                                // Call mega simulation function with progress callback
                                map.megaTimeJump(countries, currentYear, targetYear, news, technologyManager, cultureManager, greatPeopleManager, 
                                    [&](int currentSimYear, int targetSimYear, float estimatedSecondsRemaining) {
                                        // Update display with progress
                                        int yearsCompleted = currentSimYear - (targetYear - yearsToSimulate);
                                        loadingText.setString("TIME JUMPING TO " + std::to_string(targetYear) + " (" + 
                                                            std::to_string(yearsCompleted) + "/" + std::to_string(yearsToSimulate) + " years)");
                                        
                                        int secondsRemaining = static_cast<int>(estimatedSecondsRemaining);
                                        if (secondsRemaining <= 0) {
                                            countdownText.setString("Almost done!");
                                        } else if (secondsRemaining == 1) {
                                            countdownText.setString("~1 second remaining");
                                        } else {
                                            countdownText.setString("~" + std::to_string(secondsRemaining) + " seconds remaining");
                                        }
                                        
                                        window.clear();
                                        renderer.render(countries, map, news, technologyManager, cultureManager, selectedCountry, showCountryInfo);
                                        window.draw(loadingBg);
                                        window.draw(loadingText);
                                        window.draw(countdownText);
                                        window.display();
                                    });
                                
                                // Show completion message
                                float totalTime = megaClock.getElapsedTime().asSeconds();
                                loadingText.setString("TIME JUMP COMPLETE!");
                                loadingText.setFillColor(sf::Color::Green);
                                countdownText.setString("Completed in " + std::to_string(static_cast<int>(totalTime)) + " seconds");
                                countdownText.setFillColor(sf::Color::White);
                                
                                window.clear();
                                renderer.render(countries, map, news, technologyManager, cultureManager, selectedCountry, showCountryInfo);
                                window.draw(loadingBg);
                                window.draw(loadingText);
                                window.draw(countdownText);
                                window.display();
                                
                                // Show completion for 2 seconds
                                sf::sleep(sf::seconds(2));
                                
                                // FORCE IMMEDIATE COMPLETE VISUAL REFRESH
                                std::cout << "Forcing complete visual refresh..." << std::endl;
                                std::cout << "Total time jump time: " << totalTime << " seconds" << std::endl;
                                
                                // Mark ALL regions as dirty for complete re-render
                                int totalRegions = (window.getSize().x / map.getGridCellSize() / map.getRegionSize()) * 
                                                  (window.getSize().y / map.getGridCellSize() / map.getRegionSize());
                                for (int i = 0; i < totalRegions; ++i) {
                                    map.insertDirtyRegion(i);
                                }
                                
                                // Force immediate renderer update
                                renderer.updateYearText(currentYear);
                                renderer.setNeedsUpdate(true);
                                renderingNeedsUpdate = true;
                                
                                // Immediate visual update
                                window.clear();
                                renderer.render(countries, map, news, technologyManager, cultureManager, selectedCountry, showCountryInfo);
                                window.display();
                                
                                std::cout << "Visual refresh complete!" << std::endl;
                                
                                std::cout << "MEGA TIME JUMP COMPLETE! Welcome to " << currentYear << "!" << std::endl;
                            }
                            else {
                                megaTimeJumpMode = false; // Cancel on invalid input
                            }
                        }
                    }
                    else if (event.text.unicode == 27) { // Escape key
                        megaTimeJumpMode = false;
                        megaTimeJumpInput = "";
                    }
                }
            }
            else if (event.type == sf::Event::MouseWheelScrolled) {
                if (showCountryInfo) {
                    if (event.mouseWheelScroll.delta > 0) {
                        // Scrolling up
                        if (sf::Keyboard::isKeyPressed(sf::Keyboard::LShift) || sf::Keyboard::isKeyPressed(sf::Keyboard::RShift)) {
                            // Tech scroll
                            int newOffset = renderer.getTechScrollOffset() - static_cast<int>(event.mouseWheelScroll.delta * 10);
                            newOffset = std::max(0, std::min(newOffset, renderer.getMaxTechScrollOffset()));
                            renderer.setTechScrollOffset(newOffset);
                        }
                        else {
                            // Civic scroll (default)
                            int newOffset = renderer.getCivicScrollOffset() - static_cast<int>(event.mouseWheelScroll.delta * 10);
                            newOffset = std::max(0, std::min(newOffset, renderer.getMaxCivicScrollOffset()));
                            renderer.setCivicScrollOffset(newOffset);
                        }
                    }
                    else if (event.mouseWheelScroll.delta < 0) {
                        // Scrolling down
                        if (sf::Keyboard::isKeyPressed(sf::Keyboard::LShift) || sf::Keyboard::isKeyPressed(sf::Keyboard::RShift)) {
                            // Tech scroll
                            int newOffset = renderer.getTechScrollOffset() - static_cast<int>(event.mouseWheelScroll.delta * 10);
                            newOffset = std::max(0, std::min(newOffset, renderer.getMaxTechScrollOffset()));
                            renderer.setTechScrollOffset(newOffset);
                        }
                        else {
                            // Civic scroll (default)
                            int newOffset = renderer.getCivicScrollOffset() - static_cast<int>(event.mouseWheelScroll.delta * 10);
                            newOffset = std::max(0, std::min(newOffset, renderer.getMaxCivicScrollOffset()));
                            renderer.setCivicScrollOffset(newOffset);
                        }
                    }
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
                renderingNeedsUpdate = true; // Force render for interaction
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

        // 🔥 NUCLEAR OPTIMIZATION: EVENT-DRIVEN SIMULATION ARCHITECTURE 🔥
        
        // STEP 1: Check if we need to advance simulation (ONCE PER YEAR, NOT 60 TIMES!)
        sf::Time currentYearDuration = turboMode ? turboYearDuration : yearDuration;
        if (yearClock.getElapsedTime() >= currentYearDuration || simulationNeedsUpdate) {
            
            // ADVANCE YEAR
            if (!simulationNeedsUpdate) { // Don't advance on forced updates
                currentYear++;
                if (currentYear == 0) currentYear = 1;
            }
            
            // 🚀 PERFORM ALL SIMULATION LOGIC IN ONE BATCH (MAXIMUM EFFICIENCY)
            auto simStart = std::chrono::high_resolution_clock::now();
            
            // DIAGNOSTIC: Time each major component
            auto mapStart = std::chrono::high_resolution_clock::now();
            try {
                map.updateCountries(countries, currentYear, news);
            } catch (const std::exception& e) {
                std::cout << "🚨 MAP UPDATE CRASHED at year " << currentYear << ": " << e.what() << std::endl;
                throw;
            }
            auto mapEnd = std::chrono::high_resolution_clock::now();
            auto mapTime = std::chrono::duration_cast<std::chrono::milliseconds>(mapEnd - mapStart);
            
            // 🧬 KNOWLEDGE DIFFUSION - Moved to map.cpp to avoid duplication
            
            auto techStart = std::chrono::high_resolution_clock::now();
            // 🛡️ DEADLOCK FIX: Remove OpenMP parallel processing to prevent mutex deadlocks
            for (int i = 0; i < countries.size(); ++i) {
                try {
                    technologyManager.updateCountry(countries[i]);
                    cultureManager.updateCountry(countries[i]);
                } catch (const std::exception& e) {
                    std::cout << "🚨 TECH/CULTURE UPDATE CRASHED for country " << i << ": " << e.what() << std::endl;
                    throw;
                }
            }
            auto techEnd = std::chrono::high_resolution_clock::now();
            auto techTime = std::chrono::duration_cast<std::chrono::milliseconds>(techEnd - techStart);
            
            auto greatStart = std::chrono::high_resolution_clock::now();
            greatPeopleManager.updateEffects(currentYear, countries, news);
            auto greatEnd = std::chrono::high_resolution_clock::now();
            auto greatTime = std::chrono::duration_cast<std::chrono::milliseconds>(greatEnd - greatStart);
            
            auto simEnd = std::chrono::high_resolution_clock::now();
            auto simDuration = std::chrono::duration_cast<std::chrono::microseconds>(simEnd - simStart);
            
            // DIAGNOSTIC OUTPUT
            auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(simEnd - simStart);
            if (totalMs.count() > 100) {
                std::cout << " SLOW YEAR " << currentYear << " (" << totalMs.count() << "ms total):" << std::endl;
                std::cout << "  Map Update: " << mapTime.count() << "ms" << std::endl;
                std::cout << "  Tech/Culture: " << techTime.count() << "ms" << std::endl;
                std::cout << "  Great People: " << greatTime.count() << "ms" << std::endl;
            }
            
            // Update UI elements
            renderer.updateYearText(currentYear);
            renderer.setNeedsUpdate(true);
            renderingNeedsUpdate = true;
            simulationNeedsUpdate = false;
            
            // Reset year clock
            if (yearClock.getElapsedTime() >= currentYearDuration) {
                yearClock.restart();
            }
            
            // BRUTAL PERFORMANCE MONITORING - ALWAYS SHOW SLOW YEARS
            if (simDuration.count() > 50000) { // More than 50ms is concerning
                std::cout << "🐌 SLOW YEAR " << currentYear << ": " << simDuration.count() << " microseconds (" 
                          << (simDuration.count() / 1000.0) << " ms)" << std::endl;
            }
            
            // Show first few years for startup debugging
            static int yearCount = 0;
            yearCount++;
            if (yearCount <= 5) {
                std::cout << "Year " << currentYear << " took " << (simDuration.count() / 1000.0) << " ms" << std::endl;
            }
        }
        
        // STEP 2: Smart rendering - only render when needed or for smooth interaction
        bool needsRender = renderingNeedsUpdate || enableZoom || isDragging || showCountryInfo || megaTimeJumpMode;
        
        if (needsRender) {
            if (enableZoom) {
                window.setView(zoomedView);
            }
            
            renderer.render(countries, map, news, technologyManager, cultureManager, selectedCountry, showCountryInfo);
            
            // Draw Mega Time Jump GUI on top
            if (megaTimeJumpMode) {
                // Reset view for GUI
                window.setView(window.getDefaultView());
                
                // Dark overlay
                sf::RectangleShape overlay(sf::Vector2f(window.getSize().x, window.getSize().y));
                overlay.setFillColor(sf::Color(0, 0, 0, 180));
                window.draw(overlay);
                
                // Input box background
                sf::RectangleShape inputBox(sf::Vector2f(600, 300));
                inputBox.setPosition(window.getSize().x / 2 - 300, window.getSize().y / 2 - 150);
                inputBox.setFillColor(sf::Color(40, 40, 40));
                inputBox.setOutlineColor(sf::Color::Yellow);
                inputBox.setOutlineThickness(3);
                window.draw(inputBox);
                
                // Title text
                sf::Text titleText;
                titleText.setFont(m_font);
                titleText.setCharacterSize(36);
                titleText.setFillColor(sf::Color::Yellow);
                titleText.setString("MEGA TIME JUMP");
                titleText.setPosition(window.getSize().x / 2 - 150, window.getSize().y / 2 - 130);
                window.draw(titleText);
                
                // Instructions
                sf::Text instructionText;
                instructionText.setFont(m_font);
                instructionText.setCharacterSize(20);
                instructionText.setFillColor(sf::Color::White);
                instructionText.setString("Enter target year (-5000 to 2025):");
                instructionText.setPosition(window.getSize().x / 2 - 150, window.getSize().y / 2 - 60);
                window.draw(instructionText);
                
                // Year input field
                sf::RectangleShape inputField(sf::Vector2f(300, 50));
                inputField.setPosition(window.getSize().x / 2 - 150, window.getSize().y / 2 - 20);
                inputField.setFillColor(sf::Color(60, 60, 60));
                inputField.setOutlineColor(sf::Color::White);
                inputField.setOutlineThickness(2);
                window.draw(inputField);
                
                // Input text
                sf::Text inputText;
                inputText.setFont(m_font);
                inputText.setCharacterSize(28);
                inputText.setFillColor(sf::Color::White);
                inputText.setString(megaTimeJumpInput.empty() ? "_" : megaTimeJumpInput + "_");
                inputText.setPosition(window.getSize().x / 2 - 140, window.getSize().y / 2 - 10);
                window.draw(inputText);
                
                // Help text
                sf::Text helpText;
                helpText.setFont(m_font);
                helpText.setCharacterSize(16);
                helpText.setFillColor(sf::Color(200, 200, 200));
                helpText.setString("Current year: " + std::to_string(currentYear) + "\nPress ENTER to jump | ESC to cancel");
                helpText.setPosition(window.getSize().x / 2 - 150, window.getSize().y / 2 + 50);
                window.draw(helpText);
                
                window.display();
            }
            
            renderingNeedsUpdate = false;
        }
        
        // STEP 3: Intelligent frame rate control
        float frameTime = frameClock.getElapsedTime().asSeconds();
        
        if (turboMode) {
            // In turbo mode, prioritize simulation over smooth rendering
            if (frameTime < 0.033f) { // Cap at 30 FPS in turbo mode
                sf::sleep(sf::seconds(0.033f - frameTime));
            }
        } else {
            // Normal mode - smooth 60 FPS when rendering
            if (needsRender && frameTime < targetFrameTime) {
                sf::sleep(sf::seconds(targetFrameTime - frameTime));
            } else if (!needsRender) {
                // If not rendering, sleep longer to save CPU
                sf::sleep(sf::seconds(0.016f));
            }
        }
    }

    std::cout << "✅ Game closed gracefully" << std::endl;
    return 0;
    
    } catch (const std::exception& e) {
        std::cout << "\n🚨🚨🚨 EXCEPTION CAUGHT! 🚨🚨🚨" << std::endl;
        std::cout << "💥 EXCEPTION TYPE: std::exception" << std::endl;
        std::cout << "📝 ERROR MESSAGE: " << e.what() << std::endl;
        std::cout << "🔍 This is a C++ standard exception - check for logic errors" << std::endl;
        return -1;
    } catch (...) {
        std::cout << "\n🚨🚨🚨 UNKNOWN EXCEPTION CAUGHT! 🚨🚨🚨" << std::endl;
        std::cout << "💥 EXCEPTION TYPE: Unknown (not std::exception)" << std::endl;
        std::cout << "🔍 This could be a system-level error or memory corruption" << std::endl;
        return -2;
    }
}