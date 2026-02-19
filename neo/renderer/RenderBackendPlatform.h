/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 GPL Source Code ("Doom 3 Source Code").

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#ifndef __RENDER_BACKEND_PLATFORM_H__
#define __RENDER_BACKEND_PLATFORM_H__

#include "renderer/RenderSystem.h"
#include "renderer/RenderBackendGPU.h"
#include "renderer/qgl.h"

typedef struct renderBackendConfig_s {
	int			width;
	int			height;
	bool		fullScreen;
	bool		fullScreenDesktop;
	bool		stereo;
	int			displayHz;
	int			multiSamples;
} renderBackendConfig_t;

typedef enum {
	RBM_OPENGL,
	RBM_VULKAN,
	RBM_SOFTWARE,
	RBM_RAYTRACE,
	RBM_VOXEL
} renderBackendModule_t;

class idRenderBackendPlatform {
public:
	virtual					~idRenderBackendPlatform() {}
	virtual renderBackendModule_t GetModule() const = 0;
	virtual bool			Init( const renderBackendConfig_t &config ) = 0;
	virtual bool			SetScreenParms( const renderBackendConfig_t &config ) = 0;
	virtual void			Shutdown() = 0;
	virtual void			SwapBuffers() = 0;
	virtual bool			SetSwapInterval( int swapInterval ) = 0;
	virtual int				GetSwapInterval() const = 0;
	virtual float			GetDisplayRefresh() const = 0;
	virtual bool			SetWindowResizable( bool enableResizable ) = 0;
	virtual void			ResetGamma() = 0;
	virtual void			GetState( renderBackendState_t &state ) const = 0;
	virtual GLExtension_t	GetExtensionPointer( const char *name ) const = 0;
	virtual idRenderGpuCommandContext* GetImmediateContext() = 0;
};

// returns globally-owned singleton backend module instance, never NULL
idRenderBackendPlatform* R_GetRenderBackendPlatform();
// returns globally-owned singleton backend module instance for the requested module, never NULL
idRenderBackendPlatform* R_GetRenderBackendPlatform( renderBackendModule_t module );

#endif
