#include "scenes/browse.hpp"

#include "app/app.hpp"
#include "domain/installs.hpp"
#include "gfx/renderer.hpp"
#include "gfx/text.hpp"
#include "scenes/actions.hpp"
#include "sys/audio.hpp"
#include "ui/overlays.hpp"

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

using page::kColumns;
using page::kFiltersHeight;
using page::kFiltersTop;
using page::kGap;
using page::kGridTop;
using page::kSide;
constexpr float kFooter = 96;
// the next page loads once the selection is this many rows from the end
constexpr int kRowsAhead = 2;

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

BrowsePage::BrowsePage(Kind kind)
{
    query_.kind = kind;
    grid_.setBounds(kSide, kGridTop, metric::kWidth - 2 * kSide, page::kBottom - kGridTop);
    const float cellWidth = (metric::kWidth - 2 * kSide - kGap * (kColumns - 1)) / kColumns;
    grid_.setLayout(kColumns, std::round((cellWidth - 20) * 9 / 16) + 20 + card::kTextHeight, kGap);
}

BrowsePage::~BrowsePage()
{
    token_.cancel();
}

void BrowsePage::onShow()
{
    // the first time it's shown, it loads
    if (state_ == State::NotLoaded)
        reload();
}

Accent BrowsePage::accent() const
{
    return accentOf(query_.kind);
}

std::shared_ptr<Picture> BrowsePage::backdrop() const
{
    return items_.empty() ? nullptr : placeholderOf(items_[static_cast<size_t>(grid_.selected())]);
}

void BrowsePage::reload()
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

void BrowsePage::fetchMore()
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

void BrowsePage::search()
{
    const auto text = askText("Search Themezer", query_.search);
    if (!text || *text == query_.search)
        return;
    query_.search = *text;
    reload();
}

void BrowsePage::chooseSort()
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

void BrowsePage::chooseTarget()
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

void BrowsePage::openFilter()
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

std::vector<ui::Hint> BrowsePage::hints() const
{
    std::vector<ui::Hint> hints;
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

void BrowsePage::tick(float dt)
{
    approach(contentFade_, state_ == State::Ready ? 1.0f : 0.0f, dt, 8);
}

void BrowsePage::update(ui::Input& input, float dt)
{
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

void BrowsePage::drawFilters(float top)
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
    const auto chip = [&](Icon which, const std::string& text, bool highlighted, Filter filter) {
        if (const float width = filterChip(which, text, highlighted, accent(), x, top, kFiltersHeight, right); width > 0)
        {
            chips_.push_back({x, width, filter});
            x += width + 10;
        }
    };
    chip(sortIcon(query_.sort), themezer::sortLabel(query_.sort, query_.ascending), false, Filter::Sort);
    if (query_.kind == Kind::Themes)
        chip(targetIcon(query_.target), query_.target < 0 ? "Every part" : themezer::targetLabel(query_.target),
             query_.target >= 0, Filter::Target);
    if (!query_.search.empty())
        chip(Icon::Search, "“" + query_.search + "”", true, Filter::Search);
}

void BrowsePage::draw()
{
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
        spinner(metric::kWidth / 2, (kGridTop + page::kBottom) / 2, 32, accent.light);
    }
    else if (state_ == State::Failed)
    {
        drawEmptyState(Icon::WifiOff, color::kTextDim, "Couldn't load Themezer", error_, kGridTop,
                       page::kBottom);
    }
    else if (items_.empty())
    {
        drawEmptyState(Icon::SearchEmpty, color::kTextDim, query_.search.empty() ? "Nothing here" : "No results",
                       query_.search.empty() ? "There's nothing of this kind yet."
                                             : "Nothing matches “" + query_.search + "”. Try other words.",
                       kGridTop, page::kBottom);
    }

    drawFilters(kFiltersTop);
}

} // namespace tz::scenes
