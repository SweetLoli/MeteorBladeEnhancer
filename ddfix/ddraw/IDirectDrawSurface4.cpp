﻿/**
* Copyright (C) 2017 Elisha Riedlinger
*
* This software is  provided 'as-is', without any express  or implied  warranty. In no event will the
* authors be held liable for any damages arising from the use of this software.
* Permission  is granted  to anyone  to use  this software  for  any  purpose,  including  commercial
* applications, and to alter it and redistribute it freely, subject to the following restrictions:
*
*   1. The origin of this software must not be misrepresented; you must not claim that you  wrote the
*      original  software. If you use this  software  in a product, an  acknowledgment in the product
*      documentation would be appreciated but is not required.
*   2. Altered source versions must  be plainly  marked as such, and  must not be  misrepresented  as
*      being the original software.
*   3. This notice may not be removed or altered from any source distribution.
*/

#include "ddraw.h"
#include "../D3D9Context.h"
namespace ND3DX9
{
#undef _D3D9_H_
#undef _d3d9TYPES_H_
#undef _d3d9CAPS_H
#undef D3DMATRIX_DEFINED
#include <d3dx9.h>
}
#include <functional>

template <bool HAVESRCCOLORKEY, bool HAVEDESTCOLORKEY, bool CHECKDIRTY = false>
static void LineProcess(D3DCOLOR* srcLine, D3DCOLOR* destLine, int width, DWORD srcColorKey, DWORD destColorKey)
{
	for (int x = 0; x < width; x++)
	{
		auto srcPixel = srcLine[x];
		auto& destPixel = destLine[x];
		if (HAVESRCCOLORKEY)
		{
			if ((srcPixel & 0x00ffffff) == srcColorKey)
			{
				continue;
			}
		}

		if (HAVEDESTCOLORKEY)
		{
			if ((destPixel & 0x00ffffff) != destColorKey)
			{
				continue;
			}
		}

		if (CHECKDIRTY)
		{
			if (true)
			{
				// trick for GetDC
				if (srcPixel != 0)
				{
					destPixel = srcPixel | 0xff000000;
				}
			}
			else
			{
				// 标准写法
				if (srcPixel & 0xff000000)
				{
					destPixel = srcPixel | 0xff000000;
				}
			}
		}
		else
		{
			destPixel = srcPixel | 0xff000000;
		}
	}
};

struct ISurface9Wrapper
{
	virtual ~ISurface9Wrapper() = default;
	virtual SmartPtr<ND3D9::IDirect3DSurface9> GetSurface9() const = 0;
	virtual HRESULT Blt(ISurface9Wrapper* srcSurface9Wrapper, LPRECT lpSrcRect, LPRECT lpDestRect, D3DCOLOR* srcColorKey, D3DCOLOR* destColorKey) = 0;
	virtual HRESULT BltFast(ISurface9Wrapper* srcSurface9Wrapper, LPRECT lpSrcRect, DWORD destX, DWORD destY, D3DCOLOR* srcColorKey, D3DCOLOR* destColorKey) = 0;
	virtual HRESULT FillColor(LPRECT rect, D3DCOLOR color) = 0;
	virtual std::string GetImplClassName() const = 0;
};

class SoftwareSurface9Wrapper final : public ISurface9Wrapper
{
public:
	SoftwareSurface9Wrapper(ND3D9::D3D9Context* d3d9Context,int width, int height, ND3D9::D3DFORMAT format, ESurfaceType surfaceType)
		: m_d3d9Context(d3d9Context)
		, m_surfaceType(surfaceType)
	{
		if (surfaceType == ESurfaceType::BackBuffer)
		{
			m_surface9Handle = m_d3d9Context->CreateOffScreenSurface9(width, height, format, ND3D9::D3DPOOL_SYSTEMMEM);
		}
		else if (surfaceType == ESurfaceType::OffScreen)
		{
			m_surface9Handle = m_d3d9Context->CreateOffScreenSurface9(width, height, format, ND3D9::D3DPOOL_SYSTEMMEM);
		}
		else if (surfaceType == ESurfaceType::ZBuffer)
		{
			m_surface9Handle = m_d3d9Context->CreateZBufferSurface9(width, height, ND3D9::D3DFMT_D16, ND3D9::D3DMULTISAMPLE_NONE, 0, FALSE);
		}
		else if(surfaceType == ESurfaceType::Primary)
		{
			m_surface9Handle = m_d3d9Context->GetBackBuffer9();
		}
	}

	virtual ~SoftwareSurface9Wrapper()
	{
		m_d3d9Context->ReleaseResource9(m_surface9Handle);
	}

	virtual std::string GetImplClassName() const override
	{
		return "SoftwareSurface9Wrapper";
	}

	virtual SmartPtr<ND3D9::IDirect3DSurface9> GetSurface9() const override
	{
		return m_d3d9Context->GetResource9<ND3D9::IDirect3DSurface9>(m_surface9Handle, nullptr);
	}

	virtual HRESULT Blt(ISurface9Wrapper* srcSurface9Wrapper, LPRECT lpSrcRect, LPRECT lpDestRect, D3DCOLOR* srcColorKey, D3DCOLOR* destColorKey) override
	{
		HRESULT hr = DDERR_GENERIC;

		assert(srcSurface9Wrapper->GetImplClassName() == this->GetImplClassName());
		auto srcSurface9 = srcSurface9Wrapper->GetSurface9();

		std::function<void(D3DCOLOR* srcLine, D3DCOLOR* destLine, int x)> lineCallBack;

		using namespace std::placeholders;
		if (m_surfaceType == ESurfaceType::Primary)
		{
			const bool checkDirty = true; // trick for GetDC
			if (srcColorKey && !destColorKey)
			{
				lineCallBack = std::bind(&LineProcess<true, false, checkDirty>, _1, _2, _3, *srcColorKey, 0);
			}
			else if (!srcColorKey && destColorKey)
			{
				lineCallBack = std::bind(&LineProcess<false, true, checkDirty>, _1, _2, _3, 0, *destColorKey);
			}
			else if (srcColorKey && destColorKey)
			{
				lineCallBack = std::bind(&LineProcess<true, true, checkDirty>, _1, _2, _3, *srcColorKey, *destColorKey);
			}
			else
			{
				lineCallBack = std::bind(&LineProcess<false, false, checkDirty>, _1, _2, _3, 0, 0);
			}
		}
		else
		{
			if (srcColorKey && !destColorKey)
			{
				lineCallBack = std::bind(&LineProcess<true, false>, _1, _2, _3, *srcColorKey, 0);
			}
			else if (!srcColorKey && destColorKey)
			{
				lineCallBack = std::bind(&LineProcess<false, true>, _1, _2, _3, 0, *destColorKey);
			}
			else if (srcColorKey && destColorKey)
			{
				lineCallBack = std::bind(&LineProcess<true, true>, _1, _2, _3, *srcColorKey, *destColorKey);
			}
			else
			{
				lineCallBack = std::bind(&LineProcess<false, false>, _1, _2, _3, 0, 0);
			}
		}


		auto destSurface9 = GetSurface9();

		int srcWidth = lpSrcRect->right - lpSrcRect->left;
		int srcHeight = lpSrcRect->bottom - lpSrcRect->top;
		int destWidth = lpDestRect->right - lpDestRect->left;
		int destHeight = lpDestRect->bottom - lpDestRect->top;
		float horizonScale = (float)destWidth / srcWidth;
		float verticalScale = (float)destHeight / srcHeight;

		D3DCOLOR* srcSampled = new D3DCOLOR[destWidth * destHeight];

		int* sampleTableHorizon = new int[destWidth];
		int* sampleTableVertical = new int[destHeight];
		for (int x = 0; x < destWidth; x++)
		{
			sampleTableHorizon[x] = (int)(x / horizonScale);
		}

		for (int y = 0; y < destHeight; y++)
		{
			sampleTableVertical[y] = (int)(y / verticalScale);
		}

		{
			ND3D9::D3DLOCKED_RECT srcLockedRect;
			if (SUCCEEDED(hr = srcSurface9->LockRect(&srcLockedRect, lpSrcRect, D3DLOCK_READONLY)))
			{
				int destPitch = destWidth * sizeof(D3DCOLOR);
				char* srcBits = (char*)srcLockedRect.pBits;
				char* destBits = (char*)srcSampled;
				for (int y = 0; y < destHeight; y++)
				{
					D3DCOLOR* srcLineStart = (D3DCOLOR*)srcBits;
					D3DCOLOR* destLineStart = (D3DCOLOR*)destBits;
					for (int x = 0; x < destWidth; x++)
					{
						D3DCOLOR srcPixel = srcLineStart[sampleTableHorizon[x]];
						D3DCOLOR& destPixel = destLineStart[x];

						destPixel = srcPixel;
					}
					srcBits = (char*)srcLockedRect.pBits + srcLockedRect.Pitch * sampleTableVertical[y];
					destBits = (char*)destBits + destPitch;
				}
				srcSurface9->UnlockRect();
			}
		}

		delete sampleTableHorizon; sampleTableHorizon = nullptr;
		delete sampleTableVertical; sampleTableVertical = nullptr;

		ND3D9::D3DLOCKED_RECT destLockedRect;
		if (SUCCEEDED(hr = destSurface9->LockRect(&destLockedRect, lpDestRect, 0)))
		{
			int srcPitch = destWidth * sizeof(D3DCOLOR);
			char* srcBits = (char*)srcSampled;
			char* destBits = (char*)destLockedRect.pBits;
			for (int y = 0; y < destHeight; y++)
			{
				D3DCOLOR* srcLineStart = (D3DCOLOR*)srcBits;
				D3DCOLOR* destLineStart = (D3DCOLOR*)destBits;
				lineCallBack(srcLineStart, destLineStart, destWidth);
				srcBits = (char*)srcBits + srcPitch;
				destBits = (char*)destBits + destLockedRect.Pitch;
			}
			destSurface9->UnlockRect();
		}

		delete srcSampled;
		srcSampled = nullptr;

		return hr;
	}
	virtual HRESULT BltFast(ISurface9Wrapper* srcSurface9Wrapper, LPRECT lpSrcRect, DWORD destX, DWORD destY, D3DCOLOR* srcColorKey, D3DCOLOR* destColorKey) override
	{
		HRESULT hr = DDERR_GENERIC;

		assert(srcSurface9Wrapper->GetImplClassName() == this->GetImplClassName());
		auto srcSurface9 = srcSurface9Wrapper->GetSurface9();

		std::function<void(D3DCOLOR* srcLine, D3DCOLOR* destLine, int x)> lineCallBack;

		using namespace std::placeholders;

		if (srcColorKey && !destColorKey)
		{
			lineCallBack = std::bind(&LineProcess<true, false>, _1, _2, _3, *srcColorKey, 0);
		}
		else if (!srcColorKey && destColorKey)
		{
			lineCallBack = std::bind(&LineProcess<false, true>, _1, _2, _3, 0, *destColorKey);
		}
		else if (srcColorKey && destColorKey)
		{
			lineCallBack = std::bind(&LineProcess<true, true>, _1, _2, _3, *srcColorKey, *destColorKey);
		}
		else
		{
			lineCallBack = std::bind(&LineProcess<false, false>, _1, _2, _3, 0, 0);
		}

		auto destSurface9 = GetSurface9();

		int width = lpSrcRect->right - lpSrcRect->left;
		int height = lpSrcRect->bottom - lpSrcRect->top;

		RECT destRect = { 0 };
		destRect.left = destX;
		destRect.right = destX + width;
		destRect.top = destY;
		destRect.bottom = destY + height;

		ND3D9::D3DLOCKED_RECT srcLockedRect;
		if (SUCCEEDED(hr = srcSurface9->LockRect(&srcLockedRect, lpSrcRect, D3DLOCK_READONLY)))
		{
			ND3D9::D3DLOCKED_RECT destLockedRect;
			if (SUCCEEDED(hr = destSurface9->LockRect(&destLockedRect, &destRect, 0)))
			{
				char* srcBits = (char*)srcLockedRect.pBits;
				char* destBits = (char*)destLockedRect.pBits;
				for (int y = 0; y < height; y++)
				{
					D3DCOLOR* srcLineStart = (D3DCOLOR*)srcBits;
					D3DCOLOR* destLineStart = (D3DCOLOR*)destBits;
					lineCallBack(srcLineStart, destLineStart, width);
					srcBits += srcLockedRect.Pitch;
					destBits += destLockedRect.Pitch;
				}


				destSurface9->UnlockRect();
			}

			srcSurface9->UnlockRect();
		}

		return hr;
	}

	virtual HRESULT FillColor(LPRECT rect, D3DCOLOR color) override
	{
		HRESULT hr = DDERR_GENERIC;
		auto surface9 = GetSurface9();
		ND3D9::D3DLOCKED_RECT destLockedRect;

		int destWidth = rect->right - rect->left;
		int destHeight = rect->bottom - rect->top;
		if (SUCCEEDED(hr = surface9->LockRect(&destLockedRect, rect, 0)))
		{
			char* destBits = (char*)destLockedRect.pBits;
			for (int y = 0; y < destHeight; y++)
			{
				D3DCOLOR* destLineStart = (D3DCOLOR*)destBits;
				for (int x = 0; x < destWidth; x++)
				{
					D3DCOLOR& destPixel = destLineStart[x];

					destPixel = color;
				}
				destBits = (char*)destBits + destLockedRect.Pitch;
			}

			surface9->UnlockRect();
		}

		return hr;
	}

private:
	ND3D9::D3D9Context* m_d3d9Context;
	ND3D9::Resource9Handle m_surface9Handle;
	ESurfaceType m_surfaceType;
};

m_IDirectDrawSurface4::m_IDirectDrawSurface4(IDirectDrawSurface4 *aOriginal, DDSURFACEDESC2 desc, m_IDirectDrawSurface4* linkedPrevSurface, std::shared_ptr<WrapperLookupTable<void>> wrapperAddressLookupTable)
	: ProxyInterface(aOriginal)
	, m_desc(desc)
	, Refs(1)
	, WrapperAddressLookupTable(wrapperAddressLookupTable)
	, m_surfaceType(ESurfaceType::Unknown)
	, m_surface9Wrapper(nullptr)
	, m_clipper(nullptr)
	, m_linkedPrevSurface(linkedPrevSurface)
	, m_linkedNextSurface(nullptr)
	, m_tex2(nullptr)
	, m_locked(false)
{
	ProxyAddressLookupTable.SaveAddress(this, ProxyInterface);

	DWORD dwCaps = m_desc.ddsCaps.dwCaps;
	if (dwCaps & DDSCAPS_OFFSCREENPLAIN)
	{
		m_surfaceType = ESurfaceType::OffScreen;
	}
	else if (dwCaps & DDSCAPS_PRIMARYSURFACE)
	{
		assert(m_surfaceType == ESurfaceType::Unknown);
		m_surfaceType = ESurfaceType::Primary;
	}
	else if (dwCaps & DDSCAPS_TEXTURE)
	{
		assert(m_surfaceType == ESurfaceType::Unknown);
		m_surfaceType = ESurfaceType::Texture;
	}
	else if (dwCaps & DDSCAPS_OVERLAY)
	{
		assert(m_surfaceType == ESurfaceType::Unknown);
		m_surfaceType = ESurfaceType::Overlay;
	}
	else if (dwCaps & DDSCAPS_BACKBUFFER)
	{
		assert(m_surfaceType == ESurfaceType::Unknown);
		m_surfaceType = ESurfaceType::BackBuffer;
	}
	else if (dwCaps & DDSCAPS_ZBUFFER)
	{
		assert(m_surfaceType == ESurfaceType::Unknown);
		m_surfaceType = ESurfaceType::ZBuffer;
	}
	else
	{
		assert(false);
	}


	HRESULT hr = DDERR_GENERIC;
	switch (m_surfaceType)
	{
	case ESurfaceType::OffScreen:
	{
		if (m_desc.ddsCaps.dwCaps & DDSCAPS_3DDEVICE)
		{
			int haha = 0;
			haha = 1;
		}

		bool createInSysMem = m_desc.ddsCaps.dwCaps & DDSCAPS_SYSTEMMEMORY;
		createInSysMem = true;
		m_surface9Wrapper = new SoftwareSurface9Wrapper(ND3D9::D3D9Context::Instance(), m_desc.dwWidth, m_desc.dwHeight, ND3D9::D3DFMT_X8R8G8B8, m_surfaceType);
		break;
	}
	case ESurfaceType::Primary:
	{
		int width = 0;
		int height = 0;
		ND3D9::D3D9Context::Instance()->GetBackBufferSize(&width, &height);
		m_desc.dwWidth = width;
		m_desc.dwHeight = height;

		// code is taken from project DXGL
		if (m_desc.ddsCaps.dwCaps & DDSCAPS_COMPLEX)
		{
			if (m_desc.ddsCaps.dwCaps & DDSCAPS_FLIP)
			{
				if ((m_desc.dwFlags & DDSD_BACKBUFFERCOUNT) && (m_desc.dwBackBufferCount > 0))
				{
					if (!(m_desc.ddsCaps.dwCaps & DDSCAPS_BACKBUFFER))	
						m_desc.ddsCaps.dwCaps |= DDSCAPS_FRONTBUFFER;
					m_surface9Wrapper = new SoftwareSurface9Wrapper(ND3D9::D3D9Context::Instance(), m_desc.dwWidth, m_desc.dwHeight, ND3D9::D3DFMT_X8R8G8B8, m_surfaceType);

					{
						DDSURFACEDESC2 ddsdBack;
						memcpy(&ddsdBack, &m_desc, m_desc.dwSize);
						ddsdBack.dwBackBufferCount--;
						ddsdBack.ddsCaps.dwCaps |= DDSCAPS_BACKBUFFER;
						ddsdBack.ddsCaps.dwCaps &= ~DDSCAPS_FRONTBUFFER;
						auto backbuffer = new m_IDirectDrawSurface4(nullptr, ddsdBack, this, WrapperAddressLookupTable);
						m_linkedNextSurface = backbuffer;
					}
				}
				else if (m_desc.dwFlags & DDSD_BACKBUFFERCOUNT)
				{
					m_surfaceType = ESurfaceType::BackBuffer;
					m_surface9Wrapper = new SoftwareSurface9Wrapper(ND3D9::D3D9Context::Instance(), m_desc.dwWidth, m_desc.dwHeight, ND3D9::D3DFMT_A8R8G8B8, m_surfaceType);
				}
				else
				{
					assert(false);
				}
			}
		}
		else
		{
			m_surface9Wrapper = new SoftwareSurface9Wrapper(ND3D9::D3D9Context::Instance(), m_desc.dwWidth, m_desc.dwHeight, ND3D9::D3DFMT_A8R8G8B8, m_surfaceType);
		}
		break;
	}
	case ESurfaceType::Texture:
	{
		auto ddraw4 = WrapperAddressLookupTable->FindWrapper<m_IDirectDraw4>(IID_IDirectDraw4);

		if ((!(dwCaps & (DDSCAPS_MIPMAP))) ||
			((dwCaps & (DDSCAPS_MIPMAP)) && !(m_desc.ddsCaps.dwCaps2 & DDSCAPS2_MIPMAPSUBLEVEL))
			)
		{
			m_tex2 = new m_IDirect3DTexture2(nullptr, this);
			ddraw4->Tex9LookupTable->SaveAddress(this, m_tex2->GetTexture9());
		}

		if (
			((dwCaps & (DDSCAPS_MIPMAP)) && !(m_desc.ddsCaps.dwCaps2 & DDSCAPS2_MIPMAPSUBLEVEL))
			)
		{
			auto linkedPrevSurface = this;
			for (DWORD mipmapIndex = 1; mipmapIndex < m_desc.dwMipMapCount; mipmapIndex++)
			{
				DDSURFACEDESC2 newDesc = desc;
				newDesc.ddsCaps.dwCaps2 |= DDSCAPS2_MIPMAPSUBLEVEL;

				ND3D9::D3DSURFACE_DESC desc9;
				m_tex2->GetTexture9()->GetLevelDesc(mipmapIndex, &desc9);
				newDesc.dwWidth = desc9.Width;
				newDesc.dwHeight = desc9.Height;

				linkedPrevSurface->m_linkedNextSurface = new m_IDirectDrawSurface4(nullptr, newDesc, linkedPrevSurface, WrapperAddressLookupTable);
				linkedPrevSurface = linkedPrevSurface->m_linkedNextSurface;
			}
		}

		break;
	}
	case ESurfaceType::Overlay:
		break;
	case ESurfaceType::BackBuffer:
		break;
	case ESurfaceType::ZBuffer:
	{
		int width = 0;
		int height = 0;
		ND3D9::D3D9Context::Instance()->GetBackBufferSize(&width, &height);
		m_desc.dwWidth = width;
		m_desc.dwHeight = height;
		m_surface9Wrapper = new SoftwareSurface9Wrapper(ND3D9::D3D9Context::Instance(), width, height, ND3D9::D3DFMT_D16, m_surfaceType);
		break;
	}
	default:
		break;
	}
}

m_IDirectDrawSurface4::~m_IDirectDrawSurface4()
{
	ProxyAddressLookupTable.DeleteAddress(this);
	if (m_clipper)
	{
		m_clipper->Release();
		m_clipper = nullptr;
	}
	if (m_linkedNextSurface)
	{
		m_linkedNextSurface->Release();
		m_linkedNextSurface = nullptr;
	}
	if (m_tex2)
	{
		if (m_tex2->GetTexture9())
		{
			auto ddraw4 = WrapperAddressLookupTable->FindWrapperOnly<m_IDirectDraw4>(IID_IDirectDraw4);
			if (ddraw4)
			{
				ddraw4->Tex9LookupTable->DeleteAddress(this);
			}
		}
		else
		{
			assert(false);
		}
		m_tex2->Release();
		m_tex2 = nullptr;
	}
	if (m_surface9Wrapper)
	{
		delete m_surface9Wrapper;
		m_surface9Wrapper = 0;
	}
}

SmartPtr<ND3D9::IDirect3DSurface9> m_IDirectDrawSurface4::GetSurface9() const
{
	return m_surface9Wrapper->GetSurface9();
}

HRESULT m_IDirectDrawSurface4::QueryInterface(REFIID riid, LPVOID FAR * ppvObj)
{
	if ((riid == IID_IDirectDrawSurface4 || riid == IID_IUnknown) && ppvObj)
	{
		AddRef();

		*ppvObj = this;

		return S_OK;
	}

	if (riid == IID_IDirect3DTexture2)
	{
		assert(m_surfaceType == ESurfaceType::Texture);
		m_tex2->AddRef();
		*ppvObj = m_tex2;
		return DD_OK;
	}
	else
	{
		HRESULT hr = ProxyInterface->QueryInterface(riid, ppvObj);

		if (SUCCEEDED(hr))
		{
			genericQueryInterface(riid, ppvObj);
		}

		return hr;
	}
}

ULONG m_IDirectDrawSurface4::AddRef()
{
	return ++Refs;
}

ULONG m_IDirectDrawSurface4::Release()
{
	ULONG x = --Refs;

	if (x == 0)
	{
		delete this;
	}

	return x;
}

HRESULT m_IDirectDrawSurface4::AddAttachedSurface(LPDIRECTDRAWSURFACE4 a)
{
	assert(!m_linkedNextSurface);
	auto suffaceZBuffer = static_cast<m_IDirectDrawSurface4*>(a);
	assert(suffaceZBuffer->m_surfaceType == ESurfaceType::ZBuffer);
	suffaceZBuffer->AddRef();
	m_linkedNextSurface = suffaceZBuffer;
	auto zbuffer9 = suffaceZBuffer->GetSurface9();
	ND3D9::D3D9Context::Instance()->GetDevice()->SetDepthStencilSurface(zbuffer9);
	return DD_OK;
}

HRESULT m_IDirectDrawSurface4::AddOverlayDirtyRect(LPRECT a)
{
	return ProxyInterface->AddOverlayDirtyRect(a);
}

HRESULT m_IDirectDrawSurface4::Blt(LPRECT lpDestRect, LPDIRECTDRAWSURFACE4 lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwFlags, LPDDBLTFX lpDDBltFx)
{
	auto device9 = ND3D9::D3D9Context::Instance()->GetDevice();

	if (!(dwFlags & DDBLT_COLORFILL) && !lpDDSrcSurface)
	{
		return DDERR_INVALIDPARAMS;
	}

	m_IDirectDrawSurface4* srcSurface = static_cast<m_IDirectDrawSurface4*>(lpDDSrcSurface);
	m_IDirectDrawSurface4* destSurface = this;
	SmartPtr<ND3D9::IDirect3DSurface9> srcSurface9;
	SmartPtr<ND3D9::IDirect3DSurface9> destSurface9;

	int destWidth = lpDestRect ? (lpDestRect->right - lpDestRect->left) : destSurface->m_desc.dwWidth;
	int destHeight = lpDestRect ? (lpDestRect->bottom - lpDestRect->top) : destSurface->m_desc.dwHeight;
	
	RECT destRectOverride = { 0 };

	if (lpDestRect)
	{
		destRectOverride = *lpDestRect;
	}
	else
	{
		destRectOverride.left = 0;
		destRectOverride.top = 0;
		destRectOverride.right = destWidth;
		destRectOverride.bottom = destHeight;
	}

	if (dwFlags & DDBLT_COLORFILL)
	{
		D3DCOLOR color = lpDDBltFx->dwFillColor;
		if (m_surfaceType == ESurfaceType::Primary)
		{
			if (m_desc.ddsCaps.dwCaps & DDSCAPS_COMPLEX)
			{
				auto backBuffer9 = m_linkedNextSurface->GetSurface9();
				destSurface9 = backBuffer9;
			}
			else
			{
				destSurface9 = GetSurface9();
			}
		}
		else if (m_surfaceType == ESurfaceType::OffScreen)
		{
			destSurface9 = GetSurface9();
		}
		else if (m_surfaceType == ESurfaceType::BackBuffer)
		{
			destSurface9 = GetSurface9();
		}
		else
		{
			assert(false);
		}
		
		m_surface9Wrapper->FillColor(&destRectOverride, color);

		return DD_OK;
	}

	int srcWidth = lpSrcRect ? (lpSrcRect->right - lpSrcRect->left) : srcSurface->m_desc.dwWidth;
	int srcHeight = lpSrcRect ? (lpSrcRect->bottom - lpSrcRect->top) : srcSurface->m_desc.dwHeight;

	RECT srcRectOverride = { 0 };
	if (lpSrcRect)
	{
		srcRectOverride = *lpSrcRect;
	}
	else
	{
		srcRectOverride.left = 0;
		srcRectOverride.top = 0;
		srcRectOverride.right = srcWidth;
		srcRectOverride.bottom = srcHeight;
	}

	if (TRUE)
	{
		// correct dest rect offset;
		if (m_surfaceType == ESurfaceType::Primary)
		{
			int offsetX = destRectOverride.left;
			int offsetY = destRectOverride.top;
			destRectOverride.left = 0;
			destRectOverride.top = 0;
			destRectOverride.right = destRectOverride.right - offsetX;
			destRectOverride.bottom = destRectOverride.bottom - offsetY;
		}
	}

	srcSurface9 = srcSurface->GetSurface9();

	destSurface = this;
	destSurface9 = destSurface->GetSurface9();

	D3DCOLOR* srcColorKey = nullptr;
	D3DCOLOR* destColorKey = nullptr;

	if (dwFlags & DDBLT_KEYSRC)
	{
		srcColorKey = &srcSurface->m_desc.ddckCKSrcBlt.dwColorSpaceLowValue;
	}
	if (dwFlags & DDBLT_KEYDEST)
	{
		destColorKey = &destSurface->m_desc.ddckCKDestBlt.dwColorSpaceLowValue;
	}

	if (dwFlags & DDBLT_KEYSRCOVERRIDE)
	{
		srcColorKey = &lpDDBltFx->ddckSrcColorkey.dwColorSpaceLowValue;
	}
	if (dwFlags & DDBLT_KEYDESTOVERRIDE)
	{
		destColorKey = &lpDDBltFx->ddckDestColorkey.dwColorSpaceLowValue;
	}

	if (((dwFlags & DDBLT_ALPHASRC) || (dwFlags & DDBLT_ALPHADEST)))
	{
		assert(false);
	}

	m_surface9Wrapper->Blt(srcSurface->m_surface9Wrapper, &srcRectOverride, &destRectOverride, srcColorKey, destColorKey);

	if (m_surfaceType == ESurfaceType::Primary)
	{
		if (D3DERR_DEVICELOST == device9->Present(&destRectOverride, &destRectOverride, 0, nullptr))
		{
			ND3D9::D3D9Context::Instance()->TagDeviceLost();
			return DDERR_SURFACELOST;
		}
		else
		{
			srcSurface->m_surface9Wrapper->FillColor(&srcRectOverride, 0);
		}
	}
	return DD_OK;
}

HRESULT m_IDirectDrawSurface4::BltBatch(LPDDBLTBATCH a, DWORD b, DWORD c)
{
	if (a && a->lpDDSSrc)
	{
		a->lpDDSSrc = static_cast<m_IDirectDrawSurface *>(a->lpDDSSrc)->GetProxyInterface();
	}

	return ProxyInterface->BltBatch(a, b, c);
}

HRESULT m_IDirectDrawSurface4::BltFast(DWORD dwX, DWORD dwY, LPDIRECTDRAWSURFACE4 lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwFlags)
{
	if (!lpDDSrcSurface || !lpSrcRect)
	{
		return DDERR_INVALIDPARAMS;
	}

	m_IDirectDrawSurface4* srcSurface = static_cast<m_IDirectDrawSurface4*>(lpDDSrcSurface);
	if (m_surfaceType == ESurfaceType::OffScreen || m_surfaceType == ESurfaceType::BackBuffer)
	{
		m_IDirectDrawSurface4* destSurface = this;

		D3DCOLOR* srcColorKey = nullptr;
		D3DCOLOR* destColorKey = nullptr;

		if (dwFlags & DDBLTFAST_SRCCOLORKEY)
		{
			srcColorKey = &srcSurface->m_desc.ddckCKSrcBlt.dwColorSpaceLowValue;
		}
		if (dwFlags & DDBLTFAST_DESTCOLORKEY)
		{
			destColorKey = &destSurface->m_desc.ddckCKDestBlt.dwColorSpaceLowValue;
		}
		
		HRESULT hr = m_surface9Wrapper->BltFast(srcSurface->m_surface9Wrapper, lpSrcRect, dwX, dwY, srcColorKey, destColorKey);

		return hr;
	}
	else
	{
		assert(false);
	}
	return DDERR_GENERIC;
}

HRESULT m_IDirectDrawSurface4::DeleteAttachedSurface(DWORD a, LPDIRECTDRAWSURFACE4 b)
{
	if (b)
	{
		b = static_cast<m_IDirectDrawSurface4 *>(b)->GetProxyInterface();
	}

	return ProxyInterface->DeleteAttachedSurface(a, b);
}

HRESULT m_IDirectDrawSurface4::EnumAttachedSurfaces(LPVOID a, LPDDENUMSURFACESCALLBACK2 b)
{
	ENUMSURFACE2 CallbackContext;
	CallbackContext.lpContext = a;
	CallbackContext.lpCallback = b;

	return ProxyInterface->EnumAttachedSurfaces(&CallbackContext, m_IDirectDrawEnumSurface2::EnumSurface2Callback);
}

HRESULT m_IDirectDrawSurface4::EnumOverlayZOrders(DWORD a, LPVOID b, LPDDENUMSURFACESCALLBACK2 c)
{
	ENUMSURFACE2 CallbackContext;
	CallbackContext.lpContext = b;
	CallbackContext.lpCallback = c;

	return ProxyInterface->EnumOverlayZOrders(a, &CallbackContext, m_IDirectDrawEnumSurface2::EnumSurface2Callback);
}

HRESULT m_IDirectDrawSurface4::Flip(LPDIRECTDRAWSURFACE4 a, DWORD b)
{
	if (a)
	{
		a = static_cast<m_IDirectDrawSurface4 *>(a)->GetProxyInterface();
	}

	return ProxyInterface->Flip(a, b);
}

HRESULT m_IDirectDrawSurface4::GetAttachedSurface(LPDDSCAPS2 a, LPDIRECTDRAWSURFACE4 FAR * b)
{
	if (m_linkedNextSurface)
	{
		LPDDSCAPS2 nextSurfaceCaps = &m_linkedNextSurface->m_desc.ddsCaps;
		if ((nextSurfaceCaps->dwCaps & a->dwCaps) == a->dwCaps)
		{
			if (nextSurfaceCaps->dwCaps & DDSCAPS_MIPMAP)
			{
				if (nextSurfaceCaps->dwCaps2 & DDSCAPS2_MIPMAPSUBLEVEL)
				{
					*b = m_linkedNextSurface;
					m_linkedNextSurface->AddRef();
					return DD_OK;
				}
				else
				{
					// impossible run to here?
					assert(false);
					return DDERR_NOTFOUND;
				}
			}
			else if (nextSurfaceCaps->dwCaps & DDSCAPS_BACKBUFFER)
			{
				*b = m_linkedNextSurface;
				m_linkedNextSurface->AddRef();
				return DD_OK;
			}
			else
			{
				assert(false);
				return DDERR_NOTFOUND;
			}
		}
		else
		{
			*b = nullptr;
			return DDERR_NOTFOUND;
		}
	}
	else
	{
		*b = nullptr;
		return DDERR_NOTFOUND;
	}

}

HRESULT m_IDirectDrawSurface4::GetBltStatus(DWORD a)
{
	return ProxyInterface->GetBltStatus(a);
}

HRESULT m_IDirectDrawSurface4::GetCaps(LPDDSCAPS2 a)
{
	return ProxyInterface->GetCaps(a);
}

HRESULT m_IDirectDrawSurface4::GetClipper(LPDIRECTDRAWCLIPPER FAR * a)
{
	if (m_clipper)
	{
		m_clipper->AddRef();
		*a = m_clipper;
		return DD_OK;
	}
	else
	{
		*a = nullptr;
		return DDERR_NOCLIPPERATTACHED;
	}
}

HRESULT m_IDirectDrawSurface4::GetColorKey(DWORD a, LPDDCOLORKEY b)
{
	HRESULT hr = DDERR_NOCOLORKEY;
	if (b)
	{
		switch (a)
		{
		case DDCKEY_DESTBLT:
			if (m_desc.dwFlags & DDSD_CKDESTBLT)
			{
				*b = m_desc.ddckCKDestBlt;
				hr = DD_OK;
			}
			break;
		case DDCKEY_DESTOVERLAY:
			if (m_desc.dwFlags & DDSD_CKDESTOVERLAY)
			{
				*b = m_desc.ddckCKDestOverlay;
				hr = DD_OK;
			}
			break;
		case DDCKEY_SRCBLT:
			if (m_desc.dwFlags & DDSD_CKSRCBLT)
			{
				*b = m_desc.ddckCKSrcBlt;
				hr = DD_OK;
			}
			break;
		case DDCKEY_SRCOVERLAY:
			if (m_desc.dwFlags & DDSD_CKSRCOVERLAY)
			{
				*b = m_desc.ddckCKSrcOverlay;
				hr = DD_OK;
			}
			break;
		default:
			assert(false);
			break;
		}

	}
	else
	{
		hr = DDERR_INVALIDPARAMS;
	}
	return hr;
}

HRESULT m_IDirectDrawSurface4::GetDC(HDC FAR * a)
{
	// TODO: IDirect3DSurface9::GetDC() 在有ALPHA通道时，行为不确定
	return GetSurface9()->GetDC(a);
}

HRESULT m_IDirectDrawSurface4::GetFlipStatus(DWORD a)
{
	return ProxyInterface->GetFlipStatus(a);
}

HRESULT m_IDirectDrawSurface4::GetOverlayPosition(LPLONG a, LPLONG b)
{
	return ProxyInterface->GetOverlayPosition(a, b);
}

HRESULT m_IDirectDrawSurface4::GetPalette(LPDIRECTDRAWPALETTE FAR * a)
{
	HRESULT hr = ProxyInterface->GetPalette(a);

	if (SUCCEEDED(hr))
	{
		*a = ProxyAddressLookupTable.FindAddress<m_IDirectDrawPalette>(*a);
	}

	return hr;
}

HRESULT m_IDirectDrawSurface4::GetPixelFormat(LPDDPIXELFORMAT a)
{
	return ProxyInterface->GetPixelFormat(a);
}

HRESULT m_IDirectDrawSurface4::GetSurfaceDesc(LPDDSURFACEDESC2 a)
{
	*a = m_desc;
	return DD_OK;
}

HRESULT m_IDirectDrawSurface4::Initialize(LPDIRECTDRAW a, LPDDSURFACEDESC2 b)
{
	if (a)
	{
		a = static_cast<m_IDirectDraw *>(a)->GetProxyInterface();
	}

	return ProxyInterface->Initialize(a, b);
}

HRESULT m_IDirectDrawSurface4::IsLost()
{
	if (m_surfaceType == ESurfaceType::Primary &&
		ND3D9::D3D9Context::Instance()->IsDeviceLost()
		)
	{
		return DDERR_SURFACELOST;
	}
	else
	{
		return DD_OK;
	}
}

HRESULT m_IDirectDrawSurface4::Lock(LPRECT lpDestRect, LPDDSURFACEDESC2 lpDDSurfaceDesc, DWORD dwFlags, HANDLE hEvent)
{
	// TODO: https://msdn.microsoft.com/en-us/library/windows/desktop/gg426221(v=vs.85).aspx
	// Because you can call IDirectDrawSurface7::Lock multiple times for the same surface with different destination rectangles, the pointer in lpRect links the calls to the IDirectDrawSurface7::Lock and IDirectDrawSurface7::Unlock methods.

	if (m_locked)
	{
		return DDERR_GENERIC;
	}

	HRESULT hr = DDERR_GENERIC;

	bool readonly = dwFlags & DDLOCK_READONLY;
	bool nosyslock = dwFlags & DDLOCK_NOSYSLOCK;
	bool discard = dwFlags & DDLOCK_DISCARDCONTENTS;
	DWORD lockFlags =
		(readonly ? D3DLOCK_READONLY : 0) |
		(nosyslock ? D3DLOCK_NOSYSLOCK : 0) |
		(discard ? D3DLOCK_DISCARD : 0);

	if (m_surfaceType == ESurfaceType::OffScreen)
	{
		ND3D9::D3DLOCKED_RECT lockedRect = { 0 };
		if (SUCCEEDED(GetSurface9()->LockRect(&lockedRect, lpDestRect, lockFlags)))
		{
			*lpDDSurfaceDesc = m_desc;
			lpDDSurfaceDesc->lpSurface = lockedRect.pBits;
			lpDDSurfaceDesc->lPitch = lockedRect.Pitch;
			hr = DD_OK;
		}
		else
		{
			hr = DDERR_INVALIDPARAMS;
		}
	}
	else if (m_surfaceType == ESurfaceType::Texture)
	{
		ND3D9::D3DLOCKED_RECT lockedRect = { 0 };
		int level = 0;
		SmartPtr<ND3D9::IDirect3DTexture9> tex9;
		if (m_desc.ddsCaps.dwCaps & DDSCAPS_MIPMAP)
		{
			if (m_desc.ddsCaps.dwCaps2 & DDSCAPS2_MIPMAPSUBLEVEL)
			{
				auto curSurface = this;
				while (curSurface && curSurface->m_linkedPrevSurface)
				{
					level += 1;
					curSurface = curSurface->m_linkedPrevSurface;
				}
				tex9 = curSurface->m_tex2->GetTexture9();
			}
			else
			{
				tex9 = m_tex2->GetTexture9();
			}
		}
		else
		{
			tex9 = m_tex2->GetTexture9();
		}

		if (SUCCEEDED(tex9->LockRect(level, &lockedRect, lpDestRect, lockFlags)))
		{
			*lpDDSurfaceDesc = m_desc;
			lpDDSurfaceDesc->lpSurface = lockedRect.pBits;
			lpDDSurfaceDesc->lPitch = lockedRect.Pitch;
			hr = DD_OK;
		}
		else
		{
			hr = DDERR_INVALIDPARAMS;
		}
	}
	else if (m_surfaceType == ESurfaceType::BackBuffer)
	{
		ND3D9::D3DLOCKED_RECT lockedRect = { 0 };
		if (SUCCEEDED(GetSurface9()->LockRect(&lockedRect, lpDestRect, lockFlags)))
		{
			*lpDDSurfaceDesc = m_desc;
			lpDDSurfaceDesc->lpSurface = lockedRect.pBits;
			lpDDSurfaceDesc->lPitch = lockedRect.Pitch;
			hr = DD_OK;
		}
		else
		{
			hr = DDERR_INVALIDPARAMS;
		}
	}
	else
	{
		assert(false);
		hr = DDERR_GENERIC;
	}
	
	if (SUCCEEDED(hr))
	{
		m_locked = true;
	}
	return hr;
}

HRESULT m_IDirectDrawSurface4::ReleaseDC(HDC a)
{
	return GetSurface9()->ReleaseDC(a);
}

HRESULT m_IDirectDrawSurface4::Restore()
{
	if (m_surfaceType == ESurfaceType::Primary)
	{
		assert(ND3D9::D3D9Context::Instance()->IsDeviceLost());
		if (!SUCCEEDED(ND3D9::D3D9Context::Instance()->ResetDevice()))
		{
			return DDERR_GENERIC;
		}
		else
		{
			return DD_OK;
		}
	}
	else if (m_surfaceType == ESurfaceType::ZBuffer)
	{
		auto zbuffer9 = GetSurface9();
		ND3D9::D3D9Context::Instance()->GetDevice()->SetDepthStencilSurface(zbuffer9);
		return DD_OK;
	}
	else
	{
		return DD_OK;
	}
}

HRESULT m_IDirectDrawSurface4::SetClipper(LPDIRECTDRAWCLIPPER a)
{
	if (a)
	{
		if (m_clipper)
		{
			assert(m_clipper == a);
		}
		a->AddRef();
		m_clipper = static_cast<m_IDirectDrawClipper*>(a);
		return DD_OK;
	}
	else
	{
		if (m_clipper)
		{
			m_clipper->Release();
			m_clipper = nullptr;
		}
		return DD_OK;
	}

}

HRESULT m_IDirectDrawSurface4::SetColorKey(DWORD a, LPDDCOLORKEY b)
{
	if (b)
	{
		switch (a)
		{
		case DDCKEY_COLORSPACE:
			break;
		case DDCKEY_DESTBLT:
			m_desc.ddckCKDestBlt = *b;
			m_desc.dwFlags |= DDSD_CKDESTBLT;
			break;
		case DDCKEY_DESTOVERLAY:
			m_desc.ddckCKDestOverlay = *b;
			m_desc.dwFlags |= DDSD_CKDESTOVERLAY;
			break;
		case DDCKEY_SRCBLT:
			m_desc.ddckCKSrcBlt = *b;
			m_desc.dwFlags |= DDSD_CKSRCBLT;
			break;
		case DDCKEY_SRCOVERLAY:
			m_desc.ddckCKSrcOverlay = *b;
			m_desc.dwFlags |= DDSD_CKSRCOVERLAY;
			break;
		default:
			assert(false);
			break;
		}
	}
	else
	{
		switch (a)
		{
		case DDCKEY_COLORSPACE:
			break;
		case DDCKEY_DESTBLT:
			m_desc.ddckCKDestBlt = DDCOLORKEY{ 0xff000000, 0xff000000 };
			break;
		case DDCKEY_DESTOVERLAY:
			m_desc.ddckCKDestOverlay = DDCOLORKEY{ 0xff000000, 0xff000000 };
			break;
		case DDCKEY_SRCBLT:
			m_desc.ddckCKSrcBlt = DDCOLORKEY{ 0xff000000, 0xff000000 };
			break;
		case DDCKEY_SRCOVERLAY:
			m_desc.ddckCKSrcOverlay = DDCOLORKEY{ 0xff000000, 0xff000000 };
			break;
		default:
			assert(false);
			break;
		}
	}

	return DD_OK;
}

HRESULT m_IDirectDrawSurface4::SetOverlayPosition(LONG a, LONG b)
{
	return ProxyInterface->SetOverlayPosition(a, b);
}

HRESULT m_IDirectDrawSurface4::SetPalette(LPDIRECTDRAWPALETTE a)
{
	if (a)
	{
		a = static_cast<m_IDirectDrawPalette *>(a)->GetProxyInterface();
	}

	return ProxyInterface->SetPalette(a);
}

HRESULT m_IDirectDrawSurface4::Unlock(LPRECT a)
{
	if (!m_locked)
	{
		return DDERR_GENERIC;
	}

	HRESULT hr = DDERR_GENERIC;

	if (m_surfaceType == ESurfaceType::OffScreen)
	{
		if (SUCCEEDED(GetSurface9()->UnlockRect()))
		{
			hr = DD_OK;
		}
		else
		{
			hr = DDERR_INVALIDPARAMS;
		}
	}
	else if (m_surfaceType == ESurfaceType::Texture)
	{
		int level = 0;
		SmartPtr<ND3D9::IDirect3DTexture9> tex9 = nullptr;
		if (m_desc.ddsCaps.dwCaps & DDSCAPS_MIPMAP)
		{
			if (m_desc.ddsCaps.dwCaps2 & DDSCAPS2_MIPMAPSUBLEVEL)
			{
				auto curSurface = this;
				while (curSurface && curSurface->m_linkedPrevSurface)
				{
					level += 1;
					curSurface = curSurface->m_linkedPrevSurface;
				}
				tex9 = curSurface->m_tex2->GetTexture9();
			}
			else
			{
				tex9 = m_tex2->GetTexture9();
			}
		}
		else
		{
			tex9 = m_tex2->GetTexture9();
		}


		if (SUCCEEDED(tex9->UnlockRect(level)))
		{
			hr = DD_OK;
		}
		else
		{
			hr = DDERR_INVALIDPARAMS;
		}
	}
	else if (m_surfaceType == ESurfaceType::BackBuffer)
	{
		if (SUCCEEDED(GetSurface9()->UnlockRect()))
		{
			hr = DD_OK;
		}
		else
		{
			hr = DDERR_INVALIDPARAMS;
		}
	}
	else
	{
		hr = DDERR_GENERIC;
	}

	if (SUCCEEDED(hr))
	{
		m_locked = false;
	}
	return hr;
}

HRESULT m_IDirectDrawSurface4::UpdateOverlay(LPRECT a, LPDIRECTDRAWSURFACE4 b, LPRECT c, DWORD d, LPDDOVERLAYFX e)
{
	if (b)
	{
		b = static_cast<m_IDirectDrawSurface4 *>(b)->GetProxyInterface();
	}

	return ProxyInterface->UpdateOverlay(a, b, c, d, e);
}

HRESULT m_IDirectDrawSurface4::UpdateOverlayDisplay(DWORD a)
{
	return ProxyInterface->UpdateOverlayDisplay(a);
}

HRESULT m_IDirectDrawSurface4::UpdateOverlayZOrder(DWORD a, LPDIRECTDRAWSURFACE4 b)
{
	if (b)
	{
		b = static_cast<m_IDirectDrawSurface4 *>(b)->GetProxyInterface();
	}

	return ProxyInterface->UpdateOverlayZOrder(a, b);
}

HRESULT m_IDirectDrawSurface4::GetDDInterface(LPVOID FAR * a)
{
	*a = WrapperAddressLookupTable->FindWrapper<m_IDirectDraw4>(IID_IDirectDraw4);
	return DD_OK;
}

HRESULT m_IDirectDrawSurface4::PageLock(DWORD a)
{
	return ProxyInterface->PageLock(a);
}

HRESULT m_IDirectDrawSurface4::PageUnlock(DWORD a)
{
	return ProxyInterface->PageUnlock(a);
}

HRESULT m_IDirectDrawSurface4::SetSurfaceDesc(LPDDSURFACEDESC2 a, DWORD b)
{
	return ProxyInterface->SetSurfaceDesc(a, b);
}

HRESULT m_IDirectDrawSurface4::SetPrivateData(REFGUID a, LPVOID b, DWORD c, DWORD d)
{
	return ProxyInterface->SetPrivateData(a, b, c, d);
}

HRESULT m_IDirectDrawSurface4::GetPrivateData(REFGUID a, LPVOID b, LPDWORD c)
{
	return ProxyInterface->GetPrivateData(a, b, c);
}

HRESULT m_IDirectDrawSurface4::FreePrivateData(REFGUID a)
{
	return ProxyInterface->FreePrivateData(a);
}

HRESULT m_IDirectDrawSurface4::GetUniquenessValue(LPDWORD a)
{
	return ProxyInterface->GetUniquenessValue(a);
}

HRESULT m_IDirectDrawSurface4::ChangeUniquenessValue()
{
	return ProxyInterface->ChangeUniquenessValue();
}
