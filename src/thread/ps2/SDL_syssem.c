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

/* Semaphore functions for the PS2. */

#include <stdio.h>
#include <stdlib.h>

#include "SDL_error.h"
#include "SDL_thread.h"

#include <Kernel.h>

struct SDL_semaphore {
    Sint32  semid;
};


/* Create a semaphore */
SDL_sem *SDL_CreateSemaphore(Uint32 initial_value)
{
    SDL_sem *sem;
	ee_sema_t ps2sem;

    sem = (SDL_sem *) malloc(sizeof(*sem));
    if (sem != NULL) {
        /* TODO: Figure out the limit on the maximum value. */
        ps2sem.init_count = initial_value;
	    ps2sem.max_count = initial_value;
	    ps2sem.option = 0;
	    ps2sem.attr = 0;

	    sem->semid = CreateSema(&ps2sem);
        if (sem->semid < 0) {
            SDL_SetError("Couldn't create semaphore");
            free(sem);
            sem = NULL;
        }
    } else {
        SDL_OutOfMemory();
    }

    return sem;
}

/* Free the semaphore */
void SDL_DestroySemaphore(SDL_sem *sem)
{
    if (sem != NULL) {
        if (sem->semid > 0) {
            DeleteSema(sem->semid);
            sem->semid = 0;
        }

        free(sem);
    }
}

/* TODO: SDL_SemWaitTimeout is not fully implemented, just work with 0 timeout */
int SDL_SemWaitTimeout(SDL_sem *sem, Uint32 timeout)
{
    int res;

    if (sem == NULL) {
        SDL_SetError("Passed a NULL sem");
        return 0;
    }

    if (timeout == 0) {
        res = PollSema(sem->semid);
        if (res < 0) {
            return SDL_MUTEX_TIMEDOUT;
        }
        return 0;
    }

    printf("** SDL_SemWaitTimeout not implemented **\n");
	return 0;
}

int SDL_SemTryWait(SDL_sem *sem)
{
    return SDL_SemWaitTimeout(sem, 0);
}

int SDL_SemWait(SDL_sem *sem)
{
    return SDL_SemWaitTimeout(sem, SDL_MUTEX_MAXWAIT);
}

/* Returns the current count of the semaphore */
Uint32 SDL_SemValue(SDL_sem *sem)
{
    ee_sema_t info;

    if (sem == NULL) {
        SDL_SetError("Passed a NULL sem");
        return 0;
    }

    if (ReferSemaStatus(sem->semid, (ee_sema_t *)&info) >= 0) {
        return info.count;
    }

    return 0;
}

int SDL_SemPost(SDL_sem *sem)
{
    if (sem == NULL) {
        return SDL_SetError("Passed a NULL sem");
    }

    if (SignalSema(sem->semid) < 0) {
        return SDL_SetError("SignalSema() failed");
    }

    return 0;
}

#endif /* SDL_THREAD_PS2 */

/* vim: ts=4 sw=4
 */