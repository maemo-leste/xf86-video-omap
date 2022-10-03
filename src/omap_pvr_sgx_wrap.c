/*
 * omap_pvr_sgx_wrap.c
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

#include <exa.h>

#include "omap_driver.h"
#include "omap_exa.h"
#include "omap_pvr_sgx.h"

PUT_TEXTURE_IMAGE_FN_DEF(wrap)
{
	static Bool (*PutTextureImageProc)(PixmapPtr pSrcPix, BoxPtr pSrcBox,
			PixmapPtr pOsdPix, BoxPtr pOsdBox,
			PixmapPtr pDstPix, BoxPtr pDstBox,
			unsigned int extraCount, PixmapPtr *extraPix,
			unsigned int format) = NULL;

	if (!PutTextureImageProc) {
		ScrnInfoPtr pScrn = pix2scrn(pDstPix);
		OMAPPtr pOMAP = OMAPPTR(pScrn);

		if (pOMAP->chipset <= 0x34FF)
			PutTextureImageProc = PUT_TEXTURE_IMAGE_FN_NAME(343x);
		else if (pOMAP->chipset >= 0x4430 && pOMAP->chipset <= 0x443F)
			PutTextureImageProc = PUT_TEXTURE_IMAGE_FN_NAME(443x);
		else {
			static Bool once = TRUE;
			if (once) {
				ERROR_MSG("Unsupported chipset %x",
					  pOMAP->chipset);
				once = FALSE;
			}
			return FALSE;
		}
	}

	return PutTextureImageProc(pSrcPix, pSrcBox, pOsdPix, pOsdBox,
				   pDstPix, pDstBox, extraCount, extraPix,
				   format);
}
