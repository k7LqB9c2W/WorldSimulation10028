#include <SFML/Graphics.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

#include "country.h"
#include "culture.h"
#include "economy.h"
#include "great_people.h"
#include "map.h"
#include "news.h"
#include "simulation_context.h"
#include "technology.h"
#include "trade.h"

namespace {

struct RunOptions {
    std::uint64_t seed = 1;
    std::string configPath = "data/sim_config.toml";
    int startYear = std::numeric_limits<int>::min();
    int endYear = std::numeric_limits<int>::min();
    int checkpointEveryYears = 50;
    std::string outDir;
    int useGPU = -1; // -1 means "use config value", 0/1 are explicit overrides
};

struct MetricsSnapshot {
    int year = 0;
    double worldPopulation = 0.0;
    double urbanShare = 0.0;
    double medianCountryPop = 0.0;
    double medianCountryArea = 0.0;
    double warFrequencyPerCentury = 0.0;
    double tradeIntensity = 0.0;
    double capabilityTier1Share = 0.0;
    double capabilityTier2Share = 0.0;
    double capabilityTier3Share = 0.0;
    int collapseCount = 0;
    double foodSecurityMean = 0.0;
    double foodSecurityP10 = 0.0;
    double diseaseBurdenMean = 0.0;
    double diseaseBurdenP90 = 0.0;
};

bool parseUInt64(const std::string& s, std::uint64_t& out) {
    try {
        size_t pos = 0;
        const auto v = std::stoull(s, &pos);
        if (pos != s.size()) return false;
        out = static_cast<std::uint64_t>(v);
        return true;
    } catch (...) {
        return false;
    }
}

bool parseInt(const std::string& s, int& out) {
    try {
        size_t pos = 0;
        const auto v = std::stoll(s, &pos);
        if (pos != s.size()) return false;
        if (v < static_cast<long long>(std::numeric_limits<int>::min()) ||
            v > static_cast<long long>(std::numeric_limits<int>::max())) {
            return false;
        }
        out = static_cast<int>(v);
        return true;
    } catch (...) {
        return false;
    }
}

bool parseBool01(const std::string& s, bool& out) {
    if (s == "1" || s == "true" || s == "TRUE") {
        out = true;
        return true;
    }
    if (s == "0" || s == "false" || s == "FALSE") {
        out = false;
        return true;
    }
    return false;
}

void printUsage(const char* argv0) {
    std::cout << "Usage: " << (argv0 ? argv0 : "worldsim_cli")
              << " [--seed N] [--config path] [--startYear Y] [--endYear Y]\n"
              << "       [--checkpointEveryYears N] [--outDir path] [--useGPU 0|1]\n";
}

bool parseArgs(int argc, char** argv, RunOptions& opt) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] ? std::string(argv[i]) : std::string();
        auto requireValue = [&](std::string& out) -> bool {
            if (i + 1 >= argc) return false;
            out = argv[++i] ? std::string(argv[i]) : std::string();
            return true;
        };

        if (arg == "--help" || arg == "-h") {
            printUsage((argc > 0) ? argv[0] : nullptr);
            return false;
        } else if (arg == "--seed") {
            std::string v;
            if (!requireValue(v) || !parseUInt64(v, opt.seed)) return false;
        } else if (arg.rfind("--seed=", 0) == 0) {
            if (!parseUInt64(arg.substr(7), opt.seed)) return false;
        } else if (arg == "--config") {
            if (!requireValue(opt.configPath)) return false;
        } else if (arg.rfind("--config=", 0) == 0) {
            opt.configPath = arg.substr(9);
        } else if (arg == "--startYear") {
            std::string v;
            if (!requireValue(v) || !parseInt(v, opt.startYear)) return false;
        } else if (arg.rfind("--startYear=", 0) == 0) {
            if (!parseInt(arg.substr(12), opt.startYear)) return false;
        } else if (arg == "--endYear") {
            std::string v;
            if (!requireValue(v) || !parseInt(v, opt.endYear)) return false;
        } else if (arg.rfind("--endYear=", 0) == 0) {
            if (!parseInt(arg.substr(10), opt.endYear)) return false;
        } else if (arg == "--checkpointEveryYears") {
            std::string v;
            if (!requireValue(v) || !parseInt(v, opt.checkpointEveryYears)) return false;
        } else if (arg.rfind("--checkpointEveryYears=", 0) == 0) {
            if (!parseInt(arg.substr(23), opt.checkpointEveryYears)) return false;
        } else if (arg == "--outDir") {
            if (!requireValue(opt.outDir)) return false;
        } else if (arg.rfind("--outDir=", 0) == 0) {
            opt.outDir = arg.substr(9);
        } else if (arg == "--useGPU") {
            std::string v;
            bool parsed = false;
            if (!requireValue(v) || !parseBool01(v, parsed)) return false;
            opt.useGPU = parsed ? 1 : 0;
        } else if (arg.rfind("--useGPU=", 0) == 0) {
            bool parsed = false;
            if (!parseBool01(arg.substr(9), parsed)) return false;
            opt.useGPU = parsed ? 1 : 0;
        } else {
            std::cerr << "Unknown flag: " << arg << "\n";
            return false;
        }
    }
    return true;
}

bool loadImageWithFallback(sf::Image& image, const std::string& relativeAssetPath, const std::string& legacyPath) {
    if (image.loadFromFile(relativeAssetPath)) {
        return true;
    }
    return image.loadFromFile(legacyPath);
}

double percentile(std::vector<double> values, double p) {
    if (values.empty()) return 0.0;
    p = std::clamp(p, 0.0, 1.0);
    std::sort(values.begin(), values.end());
    const double pos = p * static_cast<double>(values.size() - 1);
    const size_t lo = static_cast<size_t>(std::floor(pos));
    const size_t hi = static_cast<size_t>(std::ceil(pos));
    const double t = pos - static_cast<double>(lo);
    return values[lo] * (1.0 - t) + values[hi] * t;
}

bool isFiniteNonNegative(double v) {
    return std::isfinite(v) && v >= 0.0;
}

std::string checkInvariants(const std::vector<Country>& countries,
                            const Map& map,
                            const std::vector<float>& tradeIntensity) {
    for (size_t i = 0; i < countries.size(); ++i) {
        const Country& c = countries[i];
        if (c.getPopulation() < 0) {
            std::ostringstream oss;
            oss << "negative population for country index " << i;
            return oss.str();
        }

        const auto& m = c.getMacroEconomy();
        const double checks[] = {
            m.foodStock, m.foodStockCap, m.nonFoodStock, m.capitalStock, m.infraStock,
            m.militarySupplyStock, m.servicesStock, m.foodSecurity, m.marketAccess,
            m.humanCapital, m.knowledgeStock, m.inequality, m.institutionCapacity,
            m.priceFood, m.priceGoods, m.priceServices, m.priceMilitarySupply,
            m.famineSeverity, m.migrationPressureOut, m.migrationAttractiveness, m.diseaseBurden
        };
        for (double v : checks) {
            if (!std::isfinite(v)) {
                std::ostringstream oss;
                oss << "non-finite macro value for country index " << i;
                return oss.str();
            }
        }
        if (!isFiniteNonNegative(m.foodStock) || !isFiniteNonNegative(m.foodStockCap) ||
            !isFiniteNonNegative(m.nonFoodStock) || !isFiniteNonNegative(m.capitalStock) ||
            !isFiniteNonNegative(m.infraStock) || !isFiniteNonNegative(m.militarySupplyStock) ||
            !isFiniteNonNegative(m.servicesStock)) {
            std::ostringstream oss;
            oss << "negative stock value for country index " << i;
            return oss.str();
        }

        const double f = map.getCountryFoodPotential(static_cast<int>(i));
        const double nf = map.getCountryNonFoodPotential(static_cast<int>(i));
        if (!std::isfinite(f) || !std::isfinite(nf)) {
            std::ostringstream oss;
            oss << "non-finite map potential for country index " << i;
            return oss.str();
        }
    }

    for (float v : tradeIntensity) {
        if (!std::isfinite(v)) {
            return "non-finite trade intensity";
        }
    }
    return std::string();
}

MetricsSnapshot computeSnapshot(const SimulationContext& ctx,
                                int year,
                                const std::vector<Country>& countries,
                                const std::vector<float>& tradeIntensity,
                                long long warStarts,
                                int yearsElapsed,
                                int collapseCount) {
    MetricsSnapshot s;
    s.year = year;
    s.collapseCount = collapseCount;

    std::vector<double> pops;
    std::vector<double> areas;
    std::vector<double> foodSec;
    std::vector<double> disease;
    pops.reserve(countries.size());
    areas.reserve(countries.size());
    foodSec.reserve(countries.size());
    disease.reserve(countries.size());

    double totalPop = 0.0;
    double totalUrban = 0.0;
    int live = 0;
    int tier1 = 0;
    int tier2 = 0;
    int tier3 = 0;

    const double tScale = std::max(0.25, ctx.config.tech.capabilityThresholdScale);
    const double t1 = 350.0 * tScale;
    const double t2 = 2800.0 * tScale;
    const double t3 = 16000.0 * tScale;

    for (size_t i = 0; i < countries.size(); ++i) {
        const Country& c = countries[i];
        if (c.getPopulation() <= 0) continue;
        const double pop = static_cast<double>(c.getPopulation());
        const double area = static_cast<double>(c.getTerritoryVec().size());
        totalPop += pop;
        totalUrban += std::max(0.0, c.getTotalCityPopulation());
        pops.push_back(pop);
        areas.push_back(area);
        foodSec.push_back(std::clamp(c.getMacroEconomy().foodSecurity, 0.0, 1.0));
        disease.push_back(std::clamp(c.getMacroEconomy().diseaseBurden, 0.0, 1.0));
        live++;

        const auto& k = c.getKnowledge();
        double meanDomain = 0.0;
        for (double v : k) meanDomain += std::max(0.0, v);
        meanDomain /= static_cast<double>(Country::kDomains);

        const double access = std::clamp(c.getMarketAccess(), 0.0, 1.0);
        const double inst = std::clamp(c.getInstitutionCapacity(), 0.0, 1.0);
        const double composite = meanDomain * (0.7 + 0.3 * access) * (0.7 + 0.3 * inst);
        if (composite >= t1) tier1++;
        if (composite >= t2) tier2++;
        if (composite >= t3) tier3++;
    }

    s.worldPopulation = totalPop;
    s.urbanShare = (totalPop > 1e-9) ? std::clamp(totalUrban / totalPop, 0.0, 1.0) : 0.0;
    s.medianCountryPop = percentile(pops, 0.50);
    s.medianCountryArea = percentile(areas, 0.50);
    s.foodSecurityMean = foodSec.empty() ? 0.0 : (std::accumulate(foodSec.begin(), foodSec.end(), 0.0) / static_cast<double>(foodSec.size()));
    s.foodSecurityP10 = percentile(foodSec, 0.10);
    s.diseaseBurdenMean = disease.empty() ? 0.0 : (std::accumulate(disease.begin(), disease.end(), 0.0) / static_cast<double>(disease.size()));
    s.diseaseBurdenP90 = percentile(disease, 0.90);

    s.capabilityTier1Share = (live > 0) ? static_cast<double>(tier1) / static_cast<double>(live) : 0.0;
    s.capabilityTier2Share = (live > 0) ? static_cast<double>(tier2) / static_cast<double>(live) : 0.0;
    s.capabilityTier3Share = (live > 0) ? static_cast<double>(tier3) / static_cast<double>(live) : 0.0;

    if (yearsElapsed > 0) {
        s.warFrequencyPerCentury = (100.0 * static_cast<double>(warStarts)) / static_cast<double>(yearsElapsed);
    }

    const int n = static_cast<int>(countries.size());
    if (n > 1 && tradeIntensity.size() >= static_cast<size_t>(n) * static_cast<size_t>(n)) {
        double sum = 0.0;
        int cnt = 0;
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j < n; ++j) {
                if (i == j) continue;
                sum += std::max(0.0f, tradeIntensity[static_cast<size_t>(i) * static_cast<size_t>(n) + static_cast<size_t>(j)]);
                cnt++;
            }
        }
        if (cnt > 0) {
            s.tradeIntensity = sum / static_cast<double>(cnt);
        }
    }

    return s;
}

std::string jsonEscape(const std::string& input) {
    std::string out;
    out.reserve(input.size() + 16);
    for (char c : input) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

bool containsYear(const std::set<int>& years, int y) {
    return years.find(y) != years.end();
}

} // namespace

int main(int argc, char** argv) {
    RunOptions opt;
    if (!parseArgs(argc, argv, opt)) {
        printUsage((argc > 0) ? argv[0] : nullptr);
        return 2;
    }

#ifdef _OPENMP
    omp_set_num_threads(1); // deterministic headless calibration mode
#endif

    SimulationContext ctx(opt.seed, opt.configPath);
    if (opt.useGPU >= 0) {
        ctx.config.economy.useGPU = (opt.useGPU != 0);
    }

    int worldStartYear = ctx.config.world.startYear;
    int startYear = (opt.startYear == std::numeric_limits<int>::min()) ? worldStartYear : opt.startYear;
    int endYear = (opt.endYear == std::numeric_limits<int>::min()) ? ctx.config.world.endYear : opt.endYear;
    if (startYear < worldStartYear) {
        startYear = worldStartYear;
    }
    if (endYear < startYear) {
        std::cerr << "Invalid year range: startYear=" << startYear << " endYear=" << endYear << "\n";
        return 2;
    }
    if (opt.checkpointEveryYears <= 0) {
        opt.checkpointEveryYears = 50;
    }

    if (opt.outDir.empty()) {
        std::ostringstream oss;
        oss << "out/cli_runs/seed_" << opt.seed;
        opt.outDir = oss.str();
    }

    std::filesystem::create_directories(opt.outDir);

    sf::Image baseImage;
    if (!loadImageWithFallback(baseImage, "assets/images/map.png", "map.png")) {
        std::cerr << "Error: Could not load map image.\n";
        return 1;
    }

    sf::Image resourceImage;
    sf::Image coalImage;
    sf::Image copperImage;
    sf::Image tinImage;
    sf::Image riverlandImage;
    if (!loadImageWithFallback(resourceImage, "assets/images/resource.png", "resource.png") ||
        !loadImageWithFallback(coalImage, "assets/images/coal.png", "coal.png") ||
        !loadImageWithFallback(copperImage, "assets/images/copper.png", "copper.png") ||
        !loadImageWithFallback(tinImage, "assets/images/tin.png", "tin.png") ||
        !loadImageWithFallback(riverlandImage, "assets/images/riverland.png", "riverland.png")) {
        std::cerr << "Error: Could not load one or more resource layer images.\n";
        return 1;
    }

    const sf::Vector2u baseSize = baseImage.getSize();
    auto validateLayerSize = [&](const sf::Image& layer, const char* label) -> bool {
        if (layer.getSize() != baseSize) {
            std::cerr << "Error: " << label << " size mismatch.\n";
            return false;
        }
        return true;
    };
    if (!validateLayerSize(resourceImage, "resource") ||
        !validateLayerSize(coalImage, "coal") ||
        !validateLayerSize(copperImage, "copper") ||
        !validateLayerSize(tinImage, "tin") ||
        !validateLayerSize(riverlandImage, "riverland")) {
        return 1;
    }

    const sf::Color landColor(0, 58, 0);
    const sf::Color waterColor(44, 90, 244);
    const int gridCellSize = 1;
    const int regionSize = 32;

    Map map(baseImage, resourceImage, coalImage, copperImage, tinImage, riverlandImage,
            gridCellSize, landColor, waterColor, regionSize, ctx);

    std::vector<Country> countries;
    const int numCountries = 100;
    const int maxCountries = 500;
    countries.reserve(maxCountries);

    if (!map.loadSpawnZones("assets/images/spawn.png")) {
        std::cerr << "Error: Could not load spawn zones.\n";
        return 1;
    }
    map.initializeCountries(countries, numCountries);

    TechnologyManager technologyManager;
    CultureManager cultureManager;
    GreatPeopleManager greatPeopleManager(ctx);
    TradeManager tradeManager(ctx);
    EconomyModelCPU macroEconomy(ctx);
    News news;

    std::cout << "worldsim_cli seed=" << opt.seed
              << " config=" << ctx.configPath
              << " hash=" << ctx.configHash
              << " start=" << startYear
              << " end=" << endYear
              << " gpu=" << (ctx.config.economy.useGPU ? 1 : 0)
              << "\n";

    auto simulateOneYear = [&](int year) {
        map.updateCountries(countries, year, news, technologyManager);
        map.tickWeather(year, 1);
        macroEconomy.tickYear(year, 1, map, countries, technologyManager, tradeManager, news);
        map.tickDemographyAndCities(countries, year, 1, news, &macroEconomy.getLastTradeIntensity());
        technologyManager.tickYear(countries, map, &macroEconomy.getLastTradeIntensity(), year, 1);
        cultureManager.tickYear(countries, map, technologyManager, &macroEconomy.getLastTradeIntensity(), year, 1, news);
        greatPeopleManager.updateEffects(year, countries, news, 1);
        map.processPoliticalEvents(countries, tradeManager, year, news, technologyManager, cultureManager, 1);
    };

    // Warm-up from world start to requested range start.
    for (int y = worldStartYear; y < startYear; ++y) {
        simulateOneYear(y);
    }

    std::vector<uint8_t> wasAtWar(countries.size(), 0u);
    for (size_t i = 0; i < countries.size(); ++i) {
        wasAtWar[i] = countries[i].isAtWar() ? 1u : 0u;
    }

    std::set<int> explicitCheckpoints(ctx.config.scoring.checkpointsYears.begin(),
                                      ctx.config.scoring.checkpointsYears.end());
    std::vector<MetricsSnapshot> checkpoints;
    checkpoints.reserve(static_cast<size_t>(1 + std::max(0, (endYear - startYear) / std::max(1, opt.checkpointEveryYears))));

    long long warStarts = 0;
    int collapseCount = 0;
    std::set<std::string> seenNewsTokens;

    bool invariantsOk = true;
    std::string invariantError;

    for (int y = startYear; y <= endYear; ++y) {
        simulateOneYear(y);

        if (wasAtWar.size() < countries.size()) {
            const size_t old = wasAtWar.size();
            wasAtWar.resize(countries.size(), 0u);
            for (size_t i = old; i < countries.size(); ++i) {
                wasAtWar[i] = countries[i].isAtWar() ? 1u : 0u;
            }
        }

        for (size_t i = 0; i < countries.size(); ++i) {
            const uint8_t now = countries[i].isAtWar() ? 1u : 0u;
            if (now != 0u && wasAtWar[i] == 0u) {
                warStarts++;
            }
            wasAtWar[i] = now;
        }

        for (const std::string& e : news.getEvents()) {
            const std::string token = std::to_string(y) + "|" + e;
            if (!seenNewsTokens.insert(token).second) {
                continue;
            }
            if (e.find("Civil war fractures") != std::string::npos ||
                e.find("Breakaway") != std::string::npos ||
                e.find("collapses and becomes extinct") != std::string::npos) {
                collapseCount++;
            }
        }

        const std::string inv = checkInvariants(countries, map, macroEconomy.getLastTradeIntensity());
        if (!inv.empty()) {
            invariantsOk = false;
            invariantError = "year " + std::to_string(y) + ": " + inv;
            break;
        }

        const bool cadenceHit = ((y - startYear) % opt.checkpointEveryYears) == 0;
        if (y == startYear || y == endYear || cadenceHit || containsYear(explicitCheckpoints, y)) {
            const int yearsElapsed = std::max(1, y - startYear + 1);
            checkpoints.push_back(computeSnapshot(ctx, y, countries, macroEconomy.getLastTradeIntensity(), warStarts, yearsElapsed, collapseCount));
        }
    }

    const std::filesystem::path outDir(opt.outDir);
    const std::filesystem::path csvPath = outDir / "timeseries.csv";
    const std::filesystem::path jsonPath = outDir / "run_summary.json";

    {
        std::ofstream csv(csvPath);
        csv << "year,worldPopulation,urbanShare,medianCountryPop,medianCountryArea,warFrequencyPerCentury,tradeIntensity,"
               "capabilityTier1Share,capabilityTier2Share,capabilityTier3Share,collapseCount,foodSecurityMean,foodSecurityP10,"
               "diseaseBurdenMean,diseaseBurdenP90\n";
        csv << std::fixed << std::setprecision(6);
        for (const MetricsSnapshot& s : checkpoints) {
            csv << s.year << ","
                << s.worldPopulation << ","
                << s.urbanShare << ","
                << s.medianCountryPop << ","
                << s.medianCountryArea << ","
                << s.warFrequencyPerCentury << ","
                << s.tradeIntensity << ","
                << s.capabilityTier1Share << ","
                << s.capabilityTier2Share << ","
                << s.capabilityTier3Share << ","
                << s.collapseCount << ","
                << s.foodSecurityMean << ","
                << s.foodSecurityP10 << ","
                << s.diseaseBurdenMean << ","
                << s.diseaseBurdenP90 << "\n";
        }
    }

    {
        std::ofstream js(jsonPath);
        js << std::fixed << std::setprecision(6);
        js << "{\n";
        js << "  \"seed\": " << opt.seed << ",\n";
        js << "  \"configPath\": \"" << jsonEscape(ctx.configPath) << "\",\n";
        js << "  \"configHash\": \"" << jsonEscape(ctx.configHash) << "\",\n";
        js << "  \"startYear\": " << startYear << ",\n";
        js << "  \"endYear\": " << endYear << ",\n";
        js << "  \"worldStartYear\": " << worldStartYear << ",\n";
        js << "  \"useGPU\": " << (ctx.config.economy.useGPU ? "true" : "false") << ",\n";
        js << "  \"invariants\": {\n";
        js << "    \"ok\": " << (invariantsOk ? "true" : "false") << ",\n";
        js << "    \"message\": \"" << jsonEscape(invariantError) << "\"\n";
        js << "  },\n";
        js << "  \"checkpoints\": [\n";
        for (size_t i = 0; i < checkpoints.size(); ++i) {
            const MetricsSnapshot& s = checkpoints[i];
            js << "    {\n";
            js << "      \"year\": " << s.year << ",\n";
            js << "      \"worldPopulation\": " << s.worldPopulation << ",\n";
            js << "      \"urbanShare\": " << s.urbanShare << ",\n";
            js << "      \"medianCountryPop\": " << s.medianCountryPop << ",\n";
            js << "      \"medianCountryArea\": " << s.medianCountryArea << ",\n";
            js << "      \"warFrequencyPerCentury\": " << s.warFrequencyPerCentury << ",\n";
            js << "      \"tradeIntensity\": " << s.tradeIntensity << ",\n";
            js << "      \"techCapabilityLevels\": {\n";
            js << "        \"tier1Share\": " << s.capabilityTier1Share << ",\n";
            js << "        \"tier2Share\": " << s.capabilityTier2Share << ",\n";
            js << "        \"tier3Share\": " << s.capabilityTier3Share << "\n";
            js << "      },\n";
            js << "      \"collapseCount\": " << s.collapseCount << ",\n";
            js << "      \"foodSecurity\": {\n";
            js << "        \"mean\": " << s.foodSecurityMean << ",\n";
            js << "        \"p10\": " << s.foodSecurityP10 << "\n";
            js << "      },\n";
            js << "      \"diseaseBurden\": {\n";
            js << "        \"mean\": " << s.diseaseBurdenMean << ",\n";
            js << "        \"p90\": " << s.diseaseBurdenP90 << "\n";
            js << "      }\n";
            js << "    }" << (i + 1 < checkpoints.size() ? "," : "") << "\n";
        }
        js << "  ]\n";
        js << "}\n";
    }

    std::cout << "Wrote " << jsonPath.string() << " and " << csvPath.string() << "\n";
    if (!invariantsOk) {
        std::cerr << "Invariant failure: " << invariantError << "\n";
        return 3;
    }
    return 0;
}
