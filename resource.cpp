// resource.cpp
#include "resource.h"

Resource::Resource(Type type, double amount) : m_type(type), m_amount(amount) {}

Resource::Type Resource::getType() const {
    return m_type;
}

double Resource::getAmount() const {
    return m_amount;
}

void Resource::setAmount(double amount) {
    m_amount = amount;
}

void Resource::addAmount(double amount) {
    m_amount += amount;
}

ResourceManager::ResourceManager() {
    // Initialize all resource types with 0 amount
    m_resources[Resource::Type::FOOD] = Resource(Resource::Type::FOOD);
    m_resources[Resource::Type::HORSES] = Resource(Resource::Type::HORSES);
    m_resources[Resource::Type::SALT] = Resource(Resource::Type::SALT);
    m_resources[Resource::Type::IRON] = Resource(Resource::Type::IRON);
    m_resources[Resource::Type::COAL] = Resource(Resource::Type::COAL);
    m_resources[Resource::Type::GOLD] = Resource(Resource::Type::GOLD);
}

void ResourceManager::addResource(Resource::Type type, double amount) {
    m_resources[type].addAmount(amount);
}

double ResourceManager::getResourceAmount(Resource::Type type) const {
    return m_resources.at(type).getAmount();
}

void ResourceManager::consumeResource(Resource::Type type, double amount) {
    m_resources[type].addAmount(-amount);
}

const std::unordered_map<Resource::Type, Resource>& ResourceManager::getResources() const {
    return m_resources;
}