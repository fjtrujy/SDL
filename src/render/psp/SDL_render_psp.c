/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

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

#if SDL_VIDEO_RENDER_PSP

#include "../SDL_sysrender.h"
#include "SDL_hints.h"
#include "SDL_render_psp.h"

#include <sys/param.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspthreadman.h>
#include <pspintrman.h>
#include <psputils.h>
#include <vram.h>

#define GPU_LIST_SIZE 32 * 1024
#define MAX_VERTICES 65535

typedef struct
{
    uint8_t shade; /**< GU_FLAT or GU_SMOOTH */
    uint8_t mode;  /**< GU_ADD, GU_SUBTRACT, GU_REVERSE_SUBTRACT, GU_MIN, GU_MAX */
} PSP_BlendInfo;

typedef struct
{
    uint32_t __attribute__((aligned(16))) guList[GPU_LIST_SIZE];
    void *frontbuffer;               /**< main screen buffer */
    void *backbuffer;                /**< buffer presented to display */
    PSP_BlendInfo blendInfo;         /**< current blend info */
    uint8_t drawBufferFormat;        /**< GU_PSM_8888 or GU_PSM_5650 or GU_PSM_4444 */
    uint8_t vsync;                   /* 0 (Disabled), 1 (Enabled), 2 (Dynamic) */
    SDL_bool vblank_not_reached; /**< whether vblank wasn't reached */
} PSP_RenderData;

typedef struct
{
    void *data;              /**< Image data. */
    void *swizzledData;      /**< Swizzled image data. */
    uint32_t textureWidth;   /**< Texture width (power of two). */
    uint32_t textureHeight;  /**< Texture height (power of two). */
    uint32_t width;          /**< Image width. */
    uint32_t height;         /**< Image height. */
    uint32_t pitch;          /**< Image pitch. */
    uint32_t swizzledWidth;  /**< Swizzled image width. */
    uint32_t swizzledHeight; /**< Swizzled image height. */
    uint32_t swizzledPitch;  /**< Swizzled image pitch. */
    uint32_t size;           /**< Image size in bytes. */
    uint32_t swizzledSize;   /**< Swizzled image size in bytes. */
    uint8_t format;          /**< Image format - one of ::pgePixelFormat. */
    uint8_t filter;          /**< Image filter - one of GU_NEAREST or GU_LINEAR. */
    uint8_t swizzled;        /**< Whether the image is swizzled or not. */
} PSP_Texture;

typedef struct
{
    float x, y, z;
} __attribute__((packed)) VertV;

typedef struct
{
    uint32_t col;
    float x, y, z;
} __attribute__((packed)) VertCV;

typedef struct
{
    float u, v;
    uint32_t col;
    float x, y, z;
} __attribute__((packed)) VertTCV;

typedef struct
{
    float u, v;
    float x, y, z;
} __attribute__((packed)) VertTV;

typedef struct
{
    int width;
    int height;
} SliceSize;

int SDL_PSP_RenderGetProp(SDL_Renderer *r, enum SDL_PSP_RenderProps which, void **out)
{
    PSP_RenderData *rd;
    if (r == NULL) {
        return -1;
    }
    rd = r->driverdata;
    switch (which) {
    case SDL_PSP_RENDERPROPS_FRONTBUFFER:
        *out = rd->frontbuffer;
        return 0;
    case SDL_PSP_RENDERPROPS_BACKBUFFER:
        *out = rd->backbuffer;
        return 0;
    }
    return -1;
}

/* PRIVATE METHODS */
static void psp_on_vblank(u32 sub, PSP_RenderData *data)
{
    if (data) {
        data->vblank_not_reached = SDL_FALSE;
    }
}

static inline unsigned int getMemorySize(unsigned int width, unsigned int height, unsigned int psm)
{
    switch (psm) {
    case GU_PSM_T4:
        return (width * height) >> 1;

    case GU_PSM_T8:
        return width * height;

    case GU_PSM_5650:
    case GU_PSM_5551:
    case GU_PSM_4444:
    case GU_PSM_T16:
        return 2 * width * height;

    case GU_PSM_8888:
    case GU_PSM_T32:
        return 4 * width * height;

    default:
        return 0;
    }
}

static inline int pixelFormatToPSPFMT(Uint32 format)
{
    switch (format) {
    case SDL_PIXELFORMAT_BGR565:
        return GU_PSM_5650;
    case SDL_PIXELFORMAT_ABGR1555:
        return GU_PSM_5551;
    case SDL_PIXELFORMAT_ABGR4444:
        return GU_PSM_4444;
    case SDL_PIXELFORMAT_ABGR8888:
        return GU_PSM_8888;
    default:
        return GU_PSM_8888;
    }
}

static inline int calculatePitchForTextureFormat(int width, int format)
{
    switch (format) {
    case GU_PSM_5650:
    case GU_PSM_5551:
    case GU_PSM_4444:
        return (width + 7) & ~7;
    case GU_PSM_8888:
        return (width + 3) & ~3;
    default:
        return width;
    }
}

static inline int calculatePitchForTextureFormatAndAccess(int width, int format, int access)
{
    if (access == SDL_TEXTUREACCESS_TARGET) {
        // Round up to 64 bytes because it is required to be used by sceGuDrawBufferList
        return (width + 63) & ~63;
    } else {
        return calculatePitchForTextureFormat(width, format);
    }
}

static inline int calculateHeightForSwizzledTexture(int height, int format)
{
    switch (format) {
    case GU_PSM_5650:
    case GU_PSM_5551:
    case GU_PSM_4444:
        return (height + 15) & ~15;
    case GU_PSM_8888:
        return (height + 7) & ~7;
    default:
        return height;
    }
}

/* Return next power of 2 */
static inline int calculateNextPow2(int value)
{
    int i = 1;
    while (i < value) {
        i <<= 1;
    }
    return i;
}

static inline uint8_t currentDrawBufferFormat(SDL_Renderer *renderer)
{
    PSP_RenderData *data = (PSP_RenderData *)renderer->driverdata;
    if (renderer->target) {
        SDL_Texture *texture = (SDL_Texture *)renderer->target;
        PSP_Texture *psp_tex = (PSP_Texture *)texture->driverdata;
        return psp_tex->format;
    } else {
        return data->drawBufferFormat;
    }
}

static inline int calculateBestSliceSizeForSprite(SDL_Renderer *renderer, const SDL_FRect *dstrect, SliceSize *sliceSize, SliceSize *sliceDimension)
{
    PSP_RenderData *data = (PSP_RenderData *)renderer->driverdata;

    // We split in blocks of (64 x destiny height) when 16 bits per color
    // or (32 x destiny height) when 32 bits per color

    switch (currentDrawBufferFormat(renderer)) {
    case GU_PSM_5650:
    case GU_PSM_5551:
    case GU_PSM_4444:
        sliceSize->width = dstrect->w > 64 ? 64 : dstrect->w;
        break;
    case GU_PSM_8888:
        sliceSize->width = dstrect->w > 32 ? 32 : dstrect->w;
        break;
    default:
        return -1;
    }
    sliceSize->height = dstrect->h;

    sliceDimension->width = SDL_ceilf(dstrect->w / sliceSize->width);
    sliceDimension->height = SDL_ceilf(dstrect->h / sliceSize->height);

    return 0;
}

static inline void fillSingleSliceVertices(VertTV *vertices, const SDL_Rect *srcrect, const SDL_FRect *dstrect)
{
    vertices[0].x = dstrect->x;
    vertices[0].y = dstrect->y;
    vertices[0].z = 0.0f;
    vertices[0].u = srcrect->x;
    vertices[0].v = srcrect->y;

    vertices[1].x = dstrect->x + dstrect->w;
    vertices[1].y = dstrect->y + dstrect->h;
    vertices[1].z = 0.0f;
    vertices[1].u = srcrect->x + srcrect->w;
    vertices[1].v = srcrect->y + srcrect->h;
}

static inline void fillSpriteVertices(VertTV *vertices, SliceSize *dimensions, SliceSize *sliceSize,
                                      const SDL_Rect *srcrect, const SDL_FRect *dstrect)
{

    int i, j;
    int remainingWidth, remainingHeight;
    int hasRemainingWidth, hasRemainingHeight;
    float srcrectRateWidth, srcrectRateHeight;
    float srcWidth, srcHeight;
    float remainingSrcWidth, remainingSrcHeight;

    // For avoiding division by zero
    if (dimensions->width == 1 && dimensions->height == 1) {
        fillSingleSliceVertices(vertices, srcrect, dstrect);
        return;
    }

    remainingWidth = (int)dstrect->w % sliceSize->width;
    remainingHeight = (int)dstrect->h % sliceSize->height;
    hasRemainingWidth = remainingWidth > 0;
    hasRemainingHeight = remainingHeight > 0;
    srcrectRateWidth = (float)(abs(srcrect->w - dimensions->width)) / (float)(abs(dstrect->w - dimensions->width));
    srcrectRateHeight = (float)(abs(srcrect->h - dimensions->height)) / (float)(abs(dstrect->h - dimensions->height));
    srcWidth = sliceSize->width * srcrectRateWidth;
    srcHeight = sliceSize->height * srcrectRateHeight;
    remainingSrcWidth = remainingWidth * srcrectRateWidth;
    remainingSrcHeight = remainingHeight * srcrectRateHeight;

    for (i = 0; i < dimensions->width; i++) {
        for (j = 0; j < dimensions->height; j++) {
            uint8_t currentIndex = (i * dimensions->height + j) * 2;
            vertices[currentIndex].u = srcrect->x + i * srcWidth;
            vertices[currentIndex].v = srcrect->y + j * srcHeight;
            vertices[currentIndex].x = dstrect->x + i * sliceSize->width;
            vertices[currentIndex].y = dstrect->y + j * sliceSize->height;
            vertices[currentIndex].z = 0;

            if (i == dimensions->width - 1 && hasRemainingWidth) {
                vertices[currentIndex + 1].u = vertices[currentIndex].u + remainingSrcWidth;
                vertices[currentIndex + 1].x = vertices[currentIndex].x + remainingWidth;
            } else {
                vertices[currentIndex + 1].u = vertices[currentIndex].u + srcWidth;
                vertices[currentIndex + 1].x = vertices[currentIndex].x + sliceSize->width;
            }

            if (j == dimensions->height - 1 && hasRemainingHeight) {
                vertices[currentIndex + 1].v = vertices[currentIndex].v + remainingSrcHeight;
                vertices[currentIndex + 1].y = vertices[currentIndex].y + remainingHeight;
            } else {
                vertices[currentIndex + 1].v = vertices[currentIndex].v + srcHeight;
                vertices[currentIndex + 1].y = vertices[currentIndex].y + sliceSize->height;
            }

            vertices[currentIndex + 1].z = 0;
        }
    }
}

// The slize when swizzling is always 16x32 bytes, no matter the texture format
// so the algorithm is the same for all formats
static inline int swizzle(PSP_Texture *psp_tex)
{
    uint32_t i, j, verticalSlice, dstOffset;
    uint32_t *src, *dst, *srcBlock, *dstBlock;
    uint32_t srcWidth = psp_tex->pitch >> 2;
    uint32_t dstWidth = psp_tex->swizzledPitch >> 2;
    uint32_t srcWidthBlocks = srcWidth >> 2;
    uint8_t blockSizeBytes = 16; // 4 pixels of 32 bits

    dst = (uint32_t *)psp_tex->swizzledData;
    for (j = 0; j < psp_tex->height; j++) {
        src = (uint32_t *)psp_tex->data + j * srcWidth;
        verticalSlice = (((j >> 3) << 3) * dstWidth) + ((j % 8) << 2);
        for (i = 0; i < srcWidthBlocks; i++) {
            srcBlock = src + (i << 2);
            dstOffset = verticalSlice + (i << 5);
            dstBlock = dst + dstOffset;
            memcpy(dstBlock, srcBlock, blockSizeBytes);
        }
    }

    return 1;
}

static inline int unswizzle(PSP_Texture *psp_tex)
{
    uint32_t i, j, verticalSlice, srcOffset;
    uint32_t *src, *dst, *srcBlock, *dstBlock;
    uint32_t srcWidth = psp_tex->swizzledPitch >> 2;
    uint32_t dstWidth = psp_tex->pitch >> 2;
    uint32_t dstWidthBlocks = dstWidth >> 2;
    uint8_t blockSizeBytes = 16; // 4 pixels of 32 bits

    src = (uint32_t *)psp_tex->swizzledData;
    for (j = 0; j < psp_tex->height; j++) {
        dst = (uint32_t *)psp_tex->data + j * dstWidth;
        verticalSlice = (((j >> 3) << 3) * srcWidth) + ((j % 8) << 2);
        for (i = 0; i < dstWidthBlocks; i++) {
            dstBlock = dst + (i << 2);
            srcOffset = verticalSlice + (i << 5);
            srcBlock = src + srcOffset;
            memcpy(dstBlock, srcBlock, blockSizeBytes);
        }
    }

    return 1;
}

static inline void prepareTextureForUpload(SDL_Texture *texture)
{
    PSP_Texture *psp_tex = (PSP_Texture *)texture->driverdata;
    sceKernelDcacheWritebackRange(psp_tex->data, psp_tex->size);
    if (texture->access != SDL_TEXTUREACCESS_STATIC || psp_tex->swizzled)
        return;

    psp_tex->swizzledData = vramalloc(psp_tex->swizzledSize);
    if (!psp_tex->swizzledData) {
        return;
    }

    swizzle(psp_tex);
    SDL_free(psp_tex->data);
    psp_tex->swizzled = GU_TRUE;

    sceKernelDcacheWritebackRange(psp_tex->swizzledData, psp_tex->swizzledSize);
}

static inline void prepareTextureForDownload(SDL_Texture *texture)
{
    PSP_Texture *psp_tex = (PSP_Texture *)texture->driverdata;

    if (texture->access != SDL_TEXTUREACCESS_STATIC || !psp_tex->swizzled)
        return;

    psp_tex->data = SDL_malloc(psp_tex->size);
    if (!psp_tex->data) {
        sceKernelDcacheInvalidateRange(psp_tex->swizzledData, psp_tex->swizzledSize);
        return;
    }

    unswizzle(psp_tex);
    vfree(psp_tex->swizzledData);
    psp_tex->swizzled = GU_FALSE;

    sceKernelDcacheInvalidateRange(psp_tex->data, psp_tex->size);
}

static inline void finishAndSyncGPUList(PSP_RenderData *data)
{
    int g_packet_size = sceGuFinish();
    SDL_assert(g_packet_size < GPU_LIST_SIZE);
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
}

#define GU_INVALID_ENUM -1
static int32_t GetBlendFunc(SDL_BlendFactor factor)
{
    switch (factor) {
    case SDL_BLENDFACTOR_ZERO:
        return GU_FIX;
    case SDL_BLENDFACTOR_ONE:
        return GU_FIX; /* GU_ONE not available, use GU_FIX with appropriate fix value */
    case SDL_BLENDFACTOR_SRC_COLOR:
        return GU_SRC_COLOR;
    case SDL_BLENDFACTOR_ONE_MINUS_SRC_COLOR:
        return GU_ONE_MINUS_SRC_COLOR;
    case SDL_BLENDFACTOR_SRC_ALPHA:
        return GU_SRC_ALPHA;
    case SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA:
        return GU_ONE_MINUS_SRC_ALPHA;
    case SDL_BLENDFACTOR_DST_COLOR:
        return GU_DST_COLOR;
    case SDL_BLENDFACTOR_ONE_MINUS_DST_COLOR:
        return GU_ONE_MINUS_DST_COLOR;
    case SDL_BLENDFACTOR_DST_ALPHA:
        return GU_DST_ALPHA;
    case SDL_BLENDFACTOR_ONE_MINUS_DST_ALPHA:
        return GU_ONE_MINUS_DST_ALPHA;
    default:
        return GU_INVALID_ENUM;
    }
}

static int32_t GetBlendEquation(SDL_BlendOperation operation)
{
    switch (operation) {
    case SDL_BLENDOPERATION_ADD:
        return GU_ADD;
    case SDL_BLENDOPERATION_SUBTRACT:
        return GU_SUBTRACT;
    case SDL_BLENDOPERATION_REV_SUBTRACT:
        return GU_REVERSE_SUBTRACT;
    case SDL_BLENDOPERATION_MINIMUM:
        return GU_MIN;
    case SDL_BLENDOPERATION_MAXIMUM:
        return GU_MAX;
    default:
        return GU_INVALID_ENUM;
    }
}

static inline int neededPassesForBlendMode(SDL_BlendMode blendMode)
{
    switch (blendMode) {
    case SDL_BLENDMODE_NONE:
        return 1;
    case SDL_BLENDMODE_BLEND:
        return 2;
    case SDL_BLENDMODE_ADD:
        return 1;
    case SDL_BLENDMODE_MOD:
        return 1;
    case SDL_BLENDMODE_MUL:
        return 2;
    default:
        SDL_assert("neededPassesForBlendMode: Invalid blend mode");
        return 0;
    }
}

static inline unsigned int mask_alpha(int fpf) {
    switch (fpf) {
        case GU_PSM_8888: return 0xFF000000;  // A8
        case GU_PSM_4444: return 0xF0000000;  // A4
        case GU_PSM_5551: return 0x80000000;  // A1
        case GU_PSM_5650: return 0x00000000;  // no alpha in framebuffer
        default: return 0xFF000000;
    }
}
  
static inline unsigned int mask_rgb(int fpf) {
    switch (fpf) {
        case GU_PSM_8888: return 0x00FFFFFF;  // BGR8
        case GU_PSM_4444: return 0x00F0F0F0;  // BGR4
        case GU_PSM_5551: return 0x00F8F8F8;  // BGR5
        case GU_PSM_5650: return 0x00F8FCF8;  // all 16 bits are BGR
        default: return 0x00FFFFFF;
    }
}

static inline void setShadeModel(PSP_RenderData *data, uint8_t shade)
{
    if (data->blendInfo.shade != shade) {
        sceGuShadeModel(shade);
        data->blendInfo.shade = shade;
    }
}

static inline void setBlendMode(SDL_Renderer *renderer, uint8_t blendMode, int passes)
{
    PSP_RenderData *data = (PSP_RenderData *)renderer->driverdata;
    switch (blendMode) {
    case SDL_BLENDMODE_NONE:
        if (data->blendInfo.mode != blendMode) {
            sceGuDisable(GU_BLEND);
            sceGuPixelMask(0);
            data->blendInfo.mode = blendMode;
        }
        break;
    case SDL_BLENDMODE_BLEND:
        switch (passes) {
            case 0: // RGB pass
                sceGuEnable(GU_BLEND);
                sceGuPixelMask(mask_alpha(currentDrawBufferFormat(renderer)));
                sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
                break;
            case 1: // Alpha pass
                sceGuPixelMask(mask_rgb(currentDrawBufferFormat(renderer)));
                sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
                sceGuBlendFunc(GU_ADD, GU_FIX, GU_ONE_MINUS_SRC_ALPHA, 0xFFFFFFFF, 0);
                break;
            default:
                SDL_assert("SDL_BLENDMODE_BLEND: Invalid number of passes");
        }
        break;
    case SDL_BLENDMODE_ADD:
        sceGuEnable(GU_BLEND);
        sceGuPixelMask(mask_alpha(currentDrawBufferFormat(renderer)));
        sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_FIX, 0, 0x00FFFFFF);
        break;
    case SDL_BLENDMODE_MOD:
        sceGuEnable(GU_BLEND);
        sceGuPixelMask(mask_alpha(currentDrawBufferFormat(renderer)));
        sceGuBlendFunc(GU_ADD, GU_FIX, GU_SRC_COLOR, 0, 0);
        break;
    case SDL_BLENDMODE_MUL:
        switch (passes) {
            case 0: // Cs*Cd
                sceGuEnable(GU_BLEND);
                sceGuPixelMask(mask_alpha(currentDrawBufferFormat(renderer)));
                sceGuBlendFunc(GU_ADD, GU_DST_COLOR, GU_FIX, 0, 0x00000000);
                break;
            case 1: // + Cd*(1 - srcA)
                sceGuTexFunc(data->blendInfo.shade == GU_SMOOTH ? GU_TFX_MODULATE : GU_TFX_REPLACE, GU_TCC_RGBA);
                sceGuBlendFunc(GU_ADD, GU_FIX, GU_ONE_MINUS_SRC_ALPHA, 0x00000000, 0);
                break;
            default:
                SDL_assert("SDL_BLENDMODE_MUL: Invalid number of passes");
        }
        break;
    }
}

static inline void postBlendMode(PSP_RenderData *data, uint8_t blendMode)
{
    data->blendInfo.mode = blendMode;
    sceGuPixelMask(0);
}

static void PSP_WindowEvent(SDL_Renderer *renderer, const SDL_WindowEvent *event)
{
}

static SDL_bool PSP_SupportsBlendMode(SDL_Renderer *renderer, SDL_BlendMode blendMode)
{
    SDL_BlendFactor srcColorFactor = SDL_GetBlendModeSrcColorFactor(blendMode);
    SDL_BlendFactor srcAlphaFactor = SDL_GetBlendModeSrcAlphaFactor(blendMode);
    SDL_BlendOperation colorOperation = SDL_GetBlendModeColorOperation(blendMode);
    SDL_BlendFactor dstColorFactor = SDL_GetBlendModeDstColorFactor(blendMode);
    SDL_BlendFactor dstAlphaFactor = SDL_GetBlendModeDstAlphaFactor(blendMode);
    SDL_BlendOperation alphaOperation = SDL_GetBlendModeAlphaOperation(blendMode);

    if (GetBlendFunc(srcColorFactor) == GU_INVALID_ENUM ||
        GetBlendFunc(srcAlphaFactor) == GU_INVALID_ENUM ||
        GetBlendEquation(colorOperation) == GU_INVALID_ENUM ||
        GetBlendFunc(dstColorFactor) == GU_INVALID_ENUM ||
        GetBlendFunc(dstAlphaFactor) == GU_INVALID_ENUM ||
        GetBlendEquation(alphaOperation) == GU_INVALID_ENUM) {
        return SDL_FALSE;
    }

    return SDL_TRUE;
}

static int PSP_CreateTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    PSP_Texture *psp_tex = (PSP_Texture *)SDL_calloc(1, sizeof(PSP_Texture));

    if (!psp_tex) {
        return SDL_OutOfMemory();
    }

    psp_tex->format = pixelFormatToPSPFMT(texture->format);
    psp_tex->textureWidth = calculateNextPow2(texture->w);
    psp_tex->textureHeight = calculateNextPow2(texture->h);
    psp_tex->width = calculatePitchForTextureFormatAndAccess(texture->w, psp_tex->format, texture->access);
    psp_tex->height = texture->h;
    psp_tex->pitch = psp_tex->width * SDL_BYTESPERPIXEL(texture->format);
    psp_tex->swizzledWidth = psp_tex->textureWidth;
    psp_tex->swizzledHeight = calculateHeightForSwizzledTexture(texture->h, psp_tex->format);
    psp_tex->swizzledPitch = psp_tex->swizzledWidth * SDL_BYTESPERPIXEL(texture->format);
    psp_tex->size = getMemorySize(psp_tex->width, psp_tex->height, psp_tex->format);
    psp_tex->swizzledSize = getMemorySize(psp_tex->swizzledWidth, psp_tex->swizzledHeight, psp_tex->format);

    if (texture->access != SDL_TEXTUREACCESS_STATIC) {
        psp_tex->data = vramalloc(psp_tex->size);
        if (!psp_tex->data) {
            SDL_free(psp_tex);
            return SDL_OutOfMemory();
        }
    } else {
        psp_tex->data = SDL_calloc(1, psp_tex->size);
        if (!psp_tex->data) {
            SDL_free(psp_tex);
            return SDL_OutOfMemory();
        }
    }
    psp_tex->swizzled = GU_FALSE;

    texture->driverdata = psp_tex;

    return 0;
}

static int PSP_LockTexture(SDL_Renderer *renderer, SDL_Texture *texture,
                           const SDL_Rect *rect, void **pixels, int *pitch)
{
    PSP_Texture *psp_tex = (PSP_Texture *)texture->driverdata;

    // How a pointer to the texture data is returned it need to be unswizzled before it can be used
    prepareTextureForDownload(texture);

    *pixels =
        (void *)((Uint8 *)psp_tex->data + rect->y * psp_tex->pitch +
                 rect->x * SDL_BYTESPERPIXEL(texture->format));
    *pitch = psp_tex->pitch;

    return 0;
}

static void PSP_UnlockTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    PSP_Texture *psp_tex = (PSP_Texture *)texture->driverdata;

    sceKernelDcacheWritebackRange(psp_tex->data, psp_tex->size);
}

static int PSP_UpdateTexture(SDL_Renderer *renderer, SDL_Texture *texture,
                             const SDL_Rect *rect, const void *pixels, int pitch)
{
    const Uint8 *src;
    Uint8 *dst;
    int row, length, dpitch;
    src = pixels;

    PSP_LockTexture(renderer, texture, rect, (void **)&dst, &dpitch);
    length = rect->w * SDL_BYTESPERPIXEL(texture->format);
    if (length == pitch && length == dpitch) {
        SDL_memcpy(dst, src, length * rect->h);
    } else {
        for (row = 0; row < rect->h; ++row) {
            SDL_memcpy(dst, src, length);
            src += pitch;
            dst += dpitch;
        }
    }

    PSP_UnlockTexture(renderer, texture);

    return 0;
}

static void PSP_SetTextureScaleMode(SDL_Renderer *renderer, SDL_Texture *texture, SDL_ScaleMode scaleMode)
{
    PSP_Texture *psp_tex = (PSP_Texture *)texture->driverdata;
    /*
     set texture filtering according to scaleMode
     suported hint values are nearest (0, default) or linear (1)
     GU scale mode is either GU_NEAREST (good for tile-map)
     or GU_LINEAR (good for scaling)
     */
    uint32_t guScaleMode = (scaleMode == SDL_ScaleModeNearest
                                ? GU_NEAREST
                                : GU_LINEAR);
    psp_tex->filter = guScaleMode;
}

static int PSP_SetRenderTarget(SDL_Renderer *renderer, SDL_Texture *texture)
{
    PSP_RenderData *data = (PSP_RenderData *)renderer->driverdata;

    sceKernelDcacheWritebackRange(data->guList, sizeof(data->guList));
    sceGuStart(GU_DIRECT, data->guList);
    if (texture) {
        PSP_Texture *psp_tex = (PSP_Texture *)texture->driverdata;
        sceGuDrawBuffer(psp_tex->format, vrelptr(psp_tex->data), psp_tex->width);
    } else {
        sceGuDrawBuffer(data->drawBufferFormat, vrelptr(data->frontbuffer), PSP_FRAME_BUFFER_WIDTH);
    }
    finishAndSyncGPUList(data);

    return 0;
}

static int PSP_QueueSetViewport(SDL_Renderer *renderer, SDL_RenderCommand *cmd)
{
    return 0; /* nothing to do in this backend. */
}

static int PSP_QueueDrawPoints(SDL_Renderer *renderer, SDL_RenderCommand *cmd, const SDL_FPoint *points, int count)
{
    VertV *verts = (VertV *)SDL_AllocateRenderVertices(renderer, count * sizeof(VertV), 4, &cmd->data.draw.first);
    int i;

    if (!verts) {
        return -1;
    }

    cmd->data.draw.count = count;

    for (i = 0; i < count; i++, verts++, points++) {
        verts->x = points->x;
        verts->y = points->y;
        verts->z = 0.0f;
    }

    return 0;
}

static int PSP_QueueGeometry(SDL_Renderer *renderer, SDL_RenderCommand *cmd, SDL_Texture *texture,
                             const float *xy, int xy_stride, const SDL_Color *color, int color_stride, const float *uv, int uv_stride,
                             int num_vertices, const void *indices, int num_indices, int size_indices,
                             float scale_x, float scale_y)
{
    int i;
    int count = indices ? num_indices : num_vertices;

    cmd->data.draw.count = count;
    size_indices = indices ? size_indices : 0;

    if (texture) {
        VertTCV *vertices = (VertTCV *)SDL_AllocateRenderVertices(renderer, count * sizeof(VertTCV), 4, &cmd->data.draw.first);

        if (!vertices) {
            return -1;
        }

        for (i = 0; i < count; i++) {
            int j;
            float *xy_;
            float *uv_;
            SDL_Color col_;
            if (size_indices == 4) {
                j = ((const Uint32 *)indices)[i];
            } else if (size_indices == 2) {
                j = ((const Uint16 *)indices)[i];
            } else if (size_indices == 1) {
                j = ((const Uint8 *)indices)[i];
            } else {
                j = i;
            }

            xy_ = (float *)((char *)xy + j * xy_stride);
            col_ = *(SDL_Color *)((char *)color + j * color_stride);
            uv_ = (float *)((char *)uv + j * uv_stride);

            vertices->x = xy_[0] * scale_x;
            vertices->y = xy_[1] * scale_y;
            vertices->z = 0;

            vertices->col = GU_ABGR(col_.a, col_.b, col_.g, col_.r);

            vertices->u = uv_[0] * texture->w;
            vertices->v = uv_[1] * texture->h;

            vertices++;
        }

    } else {
        VertCV *vertices = (VertCV *)SDL_AllocateRenderVertices(renderer, count * sizeof(VertCV), 4, &cmd->data.draw.first);

        if (!vertices) {
            return -1;
        }

        for (i = 0; i < count; i++) {
            int j;
            float *xy_;
            SDL_Color col_;
            if (size_indices == 4) {
                j = ((const Uint32 *)indices)[i];
            } else if (size_indices == 2) {
                j = ((const Uint16 *)indices)[i];
            } else if (size_indices == 1) {
                j = ((const Uint8 *)indices)[i];
            } else {
                j = i;
            }

            xy_ = (float *)((char *)xy + j * xy_stride);
            col_ = *(SDL_Color *)((char *)color + j * color_stride);

            vertices->x = xy_[0] * scale_x;
            vertices->y = xy_[1] * scale_y;
            vertices->z = 0;
            vertices->col = GU_ABGR(col_.a, col_.b, col_.g, col_.r);

            vertices++;
        }
    }

    return 0;
}

static int PSP_QueueFillRects(SDL_Renderer *renderer, SDL_RenderCommand *cmd, const SDL_FRect *rects, int count)
{
    VertV *verts = (VertV *)SDL_AllocateRenderVertices(renderer, count * 2 * sizeof(VertV), 4, &cmd->data.draw.first);
    int i;

    if (verts == NULL) {
        return -1;
    }

    for (i = 0; i < count; i++, rects++) {
        verts->x = rects->x;
        verts->y = rects->y;
        verts->z = 0.0f;
        verts++;

        verts->x = rects->x + rects->w;
        verts->y = rects->y + rects->h;
        verts->z = 0.0f;
        verts++;
    }

    // Update the vertex count
    cmd->data.draw.count = count * 2;

    return 0;
}

static int PSP_QueueCopy(SDL_Renderer *renderer, SDL_RenderCommand *cmd, SDL_Texture *texture,
                         const SDL_Rect *srcrect, const SDL_FRect *dstrect)
{
    VertTV *verts;
    uint8_t verticesCount;

    SliceSize sliceSize, sliceDimension;

    // In this function texture must be created
    if (!texture)
        return -1;

    if (calculateBestSliceSizeForSprite(renderer, dstrect, &sliceSize, &sliceDimension))
        return -1;

    verticesCount = sliceDimension.width * sliceDimension.height * 2;
    verts = (VertTV *)SDL_AllocateRenderVertices(renderer, verticesCount * sizeof(VertTV), 4, &cmd->data.draw.first);
    if (verts == NULL)
        return -1;

    fillSpriteVertices(verts, &sliceDimension, &sliceSize, srcrect, dstrect);

    cmd->data.draw.count = verticesCount;

    return 0;
}

static inline int PSP_RenderSetViewPort(SDL_Renderer *renderer, SDL_RenderCommand *cmd)
{
    const SDL_Rect *viewport = &cmd->data.viewport.rect;

    sceGuOffset(2048 - (viewport->w >> 1), 2048 - (viewport->h >> 1));
    sceGuViewport(2048, 2048, viewport->w, viewport->h);
    sceGuScissor(viewport->x, viewport->y, viewport->w, viewport->h);

    return 0;
}

static inline int PSP_RenderSetClipRect(SDL_Renderer *renderer, SDL_RenderCommand *cmd)
{
    const SDL_Rect *rect = &cmd->data.cliprect.rect;

    if (cmd->data.cliprect.enabled) {
        sceGuEnable(GU_SCISSOR_TEST);
        sceGuScissor(rect->x, rect->y, rect->w, rect->h);
    } else {
        sceGuDisable(GU_SCISSOR_TEST);
    }

    return 0;
}

static inline int PSP_RenderSetDrawColor(SDL_Renderer *renderer, SDL_RenderCommand *cmd)
{
    uint8_t colorR, colorG, colorB, colorA;

    colorR = cmd->data.color.r;
    colorG = cmd->data.color.g;
    colorB = cmd->data.color.b;
    colorA = cmd->data.color.a;
    sceGuColor(GU_RGBA(colorR, colorG, colorB, colorA));

    return 0;
}

static inline int PSP_RenderClear(SDL_Renderer *renderer, SDL_RenderCommand *cmd)
{
    uint8_t colorR, colorG, colorB, colorA;

    colorR = cmd->data.color.r;
    colorG = cmd->data.color.g;
    colorB = cmd->data.color.b;
    colorA = cmd->data.color.a;

    sceGuClearColor(GU_RGBA(colorR, colorG, colorB, colorA));
    sceGuClearStencil(colorA);
    sceGuClear(GU_FAST_CLEAR_BIT | GU_COLOR_BUFFER_BIT | GU_STENCIL_BUFFER_BIT);

    return 0;
}

static inline int PSP_RenderCopy(SDL_Renderer *renderer, void *vertices, SDL_RenderCommand *cmd)
{
    uint32_t tbw;
    void *twp;
    PSP_RenderData *data = (PSP_RenderData *)renderer->driverdata;
    SDL_Texture *texture = cmd->data.draw.texture;
    PSP_Texture *psp_tex = (PSP_Texture *)texture->driverdata;
    const size_t count = cmd->data.draw.count;
    const VertTV *verts = (VertTV *)(vertices + cmd->data.draw.first);
    const int passes = neededPassesForBlendMode(cmd->data.draw.blend);

    prepareTextureForUpload(texture);

    tbw = psp_tex->swizzled ? psp_tex->textureWidth : psp_tex->width;
    twp = psp_tex->swizzled ? psp_tex->swizzledData : psp_tex->data;

    sceGuTexMode(psp_tex->format, 0, 0, psp_tex->swizzled);
    sceGuTexImage(0, psp_tex->textureWidth, psp_tex->textureHeight, tbw, twp);
    sceGuTexFilter(psp_tex->filter, psp_tex->filter);
    sceGuEnable(GU_TEXTURE_2D);

    setShadeModel(data, GU_FLAT);
    for (int i = 0; i < passes; i++) {
        setBlendMode(renderer, cmd->data.draw.blend, i);
        sceGuDrawArray(GU_SPRITES, GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_2D, count, 0, verts);
    }
    postBlendMode(data, cmd->data.draw.blend);
    sceGuDisable(GU_TEXTURE_2D);

    return 0;
}

static int PSP_RunCommandQueue(SDL_Renderer *renderer, SDL_RenderCommand *cmd, void *vertices, size_t vertsize)
{
    PSP_RenderData *data = (PSP_RenderData *)renderer->driverdata;

    sceKernelDcacheWritebackRange(data->guList, sizeof(data->guList));
    sceKernelDcacheWritebackRange(vertices, vertsize);
    sceGuStart(GU_DIRECT, data->guList);

    while (cmd) {
        switch (cmd->command) {
        case SDL_RENDERCMD_SETVIEWPORT:
        {
            PSP_RenderSetViewPort(renderer, cmd);
            break;
        }
        case SDL_RENDERCMD_SETCLIPRECT:
        {
            PSP_RenderSetClipRect(renderer, cmd);
            break;
        }
        case SDL_RENDERCMD_SETDRAWCOLOR:
        {
            PSP_RenderSetDrawColor(renderer, cmd);
            break;
        }
        case SDL_RENDERCMD_CLEAR:
        {
            PSP_RenderClear(renderer, cmd);
            break;
        }
        case SDL_RENDERCMD_DRAW_POINTS:
        {
            SDL_RenderCommand *finalcmd = cmd;
            SDL_RenderCommand *nextcmd = cmd->next;
            SDL_BlendMode thisblend = cmd->data.draw.blend;
            size_t count = cmd->data.draw.count;
            const VertV *verts = (VertV *)(vertices + cmd->data.draw.first);
            const int passes = neededPassesForBlendMode(thisblend);
            void *expectedNextVerts = (void *)((uintptr_t)verts + count * sizeof(VertV));

            while (nextcmd) {
                const SDL_RenderCommandType nextcmdtype = nextcmd->command;
                const size_t nextcount = nextcmd->data.draw.count;
                const SDL_BlendMode nextblend = nextcmd->data.draw.blend;
                if (nextcmdtype != SDL_RENDERCMD_DRAW_POINTS || 
                    (vertices + nextcmd->data.draw.first) != expectedNextVerts || 
                    (count + nextcount) > MAX_VERTICES ||
                    nextblend != thisblend) {
                    break; /* can't go any further on this draw call */
                }
                expectedNextVerts = (void *)((uintptr_t)expectedNextVerts + nextcount * sizeof(VertV));
                count += nextcount;
                finalcmd = nextcmd;
                nextcmd = nextcmd->next;
            }

            setShadeModel(data, GU_FLAT);
            for (int i = 0; i < passes; i++) {
                setBlendMode(renderer, thisblend, i);
                sceGuDrawArray(GU_POINTS, GU_VERTEX_32BITF | GU_TRANSFORM_2D, count, 0, verts);
            }
            postBlendMode(data, thisblend);
            cmd = finalcmd;

            break;
        }
        case SDL_RENDERCMD_DRAW_LINES:
        {
            SDL_RenderCommand *finalcmd = cmd;
            SDL_RenderCommand *nextcmd = cmd->next;
            SDL_BlendMode thisblend = cmd->data.draw.blend;
            size_t count = cmd->data.draw.count;
            const VertV *verts = (VertV *)(vertices + cmd->data.draw.first);
            const int passes = neededPassesForBlendMode(thisblend);
            void *expectedNextVerts = (void *)((uintptr_t)verts + count * sizeof(VertV));
            uint8_t gu_primitive = count > 2 ? GU_LINE_STRIP : GU_LINES;

            while (nextcmd) {
                const SDL_RenderCommandType nextcmdtype = nextcmd->command;
                const size_t nextcount = nextcmd->data.draw.count;
                const uint8_t nextgu_primitive = nextcount > 2 ? GU_LINE_STRIP : GU_LINES;
                const SDL_BlendMode nextblend = nextcmd->data.draw.blend;
                if (nextcmdtype != SDL_RENDERCMD_DRAW_LINES || 
                    gu_primitive != GU_LINES ||
                    gu_primitive != nextgu_primitive ||
                    (vertices + nextcmd->data.draw.first) != expectedNextVerts || 
                    (count + nextcount) > MAX_VERTICES ||
                    nextblend != thisblend) {
                    break; /* can't go any further on this draw call */
                }
                expectedNextVerts = (void *)((uintptr_t)expectedNextVerts + nextcount * sizeof(VertV));
                count += nextcount;
                finalcmd = nextcmd;
                nextcmd = nextcmd->next;
            }

            setShadeModel(data, GU_FLAT);
            for (int i = 0; i < passes; i++) {
                setBlendMode(renderer, thisblend, i);
                sceGuDrawArray(gu_primitive, GU_VERTEX_32BITF | GU_TRANSFORM_2D, count, 0, verts);
            }
            postBlendMode(data, thisblend);
            cmd = finalcmd;

            break;
        }
        case SDL_RENDERCMD_FILL_RECTS:
        {
            SDL_RenderCommand *finalcmd = cmd;
            SDL_RenderCommand *nextcmd = cmd->next;
            SDL_BlendMode thisblend = cmd->data.draw.blend;
            size_t count = cmd->data.draw.count;
            const VertV *verts = (VertV *)(vertices + cmd->data.draw.first);
            const int passes = neededPassesForBlendMode(thisblend);
            void *expectedNextVerts = (void *)((uintptr_t)verts + count * sizeof(VertV));

            while (nextcmd) {
                const SDL_RenderCommandType nextcmdtype = nextcmd->command;
                const size_t nextcount = nextcmd->data.draw.count;
                const SDL_BlendMode nextblend = nextcmd->data.draw.blend;
                if (nextcmdtype != SDL_RENDERCMD_FILL_RECTS || 
                    (vertices + nextcmd->data.draw.first) != expectedNextVerts || 
                    (count + nextcount) > MAX_VERTICES ||
                    nextblend != thisblend) {
                    break; /* can't go any further on this draw call */
                }
                expectedNextVerts = (void *)((uintptr_t)expectedNextVerts + nextcount * sizeof(VertV));
                count += nextcount;
                finalcmd = nextcmd;
                nextcmd = nextcmd->next;
            }

            setShadeModel(data, GU_FLAT);
            for (int i = 0; i < passes; i++) {
                setBlendMode(renderer, thisblend, i);
                sceGuDrawArray(GU_SPRITES, GU_VERTEX_32BITF | GU_TRANSFORM_2D, count, 0, verts);
            }
            postBlendMode(data, thisblend);
            cmd = finalcmd;

            break;
        }
        case SDL_RENDERCMD_COPY:
        {
            PSP_RenderCopy(renderer, vertices, cmd);
            break;
        }
        case SDL_RENDERCMD_COPY_EX: /* unused */
            break;
        case SDL_RENDERCMD_GEOMETRY:
        {
            SDL_Texture *texture = cmd->data.draw.texture;
            SDL_RenderCommand *finalcmd = cmd;
            SDL_RenderCommand *nextcmd = cmd->next;
            SDL_BlendMode thisblend = cmd->data.draw.blend;
            const void *verts = (void *)(vertices + cmd->data.draw.first);
            size_t count = cmd->data.draw.count;
            const int passes = neededPassesForBlendMode(thisblend);
            const size_t structSize = cmd->data.draw.texture != NULL ? sizeof(VertTCV) : sizeof(VertCV);
            int vtype = texture != NULL ? 
                GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D : 
                GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D;
            void *expectedNextVerts = (void *)((uintptr_t)verts + count * structSize);

            while (nextcmd) {
                const SDL_Texture *nexttexture = nextcmd->data.draw.texture;
                const SDL_RenderCommandType nextcmdtype = nextcmd->command;
                const size_t nextcount = nextcmd->data.draw.count;
                const SDL_BlendMode nextblend = nextcmd->data.draw.blend;
                
                if (nextcmdtype != SDL_RENDERCMD_GEOMETRY || 
                    texture != nexttexture ||
                    (vertices + nextcmd->data.draw.first) != expectedNextVerts || 
                    (count + nextcount) > MAX_VERTICES ||
                    nextblend != thisblend) {
                    break; /* can't go any further on this draw call */
                }
                
                expectedNextVerts = (void *)((uintptr_t)expectedNextVerts + nextcount * structSize);
                count += nextcount;
                finalcmd = nextcmd;
                nextcmd = nextcmd->next;
            }

            if (texture != NULL) {
                uint32_t tbw;
                void *twp;
                PSP_Texture *psp_tex = (PSP_Texture *)texture->driverdata;

                prepareTextureForUpload(texture);

                tbw = psp_tex->swizzled ? psp_tex->swizzledWidth : psp_tex->width;
                twp = psp_tex->swizzled ? psp_tex->swizzledData : psp_tex->data;

                sceGuTexMode(psp_tex->format, 0, 0, psp_tex->swizzled);
                sceGuTexImage(0, psp_tex->textureWidth, psp_tex->textureHeight, tbw, twp);
                sceGuTexFilter(psp_tex->filter, psp_tex->filter);
                sceGuEnable(GU_TEXTURE_2D);
            }
            
            setShadeModel(data, GU_SMOOTH);
            for (int i = 0; i < passes; i++) {
                setBlendMode(renderer, thisblend, i);
                sceGuDrawArray(GU_TRIANGLES, vtype, count, 0, verts);
            }
            postBlendMode(data, thisblend);

            if (texture != NULL) {
                sceGuDisable(GU_TEXTURE_2D);
            }
            cmd = finalcmd;

            break;
        }
        case SDL_RENDERCMD_NO_OP:
            break;
        }
        cmd = cmd->next;
    }

    finishAndSyncGPUList(data);

    return 0;
}

static int PSP_RenderReadPixels(SDL_Renderer *renderer, const SDL_Rect *rect,
                                Uint32 format, void *pixels, int pitch)
{
    return SDL_Unsupported();
}

static int PSP_RenderPresent(SDL_Renderer *renderer)
{
    PSP_RenderData *data = (PSP_RenderData *)renderer->driverdata;

    if (((data->vsync == 2) && (data->vblank_not_reached)) || // Dynamic
        (data->vsync == 1)) {                                 // Normal VSync
        sceDisplayWaitVblankStart();
    }
    data->vblank_not_reached = SDL_TRUE;

    data->backbuffer = data->frontbuffer;
    data->frontbuffer = vabsptr(sceGuSwapBuffers());

    return 0;
}

static void PSP_DestroyTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    PSP_Texture *psp_tex = (PSP_Texture *)texture->driverdata;
    PSP_RenderData *data = (PSP_RenderData *)renderer->driverdata;

    if (!data) {
        return;
    }

    if (!psp_tex) {
        return;
    }

    if (psp_tex->swizzledData) {
        vfree(psp_tex->swizzledData);
    } else if (texture->access != SDL_TEXTUREACCESS_STATIC) {
        vfree(psp_tex->data);
    } else {
        SDL_free(psp_tex->data);
    }
    SDL_free(psp_tex);
    texture->driverdata = NULL;
}

static void PSP_DestroyRenderer(SDL_Renderer *renderer)
{
    PSP_RenderData *data = (PSP_RenderData *)renderer->driverdata;
    if (data) {
        sceKernelDisableSubIntr(PSP_VBLANK_INT, 0);
        sceKernelReleaseSubIntrHandler(PSP_VBLANK_INT, 0);
        sceDisplayWaitVblankStart();
        sceGuDisplay(GU_FALSE);

        sceGuTerm();
        vfree(data->backbuffer);
        vfree(data->frontbuffer);

        SDL_free(data);
    }
}

static int PSP_SetVSync(SDL_Renderer *renderer, const int vsync)
{
    PSP_RenderData *data = (PSP_RenderData *)renderer->driverdata;
    SDL_bool dynamicVsync = SDL_GetHintBoolean(SDL_HINT_PSP_DYNAMIC_VSYNC, SDL_FALSE);
    data->vsync = vsync ? (dynamicVsync ? 2 : 1) : 0;
    return 0;
}

static int PSP_CreateRenderer(SDL_Renderer *renderer, SDL_Window *window, Uint32 flags)
{
    PSP_RenderData *data;
    uint32_t bufferSize = 0;
    int32_t g_packet_size;
    SDL_bool dynamicVsync;

    data = (PSP_RenderData *)SDL_calloc(1, sizeof(*data));
    if (!data) {
        PSP_DestroyRenderer(renderer);
        return SDL_OutOfMemory();
    }

    // flush cache so that no stray data remains
    sceKernelDcacheWritebackAll();

    data->drawBufferFormat = pixelFormatToPSPFMT(SDL_GetWindowPixelFormat(window));

    /* Specific GU init */
    bufferSize = getMemorySize(PSP_FRAME_BUFFER_WIDTH, PSP_SCREEN_HEIGHT, data->drawBufferFormat);
    data->frontbuffer = vramalloc(bufferSize);
    data->backbuffer = vramalloc(bufferSize);

    sceGuInit();
    sceGuStart(GU_DIRECT, data->guList);
    sceGuDrawBuffer(data->drawBufferFormat, vrelptr(data->frontbuffer), PSP_FRAME_BUFFER_WIDTH);
    sceGuDispBuffer(PSP_SCREEN_WIDTH, PSP_SCREEN_HEIGHT, vrelptr(data->backbuffer), PSP_FRAME_BUFFER_WIDTH);

    sceGuOffset(2048 - (PSP_SCREEN_WIDTH >> 1), 2048 - (PSP_SCREEN_HEIGHT >> 1));
    sceGuViewport(2048, 2048, PSP_SCREEN_WIDTH, PSP_SCREEN_HEIGHT);

    sceGuDisable(GU_DEPTH_TEST);

    sceGuScissor(0, 0, PSP_SCREEN_WIDTH, PSP_SCREEN_HEIGHT);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDisable(GU_ALPHA_TEST);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);

    g_packet_size = sceGuFinish();
    sceKernelDcacheWritebackRange(data->guList, g_packet_size);
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);

    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);

    /* Improve performance when VSYC is enabled and it is not reaching the 60 FPS */
    dynamicVsync = SDL_GetHintBoolean(SDL_HINT_PSP_DYNAMIC_VSYNC, SDL_FALSE);
    data->vsync = flags & SDL_RENDERER_PRESENTVSYNC ? (dynamicVsync ? 2 : 1) : 0;
    if (data->vsync == 2) {
        sceKernelRegisterSubIntrHandler(PSP_VBLANK_INT, 0, psp_on_vblank, data);
        sceKernelEnableSubIntr(PSP_VBLANK_INT, 0);
    }
    data->vblank_not_reached = SDL_TRUE;

    renderer->WindowEvent = PSP_WindowEvent;
    renderer->SupportsBlendMode = PSP_SupportsBlendMode;
    renderer->CreateTexture = PSP_CreateTexture;
    renderer->UpdateTexture = PSP_UpdateTexture;
    renderer->LockTexture = PSP_LockTexture;
    renderer->UnlockTexture = PSP_UnlockTexture;
    renderer->SetTextureScaleMode = PSP_SetTextureScaleMode;
    renderer->SetRenderTarget = PSP_SetRenderTarget;
    renderer->QueueSetViewport = PSP_QueueSetViewport;
    renderer->QueueSetDrawColor = PSP_QueueSetViewport;
    renderer->QueueDrawPoints = PSP_QueueDrawPoints;
    renderer->QueueDrawLines = PSP_QueueDrawPoints;
    renderer->QueueGeometry = PSP_QueueGeometry;
    renderer->QueueFillRects = PSP_QueueFillRects;
    renderer->QueueCopy = PSP_QueueCopy;
    renderer->RunCommandQueue = PSP_RunCommandQueue;
    renderer->RenderReadPixels = PSP_RenderReadPixels;
    renderer->RenderPresent = PSP_RenderPresent;
    renderer->DestroyTexture = PSP_DestroyTexture;
    renderer->DestroyRenderer = PSP_DestroyRenderer;
    renderer->SetVSync = PSP_SetVSync;
    renderer->info = PSP_RenderDriver.info;
    renderer->driverdata = data;
    renderer->window = window;

    return 0;
}

SDL_RenderDriver PSP_RenderDriver = {
    .CreateRenderer = PSP_CreateRenderer,
    .info = {
        .name = "PSP_GU",
        .flags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE,
        .num_texture_formats = 4,
        .texture_formats = {
            [0] = SDL_PIXELFORMAT_BGR565,
            [1] = SDL_PIXELFORMAT_ABGR1555,
            [2] = SDL_PIXELFORMAT_ABGR4444,
            [3] = SDL_PIXELFORMAT_ABGR8888,
        },
        .max_texture_width = 512,
        .max_texture_height = 512,
    }
};

#endif /* SDL_VIDEO_RENDER_PSP */

/* vi: set ts=4 sw=4 expandtab: */
