// A grid of cards that scrolls smoothly: the D-pad moves the selection, the scroll follows it; a finger drags and flings
// it, and past an end it gives less and springs back, as Themezer-3DS's lists do.

#pragma once

#include "ui/input.hpp"

#include <functional>
#include <utility>

namespace tz::ui
{

class GridView
{
public:
    enum class Event
    {
        None,
        Moved,
        Activated,
        Above, // up on the top row: for what's over the grid
    };

    void setBounds(float x, float y, float width, float height);
    void setLayout(int columns, float cellHeight, float gap);
    void setCount(int count);
    // Room under the last row, for a footer (loading more, an error).
    void setFooter(float height);
    // Room above the first row, under something drawn over the grid's top.
    void setTopInset(float height) { topInset_ = height; }

    int count() const { return count_; }
    int selected() const { return selected_; }
    void select(int index, bool animate = true);

    Event update(Input& input, float dt);

    // Each visible cell, then the highlight under the selected one, all in the grid's clip.
    using CellPainter = std::function<void(int index, float x, float y, float w, float h, bool selected)>;
    using HighlightPainter = std::function<void(float x, float y, float w, float h, bool down)>;
    using FooterPainter = std::function<void(float x, float y, float w, float h)>;
    void draw(const CellPainter& drawCell, const HighlightPainter& drawHighlight = nullptr,
              const FooterPainter& drawFooter = nullptr) const;

    // The cells on screen, first to last; empty when (0, -1).
    std::pair<int, int> visibleRange() const;
    float cellWidth() const;
    float cellHeight() const { return cellHeight_; }
    // Where the selection's card is now, on screen.
    float selectedTop() const;
    bool settled() const;
    // Scrolled past the top: how far, for a pull.
    float scroll() const { return scroll_; }

private:
    int rows() const;
    float rowHeight() const { return cellHeight_ + gap_; }
    float minScroll() const { return -topInset_; }
    float maxScroll() const;
    float overscroll() const;
    void keepSelectionVisible();
    int cellAt(float x, float y) const;
    void cellPosition(int index, float& x, float& y) const;

    float x_ = 0, y_ = 0, width_ = 0, height_ = 0;
    int columns_ = 4;
    float cellHeight_ = 200;
    float gap_ = 16;
    float footer_ = 0;
    float topInset_ = 0;
    int count_ = 0;
    int selected_ = 0;

    float scroll_ = 0;
    float target_ = 0;
    float velocity_ = 0;
    float highlightX_ = -1;
    float highlightY_ = 0;
    bool dragging_ = false;
    bool pressedInside_ = false;
    float touchX_ = -1, touchY_ = -1;
    float pressedUntil_ = 0;
};

} // namespace tz::ui
