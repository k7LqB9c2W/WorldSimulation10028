#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "domestic_packages.h"
#include "simulation_context.h"

class Map;
class Country;
class TradeManager;

struct SettlementNode {
    int id = -1;
    int ownerCountry = -1;
    int fieldX = -1;
    int fieldY = -1;

    double population = 0.0;
    double carryingCapacity = 1.0;

    double foodProduced = 0.0;
    double foodImported = 0.0;
    double foodExported = 0.0;
    double calories = 0.0;

    double specialistShare = 0.02;

    double storageStock = 0.0;
    double waterFactor = 1.0;
    double soilFactor = 1.0;
    double techFactor = 1.0;
    double irrigationCapital = 0.0;

    // Eq09-Eq11 style local polity-economy state.
    double eliteShare = 0.10;          // resource-control concentration proxy
    double localLegitimacy = 0.45;     // 0..1
    double localAdminCapacity = 0.25;  // 0..1
    double extractionRate = 0.06;      // 0..1 of surplus

    int foundedYear = 0;
    int lastSplitYear = -9999999;

    std::array<double, static_cast<size_t>(SubsistenceMode::Count)> mix{
        0.42, 0.36, 0.10, 0.08, 0.04
    };
    std::vector<int> adoptedPackages;
};

struct TransportEdge {
    int fromNode = -1;
    int toNode = -1;
    double cost = 0.0;
    double capacity = 0.0;
    double reliability = 1.0;
    bool seaLink = false;
    // Eq25/Eq27/Eq28 campaign logistics diagnostics.
    double campaignLoad = 0.0;
    double campaignDeficit = 0.0;
    double campaignAttrition = 1.0;
};

struct SettlementCountryAggregate {
    double specialistPopulation = 0.0;
    double marketPotential = 0.0;
    double migrationPressureOut = 0.0;
    double migrationAttractiveness = 0.0;
    double knowledgeInfraSignal = 0.0;
};

class SettlementSystem {
public:
    explicit SettlementSystem(SimulationContext& ctx);
    ~SettlementSystem();

    void tickYear(int year,
                  Map& map,
                  std::vector<Country>& countries,
                  const TradeManager& tradeManager);

    bool enabled() const;
    void setDebugEnabled(bool enabled) { m_debugEnabled = enabled; }
    bool debugEnabled() const { return m_debugEnabled; }

    void printDebugSample(int year, const std::vector<Country>& countries, int maxSamples = 8) const;

    const std::vector<SettlementNode>& getNodes() const { return m_nodes; }
    const std::vector<TransportEdge>& getEdges() const { return m_edges; }
    const std::vector<float>& getCountryTradeHintMatrix() const { return m_countryTradeHintMatrix; }
    float getCountryTradeHintBlend() const;

    int getFieldWidth() const { return m_fieldW; }
    int getFieldHeight() const { return m_fieldH; }
    const std::vector<float>& getNodePopulationOverlay() const { return m_overlayNodePopulation; }
    const std::vector<std::uint8_t>& getDominantSubsistenceOverlay() const { return m_overlayDominantMode; }
    const std::vector<float>& getTransportDensityOverlay() const { return m_overlayTransportDensity; }

    std::string validateInvariants(const Map& map, int countryCount) const;
    std::uint64_t getLastDeterminismHash() const { return m_lastDeterminismHash; }

private:
    struct SettlementGpuRuntime;

    struct CandidateLink {
        int neighborIndex = -1;
        double cost = 0.0;
        double capacity = 0.0;
        double reliability = 1.0;
        bool seaLink = false;
    };

    SimulationContext* m_ctx = nullptr;
    bool m_initialized = false;
    bool m_startupLogged = false;
    bool m_debugEnabled = false;
    int m_lastTickYear = -9999999;
    int m_nextNodeId = 1;

    int m_fieldW = 0;
    int m_fieldH = 0;

    std::vector<SettlementNode> m_nodes;
    std::vector<TransportEdge> m_edges;
    std::vector<double> m_nodeOutgoingFlow;
    std::vector<double> m_nodeMarketPotential;
    std::vector<double> m_nodeUtility;

    std::vector<SettlementCountryAggregate> m_countryAgg;
    std::vector<float> m_countryTradeHintMatrix;

    std::vector<float> m_overlayNodePopulation;
    std::vector<std::uint8_t> m_overlayDominantMode;
    std::vector<float> m_overlayTransportDensity;

    // Eq18/Eq19 climate regime + fertility state (field-grid resolution).
    std::vector<float> m_fieldFertility;         // 0..1
    std::vector<std::uint8_t> m_fieldRegime;     // 0=normal,1=drought,2=pluvial,3=cold
    std::vector<float> m_fieldIrrigationCapital; // 0..1

    // Eq15-17/20-23 settlement disease state.
    std::vector<double> m_nodeS;
    std::vector<double> m_nodeI;
    std::vector<double> m_nodeR;
    std::vector<double> m_nodeDiseaseBurden;
    std::vector<double> m_nodeImportedInfection;

    // Eq24/Eq26 auxiliary settlement-state vectors.
    std::vector<double> m_nodeAdoptionPressure;
    std::vector<double> m_nodeJoinUtility;

    // Eq25/27/28 edge logistics attenuation (capacity/reliability penalty).
    std::vector<double> m_edgeLogisticsAttenuation;
    std::vector<double> m_nodeWarAttrition;
    std::vector<double> m_nodePastoralSeasonGain;
    std::vector<double> m_nodeExtractionRevenue;
    std::vector<double> m_nodePolitySwitchGain;

    std::unique_ptr<SettlementGpuRuntime> m_gpu;
    bool m_gpuStartupLogged = false;

    std::uint64_t m_lastDeterminismHash = 0;
    double m_lastFissionConservationError = 0.0;

    void ensureInitialized(int year, const Map& map, const std::vector<Country>& countries);
    void initializeNodesFromFieldPopulation(int year, const Map& map, const std::vector<Country>& countries);
    void syncNodeTotalsToCountryPopulation(const std::vector<Country>& countries);

    void updateSubsistenceMixAndPackages(int year, const Map& map, const std::vector<Country>& countries);
    void updateClimateRegimesAndFertility(int year, const Map& map);
    void updatePastoralMobilityRoutes(int year, const Map& map, const std::vector<Country>& countries);
    void recomputeFoodCaloriesAndCapacity(const Map& map, const std::vector<Country>& countries);
    void updateHouseholdsElitesExtraction(int year, std::vector<Country>& countries);
    void updateSettlementDisease(int year, const Map& map, const std::vector<Country>& countries);
    void applyGrowthAndSpecialization(int year, const std::vector<Country>& countries);
    void applyFission(int year, const Map& map, const std::vector<Country>& countries);

    void rebuildTransportGraph(int year, const Map& map, const std::vector<Country>& countries);
    void computeFlowsAndMigration(const Map& map, const std::vector<Country>& countries);
    void updateCampaignLogisticsAndAttrition(int year, const std::vector<Country>& countries);
    void updateAdoptionAndJoinUtility(int year, std::vector<Country>& countries);
    void applyPolityChoiceAssignment(int year, std::vector<Country>& countries);

    void aggregateToCountries(std::vector<Country>& countries);
    void buildCountryTradeHintMatrix(int countryCount);
    void rebuildOverlays();
    void computeDeterminismHash();

    int fieldIndex(int fx, int fy) const { return fy * m_fieldW + fx; }
    static double clamp01(double v);
    static double sigmoid(double x);
    static double finiteOr(double v, double fallback);
};
