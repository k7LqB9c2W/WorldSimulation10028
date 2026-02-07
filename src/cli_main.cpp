#include <SFML/Graphics.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
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
#include <memory>

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
#include "simulation_runner.h"
#include "technology.h"
#include "trade.h"

namespace {

constexpr int kDefaultNumCountries = 100;
constexpr int kDefaultMaxCountries = 400; // Keep aligned with GUI defaults.

struct RunOptions {
    std::uint64_t seed = 1;
    std::string configPath = "data/sim_config.toml";
    int startYear = std::numeric_limits<int>::min();
    int endYear = std::numeric_limits<int>::min();
    int checkpointEveryYears = 50;
    std::string outDir;
    int useGPU = -1; // -1 means "use config value", 0/1 are explicit overrides
    int parityCheckYears = 0;
    int parityCheckpointEveryYears = 25;
    std::string parityRole; // internal: "gui" or "cli"
    std::string parityOut;  // internal: checkpoint checksum output path
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
              << "       [--checkpointEveryYears N] [--outDir path] [--useGPU 0|1]\n"
              << "       [--parityCheckYears N] [--parityCheckpointEveryYears N]\n"
              << "       [--parityRole gui|cli] [--parityOut path]\n";
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
        } else if (arg == "--parityCheckYears") {
            std::string v;
            if (!requireValue(v) || !parseInt(v, opt.parityCheckYears)) return false;
        } else if (arg.rfind("--parityCheckYears=", 0) == 0) {
            if (!parseInt(arg.substr(19), opt.parityCheckYears)) return false;
        } else if (arg == "--parityCheckpointEveryYears") {
            std::string v;
            if (!requireValue(v) || !parseInt(v, opt.parityCheckpointEveryYears)) return false;
        } else if (arg.rfind("--parityCheckpointEveryYears=", 0) == 0) {
            if (!parseInt(arg.substr(28), opt.parityCheckpointEveryYears)) return false;
        } else if (arg == "--parityRole") {
            if (!requireValue(opt.parityRole)) return false;
        } else if (arg.rfind("--parityRole=", 0) == 0) {
            opt.parityRole = arg.substr(13);
        } else if (arg == "--parityOut") {
            if (!requireValue(opt.parityOut)) return false;
        } else if (arg.rfind("--parityOut=", 0) == 0) {
            opt.parityOut = arg.substr(12);
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

struct CliRuntime {
    SimulationContext ctx;
    sf::Image baseImage;
    sf::Image resourceImage;
    sf::Image coalImage;
    sf::Image copperImage;
    sf::Image tinImage;
    sf::Image riverlandImage;
    std::unique_ptr<Map> map;
    std::vector<Country> countries;
    TechnologyManager technologyManager;
    CultureManager cultureManager;
    GreatPeopleManager greatPeopleManager;
    TradeManager tradeManager;
    EconomyModelCPU macroEconomy;
    News news;

    explicit CliRuntime(std::uint64_t seed, const std::string& configPath)
        : ctx(seed, configPath),
          map(),
          countries(),
          technologyManager(),
          cultureManager(),
          greatPeopleManager(ctx),
          tradeManager(ctx),
          macroEconomy(ctx),
          news() {}
};

struct ParityChecksum {
    long long worldPopulation = 0;
    long long perCountryPopulationSum = 0;
    double totalGDPSum = 0.0;
    double totalStockpiles = 0.0;
    long long totalTerritoryCells = 0;
};

ParityChecksum computeParityChecksum(const std::vector<Country>& countries) {
    ParityChecksum c;
    for (const Country& country : countries) {
        const long long pop = std::max<long long>(0, country.getPopulation());
        c.worldPopulation += pop;
        c.perCountryPopulationSum += pop;
        c.totalGDPSum += std::max(0.0, country.getGDP());
        const auto& m = country.getMacroEconomy();
        c.totalStockpiles +=
            std::max(0.0, m.foodStock) +
            std::max(0.0, m.nonFoodStock) +
            std::max(0.0, m.capitalStock) +
            std::max(0.0, m.infraStock) +
            std::max(0.0, m.militarySupplyStock) +
            std::max(0.0, m.servicesStock);
        c.totalTerritoryCells += static_cast<long long>(country.getTerritoryVec().size());
    }
    return c;
}

bool almostEqual(double a, double b, double relEps = 5e-4, double absEps = 100.0) {
    const double diff = std::fabs(a - b);
    if (diff <= absEps) return true;
    return diff <= relEps * std::max({1.0, std::fabs(a), std::fabs(b)});
}

double relativeDiff(double a, double b) {
    const double denom = std::max({1.0, std::fabs(a), std::fabs(b)});
    return std::fabs(a - b) / denom;
}

std::string parityMismatchReport(const ParityChecksum& gui, const ParityChecksum& cli) {
    std::ostringstream oss;
    bool mismatch = false;
    oss << std::setprecision(17);

    constexpr long long kPopulationTolerance = 128;
    constexpr long long kTerritoryTolerance = 8;

    const long long popDiff = std::llabs(gui.worldPopulation - cli.worldPopulation);
    if (popDiff > kPopulationTolerance) {
        mismatch = true;
        oss << "  worldPopulation mismatch: gui=" << gui.worldPopulation
            << " cli=" << cli.worldPopulation
            << " absDiff=" << popDiff << "\n";
    }
    const long long sumDiff = std::llabs(gui.perCountryPopulationSum - cli.perCountryPopulationSum);
    if (sumDiff > kPopulationTolerance) {
        mismatch = true;
        oss << "  perCountryPopulationSum mismatch: gui=" << gui.perCountryPopulationSum
            << " cli=" << cli.perCountryPopulationSum
            << " absDiff=" << sumDiff << "\n";
    }
    const long long territoryDiff = std::llabs(gui.totalTerritoryCells - cli.totalTerritoryCells);
    if (territoryDiff > kTerritoryTolerance) {
        mismatch = true;
        oss << "  totalTerritoryCells mismatch: gui=" << gui.totalTerritoryCells
            << " cli=" << cli.totalTerritoryCells
            << " absDiff=" << territoryDiff << "\n";
    }
    if (!almostEqual(gui.totalGDPSum, cli.totalGDPSum)) {
        mismatch = true;
        oss << "  totalGDPSum mismatch: gui=" << gui.totalGDPSum
            << " cli=" << cli.totalGDPSum
            << " absDiff=" << std::fabs(gui.totalGDPSum - cli.totalGDPSum)
            << " relDiff=" << relativeDiff(gui.totalGDPSum, cli.totalGDPSum) << "\n";
    }
    if (!almostEqual(gui.totalStockpiles, cli.totalStockpiles)) {
        mismatch = true;
        oss << "  totalStockpiles mismatch: gui=" << gui.totalStockpiles
            << " cli=" << cli.totalStockpiles
            << " absDiff=" << std::fabs(gui.totalStockpiles - cli.totalStockpiles)
            << " relDiff=" << relativeDiff(gui.totalStockpiles, cli.totalStockpiles) << "\n";
    }

    if (!mismatch) {
        return std::string();
    }
    return oss.str();
}

bool loadCommonImages(CliRuntime& rt, std::string* errorOut) {
    if (!loadImageWithFallback(rt.baseImage, "assets/images/map.png", "map.png")) {
        if (errorOut) *errorOut = "Could not load map image.";
        return false;
    }
    if (!loadImageWithFallback(rt.resourceImage, "assets/images/resource.png", "resource.png") ||
        !loadImageWithFallback(rt.coalImage, "assets/images/coal.png", "coal.png") ||
        !loadImageWithFallback(rt.copperImage, "assets/images/copper.png", "copper.png") ||
        !loadImageWithFallback(rt.tinImage, "assets/images/tin.png", "tin.png") ||
        !loadImageWithFallback(rt.riverlandImage, "assets/images/riverland.png", "riverland.png")) {
        if (errorOut) *errorOut = "Could not load one or more resource layer images.";
        return false;
    }

    const sf::Vector2u baseSize = rt.baseImage.getSize();
    auto validateLayerSize = [&](const sf::Image& layer, const char* label) -> bool {
        if (layer.getSize() != baseSize) {
            if (errorOut) {
                *errorOut = std::string(label) + " size mismatch.";
            }
            return false;
        }
        return true;
    };
    if (!validateLayerSize(rt.resourceImage, "resource") ||
        !validateLayerSize(rt.coalImage, "coal") ||
        !validateLayerSize(rt.copperImage, "copper") ||
        !validateLayerSize(rt.tinImage, "tin") ||
        !validateLayerSize(rt.riverlandImage, "riverland")) {
        return false;
    }
    return true;
}

bool initializeRuntime(CliRuntime& rt,
                       const RunOptions& opt,
                       int numCountries,
                       int maxCountries,
                       std::string* errorOut) {
    if (opt.useGPU >= 0) {
        rt.ctx.config.economy.useGPU = (opt.useGPU != 0);
    }

    if (!loadCommonImages(rt, errorOut)) {
        return false;
    }

    const sf::Color landColor(0, 58, 0);
    const sf::Color waterColor(44, 90, 244);
    const int gridCellSize = 1;
    const int regionSize = 32;
    rt.map = std::make_unique<Map>(rt.baseImage,
                                   rt.resourceImage,
                                   rt.coalImage,
                                   rt.copperImage,
                                   rt.tinImage,
                                   rt.riverlandImage,
                                   gridCellSize,
                                   landColor,
                                   waterColor,
                                   regionSize,
                                   rt.ctx);

    rt.countries.clear();
    rt.countries.reserve(maxCountries);

    if (!rt.map->loadSpawnZones("assets/images/spawn.png")) {
        if (errorOut) *errorOut = "Could not load spawn zones.";
        return false;
    }
    rt.map->initializeCountries(rt.countries, numCountries);
    return true;
}

bool isParityRoleValid(const std::string& role) {
    return role == "gui" || role == "cli";
}

std::string shellQuote(std::string arg) {
    if (arg.find_first_of(" \t\n\"") == std::string::npos) {
        return arg;
    }
    std::string out;
    out.reserve(arg.size() + 2);
    out.push_back('"');
    for (char c : arg) {
        if (c == '"') {
            out += "\\\"";
        } else {
            out.push_back(c);
        }
    }
    out.push_back('"');
    return out;
}

bool parseLongLong(const std::string& s, long long& out) {
    try {
        size_t pos = 0;
        const auto v = std::stoll(s, &pos);
        if (pos != s.size()) return false;
        out = v;
        return true;
    } catch (...) {
        return false;
    }
}

bool parseDouble(const std::string& s, double& out) {
    try {
        size_t pos = 0;
        const auto v = std::stod(s, &pos);
        if (pos != s.size()) return false;
        out = v;
        return true;
    } catch (...) {
        return false;
    }
}

bool collectParityChecksums(const RunOptions& opt,
                            bool useGuiPath,
                            std::vector<int>& yearsOut,
                            std::vector<ParityChecksum>& sumsOut,
                            std::string* errorOut) {
    const int parityYears = std::max(1, opt.parityCheckYears);
    const int parityCheckpointEvery = std::max(1, opt.parityCheckpointEveryYears);

    CliRuntime runtime(opt.seed, opt.configPath);
    std::string initError;
    if (!initializeRuntime(runtime, opt, kDefaultNumCountries, kDefaultMaxCountries, &initError)) {
        if (errorOut) *errorOut = initError;
        return false;
    }

    yearsOut.clear();
    sumsOut.clear();

    const int worldStart = runtime.ctx.config.world.startYear;
    const int endYear = worldStart + parityYears - 1;
    for (int year = worldStart; year <= endYear; ++year) {
        SimulationStepContext stepCtx{
            *runtime.map,
            runtime.countries,
            runtime.technologyManager,
            runtime.cultureManager,
            runtime.macroEconomy,
            runtime.tradeManager,
            runtime.greatPeopleManager,
            runtime.news
        };
        if (useGuiPath) {
            runGuiHeadlessAuthoritativeYearStep(year, stepCtx);
        } else {
            runCliAuthoritativeYearStep(year, stepCtx);
        }

        const bool checkpoint = ((year - worldStart) % parityCheckpointEvery) == 0 || year == endYear;
        if (!checkpoint) {
            continue;
        }
        yearsOut.push_back(year);
        sumsOut.push_back(computeParityChecksum(runtime.countries));
    }

    return true;
}

bool writeParityChecksumsCsv(const std::filesystem::path& path,
                             const std::vector<int>& years,
                             const std::vector<ParityChecksum>& sums,
                             std::string* errorOut) {
    if (years.size() != sums.size()) {
        if (errorOut) *errorOut = "internal parity size mismatch";
        return false;
    }

    std::ofstream out(path);
    if (!out) {
        if (errorOut) *errorOut = "could not open output file: " + path.string();
        return false;
    }

    out << "year,worldPopulation,perCountryPopulationSum,totalGDPSum,totalStockpiles,totalTerritoryCells\n";
    out << std::setprecision(17);
    for (size_t i = 0; i < years.size(); ++i) {
        out << years[i] << ","
            << sums[i].worldPopulation << ","
            << sums[i].perCountryPopulationSum << ","
            << sums[i].totalGDPSum << ","
            << sums[i].totalStockpiles << ","
            << sums[i].totalTerritoryCells << "\n";
    }
    return true;
}

bool readParityChecksumsCsv(const std::filesystem::path& path,
                            std::vector<int>& yearsOut,
                            std::vector<ParityChecksum>& sumsOut,
                            std::string* errorOut) {
    std::ifstream in(path);
    if (!in) {
        if (errorOut) *errorOut = "could not open parity file: " + path.string();
        return false;
    }

    yearsOut.clear();
    sumsOut.clear();

    std::string line;
    if (!std::getline(in, line)) {
        if (errorOut) *errorOut = "empty parity file: " + path.string();
        return false;
    }

    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::string c0, c1, c2, c3, c4, c5;
        if (!std::getline(ss, c0, ',') ||
            !std::getline(ss, c1, ',') ||
            !std::getline(ss, c2, ',') ||
            !std::getline(ss, c3, ',') ||
            !std::getline(ss, c4, ',') ||
            !std::getline(ss, c5, ',')) {
            if (errorOut) *errorOut = "malformed parity row in " + path.string();
            return false;
        }

        int year = 0;
        long long wp = 0;
        long long pps = 0;
        double gdp = 0.0;
        double stock = 0.0;
        long long cells = 0;
        if (!parseInt(c0, year) ||
            !parseLongLong(c1, wp) ||
            !parseLongLong(c2, pps) ||
            !parseDouble(c3, gdp) ||
            !parseDouble(c4, stock) ||
            !parseLongLong(c5, cells)) {
            if (errorOut) *errorOut = "invalid parity value in " + path.string();
            return false;
        }

        yearsOut.push_back(year);
        sumsOut.push_back(ParityChecksum{wp, pps, gdp, stock, cells});
    }

    return true;
}

int runParityDumpMode(const RunOptions& opt) {
    if (!isParityRoleValid(opt.parityRole)) {
        std::cerr << "Invalid --parityRole. Expected gui or cli.\n";
        return 2;
    }
    if (opt.parityOut.empty()) {
        std::cerr << "--parityOut is required when --parityRole is set.\n";
        return 2;
    }
    if (opt.parityCheckYears <= 0) {
        std::cerr << "--parityCheckYears must be > 0 for parity dump mode.\n";
        return 2;
    }

    std::vector<int> years;
    std::vector<ParityChecksum> sums;
    std::string error;
    if (!collectParityChecksums(opt, opt.parityRole == "gui", years, sums, &error)) {
        std::cerr << "Parity dump failed: " << error << "\n";
        return 1;
    }

    std::filesystem::path outPath(opt.parityOut);
    if (outPath.has_parent_path()) {
        std::filesystem::create_directories(outPath.parent_path());
    }
    if (!writeParityChecksumsCsv(outPath, years, sums, &error)) {
        std::cerr << "Parity dump failed: " << error << "\n";
        return 1;
    }
    return 0;
}

int runParityCheck(const RunOptions& opt, const std::string& argv0) {
    const int parityYears = std::max(1, opt.parityCheckYears);
    const int parityCheckpointEvery = std::max(1, opt.parityCheckpointEveryYears);

    CliRuntime preview(opt.seed, opt.configPath);
    const int worldStart = preview.ctx.config.world.startYear;
    const int endYear = worldStart + parityYears - 1;
    std::cout << "Running parity check: seed=" << opt.seed
              << " years=" << parityYears
              << " checkpointEvery=" << parityCheckpointEvery
              << " start=" << worldStart
              << " end=" << endYear
              << "\n";

    const std::filesystem::path parityDir = std::filesystem::path("out") / "cli_parity";
    std::filesystem::create_directories(parityDir);
    const std::string suffix = std::to_string(opt.seed) + "_" +
                               std::to_string(parityYears) + "_" +
                               std::to_string(parityCheckpointEvery);
    const std::filesystem::path guiCsv = parityDir / ("gui_" + suffix + ".csv");
    const std::filesystem::path cliCsv = parityDir / ("cli_" + suffix + ".csv");
    const std::filesystem::path guiLog = parityDir / ("gui_" + suffix + ".log");
    const std::filesystem::path cliLog = parityDir / ("cli_" + suffix + ".log");

    std::filesystem::path exePath(argv0);
    if (!exePath.is_absolute()) {
        exePath = std::filesystem::absolute(exePath);
    }
    if (!std::filesystem::exists(exePath)) {
        const std::filesystem::path fallback = std::filesystem::current_path() / "out/cmake/release/bin/worldsim_cli.exe";
        if (std::filesystem::exists(fallback)) {
            exePath = fallback;
        }
    }

    auto runChild = [&](const char* role,
                        const std::filesystem::path& csvPath,
                        const std::filesystem::path& logPath) -> bool {
        std::ostringstream cmd;
        cmd << shellQuote(exePath.string())
            << " --seed " << opt.seed
            << " --config " << shellQuote(opt.configPath)
            << " --parityCheckYears " << parityYears
            << " --parityCheckpointEveryYears " << parityCheckpointEvery
            << " --parityRole " << role
            << " --parityOut " << shellQuote(csvPath.string());
        if (opt.useGPU >= 0) {
            cmd << " --useGPU " << (opt.useGPU != 0 ? 1 : 0);
        }
        cmd << " > " << shellQuote(logPath.string()) << " 2>&1";

        const int rc = std::system(cmd.str().c_str());
        if (rc != 0) {
            std::cerr << "Parity child run failed for role=" << role
                      << " exitCode=" << rc
                      << " log=" << logPath.string() << "\n";
            return false;
        }
        return true;
    };

    if (!runChild("gui", guiCsv, guiLog) || !runChild("cli", cliCsv, cliLog)) {
        return 6;
    }

    std::vector<int> guiYears;
    std::vector<ParityChecksum> guiChecks;
    std::vector<int> cliYears;
    std::vector<ParityChecksum> cliChecks;
    std::string error;

    if (!readParityChecksumsCsv(guiCsv, guiYears, guiChecks, &error)) {
        std::cerr << "Parity read failed for GUI checksums: " << error << "\n";
        return 6;
    }
    if (!readParityChecksumsCsv(cliCsv, cliYears, cliChecks, &error)) {
        std::cerr << "Parity read failed for CLI checksums: " << error << "\n";
        return 6;
    }
    if (guiYears != cliYears || guiChecks.size() != cliChecks.size()) {
        std::cerr << "PARITY MISMATCH: checkpoint structure differs between GUI-path and CLI-path runs.\n";
        return 5;
    }

    for (size_t i = 0; i < guiChecks.size(); ++i) {
        const std::string mismatchReport = parityMismatchReport(guiChecks[i], cliChecks[i]);
        if (!mismatchReport.empty()) {
            std::cerr << "PARITY MISMATCH at year " << guiYears[i] << "\n";
            std::cerr << mismatchReport;
            return 5;
        }
    }

    std::cout << "Parity check PASSED for " << parityYears << " years.\n";
    return 0;
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

    if (!opt.parityRole.empty()) {
        return runParityDumpMode(opt);
    }

    if (opt.parityCheckYears > 0) {
        const std::string argv0 = (argc > 0 && argv && argv[0]) ? std::string(argv[0]) : std::string("worldsim_cli");
        return runParityCheck(opt, argv0);
    }

    CliRuntime runtime(opt.seed, opt.configPath);
    std::string initError;
    if (!initializeRuntime(runtime, opt, kDefaultNumCountries, kDefaultMaxCountries, &initError)) {
        std::cerr << "Error: " << initError << "\n";
        return 1;
    }

    SimulationContext& ctx = runtime.ctx;
    Map& map = *runtime.map;
    std::vector<Country>& countries = runtime.countries;
    TechnologyManager& technologyManager = runtime.technologyManager;
    CultureManager& cultureManager = runtime.cultureManager;
    GreatPeopleManager& greatPeopleManager = runtime.greatPeopleManager;
    TradeManager& tradeManager = runtime.tradeManager;
    EconomyModelCPU& macroEconomy = runtime.macroEconomy;
    News& news = runtime.news;

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

    std::cout << "worldsim_cli seed=" << opt.seed
              << " config=" << ctx.configPath
              << " hash=" << ctx.configHash
              << " start=" << startYear
              << " end=" << endYear
              << " gpu=" << (ctx.config.economy.useGPU ? 1 : 0)
              << "\n";

    auto simulateOneYear = [&](int year) {
        SimulationStepContext stepCtx{
            map,
            countries,
            technologyManager,
            cultureManager,
            macroEconomy,
            tradeManager,
            greatPeopleManager,
            news
        };
        runCliAuthoritativeYearStep(year, stepCtx);
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
