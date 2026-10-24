// The screens of the app, one over another, as Themezer-3DS stacks its own.

#pragma once

#include "gfx/draw.hpp"
#include "gfx/palette.hpp"
#include "ui/input.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace tz::ui
{

// A part of where a screen is, for its top bar: the mode and the section, which look as the home screen's controls do,
// then the pages opened from them.
struct Crumb
{
    enum class Kind : uint8_t
    {
        Mode,    // the SD card or Themezer
        Section, // packs, themes, splashes
        Page,
    };
    Kind kind = Kind::Page;
    std::string text;
    int value = 0; // a mode: 1 online, 0 the SD card; a section: its themezer::Kind
    gfx::Icon icon = gfx::Icon::Count;
    gfx::Accent accent{};
};

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
    // Where it is, itself last: the trail of the screens opened from it. Empty for overlays.
    virtual std::vector<Crumb> crumbs() const { return {}; }

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
    // Where a scene opened now is: the crumbs of the top one that has them.
    std::vector<Crumb> trail() const;
    // Closes the scenes over this one, and drops the ones about to open.
    void closeAbove(const Scene* scene);
    void clear();

private:
    size_t firstVisible() const;

    std::vector<std::unique_ptr<Scene>> scenes_;
    std::vector<std::unique_ptr<Scene>> pending_;
};

} // namespace tz::ui
