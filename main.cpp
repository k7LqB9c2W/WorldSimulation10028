// main.cpp

#include <SFML/Graphics.hpp>
#include <vector>
#include <iostream>
#include <chrono>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_set>
#include <omp.h>
#include <csignal>
#include <exception>
#include <stdexcept>
#include <sstream>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include "country.h"
#include "renderer.h"
#include "map.h"
#include "news.h"
#include "technology.h"
#include "great_people.h"
#include "culture.h" // Include the new culture header
#include "trade.h" // Include the new trade system
#include "economy.h"

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

// Global paused state (toggled by Spacebar)
bool paused = false;

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
        
        const std::string windowTitle = "Country Simulator";
        sf::VideoMode fullscreenVideoMode(1920, 1080);
        sf::VideoMode windowedVideoMode(1280, 720);
        bool isFullscreen = true;

        if (std::find(sf::VideoMode::getFullscreenModes().begin(), sf::VideoMode::getFullscreenModes().end(), fullscreenVideoMode) == sf::VideoMode::getFullscreenModes().end()) {
            std::cerr << "Error: 1920x1080 fullscreen mode not available." << std::endl;
            return -1;
        }

        sf::RenderWindow window(fullscreenVideoMode, windowTitle, sf::Style::Fullscreen);

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
    const int maxCountries = 400;
    countries.reserve(maxCountries);

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
    
	    // Initialize the Trade Manager
		    TradeManager tradeManager;

		    // Initialize GPU economy (downsampled econ grid)
		    EconomyGPU economy;
	    EconomyGPU::Config econCfg;
		    econCfg.econCellSize = 6;
		    econCfg.tradeIters = 12;
		    econCfg.updateReadbackEveryNYears = 1;
		    economy.init(map, maxCountries, econCfg);
		    if (!economy.isInitialized()) {
		        std::cout << "⚠️ EconomyGPU disabled (shaders unavailable/init failed). Wealth/GDP/exports will stay 0." << std::endl;
		    }
		    economy.onTerritoryChanged(map);
		    economy.onStaticResourcesChanged(map);

		    Renderer renderer(window, map, waterColor);
		    News news;

		    auto tradeExportsForYear = [&](int year) -> const std::vector<double>* {
		        const auto& v = tradeManager.getLastCountryExports();
		        if (v.empty()) {
		            return nullptr;
		        }
		        const int y = tradeManager.getLastCountryExportsYear();
		        const int dy = year - y;
		        if (dy >= 0 && dy <= 12) { // trade updates can be sparse during fast-forward/mega jump
		            return &v;
		        }
		        return nullptr;
		    };

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

    const sf::Vector2u mapPixelSize = map.getBaseImage().getSize();
    auto buildWorldView = [&](const sf::Vector2u& windowSize) {
        sf::View view(sf::FloatRect(0.f, 0.f, static_cast<float>(mapPixelSize.x), static_cast<float>(mapPixelSize.y)));
        view.setCenter(static_cast<float>(mapPixelSize.x) * 0.5f, static_cast<float>(mapPixelSize.y) * 0.5f);

        float windowRatio = static_cast<float>(windowSize.x) / static_cast<float>(windowSize.y);
        float mapRatio = static_cast<float>(mapPixelSize.x) / static_cast<float>(mapPixelSize.y);
        sf::FloatRect viewport(0.f, 0.f, 1.f, 1.f);

        if (windowRatio > mapRatio) {
            viewport.width = mapRatio / windowRatio;
            viewport.left = (1.f - viewport.width) / 2.f;
        } else if (windowRatio < mapRatio) {
            viewport.height = windowRatio / mapRatio;
            viewport.top = (1.f - viewport.height) / 2.f;
        }

        view.setViewport(viewport);
        return view;
    };

    // Zoom and panning variables
	    bool enableZoom = false;
	    float zoomLevel = 1.0f;
	    sf::View defaultView = buildWorldView(window.getSize());
	    sf::View zoomedView = defaultView;
	    window.setView(defaultView);
	    ViewMode viewMode = ViewMode::Flat2D;
	    sf::Vector2f lastMousePos;
	    bool isDragging = false;
	    bool spacebarDown = false;
	    bool globeRightDragActive = false;
	    bool globeRightDragRotating = false;
	    bool globeRightClickPendingPick = false;
	    sf::Vector2i globeRightPressPos(0, 0);
	    sf::Vector2i globeLastMousePos(0, 0);
	    auto withUiView = [&](auto&& drawFn) {
	        sf::View previousView = window.getView();
	        window.setView(window.getDefaultView());
	        drawFn();
        window.setView(previousView);
    };

    // Country info window variables
    const Country* selectedCountry = nullptr;
    bool showCountryInfo = false;

    // Country add mode variables
    bool countryAddMode = false;

    // Territory paint mode variables
    bool paintMode = false;
    bool paintEraseMode = false; // false=Add, true=Erase
    bool paintAllowOverwrite = false;
    int paintBrushRadius = 8;
    int selectedPaintCountryIndex = -1;
    bool paintStrokeActive = false;
    sf::Vector2i lastPaintCell(-99999, -99999);
    std::vector<int> paintStrokeAffectedCountries;

    // Forced invasion editor variables
    bool forceInvasionMode = false;
    int forcedInvasionAttackerIndex = -1;
    int hoveredCountryIndex = -1;

    // Technology editor variables
    bool techEditorMode = false;
    std::string techEditorInput = "";
    int techEditorCountryIndex = -1;
    
    // Country Add Editor variables
    struct CountryTemplate {
        std::vector<int> unlockedTechnologies;
        std::vector<int> unlockedCultures;
        long long initialPopulation = 5000; // Default starting population
        Country::Type countryType = Country::Type::Pacifist;
        Country::ScienceType scienceType = Country::ScienceType::NS;
        Country::CultureType cultureType = Country::CultureType::NC;
        Country::Ideology ideology = Country::Ideology::Tribal;
        bool useTemplate = false; // If false, use random generation
    };
    
    CountryTemplate customCountryTemplate;
    bool countryAddEditorMode = false;
    std::string editorInput = "";
    int editorState = 0; // 0=tech selection, 1=population, 2=culture selection, 3=type selection, etc.
    int maxTechId = 0;
    int maxCultureId = 0;
    
    // Initialize max IDs for the country editor
    maxTechId = technologyManager.getTechnologies().size();
    maxCultureId = 10; // Assuming 10 culture levels, adjust as needed
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

	    // Mega time jump (background worker) state
	    bool megaTimeJumpRunning = false;
	    bool megaTimeJumpPendingClose = false;
	    int megaTimeJumpStartYear = 0;
	    int megaTimeJumpTargetYear = 0;
	    std::thread megaTimeJumpThread;
	    std::atomic<bool> megaTimeJumpCancelRequested{ false };
	    std::atomic<bool> megaTimeJumpDone{ false };
	    std::atomic<bool> megaTimeJumpCanceled{ false };
	    std::atomic<bool> megaTimeJumpFailed{ false };
	    std::atomic<int> megaTimeJumpProgressYear{ 0 };
	    std::atomic<float> megaTimeJumpEtaSeconds{ -1.0f };
	    std::mutex megaTimeJumpErrorMutex;
	    std::string megaTimeJumpError;
	    std::mutex megaTimeJumpChunkMutex;
	    std::condition_variable megaTimeJumpChunkCv;
	    int megaTimeJumpGpuChunkTicket = 0;
	    int megaTimeJumpGpuChunkAck = 0;
	    int megaTimeJumpGpuChunkEndYear = 0;
	    int megaTimeJumpGpuChunkYears = 0;
	    bool megaTimeJumpGpuChunkActive = false;
	    int megaTimeJumpGpuChunkActiveTicket = 0;
	    int megaTimeJumpGpuChunkRemainingYears = 0;
	    int megaTimeJumpGpuChunkSimYear = 0;
	    bool megaTimeJumpGpuChunkNeedsTerritorySync = false;
	    const int megaTimeJumpGpuYearsPerStep = 10;
	    const int megaTimeJumpGpuTradeItersPerStep = 3;
	    struct MegaTimeJumpThreadGuard {
	        std::thread& thread;
	        std::atomic<bool>& cancel;
	        ~MegaTimeJumpThreadGuard() {
	            cancel.store(true, std::memory_order_relaxed);
	            if (thread.joinable()) {
	                thread.join();
	            }
	        }
	    } megaTimeJumpThreadGuard{ megaTimeJumpThread, megaTimeJumpCancelRequested };

	    while (window.isOpen()) {
	        frameClock.restart(); // Start frame timing
	        
	        sf::Event event;
	        auto tryGetGridUnderMouse = [&](const sf::Vector2i& mousePos, sf::Vector2i& outGrid) -> bool {
	            if (viewMode == ViewMode::Globe) {
	                return renderer.globeScreenToGrid(mousePos, map, outGrid);
	            }
	            sf::Vector2f worldPos = window.mapPixelToCoords(mousePos);
	            outGrid = map.pixelToGrid(worldPos);
	            return true;
	        };
	        while (window.pollEvent(event)) {
	            if (megaTimeJumpRunning) {
	                if (event.type == sf::Event::Closed) {
	                    megaTimeJumpPendingClose = true;
	                    megaTimeJumpCancelRequested.store(true, std::memory_order_relaxed);
	                    megaTimeJumpChunkCv.notify_all();
	                } else if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Escape) {
	                    megaTimeJumpCancelRequested.store(true, std::memory_order_relaxed);
	                    megaTimeJumpChunkCv.notify_all();
	                }
	                continue;
	            }

	            if (event.type == sf::Event::Closed) {
	                window.close();
	            }
	            else if (event.type == sf::Event::KeyPressed) {
                if (techEditorMode) {
                    if (event.key.code == sf::Keyboard::Escape) {
                        techEditorMode = false;
                        techEditorInput.clear();
                    }
                    continue;
                }
                if (megaTimeJumpMode || countryAddEditorMode) {
                    if (event.key.code == sf::Keyboard::Escape) {
                        megaTimeJumpMode = false;
                        megaTimeJumpInput.clear();
                        countryAddEditorMode = false;
                        editorInput.clear();
                        editorState = 0;
                    }
                    continue;
                }

                if (forceInvasionMode && event.key.code == sf::Keyboard::Escape) {
                    forceInvasionMode = false;
                    forcedInvasionAttackerIndex = -1;
                    continue;
                }

                if (event.key.code == sf::Keyboard::Space) {
                    if (!spacebarDown) {
                        spacebarDown = true;
                        if (!megaTimeJumpMode && !countryAddEditorMode && !techEditorMode) {
                            paused = !paused;
                            yearClock.restart(); // Prevent immediate year jump after a long pause
                        }
                    }
                }
	                else if (!megaTimeJumpMode && !countryAddEditorMode && event.key.code == sf::Keyboard::Num0) {
	                    paintMode = !paintMode;
	                    if (paintMode) {
                        countryAddMode = false;
                        renderer.setShowCountryAddModeText(false);
                        forceInvasionMode = false;
                        forcedInvasionAttackerIndex = -1;
                        if (selectedPaintCountryIndex < 0 && selectedCountry != nullptr) {
                            selectedPaintCountryIndex = selectedCountry->getCountryIndex();
                        }
	                    } else if (paintStrokeActive) {
	                        paintStrokeActive = false;
	                        lastPaintCell = sf::Vector2i(-99999, -99999);
	                        if (!paintStrokeAffectedCountries.empty()) {
	                            std::sort(paintStrokeAffectedCountries.begin(), paintStrokeAffectedCountries.end());
		                            paintStrokeAffectedCountries.erase(
		                                std::unique(paintStrokeAffectedCountries.begin(), paintStrokeAffectedCountries.end()),
		                                paintStrokeAffectedCountries.end());
		                            map.rebuildBoundariesForCountries(countries, paintStrokeAffectedCountries);
		                            economy.onTerritoryChanged(map);
		                            renderer.setNeedsUpdate(true);
		                        }
		                        paintStrokeAffectedCountries.clear();
		                    }
	                }
                else if (!megaTimeJumpMode && !countryAddEditorMode && event.key.code == sf::Keyboard::Num1) {
                    paintEraseMode = false;
                }
                else if (!megaTimeJumpMode && !countryAddEditorMode && event.key.code == sf::Keyboard::Num2) {
                    paintEraseMode = true;
                }
                else if (!megaTimeJumpMode && !countryAddEditorMode && event.key.code == sf::Keyboard::R) {
                    paintAllowOverwrite = !paintAllowOverwrite;
                }
                else if (!megaTimeJumpMode && !countryAddEditorMode && event.key.code == sf::Keyboard::LBracket) {
                    paintBrushRadius = std::max(1, paintBrushRadius - 1);
                }
                else if (!megaTimeJumpMode && !countryAddEditorMode && event.key.code == sf::Keyboard::RBracket) {
                    paintBrushRadius = std::min(64, paintBrushRadius + 1);
                }
                else if (!megaTimeJumpMode && !countryAddEditorMode && event.key.code == sf::Keyboard::I) {
                    forceInvasionMode = !forceInvasionMode;
                    forcedInvasionAttackerIndex = -1;
                    if (forceInvasionMode) {
                        paintMode = false;
                        paintStrokeActive = false;
                        countryAddMode = false;
                        renderer.setShowCountryAddModeText(false);
                    }
                }
                else if (event.key.code == sf::Keyboard::F11 ||
                    (event.key.code == sf::Keyboard::Enter && event.key.alt)) {
                    isFullscreen = !isFullscreen;
                    sf::VideoMode targetMode = isFullscreen ? fullscreenVideoMode : windowedVideoMode;
                    sf::Uint32 targetStyle = isFullscreen ? sf::Style::Fullscreen : (sf::Style::Titlebar | sf::Style::Close);
                    sf::Vector2f previousCenter = enableZoom ? zoomedView.getCenter() : defaultView.getCenter();

                    window.create(targetMode, windowTitle, targetStyle);
                    window.setFramerateLimit(60);
                    window.setVerticalSyncEnabled(false);
                    renderer.handleWindowRecreated(map);

                    defaultView = buildWorldView(window.getSize());
                    if (enableZoom) {
                        zoomedView = defaultView;
                        zoomedView.setSize(defaultView.getSize().x * zoomLevel, defaultView.getSize().y * zoomLevel);
                        zoomedView.setCenter(previousCenter);
                        window.setView(zoomedView);
                    }
                    else {
                        window.setView(defaultView);
                    }

                    isDragging = false;
                    renderer.setNeedsUpdate(true);
                    renderer.updateYearText(currentYear);
                    renderingNeedsUpdate = true;
                }
                else if (event.key.code == sf::Keyboard::Num5) {
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
			                else if (event.key.code == sf::Keyboard::L) {
			                    // Ensure GPU economy metrics are up-to-date before showing the leaderboard.
			                    // (GDP requires at least two readbacks at different years; mega-jumps now do a baseline readback.)
			                    economy.onTerritoryChanged(map);
			                    economy.readbackMetrics(currentYear);
			                    economy.applyCountryMetrics(countries, tradeExportsForYear(currentYear));
			                    if (TechnologyManager::getDebugMode()) {
			                        double sumWealth = 0.0, sumGDP = 0.0, sumExports = 0.0;
			                        int alive = 0;
			                        for (const auto& c : countries) {
			                            if (c.getPopulation() <= 0) continue;
			                            alive++;
			                            sumWealth += c.getWealth();
			                            sumGDP += c.getGDP();
			                            sumExports += c.getExports();
			                        }
			                        std::cout << "📈 Economy debug @ year " << currentYear
			                                  << " (alive=" << alive << ")"
			                                  << " totals: wealth=" << sumWealth
			                                  << " gdp=" << sumGDP
			                                  << " exports=" << sumExports
			                                  << " (tradeExportsYear=" << tradeManager.getLastCountryExportsYear() << ")"
			                                  << std::endl;
			                    }
			                    renderer.toggleWealthLeaderboard();
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
                        
                        withUiView([&] {
                            window.clear();
                            window.draw(fastForwardText);
                        });
                        window.display();
                        
	                        std::cout << "🚀 Starting Fast Forward (100 years)..." << std::endl;
	                    
	                        // Baseline readback before fast-forward so GDP can be measured over the jump.
	                        economy.onTerritoryChanged(map);
	                        economy.readbackMetrics(currentYear);
	                        economy.applyCountryMetrics(countries, tradeExportsForYear(currentYear));

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
	                            map.fastForwardSimulation(countries, currentYear, chunkSize, news, technologyManager);
	                            
	                            // 🏪 FAST FORWARD TRADE PROCESSING
	                            std::cout << "   💰 Trade simulation..." << std::endl;
	                            tradeManager.fastForwardTrade(countries, startYear, currentYear, map, technologyManager, news);

	                            // GPU economy fast-forward aligned to simulation years (accumulates GDP/exports per step).
	                            economy.onTerritoryChanged(map);
	                            economy.tickStepGpuOnly(currentYear,
	                                                    map,
	                                                    countries,
	                                                    technologyManager,
	                                                    static_cast<float>(std::max(1, currentYear - startYear)),
	                                                    /*tradeItersOverride*/3,
	                                                    /*heatmap*/false,
	                                                    /*readbackMetricsBeforeDiffusion*/true);
                            
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
                        withUiView([&] {
                            window.clear();
                            window.draw(fastForwardText);
                        });
                        window.display();
                        
                        // 🛡️ PREVENT SYSTEM OVERLOAD: Small pause between chunks
                        sf::sleep(sf::milliseconds(50));
                    }
                    
		                    // Final updates
		                    greatPeopleManager.updateEffects(currentYear, countries, news);
		                    economy.onTerritoryChanged(map);
		                    economy.readbackMetrics(currentYear);
		                    economy.applyCountryMetrics(countries, tradeExportsForYear(currentYear));
	                    
	                    // 🔥 FORCE IMMEDIATE COMPLETE VISUAL REFRESH FOR FAST FORWARD
	                    std::cout << "🎨 Refreshing fast forward visuals..." << std::endl;
                    
                    // Mark ALL regions as dirty for complete re-render
                    int totalRegions = (mapPixelSize.x / map.getGridCellSize() / map.getRegionSize()) * 
                                      (mapPixelSize.y / map.getGridCellSize() / map.getRegionSize());
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
                    renderer.render(countries, map, news, technologyManager, cultureManager, tradeManager, selectedCountry, showCountryInfo, viewMode);
                    withUiView([&] {
                        window.draw(fastForwardText);
                    });
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
                else if (event.key.code == sf::Keyboard::E && !megaTimeJumpMode && !countryAddEditorMode) { // 🧠 TECHNOLOGY EDITOR
                    if (selectedCountry != nullptr) {
                        techEditorMode = true;
                        techEditorInput = "";
                        techEditorCountryIndex = selectedCountry->getCountryIndex();
                        std::cout << "\n🧠 TECHNOLOGY EDITOR ACTIVATED for " << selectedCountry->getName() << "!" << std::endl;
                    } else {
                        std::cout << "Select a country first (click one) to edit its technologies." << std::endl;
                    }
                }
	                else if (event.key.code == sf::Keyboard::Z) { // 🚀 MEGA TIME JUMP MODE
	                    megaTimeJumpMode = true;
	                    megaTimeJumpInput = "";
	                    std::cout << "\n🚀 MEGA TIME JUMP MODE ACTIVATED!" << std::endl;
	                }
	                else if (event.key.code == sf::Keyboard::G && !megaTimeJumpMode && !countryAddEditorMode && !techEditorMode) { // 🌍 TOGGLE GLOBE VIEW
	                    viewMode = (viewMode == ViewMode::Flat2D) ? ViewMode::Globe : ViewMode::Flat2D;
	                    if (viewMode == ViewMode::Globe) {
	                        renderer.resetGlobeView();
	                    }
	                    renderingNeedsUpdate = true;
	                }
	                else if (event.key.code == sf::Keyboard::M) { // 🏗️ COUNTRY ADD EDITOR MODE
	                    countryAddEditorMode = true;
	                    editorInput = "";
	                    editorState = 0;
	                    std::cout << "\n🏗️ COUNTRY ADD EDITOR ACTIVATED!" << std::endl;
	                }
            }
            else if (event.type == sf::Event::TextEntered) {
                // Handle text input for Technology Editor
                if (techEditorMode) {
                    if (event.text.unicode == 8 && !techEditorInput.empty()) { // Backspace
                        techEditorInput.pop_back();
                    }
                    else if (event.text.unicode == 13) { // Enter key
                        if (techEditorCountryIndex >= 0 && techEditorCountryIndex < static_cast<int>(countries.size())) {
                            Country& target = countries[static_cast<size_t>(techEditorCountryIndex)];

                            auto trim = [](std::string s) {
                                size_t start = s.find_first_not_of(" \t\r\n");
                                if (start == std::string::npos) return std::string();
                                size_t end = s.find_last_not_of(" \t\r\n");
                                return s.substr(start, end - start + 1);
                            };

                            auto toLower = [](std::string s) {
                                std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
                                    return static_cast<char>(std::tolower(c));
                                });
                                return s;
                            };

                            auto parseIds = [](const std::string& s) -> std::vector<int> {
                                std::vector<int> ids;
                                std::string token;
                                for (char ch : s) {
                                    if (std::isdigit(static_cast<unsigned char>(ch))) {
                                        token.push_back(ch);
                                    } else {
                                        if (!token.empty()) {
                                            try {
                                                ids.push_back(std::stoi(token));
                                            } catch (...) {}
                                            token.clear();
                                        }
                                    }
                                }
                                if (!token.empty()) {
                                    try {
                                        ids.push_back(std::stoi(token));
                                    } catch (...) {}
                                }
                                return ids;
                            };

                            std::string raw = trim(techEditorInput);
                            std::string lower = toLower(raw);

                            bool includePrereqs = true;
                            std::vector<int> nextTechs;

                            if (lower == "all") {
                                includePrereqs = false;
                                const auto& all = technologyManager.getSortedTechnologyIds();
                                nextTechs.assign(all.begin(), all.end());
                            }
                            else if (lower == "clear") {
                                includePrereqs = false;
                                nextTechs.clear();
                            }
                            else if (lower.rfind("add", 0) == 0) {
                                std::vector<int> toAdd = parseIds(raw);
                                const auto& current = technologyManager.getUnlockedTechnologies(target);
                                nextTechs.assign(current.begin(), current.end());
                                nextTechs.insert(nextTechs.end(), toAdd.begin(), toAdd.end());
                                includePrereqs = true;
                            }
                            else if (lower.rfind("set", 0) == 0) {
                                nextTechs = parseIds(raw);
                                includePrereqs = true;
                            }
                            else if (lower.rfind("remove", 0) == 0) {
                                includePrereqs = false;
                                std::vector<int> toRemove = parseIds(raw);

                                std::unordered_set<int> removeSet(toRemove.begin(), toRemove.end());
                                const auto& techs = technologyManager.getTechnologies();

                                bool changed = true;
                                while (changed) {
                                    changed = false;
                                    for (const auto& kv : techs) {
                                        int id = kv.first;
                                        if (removeSet.count(id)) {
                                            continue;
                                        }
                                        for (int req : kv.second.requiredTechs) {
                                            if (removeSet.count(req)) {
                                                removeSet.insert(id);
                                                changed = true;
                                                break;
                                            }
                                        }
                                    }
                                }

                                const auto& current = technologyManager.getUnlockedTechnologies(target);
                                nextTechs.reserve(current.size());
                                for (int id : current) {
                                    if (!removeSet.count(id)) {
                                        nextTechs.push_back(id);
                                    }
                                }
                            }
                            else {
                                // Default: treat as "set <ids>"
                                nextTechs = parseIds(raw);
                                includePrereqs = true;
                            }

                            technologyManager.setUnlockedTechnologiesForEditor(target, nextTechs, includePrereqs);
                            renderer.setNeedsUpdate(true);
                            renderingNeedsUpdate = true;
                            std::cout << "🧠 Updated technologies for " << target.getName() << " (" 
                                      << technologyManager.getUnlockedTechnologies(target).size() << " unlocked)" << std::endl;
                        }

                        techEditorMode = false;
                        techEditorInput.clear();
                    }
                    else if (event.text.unicode == 27) { // Escape key
                        techEditorMode = false;
                        techEditorInput.clear();
                    }
                    else if ((event.text.unicode >= 32 && event.text.unicode <= 126)) {
                        char c = static_cast<char>(event.text.unicode);
                        if (std::isalnum(static_cast<unsigned char>(c)) || c == ',' || c == ' ' ) {
                            techEditorInput.push_back(c);
                        }
                    }
                }
                // Handle text input for Mega Time Jump
                else if (megaTimeJumpMode) {
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
	                            int targetYear = 0;
	                            try {
	                                targetYear = std::stoi(megaTimeJumpInput);
	                            } catch (...) {
	                                megaTimeJumpMode = false;
	                                megaTimeJumpInput.clear();
	                                break;
	                            }

	                            // Validate year range
	                            if (targetYear >= -5000 && targetYear <= 2025 && targetYear > currentYear) {
	                                megaTimeJumpMode = false;

	                                megaTimeJumpStartYear = currentYear;
	                                megaTimeJumpTargetYear = targetYear;
	                                megaTimeJumpRunning = true;
	                                megaTimeJumpPendingClose = false;

		                                megaTimeJumpCancelRequested.store(false, std::memory_order_relaxed);
		                                megaTimeJumpDone.store(false, std::memory_order_relaxed);
		                                megaTimeJumpCanceled.store(false, std::memory_order_relaxed);
		                                megaTimeJumpFailed.store(false, std::memory_order_relaxed);
		                                megaTimeJumpProgressYear.store(currentYear, std::memory_order_relaxed);
		                                megaTimeJumpEtaSeconds.store(-1.0f, std::memory_order_relaxed);
		                                {
		                                    std::lock_guard<std::mutex> lock(megaTimeJumpChunkMutex);
		                                    megaTimeJumpGpuChunkTicket = 0;
		                                    megaTimeJumpGpuChunkAck = 0;
		                                    megaTimeJumpGpuChunkEndYear = currentYear;
		                                    megaTimeJumpGpuChunkYears = 0;
		                                }
		                                megaTimeJumpGpuChunkActive = false;
		                                megaTimeJumpGpuChunkActiveTicket = 0;
		                                megaTimeJumpGpuChunkRemainingYears = 0;
		                                megaTimeJumpGpuChunkSimYear = currentYear;
		                                megaTimeJumpGpuChunkNeedsTerritorySync = false;

		                                {
		                                    std::lock_guard<std::mutex> lock(megaTimeJumpErrorMutex);
		                                    megaTimeJumpError.clear();
		                                }

	                                if (megaTimeJumpThread.joinable()) {
	                                    megaTimeJumpThread.join();
	                                }

		                                const int yearsToSimulate = megaTimeJumpTargetYear - megaTimeJumpStartYear;
		                                std::cout << "SIMULATING " << yearsToSimulate << " YEARS OF HISTORY!" << std::endl;
		                                std::cout << "From " << megaTimeJumpStartYear << " to " << megaTimeJumpTargetYear << std::endl;

		                                // Baseline readback before mega-jump so GDP can be estimated from capital formation
		                                // across the jump interval (end readback happens after the jump completes).
		                                economy.onTerritoryChanged(map);
		                                economy.readbackMetrics(currentYear);
		                                economy.applyCountryMetrics(countries, tradeExportsForYear(currentYear));

		                                megaTimeJumpThread = std::thread([&] {
		                                    try {
		                                        const bool completed = map.megaTimeJump(
		                                            countries, currentYear, megaTimeJumpTargetYear, news,
		                                            technologyManager, cultureManager, greatPeopleManager,
		                                            [&](int currentSimYear, int targetSimYear, float estimatedSecondsRemaining) {
		                                                (void)targetSimYear;
		                                                megaTimeJumpProgressYear.store(currentSimYear, std::memory_order_relaxed);
		                                                megaTimeJumpEtaSeconds.store(estimatedSecondsRemaining, std::memory_order_relaxed);
		                                            },
		                                            [&](int currentSimYear, int chunkYears) {
		                                                const int chunkStartYear = currentSimYear - chunkYears;
		                                                tradeManager.fastForwardTrade(countries, chunkStartYear, currentSimYear, map, technologyManager, news);

		                                                // Request GPU economy chunk processing on the UI thread (SFML/OpenGL context).
		                                                std::unique_lock<std::mutex> lock(megaTimeJumpChunkMutex);
		                                                megaTimeJumpGpuChunkTicket++;
		                                                megaTimeJumpGpuChunkEndYear = currentSimYear;
		                                                megaTimeJumpGpuChunkYears = chunkYears;
		                                                megaTimeJumpChunkCv.notify_all();
		                                                megaTimeJumpChunkCv.wait(lock, [&] {
		                                                    return megaTimeJumpGpuChunkAck >= megaTimeJumpGpuChunkTicket ||
		                                                           megaTimeJumpCancelRequested.load(std::memory_order_relaxed);
		                                                });
		                                            },
		                                            &megaTimeJumpCancelRequested);

		                                        megaTimeJumpCanceled.store(!completed, std::memory_order_relaxed);
		                                    } catch (const std::exception& e) {
	                                        megaTimeJumpFailed.store(true, std::memory_order_relaxed);
	                                        std::lock_guard<std::mutex> lock(megaTimeJumpErrorMutex);
	                                        megaTimeJumpError = e.what();
	                                    } catch (...) {
	                                        megaTimeJumpFailed.store(true, std::memory_order_relaxed);
	                                        std::lock_guard<std::mutex> lock(megaTimeJumpErrorMutex);
	                                        megaTimeJumpError = "Unknown error";
	                                    }

	                                    megaTimeJumpDone.store(true, std::memory_order_relaxed);
	                                });
	                            } else {
	                                megaTimeJumpMode = false; // Cancel on invalid input
	                            }
	                        }
	                    }
                    else if (event.text.unicode == 27) { // Escape key
                        megaTimeJumpMode = false;
                        megaTimeJumpInput = "";
                    }
                }
                // Handle text input for Country Add Editor
                else if (countryAddEditorMode) {
                    if (event.text.unicode >= '0' && event.text.unicode <= '9') {
                        editorInput += static_cast<char>(event.text.unicode);
                    }
                    else if (event.text.unicode == ',' && !editorInput.empty()) {
                        editorInput += ",";
                    }
                    else if ((event.text.unicode >= 'a' && event.text.unicode <= 'z') || 
                             (event.text.unicode >= 'A' && event.text.unicode <= 'Z')) {
                        editorInput += static_cast<char>(event.text.unicode);
                    }
                    else if (event.text.unicode == 8 && !editorInput.empty()) { // Backspace
                        editorInput.pop_back();
                    }
                    else if (event.text.unicode == 13) { // Enter key
                        // Process input based on current editor state
                        if (editorState == 0) { // Technology selection
                            customCountryTemplate.unlockedTechnologies.clear();
                            
                            // Check if user wants to unlock all
                            if (editorInput == "all" || editorInput == "ALL") {
                                // Unlock all technologies
                                for (int i = 1; i <= maxTechId; i++) {
                                    customCountryTemplate.unlockedTechnologies.push_back(i);
                                }
                                std::cout << "🚀 ALL " << maxTechId << " TECHNOLOGIES UNLOCKED!" << std::endl;
                            } else {
                                // Parse comma-separated technology IDs
                                std::stringstream ss(editorInput);
                                std::string item;
                                while (std::getline(ss, item, ',')) {
                                    try {
                                        int techId = std::stoi(item);
                                        if (techId >= 1 && techId <= maxTechId) {
                                            customCountryTemplate.unlockedTechnologies.push_back(techId);
                                        }
                                    } catch (const std::exception&) {
                                        // Skip invalid entries
                                    }
                                }
                                std::cout << "🔧 " << customCountryTemplate.unlockedTechnologies.size() << " technologies selected" << std::endl;
                            }
                            editorState = 1; // Move to population selection
                            editorInput = "";
                        }
                        else if (editorState == 1) { // Population input
                            long long population = std::stoll(editorInput);
                            if (population > 0 && population <= 1000000000) { // Reasonable range
                                customCountryTemplate.initialPopulation = population;
                            }
                            editorState = 2; // Move to culture selection
                            editorInput = "";
                        }
                        else if (editorState == 2) { // Culture selection
                            customCountryTemplate.unlockedCultures.clear();
                            
                            // Check if user wants to unlock all
                            if (editorInput == "all" || editorInput == "ALL") {
                                // Unlock all cultures
                                for (int i = 1; i <= maxCultureId; i++) {
                                    customCountryTemplate.unlockedCultures.push_back(i);
                                }
                                std::cout << "🎭 ALL " << maxCultureId << " CULTURES UNLOCKED!" << std::endl;
                            } else {
                                // Parse comma-separated culture IDs
                                std::stringstream ss(editorInput);
                                std::string item;
                                while (std::getline(ss, item, ',')) {
                                    try {
                                        int cultureId = std::stoi(item);
                                        if (cultureId >= 1 && cultureId <= maxCultureId) {
                                            customCountryTemplate.unlockedCultures.push_back(cultureId);
                                        }
                                    } catch (const std::exception&) {
                                        // Skip invalid entries
                                    }
                                }
                                std::cout << "🎭 " << customCountryTemplate.unlockedCultures.size() << " cultures selected" << std::endl;
                            }
                            editorState = 3; // Move to type selection
                            editorInput = "";
                        }
                        else if (editorState == 3) { // Country type selection
                            int typeChoice = std::stoi(editorInput);
                            if (typeChoice >= 1 && typeChoice <= 3) {
                                customCountryTemplate.countryType = static_cast<Country::Type>(typeChoice - 1);
                            }
                            editorState = 4; // Move to science type selection
                            editorInput = "";
                        }
                        else if (editorState == 4) { // Science type selection
                            int scienceChoice = std::stoi(editorInput);
                            if (scienceChoice >= 1 && scienceChoice <= 3) {
                                customCountryTemplate.scienceType = static_cast<Country::ScienceType>(scienceChoice - 1);
                            }
                            editorState = 5; // Move to culture type selection
                            editorInput = "";
                        }
                        else if (editorState == 5) { // Culture type selection
                            int cultureChoice = std::stoi(editorInput);
                            if (cultureChoice >= 1 && cultureChoice <= 3) {
                                customCountryTemplate.cultureType = static_cast<Country::CultureType>(cultureChoice - 1);
                            }
                            editorState = 6; // Move to save/reset
                            editorInput = "";
                        }
                        else if (editorState == 6) { // Save or Reset
                            int choice = std::stoi(editorInput);
                            if (choice == 1) { // Save
                                customCountryTemplate.useTemplate = true;
                                std::cout << "✅ COUNTRY TEMPLATE SAVED!" << std::endl;
                                std::cout << "   Technologies: " << customCountryTemplate.unlockedTechnologies.size();
                                if (customCountryTemplate.unlockedTechnologies.size() == maxTechId) {
                                    std::cout << " (ALL UNLOCKED!)";
                                }
                                std::cout << std::endl;
                                std::cout << "   Cultures: " << customCountryTemplate.unlockedCultures.size();
                                if (customCountryTemplate.unlockedCultures.size() == maxCultureId) {
                                    std::cout << " (ALL UNLOCKED!)";
                                }
                                std::cout << std::endl;
                                std::cout << "   Population: " << customCountryTemplate.initialPopulation << std::endl;
                                std::cout << "   Type: " << static_cast<int>(customCountryTemplate.countryType) << std::endl;
                            }
                            else if (choice == 2) { // Reset
                                customCountryTemplate.useTemplate = false;
                                customCountryTemplate.unlockedTechnologies.clear();
                                customCountryTemplate.unlockedCultures.clear();
                                customCountryTemplate.initialPopulation = 5000;
                                std::cout << "🔄 RESET TO RANDOM GENERATION!" << std::endl;
                            }
                            countryAddEditorMode = false;
                            editorInput = "";
                            editorState = 0;
                        }
                    }
                    else if (event.text.unicode == 27) { // Escape key
                        countryAddEditorMode = false;
                        editorInput = "";
                        editorState = 0;
                    }
                }
            }
            else if (event.type == sf::Event::MouseWheelScrolled) {
                if (paintMode &&
                    (sf::Keyboard::isKeyPressed(sf::Keyboard::LControl) || sf::Keyboard::isKeyPressed(sf::Keyboard::RControl))) {
                    int delta = (event.mouseWheelScroll.delta > 0) ? 1 : -1;
                    paintBrushRadius = std::max(1, std::min(64, paintBrushRadius + delta));
                }
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
	                else if (viewMode == ViewMode::Globe) {
	                    renderer.addGlobeRadiusScale(event.mouseWheelScroll.delta * 0.02f);
	                    renderingNeedsUpdate = true;
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
            else if (event.type == sf::Event::MouseButtonPressed && event.mouseButton.button == sf::Mouse::Right) {
                if (viewMode == ViewMode::Globe) {
                    sf::Vector2i mousePos = sf::Mouse::getPosition(window);
                    globeRightDragActive = true;
                    globeRightDragRotating = false;
                    globeRightClickPendingPick = paintMode && !megaTimeJumpMode && !countryAddEditorMode;
                    globeRightPressPos = mousePos;
                    globeLastMousePos = mousePos;
                    continue;
                }

                if (paintMode && !megaTimeJumpMode && !countryAddEditorMode) {
                    sf::Vector2i mousePos = sf::Mouse::getPosition(window);
                    sf::Vector2i gridPos;
                    if (tryGetGridUnderMouse(mousePos, gridPos) &&
                        gridPos.x >= 0 && gridPos.x < static_cast<int>(map.getCountryGrid()[0].size()) &&
                        gridPos.y >= 0 && gridPos.y < static_cast<int>(map.getCountryGrid().size())) {
                        int owner = map.getCountryGrid()[gridPos.y][gridPos.x];
                        if (owner >= 0 && owner < static_cast<int>(countries.size())) {
                            selectedPaintCountryIndex = owner;
                        }
                    }
                }
            }
            else if (event.type == sf::Event::MouseButtonPressed && event.mouseButton.button == sf::Mouse::Left) {
                renderingNeedsUpdate = true; // Force render for interaction
                {
                    sf::Vector2i mousePos = sf::Mouse::getPosition(window);
                    const sf::FloatRect toggleRect = renderer.getViewToggleButtonBounds();
                    if (!megaTimeJumpMode && !countryAddEditorMode && !techEditorMode &&
                        toggleRect.contains(static_cast<float>(mousePos.x), static_cast<float>(mousePos.y))) {
                        viewMode = (viewMode == ViewMode::Flat2D) ? ViewMode::Globe : ViewMode::Flat2D;
                        if (viewMode == ViewMode::Globe) {
                            renderer.resetGlobeView();
                        }
                        renderingNeedsUpdate = true;
                        continue;
                    }
                }
	                if (forceInvasionMode && !megaTimeJumpMode && !countryAddEditorMode && !techEditorMode && !paintMode) {
	                    sf::Vector2i mousePos = sf::Mouse::getPosition(window);
	                    sf::Vector2i gridPos;
	                    if (!tryGetGridUnderMouse(mousePos, gridPos)) {
	                        continue;
	                    }

                    int owner = -1;
                    if (gridPos.x >= 0 && gridPos.x < static_cast<int>(map.getCountryGrid()[0].size()) &&
                        gridPos.y >= 0 && gridPos.y < static_cast<int>(map.getCountryGrid().size())) {
                        owner = map.getCountryGrid()[gridPos.y][gridPos.x];
                    }

                    if (owner >= 0 && owner < static_cast<int>(countries.size()) &&
                        countries[static_cast<size_t>(owner)].getCountryIndex() == owner &&
                        countries[static_cast<size_t>(owner)].getPopulation() > 0 &&
                        !countries[static_cast<size_t>(owner)].getBoundaryPixels().empty()) {
                        if (forcedInvasionAttackerIndex < 0) {
                            forcedInvasionAttackerIndex = owner;
                            selectedCountry = &countries[static_cast<size_t>(owner)];
                            showCountryInfo = true;
                        } else if (owner != forcedInvasionAttackerIndex) {
                            Country& attacker = countries[static_cast<size_t>(forcedInvasionAttackerIndex)];
                            Country& defender = countries[static_cast<size_t>(owner)];

                            attacker.clearWarState();
                            attacker.startWar(defender, news);
                            attacker.setWarofConquest(true);
                            attacker.setWarofAnnihilation(false);
                            attacker.setWarDuration(120);

                            news.addEvent("⚔️ FORCED INVASION: " + attacker.getName() + " invades " + defender.getName() + "!");

                            forceInvasionMode = false;
                            forcedInvasionAttackerIndex = -1;
                            renderer.setNeedsUpdate(true);
                        }
                    }
                }
	                else if (paintMode && !megaTimeJumpMode && !countryAddEditorMode) {
                    isDragging = false;
                    paintStrokeActive = false;
                    paintStrokeAffectedCountries.clear();

	                    sf::Vector2i mousePos = sf::Mouse::getPosition(window);
	                    sf::Vector2i gridPos;
	                    if (!tryGetGridUnderMouse(mousePos, gridPos)) {
	                        continue;
	                    }

                    if (gridPos.x >= 0 && gridPos.x < static_cast<int>(map.getCountryGrid()[0].size()) &&
                        gridPos.y >= 0 && gridPos.y < static_cast<int>(map.getCountryGrid().size())) {
                        int paintCountry = paintEraseMode ? -1 : selectedPaintCountryIndex;
                        if (paintEraseMode || paintCountry >= 0) {
                            bool changed = map.paintCells(paintCountry, gridPos, paintBrushRadius, paintEraseMode, paintAllowOverwrite, paintStrokeAffectedCountries);
                            paintStrokeActive = true;
                            lastPaintCell = gridPos;
                            if (changed) {
                                renderer.setNeedsUpdate(true);
                                if (!paused) {
                                    yearClock.restart();
                                }
                            }
                        }
                    }
	                }
	                else if (viewMode == ViewMode::Flat2D && enableZoom) {
	                    isDragging = true;
	                    lastMousePos = window.mapPixelToCoords(sf::Mouse::getPosition(window));
	                }
	                else if (countryAddMode) {
	                    // Add new country
	                    sf::Vector2i mousePos = sf::Mouse::getPosition(window);
	                    sf::Vector2i gridPos;
	                    if (!tryGetGridUnderMouse(mousePos, gridPos)) {
	                        continue;
	                    }

                    if (gridPos.x >= 0 && gridPos.x < map.getIsLandGrid()[0].size() &&
                        gridPos.y >= 0 && gridPos.y < map.getIsLandGrid().size() &&
                        map.getIsLandGrid()[gridPos.y][gridPos.x] && map.getCountryGrid()[gridPos.y][gridPos.x] == -1) {
                        if (static_cast<int>(countries.size()) >= maxCountries) {
                            std::cout << "dY\"? Max country limit reached (" << maxCountries << ")." << std::endl;
                            continue;
                        }

                        std::random_device rd;
                        std::mt19937 gen(rd());
                        std::uniform_int_distribution<> colorDist(50, 255);
                        std::uniform_int_distribution<> popDist(1000, 10000);
                        std::uniform_real_distribution<> growthRateDist(0.0003, 0.001);
                        std::uniform_int_distribution<> typeDist(0, 2);
                        std::discrete_distribution<> scienceTypeDist({ 50, 40, 10 });
                        std::discrete_distribution<> cultureTypeDist({ 40, 40, 20 });

                        sf::Color countryColor(colorDist(gen), colorDist(gen), colorDist(gen));
                        double growthRate = growthRateDist(gen);
                        std::string countryName = generate_country_name();
                        while (isNameTaken(countries, countryName)) {
                            countryName = generate_country_name();
                        }
                        countryName += " Tribe";
                        
                        // Use custom template if available, otherwise use random generation
                        long long initialPopulation;
                        Country::Type countryType;
                        Country::ScienceType scienceType;
                        Country::CultureType cultureType;
                        
                        if (customCountryTemplate.useTemplate) {
                            initialPopulation = customCountryTemplate.initialPopulation;
                            countryType = customCountryTemplate.countryType;
                            scienceType = customCountryTemplate.scienceType;
                            cultureType = customCountryTemplate.cultureType;
                        } else {
                            initialPopulation = popDist(gen);
                            countryType = static_cast<Country::Type>(typeDist(gen));
                            scienceType = static_cast<Country::ScienceType>(scienceTypeDist(gen));
                            cultureType = static_cast<Country::CultureType>(cultureTypeDist(gen));
                        }

                        int newCountryIndex = countries.size();
                        countries.emplace_back(newCountryIndex, countryColor, gridPos, initialPopulation, growthRate, countryName, countryType, scienceType, cultureType);

                        // Use Map's methods to update the grid and dirty regions
                        map.setCountryGridValue(gridPos.x, gridPos.y, newCountryIndex);

                        int regionIndex = static_cast<int>((gridPos.y / map.getRegionSize()) * (map.getBaseImage().getSize().x / map.getGridCellSize() / map.getRegionSize()) + (gridPos.x / map.getRegionSize()));
                        map.insertDirtyRegion(regionIndex);

                        // Apply custom template technologies and cultures if available
                        if (customCountryTemplate.useTemplate) {
                            Country& newCountry = countries.back();
                            
                            // Unlock specified technologies
                            for (int techId : customCountryTemplate.unlockedTechnologies) {
                                if (technologyManager.canUnlockTechnology(newCountry, techId)) {
                                    technologyManager.unlockTechnology(newCountry, techId);
                                }
                            }
                            
                            // Unlock specified cultures (assuming similar structure to technologies)
                            for (int cultureId : customCountryTemplate.unlockedCultures) {
                                // Note: You may need to implement culture unlocking in CultureManager
                                // cultureManager.unlockCulture(newCountry, cultureId);
                            }
                            
                            std::cout << "✅ CREATED CUSTOM COUNTRY: " << newCountry.getName() 
                                      << " with " << customCountryTemplate.unlockedTechnologies.size() 
                                      << " technologies and " << initialPopulation << " population!" << std::endl;
                        }

                        renderer.setNeedsUpdate(true);

                        // Smooth workflow: auto-select and switch into paint mode for the new country
                        selectedPaintCountryIndex = newCountryIndex;
                        selectedCountry = &countries[static_cast<size_t>(newCountryIndex)];
                        paintMode = true;
                        paintEraseMode = false;
                        paintAllowOverwrite = false;
                        paintBrushRadius = 10;
                        countryAddMode = false;
                        renderer.setShowCountryAddModeText(false);
                    }
                }
	                else {
	                    // Check if a country is clicked
	                    sf::Vector2i mousePos = sf::Mouse::getPosition(window);
	                    sf::Vector2i gridPos;
	                    if (!tryGetGridUnderMouse(mousePos, gridPos)) {
	                        showCountryInfo = false;
	                        continue;
	                    }

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
                if (paintStrokeActive) {
                    paintStrokeActive = false;
                    lastPaintCell = sf::Vector2i(-99999, -99999);
                    if (!paintStrokeAffectedCountries.empty()) {
                        std::sort(paintStrokeAffectedCountries.begin(), paintStrokeAffectedCountries.end());
	                        paintStrokeAffectedCountries.erase(
	                            std::unique(paintStrokeAffectedCountries.begin(), paintStrokeAffectedCountries.end()),
	                            paintStrokeAffectedCountries.end());
	                        map.rebuildBoundariesForCountries(countries, paintStrokeAffectedCountries);
	                        renderer.setNeedsUpdate(true);
	                    }
		                    paintStrokeAffectedCountries.clear();
		                }
            }
            else if (event.type == sf::Event::MouseButtonReleased && event.mouseButton.button == sf::Mouse::Right) {
                if (viewMode == ViewMode::Globe && globeRightDragActive) {
                    globeRightDragActive = false;
                    if (globeRightClickPendingPick && !globeRightDragRotating) {
                        sf::Vector2i mousePos = sf::Mouse::getPosition(window);
                        sf::Vector2i gridPos;
                        if (tryGetGridUnderMouse(mousePos, gridPos) &&
                            gridPos.x >= 0 && gridPos.x < static_cast<int>(map.getCountryGrid()[0].size()) &&
                            gridPos.y >= 0 && gridPos.y < static_cast<int>(map.getCountryGrid().size())) {
                            int owner = map.getCountryGrid()[gridPos.y][gridPos.x];
                            if (owner >= 0 && owner < static_cast<int>(countries.size())) {
                                selectedPaintCountryIndex = owner;
                            }
                        }
                    }
                    globeRightClickPendingPick = false;
                    globeRightDragRotating = false;
                }
            }
            else if (event.type == sf::Event::KeyReleased) {
                if (event.key.code == sf::Keyboard::Space) {
                    spacebarDown = false;
                }
            }
            else if (event.type == sf::Event::MouseMoved) {
                if (viewMode == ViewMode::Globe && globeRightDragActive) {
                    sf::Vector2i mousePos = sf::Mouse::getPosition(window);
                    sf::Vector2i fromPress(mousePos.x - globeRightPressPos.x, mousePos.y - globeRightPressPos.y);
                    if (!globeRightDragRotating && (std::abs(fromPress.x) + std::abs(fromPress.y) >= 4)) {
                        globeRightDragRotating = true;
                        globeRightClickPendingPick = false;
                    }
                    if (globeRightDragRotating) {
                        sf::Vector2i delta(mousePos.x - globeLastMousePos.x, mousePos.y - globeLastMousePos.y);
                        renderer.addGlobeRotation(static_cast<float>(delta.x) * 0.006f,
                                                 static_cast<float>(delta.y) * 0.006f);
                        renderingNeedsUpdate = true;
                    }
                    globeLastMousePos = mousePos;
                }

                // Hover tracking (for highlight + selection tools)
                if (!megaTimeJumpMode && !countryAddEditorMode && !techEditorMode) {
                    sf::Vector2i mousePos = sf::Mouse::getPosition(window);
                    sf::Vector2i gridPos;
                    if (tryGetGridUnderMouse(mousePos, gridPos) &&
                        gridPos.x >= 0 && gridPos.x < static_cast<int>(map.getCountryGrid()[0].size()) &&
                        gridPos.y >= 0 && gridPos.y < static_cast<int>(map.getCountryGrid().size())) {
                        int owner = map.getCountryGrid()[gridPos.y][gridPos.x];
                        if (owner >= 0 && owner < static_cast<int>(countries.size()) &&
                            countries[static_cast<size_t>(owner)].getCountryIndex() == owner &&
                            countries[static_cast<size_t>(owner)].getPopulation() > 0) {
                            hoveredCountryIndex = owner;
                        } else {
                            hoveredCountryIndex = -1;
                        }
                    } else {
                        hoveredCountryIndex = -1;
                    }
                }

                if (paintStrokeActive && paintMode && !megaTimeJumpMode && !countryAddEditorMode) {
                    sf::Vector2i mousePos = sf::Mouse::getPosition(window);
                    sf::Vector2i gridPos;
                    if (!tryGetGridUnderMouse(mousePos, gridPos)) {
                        continue;
                    }

                    if (gridPos != lastPaintCell &&
                        gridPos.x >= 0 && gridPos.x < static_cast<int>(map.getCountryGrid()[0].size()) &&
                        gridPos.y >= 0 && gridPos.y < static_cast<int>(map.getCountryGrid().size())) {
                        int paintCountry = paintEraseMode ? -1 : selectedPaintCountryIndex;
                        if (paintEraseMode || paintCountry >= 0) {
                            bool changed = map.paintCells(paintCountry, gridPos, paintBrushRadius, paintEraseMode, paintAllowOverwrite, paintStrokeAffectedCountries);
                            lastPaintCell = gridPos;
                            if (changed) {
                                renderer.setNeedsUpdate(true);
                                if (!paused) {
                                    yearClock.restart();
                                }
                            }
                        }
                    }
                }

                if (viewMode == ViewMode::Flat2D && isDragging && enableZoom) {
                    sf::Vector2f currentMousePos = window.mapPixelToCoords(sf::Mouse::getPosition(window));
                    sf::Vector2f delta = lastMousePos - currentMousePos;
                    zoomedView.move(delta);
                    lastMousePos = currentMousePos;
                }
		            }
	        }

	        if (megaTimeJumpRunning) {
	            // Process GPU economy updates for the jump on the UI thread (SFML/OpenGL context).
	            if (megaTimeJumpCancelRequested.load(std::memory_order_relaxed)) {
	                {
	                    std::lock_guard<std::mutex> lock(megaTimeJumpChunkMutex);
	                    if (megaTimeJumpGpuChunkAck < megaTimeJumpGpuChunkTicket) {
	                        megaTimeJumpGpuChunkAck = megaTimeJumpGpuChunkTicket;
	                    }
	                }
	                megaTimeJumpGpuChunkActive = false;
	                megaTimeJumpGpuChunkNeedsTerritorySync = false;
	                megaTimeJumpChunkCv.notify_all();
	            }

	            {
	                std::lock_guard<std::mutex> lock(megaTimeJumpChunkMutex);
	                if (!megaTimeJumpGpuChunkActive && megaTimeJumpGpuChunkTicket > megaTimeJumpGpuChunkAck) {
	                    megaTimeJumpGpuChunkActive = true;
	                    megaTimeJumpGpuChunkActiveTicket = megaTimeJumpGpuChunkTicket;
	                    megaTimeJumpGpuChunkRemainingYears = megaTimeJumpGpuChunkYears;
	                    megaTimeJumpGpuChunkSimYear = megaTimeJumpGpuChunkEndYear - megaTimeJumpGpuChunkYears;
	                    megaTimeJumpGpuChunkNeedsTerritorySync = true;
	                }
	            }

	            if (megaTimeJumpGpuChunkActive && megaTimeJumpGpuChunkNeedsTerritorySync) {
	                economy.onTerritoryChanged(map);
	                megaTimeJumpGpuChunkNeedsTerritorySync = false;
	            }

	            if (megaTimeJumpGpuChunkActive && !megaTimeJumpCancelRequested.load(std::memory_order_relaxed)) {
	                const int step = std::max(1, megaTimeJumpGpuYearsPerStep);
	                const int thisStep = std::min(step, megaTimeJumpGpuChunkRemainingYears);
	                if (thisStep > 0) {
	                    megaTimeJumpGpuChunkSimYear += thisStep;
	                    economy.tickStepGpuOnly(megaTimeJumpGpuChunkSimYear,
	                                            map,
	                                            countries,
	                                            technologyManager,
	                                            static_cast<float>(thisStep),
	                                            megaTimeJumpGpuTradeItersPerStep,
	                                            /*heatmap*/false,
	                                            /*readbackMetricsBeforeDiffusion*/true);
	                    megaTimeJumpGpuChunkRemainingYears -= thisStep;
	                }

	                if (megaTimeJumpGpuChunkRemainingYears <= 0) {
	                    {
	                        std::lock_guard<std::mutex> lock(megaTimeJumpChunkMutex);
	                        megaTimeJumpGpuChunkAck = megaTimeJumpGpuChunkActiveTicket;
	                    }
	                    megaTimeJumpGpuChunkActive = false;
	                    megaTimeJumpChunkCv.notify_all();
	                }
	            }

	            if (megaTimeJumpDone.load(std::memory_order_relaxed)) {
	                if (megaTimeJumpThread.joinable()) {
	                    megaTimeJumpThread.join();
	                }

	                megaTimeJumpRunning = false;
	                megaTimeJumpDone.store(false, std::memory_order_relaxed);
	                megaTimeJumpGpuChunkActive = false;
	                megaTimeJumpGpuChunkNeedsTerritorySync = false;

	                // Prevent an immediate real-time year jump after a long background run.
	                yearClock.restart();

	                if (megaTimeJumpPendingClose) {
	                    window.close();
	                    continue;
	                }

	                if (megaTimeJumpFailed.load(std::memory_order_relaxed)) {
	                    std::string err;
	                    {
	                        std::lock_guard<std::mutex> lock(megaTimeJumpErrorMutex);
	                        err = megaTimeJumpError;
	                    }
	                    std::cout << "🚨 MEGA TIME JUMP FAILED: " << err << std::endl;
	                } else {
	                    const bool wasCanceled = megaTimeJumpCanceled.load(std::memory_order_relaxed);
	                    std::cout << (wasCanceled ? "🛑 MEGA TIME JUMP CANCELED" : "✅ MEGA TIME JUMP COMPLETE")
	                              << "! Welcome to " << currentYear << "!" << std::endl;

	                    // Force a full visual refresh and update economy metrics after the jump.
	                    int totalRegions = (mapPixelSize.x / map.getGridCellSize() / map.getRegionSize()) *
	                                      (mapPixelSize.y / map.getGridCellSize() / map.getRegionSize());
	                    for (int i = 0; i < totalRegions; ++i) {
	                        map.insertDirtyRegion(i);
	                    }

	                    economy.onTerritoryChanged(map);
	                    economy.readbackMetrics(currentYear);
	                    economy.applyCountryMetrics(countries, tradeExportsForYear(currentYear));

	                    renderer.updateYearText(currentYear);
	                    renderer.setNeedsUpdate(true);
	                    renderingNeedsUpdate = true;
	                }
	            } else {
	                const int simYear = megaTimeJumpProgressYear.load(std::memory_order_relaxed);
	                const int totalYears = megaTimeJumpTargetYear - megaTimeJumpStartYear;
	                const int yearsDone = std::clamp(simYear - megaTimeJumpStartYear, 0, std::max(0, totalYears));

	                const float eta = megaTimeJumpEtaSeconds.load(std::memory_order_relaxed);
	                const bool canceling = megaTimeJumpCancelRequested.load(std::memory_order_relaxed);

	                sf::RectangleShape bg(sf::Vector2f(window.getSize().x, window.getSize().y));
	                bg.setFillColor(sf::Color(20, 20, 20));

	                sf::Text title;
	                title.setFont(m_font);
	                title.setCharacterSize(42);
	                title.setFillColor(sf::Color::Yellow);
	                title.setString("MEGA TIME JUMP");
	                title.setPosition(50, 40);

	                sf::Text line1;
	                line1.setFont(m_font);
	                line1.setCharacterSize(28);
	                line1.setFillColor(sf::Color::White);
	                line1.setString("Target: " + std::to_string(megaTimeJumpTargetYear) + " | Current: " + std::to_string(simYear));
	                line1.setPosition(50, 120);

	                sf::Text line2;
	                line2.setFont(m_font);
	                line2.setCharacterSize(28);
	                line2.setFillColor(sf::Color::Cyan);
	                if (totalYears > 0) {
	                    line2.setString("Progress: " + std::to_string(yearsDone) + "/" + std::to_string(totalYears) + " years");
	                } else {
	                    line2.setString("Progress: 0/0 years");
	                }
	                line2.setPosition(50, 170);

	                sf::Text line3;
	                line3.setFont(m_font);
	                line3.setCharacterSize(24);
	                line3.setFillColor(sf::Color(200, 200, 200));
	                if (canceling) {
	                    line3.setString("Canceling... (ESC)");
	                } else if (eta >= 0.0f) {
	                    std::string s = "ETA: ~" + std::to_string(static_cast<int>(eta)) + "s | Press ESC to cancel";
	                    if (megaTimeJumpGpuChunkActive) {
	                        s += " | Updating economy...";
	                    }
	                    line3.setString(s);
	                } else {
	                    std::string s = "Estimating... | Press ESC to cancel";
	                    if (megaTimeJumpGpuChunkActive) {
	                        s += " | Updating economy...";
	                    }
	                    line3.setString(s);
	                }
	                line3.setPosition(50, 220);

	                window.setView(window.getDefaultView());
	                window.clear();
	                window.draw(bg);
	                window.draw(title);
	                window.draw(line1);
	                window.draw(line2);
	                window.draw(line3);
	                window.display();

	                float frameTime = frameClock.getElapsedTime().asSeconds();
	                if (frameTime < targetFrameTime) {
	                    sf::sleep(sf::seconds(targetFrameTime - frameTime));
	                }
	            }

	            continue;
	        }

	        renderer.setHoveredCountryIndex(hoveredCountryIndex);

	        // Paint HUD (shown even when OFF to make controls discoverable)
	        std::string paintCountryName = "<none>";
        if (selectedPaintCountryIndex >= 0 && selectedPaintCountryIndex < static_cast<int>(countries.size())) {
            paintCountryName = countries[static_cast<size_t>(selectedPaintCountryIndex)].getName();
        }
        std::string paintHud = "Paint: ";
        paintHud += paintMode ? "ON" : "OFF";
        paintHud += " (Num0) | ";
        paintHud += paintEraseMode ? "Erase" : "Add";
        paintHud += " (1/2) | Radius: " + std::to_string(paintBrushRadius) + " ([/], Ctrl+Wheel) | ";
        paintHud += "Replace: ";
        paintHud += paintAllowOverwrite ? "ON" : "OFF";
        paintHud += " (R) | Country: " + paintCountryName + " (Right Click)";

        if (forceInvasionMode) {
            paintHud += " | Invade: ON (I)";
            if (forcedInvasionAttackerIndex >= 0 && forcedInvasionAttackerIndex < static_cast<int>(countries.size())) {
                paintHud += " Attacker: " + countries[static_cast<size_t>(forcedInvasionAttackerIndex)].getName() + " -> click target";
            } else {
                paintHud += " click attacker";
            }
        }
        renderer.setPaintHud(true, paintHud);

        // 🔥 NUCLEAR OPTIMIZATION: EVENT-DRIVEN SIMULATION ARCHITECTURE 🔥
        
        // STEP 1: Check if we need to advance simulation (ONCE PER YEAR, NOT 60 TIMES!)
        bool uiModalActive = megaTimeJumpMode || countryAddEditorMode || techEditorMode;
        if (uiModalActive) {
            yearClock.restart(); // Prevent time accumulation causing a jump when closing UI
        }
        sf::Time currentYearDuration = turboMode ? turboYearDuration : yearDuration;
        if (((yearClock.getElapsedTime() >= currentYearDuration) && !paused && !paintStrokeActive && !uiModalActive) || simulationNeedsUpdate) {
            
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
                map.updateCountries(countries, currentYear, news, technologyManager);
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
            
            // 🏪 TRADE SYSTEM UPDATE
            auto tradeStart = std::chrono::high_resolution_clock::now();
            tradeManager.updateTrade(countries, currentYear, map, technologyManager, news);
	            auto tradeEnd = std::chrono::high_resolution_clock::now();
	            auto tradeTime = std::chrono::duration_cast<std::chrono::milliseconds>(tradeEnd - tradeStart);

		            // GPU economy (downsampled grid fields)
		            economy.onTerritoryChanged(map);
		            economy.tickYear(currentYear, map, countries, technologyManager);
		            economy.applyCountryMetrics(countries, tradeExportsForYear(currentYear));

	            map.processPoliticalEvents(countries, tradeManager, currentYear, news);
            
            auto simEnd = std::chrono::high_resolution_clock::now();
            auto simDuration = std::chrono::duration_cast<std::chrono::microseconds>(simEnd - simStart);
            
            // DIAGNOSTIC OUTPUT
            auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(simEnd - simStart);
            if (totalMs.count() > 100) {
                std::cout << " SLOW YEAR " << currentYear << " (" << totalMs.count() << "ms total):" << std::endl;
                std::cout << "  Map Update: " << mapTime.count() << "ms" << std::endl;
                std::cout << "  Tech/Culture: " << techTime.count() << "ms" << std::endl;
                std::cout << "  Great People: " << greatTime.count() << "ms" << std::endl;
                std::cout << "  Trade System: " << tradeTime.count() << "ms" << std::endl;
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
        bool renderedFrame = false;

        if (megaTimeJumpMode) {
            renderer.renderMegaTimeJumpScreen(megaTimeJumpInput, m_font);
            renderedFrame = true;
        } else if (countryAddEditorMode) {
            renderer.renderCountryAddEditor(editorInput, editorState, maxTechId, maxCultureId, m_font);
            renderedFrame = true;
        } else if (techEditorMode) {
            if (techEditorCountryIndex >= 0 && techEditorCountryIndex < static_cast<int>(countries.size())) {
                renderer.renderTechEditor(countries[static_cast<size_t>(techEditorCountryIndex)], technologyManager, techEditorInput, m_font);
                renderedFrame = true;
            } else {
                techEditorMode = false;
                renderedFrame = false;
            }
        } else {
            window.setView(enableZoom ? zoomedView : defaultView);

            renderer.render(countries, map, news, technologyManager, cultureManager, tradeManager, selectedCountry, showCountryInfo, viewMode);

            renderedFrame = true;
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
            // Normal mode - smooth 60 FPS, fall back to light sleep when idle
            if (renderedFrame && frameTime < targetFrameTime) {
                sf::sleep(sf::seconds(targetFrameTime - frameTime));
            } else if (!renderedFrame) {
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
