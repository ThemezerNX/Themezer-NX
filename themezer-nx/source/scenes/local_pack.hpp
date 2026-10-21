// A pack's folder on the SD card, as Themezer shows a pack: its Home Menu's picture and what it is beside a grid of
// its themes, and installing them all, or deleting it.

#pragma once

#include "scenes/frame.hpp"
#include "ui/grid_view.hpp"
#include "ui/scene.hpp"
#include "ui/widgets.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace tz::scenes
{

class LocalPackScene final : public ui::Scene
{
public:
    explicit LocalPackScene(std::string dir);
    void update(ui::Input& input, float dt) override;
    void tick(float dt) override;
    void draw() override;
    bool acceptsInput() const override { return !leaving_; }

private:
    void rebuild();
    const library::Entry* cover() const;
    const library::Entry* themeAt(int index) const;
    std::vector<ui::Hint> hints() const;
    void openMore();

    std::string dir_;
    local::Pack last_;                // what it was, drawn while the page goes after the folder has
    std::vector<std::string> themes_; // its themes' files, in the parts' order
    uint32_t generation_ = UINT32_MAX;
    ui::GridView grid_;
    ui::HintBar hintBar_;
    Backdrop backdrop_;
    float appear_ = 0;
    bool leaving_ = false;
};

} // namespace tz::scenes
