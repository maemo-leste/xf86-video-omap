/*
 * omap_pvr_sgx_def.h
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

#ifndef __OMAP_PVR_SGX_DEF_H_INCLUDED__
#define __OMAP_PVR_SGX_DEF_H_INCLUDED__

#include <exa.h>

#include "omap_driver.h"
#include "omap_exa.h"
#include "omap_exa_pvr.h"

#include <services.h>
#include <pvr2d.h>
#include <sgxapi_km.h>
#include <pvr_debug.h>

typedef enum _SGXTQ_BLITTYPE_
{
	SGXTQ_BLIT = 0x1,
	SGXTQ_MIPGEN = 0x2,
	SGXTQ_UNK = 0x3,
	SGXTQ_VIDEO_BLIT = 0x4,
	SGXTQ_FILL = 0x5,
	SGXTQ_BUFFERBLT = 0x6,
	SGXTQ_CUSTOM = 0x7,
	SGXTQ_FULL_CUSTOM = 0x8,
	SGXTQ_TEXTURE_UPLOAD = 0x9,
	SGXTQ_CLIP_BLIT = 0xA,
	SGXTQ_CUSTOMSHADER_BLIT = 0xB,
	SGXTQ_COLOURLUT_BLIT = 0xC,
	SGXTQ_CLEAR_TYPE_BLEND = 0xD,
	SGXTQ_TATLAS_BLIT = 0xE,
	SGXTQ_ARGB2NV12_BLIT = 0xF,
} SGXTQ_BLITTYPE;

typedef enum _SGXTQ_FILTERTYPE_
{
	SGXTQ_FILTERTYPE_POINT = 0x0,
	SGXTQ_FILTERTYPE_LINEAR = 0x1,
	SGXTQ_FILTERTYPE_ANISOTROPIC = 0x2,
} SGXTQ_FILTERTYPE;

typedef enum _SGXTQ_COLOURKEY_
{
	SGXTQ_COLOURKEY_NONE = 0x0,
	SGXTQ_COLOURKEY_SOURCE = 0x1,
	SGXTQ_COLOURKEY_DEST = 0x2,
	SGXTQ_COLOURKEY_SOURCE_PASS = 0x3,
} SGXTQ_COLOURKEY;

typedef enum _SGXTQ_ROTATION_
{
	SGXTQ_ROTATION_NONE = 0x0,
	SGXTQ_ROTATION_90 = 0x1,
	SGXTQ_ROTATION_180 = 0x2,
	SGXTQ_ROTATION_270 = 0x3,
} SGXTQ_ROTATION;

typedef enum _SGXTQ_COPYORDER_
{
	SGXTQ_COPYORDER_AUTO = 0x0,
	SGXTQ_COPYORDER_TL2BR = 0x1,
	SGXTQ_COPYORDER_TR2BL = 0x2,
	SGXTQ_COPYORDER_BR2TL = 0x3,
	SGXTQ_COPYORDER_BL2TR = 0x4,
} SGXTQ_COPYORDER;

typedef enum _SGXTQ_ALPHA_
{
	SGXTQ_ALPHA_NONE = 0x0,
	SGXTQ_ALPHA_SOURCE = 0x1,
	SGXTQ_ALPHA_PREMUL_SOURCE = 0x2,
	SGXTQ_ALPHA_GLOBAL = 0x3,
	SGXTQ_ALPHA_PREMUL_SOURCE_WITH_GLOBAL = 0x4,
} SGXTQ_ALPHA;

typedef struct _SGXTQ_BLITOP_
{
	SGXTQ_FILTERTYPE eFilter;
	IMG_UINT32 ui32ColourKey;
	IMG_UINT32 ui32ColourKeyMask;
	IMG_BOOL bEnableGamma;
	SGXTQ_COLOURKEY eColourKey;
	SGXTQ_ROTATION eRotation;
	SGXTQ_COPYORDER eCopyOrder;
	SGXTQ_ALPHA eAlpha;
	IMG_BYTE byGlobalAlpha;
	IMG_BYTE byCustomRop3;
	IMG_DEV_VIRTADDR sUSEExecAddr;
	IMG_UINT32 UseParams[2];
	IMG_UINT32 uiNumTemporaryRegisters;
	IMG_BOOL bEnablePattern;
	IMG_BOOL bSingleSource;
} SGXTQ_BLITOP;

typedef struct _SGXTQ_FILLOP_
{
	IMG_UINT32 ui32Colour;
	IMG_BYTE byCustomRop3;
} SGXTQ_FILLOP;

typedef struct _SGXTQ_VPBCOEFFS_
{
	IMG_INT16 i16YR;
	IMG_INT16 i16UR;
	IMG_INT16 i16VR;
	IMG_INT16 i16ConstR;
	IMG_INT16 i16ShiftR;
	IMG_INT16 i16YG;
	IMG_INT16 i16UG;
	IMG_INT16 i16VG;
	IMG_INT16 i16ConstG;
	IMG_INT16 i16ShiftG;
	IMG_INT16 i16YB;
	IMG_INT16 i16UB;
	IMG_INT16 i16VB;
	IMG_INT16 i16ConstB;
	IMG_INT16 i16ShiftB;
} SGXTQ_VPBCOEFFS;

typedef struct _SGXTQ_VPBLITOP_
{
  IMG_BOOL bCoeffsGiven;
  SGXTQ_VPBCOEFFS sCoeffs;
  IMG_INT16 iCoeffIdx;
  IMG_BOOL bSeparatePlanes;
  IMG_UINT32 rotate;
  IMG_UINT32 unk2;
  IMG_UINT32 unk3;
  IMG_DEV_VIRTADDR asPlaneDevVAddr[2];
} SGXTQ_VPBLITOP;

typedef enum _SGXTQ_MEMLAYOUT_
{
	SGXTQ_MEMLAYOUT_2D = 0x0,
	SGXTQ_MEMLAYOUT_3D = 0x1,
	SGXTQ_MEMLAYOUT_CEM = 0x2,
	SGXTQ_MEMLAYOUT_STRIDE = 0x3,
	SGXTQ_MEMLAYOUT_TILED = 0x4,
	SGXTQ_MEMLAYOUT_OUT_LINEAR = 0x5,
	SGXTQ_MEMLAYOUT_OUT_TILED = 0x6,
	SGXTQ_MEMLAYOUT_OUT_TWIDDLED = 0x7,
} SGXTQ_MEMLAYOUT;

typedef struct _SGXTQ_TQSURFACE_
{
	IMG_DEV_VIRTADDR sDevVAddr;
	IMG_UINT32 ui32Width;
	IMG_UINT32 ui32Height;
	IMG_INT32 i32StrideInBytes;
	PVRSRV_PIXEL_FORMAT eFormat;
	SGXTQ_MEMLAYOUT eMemLayout;
	IMG_UINT32 ui32ChunkStride;
	PVRSRV_CLIENT_SYNC_INFO *psSyncInfo;
	IMG_UINT32 ui32ChromaPlaneOffset[2];
} SGXTQ_SURFACE;

typedef struct _SGXTQ_CUSTOMSHADEROP_
{
  IMG_DEV_VIRTADDR sUSEExecAddr;
  IMG_UINT32 ui32NumPAs;
  IMG_UINT32 ui32NumSAs;
  IMG_UINT32 UseParams[2];
  IMG_UINT32 ui32NumTempRegs;
  IMG_BOOL bUseDMAForSAs;
  IMG_DEV_VIRTADDR sDevVAddrDMASrc;
  IMG_UINT32 ui32LineOffset;
  SGXTQ_ROTATION eRotation;
  SGXTQ_FILTERTYPE aeFilter[3];
} SGXTQ_CUSTOMSHADEROP;

typedef enum _SGXTQ_ATLAS_OPTYPE_
{
  PICT_OP_OVER = 0x0,
  PICT_OP_ADD = 0x1
} SGXTQ_ATLAS_OPTYPE;

typedef struct _SGXTQ_TATLASBLITOP_
{
  SGXTQ_ALPHA eAlpha;
  IMG_UINT32 ui32NumMappings;
  IMG_RECT *psSrcRects;
  IMG_RECT *psDstRects;
  SGXTQ_ATLAS_OPTYPE eOp;
} SGXTQ_TATLASBLITOP;

typedef struct _SGX_QUEUETRANSFER_
{
	IMG_UINT32 ui32Flags;
	SGXTQ_BLITTYPE eType;
	union
	{
		SGXTQ_BLITOP sBlit;
		SGXTQ_FILLOP sFill;
		SGXTQ_VPBLITOP sVPBlit;
		SGXTQ_TATLASBLITOP sTAtlas;
		SGXTQ_CUSTOMSHADEROP sCustomShader;
#if defined SGX_OMAP_443x
		char padding[164];
#elif defined SGX_OMAP_343x
		char padding[152];
#else
#error "Unknown chipset"
#endif
	} Details;
	IMG_UINT32 ui32NumSources;
	SGXTQ_SURFACE asSources[3];
	IMG_UINT32 ui32NumDest;
	SGXTQ_SURFACE asDests[1];
	IMG_UINT32 ui32NumSrcRects;
	IMG_RECT asSrcRects[3];
	IMG_UINT32 ui32NumDestRects;
	IMG_RECT asDestRects[1];
	IMG_BOOL bPDumpContinuous;
	IMG_UINT32 ui32NumStatusValues;
	PVRSRV_MEMUPDATE asMemUpdates[2];
} SGX_QUEUETRANSFER, *PSGX_QUEUETRANSFER;

typedef struct _PVR2DCONTEXT_
{
	PVRSRV_CONNECTION *psServices;
	IMG_HANDLE hDisplayClassDevice;
	PVRSRV_DEV_DATA sDevData;
	DISPLAY_SURF_ATTRIBUTES sPrimary;
	PVR2DMEMINFO sPrimaryBuffer;
	DISPLAY_INFO sDisplayInfo;
	IMG_HANDLE hDevMemContext;
	IMG_HANDLE h2DHeap;
	IMG_DEV_VIRTADDR s2DHeapBase;
	IMG_HANDLE hGeneralMappingHeap;
	IMG_UINT32 ui32NumClipBlocks;
	IMG_UINT32 ui32NumActiveClipBlocks;
	void *psBltClipBlock;
	IMG_HANDLE hTransferContext;
	PVR2D_ULONG ulFlags;
	IMG_HANDLE hUSEMemHeap;
	void *psFlipChain;
	char padding[32];
	PVRSRV_MISC_INFO sMiscInfo;
} PVR2DCONTEXT;

PVRSRV_ERROR SGXQueueTransfer(IMG_HANDLE hTransferContext,
			      PSGX_QUEUETRANSFER psBlitInfo);
#if 0

typedef struct _SGX_CREATE_TRANSFER_CONTEXT_
{
	IMG_HANDLE hDevMemContext;
	IMG_HANDLE hOSEvent;
} SGX_CREATE_TRANSFER_CONTEXT;

PVRSRV_ERROR SGXCreateTransferContext(
		PPVRSRV_DEV_DATA psDevData, IMG_UINT32 id,
		SGX_CREATE_TRANSFER_CONTEXT *psCreateTransfer,
		IMG_HANDLE *phTransferContext);

PVRSRV_ERROR SGXSetContextPriority(PPVRSRV_DEV_DATA psDevData,
				   int *priority, IMG_UINT32 flags,
				   IMG_HANDLE hTransferContext);
PVRSRV_ERROR SGXDestroyTransferContext(IMG_HANDLE hTransferContext, int flags);

PVR2DERROR ValidateTransferContext(PVR2DCONTEXT *pContext);
#endif

#endif /* __OMAP_PVR_SGX_DEF_H_INCLUDED__ */
