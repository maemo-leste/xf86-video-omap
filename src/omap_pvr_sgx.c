/*
 * omap_pvr_sgx.c
 *
 * Copyright (C) 2022 Ivaylo Dimitrov <ivo.g.dimitrov.75@gmail.com>
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

#include "omap_pvr_sgx_def.h"
#include "omap_pvr_sgx.h"
#include "omap_exa_pvr.h"
#include "omap_pvr_use.h"

static PPVRSRV_CLIENT_MEM_INFO
pixmap2meminfo(PixmapPtr pPixmap)
{
	OMAPPixmapPrivPtr pixmapPriv = exaGetPixmapDriverPrivate(pPixmap);
	PrivPixmapPtr pvrPixmapPriv;

	pvrPixmapPriv = sgxMapPixmapBo(pPixmap->drawable.pScreen, pixmapPriv);

	if (!pvrPixmapPriv)
		return NULL;

	return pvrPixmapPriv->meminfo.hPrivateData;
}

static void
setsurf(SGXTQ_SURFACE *surf, PixmapPtr pPixmap, PVRSRV_PIXEL_FORMAT fmt)
{
	PPVRSRV_CLIENT_MEM_INFO psClientMemInfo = pixmap2meminfo(pPixmap);

	if (!psClientMemInfo)
		return;

	surf->ui32Height = pPixmap->drawable.height;
	surf->ui32Width = pPixmap->drawable.width;
	surf->eFormat = fmt;
	surf->psSyncInfo = psClientMemInfo->psClientSyncInfo;
	surf->sDevVAddr.uiAddr = psClientMemInfo->sDevVAddr.uiAddr;
	surf->i32StrideInBytes = exaGetPixmapPitch(pPixmap);
}

static void
setsurf_addr(SGXTQ_SURFACE *surf, PixmapPtr pPixmap)
{
	PPVRSRV_CLIENT_MEM_INFO psClientMemInfo = pixmap2meminfo(pPixmap);

	if (!psClientMemInfo)
		return;

	surf->psSyncInfo = psClientMemInfo->psClientSyncInfo;
	surf->sDevVAddr.uiAddr = psClientMemInfo->sDevVAddr.uiAddr;
}

#if defined SGX_OMAP_443x
PUT_TEXTURE_IMAGE_FN_DEF(443x)
#elif defined SGX_OMAP_343x
PUT_TEXTURE_IMAGE_FN_DEF(343x)
#else
#error "Unknown omap variant"
#endif
{
	static SGX_QUEUETRANSFER sBlitInfo = {
		.eType = SGXTQ_VIDEO_BLIT,
		.ui32Flags = 0x50000,
		.asSources[0].eMemLayout = SGXTQ_MEMLAYOUT_STRIDE,
		.asSources[1].eMemLayout = SGXTQ_MEMLAYOUT_STRIDE,
		.asSources[2].eMemLayout = SGXTQ_MEMLAYOUT_STRIDE,
		.ui32NumDest = 1,
		.asDests[0].eMemLayout = SGXTQ_MEMLAYOUT_STRIDE,
		.ui32NumSrcRects = 1,
		.ui32NumDestRects = 1
	};
	ScrnInfoPtr pScrn = pix2scrn(pDstPix);
	PVRPtr pPVR = PVREXAPTR(pScrn);
	int bitsPerPixel = pDstPix->drawable.bitsPerPixel;
	PVR2DCONTEXT *pContext;
	PVRSRV_PIXEL_FORMAT fmt;
	PVRSRV_ERROR err;

	int i;
	int dst_h;
	int dst_w;
	int src_w;
	int src_x1;
	int src_y1;
	int src_h;
	int dst_x1;
	int dst_y1;

	if (bitsPerPixel != 32 && bitsPerPixel != 16 && bitsPerPixel != 8)
		return FALSE;

	pContext = (PVR2DCONTEXT *)pPVR->srv->hPVR2DContext;

	if (bitsPerPixel == 8)
		fmt = PVRSRV_PIXEL_FORMAT_A8;
	else if (bitsPerPixel == 16)
		fmt = PVRSRV_PIXEL_FORMAT_RGB565;
	else
		fmt = PVRSRV_PIXEL_FORMAT_XRGB8888;

	setsurf(&sBlitInfo.asDests[0], pDstPix, fmt);

	dst_x1 = pDstBox->x1;
	dst_y1 = pDstBox->y1;
	dst_h = pDstPix->drawable.height - 1;
	dst_w = pDstPix->drawable.width - 1;

	if (dst_w < dst_x1)
		dst_x1 = dst_w;

	if (dst_h < dst_y1)
		dst_y1 = dst_h;

	if (dst_w > pDstBox->x2)
		dst_w = pDstBox->x2;

	if (dst_h >= pDstBox->y2)
		dst_h = pDstBox->y2;

	src_x1 = pSrcBox->x1;
	src_y1 = pSrcBox->y1;
	src_w = pSrcPix->drawable.width - 1;
	src_h = pSrcPix->drawable.height - 1;

	if (src_w < src_x1)
		src_x1 = src_w;

	if (src_w >= pSrcBox->x2)
		src_w = pSrcBox->x2;

	if (src_h < src_y1)
		src_y1 = src_h;

	if ( src_h >= pSrcBox->y2 )
		src_h = pSrcBox->y2;

#define POSITIVE(x) ((x) > 0 ? x : 0)
	sBlitInfo.asDestRects[0].x0 = POSITIVE(dst_x1);
	sBlitInfo.asDestRects[0].y0 = POSITIVE(dst_y1);
	sBlitInfo.asDestRects[0].x1 = POSITIVE(dst_w);
	sBlitInfo.asDestRects[0].y1 = POSITIVE(dst_h);

	sBlitInfo.asSrcRects[0].y0 = POSITIVE(src_y1);
	sBlitInfo.asSrcRects[0].x1 = POSITIVE(src_w);
	sBlitInfo.asSrcRects[0].x0 = POSITIVE(src_x1);
	sBlitInfo.asSrcRects[0].y1 = POSITIVE(src_h);
#undef POSITIVE

	switch (format) {
		case fourcc_code('Y', 'V', '1', '2'): {
			fmt = PVRSRV_PIXEL_FORMAT_YV12;
			sBlitInfo.ui32NumSources = 3;
			break;
		}
		case fourcc_code('I', '4', '2', '0'): {
			fmt = PVRSRV_PIXEL_FORMAT_I420;
			sBlitInfo.ui32NumSources = 3;
			break;
		}
		case fourcc_code('N', 'V', '1', '2'): {
			fmt = PVRSRV_PIXEL_FORMAT_NV12;
			sBlitInfo.ui32NumSources = 2;
			break;
		}
		case fourcc_code('Y', 'U', 'Y', 'V'): {
			fmt = PVRSRV_PIXEL_FORMAT_YUYV;
			sBlitInfo.ui32NumSources = 1;
			break;
		}
		case fourcc_code('Y', 'U', 'Y', '2'): {
			fmt = PVRSRV_PIXEL_FORMAT_YUY2;
			sBlitInfo.ui32NumSources = 1;
			break;
		}
		case fourcc_code('U', 'Y', 'V', 'Y'): {
			fmt = PVRSRV_PIXEL_FORMAT_UYVY;
			sBlitInfo.ui32NumSources = 1;
			break;
		}
		default:
			return FALSE;
	}

	if (extraCount) {
		if (extraCount + 1 != sBlitInfo.ui32NumSources)
			return FALSE;
	}
	else
		sBlitInfo.ui32NumSources = 1;

	setsurf(&sBlitInfo.asSources[0], pSrcPix, fmt);

	for (i = 1; i < sBlitInfo.ui32NumSources; i++)
		setsurf_addr(&sBlitInfo.asSources[i], extraPix[i - 1]);

	sBlitInfo.Details.sVPBlit.bSeparatePlanes = IMG_TRUE;
	sBlitInfo.Details.sVPBlit.asPlaneDevVAddr[0].uiAddr =
			sBlitInfo.asSources[1].sDevVAddr.uiAddr;
	sBlitInfo.Details.sVPBlit.asPlaneDevVAddr[1].uiAddr =
			sBlitInfo.asSources[2].sDevVAddr.uiAddr;

	err = SGXQueueTransfer(pContext->hTransferContext, &sBlitInfo);

	if (err == PVRSRV_OK) {
		setPixmapOnGPU(pDstPix);
		flushScanout(pDstPix);
		return TRUE;
	}

	return FALSE;
}

static PVRSRV_ERROR
PVRRenderAtlas(ScreenPtr pScreen, PVRRenderOp *pRenderOp)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	PVRPtr pPVR = PVREXAPTR(pScrn);
	SGXTQ_ATLAS_OPTYPE eOp;
	PVRSRV_ERROR iErr;
	static SGX_QUEUETRANSFER sBlitInfo;
	PVR2DCONTEXT *pContext = (PVR2DCONTEXT *)pPVR->srv->hPVR2DContext;

	if (!pRenderOp->pDestMemInfo || !pRenderOp->pSrcMemInfo)
		return PVR2DERROR_INVALID_PARAMETER;

	/* There is a bug in the blob: it uses destColorFormat to calculate
	 * source bpp. */
	if ((pRenderOp->destColorFormat == PVRSRV_PIXEL_FORMAT_A8 &&
	     pRenderOp->srcColorFormat != PVRSRV_PIXEL_FORMAT_A8) ||
	    (pRenderOp->destColorFormat != PVRSRV_PIXEL_FORMAT_A8 &&
	     pRenderOp->srcColorFormat == PVRSRV_PIXEL_FORMAT_A8)) {
		return PVR2DERROR_NOT_YET_IMPLEMENTED;
	}

	switch (pRenderOp->op) {
		case PictOpOver:
		case PictOpSrc:
			eOp = PICT_OP_OVER;
			break;
		case PictOpAdd:
			eOp = PICT_OP_ADD;
			break;
		default:
			return PVR2DERROR_NOT_YET_IMPLEMENTED;
	}

	if (pRenderOp->destColorFormat != PVRSRV_PIXEL_FORMAT_XRGB8888 &&
	    pRenderOp->destColorFormat != PVRSRV_PIXEL_FORMAT_ARGB8888 &&
	    pRenderOp->destColorFormat != PVRSRV_PIXEL_FORMAT_A8) {
		return PVR2DERROR_NOT_YET_IMPLEMENTED;
	}

	if (pRenderOp->srcColorFormat != PVRSRV_PIXEL_FORMAT_XRGB8888 &&
	    pRenderOp->srcColorFormat != PVRSRV_PIXEL_FORMAT_ARGB8888 &&
	    pRenderOp->srcColorFormat != PVRSRV_PIXEL_FORMAT_A8) {
		return PVR2DERROR_NOT_YET_IMPLEMENTED;
	}

	memset(&sBlitInfo, 0, sizeof(sBlitInfo));

	sBlitInfo.ui32Flags = 0x50000;
	sBlitInfo.eType = SGXTQ_TATLAS_BLIT;

	sBlitInfo.Details.sTAtlas.eOp = eOp;
	sBlitInfo.Details.sTAtlas.ui32NumMappings = pRenderOp->numBltRects;
	sBlitInfo.Details.sTAtlas.eAlpha =
			pRenderOp->op = PictOpSrc ?  SGXTQ_ALPHA_SOURCE :
						     SGXTQ_ALPHA_NONE;
	sBlitInfo.Details.sTAtlas.psDstRects = &pRenderOp->bltRects.destRect[0];
	sBlitInfo.Details.sTAtlas.psSrcRects = &pRenderOp->bltRects.srcRect[0];

	sBlitInfo.ui32NumSources = 1;
	sBlitInfo.ui32NumDest = 1;

	sBlitInfo.ui32NumStatusValues = 0;

	sBlitInfo.asDests[0].eMemLayout = SGXTQ_MEMLAYOUT_STRIDE;
	setsurf(&sBlitInfo.asDests[0], pRenderOp->pDest,
		pRenderOp->destColorFormat);

	sBlitInfo.asSources[0].eMemLayout = SGXTQ_MEMLAYOUT_STRIDE;
	setsurf(&sBlitInfo.asSources[0], pRenderOp->pSrc,
		pRenderOp->srcColorFormat);

	iErr = SGXQueueTransfer(pContext->hTransferContext, &sBlitInfo);

	if (iErr) {
		ErrorF("PVRRenderAtlas: SGXQueueTransfer failed %s",
		       PVRSRVGetErrorString(iErr));
	} else {
		pRenderOp->numBltRects = 0;
		pRenderOp->bltRectsIdx = 0;
	}

	return iErr;
}

static Bool
PVRRotateCopyRender(ScreenPtr pScreen, PVRRenderOp *pRenderOp)
{
	/* Not implemented yet, SW compositing seems to be faster */
	return FALSE;
}

static PVRSRV_PIXEL_FORMAT
ValidateHwColorFormat(PVRSRV_PIXEL_FORMAT colorFormat, int bitsPerPixel,
		      IMG_BOOL forceAlpha)
{
	static const PVRSRV_PIXEL_FORMAT colorFormats[32] =
	{
		PVRSRV_PIXEL_FORMAT_R1,
		PVRSRV_PIXEL_FORMAT_UNKNOWN,
		PVRSRV_PIXEL_FORMAT_UNKNOWN,
		PVRSRV_PIXEL_FORMAT_UNKNOWN,
		PVRSRV_PIXEL_FORMAT_UNKNOWN,
		PVRSRV_PIXEL_FORMAT_UNKNOWN,
		PVRSRV_PIXEL_FORMAT_UNKNOWN,
		PVRSRV_PIXEL_FORMAT_A8,
		PVRSRV_PIXEL_FORMAT_UNKNOWN,
		PVRSRV_PIXEL_FORMAT_UNKNOWN,
		PVRSRV_PIXEL_FORMAT_UNKNOWN,
		PVRSRV_PIXEL_FORMAT_UNKNOWN,
		PVRSRV_PIXEL_FORMAT_UNKNOWN,
		PVRSRV_PIXEL_FORMAT_UNKNOWN,
		PVRSRV_PIXEL_FORMAT_UNKNOWN,
		PVRSRV_PIXEL_FORMAT_RGB565,
		PVRSRV_PIXEL_FORMAT_UNKNOWN,
		PVRSRV_PIXEL_FORMAT_UNKNOWN,
		PVRSRV_PIXEL_FORMAT_UNKNOWN,
		PVRSRV_PIXEL_FORMAT_UNKNOWN,
		PVRSRV_PIXEL_FORMAT_UNKNOWN,
		PVRSRV_PIXEL_FORMAT_UNKNOWN,
		PVRSRV_PIXEL_FORMAT_UNKNOWN,
		PVRSRV_PIXEL_FORMAT_RGB888,
		PVRSRV_PIXEL_FORMAT_UNKNOWN,
		PVRSRV_PIXEL_FORMAT_UNKNOWN,
		PVRSRV_PIXEL_FORMAT_UNKNOWN,
		PVRSRV_PIXEL_FORMAT_UNKNOWN,
		PVRSRV_PIXEL_FORMAT_UNKNOWN,
		PVRSRV_PIXEL_FORMAT_UNKNOWN,
		PVRSRV_PIXEL_FORMAT_UNKNOWN,
		PVRSRV_PIXEL_FORMAT_XRGB8888
	};

	if (colorFormat == PVRSRV_PIXEL_FORMAT_UNKNOWN) {
		if (bitsPerPixel <= 32)
			return colorFormats[bitsPerPixel - 1];
	}

	switch (colorFormat) {
		case PVRSRV_PIXEL_FORMAT_RGB565:
		case PVRSRV_PIXEL_FORMAT_RGB555:
		case PVRSRV_PIXEL_FORMAT_ARGB8888:
		case PVRSRV_PIXEL_FORMAT_ABGR8888:
		case PVRSRV_PIXEL_FORMAT_XRGB8888:
		case PVRSRV_PIXEL_FORMAT_XBGR8888:
		case PVRSRV_PIXEL_FORMAT_A8:
			break;
		default:
			colorFormat = PVRSRV_PIXEL_FORMAT_UNKNOWN;
			break;
	}

	if (!forceAlpha)
		return colorFormat;

	switch (colorFormat) {
		case PVRSRV_PIXEL_FORMAT_A8:
		case PVRSRV_PIXEL_FORMAT_ARGB8888:
		case PVRSRV_PIXEL_FORMAT_ABGR8888:
			break;
		case PVRSRV_PIXEL_FORMAT_XBGR8888:
			colorFormat = PVRSRV_PIXEL_FORMAT_ABGR8888;
			break;
		case PVRSRV_PIXEL_FORMAT_XRGB8888:
			colorFormat = PVRSRV_PIXEL_FORMAT_ARGB8888;
			break;
		case PVRSRV_PIXEL_FORMAT_RGB555:
			colorFormat = PVRSRV_PIXEL_FORMAT_ARGB1555;
			break;
		default:
			colorFormat = PVRSRV_PIXEL_FORMAT_UNKNOWN;
			break;
	}

	return colorFormat;
}

static PVRSRV_PIXEL_FORMAT
GetSrvPixelFormat(CARD32 format)
{
	switch (format) {
		case PICT_r8g8b8:
			return PVRSRV_PIXEL_FORMAT_RGB888;
		case PICT_b8g8r8:
			return PVRSRV_PIXEL_FORMAT_BGR888;
		case PICT_a8r8g8b8:
			return PVRSRV_PIXEL_FORMAT_ARGB8888;
		case PICT_a8b8g8r8:
			return PVRSRV_PIXEL_FORMAT_ABGR8888;
		case PICT_x8r8g8b8:
			return PVRSRV_PIXEL_FORMAT_XRGB8888;
		case PICT_x8b8g8r8:
			return PVRSRV_PIXEL_FORMAT_XBGR8888;
		case PICT_r5g6b5:
			return PVRSRV_PIXEL_FORMAT_RGB565;
		case PICT_a8:
			return PVRSRV_PIXEL_FORMAT_A8;
		default:
			break;
	}

	return -1;
}

static Bool
PVRRenderBatch(PVRPtr pPVR, PVRRenderOp *pRenderOp)
{
	Bool rv = TRUE;
	const PVRSRV_CLIENT_MEM_INFO *psUSSEMemInfo;
	PVRSRV_ERROR iErr;
	int i;
	PVR2DCONTEXT *pContext = (PVR2DCONTEXT *)pPVR->srv->hPVR2DContext;
	static SGX_QUEUETRANSFER sBlitInfo;

	if (!pRenderOp->pDestMemInfo)
		return FALSE;

	if (!pRenderOp->pSrcMemInfo)
		return FALSE;

	if (pRenderOp->pMask && !pRenderOp->pMaskMemInfo)
		return FALSE;

	if (!pRenderOp->numBltRects)
		return FALSE;

	memset(&sBlitInfo, 0, sizeof(sBlitInfo));

	psUSSEMemInfo = pRenderOp->hCode;

	if (!psUSSEMemInfo)
		psUSSEMemInfo = PVRGetUseCodeForRender(pRenderOp);

	sBlitInfo.eType = SGXTQ_CUSTOMSHADER_BLIT;
	sBlitInfo.ui32Flags = 0x50000;

	if (pRenderOp->pMask) {
		sBlitInfo.Details.sCustomShader.ui32NumPAs  = 3;
		sBlitInfo.ui32NumSources = 3;
	} else {
		sBlitInfo.Details.sCustomShader.ui32NumPAs = 2;
		sBlitInfo.ui32NumSources = 2;
	}

	sBlitInfo.Details.sCustomShader.sUSEExecAddr.uiAddr =
			psUSSEMemInfo->sDevVAddr.uiAddr;

	sBlitInfo.Details.sCustomShader.ui32NumSAs = 2;
	sBlitInfo.Details.sCustomShader.UseParams[0] = 0;
	sBlitInfo.Details.sCustomShader.UseParams[1] = 0;
	sBlitInfo.Details.sCustomShader.ui32NumTempRegs = 3;

	sBlitInfo.ui32NumDest = 1;
	sBlitInfo.ui32NumDestRects = 1;

	setsurf(&sBlitInfo.asDests[0], pRenderOp->pDest,
		pRenderOp->destColorFormat);
	sBlitInfo.asDests[0].eMemLayout = SGXTQ_MEMLAYOUT_STRIDE;

	setsurf(&sBlitInfo.asSources[0], pRenderOp->pSrc,
		pRenderOp->srcColorFormat);
	sBlitInfo.asSources[0].eMemLayout = SGXTQ_MEMLAYOUT_STRIDE;

	setsurf(&sBlitInfo.asSources[1], pRenderOp->pDest,
		pRenderOp->destColorFormat);
	sBlitInfo.asSources[1].eMemLayout = SGXTQ_MEMLAYOUT_STRIDE;
	sBlitInfo.asSources[1].psSyncInfo = NULL;

	sBlitInfo.ui32NumSrcRects = 2;

	if (pRenderOp->pMask && pRenderOp->pMaskMemInfo) {
		sBlitInfo.ui32NumSrcRects++;

		setsurf(&sBlitInfo.asSources[2], pRenderOp->pMask,
			pRenderOp->maskColorFormat);
		sBlitInfo.asSources[2].eMemLayout = SGXTQ_MEMLAYOUT_STRIDE;
	}

	if (!pRenderOp->numBltRects) {
		iErr = PVRSRV_ERROR_INVALID_PARAMS;
		goto out;
	}

	for (i = 0 ; i < pRenderOp->numBltRects; i++) {
		PVRBltRect *bltRects = &pRenderOp->bltRects;
		SGXHW_RENDER_RECTS *dst = &bltRects->destRect[i];
		SGXHW_RENDER_RECTS *src = &bltRects->srcRect[i];

		sBlitInfo.asDestRects[0].x0 = dst->x0;
		sBlitInfo.asDestRects[0].x1 = dst->x1;
		sBlitInfo.asDestRects[0].y0 = dst->y0;
		sBlitInfo.asDestRects[0].y1 = dst->y1;

		sBlitInfo.asSrcRects[0].x0 = src->x0;
		sBlitInfo.asSrcRects[0].x1 = src->x1;
		sBlitInfo.asSrcRects[0].y0 = src->y0;
		sBlitInfo.asSrcRects[0].y1 = src->y1;

		if (src->x1 - src->x0 != dst->x1 - dst->x0 ||
		    src->y1 - src->y0 != dst->y1 - dst->y0) {
			sBlitInfo.Details.sCustomShader.aeFilter[0] =
					SGXTQ_FILTERTYPE_LINEAR;
		} else {
			sBlitInfo.Details.sCustomShader.aeFilter[0] =
					SGXTQ_FILTERTYPE_POINT;
		}

		sBlitInfo.Details.sCustomShader.aeFilter[2] =
				SGXTQ_FILTERTYPE_POINT;

		sBlitInfo.asSrcRects[1].x0 = dst->x0;
		sBlitInfo.asSrcRects[1].x1 = dst->x1;
		sBlitInfo.asSrcRects[1].y0 = dst->y0;
		sBlitInfo.asSrcRects[1].y1 = dst->y1;

		if (pRenderOp->pMask && pRenderOp->pMaskMemInfo) {

			SGXHW_RENDER_RECTS *mask = &bltRects->maskRect[i];

			sBlitInfo.asSrcRects[2].x0 = mask->x0;
			sBlitInfo.asSrcRects[2].x1 = mask->x1;
			sBlitInfo.asSrcRects[2].y0 = mask->y0;
			sBlitInfo.asSrcRects[2].y1 = mask->y1;

			if (dst->x1 - dst->x0 != mask->x1 - mask->x0 ||
			    dst->y1 - dst->y0 != mask->y1 - mask->y0) {
				sBlitInfo.Details.sCustomShader.aeFilter[2] =
						SGXTQ_FILTERTYPE_LINEAR;
			}
		}

		iErr = SGXQueueTransfer(pContext->hTransferContext,
					&sBlitInfo);
		if (iErr != PVRSRV_OK)
			break;
	}

out:
	if (iErr) {
		ErrorF("PVRRenderBatch: SGXQueueTransfer failed %s\n",
		       PVRSRVGetErrorString(iErr));
		rv = FALSE;
	}

	pRenderOp->numBltRects = 0;
	pRenderOp->bltRectsIdx = 0;

	return rv;
}


#if defined SGX_OMAP_443x
PVRRender_DEVICE_DEF(443x)
#elif defined SGX_OMAP_343x
PVRRender_DEVICE_DEF(343x)
#else
#error "Unknown omap variant"
#endif
{
	RENDER_TRANSFORMATION transform = pRenderOp->transform;
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	PVRPtr pPVR = PVREXAPTR(pScrn);

	if (pRenderOp->pDest)
		pRenderOp->pDestMemInfo = pixmap2meminfo(pRenderOp->pDest);

	if (pRenderOp->pSrc)
		pRenderOp->pSrcMemInfo = pixmap2meminfo(pRenderOp->pSrc);

	if (pRenderOp->pMask)
		pRenderOp->pMaskMemInfo = pixmap2meminfo(pRenderOp->pMask);

	if (pRenderOp->pDestPicture) {
		PicturePtr pPic = pRenderOp->pDestPicture;

		pRenderOp->destColorFormat =
				ValidateHwColorFormat(
					GetSrvPixelFormat(pPic->format),
					pRenderOp->pDest->drawable.bitsPerPixel,
					pRenderOp->destForceAlpha);
	} else {
		PixmapPtr pPix = pRenderOp->pDest;

		if (pPix &&
		    pRenderOp->destColorFormat == PVRSRV_PIXEL_FORMAT_UNKNOWN) {
			pRenderOp->destColorFormat =
					ValidateHwColorFormat(
						PVRSRV_PIXEL_FORMAT_UNKNOWN,
						pPix->drawable.bitsPerPixel,
						pRenderOp->destForceAlpha);
		}
	}

	if (pRenderOp->pSrcPicture) {
		PicturePtr pPic = pRenderOp->pSrcPicture;

		pRenderOp->srcColorFormat =
				ValidateHwColorFormat(
					GetSrvPixelFormat(pPic->format),
					pRenderOp->pSrc->drawable.bitsPerPixel,
					pRenderOp->srcForceAlpha);
		pRenderOp->srcRepeatType = pPic->repeatType;
	} else {
		PixmapPtr pPix = pRenderOp->pSrc;

		if (pPix &&
		    pRenderOp->srcColorFormat == PVRSRV_PIXEL_FORMAT_UNKNOWN) {
			pRenderOp->srcColorFormat =
					ValidateHwColorFormat(
						PVRSRV_PIXEL_FORMAT_UNKNOWN,
						pPix->drawable.bitsPerPixel,
						pRenderOp->srcForceAlpha);
		}
	}

	if (pRenderOp->pMaskPicture) {
		PicturePtr pPic = pRenderOp->pMaskPicture;

		pRenderOp->maskColorFormat =
				ValidateHwColorFormat(
					GetSrvPixelFormat(pPic->format),
					pRenderOp->pMask->drawable.bitsPerPixel,
					FALSE);
		pRenderOp->maskRepeatType = pPic->repeatType;
	} else {
		PixmapPtr pPix = pRenderOp->pMask;

		if (pPix &&
		    pRenderOp->maskColorFormat == PVRSRV_PIXEL_FORMAT_UNKNOWN) {
			pRenderOp->maskColorFormat =
					ValidateHwColorFormat(
						PVRSRV_PIXEL_FORMAT_UNKNOWN,
						pPix->drawable.bitsPerPixel,
						FALSE);
		}
	}

	if (pRenderOp->pSrc &&
		transform != TRANSF_UNKNOWN && transform != TRANSF_NONE) {
		if (pRenderOp->pMask)
			return PVRRenderBatch(pPVR, pRenderOp);

		if (pRenderOp->op == PictOpSrc)
			return PVRRotateCopyRender(pScreen, pRenderOp);

	} else if (pRenderOp->pMask)
		return PVRRenderBatch(pPVR, pRenderOp);

	if (pRenderOp->numBltRects < 2 || PVRRenderAtlas(pScreen, pRenderOp))
		return PVRRenderBatch(pPVR, pRenderOp);

	return TRUE;
}
