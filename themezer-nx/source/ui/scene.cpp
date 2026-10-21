#include "ui/scene.hpp"

namespace tz::ui
{

void SceneStack::push(std::unique_ptr<Scene> scene)
{
    // scenes pushed while the stack updates join it after the update, so they start with the next frame's input
    pending_.push_back(std::move(scene));
}

void SceneStack::update(Input& input, float dt)
{
    for (auto& scene : pending_)
        scenes_.push_back(std::move(scene));
    pending_.clear();

    for (size_t i = 0; i < scenes_.size(); ++i)
        scenes_[i]->tick(dt);
    if (Scene* scene = top(); scene && !scene->closed())
        scene->update(input, dt);

    // close what asked to be closed, and tell the scene that comes back
    bool removed = false;
    for (size_t i = scenes_.size(); i-- > 0;)
    {
        if (scenes_[i]->closed())
        {
            scenes_.erase(scenes_.begin() + static_cast<std::ptrdiff_t>(i));
            removed = true;
        }
    }
    if (removed && pending_.empty())
    {
        if (Scene* scene = top())
            scene->onResume();
    }
}

size_t SceneStack::firstVisible() const
{
    for (size_t i = scenes_.size(); i-- > 0;)
    {
        if (scenes_[i]->covers())
            return i;
    }
    return 0;
}

void SceneStack::draw()
{
    for (size_t i = firstVisible(); i < scenes_.size(); ++i)
        scenes_[i]->draw();
}

void SceneStack::clear()
{
    pending_.clear();
    while (!scenes_.empty())
        scenes_.pop_back();
}

} // namespace tz::ui
