// The scenes that open over another, as Themezer-3DS has them: a sheet of actions, a question, a task in progress, and
// passing messages.

#pragma once

#include "gfx/draw.hpp"
#include "sys/jobs.hpp"
#include "sys/lock.hpp"
#include "sys/net.hpp"
#include "ui/scene.hpp"
#include "ui/widgets.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace tz::ui
{

struct Action
{
    gfx::Icon icon = gfx::Icon::Check;
    std::string label;
    std::string detail; // what it does, shown while it's selected
    bool enabled = true;
    bool danger = false;
    std::function<void()> run;
    // Instead of run: done with the sheet staying open, for a setting switched on and off; it changes the action.
    std::function<void(Action&)> toggle;
    Keys key = 0; // the button that does it without the sheet, shown at the end of its row
    bool checked = false; // the choice in use, ticked at the end of its row
};

// The actions as rows in a panel sliding up from the bottom of the screen.
class ActionSheet final : public Scene
{
public:
    // Starting on the action at selected, or the first that can be taken.
    ActionSheet(std::string title, std::vector<Action> actions, gfx::Color accent, int selected = -1);
    void update(Input& input, float dt) override;
    void draw() override;
    bool covers() const override { return false; }

private:
    float panelTop() const;
    float panelHeight() const;
    int visibleRows() const;
    bool hasDetails() const;

    std::string title_;
    std::vector<Action> actions_;
    gfx::Color accent_;
    int selected_ = 0;
    int first_ = 0;
    float highlight_ = -1;
    KeyPress press_;
    float appear_ = 0;
    std::function<void()> chosen_;
    bool closing_ = false;
};

// A question or a message with one or two buttons. B always picks the first (cancelling) button.
struct DialogButton
{
    std::string label;
    std::function<void()> run;
    bool danger = false;
    Keys key = 0; // the button that chooses it; 0 for the usual ones
};

class Dialog final : public Scene
{
public:
    // With one button, it's a message; with two, [0] is the cancelling one (B) and [1] the confirming one (A).
    Dialog(gfx::Icon icon, gfx::Color iconColor, std::string title, std::string message,
           std::vector<DialogButton> buttons = {});
    void update(Input& input, float dt) override;
    void draw() override;
    bool covers() const override { return false; }

private:
    void choose(size_t button);
    Keys keyOf(size_t button) const;

    gfx::Icon icon_;
    gfx::Color iconColor_;
    std::string title_;
    std::string message_;
    std::vector<DialogButton> buttons_;
    float appear_ = 0;
    struct Box
    {
        float x, y, w, h;
    };
    std::vector<Box> boxes_;
};

// What a task tells the progress overlay as it goes, from its worker thread.
class TaskProgress
{
public:
    void set(int done, int total)
    {
        done_ = done;
        total_ = total;
    }
    void setStatus(std::string status)
    {
        std::lock_guard held(lock_);
        status_ = std::move(status);
    }
    std::string status() const
    {
        std::lock_guard held(lock_);
        return status_;
    }
    // From 0 to 1, or -1 while it isn't known: the download's bytes while they're known, else done of total.
    float fraction() const;
    // For the task's downloads.
    net::Progress download;

private:
    std::atomic<int> done_{0};
    std::atomic<int> total_{0};
    mutable Lock lock_;
    std::string status_;
};

// A task that runs on the network worker while this shows its progress.
class ProgressOverlay final : public Scene
{
public:
    struct Options
    {
        std::string title;
        bool cancellable = true;
        gfx::Color accent = gfx::color::kPrimary;
    };

    // work runs on the worker and returns what finish then gets on the main thread.
    template <class Result>
    static std::unique_ptr<ProgressOverlay> start(Options options,
                                                  std::function<Result(TaskProgress&, const CancelToken&)> work,
                                                  std::function<void(Result)> finish)
    {
        auto overlay = std::unique_ptr<ProgressOverlay>(new ProgressOverlay(std::move(options)));
        auto progress = overlay->progress_;
        auto done = overlay->done_;
        jobs::run(jobs::Lane::Network, [work = std::move(work), finish = std::move(finish), progress, done,
                                        token = overlay->token_]() mutable {
            auto result = std::make_shared<Result>(work(*progress, token));
            jobs::post([finish = std::move(finish), result, done]() mutable {
                done->store(true);
                finish(std::move(*result));
            });
        });
        return overlay;
    }

    ~ProgressOverlay() override;
    void update(Input& input, float dt) override;
    void draw() override;
    bool covers() const override { return false; }

private:
    explicit ProgressOverlay(Options options) : options_(std::move(options)) {}

    Options options_;
    std::shared_ptr<TaskProgress> progress_ = std::make_shared<TaskProgress>();
    std::shared_ptr<std::atomic<bool>> done_ = std::make_shared<std::atomic<bool>>(false);
    CancelToken token_;
    float appear_ = 0;
    float shown_ = 0;
    bool cancelRequested_ = false;
};

// Short messages that fade by themselves, over everything above the hint bar.
enum class ToastKind : uint8_t
{
    Info,
    Success,
    Warning,
    Error,
};
void toast(std::string message, ToastKind kind = ToastKind::Info);
void updateToasts(float dt);
void drawToasts();

// The button's symbol at the end of a row, right-aligned on right. Returns where the label must end.
float drawActionKey(Keys key, float right, float middle);

} // namespace tz::ui
