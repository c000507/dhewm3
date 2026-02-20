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

#ifndef __RENDERBACKENDDRAW_H__
#define __RENDERBACKENDDRAW_H__

struct emptyCommand_s;

/*
===============================================================================

	idRenderBackendDraw — abstract draw backend interface.

	Encapsulates the entire back-end rendering pipeline that executes the
	command list built by the front-end each frame.  The OpenGL implementation
	delegates to the existing free functions (RB_SetDefaultGLState, RB_DrawView,
	etc.).  A Vulkan implementation would record Vulkan command buffers instead.

===============================================================================
*/
class idRenderBackendDraw {
public:
	virtual					~idRenderBackendDraw() {}

	// Called once after the rendering context is created.
	virtual void			Init() = 0;

	// Called on shutdown before the rendering context is destroyed.
	virtual void			Shutdown() = 0;

	// Execute the back-end command list produced by the front-end this frame.
	virtual void			ExecuteBackEndCommands( const emptyCommand_s *cmds ) = 0;

	// Read back pixels from the most recently rendered frame.
	// Called after ExecuteBackEndCommands (including swap) has returned.
	// Output is GL_RGB format (3 bytes per pixel, bottom-up row order to
	// match the GL convention that TakeScreenshot expects).
	virtual void			ReadPixels( int x, int y, int width, int height, byte *buffer ) {}
};

// Factory: creates the appropriate draw backend for the active renderer.
idRenderBackendDraw *	R_CreateBackendDraw();

#endif /* !__RENDERBACKENDDRAW_H__ */
