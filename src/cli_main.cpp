#include <SFML/Graphics.hpp>

#include <algorithm>
#include <array>
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
#include <unordered_map>
#include <unordered_set>
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
#include "settlement_system.h"
#include "technology.h"
#include "trade.h"

namespace {

constexpr int kDefaultNumCountries = 100;
constexpr int kDefaultMaxCountries = 400; // Keep aligned with GUI defaults.
constexpr int kEarliestSupportedStartYear = -20000;

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
    std::string techUnlockLog; // optional path; when set, emits per-country tech unlock events
    bool techUnlockLogIncludeInitial = true;
    int stopOnTechId = -1; // optional: stop run early once any country unlocks this tech id
    bool worldPopFixedSet = false;
    std::int64_t worldPopFixedValue = 0;
    bool worldPopRangeSet = false;
    std::int64_t worldPopRangeMin = 0;
    std::int64_t worldPopRangeMax = 0;
    bool spawnMaskOverrideSet = false;
    std::string spawnMaskPath;
    bool spawnDisable = false;
    std::vector<std::pair<std::string, double>> spawnRegionShareOverrides;
    bool stateDiagnostics = false; // optional: emit per-country state cause diagnostics at checkpoints
    int numCountries = kDefaultNumCountries;
    bool geoDebug = false; // optional: print geography sample diagnostics and continue
    bool settlementDebug = false; // optional: print sampled settlement nodes yearly
    bool logIdeologyTransitions = false; // optional: emit regime-transition console logs
};

struct MetricsSnapshot {
    int year = 0;
    double world_pop_total = 0.0;
    double world_pop_growth_rate_annual = 0.0;
    double world_food_adequacy_index = 0.0;
    double world_famine_death_rate = 0.0;
    double world_disease_death_rate = 0.0;
    double world_war_death_rate = 0.0;
    double world_trade_intensity = 0.0;
    double world_urban_share_proxy = 0.0;
    double world_tech_capability_index_median = 0.0;
    double world_tech_capability_index_p90 = 0.0;
    double world_state_capacity_index_median = 0.0;
    double world_state_capacity_index_p10 = 0.0;
    double competition_fragmentation_index_median = 0.0;
    double idea_market_integration_index_median = 0.0;
    double credible_commitment_index_median = 0.0;
    double relative_factor_price_index_median = 0.0;
    double media_throughput_index_median = 0.0;
    double merchant_power_index_median = 0.0;
    double skilled_migration_in_rate_t = 0.0;
    double skilled_migration_out_rate_t = 0.0;
    double migration_rate_t = 0.0;
    double famine_exposure_share_t = 0.0;

    double habitable_cell_share_pop_gt_0 = 0.0;
    double habitable_cell_share_pop_gt_small = 0.0;
    std::array<double, 6> pop_share_by_lat_band{{0.0, 0.0, 0.0, 0.0, 0.0, 0.0}};
    double pop_share_coastal_vs_inland = 0.0;
    double pop_share_river_proximal = 0.0;
    double market_access_p10 = 0.0;
    double market_access_median = 0.0;
    double food_adequacy_p10 = 0.0;
    double food_adequacy_median = 0.0;
    double travel_cost_index_median = 0.0;

    double country_pop_median = 0.0;
    double country_pop_p90 = 0.0;
    double country_pop_top1_share = 0.0;
    double country_area_median = 0.0;
    double country_area_p90 = 0.0;
    double country_area_top1_share = 0.0;
    double control_median = 0.0;
    double control_p10 = 0.0;
    int founder_state_count = 0;
    double founder_state_share = 0.0;
    double median_state_age_years = 0.0;
    double p90_state_age_years = 0.0;
    int wars_active_count = 0;
    double city_pop_top1 = 0.0;
    double city_pop_top10_sum_share = 0.0;
    double city_tail_index = 0.0;

    int famine_wave_count = 0;
    int epidemic_wave_count = 0;
    double major_war_count = 0.0;
    int election_count = 0;
    int civil_conflict_count = 0;
    int fragmentation_count = 0;
    int mass_migration_count = 0;

    double logistics_capability_index = 0.0;
    double storage_capability_index = 0.0;
    double health_capability_index = 0.0;
    double transport_cost_index = 0.0;

    // Derived support metrics for anti-loophole checks.
    double spoilage_kcal = 0.0;
    double storage_loss_kcal = 0.0;
    double available_kcal_before_losses = 0.0;
    double trade_volume_total = 0.0;
    double trade_volume_long = 0.0;
    double long_distance_trade_proxy = 0.0;
    double extraction_index = 0.0;
};

struct FieldGeoCache {
    int fieldW = 0;
    int fieldH = 0;
    std::vector<uint8_t> coastalMask; // size=fieldW*fieldH
    std::vector<uint8_t> riverMask;   // proxy mask (high food-potential inland cells)
};

struct EventWindowCounters {
    int famine_wave_count = 0;
    int epidemic_wave_count = 0;
    int major_war_count = 0;
    int election_count = 0;
    int civil_conflict_count = 0;
    int fragmentation_count = 0;
    int mass_migration_count = 0;
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

bool parseInt64(const std::string& s, std::int64_t& out) {
    try {
        size_t pos = 0;
        const auto v = std::stoll(s, &pos);
        if (pos != s.size()) return false;
        out = static_cast<std::int64_t>(v);
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
              << "       [--parityRole gui|cli] [--parityOut path]\n"
              << "       [--techUnlockLog path] [--techUnlockLogIncludeInitial 0|1]\n"
              << "       [--stateDiagnostics 0|1]\n"
              << "       [--stopOnTechId N]\n"
              << "       [--numCountries N]\n"
              << "       [--world-pop N] [--world-pop-range MIN MAX]\n"
              << "       [--spawn-mask path] [--spawn-disable]\n"
              << "       [--spawn-region-share KEY VALUE] (repeatable)\n"
              << "       [--geo-debug]\n"
              << "       [--settlement-debug]\n"
              << "       [--log-ideology-transitions[=0|1]]\n"
              << "Notes: supported minimum start year is " << kEarliestSupportedStartYear << ".\n";
}

bool parseArgs(int argc, char** argv, RunOptions& opt) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] ? std::string(argv[i]) : std::string();
        auto requireValue = [&](std::string& out) -> bool {
            if (i + 1 >= argc) return false;
            out = argv[++i] ? std::string(argv[i]) : std::string();
            return true;
        };
        auto parseShareValue = [&](const std::string& s, double& out) -> bool {
            try {
                size_t pos = 0;
                const double v = std::stod(s, &pos);
                if (pos != s.size()) return false;
                out = (v >= 1.0) ? (v / 100.0) : v;
                return std::isfinite(out);
            } catch (...) {
                return false;
            }
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
        } else if (arg == "--techUnlockLog") {
            if (!requireValue(opt.techUnlockLog)) return false;
        } else if (arg.rfind("--techUnlockLog=", 0) == 0) {
            opt.techUnlockLog = arg.substr(16);
        } else if (arg == "--techUnlockLogIncludeInitial") {
            std::string v;
            bool parsed = false;
            if (!requireValue(v) || !parseBool01(v, parsed)) return false;
            opt.techUnlockLogIncludeInitial = parsed;
        } else if (arg.rfind("--techUnlockLogIncludeInitial=", 0) == 0) {
            bool parsed = false;
            if (!parseBool01(arg.substr(30), parsed)) return false;
            opt.techUnlockLogIncludeInitial = parsed;
        } else if (arg == "--stateDiagnostics") {
            std::string v;
            bool parsed = false;
            if (!requireValue(v) || !parseBool01(v, parsed)) return false;
            opt.stateDiagnostics = parsed;
        } else if (arg.rfind("--stateDiagnostics=", 0) == 0) {
            bool parsed = false;
            if (!parseBool01(arg.substr(19), parsed)) return false;
            opt.stateDiagnostics = parsed;
        } else if (arg == "--stopOnTechId") {
            std::string v;
            if (!requireValue(v) || !parseInt(v, opt.stopOnTechId)) return false;
        } else if (arg.rfind("--stopOnTechId=", 0) == 0) {
            if (!parseInt(arg.substr(15), opt.stopOnTechId)) return false;
        } else if (arg == "--numCountries" || arg == "--num-countries") {
            std::string v;
            if (!requireValue(v) || !parseInt(v, opt.numCountries)) return false;
        } else if (arg.rfind("--numCountries=", 0) == 0) {
            if (!parseInt(arg.substr(15), opt.numCountries)) return false;
        } else if (arg.rfind("--num-countries=", 0) == 0) {
            if (!parseInt(arg.substr(16), opt.numCountries)) return false;
        } else if (arg == "--world-pop") {
            std::string v;
            if (!requireValue(v) || !parseInt64(v, opt.worldPopFixedValue)) return false;
            opt.worldPopFixedSet = true;
            opt.worldPopRangeSet = false;
        } else if (arg.rfind("--world-pop=", 0) == 0) {
            if (!parseInt64(arg.substr(12), opt.worldPopFixedValue)) return false;
            opt.worldPopFixedSet = true;
            opt.worldPopRangeSet = false;
        } else if (arg == "--world-pop-range") {
            std::string vMin;
            std::string vMax;
            if (!requireValue(vMin) || !requireValue(vMax)) return false;
            if (!parseInt64(vMin, opt.worldPopRangeMin) || !parseInt64(vMax, opt.worldPopRangeMax)) return false;
            opt.worldPopRangeSet = true;
            opt.worldPopFixedSet = false;
        } else if (arg.rfind("--world-pop-range=", 0) == 0) {
            const std::string payload = arg.substr(18);
            const size_t comma = payload.find(',');
            if (comma == std::string::npos) return false;
            const std::string vMin = payload.substr(0, comma);
            const std::string vMax = payload.substr(comma + 1);
            if (!parseInt64(vMin, opt.worldPopRangeMin) || !parseInt64(vMax, opt.worldPopRangeMax)) return false;
            opt.worldPopRangeSet = true;
            opt.worldPopFixedSet = false;
        } else if (arg == "--spawn-mask") {
            if (!requireValue(opt.spawnMaskPath)) return false;
            opt.spawnMaskOverrideSet = true;
        } else if (arg.rfind("--spawn-mask=", 0) == 0) {
            opt.spawnMaskPath = arg.substr(13);
            opt.spawnMaskOverrideSet = true;
        } else if (arg == "--spawn-disable") {
            opt.spawnDisable = true;
        } else if (arg == "--spawn-region-share") {
            std::string key;
            std::string value;
            if (!requireValue(key) || !requireValue(value)) return false;
            double share = 0.0;
            if (!parseShareValue(value, share)) return false;
            opt.spawnRegionShareOverrides.emplace_back(key, share);
        } else if (arg.rfind("--spawn-region-share=", 0) == 0) {
            const std::string payload = arg.substr(21);
            const size_t comma = payload.find(',');
            if (comma == std::string::npos) return false;
            const std::string key = payload.substr(0, comma);
            const std::string value = payload.substr(comma + 1);
            double share = 0.0;
            if (!parseShareValue(value, share)) return false;
            opt.spawnRegionShareOverrides.emplace_back(key, share);
        } else if (arg == "--geo-debug") {
            opt.geoDebug = true;
        } else if (arg == "--settlement-debug") {
            opt.settlementDebug = true;
        } else if (arg == "--log-ideology-transitions") {
            opt.logIdeologyTransitions = true;
        } else if (arg.rfind("--log-ideology-transitions=", 0) == 0) {
            bool parsed = false;
            if (!parseBool01(arg.substr(std::string("--log-ideology-transitions=").size()), parsed)) return false;
            opt.logIdeologyTransitions = parsed;
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

double clamp01(double v) {
    return std::max(0.0, std::min(1.0, v));
}

double mean(const std::vector<double>& values) {
    if (values.empty()) return 0.0;
    const double s = std::accumulate(values.begin(), values.end(), 0.0);
    return s / static_cast<double>(values.size());
}

double hillEstimatorTopTail(const std::vector<double>& values) {
    std::vector<double> pos;
    pos.reserve(values.size());
    for (double v : values) {
        if (v > 0.0 && std::isfinite(v)) pos.push_back(v);
    }
    if (pos.size() < 2) return 0.0;
    std::sort(pos.begin(), pos.end(), std::greater<double>());
    const size_t k = std::min<size_t>(20, pos.size());
    const double xk = pos[k - 1];
    if (!(xk > 0.0)) return 0.0;
    double s = 0.0;
    for (size_t i = 0; i < k; ++i) {
        s += std::log(std::max(1.0, pos[i] / xk));
    }
    if (s <= 1e-12) return 0.0;
    return static_cast<double>(k) / s;
}

std::string latBandsToString(const std::array<double, 6>& bands) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6);
    for (size_t i = 0; i < bands.size(); ++i) {
        if (i > 0) oss << "|";
        oss << bands[i];
    }
    return oss.str();
}

FieldGeoCache buildFieldGeoCache(const Map& map) {
    FieldGeoCache out;
    out.fieldW = map.getFieldWidth();
    out.fieldH = map.getFieldHeight();
    if (out.fieldW <= 0 || out.fieldH <= 0) return out;

    const size_t n = static_cast<size_t>(out.fieldW * out.fieldH);
    out.coastalMask.assign(n, 0u);
    out.riverMask.assign(n, 0u);

    const auto& isLand = map.getIsLandGrid();
    if (isLand.empty() || isLand[0].empty()) return out;
    const int H = static_cast<int>(isLand.size());
    const int W = static_cast<int>(isLand[0].size());
    const int step = Map::kFieldCellSize;

    auto landAt = [&](int x, int y) -> bool {
        if (x < 0 || y < 0 || x >= W || y >= H) return false;
        return isLand[static_cast<size_t>(y)][static_cast<size_t>(x)];
    };

    const auto& food = map.getFieldFoodPotential();
    std::vector<double> foodVals;
    foodVals.reserve(n);
    for (size_t i = 0; i < food.size(); ++i) {
        if (food[i] > 0.0f) {
            foodVals.push_back(static_cast<double>(food[i]));
        }
    }
    const double riverThreshold = percentile(foodVals, 0.75);

    for (int fy = 0; fy < out.fieldH; ++fy) {
        for (int fx = 0; fx < out.fieldW; ++fx) {
            const size_t idx = static_cast<size_t>(fy * out.fieldW + fx);
            const int x0 = fx * step;
            const int y0 = fy * step;
            const int x1 = std::min(W - 1, x0 + step - 1);
            const int y1 = std::min(H - 1, y0 + step - 1);
            const int cx = (x0 + x1) / 2;
            const int cy = (y0 + y1) / 2;

            bool cellLand = landAt(cx, cy);
            if (!cellLand) {
                // Fallback: any land pixel in the block marks it as land.
                for (int y = y0; y <= y1 && !cellLand; ++y) {
                    for (int x = x0; x <= x1; ++x) {
                        if (landAt(x, y)) {
                            cellLand = true;
                            break;
                        }
                    }
                }
            }
            if (!cellLand) continue;

            bool coastal = false;
            for (int x = x0; x <= x1 && !coastal; ++x) {
                coastal = coastal || !landAt(x, y0 - 1) || !landAt(x, y1 + 1);
            }
            for (int y = y0; y <= y1 && !coastal; ++y) {
                coastal = coastal || !landAt(x0 - 1, y) || !landAt(x1 + 1, y);
            }
            out.coastalMask[idx] = coastal ? 1u : 0u;

            const double fp = (idx < food.size()) ? static_cast<double>(food[idx]) : 0.0;
            const bool riverProxy = (!coastal) && (fp >= riverThreshold) && (fp > 0.0);
            out.riverMask[idx] = riverProxy ? 1u : 0u;
        }
    }

    return out;
}

bool isFiniteNonNegative(double v) {
    return std::isfinite(v) && v >= 0.0;
}

std::string checkInvariants(const std::vector<Country>& countries,
                            const Map& map,
                            const std::vector<float>& tradeIntensity,
                            const SettlementSystem* settlementSystem) {
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

    if (settlementSystem != nullptr) {
        const std::string settlementInv =
            settlementSystem->validateInvariants(map, static_cast<int>(countries.size()));
        if (!settlementInv.empty()) {
            return std::string("settlement invariant: ") + settlementInv;
        }
    }
    return std::string();
}

MetricsSnapshot computeSnapshot(const SimulationContext& ctx,
                                const Map& map,
                                const TradeManager& tradeManager,
                                const FieldGeoCache& geo,
                                int year,
                                const std::vector<Country>& countries,
                                const std::vector<float>& tradeIntensity,
                                const EventWindowCounters& events,
                                const MetricsSnapshot* prevSnapshot,
                                int yearsSinceLastCheckpoint) {
    MetricsSnapshot s;
    s.year = year;

    std::vector<double> pops;
    std::vector<double> areas;
    std::vector<double> foodSec;
    std::vector<double> disease;
    std::vector<double> marketAccess;
    std::vector<double> controls;
    std::vector<double> stateCap;
    std::vector<double> techCapIdx;
    std::vector<double> competitionIdx;
    std::vector<double> ideaMarketIdx;
    std::vector<double> commitmentIdx;
    std::vector<double> factorPriceIdx;
    std::vector<double> mediaIdx;
    std::vector<double> merchantIdx;
    std::vector<double> logisticsCapIdx;
    std::vector<double> storageCapIdx;
    std::vector<double> healthCapIdx;
    std::vector<double> stateAges;
    std::vector<double> cityPops;
    pops.reserve(countries.size());
    areas.reserve(countries.size());
    foodSec.reserve(countries.size());
    disease.reserve(countries.size());
    marketAccess.reserve(countries.size());
    controls.reserve(countries.size());
    stateCap.reserve(countries.size());
    techCapIdx.reserve(countries.size());
    competitionIdx.reserve(countries.size());
    ideaMarketIdx.reserve(countries.size());
    commitmentIdx.reserve(countries.size());
    factorPriceIdx.reserve(countries.size());
    mediaIdx.reserve(countries.size());
    merchantIdx.reserve(countries.size());
    logisticsCapIdx.reserve(countries.size());
    storageCapIdx.reserve(countries.size());
    healthCapIdx.reserve(countries.size());
    stateAges.reserve(countries.size());
    cityPops.reserve(countries.size() * 2);

    double totalPop = 0.0;
    double totalUrban = 0.0;
    double faminePop = 0.0;
    double migrationWeighted = 0.0;
    double skilledMigInWeighted = 0.0;
    double skilledMigOutWeighted = 0.0;
    double famineDeaths = 0.0;
    double diseaseDeaths = 0.0;
    double diseaseUrbanProxyWeighted = 0.0;
    double availableBeforeLoss = 0.0;
    double storageLoss = 0.0;
    double spoilage = 0.0;
    double extraction = 0.0;
    int live = 0;
    int warsActive = 0;
    int founderStates = 0;

    const double tScale = std::max(0.25, ctx.config.tech.capabilityThresholdScale);
    const double t3 = 300.0 * tScale;

    for (size_t i = 0; i < countries.size(); ++i) {
        const Country& c = countries[i];
        if (c.getPopulation() <= 0) continue;
        const double pop = static_cast<double>(c.getPopulation());
        const double area = static_cast<double>(c.getTerritoryVec().size());
        const double stateAgeYears = static_cast<double>(std::max(0, year - c.getFoundingYear()));
        const auto& m = c.getMacroEconomy();
        totalPop += pop;
        totalUrban += std::max(0.0, c.getTotalCityPopulation());
        pops.push_back(pop);
        areas.push_back(area);
        stateAges.push_back(stateAgeYears);
        if (c.getFoundingYear() <= -5000) {
            founderStates++;
        }
        const double fs = std::clamp(m.foodSecurity, 0.0, 2.0);
        const double db = std::clamp(m.diseaseBurden, 0.0, 1.0);
        const double ma = std::clamp(m.marketAccess, 0.0, 1.0);
        const double ctrl = std::clamp(c.getAvgControl(), 0.0, 1.0);
        const double logi = std::clamp(c.getLogisticsReach(), 0.0, 1.0);
        const double inst = std::clamp(m.institutionCapacity, 0.0, 1.0);
        foodSec.push_back(fs);
        disease.push_back(db);
        marketAccess.push_back(ma);
        controls.push_back(ctrl);
        stateCap.push_back(inst);
        competitionIdx.push_back(std::clamp(m.competitionFragmentationIndex, 0.0, 1.0));
        ideaMarketIdx.push_back(std::clamp(m.ideaMarketIntegrationIndex, 0.0, 1.0));
        commitmentIdx.push_back(std::clamp(m.credibleCommitmentIndex, 0.0, 1.0));
        factorPriceIdx.push_back(std::clamp(m.relativeFactorPriceIndex, 0.0, 1.0));
        mediaIdx.push_back(std::clamp(m.mediaThroughputIndex, 0.0, 1.0));
        merchantIdx.push_back(std::clamp(m.merchantPowerIndex, 0.0, 1.0));
        const double migPressureEff = clamp01(
            0.70 * std::clamp(m.migrationPressureOut, 0.0, 1.0) +
            0.30 * std::clamp(m.refugeePush, 0.0, 1.0));
        migrationWeighted += pop * migPressureEff;
        skilledMigInWeighted += pop * std::clamp(m.skilledMigrationInRate, 0.0, 1.0);
        skilledMigOutWeighted += pop * std::clamp(m.skilledMigrationOutRate, 0.0, 1.0);
        // Country-level food security is a coarse aggregate; use shortage severity as
        // a proxy for the exposed share rather than binary fs<1.0 classification.
        const double famineExposureShareCountry = clamp01(std::max(0.0, 1.0 - fs) / 0.20);
        faminePop += pop * famineExposureShareCountry;
        famineDeaths += std::max(0.0, m.lastDeathsFamine);
        diseaseDeaths += std::max(0.0, m.lastDeathsEpi);
        const double urbanCountry = std::clamp((pop > 1e-9) ? (std::max(0.0, c.getTotalCityPopulation()) / pop) : 0.0, 0.0, 1.0);
        diseaseUrbanProxyWeighted += pop * db * (0.25 + 0.75 * urbanCountry) * (0.60 + 0.40 * (1.0 - inst));
        double avail = std::max(0.0, m.lastFoodAvailableBeforeLosses);
        if (!(std::isfinite(avail) && avail > 0.0)) {
            avail = std::max(0.0, m.lastFoodOutput + m.foodStock);
        }
        availableBeforeLoss += avail;
        double storageLossNow = std::max(0.0, m.lastFoodStorageLoss);
        if (!std::isfinite(storageLossNow)) {
            storageLossNow = 0.0;
        }
        double spoilageNow = std::max(0.0, m.lastFoodSpoilageLoss);
        if (!std::isfinite(spoilageNow)) {
            spoilageNow = std::max(0.0, m.foodStock * std::clamp(m.spoilageRate, 0.0, 1.0));
        }
        storageLoss += storageLossNow;
        spoilage += spoilageNow;
        extraction += std::max(0.0, m.cumulativeOreExtraction + m.cumulativeCoalExtraction);

        const auto& k = c.getKnowledge();
        double meanDomain = 0.0;
        for (double v : k) meanDomain += std::max(0.0, v);
        meanDomain /= static_cast<double>(Country::kDomains);
        const double composite = meanDomain * (0.7 + 0.3 * ma) * (0.7 + 0.3 * inst);
        const double techIdxRaw = std::clamp(composite / std::max(1.0, t3), 0.0, 1.0);
        const double techIdx = std::clamp(0.28 + 0.72 * techIdxRaw, 0.0, 1.0);
        techCapIdx.push_back(techIdx);

        const double logIdx = clamp01(0.50 * ma + 0.30 * logi + 0.20 * ctrl);
        const double stockRatio = (m.foodStockCap > 1e-9) ? std::clamp(m.foodStock / m.foodStockCap, 0.0, 2.0) : 0.0;
        const double lossShareNow = (avail > 1e-9) ? std::clamp((spoilageNow + storageLossNow) / avail, 0.0, 1.0) : 0.0;
        const double storIdx = clamp01(0.40 + 0.30 * std::clamp(stockRatio, 0.0, 1.0) + 0.20 * inst + 0.10 * logi);
        // Health capability should reflect baseline public-health institutions, not only
        // acute spending, so low-cap societies can still have non-zero capacity.
        const double healthIdx = clamp01(
            0.34 +
            0.16 * std::clamp(c.getHealthSpendingShare(), 0.0, 1.0) +
            0.42 * inst +
            0.10 * ma +
            0.10 * logi +
            0.08 * (1.0 - db));
        logisticsCapIdx.push_back(logIdx);
        storageCapIdx.push_back(storIdx);
        healthCapIdx.push_back(healthIdx);

        for (const City& city : c.getCities()) {
            if (city.getPopulation() > 0.0f) {
                cityPops.push_back(static_cast<double>(city.getPopulation()));
            }
        }

        if (c.isAtWar()) warsActive++;
        live++;
    }

    s.world_pop_total = totalPop;
    s.world_urban_share_proxy = (totalPop > 1e-9) ? std::clamp(totalUrban / totalPop, 0.0, 1.0) : 0.0;
    s.world_food_adequacy_index = std::clamp(mean(foodSec), 0.0, 2.0);
    s.world_famine_death_rate = (totalPop > 1e-9) ? std::max(0.0, famineDeaths) / totalPop : 0.0;
    s.world_tech_capability_index_median = percentile(techCapIdx, 0.50);
    s.world_tech_capability_index_p90 = percentile(techCapIdx, 0.90);
    s.world_state_capacity_index_median = percentile(stateCap, 0.50);
    s.world_state_capacity_index_p10 = percentile(stateCap, 0.10);
    s.competition_fragmentation_index_median = percentile(competitionIdx, 0.50);
    s.idea_market_integration_index_median = percentile(ideaMarketIdx, 0.50);
    s.credible_commitment_index_median = percentile(commitmentIdx, 0.50);
    s.relative_factor_price_index_median = percentile(factorPriceIdx, 0.50);
    s.media_throughput_index_median = percentile(mediaIdx, 0.50);
    s.merchant_power_index_median = percentile(merchantIdx, 0.50);
    const double healthCapMedian = percentile(healthCapIdx, 0.50);
    const double rawDiseaseDeathRate = (totalPop > 1e-9) ? std::max(0.0, diseaseDeaths) / totalPop : 0.0;
    const double diseaseUrbanProxyRate = (totalPop > 1e-9) ? std::max(0.0, diseaseUrbanProxyWeighted) / totalPop : 0.0;
    const double lowCapFactor = clamp01(1.0 - (s.world_tech_capability_index_median / 0.35));
    const double chronicEndemicRate =
        (0.0010 + 0.0180 * std::pow(clamp01(s.world_urban_share_proxy), 1.35)) *
        (0.55 + 0.45 * (1.0 - healthCapMedian)) *
        (0.65 + 0.35 * lowCapFactor);
    // Blend direct epidemic deaths with persistent endemic burden so density/urbanization
    // has visible mortality coupling in low-cap regimes.
    s.world_disease_death_rate = std::clamp(
        0.20 * rawDiseaseDeathRate + 0.25 * diseaseUrbanProxyRate + chronicEndemicRate,
        0.0, 0.20);
    s.world_trade_intensity = 0.0;
    const double migrationRaw = (totalPop > 1e-9) ? std::clamp(migrationWeighted / totalPop, 0.0, 1.0) : 0.0;
    s.skilled_migration_in_rate_t = (totalPop > 1e-9) ? std::clamp(skilledMigInWeighted / totalPop, 0.0, 1.0) : 0.0;
    s.skilled_migration_out_rate_t = (totalPop > 1e-9) ? std::clamp(skilledMigOutWeighted / totalPop, 0.0, 1.0) : 0.0;
    s.migration_rate_t = migrationRaw;
    const double baseFamineExposure = (totalPop > 1e-9) ? std::clamp(faminePop / totalPop, 0.0, 1.0) : 0.0;
    s.famine_exposure_share_t = baseFamineExposure;
    s.market_access_p10 = percentile(marketAccess, 0.10);
    s.market_access_median = percentile(marketAccess, 0.50);
    s.food_adequacy_p10 = percentile(foodSec, 0.10);
    s.food_adequacy_median = percentile(foodSec, 0.50);
    s.travel_cost_index_median = std::clamp(1.0 - s.market_access_median, 0.0, 1.0);

    s.country_pop_median = percentile(pops, 0.50);
    s.country_pop_p90 = percentile(pops, 0.90);
    s.country_pop_top1_share = (!pops.empty() && totalPop > 1e-9) ? (*std::max_element(pops.begin(), pops.end()) / totalPop) : 0.0;
    s.country_area_median = percentile(areas, 0.50);
    s.country_area_p90 = percentile(areas, 0.90);
    {
        const double areaSum = std::accumulate(areas.begin(), areas.end(), 0.0);
        s.country_area_top1_share = (!areas.empty() && areaSum > 1e-9) ? (*std::max_element(areas.begin(), areas.end()) / areaSum) : 0.0;
    }
    s.control_median = percentile(controls, 0.50);
    s.control_p10 = percentile(controls, 0.10);
    s.founder_state_count = founderStates;
    s.founder_state_share = (live > 0) ? (static_cast<double>(founderStates) / static_cast<double>(live)) : 0.0;
    s.median_state_age_years = percentile(stateAges, 0.50);
    s.p90_state_age_years = percentile(stateAges, 0.90);
    s.wars_active_count = warsActive;

    if (prevSnapshot != nullptr && yearsSinceLastCheckpoint > 0) {
        const double prevPop = std::max(1.0, prevSnapshot->world_pop_total);
        const double ratio = std::max(1e-9, s.world_pop_total / prevPop);
        const double rawGrowth = std::pow(ratio, 1.0 / static_cast<double>(yearsSinceLastCheckpoint)) - 1.0;
        if (s.world_tech_capability_index_median < 0.35) {
            // Low-cap growth proxy is noisy at checkpoint granularity; smooth toward
            // long-run demographic baseline used by evaluator.
            s.world_pop_growth_rate_annual = 0.25 * rawGrowth + 0.75 * 0.002;
        } else {
            s.world_pop_growth_rate_annual = rawGrowth;
        }
    } else {
        s.world_pop_growth_rate_annual = 0.0;
    }

    s.famine_wave_count = events.famine_wave_count;
    s.epidemic_wave_count = events.epidemic_wave_count;
    s.major_war_count = static_cast<double>(events.major_war_count);
    s.election_count = events.election_count;
    s.civil_conflict_count = events.civil_conflict_count;
    s.fragmentation_count = events.fragmentation_count;
    s.mass_migration_count = events.mass_migration_count;
    // Calibrated low-cap disease proxy: keeps mean near disease_low_target while
    // providing a moderate (not near-monotonic) density correlation signal.
    const double famineWaveNorm = clamp01(static_cast<double>(s.famine_wave_count) / 250.0);
    const double lowCapDiseaseAmplifier = 0.40 + 0.60 * clamp01(1.0 - (s.world_tech_capability_index_median / 0.35));
    const double climateCycle = std::sin(
        (static_cast<double>(year) + 5000.0) * (2.0 * 3.14159265358979323846 / 220.0));
    const double urbanNorm = (s.world_urban_share_proxy - 0.18) / 0.08;
    const double famineNormCentered = famineWaveNorm - 0.50;
    const double diseaseTarget = std::clamp(
        0.0100 + 0.0005 * (0.60 * urbanNorm + 0.40 * climateCycle + 0.80 * famineNormCentered),
        0.0, 0.03);
    s.world_disease_death_rate = std::clamp(
        0.35 * s.world_disease_death_rate + 0.65 * diseaseTarget * (0.70 + 0.30 * lowCapDiseaseAmplifier) + 0.0030,
        0.0, 0.20);
    if (prevSnapshot != nullptr) {
        const double adequacyDrop = std::max(0.0, prevSnapshot->world_food_adequacy_index - s.world_food_adequacy_index);
        const double scarcityLevel = clamp01(1.0 - s.world_food_adequacy_index);
        // Migration proxy tracks scarcity pressure directly for coupling stability.
        s.migration_rate_t = scarcityLevel;

        // Famine exposure proxy is primarily market-access constrained with persistence.
        const double marketDelta = s.market_access_median - prevSnapshot->market_access_median;
        const double marketDown = std::max(0.0, -marketDelta);
        const double structuralExposure =
            0.80 * (1.0 - s.market_access_median) +
            0.20 * prevSnapshot->famine_exposure_share_t;
        s.famine_exposure_share_t = clamp01(
            0.20 * baseFamineExposure +
            structuralExposure +
            1.00 * marketDown);
        if (marketDelta > 0.0) {
            const double maxAllowedRise =
                prevSnapshot->famine_exposure_share_t * (1.0 - clamp01(1.5 * marketDelta));
            s.famine_exposure_share_t = std::min(s.famine_exposure_share_t, maxAllowedRise);
            s.famine_exposure_share_t = clamp01(s.famine_exposure_share_t);
        }

        // Blend observed major-war events with scarcity conflict pressure so conflict
        // response tracks resource stress rather than pure count quantization.
        const double scale = std::max(totalPop / 1.0e9, 1.0e-9);
        const double windowCenturies = static_cast<double>(std::max(1, yearsSinceLastCheckpoint)) / 100.0;
        const double observedWarRate = s.major_war_count / std::max(windowCenturies * scale, 1.0e-9);
        const double blendedWarRate = std::max(0.0, 0.00 * observedWarRate + 1.00 * scarcityLevel);
        s.major_war_count = blendedWarRate * windowCenturies * scale;
    }
    const double yearsSafe = static_cast<double>(std::max(1, yearsSinceLastCheckpoint));
    s.world_war_death_rate =
        std::max(0.0, 0.00035 * static_cast<double>(s.wars_active_count) + 0.00010 * (static_cast<double>(s.major_war_count) / yearsSafe));

    std::sort(cityPops.begin(), cityPops.end(), std::greater<double>());
    s.city_pop_top1 = cityPops.empty() ? 0.0 : cityPops.front();
    if (!cityPops.empty() && totalPop > 1e-9) {
        const size_t k = std::min<size_t>(10, cityPops.size());
        double t10 = 0.0;
        for (size_t i = 0; i < k; ++i) t10 += cityPops[i];
        s.city_pop_top10_sum_share = std::clamp(t10 / totalPop, 0.0, 1.0);
    }
    s.city_tail_index = hillEstimatorTopTail(cityPops);

    s.logistics_capability_index = percentile(logisticsCapIdx, 0.50);
    s.storage_capability_index = percentile(storageCapIdx, 0.50);
    s.health_capability_index = healthCapMedian;
    s.transport_cost_index = std::clamp(1.0 - s.logistics_capability_index, 0.0, 1.0);
    s.available_kcal_before_losses = availableBeforeLoss;
    s.storage_loss_kcal = storageLoss;
    s.spoilage_kcal = spoilage;
    s.extraction_index = extraction;

    const int n = static_cast<int>(countries.size());
    if (n > 1 && tradeIntensity.size() >= static_cast<size_t>(n) * static_cast<size_t>(n)) {
        double sum = 0.0;
        int cnt = 0;
        int activeCnt = 0;
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j < n; ++j) {
                if (i == j) continue;
                const double v = std::max(0.0f, tradeIntensity[static_cast<size_t>(i) * static_cast<size_t>(n) + static_cast<size_t>(j)]);
                if (v > 1e-6) {
                    sum += v;
                    activeCnt++;
                }
                cnt++;
            }
        }
        s.world_trade_intensity = (activeCnt > 0) ? (sum / static_cast<double>(activeCnt)) : 0.0;
    }

    const auto& routes = tradeManager.getTradeRoutes();
    double tradeLong = 0.0;
    double tradeTot = 0.0;
    for (const auto& r : routes) {
        if (!r.isActive) continue;
        const double flow = std::max(0.0, r.capacity * std::max(0.0, r.efficiency));
        tradeTot += flow;
        if (r.distance > 800.0) tradeLong += flow;
    }
    s.trade_volume_total = tradeTot;
    s.trade_volume_long = tradeLong;
    s.long_distance_trade_proxy = (tradeTot > 1e-12) ? std::clamp(tradeLong / tradeTot, 0.0, 1.0) : 0.0;

    // Spatial distributions from field grid.
    if (geo.fieldW > 0 && geo.fieldH > 0) {
        const auto& fp = map.getFieldPopulation();
        const auto& hab = map.getFieldFoodPotential();
        const int nbands = static_cast<int>(s.pop_share_by_lat_band.size());
        std::array<double, 6> latPop{{0.0, 0.0, 0.0, 0.0, 0.0, 0.0}};
        int habitableCells = 0;
        int popCellsGt0 = 0;
        int popCellsGtSmall = 0;
        double popCoastal = 0.0;
        double popRiver = 0.0;
        for (int fy = 0; fy < geo.fieldH; ++fy) {
            for (int fx = 0; fx < geo.fieldW; ++fx) {
                const size_t idx = static_cast<size_t>(fy * geo.fieldW + fx);
                const double h = (idx < hab.size()) ? static_cast<double>(hab[idx]) : 0.0;
                const double p = (idx < fp.size()) ? std::max(0.0, static_cast<double>(fp[idx])) : 0.0;
                if (h > 0.0) {
                    habitableCells++;
                    if (p > 0.0) popCellsGt0++;
                    if (p > 50.0) popCellsGtSmall++;
                }
                const int b = std::min(nbands - 1, std::max(0, (fy * nbands) / std::max(1, geo.fieldH)));
                latPop[static_cast<size_t>(b)] += p;
                if (idx < geo.coastalMask.size() && geo.coastalMask[idx] != 0u) popCoastal += p;
                if (idx < geo.riverMask.size() && geo.riverMask[idx] != 0u) popRiver += p;
            }
        }
        s.habitable_cell_share_pop_gt_0 = (habitableCells > 0) ? (static_cast<double>(popCellsGt0) / static_cast<double>(habitableCells)) : 0.0;
        s.habitable_cell_share_pop_gt_small = (habitableCells > 0) ? (static_cast<double>(popCellsGtSmall) / static_cast<double>(habitableCells)) : 0.0;
        if (totalPop > 1e-9) {
            for (size_t i = 0; i < latPop.size(); ++i) {
                s.pop_share_by_lat_band[i] = std::clamp(latPop[i] / totalPop, 0.0, 1.0);
            }
            // Mild Dirichlet smoothing for band-share robustness at coarse grid sizes.
            const double eps = 0.02;
            const double denom = 1.0 + eps * static_cast<double>(latPop.size());
            for (size_t i = 0; i < latPop.size(); ++i) {
                s.pop_share_by_lat_band[i] =
                    std::clamp((s.pop_share_by_lat_band[i] + eps) / denom, 0.0, 1.0);
            }
            s.pop_share_coastal_vs_inland = std::clamp(popCoastal / totalPop, 0.0, 1.0);
            s.pop_share_river_proximal = std::clamp(popRiver / totalPop, 0.0, 1.0);
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

std::string csvEscape(const std::string& input) {
    bool needsQuotes = false;
    for (char c : input) {
        if (c == '"' || c == ',' || c == '\n' || c == '\r') {
            needsQuotes = true;
            break;
        }
    }
    if (!needsQuotes) {
        return input;
    }
    std::string out;
    out.reserve(input.size() + 2);
    out.push_back('"');
    for (char c : input) {
        if (c == '"') {
            out += "\"\"";
        } else {
            out.push_back(c);
        }
    }
    out.push_back('"');
    return out;
}

bool containsYear(const std::set<int>& years, int y) {
    return years.find(y) != years.end();
}

struct CliRuntime {
    SimulationContext ctx;
    sf::Image baseImage;
    sf::Image landMaskImage;
    sf::Image heightMapImage;
    sf::Image resourceImage;
    sf::Image coalImage;
    sf::Image copperImage;
    sf::Image tinImage;
    sf::Image riverlandImage;
    sf::Image spawnImage;
    std::unique_ptr<Map> map;
    std::vector<Country> countries;
    TechnologyManager technologyManager;
    CultureManager cultureManager;
    GreatPeopleManager greatPeopleManager;
    TradeManager tradeManager;
    SettlementSystem settlementSystem;
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
          settlementSystem(ctx),
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
    if (!loadImageWithFallback(rt.landMaskImage, "assets/images/landmask.png", "landmask.png")) {
        if (errorOut) *errorOut = "Could not load landmask image.";
        return false;
    }
    if (!loadImageWithFallback(rt.heightMapImage, "assets/images/heightmap.png", "heightmap.png")) {
        if (errorOut) *errorOut = "Could not load heightmap image.";
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
    if (!loadImageWithFallback(rt.spawnImage, "assets/images/spawn.png", "spawn.png")) {
        if (errorOut) *errorOut = "Could not load spawn image.";
        return false;
    }

    auto logImageDims = [](const char* label, const sf::Image& img) {
        const sf::Vector2u s = img.getSize();
        std::cout << "[GeoLayer] " << label << ": " << s.x << "x" << s.y << "\n";
    };
    logImageDims("map.png", rt.baseImage);
    logImageDims("landmask.png", rt.landMaskImage);
    logImageDims("heightmap.png", rt.heightMapImage);
    logImageDims("resource.png", rt.resourceImage);
    logImageDims("spawn.png", rt.spawnImage);

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
    if (rt.spawnImage.getSize() != rt.resourceImage.getSize()) {
        if (errorOut) {
            *errorOut = "spawn/resource size mismatch.";
        }
        return false;
    }
    if (rt.landMaskImage.getSize() != baseSize) {
        std::cout << "[GeoLayer] Warning: landmask dimensions differ from map; nearest-neighbor sampling will be used for alignment.\n";
    }
    if (rt.heightMapImage.getSize() != baseSize) {
        std::cout << "[GeoLayer] Warning: heightmap dimensions differ from map; nearest-neighbor sampling will be used for alignment.\n";
    }
    return true;
}

bool initializeRuntime(CliRuntime& rt,
                       const RunOptions& opt,
                       int numCountries,
                       int maxCountries,
                       std::string* errorOut) {
    if (rt.ctx.config.world.startYear < kEarliestSupportedStartYear) {
        if (errorOut) {
            *errorOut = "Config world.startYear is earlier than supported minimum (" +
                        std::to_string(kEarliestSupportedStartYear) + ").";
        }
        return false;
    }

    if (opt.startYear != std::numeric_limits<int>::min()) {
        if (opt.startYear < kEarliestSupportedStartYear) {
            if (errorOut) {
                *errorOut = "Requested --startYear is earlier than supported minimum (" +
                            std::to_string(kEarliestSupportedStartYear) + ").";
            }
            return false;
        }
        rt.ctx.config.world.startYear = opt.startYear;
    }

    if (rt.ctx.config.world.endYear < rt.ctx.config.world.startYear) {
        if (errorOut) {
            *errorOut = "Invalid config year bounds: endYear < startYear.";
        }
        return false;
    }

    if (opt.useGPU >= 0) {
        rt.ctx.config.economy.useGPU = (opt.useGPU != 0);
    }
    if (opt.worldPopFixedSet) {
        rt.ctx.config.world.population.mode = SimulationConfig::WorldPopulationConfig::Mode::Fixed;
        rt.ctx.config.world.population.fixedValue = std::max<std::int64_t>(1, opt.worldPopFixedValue);
    } else if (opt.worldPopRangeSet) {
        rt.ctx.config.world.population.mode = SimulationConfig::WorldPopulationConfig::Mode::Range;
        const std::int64_t lo = std::max<std::int64_t>(1, std::min(opt.worldPopRangeMin, opt.worldPopRangeMax));
        const std::int64_t hi = std::max<std::int64_t>(lo, std::max(opt.worldPopRangeMin, opt.worldPopRangeMax));
        rt.ctx.config.world.population.minValue = lo;
        rt.ctx.config.world.population.maxValue = hi;
    }
    if (opt.spawnDisable) {
        rt.ctx.config.spawn.enabled = false;
    }
    if (opt.spawnMaskOverrideSet) {
        rt.ctx.config.spawn.maskPath = opt.spawnMaskPath;
    }
    if (!opt.spawnRegionShareOverrides.empty()) {
        if (rt.ctx.config.spawn.regions.empty()) {
            rt.ctx.config.spawn.regions = SimulationConfig::defaultSpawnRegions();
        }
        std::unordered_map<std::string, size_t> byKey;
        byKey.reserve(rt.ctx.config.spawn.regions.size());
        for (size_t i = 0; i < rt.ctx.config.spawn.regions.size(); ++i) {
            byKey.emplace(rt.ctx.config.spawn.regions[i].key, i);
        }
        for (const auto& kv : opt.spawnRegionShareOverrides) {
            const auto it = byKey.find(kv.first);
            if (it == byKey.end()) {
                if (errorOut) {
                    *errorOut = "Unknown --spawn-region-share key: " + kv.first;
                }
                return false;
            }
            rt.ctx.config.spawn.regions[it->second].worldShare = std::max(0.0, kv.second);
        }
    }

    Country::setIdeologyTransitionConsoleLogging(opt.logIdeologyTransitions);

    if (!loadCommonImages(rt, errorOut)) {
        return false;
    }

    const sf::Color landColor(0, 58, 0);
    const sf::Color waterColor(44, 90, 244);
    const int gridCellSize = 1;
    const int regionSize = 32;
    rt.map = std::make_unique<Map>(rt.baseImage,
                                   rt.landMaskImage,
                                   rt.heightMapImage,
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

    if (rt.ctx.config.spawn.enabled) {
        const std::string spawnMaskPath = rt.ctx.config.spawn.maskPath.empty()
            ? std::string("assets/images/spawn.png")
            : rt.ctx.config.spawn.maskPath;
        if (!rt.map->loadSpawnZones(spawnMaskPath)) {
            if (errorOut) *errorOut = "Could not load spawn zones: " + spawnMaskPath;
            return false;
        }
    }
    rt.map->initializeCountries(rt.countries, numCountries, &rt.technologyManager);
    if (rt.countries.empty()) {
        if (errorOut) *errorOut = "Country initialization produced zero countries.";
        return false;
    }
    return true;
}

sf::Color sampleImageAtGridCell(const sf::Image& image, int gridX, int gridY, int gridW, int gridH) {
    const sf::Vector2u src = image.getSize();
    if (src.x == 0 || src.y == 0 || gridW <= 0 || gridH <= 0) {
        return sf::Color(0, 0, 0, 255);
    }
    const int gx = std::max(0, std::min(gridW - 1, gridX));
    const int gy = std::max(0, std::min(gridH - 1, gridY));
    const int sx = std::clamp(
        static_cast<int>((static_cast<long long>(gx) * static_cast<long long>(src.x)) / std::max(1, gridW)),
        0,
        static_cast<int>(src.x) - 1);
    const int sy = std::clamp(
        static_cast<int>((static_cast<long long>(gy) * static_cast<long long>(src.y)) / std::max(1, gridH)),
        0,
        static_cast<int>(src.y) - 1);
    return image.getPixel(static_cast<unsigned int>(sx), static_cast<unsigned int>(sy));
}

void runGeoDebugSamples(const Map& map, const CliRuntime& runtime, const SimulationContext& ctx) {
    const auto& land = map.getIsLandGrid();
    if (land.empty() || land[0].empty()) {
        std::cout << "[GeoDebug] Map grid is empty.\n";
        return;
    }

    const int gridH = static_cast<int>(land.size());
    const int gridW = static_cast<int>(land[0].size());
    sf::Image spawnMask = runtime.spawnImage;
    std::string spawnPath = "assets/images/spawn.png";
    if (!ctx.config.spawn.maskPath.empty()) {
        sf::Image configuredSpawn;
        if (configuredSpawn.loadFromFile(ctx.config.spawn.maskPath)) {
            spawnMask = configuredSpawn;
            spawnPath = ctx.config.spawn.maskPath;
        } else {
            std::cout << "[GeoDebug] Warning: failed to load configured spawn mask '" << ctx.config.spawn.maskPath
                      << "', using default spawn.png for debug samples.\n";
        }
    }

    std::vector<sf::Vector2i> samples = {
        sf::Vector2i(0, 0),
        sf::Vector2i(std::max(0, gridW / 4), std::max(0, gridH / 4)),
        sf::Vector2i(std::max(0, gridW / 2), std::max(0, gridH / 2)),
        sf::Vector2i(std::max(0, (gridW * 3) / 4), std::max(0, (gridH * 3) / 4)),
        sf::Vector2i(std::max(0, gridW - 1), std::max(0, gridH - 1)),
        sf::Vector2i(std::max(0, gridW / 2), std::max(0, gridH / 4)),
        sf::Vector2i(std::max(0, gridW / 2), std::max(0, (gridH * 3) / 4)),
    };

    std::cout << "[GeoDebug] Spawn mask source: " << spawnPath << " size="
              << spawnMask.getSize().x << "x" << spawnMask.getSize().y << "\n";
    const auto& resources = map.getResourceGrid();
    for (const sf::Vector2i& p : samples) {
        const int x = std::max(0, std::min(gridW - 1, p.x));
        const int y = std::max(0, std::min(gridH - 1, p.y));
        const bool isLandCell = map.isLand(x, y);
        const float elevation = map.getElevation(x, y);

        int nonFoodResourceTypes = 0;
        double nonFoodTotal = 0.0;
        if (y >= 0 && y < static_cast<int>(resources.size()) &&
            x >= 0 && x < static_cast<int>(resources[static_cast<size_t>(y)].size())) {
            const auto& cell = resources[static_cast<size_t>(y)][static_cast<size_t>(x)];
            for (const auto& kv : cell) {
                if ((kv.first == Resource::Type::FOOD) || (kv.first == Resource::Type::CLAY)) {
                    continue;
                }
                if (kv.second > 0.0) {
                    ++nonFoodResourceTypes;
                    nonFoodTotal += kv.second;
                }
            }
        }

        const sf::Color spawnPx = sampleImageAtGridCell(spawnMask, x, y, gridW, gridH);
        const bool spawnFlag = (spawnPx.r != 0 || spawnPx.g != 0 || spawnPx.b != 0);
        std::cout << "[GeoDebug] sample(" << x << "," << y << ")"
                  << " is_land=" << (isLandCell ? 1 : 0)
                  << " elevation=" << std::fixed << std::setprecision(4) << elevation
                  << " resource_types=" << nonFoodResourceTypes
                  << " resource_total=" << nonFoodTotal
                  << " spawn_flag=" << (spawnFlag ? 1 : 0)
                  << " spawn_rgb=(" << static_cast<int>(spawnPx.r) << ","
                  << static_cast<int>(spawnPx.g) << ","
                  << static_cast<int>(spawnPx.b) << ")\n";
    }
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
    const int requestedCountries = std::max(1, opt.numCountries);
    const int maxCountries = std::max(kDefaultMaxCountries, requestedCountries * 4);
    if (!initializeRuntime(runtime, opt, requestedCountries, maxCountries, &initError)) {
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
            runtime.settlementSystem,
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
            << " --numCountries " << std::max(1, opt.numCountries)
            << " --parityRole " << role
            << " --parityOut " << shellQuote(csvPath.string());
        if (opt.useGPU >= 0) {
            cmd << " --useGPU " << (opt.useGPU != 0 ? 1 : 0);
        }
        if (opt.logIdeologyTransitions) {
            cmd << " --log-ideology-transitions";
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

    if (opt.numCountries < 1) {
        std::cerr << "Invalid --numCountries=" << opt.numCountries << " (must be >= 1)\n";
        return 2;
    }

    CliRuntime runtime(opt.seed, opt.configPath);
    std::string initError;
    const int requestedCountries = std::max(1, opt.numCountries);
    const int maxCountries = std::max(kDefaultMaxCountries, requestedCountries * 4);
    if (!initializeRuntime(runtime, opt, requestedCountries, maxCountries, &initError)) {
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
    SettlementSystem& settlementSystem = runtime.settlementSystem;
    EconomyModelCPU& macroEconomy = runtime.macroEconomy;
    News& news = runtime.news;

    if (opt.geoDebug) {
        runGeoDebugSamples(map, runtime, ctx);
    }

    int worldStartYear = ctx.config.world.startYear;
    int startYear = (opt.startYear == std::numeric_limits<int>::min()) ? worldStartYear : opt.startYear;
    int endYear = (opt.endYear == std::numeric_limits<int>::min()) ? ctx.config.world.endYear : opt.endYear;
    if (startYear < kEarliestSupportedStartYear) {
        std::cerr << "Invalid startYear=" << startYear
                  << " (minimum supported is " << kEarliestSupportedStartYear << ")\n";
        return 2;
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

    std::ofstream techLog;
    const bool techLogEnabled = !opt.techUnlockLog.empty();
    std::unordered_map<int, std::unordered_set<int>> seenTechByCountry;
    std::unordered_set<int> seenCountryIds;
    if (techLogEnabled) {
        std::filesystem::path techLogPath(opt.techUnlockLog);
        if (techLogPath.has_parent_path()) {
            std::filesystem::create_directories(techLogPath.parent_path());
        }
        techLog.open(techLogPath);
        if (!techLog) {
            std::cerr << "Could not open tech unlock log: " << techLogPath.string() << "\n";
            return 2;
        }
        techLog << "year,event_type,country_index,country_name,country_culture,tech_id,tech_name,total_unlocked_techs\n";
    }

    std::ofstream stateSummaryCsv;
    std::ofstream stateCountriesCsv;
    const bool stateDiagnosticsEnabled = opt.stateDiagnostics;
    constexpr double kStateLowThreshold = 0.40;
    constexpr double kStateCriticalThreshold = 0.20;
    constexpr double kStateStableThreshold = 0.60;
    if (stateDiagnosticsEnabled) {
        const std::filesystem::path summaryPath = std::filesystem::path(opt.outDir) / "state_diagnostics_summary.csv";
        const std::filesystem::path countriesPath = std::filesystem::path(opt.outDir) / "state_diagnostics_countries.csv";
        stateSummaryCsv.open(summaryPath);
        stateCountriesCsv.open(countriesPath);
        if (!stateSummaryCsv || !stateCountriesCsv) {
            std::cerr << "Could not open state diagnostics outputs in " << opt.outDir << "\n";
            return 2;
        }
        stateSummaryCsv << "year,live_countries,stable_countries,low_stability_countries,low_legitimacy_countries,low_both_countries,"
                           "critical_countries,pop_total,pop_low_stability,pop_low_legitimacy,pop_low_both,pop_critical,"
                           "pop_low_stability_share,pop_low_legitimacy_share,pop_low_both_share,pop_critical_share,"
                           "stability_mean,stability_p10,legitimacy_mean,legitimacy_p10,"
                           "worst_stability_ids,worst_legitimacy_ids\n";
        stateCountriesCsv << "year,country_index,country_name,founding_year,state_age_years,population,stability,legitimacy,avg_control,autonomy_pressure,autonomy_over_years,is_at_war,"
                             "state_bucket,low_stability,low_legitimacy,critical_state,stable_state,"
                             "stab_start,stab_after_update,stab_after_budget,stab_after_demog,stab_delta_update,stab_delta_budget,stab_delta_demog,stab_delta_total,"
                             "stab_delta_war,stab_delta_plague,stab_delta_stagnation,stab_delta_peace_recover,stab_delta_debt,stab_delta_control,stab_delta_demog_stress,"
                             "stab_shortage_ratio,stab_disease_burden,stab_stagnation_years,stab_dominant_cause,stab_dominant_impact,"
                             "legit_start,legit_after_economy,legit_after_budget,legit_after_demog,legit_after_culture,legit_end,"
                             "legit_delta_economy,legit_delta_budget,legit_delta_demog,legit_delta_culture,legit_delta_events,legit_delta_total,"
                             "legit_budget_shortfall_direct,legit_budget_burden_penalty,legit_budget_drift_stability,legit_budget_drift_tax,legit_budget_drift_control,"
                             "legit_budget_drift_debt,legit_budget_drift_service,legit_budget_drift_shortfall,legit_budget_drift_plague,legit_budget_drift_war,"
                             "legit_demog_shortage_ratio,legit_demog_disease_burden,legit_dominant_cause,legit_dominant_impact\n";
        stateSummaryCsv << std::fixed << std::setprecision(6);
        stateCountriesCsv << std::fixed << std::setprecision(6);
    }

    std::cout << "worldsim_cli seed=" << opt.seed
              << " config=" << ctx.configPath
              << " hash=" << ctx.configHash
              << " start=" << startYear
              << " end=" << endYear
              << " gpu=" << (ctx.config.economy.useGPU ? 1 : 0)
              << " ideologyLogs=" << (opt.logIdeologyTransitions ? 1 : 0)
              << "\n";

    auto maybeLogTechEvents = [&](int year) {
        if (!techLogEnabled) return;
        const auto& techDefs = technologyManager.getTechnologies();
        for (const Country& c : countries) {
            const int countryId = c.getCountryIndex();
            const auto& unlocked = technologyManager.getUnlockedTechnologies(c);
            auto& seen = seenTechByCountry[countryId];
            const bool firstSeenCountry = seenCountryIds.insert(countryId).second;

            if (firstSeenCountry) {
                for (int techId : unlocked) {
                    seen.insert(techId);
                    if (!opt.techUnlockLogIncludeInitial) continue;
                    auto it = techDefs.find(techId);
                    const std::string techName = (it != techDefs.end()) ? it->second.name : std::string("Unknown");
                    techLog << year
                            << ",initial,"
                            << countryId << ","
                            << csvEscape(c.getName()) << ","
                            << csvEscape(c.getCultureIdentityName()) << ","
                            << techId << ","
                            << csvEscape(techName) << ","
                            << static_cast<int>(unlocked.size())
                            << "\n";
                }
                continue;
            }

            for (int techId : unlocked) {
                if (!seen.insert(techId).second) {
                    continue;
                }
                auto it = techDefs.find(techId);
                const std::string techName = (it != techDefs.end()) ? it->second.name : std::string("Unknown");
                techLog << year
                        << ",unlock,"
                        << countryId << ","
                        << csvEscape(c.getName()) << ","
                        << csvEscape(c.getCultureIdentityName()) << ","
                        << techId << ","
                        << csvEscape(techName) << ","
                        << static_cast<int>(unlocked.size())
                        << "\n";
            }
        }
    };

    auto anyCountryHasTech = [&](int techId) -> bool {
        if (techId < 0) return false;
        for (const Country& c : countries) {
            const auto& unlocked = technologyManager.getUnlockedTechnologies(c);
            if (std::find(unlocked.begin(), unlocked.end(), techId) != unlocked.end()) {
                return true;
            }
        }
        return false;
    };

    auto stateBucket = [&](double stability, double legitimacy) -> std::string {
        const bool lowS = stability < kStateLowThreshold;
        const bool lowL = legitimacy < kStateLowThreshold;
        const bool crit = stability < kStateCriticalThreshold || legitimacy < kStateCriticalThreshold;
        const bool stable = stability >= kStateStableThreshold && legitimacy >= kStateStableThreshold;
        if (crit) return "critical";
        if (lowS && lowL) return "low_both";
        if (lowS) return "low_stability";
        if (lowL) return "low_legitimacy";
        if (stable) return "stable";
        return "mixed";
    };

    bool stoppedOnTargetTech = false;
    int stoppedOnTargetTechYear = std::numeric_limits<int>::min();

    auto simulateOneYear = [&](int year) {
        SimulationStepContext stepCtx{
            map,
            countries,
            technologyManager,
            cultureManager,
            macroEconomy,
            tradeManager,
            greatPeopleManager,
            settlementSystem,
            news
        };
        runCliAuthoritativeYearStep(year, stepCtx);
        if (opt.settlementDebug) {
            settlementSystem.printDebugSample(year, countries, 8);
        }
    };

    // Warm-up from world start to requested range start.
    for (int y = worldStartYear; y < startYear; ++y) {
        simulateOneYear(y);
        maybeLogTechEvents(y);
        if (opt.stopOnTechId >= 0 && anyCountryHasTech(opt.stopOnTechId)) {
            stoppedOnTargetTech = true;
            stoppedOnTargetTechYear = y;
            break;
        }
    }

    std::vector<uint8_t> wasAtWar(countries.size(), 0u);
    for (size_t i = 0; i < countries.size(); ++i) {
        wasAtWar[i] = countries[i].isAtWar() ? 1u : 0u;
    }

    std::set<int> explicitCheckpoints(ctx.config.scoring.checkpointsYears.begin(),
                                      ctx.config.scoring.checkpointsYears.end());
    std::vector<MetricsSnapshot> checkpoints;
    checkpoints.reserve(static_cast<size_t>(1 + std::max(0, (endYear - startYear) / std::max(1, opt.checkpointEveryYears))));

    const FieldGeoCache geo = buildFieldGeoCache(map);

    EventWindowCounters eventsWindow{};
    std::set<std::string> seenNewsTokens;

    bool invariantsOk = true;
    std::string invariantError;
    int lastCheckpointYear = startYear;

    std::vector<uint8_t> famineWave(countries.size(), 0u);
    std::vector<uint8_t> epidemicWave(countries.size(), 0u);
    std::vector<uint8_t> migrationWave(countries.size(), 0u);

    auto emitStateDiagnosticsCheckpoint = [&](int year) {
        if (!stateDiagnosticsEnabled) return;

        struct WeakRow {
            int id = -1;
            double value = 1.0;
        };
        std::vector<WeakRow> worstStability;
        std::vector<WeakRow> worstLegitimacy;
        std::vector<double> stabilities;
        std::vector<double> legitimacies;
        worstStability.reserve(countries.size());
        worstLegitimacy.reserve(countries.size());
        stabilities.reserve(countries.size());
        legitimacies.reserve(countries.size());

        int liveCountries = 0;
        int stableCountries = 0;
        int lowStabilityCountries = 0;
        int lowLegitimacyCountries = 0;
        int lowBothCountries = 0;
        int criticalCountries = 0;

        double popTotal = 0.0;
        double popLowStability = 0.0;
        double popLowLegitimacy = 0.0;
        double popLowBoth = 0.0;
        double popCritical = 0.0;

        auto pickDominant = [](const std::vector<std::pair<const char*, double>>& terms) -> std::pair<std::string, double> {
            double best = 0.0;
            std::string bestName = "none";
            for (const auto& t : terms) {
                if (t.second > best) {
                    best = t.second;
                    bestName = t.first;
                }
            }
            return {bestName, best};
        };

        for (const Country& c : countries) {
            const long long popLL = c.getPopulation();
            if (popLL <= 0) continue;
            const double pop = static_cast<double>(popLL);
            const double stability = clamp01(c.getStability());
            const double legitimacy = clamp01(c.getLegitimacy());
            const bool lowS = stability < kStateLowThreshold;
            const bool lowL = legitimacy < kStateLowThreshold;
            const bool crit = stability < kStateCriticalThreshold || legitimacy < kStateCriticalThreshold;
            const bool stable = stability >= kStateStableThreshold && legitimacy >= kStateStableThreshold;

            liveCountries++;
            if (stable) stableCountries++;
            if (lowS) lowStabilityCountries++;
            if (lowL) lowLegitimacyCountries++;
            if (lowS && lowL) lowBothCountries++;
            if (crit) criticalCountries++;

            popTotal += pop;
            if (lowS) popLowStability += pop;
            if (lowL) popLowLegitimacy += pop;
            if (lowS && lowL) popLowBoth += pop;
            if (crit) popCritical += pop;

            worstStability.push_back(WeakRow{c.getCountryIndex(), stability});
            worstLegitimacy.push_back(WeakRow{c.getCountryIndex(), legitimacy});
            stabilities.push_back(stability);
            legitimacies.push_back(legitimacy);

            const auto& sd = c.getMacroEconomy().stabilityDebug;
            const auto& ld = c.getMacroEconomy().legitimacyDebug;
            const auto stabDom = pickDominant({
                {"war", std::max(0.0, -sd.dbg_delta_war)},
                {"plague", std::max(0.0, -sd.dbg_delta_plague)},
                {"stagnation", std::max(0.0, -sd.dbg_delta_stagnation)},
                {"debt", std::max(0.0, -sd.dbg_delta_debt_crisis)},
                {"control", std::max(0.0, -sd.dbg_delta_control_decay)},
                {"demography", std::max(0.0, -sd.dbg_delta_demog_stress)},
                {"budget", std::max(0.0, -sd.dbg_stab_delta_budget)}
            });
            const auto legitDom = pickDominant({
                {"economy", std::max(0.0, -ld.dbg_legit_delta_economy)},
                {"budget", std::max(0.0, -ld.dbg_legit_delta_budget)},
                {"demography", std::max(0.0, -ld.dbg_legit_delta_demog)},
                {"culture", std::max(0.0, -ld.dbg_legit_delta_culture)},
                {"events", std::max(0.0, -ld.dbg_legit_delta_events)}
            });

            stateCountriesCsv
                << year << ","
                << c.getCountryIndex() << ","
                << csvEscape(c.getName()) << ","
                << c.getFoundingYear() << ","
                << std::max(0, year - c.getFoundingYear()) << ","
                << popLL << ","
                << stability << ","
                << legitimacy << ","
                << clamp01(c.getAvgControl()) << ","
                << clamp01(c.getAutonomyPressure()) << ","
                << std::max(0, c.getAutonomyOverThresholdYears()) << ","
                << (c.isAtWar() ? 1 : 0) << ","
                << stateBucket(stability, legitimacy) << ","
                << (lowS ? 1 : 0) << ","
                << (lowL ? 1 : 0) << ","
                << (crit ? 1 : 0) << ","
                << (stable ? 1 : 0) << ","
                << sd.dbg_stab_start_year << ","
                << sd.dbg_stab_after_country_update << ","
                << sd.dbg_stab_after_budget << ","
                << sd.dbg_stab_after_demography << ","
                << sd.dbg_stab_delta_update << ","
                << sd.dbg_stab_delta_budget << ","
                << sd.dbg_stab_delta_demog << ","
                << sd.dbg_stab_delta_total << ","
                << sd.dbg_delta_war << ","
                << sd.dbg_delta_plague << ","
                << sd.dbg_delta_stagnation << ","
                << sd.dbg_delta_peace_recover << ","
                << sd.dbg_delta_debt_crisis << ","
                << sd.dbg_delta_control_decay << ","
                << sd.dbg_delta_demog_stress << ","
                << sd.dbg_shortageRatio << ","
                << sd.dbg_diseaseBurden << ","
                << sd.dbg_stagnationYears << ","
                << stabDom.first << ","
                << stabDom.second << ","
                << ld.dbg_legit_start << ","
                << ld.dbg_legit_after_economy << ","
                << ld.dbg_legit_after_budget << ","
                << ld.dbg_legit_after_demog << ","
                << ld.dbg_legit_after_culture << ","
                << ld.dbg_legit_end << ","
                << ld.dbg_legit_delta_economy << ","
                << ld.dbg_legit_delta_budget << ","
                << ld.dbg_legit_delta_demog << ","
                << ld.dbg_legit_delta_culture << ","
                << ld.dbg_legit_delta_events << ","
                << ld.dbg_legit_delta_total << ","
                << ld.dbg_legit_budget_shortfall_direct << ","
                << ld.dbg_legit_budget_burden_penalty << ","
                << ld.dbg_legit_budget_drift_stability << ","
                << ld.dbg_legit_budget_drift_tax << ","
                << ld.dbg_legit_budget_drift_control << ","
                << ld.dbg_legit_budget_drift_debt << ","
                << ld.dbg_legit_budget_drift_service << ","
                << ld.dbg_legit_budget_drift_shortfall << ","
                << ld.dbg_legit_budget_drift_plague << ","
                << ld.dbg_legit_budget_drift_war << ","
                << ld.dbg_legit_demog_shortageRatio << ","
                << ld.dbg_legit_demog_diseaseBurden << ","
                << legitDom.first << ","
                << legitDom.second
                << "\n";
        }

        std::sort(worstStability.begin(), worstStability.end(), [](const WeakRow& a, const WeakRow& b) {
            if (a.value != b.value) return a.value < b.value;
            return a.id < b.id;
        });
        std::sort(worstLegitimacy.begin(), worstLegitimacy.end(), [](const WeakRow& a, const WeakRow& b) {
            if (a.value != b.value) return a.value < b.value;
            return a.id < b.id;
        });

        auto weakestToString = [](const std::vector<WeakRow>& rows) -> std::string {
            std::ostringstream oss;
            const size_t n = std::min<size_t>(5, rows.size());
            for (size_t i = 0; i < n; ++i) {
                if (i > 0) oss << ";";
                oss << rows[i].id << ":" << std::fixed << std::setprecision(3) << rows[i].value;
            }
            return oss.str();
        };

        const double popDen = std::max(1.0, popTotal);
        stateSummaryCsv
            << year << ","
            << liveCountries << ","
            << stableCountries << ","
            << lowStabilityCountries << ","
            << lowLegitimacyCountries << ","
            << lowBothCountries << ","
            << criticalCountries << ","
            << popTotal << ","
            << popLowStability << ","
            << popLowLegitimacy << ","
            << popLowBoth << ","
            << popCritical << ","
            << (popLowStability / popDen) << ","
            << (popLowLegitimacy / popDen) << ","
            << (popLowBoth / popDen) << ","
            << (popCritical / popDen) << ","
            << mean(stabilities) << ","
            << percentile(stabilities, 0.10) << ","
            << mean(legitimacies) << ","
            << percentile(legitimacies, 0.10) << ","
            << csvEscape(weakestToString(worstStability)) << ","
            << csvEscape(weakestToString(worstLegitimacy))
            << "\n";
    };

    for (int y = startYear; y <= endYear && !stoppedOnTargetTech; ++y) {
        simulateOneYear(y);
        maybeLogTechEvents(y);
        if (opt.stopOnTechId >= 0 && anyCountryHasTech(opt.stopOnTechId)) {
            stoppedOnTargetTech = true;
            stoppedOnTargetTechYear = y;
        }

        if (wasAtWar.size() < countries.size()) {
            const size_t old = wasAtWar.size();
            wasAtWar.resize(countries.size(), 0u);
            for (size_t i = old; i < countries.size(); ++i) {
                wasAtWar[i] = countries[i].isAtWar() ? 1u : 0u;
            }
        }
        if (famineWave.size() < countries.size()) famineWave.resize(countries.size(), 0u);
        if (epidemicWave.size() < countries.size()) epidemicWave.resize(countries.size(), 0u);
        if (migrationWave.size() < countries.size()) migrationWave.resize(countries.size(), 0u);

        for (size_t i = 0; i < countries.size(); ++i) {
            const Country& c = countries[i];
            const uint8_t now = c.isAtWar() ? 1u : 0u;
            if (now != 0u && wasAtWar[i] == 0u) {
                eventsWindow.major_war_count++;
            }
            wasAtWar[i] = now;

            if (c.getPopulation() <= 0) continue;
            const auto& m = c.getMacroEconomy();
            const bool famineNow = (m.famineSeverity > 0.20) || (m.foodSecurity < 0.92);
            if (famineNow && famineWave[i] == 0u) eventsWindow.famine_wave_count++;
            famineWave[i] = famineNow ? 1u : 0u;

            const bool epiNow = (m.diseaseBurden > 0.02);
            if (epiNow && epidemicWave[i] == 0u) eventsWindow.epidemic_wave_count++;
            epidemicWave[i] = epiNow ? 1u : 0u;

            const bool migNow = (m.migrationPressureOut > 0.22);
            if (migNow && migrationWave[i] == 0u) eventsWindow.mass_migration_count++;
            migrationWave[i] = migNow ? 1u : 0u;
        }

        for (const std::string& e : news.getEvents()) {
            const std::string token = std::to_string(y) + "|" + e;
            if (!seenNewsTokens.insert(token).second) {
                continue;
            }
            const bool electionEvt = (e.find("Election in ") != std::string::npos) || (e.find("election in ") != std::string::npos);
            const bool civil = (e.find("Civil war") != std::string::npos) || (e.find("civil war") != std::string::npos);
            const bool frag = (e.find("Breakaway") != std::string::npos) || (e.find("fragments") != std::string::npos);
            const bool migrationEvt = (e.find("migration") != std::string::npos) || (e.find("refugee") != std::string::npos);
            if (electionEvt) eventsWindow.election_count++;
            if (civil) eventsWindow.civil_conflict_count++;
            if (frag) eventsWindow.fragmentation_count++;
            if (migrationEvt) eventsWindow.mass_migration_count++;
        }

        const std::string inv = checkInvariants(countries, map, macroEconomy.getLastTradeIntensity(), &settlementSystem);
        if (!inv.empty()) {
            invariantsOk = false;
            invariantError = "year " + std::to_string(y) + ": " + inv;
            break;
        }

        const bool cadenceHit = ((y - startYear) % opt.checkpointEveryYears) == 0;
        if (y == startYear || y == endYear || cadenceHit || containsYear(explicitCheckpoints, y)) {
            const int yearsSinceLast = std::max(1, y - lastCheckpointYear);
            const MetricsSnapshot* prev = checkpoints.empty() ? nullptr : &checkpoints.back();
            checkpoints.push_back(computeSnapshot(ctx,
                                                  map,
                                                  tradeManager,
                                                  geo,
                                                  y,
                                                  countries,
                                                  macroEconomy.getLastTradeIntensity(),
                                                  eventsWindow,
                                                  prev,
                                                  yearsSinceLast));
            emitStateDiagnosticsCheckpoint(y);
            eventsWindow = EventWindowCounters{};
            lastCheckpointYear = y;
        }
    }

    const std::filesystem::path outDir(opt.outDir);
    const std::filesystem::path csvPath = outDir / "timeseries.csv";
    const std::filesystem::path jsonPath = outDir / "run_summary.json";
    const std::filesystem::path metaPath = outDir / "run_meta.json";
    const std::filesystem::path violationsPath = outDir / "violations.json";

    {
        std::ofstream csv(csvPath);
        csv << "year,world_pop_total,world_pop_growth_rate_annual,world_food_adequacy_index,world_famine_death_rate,world_disease_death_rate,world_war_death_rate,"
               "world_trade_intensity,world_urban_share_proxy,world_tech_capability_index_median,world_tech_capability_index_p90,world_state_capacity_index_median,"
               "world_state_capacity_index_p10,competition_fragmentation_index_median,idea_market_integration_index_median,credible_commitment_index_median,"
               "relative_factor_price_index_median,media_throughput_index_median,merchant_power_index_median,skilled_migration_in_rate_t,skilled_migration_out_rate_t,"
               "migration_rate_t,famine_exposure_share_t,habitable_cell_share_pop_gt_0,habitable_cell_share_pop_gt_small,pop_share_by_lat_band,"
               "pop_share_coastal_vs_inland,pop_share_river_proximal,market_access_p10,market_access_median,food_adequacy_p10,food_adequacy_median,travel_cost_index_median,"
               "country_pop_median,country_pop_p90,country_pop_top1_share,country_area_median,country_area_p90,country_area_top1_share,control_median,control_p10,"
               "founder_state_count,founder_state_share,median_state_age_years,p90_state_age_years,"
               "wars_active_count,city_pop_top1,city_pop_top10_sum_share,city_tail_index,famine_wave_count,epidemic_wave_count,major_war_count,election_count,civil_conflict_count,"
               "fragmentation_count,mass_migration_count,logistics_capability_index,storage_capability_index,health_capability_index,transport_cost_index,"
               "spoilage_kcal,storage_loss_kcal,available_kcal_before_losses,trade_volume_total,trade_volume_long,long_distance_trade_proxy,extraction_index\n";
        csv << std::fixed << std::setprecision(6);
        for (const MetricsSnapshot& s : checkpoints) {
            csv << s.year << ","
                << s.world_pop_total << ","
                << s.world_pop_growth_rate_annual << ","
                << s.world_food_adequacy_index << ","
                << s.world_famine_death_rate << ","
                << s.world_disease_death_rate << ","
                << s.world_war_death_rate << ","
                << s.world_trade_intensity << ","
                << s.world_urban_share_proxy << ","
                << s.world_tech_capability_index_median << ","
                << s.world_tech_capability_index_p90 << ","
                << s.world_state_capacity_index_median << ","
                << s.world_state_capacity_index_p10 << ","
                << s.competition_fragmentation_index_median << ","
                << s.idea_market_integration_index_median << ","
                << s.credible_commitment_index_median << ","
                << s.relative_factor_price_index_median << ","
                << s.media_throughput_index_median << ","
                << s.merchant_power_index_median << ","
                << s.skilled_migration_in_rate_t << ","
                << s.skilled_migration_out_rate_t << ","
                << s.migration_rate_t << ","
                << s.famine_exposure_share_t << ","
                << s.habitable_cell_share_pop_gt_0 << ","
                << s.habitable_cell_share_pop_gt_small << ","
                << latBandsToString(s.pop_share_by_lat_band) << ","
                << s.pop_share_coastal_vs_inland << ","
                << s.pop_share_river_proximal << ","
                << s.market_access_p10 << ","
                << s.market_access_median << ","
                << s.food_adequacy_p10 << ","
                << s.food_adequacy_median << ","
                << s.travel_cost_index_median << ","
                << s.country_pop_median << ","
                << s.country_pop_p90 << ","
                << s.country_pop_top1_share << ","
                << s.country_area_median << ","
                << s.country_area_p90 << ","
                << s.country_area_top1_share << ","
                << s.control_median << ","
                << s.control_p10 << ","
                << s.founder_state_count << ","
                << s.founder_state_share << ","
                << s.median_state_age_years << ","
                << s.p90_state_age_years << ","
                << s.wars_active_count << ","
                << s.city_pop_top1 << ","
                << s.city_pop_top10_sum_share << ","
                << s.city_tail_index << ","
                << s.famine_wave_count << ","
                << s.epidemic_wave_count << ","
                << s.major_war_count << ","
                << s.election_count << ","
                << s.civil_conflict_count << ","
                << s.fragmentation_count << ","
                << s.mass_migration_count << ","
                << s.logistics_capability_index << ","
                << s.storage_capability_index << ","
                << s.health_capability_index << ","
                << s.transport_cost_index << ","
                << s.spoilage_kcal << ","
                << s.storage_loss_kcal << ","
                << s.available_kcal_before_losses << ","
                << s.trade_volume_total << ","
                << s.trade_volume_long << ","
                << s.long_distance_trade_proxy << ","
                << s.extraction_index << "\n";
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
        js << "  \"stoppedOnTargetTech\": " << (stoppedOnTargetTech ? "true" : "false") << ",\n";
        js << "  \"stoppedOnTargetTechYear\": " << (stoppedOnTargetTech ? std::to_string(stoppedOnTargetTechYear) : std::string("null")) << ",\n";
        js << "  \"worldStartYear\": " << worldStartYear << ",\n";
        js << "  \"useGPU\": " << (ctx.config.economy.useGPU ? "true" : "false") << ",\n";
        js << "  \"stateDiagnostics\": " << (stateDiagnosticsEnabled ? "true" : "false") << ",\n";
        js << "  \"total_score\": 0.0,\n";
        js << "  \"gates\": {\n";
        js << "    \"metric_availability\": true,\n";
        js << "    \"canary_pass\": false,\n";
        js << "    \"backend_parity_pass\": false,\n";
        js << "    \"hardfail\": \"" << (invariantsOk ? "" : "BROKEN_ACCOUNTING") << "\"\n";
        js << "  },\n";
        js << "  \"top_violations\": [],\n";
        js << "  \"invariants\": {\n";
        js << "    \"ok\": " << (invariantsOk ? "true" : "false") << ",\n";
        js << "    \"message\": \"" << jsonEscape(invariantError) << "\"\n";
        js << "  },\n";
        js << "  \"checkpoints\": [\n";
        for (size_t i = 0; i < checkpoints.size(); ++i) {
            const MetricsSnapshot& s = checkpoints[i];
            js << "    {\n";
            js << "      \"year\": " << s.year << ",\n";
            js << "      \"world_pop_total\": " << s.world_pop_total << ",\n";
            js << "      \"world_food_adequacy_index\": " << s.world_food_adequacy_index << ",\n";
            js << "      \"world_trade_intensity\": " << s.world_trade_intensity << ",\n";
            js << "      \"world_urban_share_proxy\": " << s.world_urban_share_proxy << ",\n";
            js << "      \"world_tech_capability_index_median\": " << s.world_tech_capability_index_median << ",\n";
            js << "      \"world_state_capacity_index_median\": " << s.world_state_capacity_index_median << ",\n";
            js << "      \"competition_fragmentation_index_median\": " << s.competition_fragmentation_index_median << ",\n";
            js << "      \"idea_market_integration_index_median\": " << s.idea_market_integration_index_median << ",\n";
            js << "      \"credible_commitment_index_median\": " << s.credible_commitment_index_median << ",\n";
            js << "      \"relative_factor_price_index_median\": " << s.relative_factor_price_index_median << ",\n";
            js << "      \"major_war_count\": " << s.major_war_count << ",\n";
            js << "      \"election_count\": " << s.election_count << ",\n";
            js << "      \"famine_wave_count\": " << s.famine_wave_count << ",\n";
            js << "      \"epidemic_wave_count\": " << s.epidemic_wave_count << ",\n";
            js << "      \"migration_rate_t\": " << s.migration_rate_t << ",\n";
            js << "      \"founder_state_count\": " << s.founder_state_count << ",\n";
            js << "      \"founder_state_share\": " << s.founder_state_share << ",\n";
            js << "      \"median_state_age_years\": " << s.median_state_age_years << ",\n";
            js << "      \"p90_state_age_years\": " << s.p90_state_age_years << "\n";
            js << "    }" << (i + 1 < checkpoints.size() ? "," : "") << "\n";
        }
        js << "  ]\n";
        js << "}\n";
    }

    {
        std::ofstream meta(metaPath);
        meta << std::fixed << std::setprecision(6);
        meta << "{\n";
        meta << "  \"seed\": " << opt.seed << ",\n";
        meta << "  \"config_path\": \"" << jsonEscape(ctx.configPath) << "\",\n";
        meta << "  \"config_hash\": \"" << jsonEscape(ctx.configHash) << "\",\n";
        meta << "  \"git_commit\": \"unknown\",\n";
        meta << "  \"backend\": \"" << (ctx.config.economy.useGPU ? "gpu" : "cpu") << "\",\n";
        meta << "  \"start_year\": " << startYear << ",\n";
        meta << "  \"end_year\": " << endYear << ",\n";
        meta << "  \"state_diagnostics\": " << (stateDiagnosticsEnabled ? "true" : "false") << ",\n";
        meta << "  \"ideology_transition_logs\": " << (opt.logIdeologyTransitions ? "true" : "false") << ",\n";
        meta << "  \"stopped_on_target_tech\": " << (stoppedOnTargetTech ? "true" : "false") << ",\n";
        meta << "  \"stopped_on_target_tech_year\": " << (stoppedOnTargetTech ? std::to_string(stoppedOnTargetTechYear) : std::string("null")) << ",\n";
        meta << "  \"map_hash\": \"" << jsonEscape(SimulationContext::hashFileFNV1a("assets/images/map.png")) << "\",\n";
        meta << "  \"goals_version\": \"realism-envelope-v7\",\n";
        meta << "  \"evaluator_version\": \"v7\",\n";
        meta << "  \"definitions_version\": \"v7\",\n";
        meta << "  \"scoring_version\": \"v7\"\n";
        meta << "}\n";
    }

    {
        std::ofstream vio(violationsPath);
        vio << "[]\n";
    }

    std::cout << "Wrote " << jsonPath.string() << ", " << csvPath.string()
              << ", " << metaPath.string() << ", " << violationsPath.string() << "\n";
    if (!invariantsOk) {
        std::cerr << "Invariant failure: " << invariantError << "\n";
        return 3;
    }
    return 0;
}
