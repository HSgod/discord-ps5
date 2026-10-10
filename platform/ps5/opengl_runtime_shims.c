/*
 * Accord - process-level runtime shims for the OpenGL runtime.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The title links ps5-opengl (and the Mesa runtime inside it) statically, and
 * that runtime references a handful of process-level entry points a native
 * title has to supply itself. Modelled on ps5-opengl's native-app shims; only
 * what this title needs is here.
 *
 * Two groups:
 *
 *   - the splash hold. The runtime asks the system to hide the splash as soon
 *     as the display opens, which would leave a black screen while shaders
 *     build and the font atlases load. Every such call is routed here by
 *     --wrap (see APP_WRAP_SYMBOLS in the Makefile) and held until the shell
 *     has presented a frame. ui/kit/platform/ps5/system.cpp already calls the
 *     weak hui_release_splash() for exactly this.
 *   - libc entry points the SDK binds to libraries a native title does not
 *     load: reaching one of those jumps to address 0. The runtime calls
 *     mkstemp() for its shader cache and popen()/openlog() from its debug
 *     paths, so those live here instead of in an import.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int sceKernelUsleep(unsigned int microseconds);
extern uint64_t sceKernelGetProcessTime(void);

/* Mesa's GL dispatch keeps its context in a thread_local; this is the C++
 * thread-storage guard the runtime calls before the first access. */
void hui_glapi_tls_context_init(void) __asm__("_ZTH23_mesa_glapi_tls_Context");
void hui_glapi_tls_context_init(void)
{
}

/* Defined in a plain object rather than pulled from an import: the caller is
 * the runtime's own assert, and there is no shell libc to fall back on. */
void __assert(const char *function, const char *file, int line, const char *expression)
{
    fprintf(stderr, "[accord] assertion failed: %s (%s:%d, %s)\n", expression, file, line,
            function);
    abort();
}

/* mkstemp's contract: six trailing 'X' before the suffix, and the answer is an
 * open descriptor to a file nobody else has. The name only has to be
 * unpredictable and unique within the process. */
int mkstemps(char *template_name, int suffix_length)
{
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    static unsigned int counter;
    const size_t length = template_name != NULL ? strlen(template_name) : 0;

    if (suffix_length < 0 || length < (size_t)suffix_length + 6u)
    {
        errno = EINVAL;
        return -1;
    }
    char *stamp = template_name + length - (size_t)suffix_length - 6u;
    for (int i = 0; i < 6; ++i)
    {
        if (stamp[i] != 'X')
        {
            errno = EINVAL;
            return -1;
        }
    }

    uint64_t state = sceKernelGetProcessTime();
    for (int attempt = 0; attempt < 64; ++attempt)
    {
        state += counter++ * UINT64_C(0x9E3779B97F4A7C15) + UINT64_C(1);
        uint64_t value = state;
        for (int i = 0; i < 6; ++i)
        {
            stamp[i] = alphabet[value % 36u];
            value /= 36u;
        }
        const int file = open(template_name, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (file >= 0 || errno != EEXIST)
            return file;
    }
    errno = EEXIST;
    return -1;
}

int mkstemp(char *template_name)
{
    return mkstemps(template_name, 0);
}

int isatty(int descriptor)
{
    (void)descriptor;
    errno = ENOTTY;
    return 0;
}

/* The runtime's debug paths: there is no syslog and no shell to run a command
 * in, and both callers check for failure. */
void openlog(const char *identifier, int option, int facility)
{
    (void)identifier;
    (void)option;
    (void)facility;
}

FILE *popen(const char *command, const char *mode)
{
    (void)command;
    (void)mode;
    errno = ENOSYS;
    return NULL;
}

int pclose(FILE *stream)
{
    (void)stream;
    errno = ENOSYS;
    return -1;
}

/* The splash hold, as --wrap sees it. */
extern int __real_sceSystemServiceHideSplashScreen(void);
static int accord_splash_released;

void hui_release_splash(void)
{
    accord_splash_released = 1;
}

int __wrap_sceSystemServiceHideSplashScreen(void)
{
    return accord_splash_released ? __real_sceSystemServiceHideSplashScreen() : 0;
}
