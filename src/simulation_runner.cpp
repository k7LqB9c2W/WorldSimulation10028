#include "simulation_runner.h"

#include "culture.h"
#include "economy.h"
#include "great_people.h"
#include "map.h"
#include "news.h"
#include "technology.h"
#include "trade.h"

void runAuthoritativeYearStep(int year, SimulationStepContext& ctx) {
    ctx.map.updateCountries(ctx.countries, year, ctx.news, ctx.technologyManager);
    ctx.map.tickWeather(year, 1);
    ctx.macroEconomy.tickYear(year, 1, ctx.map, ctx.countries, ctx.technologyManager, ctx.tradeManager, ctx.news);
    ctx.map.tickDemographyAndCities(ctx.countries, year, 1, ctx.news, &ctx.macroEconomy.getLastTradeIntensity());
    ctx.technologyManager.tickYear(ctx.countries, ctx.map, &ctx.macroEconomy.getLastTradeIntensity(), year, 1);
    ctx.cultureManager.tickYear(ctx.countries, ctx.map, ctx.technologyManager, &ctx.macroEconomy.getLastTradeIntensity(), year, 1, ctx.news);
    ctx.greatPeopleManager.updateEffects(year, ctx.countries, ctx.news, 1);
    ctx.map.processPoliticalEvents(ctx.countries, ctx.tradeManager, year, ctx.news, ctx.technologyManager, ctx.cultureManager, 1);
}

void runGuiHeadlessAuthoritativeYearStep(int year, SimulationStepContext& ctx) {
    runAuthoritativeYearStep(year, ctx);
}

void runCliAuthoritativeYearStep(int year, SimulationStepContext& ctx) {
    runAuthoritativeYearStep(year, ctx);
}
