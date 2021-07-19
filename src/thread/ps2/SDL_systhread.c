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

#if SDL_THREAD_PS2

/* PS2 thread management routines for SDL */

#include <stdio.h>
#include <stdlib.h>
#include <Kernel.h>

#include "SDL_error.h"
#include "SDL_thread.h"
#include "../SDL_systhread.h"
#include "../SDL_thread_c.h"

extern void *_gp;

static void ThreadEntry(void *argp)
{
    SDL_RunThread(*(SDL_Thread **) argp);
}

int SDL_SYS_CreateThread(SDL_Thread *thread)
{
    ee_thread_status_t status;
    ee_thread_t threadData;
    int size;
    int priority = 32;

    /* Set priority of new thread to the same as the current thread */
    if (ReferThreadStatus(GetThreadId(), &status) == 0) {
        priority = status.current_priority;
    }

    size = thread->stacksize ? ((int) thread->stacksize) : 0x8000;
    u8 ThreadStack[size] __attribute__ ((aligned(16)));

    threadData.initial_priority	= priority;
    threadData.current_priority	= priority;
	threadData.stack_size		= size;
	threadData.gp_reg			= &_gp;
	threadData.func				= (void *)ThreadEntry;
	threadData.stack			= (void *)ThreadStack;
	threadData.option			= 0;
	threadData.attr				= 0;
	
    thread->handle = CreateThread(&threadData);
    if (thread->handle < 0) {
        return SDL_SetError("CreateThread() failed");
    }

    StartThread(thread->handle, &thread);
    return 0;
}

void SDL_SYS_SetupThread(const char *name)
{
    /* Do nothing. */
}

SDL_threadID SDL_ThreadID(void)
{
    return (SDL_threadID) GetThreadId();
}

void SDL_SYS_WaitThread(SDL_Thread *thread)
{
    ReleaseWaitThread(thread->handle);
    DeleteThread(thread->handle);
}

void SDL_SYS_DetachThread(SDL_Thread *thread)
{
    /* !!! FIXME: is this correct? */
    DeleteThread(thread->handle);
}

void SDL_SYS_KillThread(SDL_Thread *thread)
{
    TerminateThread(thread->handle);
}

int SDL_SYS_SetThreadPriority(SDL_ThreadPriority priority)
{
    int value;

    if (priority == SDL_THREAD_PRIORITY_LOW) {
        value = 19;
    } else if (priority == SDL_THREAD_PRIORITY_HIGH) {
        value = -10;
    } else if (priority == SDL_THREAD_PRIORITY_TIME_CRITICAL) {
        value = -20;
    } else {
        value = 0;
    }

    return ChangeThreadPriority(GetThreadId(),value);

}

#endif /* SDL_THREAD_PS2 */

/* vim: ts=4 sw=4
 */