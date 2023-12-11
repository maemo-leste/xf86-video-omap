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

static void
setsurf(SGXTQ_SURFACE *surf, PixmapPtr pPixmap, PVRSRV_PIXEL_FORMAT fmt)
{
	OMAPPixmapPrivPtr pixmapPriv = exaGetPixmapDriverPrivate(pPixmap);
	PPVRSRV_CLIENT_MEM_INFO psClientMemInfo;
	PrivPixmapPtr pvrPixmapPriv;

	pvrPixmapPriv = sgxMapPixmapBo(pPixmap->drawable.pScreen, pixmapPriv);

	if (!pvrPixmapPriv)
		return;

	psClientMemInfo = pvrPixmapPriv->meminfo.hPrivateData;

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
	OMAPPixmapPrivPtr pixmapPriv = exaGetPixmapDriverPrivate(pPixmap);
	PPVRSRV_CLIENT_MEM_INFO psClientMemInfo;
	PrivPixmapPtr pvrPixmapPriv;

	pvrPixmapPriv = sgxMapPixmapBo(pPixmap->drawable.pScreen, pixmapPriv);

	if (!pvrPixmapPriv)
		return;

	psClientMemInfo = pvrPixmapPriv->meminfo.hPrivateData;

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

