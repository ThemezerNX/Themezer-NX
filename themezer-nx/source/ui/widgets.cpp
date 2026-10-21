#include "ui/widgets.hpp"

#include "gfx/text.hpp"
#include "sys/audio.hpp"

#include <algorithm>
#include <cmath>

namespace tz::ui
{

using namespace gfx;

namespace
{

// The hints are keycaps: pills on a ledge they sink into when pressed, A the widest, in the accent.
constexpr float kHintGap = 10;
constexpr float kHintMargin = 16;
constexpr float kKeyHeight = 44;
constexpr float kKeyPadding = 18; // either side of the button and label
constexpr float kPressTime = 0.11f;

// Where a button's hint goes, left to right: the others first, then -, +, Y, X, B, A.
int hintRank(Keys key)
{
    if (key == key::kMinus)
        return 1;
    if (key == key::kPlus)
        return 2;
    if (key == key::kY)
        return 3;
    if (key == key::kX)
        return 4;
    if (key == key::kB)
        return 5;
    if (key == key::kA)
        return 6;
    return 0;
}

} // namespace

void approach(float& value, float target, float dt, float speed)
{
    value += (target - value) * std::min(1.0f, dt * speed);
    if (std::fabs(target - value) < 0.002f)
        value = target;
}

float ease(float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    return 1 - (1 - t) * (1 - t) * (1 - t);
}

namespace
{

// What's printed on a button: its letter, or the shoulder's name.
const char* buttonLabel(Keys key)
{
    if (key == key::kA)
        return "A";
    if (key == key::kB)
        return "B";
    if (key == key::kX)
        return "X";
    if (key == key::kY)
        return "Y";
    if (key == key::kL)
        return "L";
    if (key == key::kR)
        return "R";
    if (key == key::kZL)
        return "ZL";
    if (key == key::kZR)
        return "ZR";
    return "";
}

bool isShoulder(Keys key)
{
    return key == key::kL || key == key::kR || key == key::kZL || key == key::kZR;
}

// The button's face is in color; what's on it in a colour that stands out from that.
Color inkOn(Color color)
{
    const float luminance = 0.299f * red(color) + 0.587f * green(color) + 0.114f * blue(color);
    return luminance > 140 ? withAlpha(color::kBackground, alpha(color) / 255.0f)
                           : withAlpha(color::kText, alpha(color) / 255.0f);
}

float diameter(float size)
{
    return std::round(size * 0.9f);
}

} // namespace

float buttonWidth(Keys key, float size)
{
    const float d = diameter(size);
    if (isShoulder(key))
        return std::round(textWidth(buttonLabel(key), d * 0.56f) + d * 0.7f);
    return d;
}

// Drawn rather than taken from the Switch's font, so they look the same everywhere: the face buttons and + and - as
// discs, the shoulders as pills, the D-pad's directions as a disc with an arrow.
float drawButton(Keys key, float x, float y, float size, Color color)
{
    const float d = diameter(size);
    const float middle = std::round(textMiddle(y, size));
    const float width = buttonWidth(key, size);
    const Color ink = inkOn(color);
    x = std::round(x);
    if (isShoulder(key))
    {
        const float height = std::round(d * 0.8f);
        roundedRect(x, middle - height / 2, width, height, height / 2, color);
        const float textSize = d * 0.56f;
        drawText(buttonLabel(key), x + width / 2, textTop(middle, textSize), textSize, ink, Align::Center);
        return width;
    }
    const float cx = x + d / 2;
    circle(cx, middle, d / 2, color);
    if (key == key::kPlus || key == key::kMinus)
    {
        const float bar = std::round(d * 0.5f);
        const float thickness = std::max(2.0f, std::round(d * 0.12f));
        rect(std::round(cx - bar / 2), std::round(middle - thickness / 2), bar, thickness, ink);
        if (key == key::kPlus)
            rect(std::round(cx - thickness / 2), std::round(middle - bar / 2), thickness, bar, ink);
        return width;
    }
    if (const char* label = buttonLabel(key); label[0])
    {
        const float textSize = d * 0.62f;
        drawText(label, cx, textTop(middle, textSize), textSize, ink, Align::Center);
        return width;
    }
    // a direction: an arrow pointing that way
    const float r = d * 0.24f;
    float dx = 0, dy = 0;
    if (key & key::kUp)
        dy = -1;
    else if (key & key::kDown)
        dy = 1;
    else if (key & key::kLeft)
        dx = -1;
    else
        dx = 1;
    triangle(cx + dx * r, middle + dy * r, cx - dx * r * 0.6f - dy * r, middle - dy * r * 0.6f - dx * r,
             cx - dx * r * 0.6f + dy * r, middle - dy * r * 0.6f + dx * r, ink);
    return width;
}

float hintWidth(Keys key, std::string_view label, float labelSize)
{
    return buttonWidth(key, metric::kButtonGlyph) + (label.empty() ? 0 : 8 + textWidth(label, labelSize));
}

float drawHint(Keys key, std::string_view label, float x, float middle, Color buttonColor, Color labelColor,
               float labelSize)
{
    const float button = drawButton(key, x, textTop(middle, metric::kButtonGlyph), metric::kButtonGlyph, buttonColor);
    if (label.empty())
        return button;
    return button + 8 + drawText(label, x + button + 8, textTop(middle, labelSize), labelSize, labelColor);
}

float drawKey(float x, float y, float w, float h, Color face, bool down, float radius)
{
    if (radius < 0)
        radius = h / 2;
    if (down)
        y += kKeyLip;
    else if (opacity() >= 1) // it would show through a face that's fading
        roundedRect(x, y + kKeyLip, w, h, radius, mix(face, color::kBackground, 0.55f));
    roundedRect(x, y, w, h, radius, face);
    return y;
}

Keys HintBar::keyAt(float x, float y) const
{
    if (y < y_ || y >= y_ + height_)
        return 0;
    for (const Spot& spot : spots_)
    {
        if (x >= spot.x && x < spot.x + spot.width)
            return spot.key;
    }
    return 0;
}

void HintBar::update(Input& input, const std::vector<Hint>& hints)
{
    // a finger on a key holds it down; lifted there, without having moved off, it presses the key
    const Keys touched = input.touching && !input.dragging ? keyAt(input.x, input.y) : 0;
    touchedKey_ = touched == keyAt(input.startX, input.startY) ? touched : 0;
    if (input.tap)
    {
        if (const Keys key = keyAt(input.x, input.y); key && key == keyAt(input.startX, input.startY))
        {
            input.press(key);
            input.consumeTouch();
        }
    }
    held_ = input.held;
    for (const Hint& hint : hints)
    {
        if (input.down & hint.key)
        {
            pressedKey_ = hint.key;
            pressedAt_ = time();
        }
    }
}

void HintBar::draw(const std::vector<Hint>& hints, float alpha, Color accent)
{
    height_ = metric::kHintBarHeight;
    y_ = metric::kHeight - height_;
    spots_.clear();
    if (hints.empty() || alpha <= 0)
        return;
    const Layer layer(0, 0, alpha);
    rect(0, y_, metric::kWidth, height_, color::kSurfaceSolid);
    rect(0, y_, metric::kWidth, 1, mix(color::kSurfaceSolid, color::kText, 0.1f));

    // in that order, right-aligned as the Switch's own hints are
    std::vector<const Hint*> order;
    for (const Hint& hint : hints)
        order.push_back(&hint);
    std::stable_sort(order.begin(), order.end(),
                     [](const Hint* a, const Hint* b) { return hintRank(a->key) < hintRank(b->key); });
    auto keyWidth = [&](const Hint& hint) {
        return std::round(hintWidth(hint.key, hint.label) + 2 * kKeyPadding + (hint.key == key::kA ? 24 : 0));
    };
    float total = 0;
    for (const Hint* hint : order)
        total += keyWidth(*hint) + kHintGap;
    float x = metric::kWidth - kHintMargin - (total - kHintGap);
    const float top = std::round(y_ + (height_ - kKeyHeight - kKeyLip) / 2);
    const bool justPressed = time() - pressedAt_ < kPressTime;
    for (const Hint* hint : order)
    {
        const bool primary = hint->key == key::kA;
        const float width = keyWidth(*hint);
        const bool down =
            (justPressed && hint->key == pressedKey_) || (held_ & hint->key) || hint->key == touchedKey_;
        const Color face = primary ? accent : color::kSurfaceHigh;
        const Color ink = primary ? color::kOnAccent : color::kText;
        const float keyTop = drawKey(x, top, width, kKeyHeight, face, down);
        const float content = hintWidth(hint->key, hint->label);
        drawHint(hint->key, hint->label, x + std::round((width - content) / 2), keyTop + kKeyHeight / 2, ink, ink);
        spots_.push_back({x - kHintGap / 2, width + kHintGap, hint->key});
        x += width + kHintGap;
    }
    // as update saw them this frame: without an update (an overlay is over the scene) the keys come up
    held_ = 0;
    touchedKey_ = 0;
}

bool TabBar::update(Input& input)
{
    if (tabs_.empty())
        return false;
    const int count = static_cast<int>(tabs_.size());
    const int before = selected_;
    bool forward = true;
    const auto onBar = [&](float x, float y) { return x >= left_ && x < right_ && y >= top_ && y < bottom_; };
    const bool touching =
        input.touching && !input.dragging && onBar(input.startX, input.startY) && onBar(input.x, input.y);
    const int touched = touching ? slotAt(input.x) : kNoSlot;
    bool tappedEnd = false;
    if (previousKey_ && input.pressed(previousKey_))
    {
        selected_ = (selected_ + count - 1) % count;
        forward = false;
    }
    else if (nextKey_ && input.pressed(nextKey_))
    {
        selected_ = (selected_ + 1) % count;
    }
    else if (input.tap && onBar(input.startX, input.startY) && onBar(input.x, input.y) && !slots_.empty())
    {
        const int slot = slotAt(input.x);
        if (slot == kBefore)
        {
            selected_ = (selected_ + count - 1) % count;
            forward = false;
        }
        else if (slot == kAfter)
        {
            selected_ = (selected_ + 1) % count;
        }
        else
        {
            selected_ = slot;
            forward = selected_ > before;
        }
        tappedEnd = slot < 0;
        input.consumeTouch();
    }
    previousPress_.update(touched == kBefore || (previousKey_ && (input.held & previousKey_)),
                          (previousKey_ && input.pressed(previousKey_)) || (tappedEnd && !forward));
    nextPress_.update(touched == kAfter || (nextKey_ && (input.held & nextKey_)),
                      (nextKey_ && input.pressed(nextKey_)) || (tappedEnd && forward));
    input.down &= ~(previousKey_ | nextKey_);
    input.repeat &= ~(previousKey_ | nextKey_);
    if (selected_ == before)
        return false;
    forward_ = forward;
    sfx::play(sfx::Sound::Tab);
    return true;
}

int TabBar::slotAt(float x) const
{
    if (slots_.empty())
        return kNoSlot;
    if (x < slots_.front().x)
        return kBefore;
    if (x >= slots_.back().x + slots_.back().width)
        return kAfter;
    for (size_t i = 0; i < slots_.size(); ++i)
    {
        if (x < slots_[i].x + slots_[i].width)
            return static_cast<int>(i);
    }
    return kAfter;
}

float TabBar::contentWidth(const Tab& tab, float size) const
{
    const float iconWidth = tab.icon != Icon::Count ? metric::kIcon + (tab.label.empty() ? 0 : 10) : 0;
    return iconWidth + (tab.label.empty() ? 0 : textWidth(tab.label, size));
}

float TabBar::naturalWidth(float textSize) const
{
    float width = 2 * 16 + buttonWidth(previousKey_, metric::kButtonGlyph) + buttonWidth(nextKey_, metric::kButtonGlyph) +
                  2 * 12;
    for (const Tab& tab : tabs_)
        width += contentWidth(tab, textSize) + 2 * 28;
    return width;
}

void TabBar::draw(float x, float y, float width, float height, float textSize)
{
    if (tabs_.empty())
        return;
    left_ = x;
    right_ = x + width;
    top_ = y;
    bottom_ = y + height;
    const float middle = y + height / 2;
    roundedRect(x, y, width, height, height / 2, color::kSurface);

    // the buttons that switch the tabs, at the ends: pressed, a symbol goes down as far as the keys sink, and lights up
    float left = x + 16;
    float right = x + width - 16;
    const float glyphTop = textTop(middle, metric::kButtonGlyph);
    const bool previousDown = previousPress_.down();
    const bool nextDown = nextPress_.down();
    left += drawButton(previousKey_, left, glyphTop + (previousDown ? kKeyLip : 0), metric::kButtonGlyph,
                       previousDown ? color::kText : color::kTextDim) +
            12;
    right -= buttonWidth(nextKey_, metric::kButtonGlyph);
    drawButton(nextKey_, right, glyphTop + (nextDown ? kKeyLip : 0), metric::kButtonGlyph,
               nextDown ? color::kText : color::kTextDim);
    right -= 12;

    // each tab as wide as its name, with what's left shared out evenly
    const size_t count = tabs_.size();
    float names = 0;
    for (const Tab& tab : tabs_)
        names += contentWidth(tab, textSize);
    const float padding = std::max(0.0f, (right - left - names) / static_cast<float>(2 * count));
    slots_.clear();
    float at = left;
    for (const Tab& tab : tabs_)
    {
        const float slotWidth = contentWidth(tab, textSize) + 2 * padding;
        slots_.push_back({at, slotWidth});
        at += slotWidth;
    }

    // the highlight slides to the selected tab
    const Slot& target = slots_[static_cast<size_t>(selected_)];
    if (highlightX_ < 0)
    {
        highlightX_ = target.x;
        highlightWidth_ = target.width;
    }
    highlightX_ += (target.x - highlightX_) * 0.3f;
    highlightWidth_ += (target.width - highlightWidth_) * 0.3f;
    if (std::fabs(target.x - highlightX_) < 0.5f)
    {
        highlightX_ = target.x;
        highlightWidth_ = target.width;
    }
    const float pillHeight = height - 12;
    roundedRect(std::round(highlightX_), y + 6, std::round(highlightWidth_), pillHeight, pillHeight / 2,
                withAlpha(tabs_[static_cast<size_t>(selected_)].accent.solid, 0.4f));

    const float labelTop = textTop(middle, textSize);
    for (size_t i = 0; i < count; ++i)
    {
        const Tab& tab = tabs_[i];
        const Color tint = static_cast<int>(i) == selected_ ? tab.accent.light : color::kTextDim;
        float cx = slots_[i].x + (slots_[i].width - contentWidth(tab, textSize)) / 2;
        if (tab.icon != Icon::Count)
        {
            icon(tab.icon, cx, std::round(middle - metric::kIcon / 2), metric::kIcon,
                 static_cast<int>(i) == selected_ ? tab.accent.light : color::kTextDim);
            cx += metric::kIcon + 10;
        }
        if (!tab.label.empty())
            drawText(tab.label, cx, labelTop, textSize, tint);
    }
}

} // namespace tz::ui
