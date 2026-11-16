#include "ui/grid_view.hpp"

#include "core/motion.hpp"
#include "gfx/draw.hpp"
#include "gfx/renderer.hpp"
#include "sys/audio.hpp"
#include "ui/widgets.hpp"

#include <algorithm>
#include <cmath>

namespace tz::ui
{

namespace
{
constexpr float kFollow = 14.0f;  // how fast the scroll catches up with its target
constexpr float kFriction = 3.0f; // how fast a fling slows down
constexpr float kGlide = 22.0f;   // how fast the highlight goes to the selected card
constexpr float kPressTime = 0.11f;
constexpr float kScrollbarWidth = 6;
constexpr float kEdgeRoom = 16;
} // namespace

void GridView::setBounds(float x, float y, float width, float height)
{
    x_ = x;
    y_ = y;
    width_ = width;
    height_ = height;
}

void GridView::setLayout(int columns, float cellHeight, float gap)
{
    columns_ = std::max(1, columns);
    cellHeight_ = cellHeight;
    gap_ = gap;
}

void GridView::setCount(int count)
{
    count_ = std::max(0, count);
    selected_ = std::clamp(selected_, 0, std::max(0, count_ - 1));
    if (count_ == 0)
    {
        scroll_ = target_ = minScroll();
        velocity_ = 0;
        highlightX_ = -1;
    }
    target_ = std::clamp(target_, minScroll(), maxScroll());
}

void GridView::setFooter(float height)
{
    footer_ = std::max(0.0f, height);
    target_ = std::clamp(target_, minScroll(), maxScroll());
}

int GridView::rows() const
{
    return (count_ + columns_ - 1) / columns_;
}

float GridView::cellWidth() const
{
    return (width_ - gap_ * static_cast<float>(columns_ - 1)) / static_cast<float>(columns_);
}

float GridView::maxScroll() const
{
    return std::max(minScroll(), static_cast<float>(rows()) * rowHeight() - gap_ + footer_ + kEdgeRoom - height_);
}

float GridView::overscroll() const
{
    if (scroll_ < minScroll())
        return scroll_ - minScroll();
    if (scroll_ > maxScroll())
        return scroll_ - maxScroll();
    return 0;
}

void GridView::select(int index, bool animate)
{
    if (count_ == 0)
        return;
    selected_ = std::clamp(index, 0, count_ - 1);
    keepSelectionVisible();
    if (!animate)
    {
        scroll_ = target_;
        highlightX_ = -1;
    }
}

void GridView::keepSelectionVisible()
{
    const int row = selected_ / columns_;
    const float top = static_cast<float>(row) * rowHeight();
    // at least the room the selected card's ring and ledge take, so they're never under what's round the grid
    const float margin = std::max(kEdgeRoom, height_ > rowHeight() * 2.5f ? rowHeight() * 0.35f : 0.0f);
    const float above = row == 0 ? topInset_ : margin;
    const float below = row == rows() - 1 ? std::max(margin, footer_ + kEdgeRoom) : margin;
    if (top - above < target_)
        target_ = top - above;
    else if (top + cellHeight_ + below > target_ + height_)
        target_ = top + cellHeight_ + below - height_;
    target_ = std::clamp(target_, minScroll(), maxScroll());
    velocity_ = 0;
}

void GridView::cellPosition(int index, float& x, float& y) const
{
    const int column = index % columns_;
    const int row = index / columns_;
    x = x_ + static_cast<float>(column) * (cellWidth() + gap_);
    y = y_ + static_cast<float>(row) * rowHeight();
}

int GridView::cellAt(float x, float y) const
{
    if (x < x_ || x >= x_ + width_ || y < y_ || y >= y_ + height_)
        return -1;
    const float inGrid = y - y_ + scroll_;
    const int row = static_cast<int>(std::floor(inGrid / rowHeight()));
    const int column = static_cast<int>(std::floor((x - x_) / (cellWidth() + gap_)));
    if (row < 0 || column < 0 || column >= columns_)
        return -1;
    if (inGrid - static_cast<float>(row) * rowHeight() > cellHeight_ ||
        x - x_ - static_cast<float>(column) * (cellWidth() + gap_) > cellWidth())
        return -1;
    const int index = row * columns_ + column;
    return index < count_ ? index : -1;
}

float GridView::selectedTop() const
{
    float x, y;
    cellPosition(selected_, x, y);
    return y - scroll_;
}

bool GridView::settled() const
{
    return !dragging_ && std::fabs(target_ - scroll_) < 0.5f && velocity_ == 0;
}

std::pair<int, int> GridView::visibleRange() const
{
    if (count_ == 0)
        return {0, -1};
    const int firstRow = std::max(0, static_cast<int>(std::floor(scroll_ / rowHeight())));
    const int lastRow = std::max(0, static_cast<int>(std::floor((scroll_ + height_) / rowHeight())));
    return {std::min(count_ - 1, firstRow * columns_), std::min(count_ - 1, (lastRow + 1) * columns_ - 1)};
}

GridView::Event GridView::update(Input& input, float dt)
{
    if (input.pressed(key::kUp) && selected_ < columns_)
        return Event::Above;
    Event event = Event::None;
    if (count_ > 0)
    {
        const int before = selected_;
        const int column = selected_ % columns_;
        if (input.repeated(key::kUp))
        {
            if (selected_ >= columns_)
                selected_ -= columns_;
        }
        else if (input.repeated(key::kDown))
        {
            if (selected_ + columns_ < count_)
                selected_ += columns_;
            // on the row above a shorter last row, down goes to its last card
            else if (selected_ / columns_ < rows() - 1)
                selected_ = count_ - 1;
        }
        else if (input.repeated(key::kLeft))
        {
            if (column > 0 || input.pressed(key::kLeft))
                selected_ = std::max(0, selected_ - 1);
        }
        else if (input.repeated(key::kRight))
        {
            if (column < columns_ - 1 || input.pressed(key::kRight))
                selected_ = std::min(count_ - 1, selected_ + 1);
        }
        if (selected_ != before)
        {
            keepSelectionVisible();
            event = Event::Moved;
            sfx::play(sfx::Sound::Cursor);
        }
        else if (input.pressed(key::kA))
        {
            event = Event::Activated;
            sfx::play(sfx::Sound::Confirm);
            pressedUntil_ = gfx::time() + kPressTime;
        }
    }

    const bool inside = input.x >= x_ && input.x < x_ + width_ && input.y >= y_ && input.y < y_ + height_;
    if (input.touchStarted || (!input.touching && !input.touchEnded) ||
        (input.touching && (input.startX != touchX_ || input.startY != touchY_)))
    {
        dragging_ = false;
        pressedInside_ = input.touchStarted && inside;
        touchX_ = input.startX;
        touchY_ = input.startY;
    }
    if (pressedInside_ && input.touching && !input.dragging && cellAt(input.x, input.y) == selected_ && count_ > 0)
        pressedUntil_ = gfx::time() + kPressTime;
    else if (pressedInside_ && input.dragging)
        pressedUntil_ = 0;
    if (pressedInside_ && input.dragging)
    {
        dragging_ = true;
        const float over = overscroll();
        scroll_ -= input.dy * (over != 0 ? motion::rubberGive(over, height_) : 1.0f);
        target_ = scroll_;
        velocity_ = 0;
    }
    if (input.touchEnded && pressedInside_)
    {
        if (dragging_)
        {
            velocity_ = -input.velocityY;
        }
        else if (input.tap && inside && count_ > 0)
        {
            // a card tapped opens, as on a phone: the selection goes there too, for when the page closes
            if (const int cell = cellAt(input.x, input.y); cell >= 0)
            {
                selected_ = cell;
                keepSelectionVisible();
                event = Event::Activated;
                sfx::play(sfx::Sound::Confirm);
                input.consumeTouch();
            }
        }
        dragging_ = false;
        pressedInside_ = false;
    }

    if (!dragging_)
    {
        const float over = overscroll();
        target_ = std::clamp(target_, minScroll(), maxScroll());
        if ((over != 0 && std::fabs(target_ - (scroll_ - over)) < 0.5f) || (over == 0 && velocity_ != 0))
        {
            motion::coast(scroll_, velocity_, minScroll(), maxScroll(), kFriction, dt);
            target_ = std::clamp(scroll_, minScroll(), maxScroll());
        }
        else
        {
            velocity_ = 0;
            scroll_ += (target_ - scroll_) * std::min(1.0f, dt * kFollow);
            if (std::fabs(target_ - scroll_) < 0.3f)
                scroll_ = target_;
        }
    }

    float goalX, goalY;
    cellPosition(selected_, goalX, goalY);
    if (highlightX_ < 0 || std::fabs(goalY - highlightY_) > height_)
    {
        highlightX_ = goalX;
        highlightY_ = goalY;
    }
    highlightX_ += (goalX - highlightX_) * std::min(1.0f, dt * kGlide);
    highlightY_ += (goalY - highlightY_) * std::min(1.0f, dt * kGlide);
    return event;
}

void GridView::draw(const CellPainter& drawCell, const HighlightPainter& drawHighlight,
                    const FooterPainter& drawFooter) const
{
    gfx::pushClip(x_ - 12, y_ - 12, width_ + 24, height_ + 24);
    const bool sunk = gfx::time() < pressedUntil_;
    if (drawHighlight && count_ > 0 && highlightX_ >= 0)
        drawHighlight(std::round(highlightX_), std::round(highlightY_ - scroll_), cellWidth(), cellHeight_, sunk);
    const auto [first, last] = visibleRange();
    for (int i = first; i <= last; ++i)
    {
        float x, y;
        cellPosition(i, x, y);
        const float shift = i == selected_ && sunk ? kKeyLip : 0;
        drawCell(i, std::round(x), std::round(y - scroll_) + shift, cellWidth(), cellHeight_, i == selected_);
    }
    const float footerTop = y_ + static_cast<float>(rows()) * rowHeight() - scroll_;
    if (footer_ > 0 && drawFooter && footerTop < y_ + height_)
        drawFooter(x_, footerTop, width_, footer_);

    const float total = static_cast<float>(rows()) * rowHeight() + footer_;
    if (total > height_ && maxScroll() > minScroll())
    {
        const float barHeight = std::max(48.0f, height_ * height_ / total);
        const float at = std::clamp((scroll_ - minScroll()) / (maxScroll() - minScroll()), 0.0f, 1.0f);
        gfx::roundedRect(x_ + width_ + 6, y_ + (height_ - barHeight) * at, kScrollbarWidth, barHeight,
                         kScrollbarWidth / 2, gfx::rgba(255, 255, 255, 60));
    }
    gfx::popClip();
}

} // namespace tz::ui
