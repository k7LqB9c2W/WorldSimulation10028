// map.h
#pragma once

#include <SFML/Graphics.hpp>
#include <vector>
#include <mutex>
#include <unordered_set>
#include <unordered_map>
#include <functional>
#include <cstdint>
#include <atomic>
#include <string>
#include "country.h"
#include "resource.h"
#include "news.h"
#include "simulation_context.h"

class Country;
class TradeManager;

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

std::string generate_country_name(std::mt19937_64& rng);
bool isNameTaken(const std::vector<Country>& countries, const std::string& name);

class Map {
public:
    static constexpr int kFieldCellSize = 6; // Must match EconomyGPU econCellSize for new field systems.

    Map(const sf::Image& baseImage,
        const sf::Image& resourceImage,
        const sf::Image& coalImage,
        const sf::Image& copperImage,
        const sf::Image& tinImage,
        const sf::Image& riverlandImage,
        int gridCellSize,
        const sf::Color& landColor,
        const sf::Color& waterColor,
        int regionSize,
        SimulationContext& ctx);
    void initializeCountries(std::vector<Country>& countries, int numCountries);
    void updateCountries(std::vector<Country>& countries, int currentYear, News& news, class TechnologyManager& technologyManager);
    // Phase 4 integration: run demography/migration + city updates as a separate step so
    // shortages computed by the macro economy can affect births/deaths in the same year.
    void tickDemographyAndCities(std::vector<Country>& countries,
                                 int currentYear,
                                 int dtYears,
                                 News& news,
                                 const std::vector<float>* tradeIntensityMatrix = nullptr);
    // Optional: allow Map ownership writes to keep Country territory containers in sync.
    // Recommended to call once after countries are created/reserved, and Map will also
    // re-attach automatically in methods that already have a `countries` reference.
    void attachCountriesForOwnershipSync(std::vector<Country>* countries);
    const std::vector<std::vector<bool>>& getIsLandGrid() const;
    sf::Vector2i pixelToGrid(const sf::Vector2f& pixel) const;
    int getGridCellSize() const;
    std::mutex& getGridMutex();
    const sf::Image& getBaseImage() const;
    int getRegionSize() const;
    void startPlague(int year, News& news);
    void endPlague(News& news);
    bool areNeighbors(const Country& country1, const Country& country2) const;
    bool areCountryIndicesNeighbors(int countryIndex1, int countryIndex2) const;
    bool isPlagueActive() const;
    void initializePlagueCluster(const std::vector<Country>& countries);
    bool isCountryAffectedByPlague(int countryIndex) const;
    int getPlagueStartYear() const;
    void updatePlagueDeaths(long long deaths);
    bool loadSpawnZones(const std::string& filename);
    sf::Vector2i getRandomCellInPreferredZones(std::mt19937_64& rng);
    void setCountryGridValue(int x, int y, int value);
    // Territory ownership writes must go through these methods so incremental adjacency stays correct.
    // - `setCountryOwner` locks internally.
    // - `setCountryOwnerAssumingLocked` expects the caller to already hold `getGridMutex()`.
    bool setCountryOwner(int x, int y, int newOwner);
    bool setCountryOwnerAssumingLocked(int x, int y, int newOwner);
    void insertDirtyRegion(int regionIndex);
    void triggerPlague(int year, News& news); // Add this line

    // Territory painting (editor tooling)
    bool paintCells(int countryIndex,
                    const sf::Vector2i& center,
                    int radius,
                    bool erase,
                    bool allowOverwrite,
                    std::vector<int>& affectedCountries);
    void rebuildCountryBoundary(Country& country);
    void rebuildBoundariesForCountries(std::vector<Country>& countries, const std::vector<int>& countryIndices);
    void rebuildAdjacency(const std::vector<Country>& countries);
    const std::vector<int>& getAdjacentCountryIndicesPublic(int countryIndex) const;
    int getBorderContactCount(int a, int b) const;
    
    // Road building support
    bool isValidRoadPixel(int x, int y) const;

	// Political events
	void processPoliticalEvents(std::vector<Country>& countries,
	                           TradeManager& tradeManager,
	                           int currentYear,
	                           News& news,
	                           class TechnologyManager& techManager,
	                           class CultureManager& cultureManager,
                               int dtYears = 1);
    
    // Fast Forward Mode - simulate multiple years quickly
    void fastForwardSimulation(std::vector<Country>& countries, int& currentYear, int targetYears, News& news, class TechnologyManager& technologyManager);
    
	    // MEGA TIME JUMP - simulate thousands of years with historical tracking
	    bool megaTimeJump(std::vector<Country>& countries, int& currentYear, int targetYear, News& news,
	                      class TechnologyManager& techManager, class CultureManager& cultureManager,
	                      class EconomyModelCPU& macroEconomy,
	                      TradeManager& tradeManager,
	                      class GreatPeopleManager& greatPeopleManager,
	                      std::function<void(int, int, float)> progressCallback = nullptr,
	                      std::function<void(int, int)> chunkCompletedCallback = nullptr,
	                      const std::atomic<bool>* cancelRequested = nullptr,
                          bool enablePopulationDebugLog = false,
                          const std::string& populationDebugLogPath = std::string());

    // Keep these for read-only access (const versions)
    const std::vector<std::vector<int>>& getCountryGrid() const;
    const std::unordered_set<int>& getDirtyRegions() const;

    // Keep these for modification access (non-const versions)
    // WARNING: Writing directly to the returned grid will bypass incremental adjacency tracking.
    // Prefer `setCountryOwner*()` for any ownership change.
    std::vector<std::vector<int>>& getCountryGrid();
    std::unordered_set<int>& getDirtyRegions();

    const std::vector<std::vector<std::unordered_map<Resource::Type, double>>>& getResourceGrid() const;
    const SimulationConfig& getConfig() const;

    // Fast cached accessors (used by fast-forward / mega time jump).
    double getCellFood(int x, int y) const;
    int getCellOwner(int x, int y) const;
    double getCountryFoodSum(int countryIndex) const;
    double getCountryFoodPotential(int countryIndex) const { return getCountryFoodSum(countryIndex); }
    double getCountryForagingPotential(int countryIndex) const;
    double getCountryFarmingPotential(int countryIndex) const;
    double getCountryNonFoodPotential(int countryIndex) const;
    double getCountryOrePotential(int countryIndex) const;
    double getCountryEnergyPotential(int countryIndex) const;
    double getCountryConstructionPotential(int countryIndex) const;
    int getCountryLandCellCount(int countryIndex) const;

    // Phase 2/3 field systems (econ-grid resolution)
    int getFieldWidth() const { return m_fieldW; }
    int getFieldHeight() const { return m_fieldH; }
    const std::vector<int>& getFieldOwnerId() const { return m_fieldOwnerId; }     // size = fieldW*fieldH, -1 for none
    const std::vector<float>& getFieldControl() const { return m_fieldControl; }  // size = fieldW*fieldH, 0..1
    const std::vector<float>& getFieldFoodPotential() const { return m_fieldFoodPotential; } // size = fieldW*fieldH
    bool isPopulationGridActive() const { return !m_fieldPopulation.empty(); }
    const std::vector<float>& getFieldPopulation() const { return m_fieldPopulation; } // size = fieldW*fieldH

    // Rule-based urbanization debug fields (field grid resolution).
    const std::vector<float>& getFieldCrowding() const { return m_fieldCrowding; }             // pop / K
    const std::vector<float>& getFieldSpecialization() const { return m_fieldSpecialization; } // 0..1
    const std::vector<float>& getFieldUrbanShare() const { return m_fieldUrbanShare; }         // 0..1
    const std::vector<float>& getFieldUrbanPop() const { return m_fieldUrbanPop; }             // people

	// Phase 6: climate/weather fields (field grid resolution).
	const std::vector<uint8_t>& getFieldClimateZone() const { return m_fieldClimateZone; } // discrete bands for debug
	const std::vector<uint8_t>& getFieldBiome() const { return m_fieldBiome; }             // 0..N (land only)
	const std::vector<float>& getFieldTempMean() const { return m_fieldTempMean; }
	const std::vector<float>& getFieldPrecipMean() const { return m_fieldPrecipMean; }
	const std::vector<float>& getFieldFoodYieldMult() const { return m_fieldFoodYieldMult; }
	float getCountryClimateFoodMultiplier(int countryIndex) const; // aggregated (weighted)
	const std::vector<uint8_t>& getFieldOverseasMask() const { return m_fieldOverseasMask; } // Phase 7 debug overlay (0/1)

    void tickWeather(int year, int dtYears); // Phase 6: dynamic anomalies + yield multiplier update
    void prepareCountryClimateCaches(int countryCount) const; // call once per tick for O(field) aggregation

	private:
    SimulationContext* m_ctx = nullptr;
    std::vector<Country>* m_ownershipSyncCountries = nullptr;
    std::vector<std::vector<int>> m_countryGrid;
    std::vector<std::vector<bool>> m_isLandGrid;
    int m_gridCellSize;
    int m_regionSize;
    sf::Color m_landColor;
    sf::Color m_waterColor;
    std::mutex m_gridMutex;
    sf::Image m_baseImage;
    sf::Image m_resourceImage;
    sf::Image m_coalImage;
    sf::Image m_copperImage;
    sf::Image m_tinImage;
    sf::Image m_riverlandImage;
    std::unordered_set<int> m_dirtyRegions;
    std::vector<std::vector<std::unordered_map<Resource::Type, double>>> m_resourceGrid;
    std::unordered_map<sf::Color, Resource::Type> m_resourceColors;
    void initializeResourceGrid();

    // Cached FOOD values per cell (y * width + x). 0 for non-land.
    std::vector<double> m_cellFood;
    std::vector<double> m_cellForaging;
    std::vector<double> m_cellFarming;
    void rebuildCellFoodCache();
    // Typed non-food potential caches per cell (y * width + x). 0 for non-land.
    std::vector<double> m_cellOre;
    std::vector<double> m_cellEnergy;
    std::vector<double> m_cellConstruction;
    std::vector<double> m_cellNonFood;
    void rebuildCellOreCache();
    void rebuildCellEnergyCache();
    void rebuildCellConstructionCache();

    // Incremental per-country aggregates (kept consistent via setCountryOwnerAssumingLockedImpl).
    std::vector<int> m_countryLandCellCount;
    std::vector<double> m_countryFoodPotential;
    std::vector<double> m_countryForagingPotential;
    std::vector<double> m_countryFarmingPotential;
    std::vector<double> m_countryOrePotential;
    std::vector<double> m_countryEnergyPotential;
    std::vector<double> m_countryConstructionPotential;
    std::vector<double> m_countryNonFoodPotential;
    void ensureCountryAggregateCapacityForIndex(int idx);
    void rebuildCountryPotentials(int countryCount);

    bool m_plagueActive;
    int m_plagueStartYear;
    long long m_plagueDeathToll;
    int m_plagueInterval;
    int m_nextPlagueYear;
    std::unordered_set<int> m_plagueAffectedCountries; // Track which countries are affected by current plague
    void updatePlagueSpread(const std::vector<Country>& countries);

    // Country adjacency (indexed by Country::getCountryIndex()).
    int m_countryAdjacencySize = 0;
    std::vector<std::vector<int>> m_countryAdjacency;
    // Incremental adjacency tracking (border-contact counts).
    //
    // Each time a grid cell changes owner, we update the number of border contacts between the
    // old/new owner and each of the 8 neighboring cells. This maintains:
    // - `m_countryBorderContactCounts[a][b]`: number of adjacent cell-pairs between country a and b.
    // - `m_countryAdjacency` / `m_countryAdjacencyBits`: derived neighbor sets for fast iteration / O(1) membership.
    //
    // IMPORTANT:
    // - Any write to `m_countryGrid` must go through `setCountryOwner*()` or adjacency will desync.
    std::vector<std::vector<int>> m_countryBorderContactCounts;
    std::vector<std::vector<std::uint64_t>> m_countryAdjacencyBits;
    void rebuildCountryAdjacency(const std::vector<Country>& countries);
    const std::vector<int>& getAdjacentCountryIndices(int countryIndex) const;
    void ensureAdjacencyStorageForIndex(int countryIndex);
    void setAdjacencyEdge(int a, int b, bool isNeighbor);
    void addBorderContact(int a, int b);
    void removeBorderContact(int a, int b);
    bool setCountryOwnerAssumingLockedImpl(int x, int y, int newOwner);
    void markCountryExtinct(std::vector<Country>& countries, int countryIndex, int currentYear, News& news);

    sf::Image m_spawnZoneImage;
    sf::Color m_spawnZoneColor = sf::Color(255, 132, 255);

    // Phase 2/3: coarse fields at econ-grid resolution (kFieldCellSize).
    int m_fieldW = 0;
    int m_fieldH = 0;
    std::vector<int> m_fieldOwnerId;          // fieldW*fieldH, -1 for none
    std::vector<float> m_fieldControl;        // fieldW*fieldH, 0..1
    std::vector<float> m_fieldMoveCost;       // fieldW*fieldH, travel-time friction
    std::vector<float> m_fieldCorridorWeight; // fieldW*fieldH, preferred migration corridor weight
    std::vector<float> m_fieldFoodPotential;  // fieldW*fieldH, summed food potential per field cell
    std::vector<float> m_fieldPopulation;     // fieldW*fieldH, people stock per cell
    std::vector<float> m_fieldAttractiveness; // fieldW*fieldH, computed each migration tick
    std::vector<float> m_fieldPopDelta;       // fieldW*fieldH, scratch buffer for migration
    std::vector<float> m_fieldCrowding;       // fieldW*fieldH, pop / K (debug)
    std::vector<float> m_fieldSpecialization; // fieldW*fieldH, 0..1 (debug)
    std::vector<float> m_fieldUrbanShare;     // fieldW*fieldH, 0..1 (debug)
    std::vector<float> m_fieldUrbanPop;       // fieldW*fieldH, people (debug)
    int m_lastPopulationUpdateYear = -9999999;
    bool m_controlCacheDirty = true;
    struct CountryControlCache {
        int lastComputedYear = -9999999;
        std::vector<int> fieldIndices;
        std::vector<float> travelTimes;
        size_t roadCount = 0;
        size_t portCount = 0;
    };
    std::vector<CountryControlCache> m_countryControlCache;
    struct LocalAutonomyState {
        double pressure = 0.0; // 0..1
        int overYears = 0;
    };
    std::unordered_map<std::uint64_t, LocalAutonomyState> m_localAutonomyByCenter;
    int m_lastLocalAutonomyUpdateYear = -9999999;
    void ensureFieldGrids();
    void rebuildFieldFoodPotential();
	void ensureClimateGrids();
	void initializeClimateBaseline();
	void buildCoastalLandCandidates(); // Phase 7: candidate sites (coastal land field indices)
	void rebuildFieldLandMask();
	void rebuildFieldOwnerIdAssumingLocked(int countryCount);
	void updateControlGrid(std::vector<Country>& countries, int currentYear, int dtYears);
    void rebuildFieldMoveCost(const std::vector<Country>& countries);

    void initializePopulationGridFromCountries(const std::vector<Country>& countries);
    void tickPopulationGrid(const std::vector<Country>& countries,
                            int currentYear,
                            int dtYears,
                            const std::vector<float>* tradeIntensityMatrix);
    void applyPopulationTotalsToCountries(std::vector<Country>& countries) const;
    void updateCitiesFromPopulation(std::vector<Country>& countries, int currentYear, int createEveryNYears, News& news);

	// Phase 6: climate baseline + dynamic weather anomalies (field grid resolution).
	std::vector<uint8_t> m_fieldLandMask;      // 0/1
	std::vector<uint8_t> m_fieldClimateZone;   // 0..4 for land, 255 for water
	std::vector<uint8_t> m_fieldBiome;         // 0..N for land, 255 for water
	std::vector<float> m_fieldTempMean;        // degrees C
    std::vector<float> m_fieldPrecipMean;      // 0..1
	std::vector<float> m_fieldTempAnom;        // degrees C (weather)
	std::vector<float> m_fieldPrecipAnom;      // -1..+1-ish (weather)
	std::vector<float> m_fieldFoodYieldMult;   // final multiplier (>=0)

	// Phase 7: coastal land candidates (field indices).
	std::vector<int> m_fieldCoastalLandCandidates;

	// Weather grid (coarse) for cheap spatial correlation.
	int m_weatherW = 0;
	int m_weatherH = 0;
	std::vector<float> m_weatherTemp;
    std::vector<float> m_weatherPrecip;
    int m_lastWeatherUpdateYear = -9999999;

	// Per-country aggregates (computed on-demand by scanning the field grid).
	mutable std::vector<float> m_countryClimateFoodMult;
    mutable std::vector<float> m_countryPrecipAnomMean;
    mutable int m_countryClimateCacheN = 0;
    std::vector<double> m_countryRefugeePush;

	// Phase 7: debug overlay - marks overseas field cells (updated every ~20 years).
	mutable std::vector<uint8_t> m_fieldOverseasMask;
	mutable int m_lastOverseasMaskYear = -9999999;
};
