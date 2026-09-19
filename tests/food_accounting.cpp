#include "economy.h"
#include "map.h"
#include "technology.h"
#include "trade.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
void near(double actual, double expected, const char* message) {
    check(std::isfinite(actual) && std::abs(actual - expected) <=
          1e-8 * std::max(1.0, std::abs(expected)), message);
}

struct World {
    SimulationContext ctx{42};
    TechnologyManager tech;
    TradeManager trade{ctx};
    EconomyModelCPU economy{ctx};
    News news;
    std::vector<Country> countries;
    std::unique_ptr<Map> map;

    World() {
        ctx.config.spawn.enabled = false;
        ctx.config.startTech.enabled = false;
        ctx.config.world.population.mode = SimulationConfig::WorldPopulationConfig::Mode::Fixed;
        ctx.config.world.population.fixedValue = 100000;
        auto load = [](const char* name) {
            sf::Image image;
            check(image.loadFromFile(std::string("assets/images/") + name),
                  "could not load test world asset");
            return image;
        };
        const auto base = load("map.png");
        const auto land = load("landmask.png");
        const auto height = load("heightmap.png");
        const auto resource = load("resource.png");
        const auto coal = load("coal.png");
        const auto copper = load("copper.png");
        const auto tin = load("tin.png");
        const auto riverland = load("riverland.png");
        map = std::make_unique<Map>(base, land, height, resource, coal, copper,
                                    tin, riverland, 1, sf::Color::White,
                                    sf::Color::Black, 10, ctx);
        countries.reserve(8);
        map->initializeCountries(countries, 1, &tech);
        check(countries.size() == 1 && countries[0].getPopulation() > 0,
              "fixture must have a populated country");
    }
};

// Feed already-settled balances into the real demographic update. Raw output,
// trade values and stored reserves must never override the settled ration.
double handoff(double coverage, double output, double imports, double exports,
               double stock, int years) {
    World w;
    auto& m = w.countries[0].getMacroEconomyMutable();
    m.foodSecurity = coverage;
    m.famineSeverity = 1.0 - coverage;
    m.foodStock = stock;
    m.foodStockCap = 10000.0;
    m.spoilageRate = 0.35;
    m.lastFoodOutput = output;
    m.importsValue = imports;
    m.exportsValue = exports;
    m.priceFood = 0.5;
    m.lastFoodShortage = 100.0 * (1.0 - coverage);
    w.map->tickDemographyAndCities(w.countries, -4999, years, w.news);
    near(m.foodStock, stock, "demography spent or replenished closing food stock");
    near(m.foodSecurity, coverage, "demography overwrote settled food security");
    near(m.famineSeverity, 1.0 - coverage, "demography overwrote settled famine");
    near(m.lastAvgNutrition, coverage, "nutrition differs from settled food coverage");
    near(m.lastFoodShortage, 100.0 * (1.0 - coverage), "shortage accounting changed");
    if (coverage < 1.0) check(m.lastDeathsFamine > 0.0, "shortage must cause famine deaths");
    else near(m.lastDeathsFamine, 0.0, "fully fed country suffered famine deaths");
    return m.lastDeathsFamine;
}

void economyConservation(int years) {
    World w;
    auto& m = w.countries[0].getMacroEconomyMutable();
    // Initialize the economy, then start a second interval with known reserves.
    w.economy.tickYear(-4999, 1, *w.map, w.countries, w.tech, w.trade, w.news);
    const double openingStock = m.foodStock = 75.0;
    w.economy.tickYear(-4998, years, *w.map, w.countries, w.tech, w.trade, w.news);
    const double available = openingStock + m.lastFoodOutput * years -
        (m.lastFoodSpoilageLoss + m.lastFoodStorageLoss) * years;
    const double required = m.lastFoodCons * years;
    const double eaten = std::min(available, required);
    const double discarded = std::max(0.0, available - eaten - m.foodStockCap);
    near(openingStock + m.lastFoodOutput * years,
         eaten + m.foodStock + discarded +
         (m.lastFoodSpoilageLoss + m.lastFoodStorageLoss) * years,
         "food conservation failed in economy");
    near(m.lastFoodShortage * years, required - eaten, "incorrect settled shortage");
    const double closingStock = m.foodStock;
    const double coverage = m.foodSecurity;
    w.map->tickDemographyAndCities(w.countries, -4998, years, w.news);
    near(m.foodStock, closingStock, "food conservation failed across demographic handoff");
    near(m.lastAvgNutrition, coverage, "economy nutrition was not carried into demography");
}
}

int main() {
    try {
        const double baseline = handoff(0.0, 0.0, 0.0, 0.0, 0.0, 1);
        near(handoff(0.0, 0.0, 1e9, 0.0, 0.0, 1), baseline,
             "non-food import value reduced famine");
        near(handoff(0.0, 1e9, 0.0, 1e9, 0.0, 1), baseline,
             "exported output was counted again as food");
        handoff(0.4, 1e9, 1e9, 0.0, 75.0, 1);
        handoff(0.4, 1e9, 1e9, 0.0, 75.0, 3);
        handoff(1.0, 0.0, 0.0, 0.0, 75.0, 1);
        economyConservation(1);
        economyConservation(3);
        std::cout << "Food accounting regression checks passed.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
