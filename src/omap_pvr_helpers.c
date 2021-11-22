/*
 * omap_pvr_helpers.c
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

#include <stdlib.h>

#include <pvr_debug.h>

#include "omap_driver.h"
#include "omap_exa_pvr.h"

#include "omap_pvr_helpers.h"

static PVRSERVICES gsSrv;
static unsigned int guiSrvRefCount;

IMG_BOOL
InitialiseServices(ScreenPtr pScreen, PPVRSERVICES *pSrv)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	PVRSRV_DEVICE_IDENTIFIER dev_ids[PVRSRV_MAX_DEVICES];
	PVRSRV_HEAP_INFO heap_info[PVRSRV_MAX_CLIENT_HEAPS];
	IMG_UINT32 heap_count;
	IMG_UINT32 num_devices;
	PVRSRV_ERROR err;
	unsigned i;

	TRACE_ENTER();

	if (guiSrvRefCount)
	{
		guiSrvRefCount++;
		*pSrv = &gsSrv;
		return IMG_TRUE;
	}

	err = PVRSRVConnect(&gsSrv.services, 0);
	if (err != PVRSRV_OK) {
		ERROR_MSG("PVRSRVConnect failed: %s",
			  PVRSRVGetErrorString(err));
		return IMG_FALSE;
	}

	err = PVRSRVEnumerateDevices(gsSrv.services, &num_devices, dev_ids);

	if (err != PVRSRV_OK) {
		ERROR_MSG("PVRSRVEnumerateDevices failed: %s",
			  PVRSRVGetErrorString(err));
		goto ErrorDisconnect;
	}

	for (i = 0; i < num_devices; i++) {
		if (dev_ids[i].eDeviceClass == PVRSRV_DEVICE_CLASS_3D) {
			break;
		}
	}

	if (i == num_devices) {
		ERROR_MSG("Couldn't find a 3D device");
		goto ErrorDisconnect;
	}

	err = PVRSRVAcquireDeviceData(gsSrv.services,
				      dev_ids[i].ui32DeviceIndex,
				      &gsSrv.dev_data,
				      PVRSRV_DEVICE_TYPE_UNKNOWN);
	if (err != PVRSRV_OK) {
		ERROR_MSG("PVRSRVAcquireDeviceData failed: %s",
			  PVRSRVGetErrorString(err));
		goto ErrorDisconnect;
	}

	err = PVRSRVCreateDeviceMemContext(&gsSrv.dev_data,
					   &gsSrv.h_dev_mem_context,
					   &heap_count,
					   heap_info);
	if (err != PVRSRV_OK) {
		ErrorF("PVRSRVCreateDeviceMemContext failed: %s",
		       PVRSRVGetErrorString(err));
		goto ErrorDisconnect;
	}

	for (i = 0; i < heap_count; i++) {
		if (HEAP_IDX(heap_info[i].ui32HeapID) == SGX_GENERAL_HEAP_ID) {
			gsSrv.h_mapping_heap = heap_info[i].hDevMemHeap;
			break;
		}
	}

	if (i == heap_count) {
		ERROR_MSG("Couldn't find heap %u", SGX_GENERAL_HEAP_ID);
		goto ErrorDestroyMemContext;
	}

	gsSrv.misc_info.ui32StateRequest =
			PVRSRV_MISC_INFO_GLOBALEVENTOBJECT_PRESENT;

	err = PVRSRVGetMiscInfo((IMG_HANDLE)gsSrv.services,
				&gsSrv.misc_info);
	if (err != PVRSRV_OK) {
		ERROR_MSG("Request for global event object failed: %s",
			  PVRSRVGetErrorString(err));
		goto ErrorDestroyMemContext;
	}

	err = PVR2DCreateDeviceContext(dev_ids[i].ui32DeviceIndex,
				       &gsSrv.hPVR2DContext, 0);
	if (err != PVRSRV_OK) {
		ERROR_MSG("Cannot create 2D context: %s",
			  PVRSRVGetErrorString(err));
		goto ErrorReleaseMisceInfo;
	}

	guiSrvRefCount++;
	*pSrv = &gsSrv;

	return IMG_TRUE;

ErrorReleaseMisceInfo:
	PVRSRVReleaseMiscInfo((IMG_HANDLE)gsSrv.services, &gsSrv.misc_info);
	memset(&gsSrv.misc_info, 0, sizeof(gsSrv.misc_info));

ErrorDestroyMemContext:
	PVRSRVDestroyDeviceMemContext(&gsSrv.dev_data,
				      gsSrv.h_dev_mem_context);
	gsSrv.h_dev_mem_context = 0;

ErrorDisconnect:
	PVRSRVDisconnect((IMG_HANDLE)gsSrv.services);
	gsSrv.services = NULL;

	return IMG_FALSE;
}

void
DeInitialiseServices(ScreenPtr pScreen, PPVRSERVICES pSrv)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);

	TRACE_ENTER();

	PVR_ASSERT(pSrv == &gsSrv);

	if (guiSrvRefCount)
		guiSrvRefCount--;
	else
		return;

	if (guiSrvRefCount)
		return;

	if (gsSrv.hPVR2DContext) {
		PVR2DDestroyDeviceContext(pSrv->hPVR2DContext);
		pSrv->hPVR2DContext = NULL;
	}

	if (gsSrv.services) {
		PVRSRVReleaseMiscInfo((IMG_HANDLE)pSrv->services,
				      &pSrv->misc_info);
	}

	if (gsSrv.h_dev_mem_context) {
		PVRSRVDestroyDeviceMemContext(&gsSrv.dev_data,
					      gsSrv.h_dev_mem_context);
		gsSrv.h_dev_mem_context = 0;
	}

	if (gsSrv.services) {
		PVRSRVDisconnect((IMG_HANDLE)gsSrv.services);
		pSrv->services = NULL;
	}
}

IMG_BOOL
PVRMapBo(ScreenPtr pScreen, PPVRSERVICES pSrv, struct omap_bo *bo,
	 PPVR2DMEMINFO meminfo)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	int fd = omap_bo_dmabuf(bo);
	PVRSRV_ERROR err;
	PPVRSRV_CLIENT_MEM_INFO psClientMemInfo;

	err = PVRSRVMapFullDmaBuf(
		      &pSrv->dev_data,
		      pSrv->h_mapping_heap,
		      PVRSRV_MAP_NOUSERVIRTUAL,
		      fd,
		      (PPVRSRV_CLIENT_MEM_INFO *)&meminfo->hPrivateData);

	if (err != PVRSRV_OK) {
		ERROR_MSG("PVRSRVMapFullDmaBuf failed: %s fd %d",
		PVRSRVGetErrorString(err), fd);
	}

	close(fd);

	psClientMemInfo = (PPVRSRV_CLIENT_MEM_INFO)meminfo->hPrivateData;
	meminfo->ui32MemSize = psClientMemInfo->uAllocSize;

	DEBUG_MSG("BO %p mapped, meminfo %p, sDevVAddr.uiAddr %u size %u, flags %x",
		  bo, meminfo, psClientMemInfo->sDevVAddr.uiAddr,
		  psClientMemInfo->uAllocSize, psClientMemInfo->ui32Flags);

	return err == PVRSRV_OK ? IMG_TRUE : IMG_FALSE;
}

IMG_BOOL
PVRUnMapBo(ScreenPtr pScreen, PPVRSERVICES pSrv, PPVR2DMEMINFO meminfo)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	PVRSRV_ERROR err;
	PPVRSRV_CLIENT_MEM_INFO psClientMemInfo;

	DEBUG_MSG("unmapping BO %p", meminfo);

	psClientMemInfo = (PPVRSRV_CLIENT_MEM_INFO)meminfo->hPrivateData;
	err = PVRSRVUnmapDmaBuf(&pSrv->dev_data, psClientMemInfo);

	if (err != PVRSRV_OK) {
		ErrorF("PVRSRVUnmapDmaBuf failed: %s\n",
		PVRSRVGetErrorString(err));
	}

	meminfo->hPrivateData = NULL;

	return err == PVRSRV_OK ? IMG_TRUE : IMG_FALSE;
}

/* FIXME - get from kernel headers */
struct pvr_unpriv {
	uint32_t cmd;
	uint32_t res;
};

#define DRM_PVR_UNPRIV		0x4
#define PVR_UNPRIV_INIT_SUCCESFUL 0

static int
PVRDRMOpen(void)
{
  return drmOpen("pvr", NULL);
}

int
PVRDRMServicesInitStatus(Bool *pbStatus)
{
	int fd;
	int res;
	struct pvr_unpriv cmd = {
		.cmd = PVR_UNPRIV_INIT_SUCCESFUL,
		.res = 0
	};

	fd = PVRDRMOpen();

	if (fd < 0) {
		return fd;
	}

	res = drmCommandWriteRead(fd, DRM_PVR_UNPRIV, &cmd, sizeof(cmd));
	drmClose(fd);

	if (res < 0) {
		return res;
	}

	*pbStatus = cmd.res != 0;

	return 0;
}
