#ifndef RW_ENGINE_ECS_HPP
#define RW_ENGINE_ECS_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Ecs {

using Entity = std::uint32_t;
constexpr Entity INVALID_ENTITY = UINT32_MAX;

enum class ChangeKind : std::uint8_t {
    Structure,
    Transform,
    Resource,
    Camera,
    Lighting,
    Animation,
    Audio,
    Count,
};

class World {
private:
    static constexpr std::size_t missing_entity = std::numeric_limits<std::size_t>::max();

    struct StorageBase {
        virtual ~StorageBase() = default;
        virtual void remove(Entity entity) = 0;
    };

    template <typename T>
    class Storage final : public StorageBase {
    public:
        static constexpr std::size_t missing = std::numeric_limits<std::size_t>::max();

        template <typename... Args>
        T& add(Entity entity, Args&&... args)
        {
            ensureSparse(entity);
            const std::size_t existing = sparse_[entity];
            if (existing != missing) {
                dense_components_[existing] = T(std::forward<Args>(args)...);
                return dense_components_[existing];
            }

            const std::size_t index = dense_components_.size();
            sparse_[entity] = index;
            dense_entities_.push_back(entity);
            dense_components_.emplace_back(std::forward<Args>(args)...);
            return dense_components_.back();
        }

        T *get(Entity entity)
        {
            if (entity >= sparse_.size()) return nullptr;
            const std::size_t index = sparse_[entity];
            return index == missing ? nullptr : &dense_components_[index];
        }

        const T *get(Entity entity) const
        {
            if (entity >= sparse_.size()) return nullptr;
            const std::size_t index = sparse_[entity];
            return index == missing ? nullptr : &dense_components_[index];
        }

        bool has(Entity entity) const
        {
            return entity < sparse_.size() && sparse_[entity] != missing;
        }

        void remove(Entity entity) override
        {
            if (!has(entity)) return;

            const std::size_t index = sparse_[entity];
            const std::size_t last = dense_components_.size() - 1u;
            if (index != last) {
                dense_components_[index] = std::move(dense_components_[last]);
                dense_entities_[index] = dense_entities_[last];
                sparse_[dense_entities_[index]] = index;
            }

            dense_components_.pop_back();
            dense_entities_.pop_back();
            sparse_[entity] = missing;
        }

        const std::vector<Entity>& entities() const { return dense_entities_; }
        std::vector<T>& components() { return dense_components_; }
        const std::vector<T>& components() const { return dense_components_; }

    private:
        void ensureSparse(Entity entity)
        {
            const std::size_t size = static_cast<std::size_t>(entity) + 1u;
            if (sparse_.size() < size) sparse_.resize(size, missing);
        }

        std::vector<std::size_t> sparse_;
        std::vector<Entity> dense_entities_;
        std::vector<T> dense_components_;
    };

public:
    World();

    Entity createEntity();
    bool destroyEntity(Entity entity);

    bool alive(Entity entity) const
    {
        return entity < entity_sparse_.size() && entity_sparse_[entity] != missing_entity;
    }

    template <typename T, typename... Args>
    T& add(Entity entity, Args&&... args)
    {
        requireAlive(entity);
        T& component = storage<T>().add(entity, std::forward<Args>(args)...);
        touch(ChangeKind::Structure);
        return component;
    }

    template <typename T>
    T *get(Entity entity)
    {
        Storage<T> *value = findStorage<T>();
        return value ? value->get(entity) : nullptr;
    }

    template <typename T>
    const T *get(Entity entity) const
    {
        const Storage<T> *value = findStorage<T>();
        return value ? value->get(entity) : nullptr;
    }

    template <typename T>
    bool has(Entity entity) const
    {
        const Storage<T> *value = findStorage<T>();
        return value && value->has(entity);
    }

    template <typename T>
    bool remove(Entity entity)
    {
        Storage<T> *value = findStorage<T>();
        if (!value || !value->has(entity)) return false;
        value->remove(entity);
        touch(ChangeKind::Structure);
        return true;
    }

    template <typename First, typename... Rest, typename Fn>
    void each(Fn&& fn)
    {
        Storage<First> *first = findStorage<First>();
        if (!first) return;

        auto rest = std::tuple<Storage<Rest>*...>{findStorage<Rest>()...};
        const bool pools_exist = std::apply(
            [](auto *...pool) { return ((pool != nullptr) && ...); },
            rest
        );
        if (!pools_exist) return;

        const std::vector<Entity>& dense_entities = first->entities();
        std::vector<First>& dense_components = first->components();
        for (std::size_t i = 0; i < dense_entities.size(); ++i) {
            const Entity entity = dense_entities[i];
            const bool matches = std::apply(
                [entity](auto *...pool) { return (pool->has(entity) && ...); },
                rest
            );
            if (!matches) continue;

            std::apply(
                [&](auto *...pool) {
                    std::invoke(fn, entity, dense_components[i], *pool->get(entity)...);
                },
                rest
            );
        }
    }

    template <typename First, typename... Rest, typename Fn>
    void each(Fn&& fn) const
    {
        const Storage<First> *first = findStorage<First>();
        if (!first) return;

        auto rest = std::tuple<const Storage<Rest>*...>{findStorage<Rest>()...};
        const bool pools_exist = std::apply(
            [](const auto *...pool) { return ((pool != nullptr) && ...); },
            rest
        );
        if (!pools_exist) return;

        const std::vector<Entity>& dense_entities = first->entities();
        const std::vector<First>& dense_components = first->components();
        for (std::size_t i = 0; i < dense_entities.size(); ++i) {
            const Entity entity = dense_entities[i];
            const bool matches = std::apply(
                [entity](const auto *...pool) { return (pool->has(entity) && ...); },
                rest
            );
            if (!matches) continue;

            std::apply(
                [&](const auto *...pool) {
                    std::invoke(fn, entity, dense_components[i], *pool->get(entity)...);
                },
                rest
            );
        }
    }

    const std::vector<Entity>& entities() const { return entities_; }
    std::size_t size() const { return entities_.size(); }

    std::uint64_t changeRevision() const { return change_revision_; }
    std::uint64_t changeRevision(ChangeKind kind) const
    {
        const std::size_t index = static_cast<std::size_t>(kind);
        return index < change_revisions_.size() ? change_revisions_[index] : change_revision_;
    }

    void markChanged() { touchAll(); }
    void markChanged(ChangeKind kind) { touch(kind); }

private:
    template <typename T>
    Storage<T>& storage()
    {
        const std::type_index key(typeid(T));
        const auto found = storages_.find(key);
        if (found != storages_.end()) return *static_cast<Storage<T> *>(found->second.get());

        auto value = std::make_unique<Storage<T>>();
        Storage<T> *raw = value.get();
        storages_.emplace(key, std::move(value));
        return *raw;
    }

    template <typename T>
    Storage<T> *findStorage()
    {
        const auto found = storages_.find(std::type_index(typeid(T)));
        return found == storages_.end() ? nullptr : static_cast<Storage<T> *>(found->second.get());
    }

    template <typename T>
    const Storage<T> *findStorage() const
    {
        const auto found = storages_.find(std::type_index(typeid(T)));
        return found == storages_.end() ? nullptr : static_cast<const Storage<T> *>(found->second.get());
    }

    void requireAlive(Entity entity) const
    {
        if (!alive(entity)) throw std::out_of_range("ECS entity is not alive");
    }

    void touch(ChangeKind kind);
    void touchAll();

    Entity next_entity_ = 0u;
    std::vector<Entity> entities_;
    std::vector<std::size_t> entity_sparse_;
    std::unordered_map<std::type_index, std::unique_ptr<StorageBase>> storages_;
    std::uint64_t change_revision_ = 0u;
    std::array<std::uint64_t, static_cast<std::size_t>(ChangeKind::Count)> change_revisions_{};
};

} // namespace Ecs

#endif