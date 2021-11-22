/*
 * omap_pvr_helpers.h
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

#include <limits.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

IMG_BOOL InitialiseServices(ScreenPtr pScreen, PPVRSERVICES *pSrv);
void DeInitialiseServices(ScreenPtr pScreen, PPVRSERVICES pSrv);

int PVRDRMServicesInitStatus(Bool *pbStatus);

IMG_BOOL PVRMapBo(ScreenPtr pScreen, PPVRSERVICES pSrv, struct omap_bo *bo, PPVR2DMEMINFO meminfo);
IMG_BOOL PVRUnMapBo(ScreenPtr pScreen, PPVRSERVICES pSrv, PPVR2DMEMINFO meminfo);
