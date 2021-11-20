/*
 * omap_pvr_use.c
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

#include <pvr_debug.h>

#include "omap_driver.h"
#include "omap_exa.h"

#include "omap_exa_pvr.h"
#include "omap_pvr_helpers.h"
#include "omap_pvr_use.h"

#define OP(op) {op, ARRAY_SIZE(op), NULL}

static const unsigned char solid8bit_clear[8] =
{0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0xA4, 0xFC};

static const unsigned char solid8bit_and[8] =
{0x00, 0x00, 0x00, 0xE0, 0x01, 0x10, 0x84, 0x50};

static const unsigned char solid8bit_andReverse[8] =
{0x00, 0x00, 0x00, 0xE0, 0x01, 0x18, 0x84, 0x50};

static const unsigned char solid8bit_copy[8] =
{0x00, 0x00, 0x00, 0xE0, 0x01, 0x10, 0x85, 0x28};

static const unsigned char solid8bit_andInverted[8] =
{0x00, 0x00, 0x00, 0xB0, 0x01, 0x18, 0x84, 0x50};

static const unsigned char solid8bit_noop[8] =
{0x00, 0x00, 0x00, 0xA0, 0x01, 0x10, 0x85, 0x28};

static const unsigned char solid8bit_xor[8] =
{0x00, 0x00, 0x00, 0xB0, 0x01, 0x10, 0x84, 0x58};

static const unsigned char solid8bit_or[8] =
{0x00, 0x00, 0x00, 0xB0, 0x09, 0x10, 0x84, 0x50};

static const unsigned char solid8bit_nor[24] =
{0x00, 0x00, 0x00, 0xB0, 0x08, 0x10, 0x80, 0x50, 0x7F, 0x40, 0x00, 0x20, 0x00, 0x10, 0x81, 0x58, 0x00, 0x00, 0x00, 0x20, 0x01, 0x10, 0x85, 0x28};

static const unsigned char solid8bit_equiv[] =
{0x00, 0x00, 0x00, 0xB0, 0x01, 0x18, 0x84, 0x58};

static const unsigned char solid8bit_invert[] =
{0x7F, 0x40, 0x00, 0xA0, 0x01, 0x10, 0x85, 0x58};

static const unsigned char solid8bit_orReverse[] =
{0x00, 0x00, 0x00, 0xE0, 0x09, 0x18, 0x84, 0x50};

static const unsigned char solid8bit_copyInverted[] =
{0x7F, 0x40, 0x00, 0xE0, 0x01, 0x10, 0x85, 0x58};

static const unsigned char solid8bit_orInverted[] =
{0x00, 0x00, 0x00, 0xB0, 0x09, 0x18, 0x84, 0x50};

static const unsigned char solid8bit_nand[] =
{0x00, 0x00, 0x00, 0xB0, 0x00, 0x10, 0x80, 0x50, 0x7F, 0x40, 0x00, 0x20, 0x00, 0x10, 0x81, 0x58, 0x00, 0x00, 0x00, 0x20, 0x01, 0x10, 0x85, 0x28};

static const unsigned char solid8bit_set[] =
{0xFF, 0x00, 0x00, 0x00, 0x01, 0x00, 0xA4, 0xFC};

static struct
{
	const unsigned char *use;
	const size_t len;
	PVR2D_HANDLE hFragment;
}
pbexa_solid8bit_use[16] =
{
	OP(solid8bit_clear),
	OP(solid8bit_and),
	OP(solid8bit_andReverse),
	OP(solid8bit_copy),
	OP(solid8bit_andInverted),
	OP(solid8bit_noop),
	OP(solid8bit_xor),
	OP(solid8bit_or),
	OP(solid8bit_nor),
	OP(solid8bit_equiv),
	OP(solid8bit_invert),
	OP(solid8bit_orReverse),
	OP(solid8bit_copyInverted),
	OP(solid8bit_orInverted),
	OP(solid8bit_nand),
	OP(solid8bit_set)
};

static PVR2DERROR
PVRSolid8BitInit(PPVRSERVICES pSrv)
{
	int i;
	PVR2DERROR iErr;

	for (i = 0; i < ARRAY_SIZE(pbexa_solid8bit_use); i++) {
		iErr = PVR2DLoadUseCode(pSrv->hPVR2DContext,
					pbexa_solid8bit_use[i].use,
					pbexa_solid8bit_use[i].len,
					&pbexa_solid8bit_use[i].hFragment);
		if (iErr != PVR2D_OK) {
			break;
		}
	}

	return iErr;
}

static void
PVRSolid8BitDeInit(PPVRSERVICES pSrv)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(pbexa_solid8bit_use); i++) {
		if (pbexa_solid8bit_use[i].hFragment) {
			PVR2DFreeUseCode(pSrv->hPVR2DContext,
					 pbexa_solid8bit_use[i].hFragment);
			pbexa_solid8bit_use[i].hFragment = NULL;
		}
	}
}

Bool
PVRUseInit(ScreenPtr pScreen, PVRPtr pPVR)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	PVR2DERROR iErr;

	iErr = PVRSolid8BitInit(pPVR->srv);

	if (iErr != PVR2D_OK) {
		ERROR_MSG("PVRSolid8BitInit failed (%s)",
			  sgxErrorCodeToString(iErr));
		return FALSE;
	}

	return TRUE;
}

void
PVRUseDeInit(ScreenPtr pScreen, PVRPtr pPVR)
{
	PVRSolid8BitDeInit(pPVR->srv);
}

PVR2D_HANDLE
PVRSolid8BitGetUse(int alu)
{
	return pbexa_solid8bit_use[alu].hFragment;
}

PVR2D_HANDLE
PVRGetUseCodeCopyAlphaFill(void)
{
	return NULL;
}
