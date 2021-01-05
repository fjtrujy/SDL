/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2021 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

#include "../../SDL_internal.h"

#if SDL_VIDEO_DRIVER_PS2

/* SDL internals */
#include "../SDL_sysvideo.h"
#include "SDL_version.h"
#include "SDL_syswm.h"
#include "SDL_loadso.h"
#include "SDL_events.h"
#include "../../events/SDL_mouse_c.h"
#include "../../events/SDL_keyboard_c.h"



/* PS2 declarations */
#include "SDL_ps2video.h"
#include "SDL_ps2events_c.h"
#include "SDL_ps2gl_c.h"

/* unused
static SDL_bool PS2_initialized = SDL_FALSE;
*/

static void
PS2_Destroy(SDL_VideoDevice * device)
{
/*    SDL_VideoData *phdata = (SDL_VideoData *) device->driverdata; */

    if (device->driverdata != NULL) {
        device->driverdata = NULL;
    }
}

static SDL_VideoDevice *
PS2_Create()
{
    SDL_VideoDevice *device;
    SDL_VideoData *phdata;
    SDL_GLDriverData *gldata;

    /* Initialize SDL_VideoDevice structure */
    device = (SDL_VideoDevice *) SDL_calloc(1, sizeof(SDL_VideoDevice));
    if (device == NULL) {
        SDL_OutOfMemory();
        return NULL;
    }

    /* Initialize internal PS2 specific data */
    phdata = (SDL_VideoData *) SDL_calloc(1, sizeof(SDL_VideoData));
    if (phdata == NULL) {
        SDL_OutOfMemory();
        SDL_free(device);
        return NULL;
    }

        gldata = (SDL_GLDriverData *) SDL_calloc(1, sizeof(SDL_GLDriverData));
    if (gldata == NULL) {
        SDL_OutOfMemory();
        SDL_free(device);
        SDL_free(phdata);
        return NULL;
    }
    device->gl_data = gldata;

    device->driverdata = phdata;

    phdata->egl_initialized = SDL_TRUE;


    /* Setup amount of available displays */
    device->num_displays = 0;

    /* Set device free function */
    device->free = PS2_Destroy;

    /* Setup all functions which we can handle */
    device->VideoInit = PS2_VideoInit;
    device->VideoQuit = PS2_VideoQuit;
    device->GetDisplayModes = PS2_GetDisplayModes;
    device->SetDisplayMode = PS2_SetDisplayMode;
    device->CreateSDLWindow = PS2_CreateWindow;
    device->CreateSDLWindowFrom = PS2_CreateWindowFrom;
    device->SetWindowTitle = PS2_SetWindowTitle;
    device->SetWindowIcon = PS2_SetWindowIcon;
    device->SetWindowPosition = PS2_SetWindowPosition;
    device->SetWindowSize = PS2_SetWindowSize;
    device->ShowWindow = PS2_ShowWindow;
    device->HideWindow = PS2_HideWindow;
    device->RaiseWindow = PS2_RaiseWindow;
    device->MaximizeWindow = PS2_MaximizeWindow;
    device->MinimizeWindow = PS2_MinimizeWindow;
    device->RestoreWindow = PS2_RestoreWindow;
    device->SetWindowGrab = PS2_SetWindowGrab;
    device->DestroyWindow = PS2_DestroyWindow;
#if 0
    device->GetWindowWMInfo = PS2_GetWindowWMInfo;
#endif
    device->GL_LoadLibrary = PS2_GL_LoadLibrary;
    device->GL_GetProcAddress = PS2_GL_GetProcAddress;
    device->GL_UnloadLibrary = PS2_GL_UnloadLibrary;
    device->GL_CreateContext = PS2_GL_CreateContext;
    device->GL_MakeCurrent = PS2_GL_MakeCurrent;
    device->GL_SetSwapInterval = PS2_GL_SetSwapInterval;
    device->GL_GetSwapInterval = PS2_GL_GetSwapInterval;
    device->GL_SwapWindow = PS2_GL_SwapWindow;
    device->GL_DeleteContext = PS2_GL_DeleteContext;
    device->HasScreenKeyboardSupport = PS2_HasScreenKeyboardSupport;
    device->ShowScreenKeyboard = PS2_ShowScreenKeyboard;
    device->HideScreenKeyboard = PS2_HideScreenKeyboard;
    device->IsScreenKeyboardShown = PS2_IsScreenKeyboardShown;

    device->PumpEvents = PS2_PumpEvents;

    return device;
}

VideoBootStrap PS2_bootstrap = {
    "PS2",
    "PS2 Video Driver",
    PS2_Create
};

/*****************************************************************************/
/* SDL Video and Display initialization/handling functions                   */
/*****************************************************************************/
int
PS2_VideoInit(_THIS)
{
    SDL_VideoDisplay display;
    SDL_DisplayMode current_mode;

    SDL_zero(current_mode);

    current_mode.w = 480;
    current_mode.h = 272;

    current_mode.refresh_rate = 60;
    /* 32 bpp for default */
    current_mode.format = SDL_PIXELFORMAT_ABGR8888;

    current_mode.driverdata = NULL;

    SDL_zero(display);
    display.desktop_mode = current_mode;
    display.current_mode = current_mode;
    display.driverdata = NULL;

    SDL_AddVideoDisplay(&display, SDL_FALSE);

    return 1;
}

void
PS2_VideoQuit(_THIS)
{

}

void
PS2_GetDisplayModes(_THIS, SDL_VideoDisplay * display)
{

}

int
PS2_SetDisplayMode(_THIS, SDL_VideoDisplay * display, SDL_DisplayMode * mode)
{
    return 0;
}
#define EGLCHK(stmt)                            \
    do {                                        \
        EGLint err;                             \
                                                \
        stmt;                                   \
        err = eglGetError();                    \
        if (err != EGL_SUCCESS) {               \
            SDL_SetError("EGL error %d", err);  \
            return 0;                           \
        }                                       \
    } while (0)

int
PS2_CreateWindow(_THIS, SDL_Window * window)
{
    SDL_WindowData *wdata;

    /* Allocate window internal data */
    wdata = (SDL_WindowData *) SDL_calloc(1, sizeof(SDL_WindowData));
    if (wdata == NULL) {
        return SDL_OutOfMemory();
    }

    /* Setup driver data for this window */
    window->driverdata = wdata;


    /* Window has been successfully created */
    return 0;
}

int
PS2_CreateWindowFrom(_THIS, SDL_Window * window, const void *data)
{
    return SDL_Unsupported();
}

void
PS2_SetWindowTitle(_THIS, SDL_Window * window)
{
}
void
PS2_SetWindowIcon(_THIS, SDL_Window * window, SDL_Surface * icon)
{
}
void
PS2_SetWindowPosition(_THIS, SDL_Window * window)
{
}
void
PS2_SetWindowSize(_THIS, SDL_Window * window)
{
}
void
PS2_ShowWindow(_THIS, SDL_Window * window)
{
}
void
PS2_HideWindow(_THIS, SDL_Window * window)
{
}
void
PS2_RaiseWindow(_THIS, SDL_Window * window)
{
}
void
PS2_MaximizeWindow(_THIS, SDL_Window * window)
{
}
void
PS2_MinimizeWindow(_THIS, SDL_Window * window)
{
}
void
PS2_RestoreWindow(_THIS, SDL_Window * window)
{
}
void
PS2_SetWindowGrab(_THIS, SDL_Window * window, SDL_bool grabbed)
{

}
void
PS2_DestroyWindow(_THIS, SDL_Window * window)
{
}

/*****************************************************************************/
/* SDL Window Manager function                                               */
/*****************************************************************************/
#if 0
SDL_bool
PS2_GetWindowWMInfo(_THIS, SDL_Window * window, struct SDL_SysWMinfo *info)
{
    if (info->version.major <= SDL_MAJOR_VERSION) {
        return SDL_TRUE;
    } else {
        SDL_SetError("Application not compiled with SDL %d.%d",
                     SDL_MAJOR_VERSION, SDL_MINOR_VERSION);
        return SDL_FALSE;
    }

    /* Failed to get window manager information */
    return SDL_FALSE;
}
#endif


/* TO Write Me */
SDL_bool PS2_HasScreenKeyboardSupport(_THIS)
{
    return SDL_FALSE;
}
void PS2_ShowScreenKeyboard(_THIS, SDL_Window *window)
{
}
void PS2_HideScreenKeyboard(_THIS, SDL_Window *window)
{
}
SDL_bool PS2_IsScreenKeyboardShown(_THIS, SDL_Window *window)
{
    return SDL_FALSE;
}


#endif /* SDL_VIDEO_DRIVER_PS2 */

/* vi: set ts=4 sw=4 expandtab: */
