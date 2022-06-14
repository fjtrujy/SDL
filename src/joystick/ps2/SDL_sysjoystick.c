/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2022 Sam Lantinga <slouken@libsdl.org>

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

#ifdef SDL_JOYSTICK_PS2

#include <debug.h>
#include <libmtap.h>
#include <libpad.h>
#include <loadfile.h>
#include <stdio.h>

#include "../SDL_joystick_c.h"
#include "../SDL_sysjoystick.h"
#include "SDL_events.h"
#include "SDL_joystick.h"

/* we must load our IRXs */
#define IRX_LOAD 1

/* update changed buttons/axis (or force update) */
#define UPDATE_ONLY_IF_CHANGED 1

#ifdef IRX_LOAD
extern unsigned char sio2man_irx[];
extern unsigned int size_sio2man_irx;
extern unsigned char padman_irx[];
extern unsigned int size_padman_irx;
extern unsigned char mtapman_irx[];
extern unsigned int size_mtapman_irx;
#endif

/* define this if you dont want debug messages */
/* #define DBG(...) */

/* define one of these if you want debug messages */
#define DBG(...) printf(__VA_ARGS__)
/* #define DBG(...) fprintf(stderr, __VA_ARGS__) */
/* #define DBG(...)                \
    scr_setbgcolor(0x80808080); \
    scr_printf(__VA_ARGS__);    \
    scr_setbgcolor(0); */

/* comment this if you dont want debug messages */
#define DBG_CODE

#pragma message("PS2 JOYSTICK")

#define MAX_JOYSTICKS 8
#define MAX_AXES      4
#define MAX_BUTTONS   16

static char padbufs[MAX_JOYSTICKS * 256] __attribute__((aligned(64)));

static struct
{
    int numjoysticks;
    int joyports[MAX_JOYSTICKS];
    int joyslots[MAX_JOYSTICKS];
} PS2;

struct joystick_hwdata
{
    int deviceid;
    int rumble;
    int port;
    int slot;
#ifdef UPDATE_ONLY_IF_CHANGED
    int prev_pad;
    int prev_ljoy_h;
    int prev_ljoy_v;
    int prev_rjoy_h;
    int prev_rjoy_v;
#endif
};

#ifdef UPDATE_ONLY_IF_CHANGED

int button_table[] = {
    PAD_LEFT,
    PAD_DOWN,
    PAD_RIGHT,
    PAD_UP,
    PAD_START,
    PAD_R3,
    PAD_L3,
    PAD_SELECT,
    PAD_SQUARE,
    PAD_CROSS,
    PAD_CIRCLE,
    PAD_TRIANGLE,
    PAD_R1,
    PAD_L1,
    PAD_R2,
    PAD_L2,
};

#ifdef DBG_CODE
char button_table_name[16][16] = {
    "PAD_LEFT",
    "PAD_DOWN",
    "PAD_RIGHT",
    "PAD_UP",
    "PAD_START",
    "PAD_R3",
    "PAD_L3",
    "PAD_SELECT",
    "PAD_SQUARE",
    "PAD_CROSS",
    "PAD_CIRCLE",
    "PAD_TRIANGLE",
    "PAD_R1",
    "PAD_L1",
    "PAD_R2",
    "PAD_L2",
};
#endif
#endif /* UPDATE_ONLY_IF_CHANGED */

/* C lixo, C nóia... */
static void
PS2_JoystickDetect(void);

static int
PS2_JoystickInit(void)
{
    DBG("!PS2_JoystickInit()\n");

#ifdef IRX_LOAD
    /* load IRXs */
    int ret;
    SifExecModuleBuffer(sio2man_irx, size_sio2man_irx, 0, NULL, &ret);
    if (ret < 0) {
        SDL_SetError("Failed to load SIO2MAN");
        DBG("\tERROR: %s\n", SDL_GetError());
        return 0;
    }
    SifExecModuleBuffer(padman_irx, size_padman_irx, 0, NULL, &ret);
    if (ret < 0) {
        SDL_SetError("Failed to load PADMAN");
        DBG("\tERROR: %s\n", SDL_GetError());
        return 0;
    }
    SifExecModuleBuffer(mtapman_irx, size_mtapman_irx, 0, NULL, &ret);
    if (ret < 0) {
        SDL_SetError("Failed to load MTAPMAN");
        DBG("\tERROR: %s\n", SDL_GetError());
        return 0;
    }
#endif

    /* init multitap library */
    mtapInit();
    /* init pad library */
    if (!padInit(0)) {
        DBG("\tPADINIT ERROR\n");
        return 0;
    }

    /* scan for joysticks */
    PS2_JoystickDetect();

    return PS2.numjoysticks;
}

static int
PS2_JoystickGetCount(void)
{
    return PS2.numjoysticks;
}

static void
PS2_JoystickDetect(void)
{
    DBG("!PS2_JoystickDetect()\n");

    /* scan all joysticks */
    PS2.numjoysticks = 0;

    /* for each PS2 port... */
    for (int port = 0; port < padGetPortMax(); port++) {
        /* open mtap */
        if (mtapPortOpen(port) == 1) {
            DBG("\tmtap on port %d\n", port);
            if (mtapGetConnection(port) != 1) {
                DBG("\t\t mtap exists on port but failed\n");
                mtapPortClose(port);
            }
            /* no mtap */
        }

        /* for each slot in port... */
        for (int slot = 0; slot < padGetSlotMax(port); slot++) {
            DBG("\t[port=%d slot=%d] ", port, slot);

            /* try open it */
            if (padPortOpen(port, slot, &padbufs[256 * PS2.numjoysticks]) != 0) {
                /* save info about joystick */
                PS2.joyports[PS2.numjoysticks] = port;
                PS2.joyslots[PS2.numjoysticks] = slot;
                DBG("found [id=%d]\n", PS2.numjoysticks);

                PS2.numjoysticks++;
            } else {
                DBG("failed\n");
            }
        }
    }
    DBG("\tTOTAL: %d joysticks\n", PS2.numjoysticks);
}

static const char *
PS2_JoystickGetDeviceName(int device_index)
{
    char *name = NULL;
    if (device_index < PS2.numjoysticks) {
        name = "PS2 Controller";
    } else {
        SDL_SetError("No joystick available with that index");
    }
    return name;
}

static const char *
PS2_JoystickGetDevicePath(int device_index)
{
    return NULL;
}

static int
PS2_JoystickGetDevicePlayerIndex(int device_index)
{
    return device_index;
}

static void
PS2_JoystickSetDevicePlayerIndex(int device_index,
                                 int player_index)
{
}

static SDL_JoystickGUID
PS2_JoystickGetDeviceGUID(int device_index)
{
    SDL_JoystickGUID guid;
    SDL_zero(guid);
    snprintf((char *) &guid, sizeof(guid), "PS2/%04x/%04x",
             PS2.joyports[device_index], PS2.joyslots[device_index]);
    return guid;
}

static SDL_JoystickID
PS2_JoystickGetDeviceInstanceID(int device_index)
{
    return device_index;
}

static int
PS2_JoystickOpen(SDL_Joystick *joystick, int device_index)
{
    DBG("!PS2_JoystickOpen()\n");

    /* alloc private data for joystick */
    if (!(joystick->hwdata = SDL_malloc(sizeof(struct joystick_hwdata)))) {
        SDL_SetError("Out of memory");
        return -1;
    }
    memset(joystick->hwdata, 0, sizeof(struct joystick_hwdata));

    /* set defaults */
    joystick->naxes = MAX_AXES;
    joystick->nballs = 0;
    joystick->nhats = 0;
    joystick->nbuttons = MAX_BUTTONS;

    joystick->hwdata->deviceid = -1;
    joystick->hwdata->rumble = 0;
    joystick->hwdata->port = PS2.joyports[device_index];
    joystick->hwdata->slot = PS2.joyslots[device_index];

    return 0;
}

static int
PS2_JoystickRumble(SDL_Joystick *joystick,
                   Uint16 low_frequency_rumble,
                   Uint16 high_frequency_rumble)
{
    /* FIXME: vibrate joystick */
    return SDL_Unsupported();
}

static int
PS2_JoystickRumbleTriggers(SDL_Joystick *joystick,
                           Uint16 left_rumble,
                           Uint16 right_rumble)
{
    return SDL_Unsupported();
}

static Uint32
PS2_JoystickGetCapabilities(SDL_Joystick *joystick)
{
    /* FIXME: we can vibrate */
    return 0;
}

static int
PS2_JoystickSetLED(SDL_Joystick *joystick, Uint8 red, Uint8 green,
                   Uint8 blue)
{
    return SDL_Unsupported();
}

static int
PS2_JoystickSendEffect(SDL_Joystick *joystick, const void *data,
                       int size)
{
    return SDL_Unsupported();
}

static int
PS2_JoystickSetSensorsEnabled(SDL_Joystick *joystick,
                              SDL_bool enabled)
{
    return SDL_Unsupported();
}

SDL_bool
pad_ready(SDL_Joystick *joystick)
{
    int state;
#ifdef DBG_CODE
    int last_state = -1;
    DBG("\t\tPADSTATE: ");
#endif
    do {
        state = padGetState(joystick->hwdata->port, joystick->hwdata->slot);
#ifdef DBG_CODE
        if (state != last_state) {
            char buf[16];
            padStateInt2String(state, buf);
            DBG("%s ", buf);
            last_state = state;
        }
#endif
        if (state == PAD_STATE_DISCONN) {
            DBG("\n");
            return SDL_FALSE;
        }
    }
    while ((state != PAD_STATE_STABLE) && (state != PAD_STATE_FINDCTP1));
    DBG("\n");
    return SDL_TRUE;
}

static void
PS2_JoystickUpdate(SDL_Joystick *joystick)
{
    DBG("!PS2_JoystickUpdate()\n");

    /* init joystick here (so we can retry if failed) */
    if (joystick->hwdata->deviceid == -1) {
        DBG("\tINIT:\n");
        if (!pad_ready(joystick)) {
            return;
        }
#ifdef DBG_CODE
        DBG("\t\tMODES: ");
        int modes = padInfoMode(joystick->hwdata->port, joystick->hwdata->slot, PAD_MODETABLE, -1);
        if (modes) {
            for (int i = 0; i < modes; i++) {
                int j = padInfoMode(joystick->hwdata->port, joystick->hwdata->slot, PAD_MODETABLE, i);
                DBG("%s ",
                    (j == PAD_TYPE_NEJICON) ? "PAD_TYPE_NEJICON" : (j == PAD_TYPE_KONAMIGUN) ? "KONAMIGUN"
                                                               : (j == PAD_TYPE_DIGITAL)     ? "DIGITAL"
                                                               : (j == PAD_TYPE_ANALOG)      ? "ANALOG"
                                                               : (j == PAD_TYPE_NAMCOGUN)    ? "NAMCOGUN"
                                                               : (j == PAD_TYPE_DUALSHOCK)   ? "DUALSHOCK"
                                                               : (j == PAD_TYPE_JOGCON)      ? "JOGCON"
                                                               : (j == PAD_TYPE_EX_TSURICON) ? "EX_TSURICON"
                                                               : (j == PAD_TYPE_EX_JOGCON)   ? "JOGCON"
                                                                                             : "UNKNOW");
            }
            DBG("\n");
        }
#endif

        /* get pad info */
        joystick->hwdata->deviceid = padInfoMode(joystick->hwdata->port, joystick->hwdata->slot, PAD_MODECURID, 0);
        /* set to DUALSHOCK */
        padSetMainMode(joystick->hwdata->port, joystick->hwdata->slot, PAD_MMODE_DUALSHOCK, PAD_MMODE_LOCK);
        pad_ready(joystick);
        DBG("\t\tRUMBLE: ");
        if (padInfoAct(joystick->hwdata->port, joystick->hwdata->slot, -1, 0) != 0) {
            char act_align[6];
            act_align[0] = 0; /* Enable small motor */
            act_align[1] = 1; /* Enable big motor */
            act_align[2] = 0xff;
            act_align[3] = 0xff;
            act_align[4] = 0xff;
            act_align[5] = 0xff;
            padSetActAlign(joystick->hwdata->port, joystick->hwdata->slot, act_align);
            joystick->hwdata->rumble = 1;
            DBG("OK\n");
        } else {
            DBG("NOK\n");
        }
    }

    /* read joystick */
    DBG("\tREAD:\n");

    /* no pad data? */
    if (!pad_ready(joystick)) {
        return;
    }

    struct padButtonStatus buttons;
    if (padRead(joystick->hwdata->port, joystick->hwdata->slot, &buttons) != 0) {

        /* invert */
        int pad = 0xffff ^ buttons.btns;
#ifdef UPDATE_ONLY_IF_CHANGED
        /* check buttons state for change*/
        if (pad != joystick->hwdata->prev_pad) {

            for (int b = 0; b < joystick->nbuttons; b++) {
                int button_state = pad & button_table[b];
                int old_button_state = joystick->hwdata->prev_pad & button_table[b];
                /* button changed state? */
                if (button_state != old_button_state) {
                    SDL_PrivateJoystickButton(joystick, b, button_state ? SDL_PRESSED : SDL_RELEASED);
                    DBG("\t\t%s\n", button_table_name[b]);
                }
            }

            /* update button state */
            joystick->hwdata->prev_pad = pad;
        }

        /* check axis for change */
        int axis, old_axis;

#define CHECK_AXIS(EIXO)                                             \
    /* convert from 0<->256 to -127<->128 and then -32767<->32768 */ \
    axis = (buttons.EIXO - 127) * 127;                               \
    old_axis = joystick->hwdata->prev_##EIXO;                        \
    /* check if changed */                                           \
    if (axis != old_axis) {                                          \
        SDL_PrivateJoystickAxis(joystick, 0, axis);                  \
        /* update */                                                 \
        joystick->hwdata->prev_##EIXO = axis;                        \
        DBG("\t\t%s(%d)\n", #EIXO, axis);                            \
    }

        CHECK_AXIS(ljoy_h)
        CHECK_AXIS(ljoy_v)
        CHECK_AXIS(rjoy_h)
        CHECK_AXIS(rjoy_v)

#else
#define MAP_PRESS(SDL_B, PAD_B)                \
    SDL_PrivateJoystickButton(joystick, SDL_B, \
                              (pad & PAD_B) ? SDL_PRESSED : SDL_RELEASED);

        /* update all buttons */
        MAP_PRESS(0, PAD_LEFT)
        MAP_PRESS(1, PAD_DOWN)
        MAP_PRESS(2, PAD_RIGHT)
        MAP_PRESS(3, PAD_UP)
        MAP_PRESS(4, PAD_START)
        MAP_PRESS(5, PAD_R3)
        MAP_PRESS(6, PAD_L3)
        MAP_PRESS(7, PAD_SELECT)
        MAP_PRESS(8, PAD_SQUARE)
        MAP_PRESS(9, PAD_CROSS)
        MAP_PRESS(10, PAD_CIRCLE)
        MAP_PRESS(11, PAD_TRIANGLE)
        MAP_PRESS(12, PAD_R1)
        MAP_PRESS(13, PAD_L1)
        MAP_PRESS(14, PAD_R2)
        MAP_PRESS(15, PAD_L2)

        /* convert from 0<->256 to -128<->128 and then -3278<->32767 */
        SDL_PrivateJoystickAxis(joystick, 0, (buttons.ljoy_h - 128) * 127);
        SDL_PrivateJoystickAxis(joystick, 1, (buttons.ljoy_v - 128) * 127);
        SDL_PrivateJoystickAxis(joystick, 2, (buttons.rjoy_h - 128) * 127);
        SDL_PrivateJoystickAxis(joystick, 3, (buttons.rjoy_v - 128) * 127);
#endif
    }
}

static void
PS2_JoystickClose(SDL_Joystick *joystick)
{
    DBG("!PS2_JoystickClose()\n");

    /* free private data for joystick */
    if (joystick->hwdata) {
        /* close port */
        padPortClose(joystick->hwdata->port, joystick->hwdata->slot);
        SDL_free(joystick->hwdata);
        joystick->hwdata = 0;
    }
}

static void
PS2_JoystickQuit(void)
{
    DBG("!PS2_JoystickQuit()\n");
    PS2.numjoysticks = 0;
}

static SDL_bool
PS2_JoystickGetGamepadMapping(int device_index,
                              SDL_GamepadMapping *out)
{
    return SDL_FALSE;
}

SDL_JoystickDriver SDL_PS2_JoystickDriver = {
    PS2_JoystickInit,
    PS2_JoystickGetCount,
    PS2_JoystickDetect,
    PS2_JoystickGetDeviceName,
    PS2_JoystickGetDevicePath,
    PS2_JoystickGetDevicePlayerIndex,
    PS2_JoystickSetDevicePlayerIndex,
    PS2_JoystickGetDeviceGUID,
    PS2_JoystickGetDeviceInstanceID,
    PS2_JoystickOpen,
    PS2_JoystickRumble,
    PS2_JoystickRumbleTriggers,
    PS2_JoystickGetCapabilities,
    PS2_JoystickSetLED,
    PS2_JoystickSendEffect,
    PS2_JoystickSetSensorsEnabled,
    PS2_JoystickUpdate,
    PS2_JoystickClose,
    PS2_JoystickQuit,
    PS2_JoystickGetGamepadMapping
};

#endif /* SDL_JOYSTICK_PS2 */

/* vi: set ts=4 sw=4 expandtab: */
