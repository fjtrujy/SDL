/*
    SDL_psp_main.c, placed in the public domain by Sam Lantinga  3/13/14
*/
#include "SDL_config.h"

#ifdef PS2

#include "SDL_main.h"
#include "SDL_error.h"

#include <loadfile.h>
#include <sifrpc.h>
#include <iopcontrol.h>
#include <sbv_patches.h>
#include <audsrv.h>
#include <libmtap.h>
#include <libpad.h>

extern unsigned char sio2man_irx[] __attribute__((aligned(16)));
extern unsigned int size_sio2man_irx;

extern unsigned char mcman_irx[] __attribute__((aligned(16)));
extern unsigned int size_mcman_irx;

extern unsigned char mcserv_irx[] __attribute__((aligned(16)));
extern unsigned int size_mcserv_irx;

extern unsigned char mtapman_irx[] __attribute__((aligned(16)));
extern unsigned int size_mtapman_irx;

extern unsigned char padman_irx[] __attribute__((aligned(16)));
extern unsigned int size_padman_irx;

extern unsigned char iomanX_irx[] __attribute__((aligned(16)));
extern unsigned int size_iomanX_irx;

extern unsigned char fileXio_irx[] __attribute__((aligned(16)));
extern unsigned int size_fileXio_irx;

extern unsigned char ps2dev9_irx[] __attribute__((aligned(16)));
extern unsigned int size_ps2dev9_irx;

extern unsigned char ps2atad_irx[] __attribute__((aligned(16)));
extern unsigned int size_ps2atad_irx;

extern unsigned char ps2hdd_irx[] __attribute__((aligned(16)));
extern unsigned int size_ps2hdd_irx;

extern unsigned char ps2fs_irx[] __attribute__((aligned(16)));
extern unsigned int size_ps2fs_irx;

extern unsigned char usbd_irx[] __attribute__((aligned(16)));
extern unsigned int size_usbd_irx;

extern unsigned char bdm_irx[] __attribute__((aligned(16)));
extern unsigned int size_bdm_irx;

extern unsigned char bdmfs_vfat_irx[] __attribute__((aligned(16)));
extern unsigned int size_bdmfs_vfat_irx;

extern unsigned char usbmass_bd_irx[] __attribute__((aligned(16)));
extern unsigned int size_usbmass_bd_irx;

extern unsigned char cdfs_irx[] __attribute__((aligned(16)));
extern unsigned int size_cdfs_irx;

extern unsigned char libsd_irx[] __attribute__((aligned(16)));
extern unsigned int size_libsd_irx;

extern unsigned char audsrv_irx[] __attribute__((aligned(16)));
extern unsigned int size_audsrv_irx;

extern unsigned char ps2dev9_irx[] __attribute__((aligned(16)));
extern unsigned int size_ps2dev9_irx;

extern unsigned char ps2atad_irx[] __attribute__((aligned(16)));
extern unsigned int size_ps2atad_irx;

extern unsigned char ps2hdd_irx[] __attribute__((aligned(16)));
extern unsigned int size_ps2hdd_irx;

extern unsigned char ps2fs_irx[] __attribute__((aligned(16)));
extern unsigned int size_ps2fs_irx;

extern unsigned char poweroff_irx[] __attribute__((aligned(16)));
extern unsigned int size_poweroff_irx;

#ifdef main
    #undef main
#endif

static void reset_IOP()
{
   SifInitRpc(0);
#if !defined(DEBUG) || defined(BUILD_FOR_PCSX2)
   /* Comment this line if you don't wanna debug the output */
   while(!SifIopReset(NULL, 0)){};
#endif

   while(!SifIopSync()){};
   SifInitRpc(0);
   sbv_patch_enable_lmb();
   sbv_patch_disable_prefix_check();
}

static void load_modules(void)
{
    /* I/O Files */
    SifExecModuleBuffer(&iomanX_irx, size_iomanX_irx, 0, NULL, NULL);
    SifExecModuleBuffer(&fileXio_irx, size_fileXio_irx, 0, NULL, NULL);
    SifExecModuleBuffer(&sio2man_irx, size_sio2man_irx, 0, NULL, NULL);

    /* Memory Card */
    SifExecModuleBuffer(&mcman_irx, size_mcman_irx, 0, NULL, NULL);
    SifExecModuleBuffer(&mcserv_irx, size_mcserv_irx, 0, NULL, NULL);

    /* USB */
    SifExecModuleBuffer(&usbd_irx, size_usbd_irx, 0, NULL, NULL);
    SifExecModuleBuffer(&bdm_irx, size_bdm_irx, 0, NULL, NULL);
    SifExecModuleBuffer(&bdmfs_vfat_irx, size_bdmfs_vfat_irx, 0, NULL, NULL);
    SifExecModuleBuffer(&usbmass_bd_irx, size_usbmass_bd_irx, 0, NULL, NULL);

    /* Controllers */
    SifExecModuleBuffer(&mtapman_irx, size_mtapman_irx, 0, NULL, NULL);
    SifExecModuleBuffer(&padman_irx, size_padman_irx, 0, NULL, NULL);

    /* Audio */
    SifExecModuleBuffer(&libsd_irx, size_libsd_irx, 0, NULL, NULL);
    SifExecModuleBuffer(&audsrv_irx, size_audsrv_irx, 0, NULL, NULL);
}

static void start_modules() {
    if (audsrv_init()) {
        SDL_SetError("audsrv library not initalizated");
    }

    /* Initializes pad un multitap libraries */
    if (mtapInit() != 1) {
        SDL_SetError("mtapInit library not initalizated");
    }
    if (padInit(0) != 1) {
        SDL_SetError("padInit library not initalizated\n");
    }
}

int main(int argc, char *argv[])
{
    reset_IOP();
    load_modules();
    start_modules();

    SDL_SetMainReady();

    return SDL_main(argc, argv);;
}

#endif /* _EE */

/* vi: set ts=4 sw=4 expandtab: */
