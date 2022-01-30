#include "ECS.hpp"

using namespace ECS;

EntityData& EntityData::getInstance()
{
    static EntityData ed;
    return ed;
}

Entity::Entity(size_t eid)
{
    entityData = &EntityData::getInstance();
    entity = &entityData->entities[eid];
    if (!entity->active) {
        throw std::runtime_error("Entity wrapper object initialized on inactive entity data");
    }
}

// Creates an entity adds it to EntityData. Returns an Entity wrapper.
Entity EntityManager::add_entity(std::string entityName)
{
    size_t insertPosition = 0;
    if (entityData->freeEntitySlots.size() == 0 && entityData->entityInsertPosition < MAX_ENTITIES) {
        insertPosition = entityData->entityInsertPosition;
        entityData->entityInsertPosition++;
    } else if (entityData->freeEntitySlots.size() > 0) {
        insertPosition = entityData->freeEntitySlots.back();
        entityData->freeEntitySlots.pop_back();
    } else if (entityData->entityInsertPosition >= MAX_ENTITIES) {
        throw std::runtime_error("Tried to insert more than MAX_ENTITIES");
    }

    entityData->entities[insertPosition].active = true;
    entityData->entities[insertPosition].eid = insertPosition;

    // If a name wasn't provided generate one
    if (entityName.size() == 0) {
        entityName = "Unnamed Entity. EID = " + std::to_string(insertPosition);
    }

    entityData->entityNames.insert({ entityName, &entityData->entities[insertPosition] });
    return Entity(insertPosition);
}

void EntityManager::remove_entity(Entity e)
{
    e.entity->active = false;
    entityData->freeEntitySlots.push_back(e.entity->eid);

    e.entity->eid = -1;
    e.entity->activeComponents.reset();
}

void EntityManager::remove_entity(std::string entityName)
{
    remove_entity(get_entity_by_name(entityName));
}

Entity EntityManager::get_entity_by_name(std::string entityName)
{
    try {
        return Entity(entityData->entityNames.at(entityName)->eid);
    } catch (std::runtime_error e) {
        // Throw a more descriptive error
        throw std::runtime_error("Provided entity name is not in use");
    }
}

void EntityManager::update(double dt_ms)
{
    for (System* system : entityData->systems) {
        system->update(dt_ms);
    }

    for (System* system : entityData->updateLastSystems) {
        system->update(dt_ms);
    }
}

// Resets the ECS and removes all entities and components
void EntityManager::clear()
{
    for (System* system : entityData->systems) {
        system->exit();
        delete system;
    }
    entityData->systems.clear();

    for (System* system : entityData->updateLastSystems) {
        system->exit();
        delete system;
    }

    entityData->updateLastSystems.clear();
    for (int i = 0; i < MAX_ENTITIES; ++i) {
        entityData->entities[i].active = false;
        entityData->entities[i].eid = -1;
        entityData->entities[i].activeComponents.reset();
    }

    for (int i = 0; i < MAX_COMPONENTS; ++i) {
        entityData->componentGroups[i].cgid = -1;
        entityData->componentGroups[i].cgd = nullptr;
    }

    for (auto alloc : entityData->allocations) {
        free(alloc);
    }
    entityData->allocations.clear();

    for (size_t* cgid : entityData->staticCgids) {
        *cgid = -1;
    }
    entityData->staticCgids.clear();

    entityData->entityInsertPosition = 0;
    entityData->freeEntitySlots.clear();

    entityData->componentInsertPosition = 0;
    entityData->freeComponentSlots.clear();

    entityData->entityNames.clear();
}

EntityManager& EntityManager::getInstance()
{
    static EntityManager manager;
    return manager;
}

EntityManager::EntityManager()
{
    entityData = &EntityData::getInstance();
}
