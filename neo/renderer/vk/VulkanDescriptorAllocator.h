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

#ifndef __VULKAN_DESCRIPTOR_ALLOCATOR_H__
#define __VULKAN_DESCRIPTOR_ALLOCATOR_H__

#ifdef ID_VULKAN

#include <vulkan/vulkan.h>

// Per-frame descriptor set allocator.
// Allocates descriptor sets from a pool during a frame, and resets the
// entire pool at the start of the next frame. This avoids the overhead
// of individual free operations and matches dhewm3's frame-based model.
class idVulkanDescriptorAllocator {
public:
					idVulkanDescriptorAllocator();

	bool			Init( VkDevice device, VkDescriptorSetLayout uboLayout,
						VkDescriptorSetLayout samplerLayout, int framesInFlight );
	void			Shutdown();

	// Call at the start of each frame to reset the current pool.
	void			BeginFrame( int frameIndex );

	// Allocate a UBO descriptor set for this frame.
	VkDescriptorSet	AllocUBOSet();

	// Allocate a sampler descriptor set for this frame.
	VkDescriptorSet	AllocSamplerSet();

private:
	static const int MAX_FRAMES = 3;
	static const int MAX_SETS_PER_FRAME = 4096;

	VkDevice		device;
	VkDescriptorSetLayout uboLayout;
	VkDescriptorSetLayout samplerLayout;

	VkDescriptorPool pools[MAX_FRAMES];
	int				numFrames;
	int				currentFrame;

	bool			initialized;
};

#endif // ID_VULKAN
#endif // __VULKAN_DESCRIPTOR_ALLOCATOR_H__
