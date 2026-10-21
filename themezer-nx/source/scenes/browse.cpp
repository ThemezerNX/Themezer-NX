#include "scenes/browse.hpp"

#include "app/app.hpp"
#include "domain/installs.hpp"
#include "gfx/renderer.hpp"
#include "gfx/text.hpp"
#include "scenes/actions.hpp"
#include "sys/audio.hpp"
#include "ui/overlays.hpp"

#include <switch.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace tz::scenes
{

using namespace gfx;
using ui::approach;
using ui::ease;
using themezer::Kind;

namespace
{

constexpr float kSide = 48;
constexpr float kFiltersTop = metric::kTopBarHeight + 2;
constexpr float kFiltersHeight = 44;
constexpr float kGridTop = kFiltersTop + kFiltersHeight + 18;
constexpr int kColumns = 4;
constexpr float kGap = 22;
constexpr float kFooter = 96;
// the next page loads once the selection is this many rows from the end
constexpr int kRowsAhead = 2;

constexpr Kind kKinds[] = {Kind::Packs, Kind::Themes, Kind::Splashes};

std::string keyboard(const char* guide, const std::string& initial)
{
    SwkbdConfig config;
    if (R_FAILED(swkbdCreate(&config, 0)))
        return initial;
    char out[129] = "";
    swkbdConfigMakePresetDefault(&config);
    swkbdConfigSetGuideText(&config, guide);
    swkbdConfigSetInitialText(&config, initial.c_str());
    swkbdConfigSetStringLenMax(&config, 128);
    const Result result = swkbdShow(&config, out, sizeof(out));
    swkbdClose(&config);
    if (R_FAILED(result))
        return initial; // cancelled: as it was
    std::string text = out;
    while (!text.empty() && text.back() == ' ')
        text.pop_back();
    while (!text.empty() && text.front() == ' ')
        text.erase(text.begin());
    return text;
}

// The number with thousands apart: 5,004.
std::string thousands(int number)
{
    std::string digits = std::to_string(number);
    for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3)
        digits.insert(static_cast<size_t>(i), ",");
    return digits;
}

const char* kindName(Kind kind, int count)
{
    switch (kind)
    {
    case Kind::Packs:
        return count == 1 ? "pack" : "packs";
    case Kind::Themes:
        return count == 1 ? "theme" : "themes";
    case Kind::Splashes:
        break;
    }
    return count == 1 ? "splash" : "splashes";
}

} // namespace

BrowseScene::BrowseScene()
{
    tabs_.setTabs({{"Packs", accent::kPacks, Icon::Pack},
                   {"Themes", accent::kThemes, Icon::Theme},
                   {"Splashes", accent::kSplashes, Icon::Splash}});
    grid_.setBounds(kSide, kGridTop, metric::kWidth - 2 * kSide, metric::kHeight - metric::kHintBarHeight - kGridTop);
    const float cellWidth = (metric::kWidth - 2 * kSide - kGap * (kColumns - 1)) / kColumns;
    grid_.setLayout(kColumns, std::round((cellWidth - 20) * 9 / 16) + 20 + card::kTextHeight, kGap);
    reload();
}

BrowseScene::~BrowseScene()
{
    token_.cancel();
}

Accent BrowseScene::accent() const
{
    return accentOf(query_.kind);
}

void BrowseScene::reload()
{
    token_.cancel();
    token_ = CancelToken{};
    items_.clear();
    grid_.setCount(0);
    grid_.setFooter(0);
    state_ = State::Loading;
    fetching_ = false;
    error_.clear();
    moreError_.clear();
    pageCount_ = 1;
    itemCount_ = 0;
    query_.page = 0;
    contentFade_ = 0;
    fetchMore();
}

void BrowseScene::fetchMore()
{
    if (fetching_ || (query_.page >= pageCount_ && query_.page > 0))
        return;
    fetching_ = true;
    moreError_.clear();
    themezer::Query query = query_;
    query.page = query_.page + 1;
    jobs::async<std::expected<themezer::Page, themezer::Error>>(
        jobs::Lane::Network, token_,
        [query](const CancelToken& token) { return themezer::fetchPage(query, token); },
        [this](std::expected<themezer::Page, themezer::Error> result) {
            fetching_ = false;
            if (!result)
            {
                std::printf("Page %d failed: %s (%s)\n", query_.page + 1, result.error().message.c_str(),
                            result.error().detail.c_str());
                if (items_.empty())
                {
                    state_ = State::Failed;
                    error_ = result.error().message;
                    sfx::play(sfx::Sound::Error);
                }
                else
                {
                    moreError_ = result.error().message;
                }
                return;
            }
            query_.page = result->page;
            pageCount_ = std::max(1, result->pageCount);
            itemCount_ = result->itemCount;
            for (themezer::Item& item : result->items)
                items_.push_back(std::move(item));
            state_ = State::Ready;
            grid_.setCount(static_cast<int>(items_.size()));
            grid_.setFooter(query_.page < pageCount_ ? kFooter : 0);
        });
}

void BrowseScene::search()
{
    const std::string text = keyboard("Search Themezer", query_.search);
    if (text == query_.search)
        return;
    query_.search = text;
    reload();
}

void BrowseScene::chooseSort()
{
    // each sort both ways round, as the website offers them; the one in use ticked
    std::vector<ui::Action> actions;
    int current = 0;
    for (int i = 0; i < static_cast<int>(themezer::Sort::Count); ++i)
    {
        const auto sort = static_cast<themezer::Sort>(i);
        for (bool ascending : {false, true})
        {
            const bool chosen = sort == query_.sort && ascending == query_.ascending;
            if (chosen)
                current = static_cast<int>(actions.size());
            ui::Action action{sortIcon(sort), themezer::sortLabel(sort, ascending), "", true, false,
                              [this, sort, ascending] {
                                  if (sort == query_.sort && ascending == query_.ascending)
                                      return;
                                  query_.sort = sort;
                                  query_.ascending = ascending;
                                  reload();
                              }};
            action.checked = chosen;
            actions.push_back(std::move(action));
        }
    }
    App::get().scenes().emplace<ui::ActionSheet>("Sort by", std::move(actions), accent().solid, current);
}

void BrowseScene::chooseTarget()
{
    std::vector<ui::Action> actions;
    for (int target = -1; target < themezer::kTargetCount; ++target)
    {
        ui::Action action{targetIcon(target), target < 0 ? "Every part" : themezer::targetLabel(target), "", true,
                          false, [this, target] {
                              if (query_.target == target)
                                  return;
                              query_.target = target;
                              reload();
                          }};
        action.checked = query_.target == target;
        actions.push_back(std::move(action));
    }
    App::get().scenes().emplace<ui::ActionSheet>("Show themes for", std::move(actions), accent().solid,
                                                 query_.target + 1);
}

void BrowseScene::openFilter()
{
    // packs and splashes are only sorted: their sorts straight away
    if (query_.kind != Kind::Themes)
    {
        chooseSort();
        return;
    }
    std::vector<ui::Action> actions;
    actions.push_back({sortIcon(query_.sort), std::string("Sort: ") + themezer::sortLabel(query_.sort, query_.ascending),
                       "", true, false, [this] { chooseSort(); }});
    actions.push_back({targetIcon(query_.target),
                       std::string("Part of the HOME Menu: ") +
                           (query_.target < 0 ? "Every part" : themezer::targetLabel(query_.target)),
                       "", true, false, [this] { chooseTarget(); }});
    App::get().scenes().emplace<ui::ActionSheet>("Sort and filter", std::move(actions), accent().solid);
}

std::vector<ui::Hint> BrowseScene::hints() const
{
    std::vector<ui::Hint> hints{{ui::key::kMinus, "Queue"}, {ui::key::kPlus, "Menu"}};
    if (state_ == State::Failed)
    {
        hints.push_back({ui::key::kA, "Try again"});
        return hints;
    }
    hints.push_back({ui::key::kY, "Search"});
    hints.push_back({ui::key::kX, "Sort"});
    if (!query_.search.empty())
        hints.push_back({ui::key::kB, "Clear search"});
    if (!items_.empty())
        hints.push_back({ui::key::kA, "Open"});
    return hints;
}

void BrowseScene::tick(float dt)
{
    backdrop_.tick(dt);
    approach(contentFade_, state_ == State::Ready ? 1.0f : 0.0f, dt, 8);
    if (!items_.empty())
    {
        const themezer::Item& item = items_[static_cast<size_t>(grid_.selected())];
        backdrop_.set(placeholderOf(item), accent().solid);
    }
    else
    {
        backdrop_.set(nullptr, accent().solid);
    }
}

void BrowseScene::update(ui::Input& input, float dt)
{
    const auto shown = hints();
    hintBar_.update(input, shown);
    if (tabs_.update(input))
    {
        query_.kind = kKinds[tabs_.selected()];
        reload();
        return;
    }
    if (input.pressed(ui::key::kPlus))
    {
        openMenu();
        return;
    }
    if (input.pressed(ui::key::kMinus))
    {
        openQueue();
        return;
    }
    if (state_ == State::Failed)
    {
        if (input.pressed(ui::key::kA))
        {
            sfx::play(sfx::Sound::Confirm);
            reload();
        }
        return;
    }
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
    if (input.tap && input.y >= kFiltersTop && input.y < kFiltersTop + kFiltersHeight)
    {
        for (const Chip& chip : chips_)
        {
            if (input.x < chip.x || input.x >= chip.x + chip.width)
                continue;
            input.consumeTouch();
            if (chip.filter == Filter::Sort)
                chooseSort();
            else if (chip.filter == Filter::Target)
                chooseTarget();
            else
                search();
            return;
        }
    }
    if (input.pressed(ui::key::kB) && !query_.search.empty())
    {
        sfx::play(sfx::Sound::Back);
        query_.search.clear();
        reload();
        return;
    }

    if (grid_.update(input, dt) == ui::GridView::Event::Activated && !items_.empty())
        openItem(items_[static_cast<size_t>(grid_.selected())]);

    // near the end: the next page, or again after it failed when the selection moves on to the end
    const int rowsLeft = (static_cast<int>(items_.size()) - 1) / kColumns - grid_.selected() / kColumns;
    const auto [first, last] = grid_.visibleRange();
    if (state_ == State::Ready && moreError_.empty() &&
        (rowsLeft <= kRowsAhead || last >= static_cast<int>(items_.size()) - kColumns))
        fetchMore();
    if (!moreError_.empty() && input.pressed(ui::key::kA) && rowsLeft == 0)
        fetchMore();
}

void BrowseScene::drawFilters(float top)
{
    // what the list is: its sort, the part of the HOME Menu, the search; and how much there is. A chip tapped changes it.
    const float middle = top + kFiltersHeight / 2;
    float right = metric::kWidth - kSide;
    if (state_ == State::Ready)
    {
        const std::string count = thousands(itemCount_) + " " + kindName(query_.kind, itemCount_);
        drawText(count, right, textTop(middle, metric::kTextSmall), metric::kTextSmall, color::kTextDim, Align::Right);
        right -= textWidth(count, metric::kTextSmall) + 24;
    }
    chips_.clear();
    float x = kSide;
    const float iconSize = metric::kIcon * 0.75f;
    const auto chip = [&](Icon which, const std::string& text, bool highlighted, Filter filter) {
        const float room = right - x - (iconSize + 10 + 32);
        if (room < 40)
            return;
        const std::string shown = ellipsize(text, room, metric::kTextSmall);
        const float width = iconSize + 10 + textWidth(shown, metric::kTextSmall) + 32;
        roundedRect(x, top, width, kFiltersHeight, kFiltersHeight / 2,
                    highlighted ? withAlpha(accent().solid, 0.35f) : color::kSurface);
        icon(which, x + 16, std::round(middle - iconSize / 2), iconSize, highlighted ? accent().light : color::kTextDim);
        drawText(shown, x + 16 + iconSize + 10, textTop(middle, metric::kTextSmall), metric::kTextSmall, color::kText);
        chips_.push_back({x, width, filter});
        x += width + 10;
    };
    chip(sortIcon(query_.sort), themezer::sortLabel(query_.sort, query_.ascending), false, Filter::Sort);
    if (query_.kind == Kind::Themes)
        chip(targetIcon(query_.target), query_.target < 0 ? "Every part" : themezer::targetLabel(query_.target),
             query_.target >= 0, Filter::Target);
    if (!query_.search.empty())
        chip(Icon::Search, "“" + query_.search + "”", true, Filter::Search);
}

void BrowseScene::draw()
{
    backdrop_.draw(0.62f);
    const Accent accent = this->accent();
    const bool showTarget = query_.kind == Kind::Themes && query_.target < 0;

    {
        const Layer layer(0, (1 - ease(contentFade_)) * 24, ease(contentFade_));
        grid_.draw(
            [&](int index, float x, float y, float w, float h, bool selected) {
                card::draw(items_[static_cast<size_t>(index)], x, y, w, h, selected, accent, showTarget);
            },
            [&](float x, float y, float w, float h, bool down) { card::highlight(x, y, w, h, down, accent); },
            [&](float x, float y, float w, float h) {
                const float middle = y + h / 2;
                if (!moreError_.empty())
                {
                    drawText(moreError_, x + w / 2, textTop(middle - 16, metric::kTextSmall), metric::kTextSmall,
                             color::kTextDim, Align::Center, w);
                    const float width = ui::hintWidth(ui::key::kA, "Try again");
                    ui::drawHint(ui::key::kA, "Try again", x + (w - width) / 2, middle + 20, color::kText,
                                 color::kTextDim);
                }
                else
                {
                    spinner(x + w / 2, middle, 20, accent.light);
                }
            });
    }

    if (state_ == State::Loading)
    {
        spinner(metric::kWidth / 2, (kGridTop + metric::kHeight - metric::kHintBarHeight) / 2, 32, accent.light);
    }
    else if (state_ == State::Failed)
    {
        drawEmptyState(Icon::WifiOff, color::kTextDim, "Couldn't load Themezer", error_, kGridTop,
                       metric::kHeight - metric::kHintBarHeight);
    }
    else if (items_.empty())
    {
        drawEmptyState(Icon::SearchEmpty, color::kTextDim, query_.search.empty() ? "Nothing here" : "No results",
                       query_.search.empty() ? "There's nothing of this kind yet."
                                             : "Nothing matches “" + query_.search + "”. Try other words.",
                       kGridTop, metric::kHeight - metric::kHintBarHeight);
    }

    const float statusLeft = drawTopBar();
    const float barHeight = 52;
    const float barWidth = std::min(tabs_.naturalWidth(metric::kTextNormal), statusLeft - 260);
    tabs_.draw(std::round((metric::kWidth - barWidth) / 2), std::round((metric::kTopBarHeight - barHeight) / 2),
               barWidth, barHeight, metric::kTextNormal);
    drawFilters(kFiltersTop);

    // the queue's size on its key
    auto shown = hints();
    if (const int queued = installs::queuedCount(); queued > 0)
        shown.front().label = "Queue (" + std::to_string(queued) + ")";
    hintBar_.draw(shown, 1.0f, accent.solid);
}

} // namespace tz::scenes
