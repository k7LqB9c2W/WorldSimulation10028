#pragma once

#include <vector>

class Country;
class Map;
class TechnologyManager;
class CultureManager;
class EconomyModelCPU;
class TradeManager;
class GreatPeopleManager;
class News;
class SettlementSystem;

struct SimulationStepContext {
    Map& map;
    std::vector<Country>& countries;
    TechnologyManager& technologyManager;
    CultureManager& cultureManager;
    EconomyModelCPU& macroEconomy;
    TradeManager& tradeManager;
    GreatPeopleManager& greatPeopleManager;
    SettlementSystem& settlementSystem;
    News& news;
};

// Authoritative yearly step used by GUI and CLI.
// This is intentionally the same sequence as the GUI live-year loop.
void runAuthoritativeYearStep(int year, SimulationStepContext& ctx);

// Named wrappers used by parity checks and future pipeline-specific instrumentation.
void runGuiHeadlessAuthoritativeYearStep(int year, SimulationStepContext& ctx);
void runCliAuthoritativeYearStep(int year, SimulationStepContext& ctx);
