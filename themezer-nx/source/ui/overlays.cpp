#include "ui/overlays.hpp"

#include "gfx/text.hpp"
#include "sys/audio.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tz::ui
{

using namespace gfx;

namespace
{

constexpr float kSheetWidth = 760;
constexpr float kSheetRow = 68;
constexpr float kSheetPadding = 12;
constexpr float kSheetHeader = 72;
constexpr float kSheetFooter = 56;
constexpr int kSheetMaxRows = 8;
constexpr float kButtonHeight = 56;

// How much of a download is in: "1.2 of 3.4 MB", or "1.2 MB" while its size isn't known; nothing before it starts.
std::string downloaded(const net::Progress& download)
{
    const uint64_t received = download.received.load();
    const uint64_t total = download.total.load();
    if (received == 0)
        return {};
    char text[48];
    if (total > 0)
        std::snprintf(text, sizeof(text), "%.1f of %.1f MB", received / 1048576.0, total / 1048576.0);
    else
        std::snprintf(text, sizeof(text), "%.1f MB", received / 1048576.0);
    return text;
}

} // namespace

float drawActionKey(Keys key, float right, float middle)
{
    if (!key)
        return right;
    const float left = right - buttonWidth(key, metric::kButtonGlyph);
    drawButton(key, left, textTop(middle, metric::kButtonGlyph), metric::kButtonGlyph, color::kTextDim);
    return left - 12;
}

// ---------------------------------------------------------------------------------------------------------------------
// ActionSheet

ActionSheet::ActionSheet(std::string title, std::vector<Action> actions, Color accent, int selected)
    : title_(std::move(title)), actions_(std::move(actions)), accent_(accent)
{
    sfx::play(sfx::Sound::Open);
    if (selected >= 0 && selected < static_cast<int>(actions_.size()) && actions_[selected].enabled)
    {
        selected_ = selected;
        return;
    }
    for (size_t i = 0; i < actions_.size(); ++i)
    {
        if (actions_[i].enabled)
        {
            selected_ = static_cast<int>(i);
            break;
        }
    }
}

int ActionSheet::visibleRows() const
{
    return std::min(static_cast<int>(actions_.size()), kSheetMaxRows);
}

float ActionSheet::panelHeight() const
{
    return kSheetHeader + kSheetRow * static_cast<float>(visibleRows()) + kSheetPadding + kSheetFooter;
}

float ActionSheet::panelTop() const
{
    return std::round(metric::kHeight - panelHeight() * ease(appear_));
}

void ActionSheet::update(Input& input, float dt)
{
    if (closing_)
    {
        approach(appear_, 0, dt, 14);
        if (appear_ < 0.05f)
        {
            close();
            if (chosen_)
                chosen_();
        }
        return;
    }
    approach(appear_, 1, dt, 14);

    const int count = static_cast<int>(actions_.size());
    auto step = [&](int direction) {
        for (int i = 1; i <= count; ++i)
        {
            const int next = (selected_ + direction * i + count) % count;
            if (actions_[next].enabled)
            {
                if (next != selected_)
                    sfx::play(sfx::Sound::Cursor);
                selected_ = next;
                return;
            }
        }
    };
    if (input.repeated(key::kUp))
        step(-1);
    else if (input.repeated(key::kDown))
        step(1);
    first_ = std::clamp(first_, selected_ - visibleRows() + 1, selected_);
    if (highlight_ < 0)
        highlight_ = static_cast<float>(selected_);
    highlight_ += (static_cast<float>(selected_) - highlight_) * std::min(1.0f, dt * 24);

    auto choose = [&](int index) {
        if (index < 0 || index >= count || !actions_[index].enabled)
            return;
        Action& action = actions_[index];
        press_.update(false, true);
        sfx::play(sfx::Sound::Confirm);
        if (action.toggle)
        {
            action.toggle(action);
            return;
        }
        chosen_ = action.run;
        closing_ = true;
        highlight_ = static_cast<float>(index);
    };
    auto cancel = [&] {
        closing_ = true;
        highlight_ = static_cast<float>(selected_);
        sfx::play(sfx::Sound::Close);
    };

    const float rowsTop = panelTop() + kSheetHeader;
    const auto rowAt = [&](float y) { return y < rowsTop ? -1 : first_ + static_cast<int>((y - rowsTop) / kSheetRow); };
    const float left = (metric::kWidth - kSheetWidth) / 2;
    const bool inPanel = input.x >= left && input.x < left + kSheetWidth;
    press_.update(input.touching && !input.dragging && inPanel && rowAt(input.startY) == selected_ &&
                      rowAt(input.y) == selected_,
                  false);

    // a button that does an action without the sheet does it with the sheet open too
    for (int i = 0; i < count; ++i)
    {
        if (actions_[i].key && input.pressed(actions_[i].key) && actions_[i].key != key::kA &&
            actions_[i].key != key::kB)
        {
            selected_ = i;
            choose(i);
            return;
        }
    }
    if (input.pressed(key::kA))
    {
        choose(selected_);
    }
    else if (input.pressed(key::kB))
    {
        cancel();
    }
    else if (input.tap)
    {
        if (input.y < panelTop() || !inPanel)
        {
            cancel();
        }
        else if (const int row = rowAt(input.y); row >= first_ && row < first_ + visibleRows())
        {
            if (actions_[row].enabled)
            {
                selected_ = row;
                choose(row);
            }
        }
        input.consumeTouch();
    }
}

void ActionSheet::draw()
{
    fill(withAlpha(color::kScrim, ease(appear_)));
    const float top = panelTop();
    const float left = (metric::kWidth - kSheetWidth) / 2;
    shadow(left, top, kSheetWidth, panelHeight() + 24, 24, 32, ease(appear_));
    roundedRect(left, top, kSheetWidth, panelHeight() + 24, 24, color::kSurfaceSolid);
    drawText(title_, left + 32, textTop(top + kSheetHeader / 2 + 4, metric::kTextLarge), metric::kTextLarge,
             color::kText, Align::Left, kSheetWidth - 64);

    float y = top + kSheetHeader;
    const bool down = press_.down();
    if (highlight_ >= 0)
    {
        const float row = std::clamp(highlight_, static_cast<float>(first_),
                                     static_cast<float>(first_ + visibleRows() - 1)) -
                          static_cast<float>(first_);
        drawKey(left + 12, std::round(y + row * kSheetRow) + 4, kSheetWidth - 24, kSheetRow - 8 - kKeyLip,
                color::kSurfaceHigh, down, metric::kRadius);
    }
    for (int i = first_; i < first_ + visibleRows(); ++i)
    {
        const Action& action = actions_[static_cast<size_t>(i)];
        const bool selected = i == selected_;
        const float middle = y + (kSheetRow - kKeyLip) / 2 + (selected && down ? kKeyLip : 0);
        const Color tint = !action.enabled ? color::kTextFaint
                                           : (action.danger ? color::kDanger : (selected ? accent_ : color::kTextDim));
        icon(action.icon, left + 36, std::round(middle - metric::kIcon / 2), metric::kIcon, tint);
        const Color textColor = action.enabled ? (action.danger ? color::kDanger : color::kText) : color::kTextFaint;
        const float labelRight = drawActionKey(action.key, left + kSheetWidth - 36, middle);
        drawText(action.label, left + 88, textTop(middle, metric::kTextNormal), metric::kTextNormal, textColor,
                 Align::Left, labelRight - left - 88);
        y += kSheetRow;
    }
    // what the selected action does
    if (selected_ >= 0 && selected_ < static_cast<int>(actions_.size()))
    {
        const float middle = top + panelHeight() - kSheetFooter / 2;
        drawText(actions_[static_cast<size_t>(selected_)].detail, left + 36, textTop(middle, metric::kTextSmall),
                 metric::kTextSmall, color::kTextDim, Align::Left, kSheetWidth - 72);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Dialog

Dialog::Dialog(Icon icon, Color iconColor, std::string title, std::string message, std::vector<DialogButton> buttons)
    : icon_(icon), iconColor_(iconColor), title_(std::move(title)), message_(std::move(message)),
      buttons_(std::move(buttons))
{
    if (buttons_.empty())
        buttons_.push_back({"OK", nullptr});
    // an error says so as it opens
    sfx::play(iconColor == color::kDanger ? sfx::Sound::Error : sfx::Sound::Open);
}

void Dialog::choose(size_t button)
{
    sfx::play(buttons_.size() == 1 ? sfx::Sound::Close : (button == 0 ? sfx::Sound::Back : sfx::Sound::Confirm));
    close();
    if (button < buttons_.size() && buttons_[button].run)
        buttons_[button].run();
}

Keys Dialog::keyOf(size_t button) const
{
    if (buttons_[button].key)
        return buttons_[button].key;
    if (buttons_.size() == 1)
        return key::kA | key::kB;
    return button == 0 ? key::kB : key::kA;
}

void Dialog::update(Input& input, float dt)
{
    approach(appear_, 1, dt, 12);
    for (size_t i = 0; i < buttons_.size(); ++i)
    {
        if (input.pressed(keyOf(i)))
        {
            choose(i);
            return;
        }
    }
    if (input.tap)
    {
        for (size_t i = 0; i < boxes_.size(); ++i)
        {
            const Box& box = boxes_[i];
            if (input.x >= box.x && input.x < box.x + box.w && input.y >= box.y && input.y < box.y + box.h)
            {
                input.consumeTouch();
                choose(i);
                return;
            }
        }
    }
}

void Dialog::draw()
{
    fill(withAlpha(color::kScrim, appear_));
    const float width = 680;
    const float messageWidth = width - 80;
    const auto lines = wrap(message_, messageWidth, metric::kTextNormal, 8);
    const float messageHeight = static_cast<float>(lines.size()) * lineHeight(metric::kTextNormal);
    const float height = 40 + metric::kIconLarge * 0.75f + 20 + lineHeight(metric::kTextLarge) + 12 + messageHeight +
                         36 + kButtonHeight + 32;
    const float x = (metric::kWidth - width) / 2;
    const float y = std::round((metric::kHeight - height) / 2 + (1 - ease(appear_)) * 40);
    const Layer layer(0, 0, ease(appear_));

    shadow(x, y, width, height, 24, 32);
    roundedRect(x, y, width, height, 24, color::kSurfaceSolid);
    float at = y + 40;
    const float iconSize = metric::kIconLarge * 0.75f;
    icon(icon_, x + (width - iconSize) / 2, at, iconSize, iconColor_);
    at += iconSize + 20;
    drawText(title_, x + width / 2, at, metric::kTextLarge, color::kText, Align::Center, messageWidth);
    at += lineHeight(metric::kTextLarge) + 12;
    drawWrapped(message_, x + 40, at, messageWidth, metric::kTextNormal, color::kTextDim, 8, Align::Center);

    const float buttonsTop = y + height - 32 - kButtonHeight;
    const float slotWidth = (width - 64) / static_cast<float>(buttons_.size());
    boxes_.clear();
    for (size_t i = 0; i < buttons_.size(); ++i)
        boxes_.push_back({std::round(x + 32 + slotWidth * static_cast<float>(i) + 6), buttonsTop,
                          std::round(slotWidth - 12), kButtonHeight});
    for (size_t i = 0; i < buttons_.size(); ++i)
    {
        const DialogButton& button = buttons_[i];
        const Box& box = boxes_[i];
        const bool primary = i + 1 == buttons_.size() && buttons_.size() > 1;
        const Color fillColor = primary ? (button.danger ? color::kDanger : color::kPrimary) : color::kSurfaceHigh;
        roundedRect(box.x, box.y, box.w, box.h, box.h / 2, fillColor);
        // a message's one button shows A, which B also presses
        const Keys keys = keyOf(i) == (key::kA | key::kB) ? key::kA : keyOf(i);
        const Color textColor = primary ? color::kOnAccent : color::kText;
        const std::string label = ellipsize(button.label, box.w - 24 - hintWidth(keys, "", metric::kTextNormal),
                                            metric::kTextNormal);
        drawHint(keys, label, box.x + (box.w - hintWidth(keys, label, metric::kTextNormal)) / 2, box.y + box.h / 2,
                 textColor, textColor, metric::kTextNormal);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// ProgressOverlay

float TaskProgress::fraction() const
{
    const uint64_t bytes = download.total.load();
    const int total = total_.load();
    float part = bytes > 0 ? static_cast<float>(download.received.load()) / static_cast<float>(bytes) : -1.0f;
    if (total > 1)
        return (static_cast<float>(done_.load()) + std::max(0.0f, part)) / static_cast<float>(total);
    return part;
}

ProgressOverlay::~ProgressOverlay()
{
    token_.cancel();
}

void ProgressOverlay::update(Input& input, float dt)
{
    approach(appear_, 1, dt, 10);
    const float fraction = progress_->fraction();
    if (fraction >= 0)
        approach(shown_, fraction, dt, 8);
    if (options_.cancellable && !cancelRequested_ && input.pressed(key::kB))
    {
        cancelRequested_ = true;
        token_.cancel();
        sfx::play(sfx::Sound::Back);
    }
    if (done_->load())
        close();
}

void ProgressOverlay::draw()
{
    fill(withAlpha(color::kScrim, appear_));
    const Layer layer(0, 0, ease(appear_));
    const float width = 640;
    const float height = 200;
    const float x = (metric::kWidth - width) / 2;
    const float y = (metric::kHeight - height) / 2;
    shadow(x, y, width, height, 24, 32);
    roundedRect(x, y, width, height, 24, color::kSurfaceSolid);

    drawText(options_.title, x + 40, textTop(y + 52, metric::kTextLarge), metric::kTextLarge, color::kText,
             Align::Left, width - 80);
    std::string status = progress_->status();
    const std::string bytes = downloaded(progress_->download);
    if (status.empty())
        status = bytes;
    else if (!bytes.empty())
        status += "  ·  " + bytes;
    drawText(status, x + 40, textTop(y + 92, metric::kTextSmall), metric::kTextSmall, color::kTextDim, Align::Left,
             width - 80);

    const float barY = y + 120;
    if (progress_->fraction() >= 0)
    {
        progressBar(x + 40, barY, width - 80, 12, shown_, color::kSurfaceHigh, options_.accent);
    }
    else
    {
        // nothing to measure yet: a stripe going back and forth
        const float t = std::fmod(time() * 0.8f, 2.0f);
        const float position = t < 1 ? t : 2 - t;
        roundedRect(x + 40, barY, width - 80, 12, 6, color::kSurfaceHigh);
        roundedRect(x + 40 + (width - 80 - 140) * position, barY, 140, 12, 6, options_.accent);
    }

    if (options_.cancellable)
    {
        const char* label = cancelRequested_ ? "Cancelling…" : "Cancel";
        drawHint(key::kB, label, x + width - 40 - hintWidth(key::kB, label), y + height - 36, color::kText,
                 color::kTextDim);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Toasts

namespace
{
struct Toast
{
    std::string message;
    ToastKind kind;
    float age = 0;
};
std::vector<Toast> toasts;
bool successThisFrame = false;
constexpr float kToastTime = 2.8f;
constexpr float kToastFade = 0.25f;
} // namespace

void toast(std::string message, ToastKind kind)
{
    if (kind == ToastKind::Success)
    {
        sfx::play(sfx::Sound::Done);
        successThisFrame = true;
    }
    else if (kind == ToastKind::Error || (kind == ToastKind::Warning && !successThisFrame))
    {
        sfx::play(sfx::Sound::Error);
    }
    for (Toast& t : toasts)
    {
        if (t.message == message)
        {
            t.age = std::min(t.age, kToastFade);
            return;
        }
    }
    toasts.push_back({std::move(message), kind});
    if (toasts.size() > 3)
        toasts.erase(toasts.begin());
}

void updateToasts(float dt)
{
    successThisFrame = false;
    for (Toast& t : toasts)
        t.age += dt;
    std::erase_if(toasts, [](const Toast& t) { return t.age > kToastTime; });
}

void drawToasts()
{
    float bottom = metric::kHeight - metric::kHintBarHeight - 16;
    for (auto it = toasts.rbegin(); it != toasts.rend(); ++it)
    {
        const Toast& t = *it;
        const float in = std::clamp(t.age / kToastFade, 0.0f, 1.0f);
        const float out = std::clamp((kToastTime - t.age) / kToastFade, 0.0f, 1.0f);
        const float alpha = std::min(in, out);
        const float size = metric::kTextNormal;
        const float wrapWidth = 720;
        const auto lines = wrap(t.message, wrapWidth, size, 2);
        const float height = static_cast<float>(lines.size()) * lineHeight(size) + 28;
        float width = 0;
        for (const std::string& line : lines)
            width = std::max(width, textWidth(line, size));
        width += 96;
        const float x = std::round((metric::kWidth - width) / 2);
        const float y = std::round(bottom - height + (1 - ease(in)) * 16);

        Icon which = Icon::Info;
        Color tint = color::kTextDim;
        switch (t.kind)
        {
        case ToastKind::Success:
            which = Icon::CheckCircle;
            tint = color::kPrimaryLight;
            break;
        case ToastKind::Warning:
            which = Icon::Warning;
            tint = color::kWarning;
            break;
        case ToastKind::Error:
            which = Icon::Error;
            tint = color::kDanger;
            break;
        case ToastKind::Info:
            break;
        }
        const Layer layer(0, 0, alpha);
        shadow(x, y, width, height, std::min(28.0f, height / 2), 16);
        roundedRect(x, y, width, height, std::min(28.0f, height / 2), color::kSurfaceRaised);
        icon(which, x + 28, std::round(y + height / 2 - metric::kIcon / 2), metric::kIcon, tint);
        const float textTopY =
            lines.size() == 1 ? textTop(y + height / 2, size) : y + (height - lineHeight(size) * 2) / 2;
        drawWrapped(t.message, x + 72, textTopY, wrapWidth, size, color::kText, 2);
        bottom = y - 12;
    }
}

} // namespace tz::ui
