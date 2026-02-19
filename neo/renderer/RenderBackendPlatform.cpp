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

#include "renderer/tr_local.h"
#include "renderer/RenderBackendPlatform.h"
#include <cstring>

class idOpenGLRenderBackendPlatform : public idRenderBackendPlatform {
public:
	virtual renderBackendModule_t GetModule() const {
		return RBM_OPENGL;
	}

	virtual bool Init( const renderBackendConfig_t &config ) {
		glimpParms_t parms;
		// keep this explicit zero-init to stay C++03-safe and robust if fields are added later
		memset( &parms, 0, sizeof(parms) );
		parms.width = config.width;
		parms.height = config.height;
		parms.fullScreen = config.fullScreen;
		parms.fullScreenDesktop = config.fullScreenDesktop;
		parms.stereo = config.stereo;
		parms.displayHz = config.displayHz;
		parms.multiSamples = config.multiSamples;
		return GLimp_Init( parms );
	}

	virtual bool SetScreenParms( const renderBackendConfig_t &config ) {
		glimpParms_t parms;
		// keep this explicit zero-init to stay C++03-safe and robust if fields are added later
		memset( &parms, 0, sizeof(parms) );
		parms.width = config.width;
		parms.height = config.height;
		parms.fullScreen = config.fullScreen;
		parms.fullScreenDesktop = config.fullScreenDesktop;
		parms.stereo = config.stereo;
		parms.displayHz = config.displayHz;
		parms.multiSamples = config.multiSamples;
		return GLimp_SetScreenParms( parms );
	}

	virtual void Shutdown() {
		GLimp_Shutdown();
	}

	virtual void SwapBuffers() {
		GLimp_SwapBuffers();
	}

	virtual bool SetSwapInterval( int swapInterval ) {
		return GLimp_SetSwapInterval( swapInterval );
	}

	virtual int GetSwapInterval() const {
		return GLimp_GetSwapInterval();
	}

	virtual float GetDisplayRefresh() const {
		return GLimp_GetDisplayRefresh();
	}

	virtual bool SetWindowResizable( bool enableResizable ) {
		return GLimp_SetWindowResizable( enableResizable );
	}

	virtual void ResetGamma() {
		GLimp_ResetGamma();
	}

	virtual void GetState( renderBackendState_t &state ) const {
		glimpParms_t curState = GLimp_GetCurState();
		state.width = curState.width;
		state.height = curState.height;
		state.fullScreen = curState.fullScreen;
		state.fullScreenDesktop = curState.fullScreenDesktop;
		state.displayHz = curState.displayHz;
		state.multiSamples = curState.multiSamples;
		state.swapInterval = GLimp_GetSwapInterval();
		state.displayRefreshHz = GLimp_GetDisplayRefresh();
	}
};

class idUnsupportedRenderBackendPlatform : public idRenderBackendPlatform {
public:
	idUnsupportedRenderBackendPlatform( renderBackendModule_t module_ ) : module(module_) {}

	virtual renderBackendModule_t GetModule() const {
		return module;
	}

	virtual bool Init( const renderBackendConfig_t & ) {
		return false;
	}

	virtual bool SetScreenParms( const renderBackendConfig_t & ) {
		return false;
	}

	virtual void Shutdown() {
	}

	virtual void SwapBuffers() {
	}

	virtual bool SetSwapInterval( int ) {
		return false;
	}

	virtual int GetSwapInterval() const {
		return 0;
	}

	virtual float GetDisplayRefresh() const {
		return 0.0f;
	}

	virtual bool SetWindowResizable( bool ) {
		return false;
	}

	virtual void ResetGamma() {
	}

	virtual void GetState( renderBackendState_t &state ) const {
		memset( &state, 0, sizeof(state) );
	}

private:
	renderBackendModule_t module;
};

static idOpenGLRenderBackendPlatform s_openGLBackendPlatform;
static idUnsupportedRenderBackendPlatform s_vulkanBackendPlatform( RBM_VULKAN );
static idUnsupportedRenderBackendPlatform s_softwareBackendPlatform( RBM_SOFTWARE );
static idUnsupportedRenderBackendPlatform s_raytraceBackendPlatform( RBM_RAYTRACE );
static idUnsupportedRenderBackendPlatform s_voxelBackendPlatform( RBM_VOXEL );

idRenderBackendPlatform* R_GetRenderBackendPlatform( renderBackendModule_t module ) {
	switch( module ) {
		case RBM_VULKAN: return &s_vulkanBackendPlatform;
		case RBM_SOFTWARE: return &s_softwareBackendPlatform;
		case RBM_RAYTRACE: return &s_raytraceBackendPlatform;
		case RBM_VOXEL: return &s_voxelBackendPlatform;
		case RBM_OPENGL:
		default:
			return &s_openGLBackendPlatform;
	}
}

idRenderBackendPlatform* R_GetRenderBackendPlatform() {
	return &s_openGLBackendPlatform;
}
