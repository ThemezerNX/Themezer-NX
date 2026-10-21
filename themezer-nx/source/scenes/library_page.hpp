// The SD card's pages, as Themezer's: the packs (the folders of themes), the themes and the splashes on the SD card,
// sorted, filtered and searched.

#pragma once

#include "domain/library.hpp"
#include "scenes/frame.hpp"
#include "scenes/page.hpp"
#include "ui/grid_view.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace tz::scenes
{

class LibraryPage final : public Page
{
public:
    explicit LibraryPage(themezer::Kind kind);
    void onShow() override;
    void tick(float dt) override;
    void update(ui::Input& input, float dt) override;
    void draw() override;
    std::vector<ui::Hint> hints() const override;
    gfx::Accent accent() const override;
    std::shared_ptr<gfx::Picture> backdrop() const override;

private:
    enum class Sort : uint8_t
    {
        Newest,
        Name,
        Author,
    };

    void rebuild();
    // What a row shows: a theme or a splash, or a pack's cover; and on the packs' page, the pack.
    const library::Entry* entryAt(int row) const;
    const local::Pack* packAt(int row) const;
    // What a row is, to find it again after a scan: its file, or its pack's folder.
    std::string keyAt(int row) const;
    void showTarget(int target);
    void openFilter();
    void chooseSort();
    void chooseTarget();
    void search();
    void drawFilters(float top);

    themezer::Kind kind_;
    std::vector<size_t> rows_; // the Library's entries shown, or its packs, in order
    std::string selected_;     // the selected row's key, from before the Library changed
    uint32_t generation_ = UINT32_MAX;
    Sort sort_ = Sort::Newest;
    int target_ = -1;
    std::string search_;
    ui::GridView grid_;

    enum class Filter : uint8_t
    {
        Sort,
        Target,
        Search,
    };
    struct Chip
    {
        float x, width;
        Filter filter;
    };
    std::vector<Chip> chips_;
};

} // namespace tz::scenes
