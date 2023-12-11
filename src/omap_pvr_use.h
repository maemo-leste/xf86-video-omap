/*
 * omap_pvr_use.h
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

#ifndef __OMAP_PVR_USE_H__
#define __OMAP_PVR_USE_H__

Bool PVRUseInit(ScreenPtr pScreen, PVRPtr pPVR);
void PVRUseDeInit(ScreenPtr pScreen, PVRPtr pPVR);

PVR2D_HANDLE PVRSolid8BitGetUse(int alu);
PVR2D_HANDLE PVRGetUseCodeCopyAlphaFill(void);

PVR2D_HANDLE
PVRGetUseCodeForRender(PVRRenderOp *pRender);

#endif /* __OMAP_PVR_USE_H__ */
