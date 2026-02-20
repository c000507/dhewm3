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

#ifndef __VULKAN_ACCEL_STRUCTURE_H__
#define __VULKAN_ACCEL_STRUCTURE_H__

#ifdef ID_VULKAN

#include <vulkan/vulkan.h>
#include <unordered_map>
#include <functional>

// Forward declarations using the typedef-compatible form
struct srfTriangles_s;
typedef struct srfTriangles_s srfTriangles_t;
struct drawSurf_s;
typedef struct drawSurf_s drawSurf_t;

// A single bottom-level acceleration structure entry (one per static mesh).
struct blasEntry_t {
	VkAccelerationStructureKHR  as;
	VkBuffer                    buffer;
	VkDeviceMemory              memory;
	VkDeviceAddress             deviceAddress;
	// Vertex/index device buffers (kept alive for BLAS lifetime)
	VkBuffer                    vertBuf;
	VkDeviceMemory              vertMem;
	VkBuffer                    idxBuf;
	VkDeviceMemory              idxMem;
};

// Manages BLAS (per-mesh) and TLAS (per-frame) acceleration structures
// for the Vulkan ray tracing backend.
class idVulkanAccelStructureMgr {
public:
	idVulkanAccelStructureMgr();

	bool Init( VkDevice device, VkPhysicalDevice physicalDevice,
	           uint32_t graphicsQueueFamilyIndex, VkQueue graphicsQueue );
	void Shutdown();

	// Build or retrieve a cached BLAS for a mesh.
	// The cache key is the srfTriangles_t pointer (deformed/dynamic
	// surfaces have new pointers each frame and rebuild automatically).
	// Returns VK_NULL_HANDLE on failure.
	VkDeviceAddress GetOrBuildBLAS( const srfTriangles_t *tri );

	// Build the TLAS this frame using the given draw surfaces.
	// Records into cmd; caller must ensure cmd is open.
	// Returns VK_NULL_HANDLE on failure.
	VkAccelerationStructureKHR BuildTLAS( VkCommandBuffer cmd,
	                                      drawSurf_t **surfs, int numSurfs );

private:
	uint32_t FindMemoryType( uint32_t typeFilter, VkMemoryPropertyFlags props ) const;

	VkBuffer AllocBuffer( VkDeviceSize size, VkBufferUsageFlags usage,
	                      VkMemoryPropertyFlags memProps, VkDeviceMemory &outMem,
	                      VkDeviceAddress *outAddr = nullptr );

	void SubmitOneTime( std::function<void(VkCommandBuffer)> fn );

	void FreeBLAS( blasEntry_t &e );
	void FreeTLAS();

	VkDevice           device;
	VkPhysicalDevice   physicalDevice;
	VkCommandPool      oneTimePool;
	VkQueue            graphicsQueue;
	VkPhysicalDeviceMemoryProperties memProps;

	std::unordered_map<const srfTriangles_t*, blasEntry_t> blasCache;

	VkAccelerationStructureKHR tlas;
	VkBuffer                   tlasBuffer;
	VkDeviceMemory             tlasMem;
	VkBuffer                   instanceBuf;     // CPU-writable instance upload buffer
	VkDeviceMemory             instanceMem;
	VkBuffer                   tlasScratchBuf;  // scratch reused each frame
	VkDeviceMemory             tlasScratchMem;
	VkDeviceSize               tlasScratchSize;
	uint32_t                   tlasMaxInstances;

	bool initialized;
};

extern idVulkanAccelStructureMgr vkRayAccelMgr;

#endif // ID_VULKAN
#endif // __VULKAN_ACCEL_STRUCTURE_H__
