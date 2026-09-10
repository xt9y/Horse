#ifndef HORSE_RENDERER_MANAGER_HPP
#define HORSE_RENDERER_MANAGER_HPP

#include "Renderer/Renderer.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Renderer {

class Manager {
public:
    struct Entry {
        std::string name;
        std::unique_ptr<IRenderer> renderer;
        bool available = false;
    };

    template <typename T, typename... Args>
    T& add(std::string name, Args&&... args)
    {
        auto renderer = std::make_unique<T>(std::forward<Args>(args)...);
        T& reference = *renderer;
        entries_.push_back(Entry{std::move(name), std::move(renderer), false});
        return reference;
    }

    bool initialize()
    {
        shutdown();
        active_ = invalidIndex();

        for (std::size_t index = 0u; index < entries_.size(); ++index) {
            Entry& entry = entries_[index];
            entry.available = entry.renderer && entry.renderer->init();
            if (!entry.available) continue;
            entry.renderer->deactivate();
            if (active_ == invalidIndex()) active_ = index;
        }

        if (active_ == invalidIndex()) return false;
        return activate(active_);
    }

    void shutdown()
    {
        if (Entry *entry = activeEntry()) entry->renderer->deactivate();
        for (auto iterator = entries_.rbegin(); iterator != entries_.rend(); ++iterator) {
            if (iterator->renderer) iterator->renderer->shutdown();
            iterator->available = false;
        }
        active_ = invalidIndex();
    }

    bool activate(std::size_t index)
    {
        if (index >= entries_.size() || !entries_[index].available) return false;
        if (active_ == index) return entries_[index].renderer->activate();

        const std::size_t previous = active_;
        if (Entry *entry = activeEntry()) entry->renderer->deactivate();

        if (!entries_[index].renderer->activate()) {
            if (previous < entries_.size() && entries_[previous].available &&
                entries_[previous].renderer->activate())
            {
                active_ = previous;
            } else {
                active_ = invalidIndex();
            }
            return false;
        }

        active_ = index;
        return true;
    }

    bool activate(std::string_view name)
    {
        for (std::size_t index = 0u; index < entries_.size(); ++index) {
            if (entries_[index].name == name) return activate(index);
        }
        return false;
    }

    bool next()
    {
        if (entries_.empty()) return false;
        const std::size_t start = active_ < entries_.size() ? active_ : entries_.size() - 1u;
        for (std::size_t step = 1u; step <= entries_.size(); ++step) {
            const std::size_t index = (start + step) % entries_.size();
            if (entries_[index].available) return activate(index);
        }
        return false;
    }

    void resize(int width, int height)
    {
        for (Entry& entry : entries_) {
            if (entry.available) entry.renderer->resize(width, height);
        }
    }

    void render(const Ecs::World& world)
    {
        Entry *entry = activeEntry();
        if (entry && entry->available) entry->renderer->render(world);
    }

    std::size_t count() const { return entries_.size(); }
    std::size_t activeIndex() const { return active_; }

    Entry *entry(std::size_t index)
    {
        return index < entries_.size() ? &entries_[index] : nullptr;
    }

    const Entry *entry(std::size_t index) const
    {
        return index < entries_.size() ? &entries_[index] : nullptr;
    }

    Entry *activeEntry() { return entry(active_); }
    const Entry *activeEntry() const { return entry(active_); }

    IRenderer *active()
    {
        Entry *value = activeEntry();
        return value ? value->renderer.get() : nullptr;
    }

    const IRenderer *active() const
    {
        const Entry *value = activeEntry();
        return value ? value->renderer.get() : nullptr;
    }

    template <typename T>
    T *find()
    {
        for (Entry& entry : entries_) {
            if (auto *value = dynamic_cast<T *>(entry.renderer.get())) return value;
        }
        return nullptr;
    }

private:
    static constexpr std::size_t invalidIndex()
    {
        return static_cast<std::size_t>(-1);
    }

    std::vector<Entry> entries_;
    std::size_t active_ = invalidIndex();
};

} // namespace Renderer

#endif
