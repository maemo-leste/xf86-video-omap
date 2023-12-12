/*
 * omap_exa_pvr.c
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <dlfcn.h>
#include <sys/time.h>

#include <exa.h>
#include <gc.h>
#include <list.h>

#include <pvr_debug.h>

#include "omap_driver.h"
#include "omap_exa.h"

#include "omap_exa_pvr.h"
#include "omap_pvr_helpers.h"
#include "omap_pvr_use.h"
#include "omap_pvr_sgx.h"

#define PVR_MAX_BO_MAPS 32
/* semi-arbitrary value, maybe add new parameter */
#define PVR_BO_CACHE_SIZE 4 * 1024 * 1024

/* #define INSTRUMENT_BO_MAP */

/* seems to be slower, also, transparency is broken */
/* #define USE_HW_COMPOSITING */

static PVRSolidOp gsSolidOp;
static PVRCopyOp gsCopy2DOp;
static PVRRenderOp gsRenderOp;

static Bool
copy2d(int bitsPerPixel)
{
	return bitsPerPixel != 8;
}

static unsigned char sgxRop[2][16] =
{
	{
		PVR2DROPclear,		/* GXclear */
		PVR2DROPand,		/* GXand */
		PVR2DROPandReverse,	/* GXandReverse */
		PVR2DROPcopy,		/* GXcopy */
		PVR2DROPandInverted,	/* GXandInverted */
		PVR2DROPnoop,		/* GXnoop */
		PVR2DROPxor,		/* GXxor */
		PVR2DROPor,		/* GXor */
		PVR2DROPnor,		/* GXnor */
		PVR2DROPequiv,		/* GXequiv */
		PVR2DROPinvert,		/* GXinvert */
		PVR2DROPorReverse,	/* GXorReverse */
		PVR2DROPcopyInverted,	/* GXcopyInverted */
		PVR2DROPorInverted,	/* GXorInverted */
		PVR2DROPnand,		/* GXnand */
		PVR2DROPset		/* GXset */
	},
	{
		PVR2DROPclear,
		PVR2DPATROPand,
		PVR2DPATROPandReverse,
		PVR2DPATROPcopy,
		PVR2DPATROPandInverted,
		PVR2DROPnoop,
		PVR2DPATROPxor,
		PVR2DPATROPor,
		PVR2DPATROPnor,
		PVR2DPATROPequiv,
		PVR2DPATROPinvert,
		PVR2DPATROPorReverse,
		PVR2DPATROPcopyInverted,
		PVR2DPATROPorInverted,
		PVR2DPATROPnand,
		PVR2DROPset
	}
};

const char *
sgxErrorCodeToString(PVR2DERROR err)
{
	switch (err)
	{
		case PVR2DERROR_MAPPING_FAILED:
			return "Mapping failed";
		case PVR2DERROR_NOT_YET_IMPLEMENTED:
			return "Not yet implemented";
		case PVR2DERROR_HW_FEATURE_NOT_SUPPORTED:
			return "Hardware feature not supported";
		case PVR2DERROR_BLT_NOTCOMPLETE:
			return "Blit not complete";
		case PVR2DERROR_GENERIC_ERROR:
			return "Generic error";
		case PVR2DERROR_IOCTL_ERROR:
			return "IOCTL error";
		case PVR2DERROR_DEVICE_NOT_PRESENT:
			return "Device not present";
		case PVR2DERROR_MEMORY_UNAVAILABLE:
			return "Memory unavailable";
		case PVR2DERROR_INVALID_CONTEXT:
			return "Invalid context";
		case PVR2DERROR_DEVICE_UNAVAILABLE:
			return "Device unavailable";
		case PVR2DERROR_INVALID_PARAMETER:
			return "Invalid parameter";
		case PVR2D_OK:
			return "Ok";
			break;
		default:
			return "Unknown error";
	}
}

static void
waitForBlitsCompleteOnDeviceMem(PixmapPtr pPixmap)
{
	ScrnInfoPtr pScrn = pix2scrn(pPixmap);
	PVRPtr pPVR = PVREXAPTR(pScrn);
	OMAPPixmapPrivPtr pixmapPriv = exaGetPixmapDriverPrivate(pPixmap);
	PrivPixmapPtr pvrPixmapPriv;
	PVR2DERROR err;

	pvrPixmapPriv = sgxMapPixmapBo(pPixmap->drawable.pScreen, pixmapPriv);

	if (!pvrPixmapPriv)
		return;

	err = PVR2DQueryBlitsComplete(pPVR->srv->hPVR2DContext,
				      &pvrPixmapPriv->meminfo, 1);
	if (err)
	{
		ERROR_MSG("%s: PVR2DQueryBlitsComplete failed with error code: %d (%s)",
			  __func__, err, sgxErrorCodeToString(err));
	}
}

static void
sgxWaitPixmap(PixmapPtr pPixmap)
{
	OMAPPixmapPrivPtr pixmapPriv;
	PrivPixmapPtr priv;
	ScrnInfoPtr pScrn;
	OMAPPtr pOMAP;

	if (!pPixmap)
		return;

	pScrn = pix2scrn(pPixmap);
	pOMAP = OMAPPTR(pScrn);
	pixmapPriv = exaGetPixmapDriverPrivate(pPixmap);
	priv = pixmapPriv->priv;

	if (priv) {
		if (priv->is_gpu &&
		    (pixmapPriv->bo != pOMAP->scanout || pOMAP->ManualUpdate)) {
			waitForBlitsCompleteOnDeviceMem(pPixmap);
		}

		priv->is_gpu = FALSE;
	}
}

void
flushScanout(PixmapPtr pPixmap)
{
	ScrnInfoPtr pScrn = pix2scrn(pPixmap);
	OMAPPixmapPrivPtr pixmapPriv = exaGetPixmapDriverPrivate(pPixmap);
	OMAPPtr pOMAP = OMAPPTR(pScrn);

	if (pOMAP->ManualUpdate && pixmapPriv->bo == pOMAP->scanout) {
		sgxWaitPixmap(pPixmap);
		drmmode_flush_scanout(pScrn);
	}
}

void
setPixmapOnGPU(PixmapPtr pPixmap)
{
	OMAPPixmapPrivPtr pixmapPriv = exaGetPixmapDriverPrivate(pPixmap);
	PrivPixmapPtr priv = pixmapPriv->priv;

	priv->is_gpu = TRUE;
}

static void
sgxAccelDestroy(ScreenPtr pScreen, PVRPtr pPVR)
{
	PVRUseDeInit(pScreen, pPVR);
	DeInitialiseServices(pScreen, pPVR->srv);
}

static void
sgxCacheInit(PVRPtr pPVR)
{
	xorg_list_init(&pPVR->map_list);
	pPVR->map_count = 0;
	xorg_list_init(&pPVR->bo_list);
	pPVR->bo_count = 0;
	pPVR->bo_size = 0;
#ifdef INSTRUMENT_BO_CACHE
	pPVR->bo_cache_hit = 0;
	pPVR->bo_cache_miss = 0;
#endif
}

static void
sgxMapRemove(ScreenPtr pScreen, PVRPtr pPVR, PrivPixmapPtr pvrPixmapPriv)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);

	if (!pvrPixmapPriv->meminfo.hPrivateData)
		return;

	pPVR->map_count--;
	xorg_list_del(&pvrPixmapPriv->map);

	DEBUG_MSG("Mappings entry %p removed, list size %lu",
		  pvrPixmapPriv, pPVR->map_count);
}

static void
sgxMapUnmapLRU(ScreenPtr pScreen, PVRPtr pPVR)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	PrivPixmapPtr pvrPixmapPriv;

	if (pPVR->map_count <= PVR_MAX_BO_MAPS)
		return;

	pvrPixmapPriv = xorg_list_first_entry(
				&pPVR->map_list, PrivPixmapRec, map);
	DEBUG_MSG("Mappings list is full, removing last entry");

	sgxMapRemove(pScreen, pPVR, pvrPixmapPriv);

	if (pvrPixmapPriv->meminfo.hPrivateData) {
		PVRUnMapBo(pScreen, pPVR->srv, &pvrPixmapPriv->meminfo);
		pvrPixmapPriv->meminfo.hPrivateData = NULL;
	}
}

static void
sgxMapMarkUsed(PVRPtr pPVR, PrivPixmapPtr pvrPixmapPriv)
{
	struct xorg_list *map = &pvrPixmapPriv->map;

	if (map->next == map->prev && map->prev == map)
		pPVR->map_count++;
	else
		xorg_list_del(map);

	xorg_list_append(map, &pPVR->map_list);
}

PrivPixmapPtr
sgxMapPixmapBo(ScreenPtr pScreen, OMAPPixmapPrivPtr pixmapPriv)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	PrivPixmapPtr pvrPixmapPriv;
	OMAPPtr pOMAP = OMAPPTR(pScrn);
	PVRPtr pPVR = PVREXAPTR(pScrn);

	if (!pixmapPriv->priv) {
		pixmapPriv->priv = calloc(sizeof(PrivPixmapRec), 1);
		xorg_list_init(&((PrivPixmapPtr)pixmapPriv->priv)->map);
	}

	pvrPixmapPriv = pixmapPriv->priv;

	if (!pvrPixmapPriv->meminfo.hPrivateData) {
#ifdef INSTRUMENT_BO_MAP
		struct timeval stop, start;

		gettimeofday(&start, NULL);
#endif
		sgxMapUnmapLRU(pScreen, pPVR);

		if (!PVRMapBo(pScreen, pPVR->srv, pOMAP->drmFD, pixmapPriv->bo,
			      &pvrPixmapPriv->meminfo)) {
			free(pvrPixmapPriv);
			pixmapPriv->priv = NULL;
			return NULL;
		}
#ifdef INSTRUMENT_BO_MAP
		gettimeofday(&stop, NULL);
		DEBUG_MSG("PVRMapBo took %lu us",
			  (stop.tv_sec - start.tv_sec) * 1000000 +
			  stop.tv_usec - start.tv_usec);
#endif
	}

	/*
	 * We have to keep a pointer, as otherwise we cannot unmap in
	 * CloseScreen. Also, never add scanout mappings to the list.
	 */
	if (pixmapPriv->bo == pOMAP->scanout)
		pPVR->scanout_priv = pixmapPriv->priv;
	else
		sgxMapMarkUsed(pPVR, pvrPixmapPriv);

	return pvrPixmapPriv;
}

void
sgxUnmapPixmapBo(ScreenPtr pScreen, OMAPPixmapPrivPtr pixmapPriv)
{
	PrivPixmapPtr pvrPixmapPriv = pixmapPriv->priv;
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	PVRPtr pPVR = PVREXAPTR(pScrn);

	if (pvrPixmapPriv) {
		if (pPVR->scanout_priv == pvrPixmapPriv)
			pPVR->scanout_priv = NULL;
		else
			sgxMapRemove(pScreen, pPVR, pvrPixmapPriv);

		if (pvrPixmapPriv->meminfo.hPrivateData)
			PVRUnMapBo(pScreen, pPVR->srv, &pvrPixmapPriv->meminfo);

		free(pvrPixmapPriv);
		pixmapPriv->priv = NULL;
	}
}

static void
sgxBoCacheRemove(ScreenPtr pScreen, PVRPtr pPVR, BoCacheEntryPtr entry)
{
	PrivPixmapPtr priv = entry->priv;

	if (priv && priv->meminfo.hPrivateData) {
		sgxMapRemove(pScreen, pPVR, priv);
		PVRUnMapBo(pScreen, pPVR->srv, &priv->meminfo);
	}

	free(entry->priv);
	pPVR->bo_size -= omap_bo_size(entry->bo);
	pPVR->bo_count--;
	omap_bo_del(entry->bo);
	xorg_list_del(&entry->list);
	free(entry);
}

static struct omap_bo *
sgxBoCacheGet(ScreenPtr pScreen, uint32_t size, void **priv)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	PVRPtr pPVR = PVREXAPTR(pScrn);
	BoCacheEntryPtr entry = NULL;

	size = (size + (4096 - 1)) & ~(4096 - 1);

#ifdef INSTRUMENT_BO_CACHE
	DEBUG_MSG("%s cache stats: hits %lu, misses %lu", __func__,
		  pPVR->bo_cache_hit, pPVR->bo_cache_miss);
#endif
	xorg_list_for_each_entry(entry, &pPVR->bo_list, list) {
		if (omap_bo_size(entry->bo) == size) {
			struct omap_bo *bo = entry->bo;

			*priv = entry->priv;
			pPVR->bo_count--;
			pPVR->bo_size -= omap_bo_size(bo);
			xorg_list_del(&entry->list);
			free(entry);
			DEBUG_MSG("%s cache hit entries %lu, bo %p, block size %u",
				  __func__, pPVR->bo_count, bo, size);
#ifdef INSTRUMENT_BO_CACHE
			pPVR->bo_cache_hit++;
#endif
			return bo;
		}
	}

	DEBUG_MSG("%s cache miss for size %u", __func__, size);
#ifdef INSTRUMENT_BO_CACHE
	pPVR->bo_cache_miss++;
#endif
	return NULL;
}

static Bool
sgxBoCachePut(ScreenPtr pScreen, struct omap_bo *bo, PrivPixmapPtr priv)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	PVRPtr pPVR = PVREXAPTR(pScrn);
	BoCacheEntryPtr entry;

	if (!bo)
		return FALSE;

	entry = calloc(sizeof(BoCacheEntryRec), 1);
	entry->bo = bo;
	entry->priv = priv;
	pPVR->bo_count++;
	pPVR->bo_size += omap_bo_size(bo);
	xorg_list_append(&entry->list, &pPVR->bo_list);

	while (pPVR->bo_size > PVR_BO_CACHE_SIZE) {
		entry = xorg_list_first_entry(&pPVR->bo_list, BoCacheEntryRec,
					      list);
		sgxBoCacheRemove(pScreen, pPVR, entry);
	}

	DEBUG_MSG("%s bo %p put in cache, block size %u, entries count %lu, total %lu KiB",
		  __func__, bo, omap_bo_size(bo), pPVR->bo_count,
		  pPVR->bo_size / 1024);

	return TRUE;
}

static Bool
CloseScreen(CLOSE_SCREEN_ARGS_DECL)
{
	BoCacheEntryPtr entry, tmp;
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	PVRPtr pPVR = PVREXAPTR(pScrn);
	OMAPPtr pOMAP = OMAPPTR_FROM_SCREEN(pScreen);

	xorg_list_for_each_entry_safe(entry, tmp, &pPVR->bo_list, list)
		sgxBoCacheRemove(pScreen, pPVR, entry);

	DEBUG_MSG("%s bo cache size after cleanup %lu",
		  __func__, pPVR->bo_count);

	if (pPVR->scanout_priv) {
		PrivPixmapPtr pvrPixmapPriv = pPVR->scanout_priv;

		if (pvrPixmapPriv->meminfo.hPrivateData)
			PVRUnMapBo(pScreen, pPVR->srv, &pvrPixmapPriv->meminfo);

		free(pvrPixmapPriv);
		pPVR->scanout_priv = NULL;
	}

	exaDriverFini(pScreen);
	free(pOMAP->pOMAPEXA);
	pOMAP->pOMAPEXA = NULL;

	return TRUE;
}

static void
FreeScreen(FREE_SCREEN_ARGS_DECL)
{
	SCRN_INFO_PTR(arg);
	ScreenPtr pScreen = xf86ScrnToScreen(pScrn);
	PVRPtr pPVR = PVREXAPTR(pScrn);

	sgxAccelDestroy(pScreen, pPVR);
}

static void
sgxDestroyPixmap(ScreenPtr pScreen, void *driverPriv)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	OMAPPixmapPrivPtr pixmapPriv = driverPriv;

	DEBUG_MSG("%s", __func__);

	if ((pixmapPriv->flags & OMAP_BO_WC) &&
	    (pixmapPriv->flags & ~OMAP_BO_WC) == 0) {
		if (!sgxBoCachePut(pScreen, pixmapPriv->bo, pixmapPriv->priv))
			sgxUnmapPixmapBo(pScreen, driverPriv);
		else {
			/* cache takes ownership */
			pixmapPriv->bo = NULL;
			pixmapPriv->priv = NULL;
		}
	} else
		sgxUnmapPixmapBo(pScreen, driverPriv);

	OMAPDestroyPixmap(pScreen, driverPriv);
}

static Bool
sgxModifyPixmapHeader(PixmapPtr pPixmap, int width, int height, int depth,
		      int bitsPerPixel, int devKind, pointer pPixData)
{
	ScrnInfoPtr pScrn = pix2scrn(pPixmap);
	ScreenPtr pScreen = pPixmap->drawable.pScreen;
	OMAPPtr pOMAP = OMAPPTR(pScrn);
	OMAPPixmapPrivPtr priv = exaGetPixmapDriverPrivate(pPixmap);
	uint32_t size;
	Bool ret;

	if (pPixmap->usage_hint & (OMAP_CREATE_PIXMAP_TILED |
				   OMAP_CREATE_PIXMAP_SCANOUT |
				   OMAP_CREATE_PIXMAP_XV) ||
	    pPixData) {
		sgxUnmapPixmapBo(pScreen, priv);
		return OMAPModifyPixmapHeader(pPixmap, width, height, depth,
					      bitsPerPixel, devKind, pPixData);
	}

	ret = miModifyPixmapHeader(pPixmap, width, height, depth,
			bitsPerPixel, devKind, NULL);
	if (!ret)
		return ret;

	width	= pPixmap->drawable.width;
	height	= pPixmap->drawable.height;
	depth	= pPixmap->drawable.depth;
	bitsPerPixel = pPixmap->drawable.bitsPerPixel;
	pPixmap->devKind = OMAPCalculateStride(width, bitsPerPixel);
	size = pPixmap->devKind * height;

	if ((!priv->bo) || (omap_bo_size(priv->bo) < size)) {
		struct omap_bo *bo;

		if (sgxBoCachePut(pScreen, priv->bo, priv->priv)) {
			priv->bo = NULL;
			priv->priv = NULL;
		}

		bo = sgxBoCacheGet(pScreen, size, &priv->priv);

		if (!bo) {
			size = (size + (4096 - 1)) & ~(4096 - 1);
			sgxUnmapPixmapBo(pScreen, priv);
			omap_bo_del(priv->bo);
			priv->bo = omap_bo_new(pOMAP->dev, size, OMAP_BO_WC);
		} else
			priv->bo = bo;

		priv->flags = OMAP_BO_WC;
		priv->tiled = FALSE;
	}

	if (!priv->bo) {
		ERROR_MSG("failed to allocate %dx%d bo, size=%d",
			  width, height, size);
	}

	return priv->bo != NULL;
}

static IMG_BOOL
pvrAddRectToBoundingBox(SGXHW_RENDER_RECTS *pBoundingBox,
			SGXHW_RENDER_RECTS *pNewRect,
			SGXHW_RENDER_RECTS *surfaceBox)
{
	IMG_INT32 x0;
	IMG_INT32 y0;
	IMG_INT32 x1;
	IMG_INT32 y1;
	IMG_INT32 x;

	if (pBoundingBox == NULL || pNewRect == NULL)
		return IMG_FALSE;

	x0 = pBoundingBox->x0;

	if (x0)
	{
		y0 = pBoundingBox->y0;
		x1 = pBoundingBox->x1;
		y1 = pBoundingBox->y1;
	}
	else
	{
		x1 = pBoundingBox->x1;
		y0 = pBoundingBox->y0;
		y1 = pBoundingBox->y1;

		if (!x1)
		{
			x0 = 0;

			if (!y0)
			{
				x1 = 0;
				x0 = 0;

				if ( !y1 )
				{
					x0 = surfaceBox->x1;
					y0 = surfaceBox->y1;
					pBoundingBox->x0 = x0;
					pBoundingBox->y0 = y0;
					x1 = surfaceBox->x0;
					y1 = surfaceBox->y0;
					pBoundingBox->x1 = surfaceBox->x0;
					pBoundingBox->y1 = y1;
				}
			}
		}
	}

	x = pNewRect->x0;

	if (x0 < pNewRect->x0)
		x = x0;

	if (x1 < pNewRect->x1)
		x1 = pNewRect->x1;

	if (x1 <= x)
		return IMG_FALSE;

	if (y0 >= pNewRect->y0)
		y0 = pNewRect->y0;

	if (y1 < pNewRect->y1)
		y1 = pNewRect->y1;

	if (y1 <= y0)
		return IMG_FALSE;

	if (x < surfaceBox->x0)
		pBoundingBox->x0 = surfaceBox->x0;
	else
		pBoundingBox->x0 = x;

	if (x1 > surfaceBox->x1)
		pBoundingBox->x1 = surfaceBox->x1;
	else
		pBoundingBox->x1 = x1;

	if (y0 < surfaceBox->y0)
		pBoundingBox->y0 = surfaceBox->y0;
	else
		pBoundingBox->y0 = y0;

	if (y1 > surfaceBox->y1)
		pBoundingBox->y1 = surfaceBox->y1;
	else
		pBoundingBox->y1 = y1;

	return IMG_TRUE;
}

static IMG_BOOL
pvrInitBoundingBox(SGXHW_RENDER_RECTS *pBoundingBox,
		   SGXHW_RENDER_RECTS *surfaceBox, IMG_INT32 x, IMG_INT32 y)
{
	if (pBoundingBox == NULL || surfaceBox == NULL)
		return IMG_FALSE;

	surfaceBox->x0 = 0;
	surfaceBox->y0 = 0;
	pBoundingBox->x0 = x;
	pBoundingBox->y0 = y;
	surfaceBox->x1 = x;
	surfaceBox->y1 = y;
	pBoundingBox->x1 = surfaceBox->x0;
	pBoundingBox->y1 = surfaceBox->y0;

	return IMG_TRUE;
}

static void
sgxCompositeResetCoordinates(PVRRenderOp *pRenderOp)
{
	pRenderOp->numBltRects = 0;
	pRenderOp->bltRectsIdx = 0;

	if (pRenderOp->pDest ){
		pvrInitBoundingBox(&pRenderOp->bltRects.destBoundBox,
				   &pRenderOp->bltRects.destSurfaceBox,
				   pRenderOp->pDest->drawable.width,
				   pRenderOp->pDest->drawable.height);
	}

	if (pRenderOp->pSrc) {
		pvrInitBoundingBox(&pRenderOp->bltRects.srcBoundBox,
				   &pRenderOp->bltRects.srcSurfaceBox,
				   pRenderOp->pSrc->drawable.width,
				   pRenderOp->pSrc->drawable.height);
	}

	if (pRenderOp->pMask) {
		pvrInitBoundingBox(&pRenderOp->bltRects.maskBoundBox,
				   &pRenderOp->bltRects.maskSurfaceBox,
				   pRenderOp->pMask->drawable.width,
				   pRenderOp->pMask->drawable.height);
	}
}

static IMG_BOOL
sgxCompositeValidateBoundingBox(PVRRenderOp *pRenderOp)
{
	if (pRenderOp->pDest) {
		if (pRenderOp->bltRects.destBoundBox.x1 <=
		    pRenderOp->bltRects.destBoundBox.x0) {
			pRenderOp->bltRects.destBoundBox.x0 =
					pRenderOp->bltRects.destSurfaceBox.x0;
			pRenderOp->bltRects.destBoundBox.x1 =
					pRenderOp->bltRects.destSurfaceBox.x1;
		}

		if (pRenderOp->bltRects.destBoundBox.y1 <=
		    pRenderOp->bltRects.destBoundBox.y0) {
			pRenderOp->bltRects.destBoundBox.y0 =
					pRenderOp->bltRects.destSurfaceBox.y0;
			pRenderOp->bltRects.destBoundBox.y1 =
					pRenderOp->bltRects.destSurfaceBox.y1;
		}
	}

	if (pRenderOp->pSrc) {
		if (pRenderOp->bltRects.srcBoundBox.x1 <=
		    pRenderOp->bltRects.srcBoundBox.x0) {
			pRenderOp->bltRects.srcBoundBox.x0 =
					pRenderOp->bltRects.srcSurfaceBox.x0;
			pRenderOp->bltRects.srcBoundBox.x1 =
					pRenderOp->bltRects.srcSurfaceBox.x1;
		}

		if (pRenderOp->bltRects.srcBoundBox.y1 <=
		    pRenderOp->bltRects.srcBoundBox.y0) {
			pRenderOp->bltRects.srcBoundBox.y0 =
					pRenderOp->bltRects.srcSurfaceBox.y0;
			pRenderOp->bltRects.srcBoundBox.y1 =
					pRenderOp->bltRects.srcSurfaceBox.y1;
		}
	}

	if (pRenderOp->pMask) {
		if (pRenderOp->bltRects.maskBoundBox.x1 <=
		    pRenderOp->bltRects.maskBoundBox.x0)
		{
			pRenderOp->bltRects.maskBoundBox.x0 =
					pRenderOp->bltRects.maskSurfaceBox.x0;
			pRenderOp->bltRects.maskBoundBox.x1 =
					pRenderOp->bltRects.maskSurfaceBox.x1;
		}

		if (pRenderOp->bltRects.maskBoundBox.y1 <=
		    pRenderOp->bltRects.maskBoundBox.y0)
		{
			pRenderOp->bltRects.maskBoundBox.y0 =
					pRenderOp->bltRects.maskSurfaceBox.y0;
			pRenderOp->bltRects.maskBoundBox.y1 =
					pRenderOp->bltRects.maskSurfaceBox.y1;
		}
	}

	return IMG_TRUE;
}

static Bool
sgxPrepareSolid(PixmapPtr pPixmap, int alu, Pixel planemask, Pixel fill_colour)
{
	OMAPPixmapPrivPtr pixmapPriv = exaGetPixmapDriverPrivate(pPixmap);
	PrivPixmapPtr pvrPixmapPriv;
	int bitsPerPixel = pPixmap->drawable.bitsPerPixel;

	if (planemask != -1)
		return FALSE;

	if (bitsPerPixel != 8 && bitsPerPixel != 16 && bitsPerPixel != 32) {
		return FALSE;
	}

	pvrPixmapPriv = sgxMapPixmapBo(pPixmap->drawable.pScreen, pixmapPriv);

	if (!pvrPixmapPriv)
		return FALSE;

	memset(&gsSolidOp, 0, sizeof(gsSolidOp));
	gsSolidOp.pPixmap = pPixmap;

	if (bitsPerPixel == 16 || bitsPerPixel == 32) {
		gsSolidOp.solid2D.ColourKey = 0;
		gsSolidOp.solid2D.CopyCode = sgxRop[1][alu];

		gsSolidOp.solid2D.pSrcMemInfo = 0;
		gsSolidOp.solid2D.SrcOffset = 0;
		gsSolidOp.solid2D.SrcStride = 0;
		gsSolidOp.solid2D.SrcX = 0;
		gsSolidOp.solid2D.SrcY = 0;
		gsSolidOp.solid2D.SrcFormat = PVR2D_ARGB8888;

		gsSolidOp.solid2D.pDstMemInfo = &pvrPixmapPriv->meminfo;
		gsSolidOp.solid2D.DstStride = exaGetPixmapPitch(pPixmap);
		gsSolidOp.solid2D.DstSurfWidth = pPixmap->drawable.width;
		gsSolidOp.solid2D.DstSurfHeight = pPixmap->drawable.height;
		gsSolidOp.solid2D.pMaskMemInfo = 0;
		gsSolidOp.solid2D.BlitFlags = PVR2D_BLIT_DISABLE_ALL;

		if (bitsPerPixel == 16) {
			gsSolidOp.solid2D.Colour = CFILL_565(fill_colour);
			gsSolidOp.solid2D.DstFormat = PVR2D_RGB565;
		} else {
			gsSolidOp.solid2D.DstFormat = PVR2D_ARGB8888;
			gsSolidOp.solid2D.Colour = fill_colour;
		}
	} else {
		gsSolidOp.solid3DExt.sDst.pSurfMemInfo =
				&pvrPixmapPriv->meminfo;
		gsSolidOp.solid3DExt.sDst.SurfOffset = 0;
		gsSolidOp.solid3DExt.sDst.Stride = exaGetPixmapPitch(pPixmap);
		gsSolidOp.solid3DExt.sDst.Format = PVR2D_ALPHA8;
		gsSolidOp.solid3DExt.sDst.SurfWidth = pPixmap->drawable.width;
		gsSolidOp.solid3DExt.sDst.SurfHeight = pPixmap->drawable.height;
		gsSolidOp.solid3DExt.sSrc.pSurfMemInfo =
				&pvrPixmapPriv->meminfo;
		gsSolidOp.solid3DExt.sSrc.SurfOffset = 0;
		gsSolidOp.solid3DExt.sSrc.Stride = exaGetPixmapPitch(pPixmap);
		gsSolidOp.solid3DExt.sSrc.Format = PVR2D_ALPHA8;
		gsSolidOp.solid3DExt.sSrc.SurfWidth = pPixmap->drawable.width;
		gsSolidOp.solid3DExt.sSrc.SurfHeight = pPixmap->drawable.height;
		gsSolidOp.solid3DExt.hUseCode = PVRSolid8BitGetUse(alu);
		gsSolidOp.solid3DExt.uiNumTemporaryRegisters = 1;
		gsSolidOp.solid3DExt.UseParams[0] = fill_colour;
	}

	return TRUE;
}

static void
sgxSolidNextBatch(ScrnInfoPtr pScrn, PVRPtr pPVR, Bool flush)
{
	int i;

	if (!flush) {
		if (gsSolidOp.numBltRects < MAX_SOLID_BATCH_RECTS) {
			gsSolidOp.bltRectsIdx++;
			return;
		}
	}

	if (gsSolidOp.destBoundBox.x1 <= gsSolidOp.destBoundBox.x0)
	{
		gsSolidOp.destBoundBox.x0 = gsSolidOp.destSurfaceBox.x0;
		gsSolidOp.destBoundBox.x1 = gsSolidOp.destSurfaceBox.x1;
	}

	if (gsSolidOp.destBoundBox.y1 <= gsSolidOp.destBoundBox.y0)
	{
		gsSolidOp.destBoundBox.y0 = gsSolidOp.destSurfaceBox.y0;
		gsSolidOp.destBoundBox.y1 = gsSolidOp.destSurfaceBox.y1;
	}

	for (i = 0; i < gsSolidOp.numBltRects; i++) {
		SGXHW_RENDER_RECTS *psRect = &gsSolidOp.destRect[i];
		PVR2DERROR iErr;

		if (gsSolidOp.pPixmap->drawable.bitsPerPixel == 8)
		{
			gsSolidOp.solid3DExt.rcSource.left = psRect->x0;
			gsSolidOp.solid3DExt.rcSource.top = psRect->y0;
			gsSolidOp.solid3DExt.rcSource.right = psRect->x1;
			gsSolidOp.solid3DExt.rcSource.bottom = psRect->y1;
			gsSolidOp.solid3DExt.rcDest.left = psRect->x0;
			gsSolidOp.solid3DExt.rcDest.top = psRect->y0;
			gsSolidOp.solid3DExt.rcDest.right = psRect->x1;
			gsSolidOp.solid3DExt.rcDest.bottom = psRect->y1;

			iErr = PVR2DBlt3DExt(pPVR->srv->hPVR2DContext,
					     &gsSolidOp.solid3DExt);

			if (iErr != PVR2D_OK)
			{
				ERROR_MSG("%s: PVR3DBltExt failed with error code: %d (%s)",
					  __func__, iErr,
					  sgxErrorCodeToString(iErr));
				break;
			}
		}
		else
		{
			gsSolidOp.solid2D.DstX = psRect->x0;
			gsSolidOp.solid2D.DstY = psRect->y0;
			gsSolidOp.solid2D.DSizeX = psRect->x1 - psRect->x0;
			gsSolidOp.solid2D.DSizeY = psRect->y1 - psRect->y0;

			iErr = PVR2DBlt(pPVR->srv->hPVR2DContext,
					&gsSolidOp.solid2D);

			if (iErr != PVR2D_OK)
			{
				ERROR_MSG("%s: PVR2DBlt failed with error code: %d (%s)",
				       __func__, iErr,
					  sgxErrorCodeToString(iErr));
				break;
			}
		}
	}

	gsSolidOp.numBltRects = 0;
	gsSolidOp.bltRectsIdx = 0;

	if (flush) {
		if (gsSolidOp.softFallback.psGC) {
			FreeScratchGC(gsSolidOp.softFallback.psGC);
		}
	} else {
		gsSolidOp.numBltRects = 0;
		gsSolidOp.bltRectsIdx = 0;
		gsSolidOp.averageArea = 0;
		gsSolidOp.maxArea = 0;
		gsSolidOp.minArea = gsSolidOp.pPixmap->drawable.height *
				    gsSolidOp.pPixmap->drawable.width;
		pvrInitBoundingBox(&gsSolidOp.destBoundBox,
				   &gsSolidOp.destSurfaceBox,
				   gsSolidOp.pPixmap->drawable.width,
				   gsSolidOp.pPixmap->drawable.height);
	}
}

static void
sgxSolid(PixmapPtr pPixmap, int x1, int y1, int x2, int y2)
{
	ScrnInfoPtr pScrn = pix2scrn(pPixmap);
	PVRPtr pPVR = PVREXAPTR(pScrn);
	unsigned int area = area = (y2 - y1) * (x2 - x1);
	SGXHW_RENDER_RECTS *destRect =
			&gsSolidOp.destRect[gsSolidOp.bltRectsIdx];

	PVR_ASSERT(gsSolidOp.pPixmap == pPixmap);

	if (area > gsSolidOp.maxArea)
		gsSolidOp.maxArea = area;

	if (area < gsSolidOp.minArea)
		gsSolidOp.minArea = area;

	if (gsSolidOp.averageArea)
		gsSolidOp.averageArea = (gsSolidOp.averageArea + area) >> 1;
	else
		gsSolidOp.averageArea = area;

	destRect->x0 = x1;
	destRect->x1 = x2;
	destRect->y0 = y1;
	destRect->y1 = y2;
	pvrAddRectToBoundingBox(&gsSolidOp.destBoundBox, destRect,
				&gsSolidOp.destSurfaceBox);
	gsSolidOp.numBltRects++;
	sgxSolidNextBatch(pScrn, pPVR, FALSE);
}

static void
sgxDoneSolid(PixmapPtr pPixmap)
{
	ScrnInfoPtr pScrn = pix2scrn(pPixmap);
	PVRPtr pPVR = PVREXAPTR(pScrn);

	PVR_ASSERT(gsSolidOp.pPixmap == pPixmap);

	sgxSolidNextBatch(pScrn, pPVR, TRUE);

	setPixmapOnGPU(pPixmap);
	flushScanout(pPixmap);

	gsSolidOp.softFallback.psGC = NULL;
	gsSolidOp.pPixmap = NULL;
}

static PVR2DFORMAT
convertBitsPerPixelToPVR2DFormat(int bitsPerPixel)
{
	switch (bitsPerPixel)
	{
		case 1:
			return PVR2D_1BPP;
		case 8:
			return PVR2D_ALPHA8;
		case 16:
			return PVR2D_RGB565;
		case 24:
			return PVR2D_RGB888;
		case 32:
			return PVR2D_ARGB8888;
		default:
			return  -1;
	}
}

static Bool
sgxPrepareCopy(PixmapPtr pSrc, PixmapPtr pDst, int xdir, int ydir, int alu,
	       Pixel planemask)
{
	int bitsPerPixel = pSrc->drawable.bitsPerPixel;
	OMAPPixmapPrivPtr dstPriv;
	OMAPPixmapPrivPtr srcPriv;
	PrivPixmapPtr pvrDstPriv;
	PrivPixmapPtr pvrSrcPriv;

	if (planemask != -1)
		return FALSE;

	if (bitsPerPixel != 8 && bitsPerPixel != 16 && bitsPerPixel != 32) {
		return FALSE;
	}

	memset(&gsCopy2DOp, 0, sizeof(gsCopy2DOp));

	dstPriv = exaGetPixmapDriverPrivate(pDst);
	pvrDstPriv = sgxMapPixmapBo(pDst->drawable.pScreen, dstPriv);
	srcPriv = exaGetPixmapDriverPrivate(pSrc);
	pvrSrcPriv = sgxMapPixmapBo(pSrc->drawable.pScreen, srcPriv);

	/* fallback to software instead of crashing */
	if (!pvrDstPriv || !pvrSrcPriv)
		return FALSE;

	if (copy2d(bitsPerPixel)) {
		gsCopy2DOp.blt2D.ColourKey = 0;
		gsCopy2DOp.blt2D.CopyCode = sgxRop[0][alu];

		gsCopy2DOp.blt2D.SrcFormat =
				convertBitsPerPixelToPVR2DFormat(
					pSrc->drawable.bitsPerPixel);
		gsCopy2DOp.blt2D.pSrcMemInfo = &pvrSrcPriv->meminfo;
		gsCopy2DOp.blt2D.SrcStride = exaGetPixmapPitch(pSrc);
		gsCopy2DOp.blt2D.SrcSurfWidth = pSrc->drawable.width;
		gsCopy2DOp.blt2D.SrcSurfHeight = pSrc->drawable.height;

		gsCopy2DOp.blt2D.DstFormat =
				convertBitsPerPixelToPVR2DFormat(
					pDst->drawable.bitsPerPixel);
		gsCopy2DOp.blt2D.pDstMemInfo = &pvrDstPriv->meminfo;
		gsCopy2DOp.blt2D.DstStride = exaGetPixmapPitch(pDst);
		gsCopy2DOp.blt2D.DstSurfWidth = pDst->drawable.width;
		gsCopy2DOp.blt2D.DstSurfHeight = pDst->drawable.height;

		gsCopy2DOp.blt2D.pMaskMemInfo = NULL;
		gsCopy2DOp.blt2D.BlitFlags = PVR2D_BLIT_DISABLE_ALL;
	} else {
		gsCopy2DOp.blt3D.sSrc.Format =
				convertBitsPerPixelToPVR2DFormat(
					pSrc->drawable.bitsPerPixel);
		gsCopy2DOp.blt3D.sSrc.pSurfMemInfo = &pvrSrcPriv->meminfo;
		gsCopy2DOp.blt3D.sSrc.SurfWidth = pSrc->drawable.width;
		gsCopy2DOp.blt3D.sSrc.SurfHeight = pSrc->drawable.height;
		gsCopy2DOp.blt3D.sSrc.Stride = exaGetPixmapPitch(pSrc);

		gsCopy2DOp.blt3D.sDst.pSurfMemInfo = &pvrDstPriv->meminfo;
		gsCopy2DOp.blt3D.sDst.SurfWidth = pDst->drawable.width;
		gsCopy2DOp.blt3D.sDst.SurfHeight = pDst->drawable.height;
		gsCopy2DOp.blt3D.sDst.Stride = exaGetPixmapPitch(pDst);
		gsCopy2DOp.blt3D.sDst.Format =
				convertBitsPerPixelToPVR2DFormat(
					pDst->drawable.bitsPerPixel);

		gsCopy2DOp.blt3D.UseParams[0] = 0;
		gsCopy2DOp.blt3D.UseParams[1] = 0;
		gsCopy2DOp.blt3D.hUseCode = PVRGetUseCodeCopyAlphaFill();
	}

	gsCopy2DOp.renderOp.pSrc = pSrc;
	gsCopy2DOp.renderOp.pDest = pDst;
	gsCopy2DOp.renderOp.handleDestDamage = 0;

	sgxCompositeResetCoordinates(&gsCopy2DOp.renderOp);

	return TRUE;
}

static void
sgxCopyNextBatch(ScreenPtr pScreen, Bool flush)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	PVRPtr pPVR = PVREXAPTR(pScrn);
	int i;
	int bitsPerPixel;

	if (flush) {
		if (!gsCopy2DOp.renderOp.numBltRects) {
			return;
		}
	} else if (gsCopy2DOp.renderOp.numBltRects < MAX_COPY_BATCH_RECTS) {
		gsCopy2DOp.renderOp.bltRectsIdx++;
		return;
	}

	if (!sgxCompositeValidateBoundingBox(&gsCopy2DOp.renderOp)) {
		return;
	}

	bitsPerPixel= gsCopy2DOp.renderOp.pSrc->drawable.bitsPerPixel;

	for (i = 0; i < gsCopy2DOp.renderOp.numBltRects; i++)
	{
		SGXHW_RENDER_RECTS *psSrcRect =
				&gsCopy2DOp.renderOp.bltRects.srcRect[i];
		SGXHW_RENDER_RECTS *psDstRect =
				&gsCopy2DOp.renderOp.bltRects.destRect[i];
		PVR2DERROR iErr;

		if (copy2d(bitsPerPixel)) {
			gsCopy2DOp.blt2D.SrcX = psSrcRect->x0;
			gsCopy2DOp.blt2D.SrcY = psSrcRect->y0;
			gsCopy2DOp.blt2D.SizeX =
					psSrcRect->x1 - psSrcRect->x0;
			gsCopy2DOp.blt2D.SizeY =
					psSrcRect->y1 - psSrcRect->y0;

			gsCopy2DOp.blt2D.DstX = psDstRect->x0;
			gsCopy2DOp.blt2D.DstY = psDstRect->y0;
			gsCopy2DOp.blt2D.DSizeX =
					psDstRect->x1 - psDstRect->x0;
			gsCopy2DOp.blt2D.DSizeY =
					psDstRect->y1 - psDstRect->y0;

			iErr = PVR2DBlt(pPVR->srv->hPVR2DContext,
					&gsCopy2DOp.blt2D);

			if (iErr != PVR2D_OK)
			{
				ERROR_MSG("%s: PVR2DBlt failed with error code: %d (%s)",
					  __func__, iErr,
					  sgxErrorCodeToString(iErr));
				break;
			}

		} else {
			gsCopy2DOp.blt3D.rcSource.left = psSrcRect->x0;
			gsCopy2DOp.blt3D.rcSource.top = psSrcRect->y0;
			gsCopy2DOp.blt3D.rcSource.right = psSrcRect->x1;
			gsCopy2DOp.blt3D.rcSource.bottom = psSrcRect->y1;

			gsCopy2DOp.blt3D.rcDest.left = psDstRect->x0;
			gsCopy2DOp.blt3D.rcDest.top = psDstRect->y0;
			gsCopy2DOp.blt3D.rcDest.right = psDstRect->x1;
			gsCopy2DOp.blt3D.rcDest.bottom = psDstRect->y1;

			iErr = PVR2DBlt3D(pPVR->srv->hPVR2DContext,
					  &gsCopy2DOp.blt3D);

			if (iErr != PVR2D_OK)
			{
				ERROR_MSG("%s: PVR3DBltExt failed with error code: %d (%s)",
					  __func__, iErr,
					  sgxErrorCodeToString(iErr));
				break;
			}
		}
	}

	sgxCompositeResetCoordinates(&gsCopy2DOp.renderOp);
}

static void
sgxCopy(PixmapPtr pDstPixmap, int srcX, int srcY, int dstX, int dstY,
	int width, int height)
{
	int w = gsCopy2DOp.renderOp.pDest->drawable.width;
	int h = gsCopy2DOp.renderOp.pDest->drawable.height;
	SGXHW_RENDER_RECTS *destRect;
	SGXHW_RENDER_RECTS *srcRect;
	int bltRectsIdx = gsCopy2DOp.renderOp.bltRectsIdx;
	PPVRBltRect rects;

	if (dstX >= w || dstY >= h)
		return;

	if (dstX < 0)
	{
		width += dstX;
		srcX -= dstX;
		dstX = 0;
	}

	if (dstY < 0)
	{
		srcY -= dstY;
		height += dstY;
		dstY = 0;
	}

	w -= dstX;

	if (w >= width)
		w = width;

	h -= dstY;

	if (h >= height)
		h = height;

	if (w <= 0 || h <= 0)
		return;

	rects = &gsCopy2DOp.renderOp.bltRects;

	destRect = &rects->destRect[bltRectsIdx];
	srcRect = &rects->srcRect[bltRectsIdx ];

	destRect->x0 = dstX;
	destRect->y0 = dstY;
	destRect->x1 = dstX + w;
	destRect->y1 = dstY + h;

	pvrAddRectToBoundingBox(&rects->destBoundBox,
				&rects->destRect[bltRectsIdx],
				&rects->destSurfaceBox);

	srcRect->x0 = srcX;
	srcRect->y0 = srcY;
	srcRect->x1 = srcX + w;
	srcRect->y1 = srcY + h;

	pvrAddRectToBoundingBox(&rects->srcBoundBox,
				&rects->srcRect[bltRectsIdx],
				&rects->srcSurfaceBox);

	gsCopy2DOp.renderOp.numBltRects++;
	sgxCopyNextBatch(pDstPixmap->drawable.pScreen, FALSE);
}

static void
sgxDoneCopy(PixmapPtr pPixmap)
{
	PVR_ASSERT(gsCopy2DOp.renderOp.pDest == pPixmap);

	sgxCopyNextBatch(pPixmap->drawable.pScreen, TRUE);

	setPixmapOnGPU(pPixmap);
	flushScanout(pPixmap);

	gsCopy2DOp.renderOp.pSrc = NULL;
	gsCopy2DOp.renderOp.pDest = NULL;
}

static RENDER_TRANSFORMATION
PVRGetTransformation(PictTransformPtr psTransform)
{
	double m00;
	double m01;
	double m10;
	double m11;

	if (!psTransform)
		return TRANSF_NONE;

	m00 = (double)psTransform->matrix[0][0] / 65536.0;
	m01 = (double)psTransform->matrix[0][1] / 65536.0;
	m10 = (double)psTransform->matrix[1][0] / 65536.0;
	m11 = (double)psTransform->matrix[1][1] / 65536.0;

	if (m00 > 0.0 && m11 > 0.0 && m01 == 0.0 && m10 == 0.0)
		return TRANSF_NONE;

	if (m01 > 0.0 && m10 < 0.0 && m00 == 0.0 && m11 == 0.0)
		return TRANSF_ROT_90;

	if (m00 >= 0.0 || m11 >= 0.0 || m01 != 0.0 || m10 != 0.0) {
		if (m01 >= 0.0 || m10 <= 0.0 || m00 != 0.0 || m11 != 0.0) {
			if (pixman_transform_is_scale(psTransform))
				return TRANSF_SCALE;
			else
				return TRANSF_UNKNOWN;
		}
		else
			return TRANSF_ROT_270;
	}

	return TRANSF_ROT_180;
}

static PVR2D_HANDLE
PVRRenderIsAccelerated(ScreenPtr pScreen, PVRRenderOp *pRender)
{
	PicturePtr pDestPicture = pRender->pDestPicture;
	PicturePtr pSrcPicture = pRender->pSrcPicture;
	PicturePtr pMaskPicture = pRender->pMaskPicture;
	PixmapPtr pDest = pRender->pDest;
	PixmapPtr pSrc = pRender->pSrc;
	PixmapPtr pMask = pRender->pMask;
	unsigned int destFormat;
	unsigned int maskFormat;
	unsigned int srcFormat;

	if (!pDestPicture || !pDest)
		return NULL;

	if (pSrcPicture && !pSrc)
		return NULL;

	if (pMaskPicture && !pMask)
		return NULL;

	destFormat = pDestPicture->format;

	if (destFormat != PICT_a8r8g8b8 && destFormat != PICT_a8b8g8r8 &&
	    destFormat != PICT_x8r8g8b8 && destFormat != PICT_x8b8g8r8 &&
	    destFormat != PICT_a8) {
		return NULL;
	}

	if (pSrcPicture->repeatType &&
	    pSrc->drawable.width != 1 &&
	    pSrc->drawable.height != 1) {
		return NULL;
	}

	if (pMask) {
		if (pMaskPicture->repeatType &&
		    pMask->drawable.width != 1 &&
		    pMask->drawable.height != 1) {
			return NULL;
		}
	}

	if (pSrcPicture) {
		srcFormat = pSrcPicture->format;

		if (srcFormat != PICT_a8r8g8b8 && srcFormat != PICT_a8b8g8r8 &&
		    srcFormat != PICT_x8r8g8b8 && srcFormat != PICT_x8b8g8r8 &&
		    srcFormat != PICT_a8) {
			return NULL;
		}

		if (pSrcPicture->transform) {
			pRender->transform = PVRGetTransformation(
						     pSrcPicture->transform);

			switch (pRender->transform) {
				case TRANSF_UNKNOWN:
					return NULL;
				case TRANSF_ROT_90:
				case TRANSF_ROT_180:
				case TRANSF_ROT_270:
					if (!pMaskPicture &&
					    pRender->op == PictOpSrc)
						return PVRGetUseCodeForRender(pRender);

					return NULL;
				default:
					break;
			}
		}
	}

	if (!pMaskPicture)
		return PVRGetUseCodeForRender(pRender);

	if (pMaskPicture->transform &&
	    PVRGetTransformation(pMaskPicture->transform)) {
		return NULL;
	}

	maskFormat = pMaskPicture->format;

	if (maskFormat != PICT_a8r8g8b8 && maskFormat != PICT_a8b8g8r8 &&
	    maskFormat != PICT_x8r8g8b8 && maskFormat != PICT_x8b8g8r8 &&
	    maskFormat != PICT_a8) {
		return NULL;
	}

	return PVRGetUseCodeForRender(pRender);
}

static Bool
sgxCheckComposite(int op, PicturePtr pSrcPicture, PicturePtr pMaskPicture,
		  PicturePtr pDstPicture)
{
#ifndef USE_HW_COMPOSITING
	if (1) {
#else
	if (0) {
#endif
		sgxWaitPixmap(gsRenderOp.pSrc);
		sgxWaitPixmap(gsRenderOp.pDest);
		sgxWaitPixmap(gsRenderOp.pMask);

		return FALSE;
	}

	memset(&gsRenderOp, 0, sizeof(gsRenderOp));

	gsRenderOp.op = op;

	gsRenderOp.pDestPicture = pDstPicture;
	gsRenderOp.pSrcPicture = pSrcPicture;
	gsRenderOp.pMaskPicture = pMaskPicture;

	gsRenderOp.pDest = draw2pix(pDstPicture->pDrawable);

	if (pSrcPicture)
		gsRenderOp.pSrc = draw2pix(pSrcPicture->pDrawable);
	else
		gsRenderOp.pSrc = NULL;

	if (pMaskPicture)
		gsRenderOp.pMask = draw2pix(pMaskPicture->pDrawable);
	else
		gsRenderOp.pMask = NULL;

	gsRenderOp.destForceAlpha = FALSE;
	gsRenderOp.srcForceAlpha = FALSE;

	gsRenderOp.hCode = PVRRenderIsAccelerated(
				   pDstPicture->pDrawable->pScreen,
				   &gsRenderOp);

	if (pSrcPicture)
		gsRenderOp.srcRepeatType = pSrcPicture->repeatType;
	else
		gsRenderOp.srcRepeatType = SGXTQ_RENDER_RECTS_REPEAT_NONE;

	if (pMaskPicture)
		gsRenderOp.maskRepeatType = pMaskPicture->repeatType;
	else
		gsRenderOp.maskRepeatType = SGXTQ_RENDER_RECTS_REPEAT_NONE;

	if (gsRenderOp.hCode) {
		gsRenderOp.handleDestDamage = FALSE;
		sgxCompositeResetCoordinates(&gsRenderOp);
	}

	if (!gsRenderOp.hCode) {
		sgxWaitPixmap(gsRenderOp.pSrc);
		sgxWaitPixmap(gsRenderOp.pDest);
		sgxWaitPixmap(gsRenderOp.pMask);

		return FALSE;
	}

	return TRUE;
}

static Bool
sgxPrepareComposite(int op, PicturePtr pSrcPicture, PicturePtr pMaskPicture,
		     PicturePtr pDstPicture, PixmapPtr pSrc, PixmapPtr pMask,
		     PixmapPtr pDst)
{
	return TRUE;
}

static void
PVRApplyTransformation(PictTransformPtr psTransform, int *iStartX, int *iStartY,
		       int *iWidth, int *iHeight)
{
	PictVector v;
	int iSrcX1;
	int iSrcY1;
	int iSrcX2;
	int iSrcY2;
	int iRealStartX;
	int iRealStartY;
	int iRealEndX;
	int iRealEndY;

	v.vector[0] = IntToxFixed(*iStartX);
	v.vector[1] = IntToxFixed(*iStartY);
	v.vector[2] = xFixed1;
	PictureTransformPoint(psTransform, &v);
	iSrcX1 = xFixedToInt(v.vector[0]);
	iSrcY1 = xFixedToInt(v.vector[1]);

	v.vector[0] = IntToxFixed(*iStartX + *iWidth);
	v.vector[1] = IntToxFixed(*iStartY + *iHeight);
	v.vector[2] = xFixed1;
	PictureTransformPoint(psTransform, &v);
	iSrcX2 = xFixedToInt(v.vector[0]);
	iSrcY2 = xFixedToInt(v.vector[1]);

	iRealStartX = iSrcX2 < iSrcX1 ? iSrcX2 : iSrcX1;
	iRealStartY = iSrcY2 < iSrcY1 ? iSrcY2 : iSrcY1;
	iRealEndX = iSrcX2 >= iSrcX1 ? iSrcX2 : iSrcX1;
	iRealEndY = iSrcY2 >= iSrcY1 ? iSrcY2 : iSrcY1;

	*iStartX = iRealStartX;
	*iStartY = iRealStartY;
	*iWidth = iRealEndX - iRealStartX;
	*iHeight = iRealEndY - iRealStartY;
}

static Bool
sgxCompositeNextBatch(ScreenPtr pScreen, IMG_BOOL lastBatch)
{
	Bool rv;

	if (lastBatch) {
		if (!gsRenderOp.numBltRects)
			return FALSE;
	} else if (gsRenderOp.numBltRects < MAX_COPY_BATCH_RECTS) {
		gsRenderOp.bltRectsIdx++;
		return TRUE;
	}

	if ((rv = sgxCompositeValidateBoundingBox(&gsRenderOp))) {
		rv = PVRRender(pScreen, &gsRenderOp);
		sgxCompositeResetCoordinates(&gsRenderOp);
	}

	return rv;
}

inline static Bool
clamp(int *val, int low, int high)
{
	int tmp = ((*val) > (high)) ?
			  (high) : (((*val) < (low)) ? (low) : (*val));
	Bool clamped = tmp != *val;

	*val = tmp;

	return clamped;
}

static void
sgxComposite(PixmapPtr pDstPixmap, int srcX, int srcY, int maskX, int maskY,
	     int dstX, int dstY, int width, int height)
{
	PicturePtr pDestPicture = gsRenderOp.pDestPicture;
	PicturePtr pMaskPicture = gsRenderOp.pMaskPicture;
	PicturePtr pSrcPicture = gsRenderOp.pSrcPicture;
	Bool badMask = FALSE;
	int iRealMaskWidth = width;
	int iRealDestWidth = width;
	int iRealSrcWidth = width;
	int iRealMaskHeight = height;
	int iRealDestHeight = height;
	int iRealSrcHeight = height;
	IMG_INT bltRectsIdx = gsRenderOp.bltRectsIdx;
	ScreenPtr pScreen = pDstPixmap->drawable.pScreen;
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	SGXHW_RENDER_RECTS *destRect;
	SGXHW_RENDER_RECTS *srcRect;

	if (pMaskPicture)
		badMask = pMaskPicture->transform != NULL;

	if (pSrcPicture->transform) {
		PVRApplyTransformation(pSrcPicture->transform, &srcX, &srcY,
				       &iRealSrcWidth, &iRealSrcHeight);
	}

	if (pSrcPicture->repeatType != RepeatNone) {
		srcX = 0;
		srcY = 0;
		iRealSrcWidth = 1;
		iRealSrcHeight = 1;
	} else {
		int h = pSrcPicture->pDrawable->height;
		int w = pSrcPicture->pDrawable->width;

		clamp(&srcX, 0, w - 1);
		clamp(&srcY, 0, h - 1);
		clamp(&iRealSrcWidth, 0, w - srcX);
		clamp(&iRealSrcHeight, 0, h - srcY);
	}

	if (pMaskPicture) {
		if (pMaskPicture->transform) {
			PVRApplyTransformation(pMaskPicture->transform,
					       &maskX, &maskY,
					       &iRealMaskWidth, &iRealMaskHeight);
		}

		if (pMaskPicture->repeatType != RepeatNone) {
			maskX = 0;
			maskY = 0;
			iRealMaskWidth = 1;
			iRealMaskHeight = 1;
		} else {
			int h = pMaskPicture->pDrawable->height;
			int w = pMaskPicture->pDrawable->width;

			badMask |= clamp(&maskX, 0, w - 1);
			badMask |= clamp(&maskY, 0, h - 1);
			badMask |= clamp(&iRealMaskWidth, 0, w - maskX);
			badMask |= clamp(&iRealMaskHeight, 0, h - maskY);
		}
	}

	if (pDestPicture->transform) {
		PVRApplyTransformation(pDestPicture->transform, &dstX, &dstY,
				       &iRealDestWidth, &iRealDestHeight);
	}

	if (pDestPicture->repeatType != RepeatNone) {
		dstX = 0;
		dstY = 0;
		iRealDestWidth = 1;
		iRealDestHeight = 1;
	} else {
		int h = pDestPicture->pDrawable->height;
		int w = pDestPicture->pDrawable->width;

		clamp(&dstX, 0, w - 1);
		clamp(&dstY, 0, h - 1);
		clamp(&iRealDestWidth, 0, w - dstX);
		clamp(&iRealDestHeight, 0, h - dstY);
	}

	if (badMask) {
		ERROR_MSG("Composite failed: coordinate values out of range");
		return;
	}

	destRect = &gsRenderOp.bltRects.destRect[bltRectsIdx];
	destRect->x0 = dstX;
	destRect->x1 = dstX + iRealDestWidth;
	destRect->y0 = dstY;
	destRect->y1 = dstY + iRealDestHeight;
	pvrAddRectToBoundingBox(&gsRenderOp.bltRects.destBoundBox, destRect,
				&gsRenderOp.bltRects.destSurfaceBox);

	srcRect = &gsRenderOp.bltRects.srcRect[bltRectsIdx];
	srcRect->x0 = srcX;
	srcRect->x1 = srcX + iRealSrcWidth;
	srcRect->y0 = srcY;
	srcRect->y1 = srcY + iRealSrcHeight;
	pvrAddRectToBoundingBox(&gsRenderOp.bltRects.srcBoundBox, srcRect,
				&gsRenderOp.bltRects.srcSurfaceBox);

	if (gsRenderOp.pMask) {
		SGXHW_RENDER_RECTS *maskRect =
				&gsRenderOp.bltRects.maskRect[bltRectsIdx];

		maskRect->x0 = maskX;
		maskRect->x1 = maskX + iRealMaskWidth;
		maskRect->y0 = maskY;
		maskRect->y1 = maskY + iRealMaskHeight;
		pvrAddRectToBoundingBox(&gsRenderOp.bltRects.maskBoundBox,
					maskRect,
					&gsRenderOp.bltRects.maskSurfaceBox);
	}

	gsRenderOp.numBltRects++;

	if (!sgxCompositeNextBatch(pScreen, FALSE))
		ERROR_MSG("sgxComposite: GPU failed to perform composite operation!!!");
}

static void
sgxDoneComposite(PixmapPtr pDst)
{
	sgxCompositeNextBatch(pDst->drawable.pScreen, TRUE);

	setPixmapOnGPU(pDst);
	flushScanout(pDst);

	gsRenderOp.pDest = NULL;
	gsRenderOp.hCode = NULL;
}

static Bool
PVRInitServices(ScrnInfoPtr pScrn)
{
	void *hLibSrvInit;
	Bool bStatus;
	int (*SrvInit)(void);
	PVRSRV_ERROR eError = -1;

	if (PVRDRMServicesInitStatus(&bStatus)) {
		WARNING_MSG("PVRDRMServicesInitStatus failed %d", errno);
		return FALSE;
	}

	if (bStatus)
		return TRUE;

	hLibSrvInit = dlopen("libsrv_init.so", RTLD_NOW);

	if (!hLibSrvInit) {
		WARNING_MSG("Couldn't load PVR Services initialisation library: %s",
			    dlerror());
		return FALSE;
	}

	SrvInit = dlsym(hLibSrvInit, "SrvInit");

	if (!SrvInit) {
		WARNING_MSG("Couldn't find PVR Services initialisation entry point %s",
			    dlerror());
	} else {
		eError = SrvInit();
	}

	dlclose(hLibSrvInit);

	if (eError != PVRSRV_OK) {
		return FALSE;
	}

	if (PVRDRMServicesInitStatus(&bStatus)) {
		WARNING_MSG("PVRDRMServicesInitStatus failed %d",
			    errno);
		return FALSE;
	}

	if (bStatus) {
		return TRUE;
	}

	return FALSE;
}

static Bool
sgxAccelInit(ScreenPtr pScreen, PVRPtr pPVR, int fd)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);

	sgxCacheInit(pPVR);
	PVRInitServices(pScrn);

	if (!InitialiseServices(pScreen, &pPVR->srv)) {
		ERROR_MSG("InitialiseServices failed");
		return FALSE;
	}

	if (!PVRUseInit(pScreen, pPVR))
		return FALSE;

	return TRUE;

}

static unsigned int
GetFormats(unsigned int *formats)
{
	int i = 0;

	/*formats[i++] = fourcc_code('N', 'V', '1', '2');*/
	formats[i++] = fourcc_code('Y', 'V', '1', '2');
	formats[i++] = fourcc_code('I', '4', '2', '0');
	formats[i++] = fourcc_code('U', 'Y', 'V', 'Y');
	formats[i++] = fourcc_code('Y', 'U', 'Y', 'V');
	formats[i++] = fourcc_code('Y', 'U', 'Y', '2');

	return i;
}

static Bool
sgxPrepareAccess(PixmapPtr pPixmap, int index)
{
	OMAPPixmapPrivPtr priv = exaGetPixmapDriverPrivate(pPixmap);

	pPixmap->devPrivate.ptr = omap_bo_map(priv->bo);

	if (!pPixmap->devPrivate.ptr)
		return FALSE;

	sgxWaitPixmap(pPixmap);

	return TRUE;
}

_X_EXPORT OMAPEXAPtr
InitPowerVREXA(ScreenPtr pScreen, ScrnInfoPtr pScrn, int fd)
{
	OMAPPtr pOMAP = OMAPPTR(pScrn);
	PVRPtr pPVR = calloc(sizeof(*pPVR), 1);
	OMAPEXAPtr omap_exa = (OMAPEXAPtr)pPVR;
	ExaDriverPtr exa;
	int maxSize;

	if (!sgxAccelInit(pScreen, pPVR, fd)) {
		goto fail;
	}

	INFO_MSG("PVR EXA mode\n");

	exa = exaDriverAlloc();

	if (!exa) {
		goto fail;
	}

	pPVR->exa = exa;
	exa->exa_major = EXA_VERSION_MAJOR;
	exa->exa_minor = EXA_VERSION_MINOR;

	exa->pixmapOffsetAlign = 0;
	exa->pixmapPitchAlign = 32 * 4;

	exa->flags = EXA_OFFSCREEN_PIXMAPS |
		     EXA_HANDLES_PIXMAPS | EXA_SUPPORTS_PREPARE_AUX;

	if (pOMAP->chipset <= 0x446F)
		maxSize = 2048;
	else
		maxSize = 4096;

	exa->maxX = maxSize;
	exa->maxY = maxSize;

	exa->WaitMarker = OMAPWaitMarker;
	exa->CreatePixmap2 = OMAPCreatePixmap;
	exa->DestroyPixmap = sgxDestroyPixmap;
	exa->ModifyPixmapHeader = sgxModifyPixmapHeader;
	exa->PrepareAccess = sgxPrepareAccess;
	exa->FinishAccess = OMAPFinishAccess;
	exa->PixmapIsOffscreen = OMAPPixmapIsOffscreen;
	exa->PrepareSolid = sgxPrepareSolid;
	exa->Solid = sgxSolid;
	exa->DoneSolid = sgxDoneSolid;

	exa->PrepareCopy = sgxPrepareCopy;
	exa->Copy = sgxCopy;
	exa->DoneCopy = sgxDoneCopy;

	exa->CheckComposite = sgxCheckComposite;
	exa->PrepareComposite = sgxPrepareComposite;
	exa->Composite = sgxComposite;
	exa->DoneComposite = sgxDoneComposite;

	if (!exaDriverInit(pScreen, exa)) {
		ERROR_MSG("exaDriverInit failed");
		goto fail;
	}

	omap_exa->GetFormats = GetFormats;
	omap_exa->PutTextureImage = PUT_TEXTURE_IMAGE_FN;
	omap_exa->CloseScreen = CloseScreen;
	omap_exa->FreeScreen = FreeScreen;

	return omap_exa;

fail:
	if (pPVR) {
		free(pPVR);
	}

	return NULL;
}

/** Provide basic version information to the XFree86 code. */
static XF86ModuleVersionInfo OMAPVersRec =
{
		"omap_pvr",
		MODULEVENDORSTRING,
		MODINFOSTRING1,
		MODINFOSTRING2,
		XORG_VERSION_CURRENT,
		OMAP_MAJOR_VERSION, OMAP_MINOR_VERSION, OMAP_PATCHLEVEL,
		ABI_CLASS_VIDEODRV,
		ABI_VIDEODRV_VERSION,
		MOD_CLASS_VIDEODRV,
		{0, 0, 0, 0}
};

static pointer
PVRSetup(pointer module, pointer opts, int *errmaj, int *errmin)
{
	return (pointer)1;
}

_X_EXPORT XF86ModuleData omap_pvrModuleData = {&OMAPVersRec, PVRSetup, NULL};
