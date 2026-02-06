// main.cpp

#include <SFML/Graphics.hpp>
#include <vector>
#include <iostream>
#include <chrono>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
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
#include <random>
#include <fstream>
#include <optional>
#include <imgui.h>
#include <imgui-SFML.h>
#include <imgui_stdlib.h>
#include "country.h"
#include "renderer.h"
#include "map.h"
#include "news.h"
#include "technology.h"
#include "great_people.h"
#include "culture.h" // Include the new culture header
#include "trade.h" // Include the new trade system
#include "economy.h"
#include "simulation_context.h"

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

namespace {
std::optional<std::uint64_t> tryReadSeedFile(const char* filename) {
    std::ifstream in(filename);
    if (!in) {
        return std::nullopt;
    }
    std::uint64_t seed = 0;
    in >> seed;
    if (!in) {
        return std::nullopt;
    }
    return seed;
}

std::optional<std::uint64_t> tryParseSeedArg(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] ? std::string(argv[i]) : std::string();
        if (arg == "--seed" && i + 1 < argc) {
            try {
                return static_cast<std::uint64_t>(std::stoull(argv[i + 1]));
            } catch (...) {
                return std::nullopt;
            }
        }
        const std::string prefix = "--seed=";
        if (arg.rfind(prefix, 0) == 0) {
            try {
                return static_cast<std::uint64_t>(std::stoull(arg.substr(prefix.size())));
            } catch (...) {
                return std::nullopt;
            }
        }
    }
    return std::nullopt;
}

ImVec4 toImVec4(const sf::Color& c, float alphaScale = 1.0f) {
    return ImVec4(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, (c.a / 255.0f) * alphaScale);
}

std::string formatMoneyAbbrev(double v) {
    const double av = std::abs(v);
    char buf[64];
    if (av >= 1e12) std::snprintf(buf, sizeof(buf), "%.2fT", v / 1e12);
    else if (av >= 1e9) std::snprintf(buf, sizeof(buf), "%.2fB", v / 1e9);
    else if (av >= 1e6) std::snprintf(buf, sizeof(buf), "%.2fM", v / 1e6);
    else if (av >= 1e3) std::snprintf(buf, sizeof(buf), "%.2fK", v / 1e3);
    else std::snprintf(buf, sizeof(buf), "%.2f", v);
    return std::string(buf);
}

std::string trimCopy(std::string s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return std::string();
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::string toLowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

std::vector<int> parseIdsFromString(const std::string& s) {
    std::vector<int> ids;
    std::string token;
    for (char ch : s) {
        if (std::isdigit(static_cast<unsigned char>(ch))) {
            token.push_back(ch);
        } else {
            if (!token.empty()) {
                try { ids.push_back(std::stoi(token)); } catch (...) {}
                token.clear();
            }
        }
    }
    if (!token.empty()) {
        try { ids.push_back(std::stoi(token)); } catch (...) {}
    }
    return ids;
}
} // namespace

int main(int argc, char** argv) {
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

	    ImGui::SFML::Init(window);
	    struct ImGuiGuard {
	        ~ImGuiGuard() { ImGui::SFML::Shutdown(); }
	    } imguiGuard;

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

	    std::uint64_t worldSeed = 0;
	    if (auto s = tryParseSeedArg(argc, argv)) {
	        worldSeed = *s;
	    } else if (auto s = tryReadSeedFile("seed.txt")) {
	        worldSeed = *s;
	    } else {
	        std::random_device rd;
	        worldSeed = (static_cast<std::uint64_t>(rd()) << 32) ^ static_cast<std::uint64_t>(rd());
	        worldSeed ^= static_cast<std::uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
	    }
	    std::cout << "World seed: " << worldSeed << std::endl;
	    SimulationContext ctx(worldSeed);
	    
	    std::cout << "🚀 INITIALIZING MAP..." << std::endl;
	    auto mapStart = std::chrono::high_resolution_clock::now();
	    Map map(baseImage, resourceImage, gridCellSize, landColor, waterColor, regionSize, ctx);
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
	    GreatPeopleManager greatPeopleManager(ctx);
	    
		    // Initialize the Trade Manager
			    TradeManager tradeManager(ctx);

            // Phase 4: CPU-authoritative macro economy + directed trade.
            EconomyModelCPU macroEconomy(ctx);

		    // Initialize GPU economy (downsampled econ grid)
		    EconomyGPU economy;
	    EconomyGPU::Config econCfg;
			    econCfg.econCellSize = Map::kFieldCellSize;
			    econCfg.tradeIters = 12;
			    econCfg.updateReadbackEveryNYears = 1;
		    economy.init(map, maxCountries, econCfg);
		    if (!economy.isInitialized()) {
		        std::cout << "⚠️ EconomyGPU disabled (shaders unavailable/init failed). Using CPU fallback for wealth/GDP/exports." << std::endl;
		    }
		    economy.onTerritoryChanged(map);
		    economy.onStaticResourcesChanged(map);

			    Renderer renderer(window, map, waterColor);
			    News news;

			    bool guiVisible = true;
			    renderer.setGuiVisible(guiVisible);
			    bool guiShowTools = true;
			    bool guiShowInspector = true;
				    bool guiShowLeaderboard = false;
				    bool guiShowTemplateEditor = false;
				    bool guiShowTechEditor = false;
				    std::string guiTemplateTechIds;
				    std::string guiTemplateCultureIds;

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
	    std::string techEditorInput = "";
	    int techEditorCountryIndex = -1;
    
    // Country Add Editor variables
    struct CountryTemplate {
        std::vector<int> unlockedTechnologies;
        std::vector<int> unlockedCultures;
        long long initialPopulation = 5000; // Default starting population
        Country::Type countryType = Country::Type::Pacifist;
        Country::Ideology ideology = Country::Ideology::Tribal;
        bool useTemplate = false; // If false, use random generation
	    };
	    
	    CountryTemplate customCountryTemplate;
	    sf::Font m_font; // Font loading moved outside the loop
	    if (!m_font.loadFromFile("arial.ttf")) {
	        std::cerr << "Error: Could not load font file." << std::endl;
	        return -1;
	    }

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

		    sf::Clock imguiDeltaClock;

			    while (window.isOpen()) {
			        frameClock.restart(); // Start frame timing
			        const sf::Time imguiDt = imguiDeltaClock.restart();
		        
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

		            if (guiVisible && !megaTimeJumpMode) {
		                ImGui::SFML::ProcessEvent(window, event);
		            }

		            if (event.type == sf::Event::Closed) {
		                window.close();
		            }
		            else if (event.type == sf::Event::KeyPressed) {
	                if (event.key.code == sf::Keyboard::F1) {
	                    guiVisible = !guiVisible;
	                    renderer.setGuiVisible(guiVisible);
	                    continue;
	                }

	                if (megaTimeJumpMode) {
	                    if (event.key.code == sf::Keyboard::Escape) {
	                        megaTimeJumpMode = false;
	                        megaTimeJumpInput.clear();
	                    }
	                    continue;
	                }

	                const bool guiCapturesKeyboard = guiVisible && ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureKeyboard;
	                if (guiCapturesKeyboard) {
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
	                        paused = !paused;
	                        yearClock.restart(); // Prevent immediate year jump after a long pause
	                    }
	                }
		                else if (!megaTimeJumpMode && event.key.code == sf::Keyboard::Num0) {
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
	                else if (!megaTimeJumpMode && event.key.code == sf::Keyboard::Num1) {
	                    paintEraseMode = false;
	                }
	                else if (!megaTimeJumpMode && event.key.code == sf::Keyboard::Num2) {
	                    paintEraseMode = true;
	                }
	                else if (!megaTimeJumpMode && event.key.code == sf::Keyboard::R) {
	                    paintAllowOverwrite = !paintAllowOverwrite;
	                }
	                else if (!megaTimeJumpMode && event.key.code == sf::Keyboard::LBracket) {
	                    paintBrushRadius = std::max(1, paintBrushRadius - 1);
	                }
	                else if (!megaTimeJumpMode && event.key.code == sf::Keyboard::RBracket) {
	                    paintBrushRadius = std::min(64, paintBrushRadius + 1);
	                }
	                else if (!megaTimeJumpMode && event.key.code == sf::Keyboard::I) {
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
	                    ImGui::SFML::SetCurrentWindow(window);
	                    ImGui::SFML::UpdateFontTexture();
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
				                    // Phase 4: macro economy is authoritative for Wealth/GDP/Exports.
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
				                    guiShowLeaderboard = !guiShowLeaderboard;
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
	                    
	                    // 🛡️ CRASH FIX: Process in small chunks to avoid memory overflow
	                    const int totalYears = 100;
	                    const int chunkSize = 10; // Process 10 years at a time
                    
                    for (int chunk = 0; chunk < totalYears / chunkSize; ++chunk) {
                        
                        std::cout << "🔍 CHUNK " << (chunk + 1) << "/10: Processing years " 
                                  << currentYear << " to " << (currentYear + chunkSize) << std::endl;
                        
                        try {
                            // Perform simulation for this chunk using the same realism-first ordering
                            // as the live per-year tick (macro economy drives shortages and trade).
                            for (int step = 0; step < chunkSize; ++step) {
                                currentYear++;
                                if (currentYear == 0) currentYear = 1;

                                map.updateCountries(countries, currentYear, news, technologyManager);
                                macroEconomy.tickYear(currentYear, 1, map, countries, technologyManager, tradeManager, news);
                                map.tickDemographyAndCities(countries, currentYear, 1, news, &macroEconomy.getLastTradeIntensity());

                                if (currentYear % 5 == 0) {
                                    technologyManager.tickYear(countries, map, &macroEconomy.getLastTradeIntensity(), currentYear, 5);
                                    cultureManager.tickYear(countries, map, technologyManager, &macroEconomy.getLastTradeIntensity(), currentYear, 5, news);
                                }
                            }
                            
                            // 🚀 OPTIMIZED TECH/CULTURE: Batch processing and reduced frequency
                            std::cout << "   🧠 Tech/Culture updates for " << countries.size() << " countries..." << std::endl;
                            
                            std::cout << "     ✅ Tech/Culture handled during year stepping" << std::endl;
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
		                else if (event.key.code == sf::Keyboard::C) { // 🌦️ CLIMATE OVERLAY (Phase 6)
		                    if (event.key.shift) {
		                        renderer.cycleClimateOverlayMode();
		                    } else {
		                        renderer.toggleClimateOverlay();
		                    }
		                    renderingNeedsUpdate = true;
		                }
		                else if (event.key.code == sf::Keyboard::U) { // 🏙️ URBAN DEBUG OVERLAY
		                    if (event.key.shift) {
		                        renderer.cycleUrbanOverlayMode();
		                    } else {
		                        renderer.toggleUrbanOverlay();
		                    }
		                    renderingNeedsUpdate = true;
		                }
		                else if (event.key.code == sf::Keyboard::O) { // 🌍 OVERSEAS VIEW (Phase 7 debug)
		                    renderer.toggleOverseasOverlay();
		                    renderingNeedsUpdate = true;
		                }
			                else if (event.key.code == sf::Keyboard::E) { // 🧠 TECHNOLOGY EDITOR
			                    if (selectedCountry != nullptr) {
			                        guiShowTechEditor = true;
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
		                else if (event.key.code == sf::Keyboard::G && !megaTimeJumpMode) { // 🌍 TOGGLE GLOBE VIEW
		                    viewMode = (viewMode == ViewMode::Flat2D) ? ViewMode::Globe : ViewMode::Flat2D;
		                    if (viewMode == ViewMode::Globe) {
		                        renderer.resetGlobeView();
		                    }
		                    renderingNeedsUpdate = true;
		                }
		                else if (event.key.code == sf::Keyboard::M) { // 🏗️ COUNTRY TEMPLATE EDITOR
		                    guiShowTemplateEditor = !guiShowTemplateEditor;
		                }
            }
            else if (event.type == sf::Event::TextEntered) {
                // Only keep the legacy Mega Time Jump input flow (Z). Everything else moved to ImGui.
                if (!megaTimeJumpMode) {
                    continue;
                }

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
                                        [&](int, int) {
                                            // Macro economy runs inside `Map::megaTimeJump`; no GPU/trade chunk required here.
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
            else if (event.type == sf::Event::MouseWheelScrolled) {
                const bool guiCapturesMouse = guiVisible && ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse;
                if (guiCapturesMouse) {
                    continue;
                }

                if (paintMode &&
                    (sf::Keyboard::isKeyPressed(sf::Keyboard::LControl) || sf::Keyboard::isKeyPressed(sf::Keyboard::RControl))) {
                    int delta = (event.mouseWheelScroll.delta > 0) ? 1 : -1;
                    paintBrushRadius = std::max(1, std::min(64, paintBrushRadius + delta));
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
                const bool guiCapturesMouse = guiVisible && ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse;
                if (guiCapturesMouse) {
                    continue;
                }
                if (viewMode == ViewMode::Globe) {
                    sf::Vector2i mousePos = sf::Mouse::getPosition(window);
                    globeRightDragActive = true;
                    globeRightDragRotating = false;
                    globeRightClickPendingPick = paintMode && !megaTimeJumpMode;
                    globeRightPressPos = mousePos;
                    globeLastMousePos = mousePos;
                    continue;
                }

                if (paintMode && !megaTimeJumpMode) {
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
                const bool guiCapturesMouse = guiVisible && ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse;
                if (guiCapturesMouse) {
                    continue;
                }
                renderingNeedsUpdate = true; // Force render for interaction
		                if (forceInvasionMode && !megaTimeJumpMode && !paintMode) {
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
		                else if (paintMode && !megaTimeJumpMode) {
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

	                        std::mt19937_64& gen = ctx.worldRng;
	                        std::uniform_int_distribution<> colorDist(50, 255);
	                        std::uniform_int_distribution<> popDist(1000, 10000);
	                        std::uniform_real_distribution<> growthRateDist(0.0003, 0.001);
	                        std::uniform_int_distribution<> typeDist(0, 2);

	                        sf::Color countryColor(colorDist(gen), colorDist(gen), colorDist(gen));
	                        double growthRate = growthRateDist(gen);
	                        std::string countryName = generate_country_name(gen);
	                        while (isNameTaken(countries, countryName)) {
	                            countryName = generate_country_name(gen);
	                        }
	                        countryName += " Tribe";
                        
                        // Use custom template if available, otherwise use random generation
                        long long initialPopulation;
                        Country::Type countryType;
                        
                        if (customCountryTemplate.useTemplate) {
                            initialPopulation = customCountryTemplate.initialPopulation;
                            countryType = customCountryTemplate.countryType;
                        } else {
                            initialPopulation = popDist(gen);
                            countryType = static_cast<Country::Type>(typeDist(gen));
	                        }

	                        int newCountryIndex = countries.size();
	                        countries.emplace_back(newCountryIndex,
	                                               countryColor,
	                                               gridPos,
	                                               initialPopulation,
	                                               growthRate,
	                                               countryName,
	                                               countryType,
	                                               ctx.seedForCountry(newCountryIndex));

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
	                const bool guiCapturesMouse = guiVisible && ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse;
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
	                if (!megaTimeJumpMode && !guiCapturesMouse) {
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

	                if (paintStrokeActive && paintMode && !megaTimeJumpMode && !guiCapturesMouse) {
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

	                    // Phase 4: macro economy is authoritative; GPU economy is visualization-only.

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

		        // 🔥 NUCLEAR OPTIMIZATION: EVENT-DRIVEN SIMULATION ARCHITECTURE 🔥
		        
		        // STEP 1: Check if we need to advance simulation (ONCE PER YEAR, NOT 60 TIMES!)
		        bool uiModalActive = megaTimeJumpMode;
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
            
	            // Phase 6: weather anomalies (field-grid, cheap) before economy so yields affect output.
	            map.tickWeather(currentYear, 1);

	            // Phase 4: macro economy + directed, capacity-limited trade (CPU authoritative).
	            auto econStart = std::chrono::high_resolution_clock::now();
	            macroEconomy.tickYear(currentYear, 1, map, countries, technologyManager, tradeManager, news);
            // Demography/cities step runs after macro economy so food security shortages affect births/deaths.
            map.tickDemographyAndCities(countries, currentYear, 1, news, &macroEconomy.getLastTradeIntensity());
            auto econEnd = std::chrono::high_resolution_clock::now();
            auto econTime = std::chrono::duration_cast<std::chrono::milliseconds>(econEnd - econStart);

            auto techStart = std::chrono::high_resolution_clock::now();
            try {
                technologyManager.tickYear(countries, map, &macroEconomy.getLastTradeIntensity(), currentYear, 1);
                cultureManager.tickYear(countries, map, technologyManager, &macroEconomy.getLastTradeIntensity(), currentYear, 1, news);
            } catch (const std::exception& e) {
                std::cout << "🚨 TECH/CULTURE UPDATE CRASHED at year " << currentYear << ": " << e.what() << std::endl;
                throw;
            }
            auto techEnd = std::chrono::high_resolution_clock::now();
            auto techTime = std::chrono::duration_cast<std::chrono::milliseconds>(techEnd - techStart);

            auto greatStart = std::chrono::high_resolution_clock::now();
            greatPeopleManager.updateEffects(currentYear, countries, news);
            auto greatEnd = std::chrono::high_resolution_clock::now();
            auto greatTime = std::chrono::duration_cast<std::chrono::milliseconds>(greatEnd - greatStart);

            // GPU economy remains optional for visualization; macro economy sets country metrics.
            // economy.onTerritoryChanged(map);
            // economy.tickYear(currentYear, map, countries, technologyManager);

	            map.processPoliticalEvents(countries, tradeManager, currentYear, news, technologyManager, cultureManager);
            
            auto simEnd = std::chrono::high_resolution_clock::now();
            auto simDuration = std::chrono::duration_cast<std::chrono::microseconds>(simEnd - simStart);
            
            // DIAGNOSTIC OUTPUT
            auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(simEnd - simStart);
            if (totalMs.count() > 100) {
                std::cout << " SLOW YEAR " << currentYear << " (" << totalMs.count() << "ms total):" << std::endl;
                std::cout << "  Map Update: " << mapTime.count() << "ms" << std::endl;
                std::cout << "  Economy: " << econTime.count() << "ms" << std::endl;
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
		        bool renderedFrame = false;

		        if (megaTimeJumpMode) {
		            renderer.renderMegaTimeJumpScreen(megaTimeJumpInput, m_font);
		            renderedFrame = true;
		        } else {
		            if (guiVisible) {
		                ImGui::SFML::Update(window, imguiDt);

		                auto applyTechEditor = [&](Country& target, const std::string& command) {
		                    std::string raw = trimCopy(command);
		                    std::string lower = toLowerCopy(raw);

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
		                        std::vector<int> toAdd = parseIdsFromString(raw);
		                        const auto& current = technologyManager.getUnlockedTechnologies(target);
		                        nextTechs.assign(current.begin(), current.end());
		                        nextTechs.insert(nextTechs.end(), toAdd.begin(), toAdd.end());
		                        includePrereqs = true;
		                    }
		                    else if (lower.rfind("set", 0) == 0) {
		                        nextTechs = parseIdsFromString(raw);
		                        includePrereqs = true;
		                    }
		                    else if (lower.rfind("remove", 0) == 0) {
		                        includePrereqs = false;
		                        std::vector<int> toRemove = parseIdsFromString(raw);

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
		                    else if (!raw.empty()) {
		                        nextTechs = parseIdsFromString(raw);
		                        includePrereqs = true;
		                    }

		                    technologyManager.setUnlockedTechnologiesForEditor(target, nextTechs, includePrereqs);
		                    renderer.setNeedsUpdate(true);
		                    renderingNeedsUpdate = true;
		                };

		                // Top-left status
		                ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
		                ImGui::SetNextWindowBgAlpha(0.35f);
		                ImGuiWindowFlags statusFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
		                                              ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
		                                              ImGuiWindowFlags_NoNav;
		                if (ImGui::Begin("##Status", nullptr, statusFlags)) {
		                    long long totalPop = 0;
		                    for (const auto& c : countries) totalPop += c.getPopulation();
		                    ImGui::Text("Year: %d", currentYear);
		                    ImGui::SameLine();
		                    if (paused) { ImGui::TextUnformatted("| PAUSED"); ImGui::SameLine(); }
		                    if (turboMode) { ImGui::TextUnformatted("| TURBO"); ImGui::SameLine(); }
		                    ImGui::Text("| World Pop: %lld", totalPop);
		                    ImGui::Text("F1: Hide/Show GUI");
		                }
		                ImGui::End();

		                // Tools window
		                if (guiShowTools) {
		                    ImGui::SetNextWindowPos(ImVec2(10.0f, 90.0f), ImGuiCond_FirstUseEver);
		                    ImGui::SetNextWindowSize(ImVec2(360.0f, 600.0f), ImGuiCond_FirstUseEver);
		                    if (ImGui::Begin("Tools", &guiShowTools)) {
		                        ImGui::Checkbox("Paused (Space)", &paused);
		                        ImGui::Checkbox("Turbo Mode (T)", &turboMode);
		                        if (ImGui::Button("Trigger Plague (8)")) {
		                            map.triggerPlague(currentYear, news);
		                        }
		                        ImGui::Separator();

		                        bool showNews = news.isWindowVisible();
		                        if (ImGui::Checkbox("News (5)", &showNews)) {
		                            news.setWindowVisible(showNews);
		                        }
		                        ImGui::Checkbox("Wealth Leaderboard (L)", &guiShowLeaderboard);
		                        ImGui::Separator();

		                        bool warm = renderer.warmongerHighlightsEnabled();
		                        if (ImGui::Checkbox("Warmonger Highlights (4)", &warm)) {
		                            renderer.setWarmongerHighlights(warm);
		                            renderingNeedsUpdate = true;
		                        }
		                        bool war = renderer.warHighlightsEnabled();
		                        if (ImGui::Checkbox("War Highlights (6)", &war)) {
		                            renderer.setWarHighlights(war);
		                            renderingNeedsUpdate = true;
		                        }

		                        bool climate = renderer.climateOverlayEnabled();
		                        if (ImGui::Checkbox("Climate Overlay (C)", &climate)) {
		                            renderer.setClimateOverlay(climate);
		                            renderingNeedsUpdate = true;
		                        }
		                        if (climate) {
		                            int mode = renderer.climateOverlayMode();
		                            const char* modes[] = {"Zone", "Biome", "Temp Mean", "Prec Mean"};
		                            if (ImGui::Combo("Climate Mode", &mode, modes, 4)) {
		                                renderer.setClimateOverlayMode(mode);
		                                renderingNeedsUpdate = true;
		                            }
		                        }

		                        bool urban = renderer.urbanOverlayEnabled();
		                        if (ImGui::Checkbox("Urban Overlay (U)", &urban)) {
		                            renderer.setUrbanOverlay(urban);
		                            renderingNeedsUpdate = true;
		                        }
		                        if (urban) {
		                            int mode = renderer.urbanOverlayMode();
		                            const char* modes[] = {"Crowding", "Specialization", "Urban Share", "Urban Pop"};
		                            if (ImGui::Combo("Urban Mode", &mode, modes, 4)) {
		                                renderer.setUrbanOverlayMode(mode);
		                                renderingNeedsUpdate = true;
		                            }
		                        }

		                        bool overseas = renderer.overseasOverlayEnabled();
		                        if (ImGui::Checkbox("Overseas Overlay (O)", &overseas)) {
		                            renderer.setOverseasOverlay(overseas);
		                            renderingNeedsUpdate = true;
		                        }

		                        ImGui::Separator();
		                        ImGui::Checkbox("Add Country Mode (9)", &countryAddMode);

		                        if (ImGui::Checkbox("Paint Mode (0)", &paintMode)) {
		                            if (paintMode) {
		                                countryAddMode = false;
		                                forceInvasionMode = false;
		                                forcedInvasionAttackerIndex = -1;
		                                if (selectedPaintCountryIndex < 0 && selectedCountry != nullptr) {
		                                    selectedPaintCountryIndex = selectedCountry->getCountryIndex();
		                                }
		                            } else {
		                                paintStrokeActive = false;
		                            }
		                        }

		                        if (paintMode) {
		                            int paintOp = paintEraseMode ? 1 : 0;
		                            ImGui::RadioButton("Add", &paintOp, 0);
		                            ImGui::SameLine();
		                            ImGui::RadioButton("Erase", &paintOp, 1);
		                            paintEraseMode = (paintOp == 1);
		                            ImGui::SliderInt("Brush Radius", &paintBrushRadius, 1, 64);
		                            ImGui::Checkbox("Replace (R)", &paintAllowOverwrite);
		                            if (selectedPaintCountryIndex >= 0 && selectedPaintCountryIndex < static_cast<int>(countries.size())) {
		                                ImGui::Text("Paint Country: %s", countries[static_cast<size_t>(selectedPaintCountryIndex)].getName().c_str());
		                            } else {
		                                ImGui::TextUnformatted("Paint Country: <none> (right click to pick)");
		                            }
		                            if (selectedCountry != nullptr) {
		                                if (ImGui::Button("Use Selected Country")) {
		                                    selectedPaintCountryIndex = selectedCountry->getCountryIndex();
		                                }
		                            }
		                        }

		                        ImGui::Separator();
		                        ImGui::Checkbox("Forced Invasion (I)", &forceInvasionMode);
		                        if (forceInvasionMode) {
		                            if (forcedInvasionAttackerIndex >= 0 && forcedInvasionAttackerIndex < static_cast<int>(countries.size())) {
		                                ImGui::Text("Attacker: %s", countries[static_cast<size_t>(forcedInvasionAttackerIndex)].getName().c_str());
		                                if (ImGui::Button("Clear Attacker")) {
		                                    forcedInvasionAttackerIndex = -1;
		                                }
		                            } else {
		                                ImGui::TextUnformatted("Attacker: <click a country>");
		                            }
		                        }

		                        ImGui::Separator();
		                        if (ImGui::Checkbox("Country Template (M)", &guiShowTemplateEditor)) {
		                        }
		                        if (selectedCountry != nullptr) {
		                            ImGui::Checkbox("Tech Editor (E)", &guiShowTechEditor);
		                        }

		                        ImGui::Separator();
		                        int vm = (viewMode == ViewMode::Globe) ? 1 : 0;
		                        const char* vms[] = {"2D", "Globe"};
		                        if (ImGui::Combo("View Mode", &vm, vms, 2)) {
		                            ViewMode next = (vm == 1) ? ViewMode::Globe : ViewMode::Flat2D;
		                            if (next != viewMode) {
		                                viewMode = next;
		                                if (viewMode == ViewMode::Globe) renderer.resetGlobeView();
		                                renderingNeedsUpdate = true;
		                            }
		                        }
		                    }
		                    ImGui::End();
		                }

		                // Inspector
		                if (guiShowInspector) {
		                    ImGui::SetNextWindowPos(ImVec2(static_cast<float>(window.getSize().x) - 420.0f, 10.0f), ImGuiCond_FirstUseEver);
		                    ImGui::SetNextWindowSize(ImVec2(410.0f, 720.0f), ImGuiCond_FirstUseEver);
		                    if (ImGui::Begin("Inspector", &guiShowInspector)) {
		                        const Country* c = selectedCountry;
		                        if (c == nullptr && hoveredCountryIndex >= 0 && hoveredCountryIndex < static_cast<int>(countries.size())) {
		                            c = &countries[static_cast<size_t>(hoveredCountryIndex)];
		                        }

		                        if (c == nullptr) {
		                            ImGui::TextUnformatted("Click a country to inspect.");
		                        } else {
		                            ImGui::ColorButton("##c", toImVec4(c->getColor()), ImGuiColorEditFlags_NoTooltip, ImVec2(14, 14));
		                            ImGui::SameLine();
		                            ImGui::TextUnformatted(c->getName().c_str());
		                            ImGui::Separator();

		                            ImGui::Text("Population: %lld", c->getPopulation());
		                            ImGui::Text("Territory: %d pixels", static_cast<int>(c->getBoundaryPixels().size()));
		                            ImGui::Text("Cities: %d", static_cast<int>(c->getCities().size()));
		                            ImGui::Text("Gold: %d", static_cast<int>(c->getGold()));
		                            ImGui::Text("Wealth: %s", formatMoneyAbbrev(c->getWealth()).c_str());
		                            ImGui::Text("GDP: %s", formatMoneyAbbrev(c->getGDP()).c_str());
		                            ImGui::Text("Exports: %s", formatMoneyAbbrev(c->getExports()).c_str());
		                            ImGui::Text("Ideology: %s", c->getIdeologyString().c_str());

		                            if (ImGui::Button("Open Tech Editor")) {
		                                guiShowTechEditor = true;
		                                techEditorCountryIndex = c->getCountryIndex();
		                            }

		                            if (ImGui::CollapsingHeader("Technologies", ImGuiTreeNodeFlags_DefaultOpen)) {
		                                const auto& unlocked = technologyManager.getUnlockedTechnologies(*c);
		                                const auto& techs = technologyManager.getTechnologies();
		                                ImGui::Text("Unlocked: %d", static_cast<int>(unlocked.size()));
		                                ImGui::BeginChild("##techs", ImVec2(0, 200), true);
		                                ImGuiListClipper clipper;
		                                clipper.Begin(static_cast<int>(unlocked.size()));
		                                while (clipper.Step()) {
		                                    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
		                                        auto it = techs.find(unlocked[static_cast<size_t>(i)]);
		                                        if (it != techs.end()) {
		                                            ImGui::BulletText("%s", it->second.name.c_str());
		                                        }
		                                    }
		                                }
		                                ImGui::EndChild();
		                            }

		                            if (ImGui::CollapsingHeader("Institutions", ImGuiTreeNodeFlags_DefaultOpen)) {
		                                const auto& unlocked = cultureManager.getUnlockedCivics(*c);
		                                const auto& civics = cultureManager.getCivics();
		                                ImGui::Text("Unlocked: %d", static_cast<int>(unlocked.size()));
		                                ImGui::BeginChild("##civics", ImVec2(0, 200), true);
		                                ImGuiListClipper clipper;
		                                clipper.Begin(static_cast<int>(unlocked.size()));
		                                while (clipper.Step()) {
		                                    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
		                                        auto it = civics.find(unlocked[static_cast<size_t>(i)]);
		                                        if (it != civics.end()) {
		                                            ImGui::BulletText("%s", it->second.name.c_str());
		                                        }
		                                    }
		                                }
		                                ImGui::EndChild();
		                            }
		                        }
		                    }
		                    ImGui::End();
		                }

		                // News window
		                if (news.isWindowVisible()) {
		                    bool open = true;
		                    ImGui::SetNextWindowSize(ImVec2(420.0f, 260.0f), ImGuiCond_FirstUseEver);
		                    if (ImGui::Begin("News", &open)) {
		                        if (ImGui::Button("Clear")) {
		                            news.clearEvents();
		                        }
		                        ImGui::SameLine();
		                        if (ImGui::Button("Close")) {
		                            open = false;
		                        }

		                        ImGui::Separator();
		                        ImGui::BeginChild("##news", ImVec2(0, 0), true);
		                        for (const auto& e : news.getEvents()) {
		                            ImGui::TextUnformatted(e.c_str());
		                        }
		                        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
		                            ImGui::SetScrollHereY(1.0f);
		                        }
		                        ImGui::EndChild();
		                    }
		                    ImGui::End();

		                    if (!open) {
		                        news.setWindowVisible(false);
		                    }
		                }

		                // Wealth leaderboard
		                if (guiShowLeaderboard) {
		                    ImGui::SetNextWindowSize(ImVec2(820.0f, 700.0f), ImGuiCond_FirstUseEver);
		                    if (ImGui::Begin("Wealth Leaderboard", &guiShowLeaderboard)) {
		                        struct Row { int idx; double wealth; double gdp; double exports; long long pop; };
		                        std::vector<Row> rows;
		                        rows.reserve(countries.size());
		                        for (int i = 0; i < static_cast<int>(countries.size()); ++i) {
		                            const Country& c = countries[static_cast<size_t>(i)];
		                            if (c.getPopulation() <= 0) continue;
		                            rows.push_back({i, c.getWealth(), c.getGDP(), c.getExports(), c.getPopulation()});
		                        }

		                        if (ImGui::BeginTable("##wealth", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
		                                                       ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable,
		                                              ImVec2(0, 0))) {
		                            ImGui::TableSetupScrollFreeze(0, 1);
		                            ImGui::TableSetupColumn("Rank", ImGuiTableColumnFlags_NoSort, 60.0f);
		                            ImGui::TableSetupColumn(" ", ImGuiTableColumnFlags_NoSort, 24.0f);
		                            ImGui::TableSetupColumn("Country");
		                            ImGui::TableSetupColumn("Wealth");
		                            ImGui::TableSetupColumn("GDP");
		                            ImGui::TableSetupColumn("Exports");
		                            ImGui::TableSetupColumn("Pop");
		                            ImGui::TableHeadersRow();

		                            if (ImGuiTableSortSpecs* sort = ImGui::TableGetSortSpecs()) {
		                                if (sort->SpecsDirty && sort->SpecsCount > 0) {
		                                    const ImGuiTableColumnSortSpecs& s = sort->Specs[0];
		                                    auto cmp = [&](const Row& a, const Row& b) {
		                                        auto dir = (s.SortDirection == ImGuiSortDirection_Ascending) ? 1 : -1;
		                                        auto by = [&](auto va, auto vb) {
		                                            if (va < vb) return -1 * dir;
		                                            if (va > vb) return  1 * dir;
		                                            return 0;
		                                        };
		                                        int r = 0;
		                                        switch (s.ColumnIndex) {
		                                            case 3: r = by(a.wealth, b.wealth); break;
		                                            case 4: r = by(a.gdp, b.gdp); break;
		                                            case 5: r = by(a.exports, b.exports); break;
		                                            case 6: r = by(a.pop, b.pop); break;
		                                            default: r = 0; break;
		                                        }
		                                        if (r != 0) return r < 0;
		                                        return a.idx < b.idx;
		                                    };
		                                    std::sort(rows.begin(), rows.end(), cmp);
		                                    sort->SpecsDirty = false;
		                                }
		                            } else {
		                                std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
		                                    if (a.wealth != b.wealth) return a.wealth > b.wealth;
		                                    return a.idx < b.idx;
		                                });
		                            }

		                            ImGuiListClipper clipper;
		                            clipper.Begin(static_cast<int>(rows.size()));
		                            while (clipper.Step()) {
		                                for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
		                                    const Country& c = countries[static_cast<size_t>(rows[r].idx)];
		                                    ImGui::TableNextRow();

		                                    ImGui::TableSetColumnIndex(0);
		                                    ImGui::Text("%d", r + 1);

		                                    ImGui::TableSetColumnIndex(1);
		                                    ImGui::ColorButton(("##col" + std::to_string(r)).c_str(), toImVec4(c.getColor()), ImGuiColorEditFlags_NoTooltip, ImVec2(12, 12));

		                                    ImGui::TableSetColumnIndex(2);
		                                    if (ImGui::Selectable(c.getName().c_str(), selectedCountry && selectedCountry->getCountryIndex() == c.getCountryIndex(),
		                                                          ImGuiSelectableFlags_SpanAllColumns)) {
		                                        selectedCountry = &countries[static_cast<size_t>(rows[r].idx)];
		                                        showCountryInfo = true;
		                                    }

		                                    ImGui::TableSetColumnIndex(3);
		                                    ImGui::TextUnformatted(formatMoneyAbbrev(rows[r].wealth).c_str());
		                                    ImGui::TableSetColumnIndex(4);
		                                    ImGui::TextUnformatted(formatMoneyAbbrev(rows[r].gdp).c_str());
		                                    ImGui::TableSetColumnIndex(5);
		                                    ImGui::TextUnformatted(formatMoneyAbbrev(rows[r].exports).c_str());
		                                    ImGui::TableSetColumnIndex(6);
		                                    ImGui::Text("%lld", rows[r].pop);
		                                }
		                            }

		                            ImGui::EndTable();
		                        }
		                    }
		                    ImGui::End();
		                }

		                // Template editor
		                if (guiShowTemplateEditor) {
		                    ImGui::SetNextWindowSize(ImVec2(520.0f, 520.0f), ImGuiCond_FirstUseEver);
		                    if (ImGui::Begin("Country Template", &guiShowTemplateEditor)) {
		                        ImGui::Checkbox("Use Template", &customCountryTemplate.useTemplate);
		                        ImGui::InputScalar("Starting Population", ImGuiDataType_S64, &customCountryTemplate.initialPopulation);

		                        int type = static_cast<int>(customCountryTemplate.countryType);
		                        const char* types[] = {"Warmonger", "Pacifist", "Trader"};
		                        if (ImGui::Combo("Country Type", &type, types, 3)) {
		                            customCountryTemplate.countryType = static_cast<Country::Type>(type);
		                        }

		                        ImGui::Separator();
		                        ImGui::TextUnformatted("Technologies (IDs or 'all'):");
		                        ImGui::InputText("##tmplTech", &guiTemplateTechIds);
		                        if (ImGui::Button("Apply Tech List")) {
		                            const int maxTechId = static_cast<int>(technologyManager.getTechnologies().size());
		                            customCountryTemplate.unlockedTechnologies.clear();
		                            std::string raw = trimCopy(guiTemplateTechIds);
		                            std::string lower = toLowerCopy(raw);
		                            if (lower == "all") {
		                                for (int i = 1; i <= maxTechId; ++i) customCountryTemplate.unlockedTechnologies.push_back(i);
		                            } else {
		                                for (int id : parseIdsFromString(raw)) {
		                                    if (id >= 1 && id <= maxTechId) customCountryTemplate.unlockedTechnologies.push_back(id);
		                                }
		                            }
		                        }
		                        ImGui::SameLine();
		                        ImGui::Text("Selected: %d", static_cast<int>(customCountryTemplate.unlockedTechnologies.size()));

		                        ImGui::TextUnformatted("Cultures (IDs, 1-10 or 'all'):");
		                        ImGui::InputText("##tmplCult", &guiTemplateCultureIds);
		                        if (ImGui::Button("Apply Culture List")) {
		                            const int maxCultureId = 10;
		                            customCountryTemplate.unlockedCultures.clear();
		                            std::string raw = trimCopy(guiTemplateCultureIds);
		                            std::string lower = toLowerCopy(raw);
		                            if (lower == "all") {
		                                for (int i = 1; i <= maxCultureId; ++i) customCountryTemplate.unlockedCultures.push_back(i);
		                            } else {
		                                for (int id : parseIdsFromString(raw)) {
		                                    if (id >= 1 && id <= maxCultureId) customCountryTemplate.unlockedCultures.push_back(id);
		                                }
		                            }
		                        }
		                        ImGui::SameLine();
		                        ImGui::Text("Selected: %d", static_cast<int>(customCountryTemplate.unlockedCultures.size()));

		                        ImGui::Separator();
		                        if (ImGui::Button("Reset Template")) {
		                            customCountryTemplate.useTemplate = false;
		                            customCountryTemplate.unlockedTechnologies.clear();
		                            customCountryTemplate.unlockedCultures.clear();
		                            customCountryTemplate.initialPopulation = 5000;
		                            customCountryTemplate.countryType = Country::Type::Pacifist;
		                            guiTemplateTechIds.clear();
		                            guiTemplateCultureIds.clear();
		                        }
		                    }
		                    ImGui::End();
		                }

		                // Tech editor window
		                if (guiShowTechEditor) {
		                    if (techEditorCountryIndex < 0 && selectedCountry != nullptr) {
		                        techEditorCountryIndex = selectedCountry->getCountryIndex();
		                    }
		                    if (techEditorCountryIndex >= 0 && techEditorCountryIndex < static_cast<int>(countries.size())) {
		                        Country& target = countries[static_cast<size_t>(techEditorCountryIndex)];
		                        ImGui::SetNextWindowSize(ImVec2(600.0f, 240.0f), ImGuiCond_FirstUseEver);
		                        if (ImGui::Begin("Technology Editor", &guiShowTechEditor)) {
		                            ImGui::Text("Country: %s", target.getName().c_str());
		                            ImGui::TextUnformatted("Commands: all | clear | add 1,2,3 | remove 5,7 | set 10,11,14");
		                            ImGui::InputText("##techcmd", &techEditorInput);
		                            if (ImGui::Button("Apply")) {
		                                applyTechEditor(target, techEditorInput);
		                            }
		                            ImGui::SameLine();
		                            if (ImGui::Button("All")) {
		                                techEditorInput = "all";
		                                applyTechEditor(target, techEditorInput);
		                            }
		                            ImGui::SameLine();
		                            if (ImGui::Button("Clear")) {
		                                techEditorInput = "clear";
		                                applyTechEditor(target, techEditorInput);
		                            }
		                        }
		                        ImGui::End();
		                    } else {
		                        guiShowTechEditor = false;
		                    }
		                }

			            }

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
