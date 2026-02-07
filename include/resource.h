// resource.h
#pragma once

#include <array>
#include <string>
#include <unordered_map>

class Resource {
public:
    enum class Type {
        FOOD = 0,
        HORSES = 1,
        SALT = 2,
        IRON = 3,
        COAL = 4,
        GOLD = 5,
        COPPER = 6,
        TIN = 7,
        CLAY = 8
    };

    static constexpr int kTypeCount = 9;
    static constexpr std::array<Type, kTypeCount> kAllTypes = {
        Type::FOOD,
        Type::HORSES,
        Type::SALT,
        Type::IRON,
        Type::COAL,
        Type::GOLD,
        Type::COPPER,
        Type::TIN,
        Type::CLAY
    };

    //Only change is adding default values here so that the default constructor is generated as well
    Resource(Type type = Type::FOOD, double amount = 0.0);
    Type getType() const;
    double getAmount() const;
    void setAmount(double amount);
    void addAmount(double amount);

private:
    Type m_type;
    double m_amount;
};


class ResourceManager {
public:
    ResourceManager();
    void addResource(Resource::Type type, double amount);
    double getResourceAmount(Resource::Type type) const;
    void consumeResource(Resource::Type type, double amount);
    const std::unordered_map<Resource::Type, Resource>& getResources() const;

private:
    std::unordered_map<Resource::Type, Resource> m_resources;
};
