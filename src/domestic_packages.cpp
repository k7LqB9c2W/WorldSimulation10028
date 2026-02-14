#include "domestic_packages.h"

const std::vector<DomesticPackageDefinition>& getDefaultDomesticPackages() {
    static const std::vector<DomesticPackageDefinition> kPackages = {
        {0, "floodplain_irrigation", 0.96, 1.34, 0.90, 0.88, 0.12, 0.95, 0.10, 0.05, 0.25},
        {1, "clay_granaries", 1.00, 1.08, 1.00, 1.02, 0.24, 0.40, 0.20, 0.20, 0.35},
        {2, "caravan_herding", 0.92, 0.95, 1.28, 0.82, 0.08, 0.10, 0.92, 0.20, 0.45},
        {3, "littoral_fishery", 0.86, 0.88, 0.84, 1.46, 0.10, 1.00, 0.10, 0.05, 0.30},
        {4, "craft_market_towns", 0.90, 1.02, 0.94, 0.94, 0.06, 0.20, 0.20, 0.15, 1.00},
    };
    return kPackages;
}
