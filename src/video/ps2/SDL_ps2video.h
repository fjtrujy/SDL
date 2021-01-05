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

#ifndef SDL_ps2video_h_
#define SDL_ps2video_h_

#include <GLES/egl.h>

#include "../../SDL_internal.h"
#include "../SDL_sysvideo.h"

typedef struct SDL_VideoData
{
    SDL_bool egl_initialized;   /* OpenGL ES device initialization status */
    uint32_t egl_refcount;      /* OpenGL ES reference count              */



} SDL_VideoData;


typedef struct SDL_DisplayData
{

} SDL_DisplayData;


typedef struct SDL_WindowData
{
    SDL_bool uses_gles;         /* if true window must support OpenGL ES */

} SDL_WindowData;




/****************************************************************************/
/* SDL_VideoDevice functions declaration                                    */
/****************************************************************************/

/* Display and window functions */
int PS2_VideoInit(_THIS);
void PS2_VideoQuit(_THIS);
void PS2_GetDisplayModes(_THIS, SDL_VideoDisplay * display);
int PS2_SetDisplayMode(_THIS, SDL_VideoDisplay * display, SDL_DisplayMode * mode);
int PS2_CreateWindow(_THIS, SDL_Window * window);
int PS2_CreateWindowFrom(_THIS, SDL_Window * window, const void *data);
void PS2_SetWindowTitle(_THIS, SDL_Window * window);
void PS2_SetWindowIcon(_THIS, SDL_Window * window, SDL_Surface * icon);
void PS2_SetWindowPosition(_THIS, SDL_Window * window);
void PS2_SetWindowSize(_THIS, SDL_Window * window);
void PS2_ShowWindow(_THIS, SDL_Window * window);
void PS2_HideWindow(_THIS, SDL_Window * window);
void PS2_RaiseWindow(_THIS, SDL_Window * window);
void PS2_MaximizeWindow(_THIS, SDL_Window * window);
void PS2_MinimizeWindow(_THIS, SDL_Window * window);
void PS2_RestoreWindow(_THIS, SDL_Window * window);
void PS2_SetWindowGrab(_THIS, SDL_Window * window, SDL_bool grabbed);
void PS2_DestroyWindow(_THIS, SDL_Window * window);

/* Window manager function */
SDL_bool PS2_GetWindowWMInfo(_THIS, SDL_Window * window,
                             struct SDL_SysWMinfo *info);

/* OpenGL/OpenGL ES functions */
int PS2_GL_LoadLibrary(_THIS, const char *path);
void *PS2_GL_GetProcAddress(_THIS, const char *proc);
void PS2_GL_UnloadLibrary(_THIS);
SDL_GLContext PS2_GL_CreateContext(_THIS, SDL_Window * window);
int PS2_GL_MakeCurrent(_THIS, SDL_Window * window, SDL_GLContext context);
int PS2_GL_SetSwapInterval(_THIS, int interval);
int PS2_GL_GetSwapInterval(_THIS);
int PS2_GL_SwapWindow(_THIS, SDL_Window * window);
void PS2_GL_DeleteContext(_THIS, SDL_GLContext context);

/* PS2 on screen keyboard */
SDL_bool PS2_HasScreenKeyboardSupport(_THIS);
void PS2_ShowScreenKeyboard(_THIS, SDL_Window *window);
void PS2_HideScreenKeyboard(_THIS, SDL_Window *window);
SDL_bool PS2_IsScreenKeyboardShown(_THIS, SDL_Window *window);

#endif /* SDL_ps2video_h_ */

/* vi: set ts=4 sw=4 expandtab: */
