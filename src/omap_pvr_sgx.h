/*
 * omap_pvr_sgx.h
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

#ifndef __OMAP_PVR_SGX_H_INCLUDED__
#define __OMAP_PVR_SGX_H_INCLUDED__

#define PUT_TEXTURE_IMAGE_FN_NAME(omap) \
	PutTextureImage_##omap

#define PUT_TEXTURE_IMAGE_FN_DEF(omap) \
Bool PUT_TEXTURE_IMAGE_FN_NAME(omap) \
	(PixmapPtr pSrcPix, BoxPtr pSrcBox, PixmapPtr pOsdPix, \
	BoxPtr pOsdBox, PixmapPtr pDstPix, BoxPtr pDstBox, \
	unsigned int extraCount, PixmapPtr *extraPix, \
	unsigned int format)

PUT_TEXTURE_IMAGE_FN_DEF(443x);
PUT_TEXTURE_IMAGE_FN_DEF(343x);
PUT_TEXTURE_IMAGE_FN_DEF(wrap);

#define PUT_TEXTURE_IMAGE_FN PUT_TEXTURE_IMAGE_FN_NAME(wrap)

#endif /* __OMAP_PVR_SGX_H_INCLUDED__ */
