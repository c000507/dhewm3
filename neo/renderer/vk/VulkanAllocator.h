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

#ifndef __VULKAN_ALLOCATOR_H__
#define __VULKAN_ALLOCATOR_H__

#ifdef ID_VULKAN

#include <vulkan/vulkan.h>
#include "renderer/vk/VulkanFrameSync.h"

// A sub-allocation from a larger VkDeviceMemory block.
struct vulkanAllocation_t {
	VkDeviceMemory		memory;
	VkDeviceSize		offset;
	VkDeviceSize		size;
	void *				mappedData;		// non-NULL for host-visible allocations
	uint32_t			blockIndex;		// which block this came from
};

// A buffer with its backing memory allocation.
struct vulkanBuffer_t {
	VkBuffer			buffer;
	vulkanAllocation_t	allocation;
};

// Usage hints for buffer allocation.
typedef enum {
	VK_BUFFER_USAGE_VERTEX,			// vertex data (device-local preferred)
	VK_BUFFER_USAGE_INDEX,			// index data (device-local preferred)
	VK_BUFFER_USAGE_UNIFORM,		// uniform/constant buffer
	VK_BUFFER_USAGE_STAGING,		// CPU->GPU staging transfer source
	VK_BUFFER_USAGE_FRAME_TEMP		// per-frame temporary (host-visible ring)
} vulkanBufferUsage_t;

// Simple Vulkan memory allocator with sub-allocation from large blocks.
//
// Strategy:
//  - Device-local memory: large blocks sub-allocated for static geometry
//  - Host-visible memory: used for staging and frame-temporary data
//  - Frame-temporary data uses a ring buffer that cycles per frame-in-flight
class idVulkanAllocator {
public:
					idVulkanAllocator();

	bool			Init( VkDevice device, VkPhysicalDevice physicalDevice );
	void			Shutdown();

	// Allocate a buffer with the given usage and size.
	// For FRAME_TEMP buffers, the allocation is valid only until the
	// same frame slot is reused (after VK_MAX_FRAMES_IN_FLIGHT frames).
	bool			AllocBuffer( VkDeviceSize size, vulkanBufferUsage_t usage, vulkanBuffer_t &out );

	// Free a buffer (not valid for FRAME_TEMP allocations, those are auto-recycled).
	void			FreeBuffer( vulkanBuffer_t &buf );

	// Create a staging buffer, upload data, return the staging buffer.
	// Caller must ensure the staging buffer lives until the transfer completes.
	bool			CreateStagingBuffer( VkDeviceSize size, const void *data, vulkanBuffer_t &out );

	// Called at the start of each frame to reset frame-temp allocations for
	// the given frame index.
	void			BeginFrame( int frameIndex );

	// Allocate frame-temporary vertex/index data.
	// Returns the buffer and offset for binding. Data is written through the mapped pointer.
	bool			AllocFrameTemp( VkDeviceSize size, VkDeviceSize alignment,
						VkBuffer &outBuffer, VkDeviceSize &outOffset, void **outData );

	// Find a memory type index matching the requirements.
	uint32_t		FindMemoryType( uint32_t typeFilter, VkMemoryPropertyFlags properties ) const;

private:
	static const VkDeviceSize FRAME_TEMP_BUFFER_SIZE = 16 * 1024 * 1024;	// 16 MB per frame

	VkDevice					device;
	VkPhysicalDeviceMemoryProperties memProperties;

	// Per-frame temporary buffers (host-visible, coherent)
	struct frameTempBuffer_t {
		VkBuffer			buffer;
		VkDeviceMemory		memory;
		void *				mappedData;
		VkDeviceSize		currentOffset;
	};
	frameTempBuffer_t			frameTempBuffers[VK_MAX_FRAMES_IN_FLIGHT];
	int							currentFrameIndex;

	bool						initialized;
};

#endif // ID_VULKAN
#endif // __VULKAN_ALLOCATOR_H__
