/*
 * Accord - PS5 entry point.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The boilerplate supplies no main() of its own: it compiles exactly what
 * APP_SOURCE_DIR holds, so this file is the whole application entry.
 *
 * The shell opens the console's EGL display and the kit's GL renderer, loads
 * the font atlases the title ships in its .ffpkg, and then runs one frame at a
 * time: read the pad, step the screen, draw it, swap. The screens themselves
 * are the same functions the host renders (make host-snapshots), so a picture
 * reviewed on the PC is the picture this loop draws.
 *
 * Nothing here talks to Discord. The daemon owns the session; this title will
 * show what the daemon reports over 127.0.0.1.
 */

#include "core/app_info.hpp"
#include "core/log.hpp"
#include "platform/ps5/log_sink.hpp"
#include "ui/kit/core/save_file.hpp"
#include "ui/kit/gfx/font.hpp"
#include "ui/kit/gfx/renderer.hpp"
#include "ui/kit/platform/ps5/display_egl.hpp"
#include "ui/kit/platform/ps5/pad.hpp"
#include "ui/kit/platform/ps5/system.hpp"
#include "ui/kit/ui/components/keyboard.hpp"
#include "ui/kit/ui/feedback.hpp"
#include "ui/kit/ui/fonts.hpp"
#include "ui/kit/ui/theme.hpp"
#include "ui/screens.hpp"

#include <GL/glcorearb.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace
{
using accord::core::LogLevel;
using accord::core::log;

// Where the .ffpkg's files are mounted while the title runs; the fonts are
// staged into assets/fonts by tools/stage-ps5-sources.sh.
constexpr const char *kAssetsRoot = "/app0/assets";
// The theme the shell draws in. The host preview renders the same one, so the
// PNGs in build/snapshots match what the console shows.
constexpr const char *kThemeId = "acrylic";
// The on-screen keyboard, centred where the screens leave room for it.
constexpr hui::gfx::Rect kKeyboardBounds{560.0f, 600.0f, 800.0f, 380.0f};

const hui::ui::Theme *find_theme(std::string_view id)
{
    for (const hui::ui::Theme &theme : hui::ui::themes())
    {
        if (id == theme.id)
            return &theme;
    }
    return nullptr;
}

bool load_font(hui::gfx::Renderer &renderer, const char *file, hui::gfx::Font *font,
               hui::ui::FontRef *ref)
{
    const std::string path = std::string(kAssetsRoot) + "/fonts/" + file;
    std::string data;
    if (!hui::save::read_file(path, &data) || !font->load(data))
    {
        log(LogLevel::error, std::string("font failed: ") + path + ": " + font->error());
        return false;
    }
    ref->font = font;
    ref->texture = renderer.batch().create_font_texture(*font);
    return true;
}

// Everything the shell carries between frames.
struct Shell
{
    const hui::ui::Fonts &fonts;
    const hui::ui::Theme &theme;
    hui::gfx::Renderer &renderer;
    std::span<const accord::ui::Screen> screens;
    std::size_t screen = 0;
    // The keyboard is up: it owns the input, and the text it produces is kept
    // until there is a composer to put it in.
    bool typing = false;
    std::string typed;
    hui::ui::Keyboard keyboard;
};

void settle_screen(Shell &shell, std::ptrdiff_t step)
{
    const auto count = static_cast<std::ptrdiff_t>(shell.screens.size());
    const auto index = static_cast<std::ptrdiff_t>(shell.screen);
    shell.screen = static_cast<std::size_t>(((index + step) % count + count) % count);
    log(LogLevel::info, std::string("screen ") + shell.screens[shell.screen].title);
}

// The keyboard takes the frame while it is up; a Done key press puts it away
// and reports what was typed.
void drive_keyboard(Shell &shell, const hui::InputFrame &input, hui::ui::Feedback &feedback)
{
    const hui::ui::Event event = shell.keyboard.handle(input, feedback);
    if (event == hui::ui::Event::activated || event == hui::ui::Event::cancelled)
    {
        log(LogLevel::info, std::string("keyboard closed: \"") + shell.typed + "\"");
        shell.typing = false;
        shell.keyboard.set_active(false);
        shell.typed.clear();
    }
}

void drive_screen(Shell &shell, const hui::InputFrame &input)
{
    // Directions walk the screens the way the screens themselves are ordered;
    // Cross opens the next one, Circle goes back.
    if (input.nav == hui::Direction::right || input.is_pressed(hui::Action::page_next) ||
        input.is_pressed(hui::Action::confirm))
        settle_screen(shell, 1);
    else if (input.nav == hui::Direction::left || input.is_pressed(hui::Action::page_prev) ||
             input.is_pressed(hui::Action::back))
        settle_screen(shell, -1);
    else if (input.is_pressed(hui::Action::north))
    {
        shell.typing = true;
        shell.keyboard.set_active(true);
        shell.keyboard.enter();
        log(LogLevel::info, "keyboard opened");
    }
    else if (input.is_pressed(hui::Action::menu))
    {
        // No menu yet: Options is the way out of a development build.
        hui::sys::quit();
    }
}

void draw_frame(Shell &shell, float seconds)
{
    const accord::ui::Screen &screen = shell.screens[shell.screen];
    const accord::ui::ScreenContext context{shell.fonts, shell.theme,
                                            shell.renderer.glass_texture(), seconds};
    hui::gfx::DrawList list;
    screen.draw(list, context);

    if (shell.typing)
    {
        // A scrim, then the keyboard on top of the screen it belongs to.
        list.rounded_rect({0.0f, 0.0f, 1920.0f, 1080.0f}, 0.0f,
                          hui::gfx::Color::rgb(0x05060d, 0.55f));
        hui::ui::Canvas canvas{list, shell.fonts, shell.renderer.glass_texture(), seconds};
        shell.keyboard.draw(canvas);
    }

    hui::gfx::BackdropSpec backdrop = shell.theme.backdrop;
    backdrop.time = seconds;
    shell.renderer.begin();
    shell.renderer.backdrop(backdrop);
    shell.renderer.draw(list);
    shell.renderer.glass();
}
} // namespace

int main()
{
    accord::ps5::install_log_sink();

    const accord::core::AppInfo &info = accord::core::app_info();
    log(LogLevel::info, std::string(info.name) + " " + std::string(info.version));

    const hui::ui::Theme *theme = find_theme(kThemeId);
    if (theme == nullptr)
    {
        log(LogLevel::error, std::string("no theme named ") + kThemeId);
        hui::sys::park();
    }

    hui::ps5::Display display;
    if (!display.open())
    {
        log(LogLevel::error, "display open failed");
        hui::sys::park();
    }

    hui::gfx::Renderer renderer;
    hui::gfx::Font regular;
    hui::gfx::Font semibold;
    hui::gfx::Font display_font;
    hui::gfx::Font mono;
    hui::gfx::Font pixel;
    hui::gfx::Font hand;
    hui::ui::Fonts fonts;
    if (!renderer.init() ||
        !load_font(renderer, "inter-regular.huifont", &regular, &fonts.regular) ||
        !load_font(renderer, "inter-semibold.huifont", &semibold, &fonts.semibold) ||
        !load_font(renderer, "montserrat-medium.huifont", &display_font, &fonts.display) ||
        !load_font(renderer, "dejavu-sans-mono.huifont", &mono, &fonts.mono) ||
        !load_font(renderer, "press-start-2p.huifont", &pixel, &fonts.pixel) ||
        !load_font(renderer, "patrick-hand.huifont", &hand, &fonts.hand))
    {
        log(LogLevel::error, "renderer init or fonts failed");
        hui::sys::park();
    }

    hui::ps5::Pad pad;
    pad.open();
    hui::InputTracker tracker;

    Shell shell{fonts, *theme, renderer, accord::ui::screens()};
    shell.keyboard.style.theme = *theme;
    shell.keyboard.style.bindings = hui::ui::KeyboardBindings::standard();
    shell.keyboard.set_bounds(kKeyboardBounds);
    shell.keyboard.set_active(false);
    shell.keyboard.on_text = [&shell](std::string_view utf8) { shell.typed += utf8; };
    shell.keyboard.on_backspace = [&shell]() {
        if (!shell.typed.empty())
            shell.typed.pop_back();
    };
    log(LogLevel::info, std::string("shell ready: ") + shell.screens.front().title);

    const std::int64_t start = hui::sys::monotonic_us();
    std::int64_t previous = start;
    hui::PadSample samples[64];
    std::uint64_t frames = 0;
    for (;;)
    {
        const std::int64_t now = hui::sys::monotonic_us();
        float dt = frames == 0 ? 1.0f / 60.0f : static_cast<float>(now - previous) / 1e6f;
        previous = now;
        if (dt > 0.05f)
            dt = 0.05f; // a hitch must not teleport the animations

        const std::size_t count = pad.read(samples);
        const hui::InputFrame input =
            tracker.update(std::span<const hui::PadSample>(samples, count),
                           static_cast<std::uint64_t>(now));

        hui::ui::Feedback feedback;
        if (shell.typing)
        {
            drive_keyboard(shell, input, feedback);
            shell.keyboard.set_length(static_cast<int>(shell.typed.size()));
        }
        else
        {
            drive_screen(shell, input);
        }
        shell.keyboard.update(dt);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        draw_frame(shell, static_cast<float>(now - start) / 1e6f);
        renderer.present(0, display.width(), display.height());

        if (!display.swap())
        {
            log(LogLevel::error, std::string("swap failed: ") +
                                     hui::ps5::egl_error_name(display.last_error()));
            hui::sys::park();
        }
        ++frames;
        if (frames == 1)
        {
            log(LogLevel::info, std::string("first swap ok, draws=") +
                                    std::to_string(renderer.last_draw_calls()));
            hui::sys::hide_splash_screen();
        }
        else if (frames % 600 == 0)
        {
            log(LogLevel::info, std::string("frame ") + std::to_string(frames) + ", draws=" +
                                    std::to_string(renderer.last_draw_calls()));
        }
    }
}
