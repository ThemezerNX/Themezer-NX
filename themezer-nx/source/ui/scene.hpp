// The screens of the app, one over another, as Themezer-3DS stacks its own.

#pragma once

#include "ui/input.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace tz::ui
{

// A screen of the app. Only the scene on top gets input; overlays (dialogs, sheets) let the scenes under them show
// through.
class Scene
{
public:
    virtual ~Scene() = default;
    virtual void update(Input& input, float dt) = 0;
    virtual void draw() = 0;
    // False for overlays: the scenes under them are drawn first.
    virtual bool covers() const { return true; }
    // Called when the scene above this one has closed.
    virtual void onResume() {}
    // Called every frame, also when covered, for things that keep going (loading, animations).
    virtual void tick(float /*dt*/) {}
    // False while it's on its way out: the input goes to the scene under it.
    virtual bool acceptsInput() const { return true; }
    // Where it is, for the breadcrumb of the screens opened from it: "Pixel Forest". Empty for overlays.
    virtual std::string crumb() const { return {}; }

    bool closed() const { return closed_; }
    void close() { closed_ = true; }

private:
    bool closed_ = false;
};

class SceneStack
{
public:
    void push(std::unique_ptr<Scene> scene);
    template <class T, class... Args>
    T& emplace(Args&&... args)
    {
        auto scene = std::make_unique<T>(std::forward<Args>(args)...);
        T& ref = *scene;
        push(std::move(scene));
        return ref;
    }

    void update(Input& input, float dt);
    void draw();
    bool empty() const { return scenes_.empty() && pending_.empty(); }
    size_t size() const { return scenes_.size(); }
    Scene* top() const { return scenes_.empty() ? nullptr : scenes_.back().get(); }
    // The scenes' crumbs, the bottom one first: where a scene opened now is.
    std::vector<std::string> trail() const;
    void clear();

private:
    size_t firstVisible() const;

    std::vector<std::unique_ptr<Scene>> scenes_;
    std::vector<std::unique_ptr<Scene>> pending_;
};

} // namespace tz::ui
