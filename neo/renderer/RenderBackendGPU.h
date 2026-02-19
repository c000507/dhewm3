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

#ifndef __RENDER_BACKEND_GPU_H__
#define __RENDER_BACKEND_GPU_H__

class idRenderGpuBuffer {
public:
	virtual					~idRenderGpuBuffer() {}
	virtual int				GetSize() const = 0;
};

class idRenderGpuTexture {
public:
	virtual					~idRenderGpuTexture() {}
	virtual int				GetWidth() const = 0;
	virtual int				GetHeight() const = 0;
};

class idRenderGpuPipeline {
public:
	virtual					~idRenderGpuPipeline() {}
};

class idRenderGpuCommandContext {
public:
	virtual					~idRenderGpuCommandContext() {}
	virtual void			BindPipeline( idRenderGpuPipeline *pipeline ) = 0;
	virtual void			SetBlendEnabled( bool enabled ) = 0;
	virtual void			SetScissorEnabled( bool enabled ) = 0;
	virtual void			SetTexture2DEnabled( bool enabled ) = 0;
	virtual void			SetBlendEquationAdd() = 0;
	virtual void			Finish() = 0;
};

class idRenderGpuDevice {
public:
	virtual					~idRenderGpuDevice() {}
	virtual idRenderGpuBuffer* CreateBuffer( int sizeBytes ) = 0;
	virtual idRenderGpuTexture* CreateTexture2D( int width, int height ) = 0;
	virtual idRenderGpuPipeline* CreatePipeline() = 0;
	virtual idRenderGpuCommandContext* GetImmediateContext() = 0;
};

#endif
