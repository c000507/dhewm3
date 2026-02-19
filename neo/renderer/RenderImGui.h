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

#ifndef __RENDER_IMGUI_H__
#define __RENDER_IMGUI_H__

/*
===============================================================================

	idRenderImGuiBackend - abstraction for ImGui rendering backend.
	Keeps all GL/Vulkan/etc details inside the renderer, so code outside
	renderer/ never needs to include qgl.h or tr_local.h for ImGui support.

===============================================================================
*/

class idRenderImGuiBackend {
public:
	virtual					~idRenderImGuiBackend() {}

	// Initialize the renderer-specific ImGui backend (e.g. ImGui_ImplOpenGL2_Init)
	virtual bool			Init() = 0;

	// Shut down the renderer-specific ImGui backend
	virtual void			Shutdown() = 0;

	// Called once per frame before ImGui::NewFrame()
	virtual void			NewFrame() = 0;

	// Render ImGui draw data. All renderer state save/restore happens here.
	virtual void			RenderDrawData() = 0;
};

// Factory function: creates the renderer-specific backend implementation
idRenderImGuiBackend* CreateRenderImGuiBackend();

#endif /* !__RENDER_IMGUI_H__ */
