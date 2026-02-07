#include "simulation_runner.h"

#include "culture.h"
#include "economy.h"
#include "great_people.h"
#include "map.h"
#include "news.h"
#include "technology.h"
#include "trade.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

double quantize(double v, double scale) {
    if (!std::isfinite(v)) return v;
    return std::round(v * scale) / scale;
}

std::uint64_t mixHash(std::uint64_t h, std::uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    return h;
}

std::uint64_t hashDouble(double v, double scale = 1.0e6) {
    if (!std::isfinite(v)) {
        return 0xFFFFFFFFFFFFFFFFull;
    }
    const double q = std::round(v * scale) / scale;
    return static_cast<std::uint64_t>(static_cast<std::int64_t>(q * scale));
}

std::uint64_t computeStateHash(const SimulationStepContext& ctx) {
    std::uint64_t h = 0xC0DEC0DE12345678ull;
    h = mixHash(h, static_cast<std::uint64_t>(ctx.countries.size()));
    for (const Country& c : ctx.countries) {
        h = mixHash(h, static_cast<std::uint64_t>(std::max(0, c.getCountryIndex()) + 1));
        h = mixHash(h, static_cast<std::uint64_t>(std::max<long long>(0, c.getPopulation())));
        h = mixHash(h, hashDouble(c.getStability(), 1.0e6));
        h = mixHash(h, hashDouble(c.getLegitimacy(), 1.0e6));
        h = mixHash(h, hashDouble(c.getAvgControl(), 1.0e6));
        h = mixHash(h, hashDouble(c.getAutonomyPressure(), 1.0e6));
        const auto& m = c.getMacroEconomy();
        h = mixHash(h, hashDouble(m.foodStock, 1.0e3));
        h = mixHash(h, hashDouble(m.nonFoodStock, 1.0e3));
        h = mixHash(h, hashDouble(m.capitalStock, 1.0e3));
        h = mixHash(h, hashDouble(m.infraStock, 1.0e3));
        h = mixHash(h, hashDouble(m.militarySupplyStock, 1.0e3));
        h = mixHash(h, hashDouble(m.foodSecurity, 1.0e6));
        h = mixHash(h, hashDouble(m.marketAccess, 1.0e6));
    }
    return h;
}

int determinismTraceYear() {
    static int year = []() {
        const char* v = std::getenv("WORLDSIM_TRACE_YEAR");
        if (!v || !*v) return std::numeric_limits<int>::max();
        return std::atoi(v);
    }();
    return year;
}

void maybeTraceDeterminismStage(const char* stage, int year, const SimulationStepContext& ctx) {
    if (year != determinismTraceYear()) {
        return;
    }
    const std::uint64_t h = computeStateHash(ctx);
    std::cout << "[det-trace] year=" << year << " stage=" << stage << " hash=" << h << std::endl;
}

void canonicalizeDeterministicState(SimulationStepContext& ctx) {
    if (!ctx.map.getConfig().world.deterministicMode) {
        return;
    }

    constexpr double kScale = 1.0e6;     // fine-grained state canonicalization
    constexpr double kGovScale = 1.0e4;  // governance/migration controls to suppress drift
    for (Country& c : ctx.countries) {
        c.canonicalizeDeterministicContainers();
        c.canonicalizeDeterministicScalars(kScale, kGovScale);
        c.setStability(quantize(c.getStability(), kGovScale));
        c.setLegitimacy(quantize(c.getLegitimacy(), kGovScale));
        c.setAvgControl(quantize(c.getAvgControl(), kGovScale));
        c.setAutonomyPressure(quantize(c.getAutonomyPressure(), kGovScale));

        auto& cohorts = c.getPopulationCohortsMutable();
        for (double& v : cohorts) {
            v = std::max(0.0, quantize(v, kScale));
        }
        c.renormalizePopulationCohortsToTotal();
        auto& epi = c.getEpidemicStateMutable();
        epi.s = std::clamp(quantize(epi.s, kScale), 0.0, 1.0);
        epi.i = std::clamp(quantize(epi.i, kScale), 0.0, 1.0);
        epi.r = std::clamp(quantize(epi.r, kScale), 0.0, 1.0);
        const double epiSum = epi.s + epi.i + epi.r;
        if (epiSum > 1.0e-9) {
            epi.s /= epiSum;
            epi.i /= epiSum;
            epi.r /= epiSum;
        } else {
            epi.s = 1.0;
            epi.i = 0.0;
            epi.r = 0.0;
        }

        auto& knowledge = c.getKnowledgeMutable();
        for (double& v : knowledge) {
            v = std::max(0.0, quantize(v, kScale));
        }
        auto& traits = c.getTraitsMutable();
        for (double& v : traits) {
            v = std::clamp(quantize(v, kScale), 0.0, 1.0);
        }
        auto& regions = c.getRegionsMutable();
        for (auto& r : regions) {
            r.popShare = std::clamp(quantize(r.popShare, kGovScale), 0.0, 1.0);
            r.localControl = std::clamp(quantize(r.localControl, kGovScale), 0.0, 1.0);
            r.grievance = std::clamp(quantize(r.grievance, kGovScale), 0.0, 1.0);
            r.elitePower = std::clamp(quantize(r.elitePower, kGovScale), 0.0, 1.0);
            r.distancePenalty = std::clamp(quantize(r.distancePenalty, kGovScale), 0.0, 1.0);
        }
        auto& ex = c.getExplorationMutable();
        ex.explorationDrive = static_cast<float>(std::clamp(quantize(static_cast<double>(ex.explorationDrive), kGovScale), 0.0, 1.0));
        ex.colonialOverstretch = static_cast<float>(std::clamp(quantize(static_cast<double>(ex.colonialOverstretch), kGovScale), 0.0, 1.0));
        ex.overseasLowControlYears = std::max(0, ex.overseasLowControlYears);
        c.setKnowledgeInfra(std::max(0.0, quantize(c.getKnowledgeInfra(), kScale)));
        c.setInnovationRate(std::max(0.0, quantize(c.getInnovationRate(), kScale)));

        Country::MacroEconomyState& m = c.getMacroEconomyMutable();
        m.foodStock = std::max(0.0, quantize(m.foodStock, kScale));
        m.foodStockCap = std::max(0.0, quantize(m.foodStockCap, kScale));
        m.spoilageRate = std::clamp(quantize(m.spoilageRate, kScale), 0.0, 1.0);
        m.nonFoodStock = std::max(0.0, quantize(m.nonFoodStock, kScale));
        m.capitalStock = std::max(0.0, quantize(m.capitalStock, kScale));
        m.infraStock = std::max(0.0, quantize(m.infraStock, kScale));
        m.militarySupplyStock = std::max(0.0, quantize(m.militarySupplyStock, kScale));
        m.servicesStock = std::max(0.0, quantize(m.servicesStock, kScale));
        m.cumulativeOreExtraction = std::max(0.0, quantize(m.cumulativeOreExtraction, kScale));
        m.cumulativeCoalExtraction = std::max(0.0, quantize(m.cumulativeCoalExtraction, kScale));
        m.refugeePush = std::clamp(quantize(m.refugeePush, kScale), 0.0, 1.0);
        m.foodSecurity = std::clamp(quantize(m.foodSecurity, kScale), 0.0, 1.0);
        m.marketAccess = std::clamp(quantize(m.marketAccess, kScale), 0.0, 1.0);
        m.famineSeverity = std::clamp(quantize(m.famineSeverity, kScale), 0.0, 1.0);
        m.diseaseBurden = std::clamp(quantize(m.diseaseBurden, kScale), 0.0, 1.0);
        m.migrationPressureOut = std::clamp(quantize(m.migrationPressureOut, kScale), 0.0, 1.0);
        m.migrationAttractiveness = std::clamp(quantize(m.migrationAttractiveness, kScale), 0.0, 1.0);
        m.humanCapital = std::clamp(quantize(m.humanCapital, kScale), 0.0, 1.0);
        m.knowledgeStock = std::clamp(quantize(m.knowledgeStock, kScale), 0.0, 1.0);
        m.inequality = std::clamp(quantize(m.inequality, kScale), 0.0, 1.0);
        m.educationInvestment = std::clamp(quantize(m.educationInvestment, kScale), 0.0, 1.0);
        m.rndInvestment = std::clamp(quantize(m.rndInvestment, kScale), 0.0, 1.0);
        m.connectivityIndex = std::clamp(quantize(m.connectivityIndex, kScale), 0.0, 1.0);
        m.institutionCapacity = std::clamp(quantize(m.institutionCapacity, kScale), 0.0, 1.0);
        m.compliance = std::clamp(quantize(m.compliance, kScale), 0.0, 1.0);
        m.leakageRate = std::clamp(quantize(m.leakageRate, kScale), 0.0, 1.0);
        m.realWage = std::max(0.0, quantize(m.realWage, kScale));
        m.netRevenue = std::max(0.0, quantize(m.netRevenue, kScale));
    }
}

} // namespace

void runAuthoritativeYearStep(int year, SimulationStepContext& ctx) {
    maybeTraceDeterminismStage("start", year, ctx);
    ctx.map.updateCountries(ctx.countries, year, ctx.news, ctx.technologyManager);
    maybeTraceDeterminismStage("updateCountries", year, ctx);
    ctx.map.tickWeather(year, 1);
    maybeTraceDeterminismStage("tickWeather", year, ctx);
    ctx.macroEconomy.tickYear(year, 1, ctx.map, ctx.countries, ctx.technologyManager, ctx.tradeManager, ctx.news);
    maybeTraceDeterminismStage("macroEconomy", year, ctx);
    ctx.map.tickDemographyAndCities(ctx.countries, year, 1, ctx.news, &ctx.macroEconomy.getLastTradeIntensity());
    maybeTraceDeterminismStage("demography", year, ctx);
    ctx.technologyManager.tickYear(ctx.countries, ctx.map, &ctx.macroEconomy.getLastTradeIntensity(), year, 1);
    maybeTraceDeterminismStage("technology", year, ctx);
    ctx.cultureManager.tickYear(ctx.countries, ctx.map, ctx.technologyManager, &ctx.macroEconomy.getLastTradeIntensity(), year, 1, ctx.news);
    maybeTraceDeterminismStage("culture", year, ctx);
    ctx.greatPeopleManager.updateEffects(year, ctx.countries, ctx.news, 1);
    maybeTraceDeterminismStage("greatPeople", year, ctx);
    ctx.map.processPoliticalEvents(ctx.countries, ctx.tradeManager, year, ctx.news, ctx.technologyManager, ctx.cultureManager, 1);
    maybeTraceDeterminismStage("politicalEvents", year, ctx);
    canonicalizeDeterministicState(ctx);
    maybeTraceDeterminismStage("canonicalized", year, ctx);
}

void runGuiHeadlessAuthoritativeYearStep(int year, SimulationStepContext& ctx) {
    runAuthoritativeYearStep(year, ctx);
}

void runCliAuthoritativeYearStep(int year, SimulationStepContext& ctx) {
    runAuthoritativeYearStep(year, ctx);
}
