// One kind of Themezer's catalogue: packs, themes or splash screens in a grid, a page at a time (the next page loads as
// the grid nears its end), sorted, filtered and searched.

#pragma once

#include "scenes/frame.hpp"
#include "scenes/page.hpp"
#include "services/themezer.hpp"
#include "sys/jobs.hpp"
#include "ui/grid_view.hpp"

#include <string>
#include <vector>

namespace tz::scenes
{

class BrowsePage final : public Page
{
public:
    explicit BrowsePage(themezer::Kind kind);
    ~BrowsePage() override;

    void onShow() override;
    void tick(float dt) override;
    void update(ui::Input& input, float dt) override;
    void draw() override;
    std::vector<ui::Hint> hints() const override;
    gfx::Accent accent() const override;
    std::shared_ptr<gfx::Picture> backdrop() const override;

private:
    enum class State : uint8_t
    {
        NotLoaded,
        Loading,
        Ready,
        Failed,
    };

    void reload();
    void fetchMore();
    void search();
    void openFilter();
    void chooseSort();
    void chooseTarget();
    void choosePacked();
    void drawFilters(float top);

    themezer::Query query_;
    std::vector<themezer::Item> items_;
    int pageCount_ = 1;
    int itemCount_ = 0;
    State state_ = State::NotLoaded;
    bool fetching_ = false;
    std::string error_;
    std::string moreError_;
    CancelToken token_;

    ui::GridView grid_;
    float contentFade_ = 0; // a new list fades in

    // the chips under the tabs, where the last frame drew them
    enum class Filter : uint8_t
    {
        Sort,
        Target,
        Packed,
        Search,
    };
    struct Chip
    {
        float x, width;
        Filter filter;
    };
    std::vector<Chip> chips_;
    int chipFocus_ = -1; // the chip the keys are on, -1 while they're on the grid
    void openChip(Filter filter);
};

} // namespace tz::scenes
