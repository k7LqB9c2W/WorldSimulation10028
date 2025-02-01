#pragma once
#include <string>
#include <vector>
#include "country.h"
#include "news.h"

// The field in which a great person excels.
enum class GreatPersonField {
    Military,
    Science
};

// A structure to hold all the data for one great person effect.
struct GreatPersonEffect {
    int countryIndex;                // The country that receives the bonus.
    GreatPersonField field;          // The field (Military or Science).
    std::string name;                // The generated name.
    double multiplier;               // The bonus multiplier (1.25 to 2.0).
    int startYear;                   // The year when the effect starts.
    int duration;                    // Duration of the effect (in years, 30–40).
    int expiryYear;                  // Computed as startYear + duration.
};

class GreatPeopleManager {
public:
    GreatPeopleManager();

    // Call this once per simulation year.
    void updateEffects(int currentYear, std::vector<Country>& countries, News& news);

    // When computing bonus stats, use these helper functions.
    double getMilitaryBonus(int countryIndex, int currentYear) const;
    double getScienceBonus(int countryIndex, int currentYear) const;

private:
    int m_nextEventYear;                    // The next simulation year when a great person event will occur.
    std::vector<GreatPersonEffect> m_activeEffects;

    // Utility functions to generate random values.
    std::string generateRandomName();
    GreatPersonField getRandomField();
    double getRandomMultiplier();
    int getRandomDuration();
    int getRandomEventInterval();  // Random interval between 100 and 500 years.
};