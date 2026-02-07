#include "great_people.h"
#include <algorithm>
#include <cctype>

// Constructor: set the first event to occur between 100 and 500 years after simulation start.
GreatPeopleManager::GreatPeopleManager(SimulationContext& ctx)
    : m_rng(ctx.makeRng(0x4752454154504552ull)) { // "GREATPER"
    std::uniform_int_distribution<> eventIntervalDist(100, 500);
    m_nextEventYear = -5000 + eventIntervalDist(m_rng);
}

// Generates a random name from a mixture of syllables and optional prefixes/suffixes.
std::string GreatPeopleManager::generateRandomName() {
    std::vector<std::string> syllables = {
        "an", "ka", "li", "ra", "to", "mi", "shi", "zen",
        "abu", "ori", "mar", "dak", "wen", "sei", "yan", "tuk",
        "sal", "nak", "dor", "gui"
    };
    std::vector<std::string> prefixes = {
        "Al", "La", "De", "Da", "El", "Ma", "Ni", "Su", "Ta", "Lu", "Ko", "Fe"
    };
    std::vector<std::string> suffixes = {
        "son", "sen", "man", "ski", "ez", "ov", "ing", "ton", "shi", "li", "zu", "ra"
    };

    std::uniform_int_distribution<> syllableCountDist(2, 3);
    int syllableCount = syllableCountDist(m_rng);

    std::string name;
    std::uniform_int_distribution<> coin(0, 1);
    // 50% chance to add a prefix.
    if (coin(m_rng) == 1) {
        std::uniform_int_distribution<> prefixDist(0, static_cast<int>(prefixes.size()) - 1);
        name += prefixes[prefixDist(m_rng)];
    }
    // Add 2–3 syllables.
    for (int i = 0; i < syllableCount; i++) {
        std::uniform_int_distribution<> syllableDist(0, static_cast<int>(syllables.size()) - 1);
        name += syllables[syllableDist(m_rng)];
    }
    // 50% chance to add a suffix.
    if (coin(m_rng) == 1) {
        std::uniform_int_distribution<> suffixDist(0, static_cast<int>(suffixes.size()) - 1);
        name += suffixes[suffixDist(m_rng)];
    }
    // Capitalize the first letter.
    if (!name.empty())
        name[0] = std::toupper(name[0]);
    return name;
}

GreatPersonField GreatPeopleManager::getRandomField() {
    std::uniform_int_distribution<> fieldDist(0, 1);
    return (fieldDist(m_rng) == 0) ? GreatPersonField::Military : GreatPersonField::Science;
}

double GreatPeopleManager::getRandomMultiplier() {
    std::uniform_real_distribution<> multDist(1.25, 2.0);
    return multDist(m_rng);
}

int GreatPeopleManager::getRandomDuration() {
    std::uniform_int_distribution<> durationDist(30, 40);
    return durationDist(m_rng);
}

int GreatPeopleManager::getRandomEventInterval() {
    std::uniform_int_distribution<> intervalDist(100, 500);
    return intervalDist(m_rng);
}

// Each simulation year call updateEffects() to both remove expired great person effects
// and possibly generate new ones.
void GreatPeopleManager::updateEffects(int currentYear,
                                       std::vector<Country>& countries,
                                       News& news,
                                       int dtYears) {
    const int years = std::max(1, dtYears);
    const int startYear = currentYear - years + 1;

    auto processOneYear = [&](int simYear) {
        // Remove expired effects.
        m_activeEffects.erase(
            std::remove_if(m_activeEffects.begin(), m_activeEffects.end(),
                [simYear](const GreatPersonEffect& effect) {
                    return simYear >= effect.expiryYear;
                }),
            m_activeEffects.end());

        // If it's time for a new great person event...
        if (simYear >= m_nextEventYear) {
            int numCountries = static_cast<int>(countries.size());
            int numGreatPeople = numCountries * 5 / 100; // 5% (rounded down)
            if (numGreatPeople > 0) {
                // Shuffle country indices.
                std::vector<int> indices(numCountries);
                for (int i = 0; i < numCountries; ++i) {
                    indices[i] = i;
                }
                std::shuffle(indices.begin(), indices.end(), m_rng);

                // For the first numGreatPeople countries, create a new effect.
                for (int i = 0; i < numGreatPeople; ++i) {
                    int countryIndex = indices[i];
                    GreatPersonField field = getRandomField();
                    double multiplier = getRandomMultiplier();
                    int duration = getRandomDuration();
                    int expiryYear = simYear + duration;
                    std::string personName = generateRandomName();

                    GreatPersonEffect effect;
                    effect.countryIndex = countryIndex;
                    effect.field = field;
                    effect.name = personName;
                    effect.multiplier = multiplier;
                    effect.startYear = simYear;
                    effect.duration = duration;
                    effect.expiryYear = expiryYear;

                    m_activeEffects.push_back(effect);

                    // Announce the event.
                    std::string fieldName = (field == GreatPersonField::Military) ? "Military" : "Science";
                    std::string newsEvent = "Great " + fieldName + " Person " + personName +
                        " was born in " + countries[static_cast<size_t>(countryIndex)].getName() + "!";
                    news.addEvent(newsEvent);
                }
            }
            // Schedule the next event.
            m_nextEventYear = simYear + getRandomEventInterval();
        }

        // Now, for each country, update its military (and optionally science) stats directly.
        // First, reset each country’s stat to its base value.
        for (auto& country : countries) {
            country.resetMilitaryStrength();
            // (Similarly, you could reset science production if desired.)
        }

        // Then, for each active effect, if it applies to a given country and is active,
        // apply the bonus. (If a country gets more than one effect in the same field,
        // we use the highest multiplier.)
        for (const auto& effect : m_activeEffects) {
            if (simYear >= effect.startYear && simYear < effect.expiryYear) {
                // For now, we only handle military field.
                if (effect.field == GreatPersonField::Military) {
                    const int idx = effect.countryIndex;
                    if (idx < 0 || idx >= static_cast<int>(countries.size())) {
                        continue;
                    }
                    // Apply the bonus directly.
                    // (Note: if multiple effects apply, this simple method will multiply several times.
                    // To simply use the maximum, you could first compute the max bonus per country.)
                    // For a minimal integration that uses maximum, we do:
                    double currentBonus = getMilitaryBonus(idx, simYear);
                    // Reset the country to base, then apply the maximum bonus.
                    // (Alternatively, you could compute a per-country max bonus outside the loop.)
                    countries[static_cast<size_t>(idx)].resetMilitaryStrength();
                    countries[static_cast<size_t>(idx)].applyMilitaryBonus(currentBonus);
                }
                // (Similarly, if you want to handle science, add a branch for GreatPersonField::Science.)
            }
        }
    };

    for (int simYear = startYear; simYear <= currentYear; ++simYear) {
        processOneYear(simYear);
    }
}


// These helper functions return the highest bonus (if any) that is currently active for a given country.
double GreatPeopleManager::getMilitaryBonus(int countryIndex, int currentYear) const {
    double bonus = 1.0;
    for (const auto& effect : m_activeEffects) {
        if (effect.countryIndex == countryIndex && effect.field == GreatPersonField::Military) {
            if (currentYear >= effect.startYear && currentYear < effect.expiryYear) {
                bonus = std::max(bonus, effect.multiplier);
            }
        }
    }
    return bonus;
}

double GreatPeopleManager::getScienceBonus(int countryIndex, int currentYear) const {
    double bonus = 1.0;
    for (const auto& effect : m_activeEffects) {
        if (effect.countryIndex == countryIndex && effect.field == GreatPersonField::Science) {
            if (currentYear >= effect.startYear && currentYear < effect.expiryYear) {
                bonus = std::max(bonus, effect.multiplier);
            }
        }
    }
    return bonus;
}
