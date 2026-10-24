#include "scenes/library_page.hpp"

#include "app/app.hpp"
#include "gfx/renderer.hpp"
#include "gfx/text.hpp"
#include "scenes/actions.hpp"
#include "services/local.hpp"
#include "sys/audio.hpp"
#include "ui/overlays.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace tz::scenes
{

using namespace gfx;
using themezer::Kind;

namespace
{

using page::kColumns;
using page::kFiltersHeight;
using page::kFiltersTop;
using page::kGap;
using page::kGridTop;
using page::kSide;

// thumbnails of what's on the SD card, as wide as a card shows them
constexpr int kThumbWidth = 480;

void layOut(ui::GridView& grid)
{
    grid.setBounds(kSide, kGridTop, metric::kWidth - 2 * kSide, page::kBottom - kGridTop);
    const float cellWidth = (metric::kWidth - 2 * kSide - kGap * (kColumns - 1)) / kColumns;
    grid.setLayout(kColumns, std::round((cellWidth - 20) * 9 / 16) + 20 + card::kTextHeight, kGap);
}

std::string lower(std::string text)
{
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

// What the page's rows are called: "3 packs", "a pack".
const char* nameOf(Kind kind, bool one)
{
    switch (kind)
    {
    case Kind::Packs:
        return one ? "pack" : "packs";
    case Kind::Splashes:
        return one ? "splash" : "splashes";
    case Kind::Themes:
        break;
    }
    return one ? "theme" : "themes";
}

} // namespace

LibraryPage::LibraryPage(Kind kind) : kind_(kind)
{
    layOut(grid_);
}

Accent LibraryPage::accent() const
{
    return accentOf(kind_);
}

void LibraryPage::onShow()
{
    rebuild();
}

void LibraryPage::tick(float)
{
    // a scan or a file's info.json read, also while a sheet is over the page
    if (generation_ != local::Library::get().generation())
        rebuild();
    selected_ = keyAt(grid_.selected());
}

void LibraryPage::showTarget(int target)
{
    target_ = target;
    search_.clear();
    rebuild();
    grid_.select(0, false);
}

const local::Pack* LibraryPage::packAt(int row) const
{
    const auto& packs = local::Library::get().packs();
    if (kind_ != Kind::Packs || row < 0 || row >= static_cast<int>(rows_.size()) ||
        rows_[static_cast<size_t>(row)] >= packs.size())
        return nullptr;
    return &packs[rows_[static_cast<size_t>(row)]];
}

const library::Entry* LibraryPage::entryAt(int row) const
{
    if (kind_ == Kind::Packs)
    {
        const local::Pack* pack = packAt(row);
        return pack ? local::Library::get().cover(*pack) : nullptr;
    }
    const auto& entries = local::Library::get().entries();
    if (row < 0 || row >= static_cast<int>(rows_.size()) || rows_[static_cast<size_t>(row)] >= entries.size())
        return nullptr;
    return &entries[rows_[static_cast<size_t>(row)]];
}

std::string LibraryPage::keyAt(int row) const
{
    if (const local::Pack* pack = packAt(row))
        return pack->dir;
    const library::Entry* entry = kind_ == Kind::Packs ? nullptr : entryAt(row);
    return entry ? entry->path : "";
}

void LibraryPage::rebuild()
{
    local::Library& library = local::Library::get();
    generation_ = library.generation();
    const std::string needle = lower(search_);
    const auto matches = [&](const std::string& name, const std::string& author, const std::string& file) {
        return needle.empty() || lower(name).find(needle) != std::string::npos ||
               lower(author).find(needle) != std::string::npos || lower(file).find(needle) != std::string::npos;
    };
    struct Row
    {
        size_t index;
        const std::string* name;
        const std::string* author;
        int64_t modified;
    };
    std::vector<Row> rows;
    if (kind_ == Kind::Packs)
    {
        const auto& packs = library.packs();
        for (size_t i = 0; i < packs.size(); ++i)
        {
            if (matches(packs[i].name, packs[i].author, packs[i].dir))
                rows.push_back({i, &packs[i].name, &packs[i].author, packs[i].modified});
        }
    }
    else
    {
        const auto kind = kind_ == Kind::Splashes ? library::Kind::Splash : library::Kind::Theme;
        const auto& entries = library.entries();
        for (size_t i = 0; i < entries.size(); ++i)
        {
            const library::Entry& entry = entries[i];
            if (entry.kind != kind)
                continue;
            if (kind_ == Kind::Themes && target_ >= 0 && entry.target != target_)
            {
                // a part not known yet is in its info.json: read it, it's sorted in when it's there
                if (entry.target < 0)
                    library.wantInfo(i);
                continue;
            }
            if (matches(entry.name, entry.author, entry.fileName))
                rows.push_back({i, &entry.name, &entry.author, entry.modified});
        }
    }
    std::stable_sort(rows.begin(), rows.end(), [&](const Row& x, const Row& y) {
        switch (sort_)
        {
        case Sort::Newest:
            if (x.modified != y.modified)
                return x.modified > y.modified;
            break;
        case Sort::Author:
            if (const std::string ax = lower(*x.author), ay = lower(*y.author); ax != ay)
                return ax < ay;
            break;
        case Sort::Name:
            break;
        }
        return lower(*x.name) < lower(*y.name);
    });
    rows_.clear();
    for (const Row& row : rows)
        rows_.push_back(row.index);
    grid_.setCount(static_cast<int>(rows_.size()));
    // the selection stays on its file, or its pack
    for (int row = 0; row < static_cast<int>(rows_.size()) && !selected_.empty(); ++row)
    {
        if (keyAt(row) == selected_)
        {
            grid_.select(row);
            break;
        }
    }
}

std::shared_ptr<Picture> LibraryPage::backdrop() const
{
    const library::Entry* entry = entryAt(grid_.selected());
    return entry ? localBackdrop(*entry) : nullptr;
}

void LibraryPage::search()
{
    const std::string guide = std::string("Search the ") + nameOf(kind_, false);
    const auto text = askText(guide.c_str(), search_);
    if (!text || *text == search_)
        return;
    search_ = *text;
    rebuild();
    grid_.select(0, false);
}

void LibraryPage::chooseSort()
{
    constexpr std::pair<Sort, const char*> kSorts[] = {
        {Sort::Newest, "Newest first"}, {Sort::Name, "Name A-Z"}, {Sort::Author, "Author A-Z"}};
    constexpr Icon kIcons[] = {Icon::CalendarPlus, Icon::Sort, Icon::User};
    std::vector<ui::Action> actions;
    int current = 0;
    for (size_t i = 0; i < std::size(kSorts); ++i)
    {
        const Sort sort = kSorts[i].first;
        if (sort == sort_)
            current = static_cast<int>(i);
        ui::Action action{kIcons[i], kSorts[i].second, "", true, false, [this, sort] {
                              sort_ = sort;
                              rebuild();
                          }};
        action.checked = sort == sort_;
        actions.push_back(std::move(action));
    }
    App::get().scenes().emplace<ui::ActionSheet>("Sort by", std::move(actions), accent().solid, current);
}

void LibraryPage::chooseTarget()
{
    std::vector<ui::Action> actions;
    for (int target = -1; target < themezer::kTargetCount; ++target)
    {
        ui::Action action{targetIcon(target), target < 0 ? "Every part" : themezer::targetLabel(target), "", true,
                          false, [this, target] { showTarget(target); }};
        action.checked = target_ == target;
        actions.push_back(std::move(action));
    }
    App::get().scenes().emplace<ui::ActionSheet>("Show themes for", std::move(actions), accent().solid, target_ + 1);
}

void LibraryPage::openFilter()
{
    if (kind_ != Kind::Themes)
    {
        chooseSort();
        return;
    }
    const char* sorts[] = {"Newest first", "Name A-Z", "Author A-Z"};
    std::vector<ui::Action> actions;
    actions.push_back({Icon::Sort, std::string("Sort: ") + sorts[static_cast<size_t>(sort_)], "", true, false,
                       [this] { chooseSort(); }});
    actions.push_back({targetIcon(target_),
                       std::string("Part of the HOME Menu: ") +
                           (target_ < 0 ? "Every part" : themezer::targetLabel(target_)),
                       "", true, false, [this] { chooseTarget(); }});
    App::get().scenes().emplace<ui::ActionSheet>("Sort and filter", std::move(actions), accent().solid);
}

std::vector<ui::Hint> LibraryPage::hints() const
{
    std::vector<ui::Hint> hints{{ui::key::kY, "Search"}, {ui::key::kX, "Sort"}};
    if (!search_.empty())
        hints.push_back({ui::key::kB, "Clear search"});
    if (chipFocus_ >= 0)
        hints.push_back({ui::key::kA, "Choose"});
    else if (!rows_.empty())
        hints.push_back({ui::key::kA, "Open"});
    return hints;
}

void LibraryPage::update(ui::Input& input, float dt)
{
    local::Library& library = local::Library::get();
    if (input.pressed(ui::key::kY))
    {
        sfx::play(sfx::Sound::Confirm);
        search();
        return;
    }
    if (input.pressed(ui::key::kX))
    {
        openFilter();
        return;
    }
    if (input.pressed(ui::key::kB) && !search_.empty())
    {
        sfx::play(sfx::Sound::Back);
        search_.clear();
        rebuild();
        return;
    }
    if (input.tap && input.y >= kFiltersTop && input.y < kFiltersTop + kFiltersHeight)
    {
        for (const Chip& chip : chips_)
        {
            if (input.x < chip.x || input.x >= chip.x + chip.width)
                continue;
            input.consumeTouch();
            openChip(chip.filter);
            return;
        }
    }
    if (chipFocus_ >= 0)
    {
        if (const int chosen = chipKeys(input, chipFocus_, static_cast<int>(chips_.size())); chosen >= 0)
            openChip(chips_[static_cast<size_t>(chosen)].filter);
        return;
    }
    const auto event = grid_.update(input, dt);
    if (event == ui::GridView::Event::Above && !chips_.empty())
    {
        chipFocus_ = 0;
        sfx::play(sfx::Sound::Cursor);
    }
    if (event == ui::GridView::Event::Activated)
    {
        if (const local::Pack* pack = packAt(grid_.selected()))
            openLocalPack(pack->dir);
        else if (const library::Entry* entry = kind_ == Kind::Packs ? nullptr : entryAt(grid_.selected()))
            openLocal(*entry);
    }
    // the names and parts of what's on screen, from inside the files
    if (kind_ == Kind::Packs)
        return;
    const auto [first, last] = grid_.visibleRange();
    for (int row = first; row <= last; ++row)
    {
        if (row >= 0 && row < static_cast<int>(rows_.size()))
            library.wantInfo(rows_[static_cast<size_t>(row)]);
    }
}

void LibraryPage::openChip(Filter filter)
{
    if (filter == Filter::Sort)
        chooseSort();
    else if (filter == Filter::Target)
        chooseTarget();
    else
        search();
}

void LibraryPage::drawFilters(float top)
{
    const float middle = top + kFiltersHeight / 2;
    float right = metric::kWidth - kSide;
    if (local::Library::get().scanned())
    {
        const int count = static_cast<int>(rows_.size());
        const std::string text = thousands(count) + " " + nameOf(kind_, count == 1) + " on the SD card";
        drawText(text, right, textTop(middle, metric::kTextSmall), metric::kTextSmall, color::kTextDim, Align::Right);
        right -= textWidth(text, metric::kTextSmall) + 24;
    }
    chips_.clear();
    float x = kSide;
    const auto chip = [&](Icon which, const std::string& text, bool highlighted, Filter filter) {
        if (const float width = filterChip(which, text, highlighted, accent(), x, top, kFiltersHeight, right); width > 0)
        {
            chips_.push_back({x, width, filter});
            x += width + 10;
        }
    };
    const char* sorts[] = {"Newest first", "Name A-Z", "Author A-Z"};
    chip(sort_ == Sort::Newest ? Icon::CalendarPlus : (sort_ == Sort::Author ? Icon::User : Icon::Sort),
         sorts[static_cast<size_t>(sort_)], false, Filter::Sort);
    if (kind_ == Kind::Themes)
        chip(targetIcon(target_), target_ < 0 ? "Every part" : themezer::targetLabel(target_), target_ >= 0,
             Filter::Target);
    if (!search_.empty())
        chip(Icon::Search, "“" + search_ + "”", true, Filter::Search);
    if (chipFocus_ >= 0 && chipFocus_ < static_cast<int>(chips_.size()))
        chipRing(chips_[static_cast<size_t>(chipFocus_)].x, top, chips_[static_cast<size_t>(chipFocus_)].width,
                 kFiltersHeight, accent());
}

void LibraryPage::draw()
{
    const Accent accent = this->accent();
    local::Library& library = local::Library::get();
    const bool showTarget = kind_ == Kind::Themes && target_ < 0;
    grid_.draw(
        [&](int index, float x, float y, float w, float h, bool selected) {
            card::Content content;
            const library::Entry* entry = entryAt(index);
            if (const local::Pack* pack = packAt(index))
            {
                const size_t count = pack->themes.size();
                content.name = pack->name;
                content.creator = pack->author;
                content.icon = Icon::Pack;
                content.chip = std::to_string(count) + (count == 1 ? " theme" : " themes");
                content.badge = badgeOf(*pack);
            }
            else if (entry)
            {
                content.name = entry->name;
                content.creator = entry->author.empty() ? entry->folder : entry->author;
                content.icon = kind_ == Kind::Splashes ? Icon::Splash : Icon::Theme;
                content.badge = badgeOf(*entry);
                if (showTarget && entry->target >= 0)
                    content.chip = themezer::targetLabel(entry->target);
            }
            else
            {
                return;
            }
            if (entry)
                content.picture = localPicture(*entry, kThumbWidth);
            card::draw(content, x, y, w, h, selected, accent);
        },
        [&](float x, float y, float w, float h, bool down) {
                if (chipFocus_ < 0)
                    card::highlight(x, y, w, h, down, accent);
            });

    if (!library.scanned())
    {
        spinner(metric::kWidth / 2, (kGridTop + page::kBottom) / 2, 32, accent.light);
    }
    else if (rows_.empty())
    {
        const bool filtered = !search_.empty() || target_ >= 0;
        const std::string what = nameOf(kind_, false);
        drawEmptyState(filtered ? Icon::SearchEmpty : Icon::SdCard, color::kTextDim,
                       filtered ? "No results" : "No " + what + " on the SD card yet",
                       filtered ? (search_.empty() ? std::string("There are none for ") +
                                                         themezer::targetLabel(target_) + " on the SD card."
                                                   : "Nothing matches “" + search_ + "”.")
                       : kind_ == Kind::Packs
                           ? std::string("The packs you download from Themezer show here, with any other folders of "
                                         "themes in /Themes.")
                           : "The " + what + " you download from Themezer show here, with any others in /Themes.",
                       kGridTop, page::kBottom);
    }
    drawFilters(kFiltersTop);
}

} // namespace tz::scenes
