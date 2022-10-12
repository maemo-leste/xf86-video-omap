/*
 * omap_exa_pvr.h
 *
 * Copyright (C) 2021 Ivaylo Dimitrov <ivo.g.dimitrov.75@gmail.com>
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <https://www.gnu.org/licenses/>.
 *
 */

#ifndef __OMAP_EXA_PVR_H__
#define __OMAP_EXA_PVR_H__

#include <services.h>
#include <pvr2d.h>
#include <sgxapi_km.h>

typedef struct
{
	PVRSRV_CONNECTION *services;
	PVRSRV_DEV_DATA dev_data;
	IMG_HANDLE h_dev_mem_context;
	IMG_HANDLE h_mapping_heap;
	PVRSRV_MISC_INFO misc_info;
	PVR2DCONTEXTHANDLE hPVR2DContext;
} PVRSERVICES, *PPVRSERVICES;

/* #define INSTRUMENT_BO_CACHE */
typedef struct PVR
{
	OMAPEXARec base;
	ExaDriverPtr exa;
	PPVRSERVICES srv;
	void *scanout_priv;
	/* LRU BO maps */
	struct xorg_list map_list;
	unsigned long map_count;
	/* cached BOs, i.e */
	struct xorg_list bo_list;
	unsigned long bo_count;
	unsigned long bo_size;
#ifdef INSTRUMENT_BO_CACHE
	unsigned long bo_cache_hit;
	unsigned long bo_cache_miss;
#endif
} PVRRec, *PVRPtr;

typedef struct PrivPixmap
{
	PVR2DMEMINFO meminfo;
	struct xorg_list map;
} PrivPixmapRec, *PrivPixmapPtr;

typedef struct BoCacheEntry
{
	struct omap_bo *bo;
	PrivPixmapPtr priv;
	struct xorg_list list;
} BoCacheEntryRec, *BoCacheEntryPtr;

typedef enum
{
	SURFACE_BUFF_ACCESS_UNKNOWN = 0x0,
	SURFACE_BUFF_ACCESS_SW = 0x1,
	SURFACE_BUFF_ACCESS_GPU = 0x2,
} SURFACE_BUFF_ACCESS_STATE_T;

struct PvrExaSoftFallbackParams
{
	PicturePtr pPicSrc;
	PicturePtr pPicDst;
	PicturePtr pPicMask;
	int alu;
	Pixel planemask;
	Pixel fgPixel;
	Pixel fillColour;
	SURFACE_BUFF_ACCESS_STATE_T eDestState;
	GCPtr psGC;
};

typedef IMG_RECT SGXHW_RENDER_RECTS;

#define MAX_SOLID_BATCH_RECTS 128
#define MAX_COPY_BATCH_RECTS 51

typedef struct PVRSolidOp_TAG
{
	PixmapPtr pPixmap;
	union
	{
		PVR2DBLTINFO solid2D;
		PVR2D_3DBLT_EXT solid3DExt;
	};
	struct PvrExaSoftFallbackParams softFallback;
	SGXHW_RENDER_RECTS destRect[MAX_SOLID_BATCH_RECTS];
	SGXHW_RENDER_RECTS destBoundBox;
	SGXHW_RENDER_RECTS destSurfaceBox;
	unsigned int minArea;
	unsigned int maxArea;
	unsigned int averageArea;
	IMG_INT bltRectsIdx;
	IMG_INT numBltRects;
	Bool handleDestDamage;
} PVRSolidOp, *PPVRSolidOp;

typedef enum
{
	TRANSF_UNKNOWN = 0xFFFFFFFF,
	TRANSF_NONE = 0x0,
	TRANSF_ROT_90 = 0x1,
	TRANSF_ROT_180 = 0x2,
	TRANSF_ROT_270 = 0x3,
	TRANSF_SCALE = 0x4
} RENDER_TRANSFORMATION;

typedef struct PVRBltRect_TAG
{
	SGXHW_RENDER_RECTS destRect[MAX_COPY_BATCH_RECTS];
	SGXHW_RENDER_RECTS srcRect[MAX_COPY_BATCH_RECTS];
	SGXHW_RENDER_RECTS maskRect[MAX_COPY_BATCH_RECTS];
	SGXHW_RENDER_RECTS destBoundBox;
	SGXHW_RENDER_RECTS srcBoundBox;
	SGXHW_RENDER_RECTS maskBoundBox;
	SGXHW_RENDER_RECTS destSurfaceBox;
	SGXHW_RENDER_RECTS srcSurfaceBox;
	SGXHW_RENDER_RECTS maskSurfaceBox;
} PVRBltRect, *PPVRBltRect;

typedef enum
{
	SGXTQ_RENDER_RECTS_REPEAT_NONE = 0x0,
	SGXTQ_RENDER_RECTS_REPEAT_NORMAL = 0x1,
	SGXTQ_RENDER_RECTS_REPEAT_PAD = 0x2,
	SGXTQ_RENDER_RECTS_REPEAT_REFLECT = 0x3,
} SGXHW_RENDER_RECTS_REPEAT_TYPE;

typedef struct PVRRenderOp_TAG
{
	PicturePtr pSrcPicture;
	PixmapPtr pSrc;
	PicturePtr pMaskPicture;
	PixmapPtr pMask;
	PicturePtr pDestPicture;
	PixmapPtr pDest;
	int op;
	PVRBltRect bltRects;
	IMG_INT bltRectsIdx;
	IMG_INT numBltRects;
	IMG_BOOL supportsAtlas;
	IMG_BOOL isBatchCopy;
	PVRSRV_PIXEL_FORMAT destColorFormat;
	PVRSRV_PIXEL_FORMAT srcColorFormat;
	PVRSRV_PIXEL_FORMAT maskColorFormat;
	SGXHW_RENDER_RECTS_REPEAT_TYPE srcRepeatType;
	SGXHW_RENDER_RECTS_REPEAT_TYPE maskRepeatType;
	IMG_BOOL srcForceAlpha;
	IMG_BOOL destForceAlpha;
	PVRSRV_CLIENT_MEM_INFO *pDestMemInfo;
	PVRSRV_CLIENT_MEM_INFO *pSrcMemInfo;
	PVRSRV_CLIENT_MEM_INFO *pMaskMemInfo;
	PVR2D_HANDLE hCode;
	RENDER_TRANSFORMATION transform;
	Bool handleDestDamage;
} PVRRenderOp, *PPVRRenderOp;

typedef struct PVRCopyOp_TAG
{
	union
	{
		PVR2DBLTINFO blt2D;
		PVR2D_3DBLT blt3D;
	};

	PVRRenderOp renderOp;
} PVRCopyOp, *PPVRCopyOp;

const char *sgxErrorCodeToString(PVR2DERROR err);

static inline PVRPtr
PVREXAPTR(ScrnInfoPtr pScrn)
{
	return (PVRPtr)OMAPEXAPTR(pScrn);
}

void sgxUnmapPixmapBo(ScreenPtr pScreen, OMAPPixmapPrivPtr pixmapPriv);
PrivPixmapPtr sgxMapPixmapBo(ScreenPtr pScreen, OMAPPixmapPrivPtr pixmapPriv);
void sgxWaitPixmap(PixmapPtr pPixmap);

#endif /* __OMAP_EXA_PVR_H__ */
