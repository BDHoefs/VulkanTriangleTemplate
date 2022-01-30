#pragma once
#include <bitset>
#include <functional>
#include <optional>
#include <queue>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace ECS {
// The maximum number of entities that can be active at once
constexpr int MAX_ENTITIES = 8192;
// The maximum number of unique component types in the scene
constexpr int MAX_COMPONENTS = 128;

template <typename T>
struct ComponentGroupData {
    // Indexed into with Entity::eid to get the component of the templated type that corresponds to that entity
    T components[MAX_ENTITIES];
};

// Untemplated wrapper around a pointer to a ComponentGroupData so that objects of this type can be stored in an array
struct ComponentGroup {
    void* cgd = nullptr;
    size_t cgid = -1;
};

struct RawEntity {
    // Unique id for this entity
    size_t eid = 0;
    // Describes which components are active for this entity
    std::bitset<MAX_COMPONENTS> activeComponents;

    bool active = false;
};

// Abstract system class
class EntityManager;
class System {
public:
    virtual void init() = 0;
    virtual void update(double dt_ms) = 0;
    virtual void exit() = 0;
};

// Singleton class due to the method that get_component_group() uses to find the correct ComponentGroup for a given type
struct EntityData {
    template <typename T>
    ComponentGroup* get_component_group()
    {
        // This method of figuring out the type with a static variable in a templated function means this class must be a singleton
        static uint64_t cgid = -1;

        // get_component_group() has not been called for this type before. Create a spot for it in componentGroups
        if (cgid == -1) {
            // Store the location of cgid for this particular templated type so that the ECS can be reset
            staticCgids.push_back(&cgid);

            if (freeComponentSlots.size() == 0 && componentInsertPosition < MAX_COMPONENTS) {
                componentGroups[componentInsertPosition].cgid = componentInsertPosition;
                componentGroups[componentInsertPosition].cgd = calloc(1, sizeof(ComponentGroupData<T>));
                allocations.push_back(componentGroups[componentInsertPosition].cgd);

                cgid = componentInsertPosition;
                componentInsertPosition++;
            } else if (freeComponentSlots.size() > 0) {
                componentGroups[freeComponentSlots.back()].cgid = freeComponentSlots.back();
                componentGroups[freeComponentSlots.back()].cgd = calloc(1, sizeof(ComponentGroupData<T>));
                allocations.push_back(componentGroups[freeComponentSlots.back()].cgd);

                cgid = freeComponentSlots.back();
                freeComponentSlots.pop_back();
            } else if (componentInsertPosition >= MAX_COMPONENTS) {
                throw std::runtime_error("Maximum number of unique component types, MAX_COMPONENTS, exceeded. Cannot add another component");
            }
        }

        return &componentGroups[cgid];
    }

    EntityData() { }

    // Singleton class
    static EntityData& getInstance();
    EntityData(EntityData&) = delete;
    void operator=(EntityData const&) = delete;

    ComponentGroup componentGroups[MAX_COMPONENTS];
    // Indicates the current last free position in componentGroups
    size_t componentInsertPosition = 0;
    // Indicates free positions behind the insertPosition
    std::vector<size_t> freeComponentSlots;

    RawEntity entities[MAX_ENTITIES];
    size_t entityInsertPosition = 0;
    std::vector<size_t> freeEntitySlots;

    std::unordered_map<std::string, RawEntity*> entityNames;

    // Keeps track of allocations made in get_component_group so that the ECS can be reset and not leak memory
    std::vector<void*> allocations;
    // Stores the locations of the static variable in get_component_group so that the ECS can be reset
    std::vector<size_t*> staticCgids;

    std::vector<System*> systems;
    // Systems that should be run after the other systems
    std::vector<System*> updateLastSystems;
};

// User friendly wrapper around RawEntity
class Entity {
public:
    Entity(size_t eid);

    template <typename T, class... Args>
    void add_component(Args&&... args)
    {
        ComponentGroup* cg = entityData->get_component_group<T>();
        ComponentGroupData<T>* cgd = (ComponentGroupData<T>*)cg->cgd;
        //Check that this entity doesn't already have this component
        if (entity->activeComponents.test(cg->cgid) == true) {
            throw std::runtime_error("This entity already has the given component type");
        }

        cgd->components[entity->eid] = T(std::forward<Args>(args)...);

        entity->activeComponents.set(cg->cgid, true);
    }

    template <typename T>
    std::optional<T*> get_component()
    {
        ComponentGroup* cg = entityData->get_component_group<T>();
        ComponentGroupData<T>* cgd = (ComponentGroupData<T>*)cg->cgd;
        if (entity->activeComponents.test(cg->cgid) == false) {
            return std::optional<T*>();
        }

        return &cgd->components[entity->eid];
    }

    template <typename T>
    void remove_component()
    {
        ComponentGroup* cg = entityData->get_component_group<T>();
        ComponentGroupData<T>* cgd = (ComponentGroupData<T>*)cg->cgd;
        // Make sure that the entity does have this component
        if (entity->activeComponents.test(cg->cgid) == false) {
            throw std::runtime_error("Cannot remove component that has not been added");
        }

        entity->activeComponents.set(cg->cgid, false);
    }

    int get_eid()
    {
        return entity->eid;
    }

private:
    EntityData* entityData = nullptr;
    RawEntity* entity = nullptr;

    friend class EntityManager;
};

// User friendly wrapper around ECS data types. Singleton class because entityData also is.
class EntityManager {
public:
    EntityManager();

    Entity add_entity(std::string entityName = std::string());
    void remove_entity(Entity e);
    void remove_entity(std::string entityName);
    Entity get_entity_by_name(std::string entityName);
    void update(double dt_ms);

    void clear();

    // Adds a new system to the Entity Manager. Returns a pointer to the constructed system
    template <typename T, class... Args>
    T& add_system(Args&&... args)
    {
        T* system = new T(std::forward<Args>(args)...);
        entityData->systems.push_back((System*)system);

        system->init();

        return *system;
    }

    // Adds a system that gets run after other systems
    template <typename T, class... Args>
    void add_update_last_system(Args&&... args)
    {
        T* system = new T(std::forward<Args>(args)...);
        entityData->updateLastSystems.push_back((System*)system);

        system->init();
    }

    // Runs the given function on each component of the type provided by the template parameter.
    // Provides the entity associated with that component as well as the component itself.
    template <typename T>
    void each_component(std::function<void(Entity&, T*)> f)
    {
        ComponentGroup* cg = entityData->get_component_group<T>();
        ComponentGroupData<T>* cgd = (ComponentGroupData<T>*)cg->cgd;
        for (int i = 0; i < entityData->entityInsertPosition; ++i) {
            // If the entity and component are both active
            if (entityData->entities[i].active && entityData->entities[i].activeComponents.test(cg->cgid)) {
                Entity e(i);
                f(e, &cgd->components[i]);
            }
        }
    }

    // Singleton
    static EntityManager& getInstance();
    EntityManager(EntityManager&) = delete;
    void operator=(EntityManager const&) = delete;

private:
    EntityData* entityData = nullptr;
};
}