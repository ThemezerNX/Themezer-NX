// The About page, as Themezer-3DS has it: what the app is and its license beside the credits and the notices of what
// it's built with (romfs/licenses.txt, made by tools/make_licenses.py), which scroll.

#include "app/app.hpp"
#include "core/motion.hpp"
#include "gfx/draw.hpp"
#include "gfx/renderer.hpp"
#include "gfx/text.hpp"
#include "scenes/actions.hpp"
#include "scenes/frame.hpp"
#include "sys/audio.hpp"
#include "ui/scene.hpp"
#include "ui/widgets.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace tz::scenes
{

using namespace gfx;

namespace
{

constexpr float kColumnX = 48;
constexpr float kColumnW = 440;
constexpr float kPanelX = kColumnX + kColumnW + 40;
constexpr float kPanelY = metric::kTopBarHeight + 8;
constexpr float kPanelW = metric::kWidth - kPanelX - 48;
constexpr float kPanelH = metric::kHeight - metric::kHintBarHeight - kPanelY - 16;
constexpr float kHeader = 64;
constexpr float kTextInset = 28;
constexpr float kFriction = 3;       // how fast a fling slows down, as in the grids
constexpr float kHeldSpeed = 900;    // pixels a second, while up or down is held

const char* kCredits =
    "Themezer-NX is made by suchmememanyskill and the Themezer contributors. Thanks to Exelix11 for NXThemesInstaller, "
    "which installs what the app downloads, and for the help with cURL. The look, the sounds and the music come from "
    "Themezer-3DS; the icons are Lucide's.";

class AboutScene final : public ui::Scene
{
public:
    AboutScene()
    {
        sfx::play(sfx::Sound::Open);
        std::string text = kCredits;
        if (FILE* file = std::fopen("romfs:/licenses.txt", "rb"))
        {
            text += "\n\n";
            char buffer[4096];
            size_t read;
            while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
                text.append(buffer, read);
            std::fclose(file);
        }
        lines_ = wrap(text, kPanelW - 2 * kTextInset, metric::kTextSmall);
    }

    void update(ui::Input& input, float dt) override
    {
        hintBar_.update(input, hints_);
        if (input.pressed(ui::key::kB))
        {
            sfx::play(sfx::Sound::Back);
            leaving_ = true;
            return;
        }

        // as the grids scroll: a drag past an end gives less and springs back, a fling bounces off an end; up and down
        // go steadily, left and right a page at a time
        const float view = kPanelH - kHeader;
        const float maxScroll =
            std::max(0.0f, static_cast<float>(lines_.size()) * lineHeight(metric::kTextSmall) - view + 16);
        const float over = scroll_ - std::clamp(scroll_, 0.0f, maxScroll);
        if (input.dragging)
        {
            scroll_ -= input.dy * (over != 0 ? motion::rubberGive(over, view) : 1.0f);
            velocity_ = 0;
            return;
        }
        if (input.touchEnded)
            velocity_ = -input.velocityY;
        const float page = input.repeated(ui::key::kRight) ? view - 60 : input.repeated(ui::key::kLeft) ? -(view - 60) : 0;
        float held = 0;
        if (input.held & ui::key::kDown)
            held += kHeldSpeed * dt;
        if (input.held & ui::key::kUp)
            held -= kHeldSpeed * dt;
        if (page != 0 || (held != 0 && over == 0))
        {
            scroll_ = std::clamp(std::clamp(scroll_, 0.0f, maxScroll) + page + held, 0.0f, maxScroll);
            velocity_ = 0;
        }
        else
        {
            motion::coast(scroll_, velocity_, 0, maxScroll, kFriction, dt);
        }
    }

    void tick(float dt) override
    {
        ui::approach(appear_, leaving_ ? 0.0f : 1.0f, dt, leaving_ ? 16 : 10);
        if (leaving_ && appear_ < 0.05f)
            close();
    }

    bool acceptsInput() const override { return !leaving_; }

    void draw() override
    {
        const float shown = ui::ease(appear_);
        {
            const Layer layer(0, 0, shown);
            fill(color::kBackground);
            gradientV(0, 0, metric::kWidth, metric::kHeight, mix(color::kBackground, color::kSecondary, 0.16f),
                      color::kBackground);

            {
                // what the app is
                const Layer column(0, (1 - shown) * 40, 1);
                float y = kPanelY + 24;
                brand(Brand::Icon, std::round(kColumnX + (kColumnW - brandWidth(Brand::Icon)) / 2), y);
                y += brandHeight(Brand::Icon) + 24;
                drawText("Themezer-NX", kColumnX + kColumnW / 2, y, metric::kTextTitle, color::kText, Align::Center);
                y += lineHeight(metric::kTextTitle);
                drawText("Version " APP_VERSION, kColumnX + kColumnW / 2, y, metric::kTextNormal, color::kTextDim,
                         Align::Center);
                y += lineHeight(metric::kTextNormal) + 24;
                y += drawWrapped("Free software under the GNU General Public License, version 3, with ABSOLUTELY NO "
                                 "WARRANTY. You may share and change it under that license.",
                                 kColumnX, y, kColumnW, metric::kTextSmall, color::kTextDim, 0, Align::Center);
                y += 24;
                drawText("Source code", kColumnX + kColumnW / 2, y, metric::kTextSmall, color::kTextDim, Align::Center);
                y += lineHeight(metric::kTextSmall);
                // on a line of its own, small enough to fit
                drawText("github.com/ThemezerNX/Themezer-NX", kColumnX + kColumnW / 2, y, metric::kTextSmall - 2,
                         color::kText, Align::Center, kColumnW);
            }

            {
                // the credits and the notices, scrolling
                const Layer panel((1 - shown) * 40, 0, 1);
                roundedRect(kPanelX, kPanelY, kPanelW, kPanelH, metric::kRadius, color::kSurfaceSolid);
                drawText("Credits and licenses", kPanelX + kTextInset, textTop(kPanelY + kHeader / 2, metric::kTextLarge),
                         metric::kTextLarge, color::kText);
                pushClip(kPanelX, kPanelY + kHeader, kPanelW, kPanelH - kHeader);
                const float step = lineHeight(metric::kTextSmall);
                const int first = std::max(0, static_cast<int>(scroll_ / step));
                for (int i = first; i < static_cast<int>(lines_.size()); ++i)
                {
                    const float y = kPanelY + kHeader + static_cast<float>(i) * step - std::round(scroll_);
                    if (y > kPanelY + kPanelH)
                        break;
                    drawText(lines_[static_cast<size_t>(i)], kPanelX + kTextInset, y, metric::kTextSmall, color::kTextDim);
                }
                popClip();
            }

            drawTopBar();
        }
        // the bar every screen has, not faded in with the page: faded, its keys are drawn without their depth
        hintBar_.draw(hints_);
    }

private:
    std::vector<std::string> lines_;
    float scroll_ = 0;
    float velocity_ = 0;
    float appear_ = 0;
    bool leaving_ = false;
    ui::HintBar hintBar_;
    std::vector<ui::Hint> hints_{{ui::key::kDown, "Scroll"}, {ui::key::kB, "Back"}};
};

} // namespace

void openAbout()
{
    App::get().scenes().emplace<AboutScene>();
}

} // namespace tz::scenes
