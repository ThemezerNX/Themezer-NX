// Themezer's catalogue: packs, themes and splash screens in tabs, a grid of them a page at a time (the next page loads
// as the grid nears its end), sorted, filtered and searched.

#pragma once

#include "scenes/frame.hpp"
#include "services/themezer.hpp"
#include "sys/jobs.hpp"
#include "ui/grid_view.hpp"
#include "ui/scene.hpp"
#include "ui/widgets.hpp"

#include <string>
#include <vector>

namespace tz::scenes
{

class BrowseScene final : public ui::Scene
{
public:
    BrowseScene();
    ~BrowseScene() override;
    void update(ui::Input& input, float dt) override;
    void tick(float dt) override;
    void draw() override;

private:
    enum class State : uint8_t
    {
        Loading,
        Ready,
        Failed,
    };

    void reload();
    void fetchMore();
    void search();
    void openSort();
    void openFilter();
    void chooseSort();
    void chooseTarget();
    std::vector<ui::Hint> hints() const;
    gfx::Accent accent() const;
    void drawFilters(float top);

    themezer::Query query_;
    std::vector<themezer::Item> items_;
    int pageCount_ = 1;
    int itemCount_ = 0;
    State state_ = State::Loading;
    bool fetching_ = false;
    std::string error_;
    std::string moreError_;
    CancelToken token_;

    ui::GridView grid_;
    ui::TabBar tabs_;
    ui::HintBar hintBar_;
    Backdrop backdrop_;
    float contentFade_ = 0; // a new list fades in
    float filtersRight_ = 0;
};

} // namespace tz::scenes
