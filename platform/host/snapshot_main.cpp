/*
 * Discord PS5 - Host snapshot runner: renders every screen off-screen to PNG.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Lives in platform/host/ because it is a host substitute: it must never reach
 * the console, and the staging script excludes this directory for that reason.
 *
 * usage: hui_snapshots <assets dir> <output dir> [theme id]
 *
 * Opens a surfaceless Mesa (llvmpipe) EGL context and calls the same screen
 * functions the console will call, so the picture reviewed here is the picture
 * the console draws.
 */

#include "ui/kit/gfx/gl_program.hpp"
#include "ui/kit/gfx/renderer.hpp"
#include "ui/kit/ui/fonts.hpp"
#include "ui/kit/ui/theme.hpp"
#include "ui/screens.hpp"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/glcorearb.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "ui/kit/third_party/stb/stb_image_write.h"

namespace
{
constexpr int kWidth = 1920;
constexpr int kHeight = 1080;

bool open_context()
{
    auto get_platform_display = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
        eglGetProcAddress("eglGetPlatformDisplayEXT"));
    EGLDisplay display =
        get_platform_display != nullptr
            ? get_platform_display(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr)
            : eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0;
    EGLint minor = 0;
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor) ||
        !eglBindAPI(EGL_OPENGL_API))
    {
        return false;
    }
    const EGLint attributes[] = {EGL_CONTEXT_MAJOR_VERSION, 4, EGL_CONTEXT_MINOR_VERSION, 5,
                                 EGL_CONTEXT_OPENGL_PROFILE_MASK,
                                 EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
    EGLContext context =
        eglCreateContext(display, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attributes);
    return context != EGL_NO_CONTEXT &&
           eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context);
}

bool read_file(const std::string &path, std::string *out)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return false;
    }
    file.seekg(0, std::ios::end);
    out->resize(static_cast<std::size_t>(file.tellg()));
    file.seekg(0, std::ios::beg);
    file.read(out->data(), static_cast<std::streamsize>(out->size()));
    return file.good() || file.eof();
}

bool load_font(hui::gfx::Renderer &renderer, const std::string &path, hui::gfx::Font *font,
               hui::ui::FontRef *ref)
{
    std::string data;
    if (!read_file(path, &data) || !font->load(data))
    {
        std::fprintf(stderr, "cannot load font %s\n", path.c_str());
        return false;
    }
    ref->font = font;
    ref->texture = renderer.batch().create_font_texture(*font);
    return true;
}

const hui::ui::Theme *find_theme(std::string_view id)
{
    const std::span<const hui::ui::Theme> all = hui::ui::themes();
    for (const hui::ui::Theme &theme : all)
    {
        if (id == theme.id)
        {
            return &theme;
        }
    }
    return nullptr;
}
} // namespace

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: %s <assets dir> <output dir> [theme id]\n", argv[0]);
        return 2;
    }
    const std::string assets = argv[1];
    const std::string output = argv[2];
    const std::string theme_id = argc > 3 ? argv[3] : "acrylic";

    const hui::ui::Theme *theme = find_theme(theme_id);
    if (theme == nullptr)
    {
        std::fprintf(stderr, "no theme named '%s'\n", theme_id.c_str());
        return 2;
    }

    if (!open_context())
    {
        std::fprintf(stderr, "no surfaceless EGL OpenGL 4.5 context\n");
        return 1;
    }
    std::fprintf(stderr, "GL %s / %s\n",
                 reinterpret_cast<const char *>(glGetString(GL_VERSION)),
                 reinterpret_cast<const char *>(glGetString(GL_RENDERER)));
    hui::gfx::set_glsl_prefix("#version 450 core\n");

    hui::gfx::Renderer renderer;
    hui::gfx::Font regular;
    hui::gfx::Font semibold;
    hui::gfx::Font display;
    hui::gfx::Font mono;
    hui::gfx::Font pixel;
    hui::gfx::Font hand;
    hui::ui::Fonts fonts;
    if (!renderer.init() ||
        !load_font(renderer, assets + "/fonts/inter-regular.huifont", &regular, &fonts.regular) ||
        !load_font(renderer, assets + "/fonts/inter-semibold.huifont", &semibold,
                   &fonts.semibold) ||
        !load_font(renderer, assets + "/fonts/montserrat-medium.huifont", &display,
                   &fonts.display) ||
        !load_font(renderer, assets + "/fonts/dejavu-sans-mono.huifont", &mono, &fonts.mono) ||
        !load_font(renderer, assets + "/fonts/press-start-2p.huifont", &pixel, &fonts.pixel) ||
        !load_font(renderer, assets + "/fonts/patrick-hand.huifont", &hand, &fonts.hand))
    {
        return 1;
    }

    GLuint framebuffer = 0;
    GLuint colour = 0;
    glGenFramebuffers(1, &framebuffer);
    glGenRenderbuffers(1, &colour);
    glBindRenderbuffer(GL_RENDERBUFFER, colour);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, kWidth, kHeight);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, colour);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        std::fprintf(stderr, "framebuffer incomplete\n");
        return 1;
    }

    std::vector<unsigned char> pixels(static_cast<std::size_t>(kWidth) * kHeight * 4);
    stbi_flip_vertically_on_write(1);

    // Two passes: the first fills the renderer's glass texture, so a theme whose
    // surfaces are frosted has something to blur on the pass that is written.
    const std::span<const discord_ps5::ui::Screen> all = discord_ps5::ui::screens();
    for (int pass = 0; pass < 2; ++pass)
    {
        for (const discord_ps5::ui::Screen &screen : all)
        {
            hui::gfx::DrawList list;
            const discord_ps5::ui::ScreenContext context{fonts, *theme,
                                                         renderer.glass_texture(), 4.0f};
            screen.draw(list, context);

            hui::gfx::BackdropSpec spec = theme->backdrop;
            spec.time = 4.0f;

            glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            renderer.begin();
            renderer.backdrop(spec);
            renderer.draw(list);
            renderer.glass();
            renderer.present(framebuffer, kWidth, kHeight);

            if (pass == 0)
            {
                continue;
            }

            glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
            glReadPixels(0, 0, kWidth, kHeight, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            for (std::size_t index = 3; index < pixels.size(); index += 4)
            {
                pixels[index] = 255;
            }
            const std::string path = output + "/" + screen.id + ".png";
            if (stbi_write_png(path.c_str(), kWidth, kHeight, 4, pixels.data(), kWidth * 4) == 0)
            {
                std::fprintf(stderr, "cannot write %s\n", path.c_str());
                return 1;
            }
            std::fprintf(stderr, "%s  %s  (%zu draw calls, GL error 0x%x)\n", screen.id,
                         screen.title, renderer.last_draw_calls(), glGetError());
        }
    }

    std::printf("wrote %zu screens to %s\n", all.size(), output.c_str());
    return 0;
}
