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
#include "renderer/RenderBackendDraw.h"

/*
===============================================================================

	OpenGL draw backend — delegates to the existing free functions in
	tr_backend.cpp, draw_common.cpp, draw_arb2.cpp, tr_render.cpp.

===============================================================================
*/
class idRenderBackendDrawGL : public idRenderBackendDraw {
public:
	virtual void			Init();
	virtual void			Shutdown();
	virtual void			ExecuteBackEndCommands( const emptyCommand_t *cmds );
	virtual void			ReadPixels( int x, int y, int width, int height, byte *buffer );
};

void idRenderBackendDrawGL::Init() {
	// GL-specific draw backend init is handled by R_InitOpenGL
	// (ARB program loading, vertex cache init, etc.)
}

void idRenderBackendDrawGL::Shutdown() {
	RB_ShutdownDebugTools();
}

void idRenderBackendDrawGL::ExecuteBackEndCommands( const emptyCommand_t *cmds ) {
	RB_ExecuteBackEndCommands( cmds );
}

void idRenderBackendDrawGL::ReadPixels( int x, int y, int width, int height, byte *buffer ) {
	if ( glConfig.isWayland ) {
		qglReadPixels( x, y, width, height, GL_RGB, GL_UNSIGNED_BYTE, buffer );
	} else {
		GLint oldReadBuf = GL_BACK;
		qglGetIntegerv( GL_READ_BUFFER, &oldReadBuf );
		qglReadBuffer( GL_FRONT );
		qglReadPixels( x, y, width, height, GL_RGB, GL_UNSIGNED_BYTE, buffer );
		qglReadBuffer( oldReadBuf );
	}
}

// ---------------------------------------------------------------------------

#ifdef ID_VULKAN
// Defined in vk/VulkanBackendDraw.cpp
extern idRenderBackendDraw * R_GetVulkanBackendDraw();
// Defined in vk-ray/VulkanBackendDraw.cpp
extern idRenderBackendDraw * R_GetVulkanRayBackendDraw();
#endif

// ---------------------------------------------------------------------------

static idRenderBackendDrawGL s_glBackendDraw;

idRenderBackendDraw * R_CreateBackendDraw() {
#ifdef ID_VULKAN
	if ( idStr::Icmp( r_renderBackend.GetString(), "vulkan" ) == 0 ) {
		return R_GetVulkanBackendDraw();
	}
	if ( idStr::Icmp( r_renderBackend.GetString(), "vulkan-ray" ) == 0 ) {
		return R_GetVulkanRayBackendDraw();
	}
#endif
	return &s_glBackendDraw;
}
