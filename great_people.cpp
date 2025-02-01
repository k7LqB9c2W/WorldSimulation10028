#include "great_people.h"
#include <algorithm>
#include <random>
#include <cctype>

// Constructor: set the first event to occur between 100 and 500 years after simulation start.
GreatPeopleManager::GreatPeopleManager() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> eventIntervalDist(100, 500);
    m_nextEventYear = -5000 + eventIntervalDist(gen);
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

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> syllableCountDist(2, 3);
    int syllableCount = syllableCountDist(gen);

    std::string name;
    std::uniform_int_distribution<> coin(0, 1);
    // 50% chance to add a prefix.
    if (coin(gen) == 1) {
        std::uniform_int_distribution<> prefixDist(0, prefixes.size() - 1);
        name += prefixes[prefixDist(gen)];
    }
    // Add 2–3 syllables.
    for (int i = 0; i < syllableCount; i++) {
        std::uniform_int_distribution<> syllableDist(0, syllables.size() - 1);
        name += syllables[syllableDist(gen)];
    }
    // 50% chance to add a suffix.
    if (coin(gen) == 1) {
        std::uniform_int_distribution<> suffixDist(0, suffixes.size() - 1);
        name += suffixes[suffixDist(gen)];
    }
    // Capitalize the first letter.
    if (!name.empty())
        name[0] = std::toupper(name[0]);
    return name;
}

GreatPersonField GreatPeopleManager::getRandomField() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> fieldDist(0, 1);
    return (fieldDist(gen) == 0) ? GreatPersonField::Military : GreatPersonField::Science;
}

double GreatPeopleManager::getRandomMultiplier() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> multDist(1.25, 2.0);
    return multDist(gen);
}

int GreatPeopleManager::getRandomDuration() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> durationDist(30, 40);
    return durationDist(gen);
}

int GreatPeopleManager::getRandomEventInterval() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> intervalDist(100, 500);
    return intervalDist(gen);
}

// Each simulation year call updateEffects() to both remove expired great person effects
// and possibly generate new ones.
void GreatPeopleManager::updateEffects(int currentYear, std::vector<Country>& countries, News& news) {
    // Remove expired effects.
    m_activeEffects.erase(
        std::remove_if(m_activeEffects.begin(), m_activeEffects.end(),
            [currentYear](const GreatPersonEffect& effect) {
                return currentYear >= effect.expiryYear;
            }),
        m_activeEffects.end());

    // If it is time for a new event, trigger it.
    if (currentYear >= m_nextEventYear) {
        int numCountries = static_cast<int>(countries.size());
        int numGreatPeople = numCountries * 5 / 100; // 5% of countries (rounds down)
        if (numGreatPeople > 0) {
            // Create a list of country indices and shuffle it.
            std::vector<int> indices(numCountries);
            for (int i = 0; i < numCountries; ++i)
                indices[i] = i;
            std::random_device rd;
            std::mt19937 gen(rd());
            std::shuffle(indices.begin(), indices.end(), gen);

            // For the first numGreatPeople countries, create a new effect.
            for (int i = 0; i < numGreatPeople; ++i) {
                int countryIndex = indices[i];
                GreatPersonField field = getRandomField();
                double multiplier = getRandomMultiplier();
                int duration = getRandomDuration();
                int expiryYear = currentYear + duration;
                std::string personName = generateRandomName();

                GreatPersonEffect effect;
                effect.countryIndex = countryIndex;
                effect.field = field;
                effect.name = personName;
                effect.multiplier = multiplier;
                effect.startYear = currentYear;
                effect.duration = duration;
                effect.expiryYear = expiryYear;

                m_activeEffects.push_back(effect);

                // Announce the event in the news.
                std::string fieldName = (field == GreatPersonField::Military) ? "Military" : "Science";
                std::string newsEvent = "Great " + fieldName + " Person " + personName +
                    " was born in " + countries[countryIndex].getName() + "!";
                news.addEvent(newsEvent);
            }
        }
        // Schedule the next event.
        m_nextEventYear = currentYear + getRandomEventInterval();
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
