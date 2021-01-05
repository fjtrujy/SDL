/*
    SDL_ps2_main.c, placed in the public domain by Sam Lantinga  3/13/14
*/
#include "SDL_config.h"

#ifdef __PS2__

#include "SDL_main.h"
#include <ps2kernel.h>
#include <ps2debug.h>
#include <ps2sdk.h>
#include <ps2threadman.h>
#include <stdlib.h>
#include <stdio.h>

/* If application's main() is redefined as SDL_main, and libSDLmain is
   linked, then this file will create the standard exit callback,
   define the PS2_MODULE_INFO macro, and exit back to the browser when
   the program is finished.

   You can still override other parameters in your own code if you
   desire, such as PS2_HEAP_SIZE_KB, PS2_MAIN_THREAD_ATTR,
   PS2_MAIN_THREAD_STACK_SIZE, etc.
*/

PS2_MODULE_INFO("SDL App", 0, 1, 1);

// int sdl_ps2_exit_callback(int arg1, int arg2, void *common)
// {
//     exit(0);
//     return 0;
// }

// int sdl_ps2_callback_thread(SceSize args, void *argp)
// {
//     int cbid;
//     cbid = sceKernelCreateCallback("Exit Callback",
//                        sdl_ps2_exit_callback, NULL);
//     sceKernelRegisterExitCallback(cbid);
//     sceKernelSleepThreadCB();
//     return 0;
// }

// int sdl_ps2_setup_callbacks(void)
// {
//     int thid = 0;
//     thid = sceKernelCreateThread("update_thread",
//                      sdl_ps2_callback_thread, 0x11, 0xFA0, 0, 0);
//     if(thid >= 0)
//         sceKernelStartThread(thid, 0, 0);
//     return thid;
// }

int main(int argc, char *argv[])
{
    // ps2DebugScreenInit();
    // sdl_ps2_setup_callbacks();

    // /* Register sceKernelExitGame() to be called when we exit */
    // atexit(sceKernelExitGame);

    SDL_SetMainReady();

    (void)SDL_main(argc, argv);
    return 0;
}

#endif /* __PS2__ */

/* vi: set ts=4 sw=4 expandtab: */
