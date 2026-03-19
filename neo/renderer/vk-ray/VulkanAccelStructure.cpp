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

#ifdef ID_VULKAN

#include "renderer/vk-ray/VulkanAccelStructure.h"
#include "renderer/vk-ray/VulkanRTFuncs.h"
#include "renderer/tr_local.h"
#include "framework/Common.h"
#include <cstring>
#include <vector>

idVulkanAccelStructureMgr vkRayAccelMgr;

idVulkanAccelStructureMgr::idVulkanAccelStructureMgr()
	: device( VK_NULL_HANDLE )
	, physicalDevice( VK_NULL_HANDLE )
	, oneTimePool( VK_NULL_HANDLE )
	, graphicsQueue( VK_NULL_HANDLE )
	, tlas( VK_NULL_HANDLE )
	, tlasBuffer( VK_NULL_HANDLE )
	, tlasMem( VK_NULL_HANDLE )
	, instanceBuf( VK_NULL_HANDLE )
	, instanceMem( VK_NULL_HANDLE )
	, tlasScratchBuf( VK_NULL_HANDLE )
	, tlasScratchMem( VK_NULL_HANDLE )
	, tlasScratchSize( 0 )
	, tlasMaxInstances( 0 )
	, initialized( false )
{
}

// ---------------------------------------------------------------------------

bool idVulkanAccelStructureMgr::Init( VkDevice dev, VkPhysicalDevice phyDev,
                                      uint32_t queueFamily, VkQueue queue ) {
	device        = dev;
	physicalDevice = phyDev;
	graphicsQueue = queue;

	vkGetPhysicalDeviceMemoryProperties( physicalDevice, &memProps );

	VkCommandPoolCreateInfo poolCI = {};
	poolCI.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolCI.queueFamilyIndex = queueFamily;
	poolCI.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	if ( vkCreateCommandPool( device, &poolCI, NULL, &oneTimePool ) != VK_SUCCESS ) {
		common->Warning( "AccelMgr: failed to create command pool" );
		return false;
	}

	initialized = true;
	return true;
}

void idVulkanAccelStructureMgr::Shutdown() {
	if ( !initialized ) return;

	FreeTLAS();

	for ( auto &kv : blasCache ) {
		FreeBLAS( kv.second );
	}
	blasCache.clear();

	if ( oneTimePool != VK_NULL_HANDLE ) {
		vkDestroyCommandPool( device, oneTimePool, NULL );
		oneTimePool = VK_NULL_HANDLE;
	}
	initialized = false;
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

uint32_t idVulkanAccelStructureMgr::FindMemoryType( uint32_t filter,
                                                     VkMemoryPropertyFlags props ) const {
	for ( uint32_t i = 0; i < memProps.memoryTypeCount; i++ ) {
		if ( (filter & (1u << i)) &&
		     (memProps.memoryTypes[i].propertyFlags & props) == props ) {
			return i;
		}
	}
	return UINT32_MAX;
}

VkBuffer idVulkanAccelStructureMgr::AllocBuffer( VkDeviceSize size,
                                                  VkBufferUsageFlags usage,
                                                  VkMemoryPropertyFlags memPropFlags,
                                                  VkDeviceMemory &outMem,
                                                  VkDeviceAddress *outAddr ) {
	VkBufferCreateInfo bci = {};
	bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bci.size  = size;
	bci.usage = usage;
	bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VkBuffer buf;
	if ( vkCreateBuffer( device, &bci, NULL, &buf ) != VK_SUCCESS ) {
		return VK_NULL_HANDLE;
	}

	VkMemoryRequirements mr;
	vkGetBufferMemoryRequirements( device, buf, &mr );

	VkMemoryAllocateFlagsInfo flagsInfo = {};
	flagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
	flagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

	VkMemoryAllocateInfo mai = {};
	mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	mai.allocationSize  = mr.size;
	mai.memoryTypeIndex = FindMemoryType( mr.memoryTypeBits, memPropFlags );
	if ( usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT ) {
		mai.pNext = &flagsInfo;  // required for vkGetBufferDeviceAddressKHR
	}

	if ( vkAllocateMemory( device, &mai, NULL, &outMem ) != VK_SUCCESS ) {
		vkDestroyBuffer( device, buf, NULL );
		return VK_NULL_HANDLE;
	}
	vkBindBufferMemory( device, buf, outMem, 0 );

	if ( outAddr ) {
		VkBufferDeviceAddressInfo addrInfo = {};
		addrInfo.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
		addrInfo.buffer = buf;
		*outAddr = pfn_vkGetBufferDeviceAddressKHR( device, &addrInfo );
	}
	return buf;
}

void idVulkanAccelStructureMgr::SubmitOneTime( std::function<void(VkCommandBuffer)> fn ) {
	VkCommandBufferAllocateInfo ai = {};
	ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	ai.commandPool        = oneTimePool;
	ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	ai.commandBufferCount = 1;

	VkCommandBuffer cmd;
	if ( vkAllocateCommandBuffers( device, &ai, &cmd ) != VK_SUCCESS ) return;

	VkCommandBufferBeginInfo bi = {};
	bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer( cmd, &bi );

	fn( cmd );

	vkEndCommandBuffer( cmd );

	VkSubmitInfo si = {};
	si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	si.commandBufferCount = 1;
	si.pCommandBuffers    = &cmd;
	vkQueueSubmit( graphicsQueue, 1, &si, VK_NULL_HANDLE );
	vkQueueWaitIdle( graphicsQueue );
	vkFreeCommandBuffers( device, oneTimePool, 1, &cmd );
}

void idVulkanAccelStructureMgr::FreeBLAS( blasEntry_t &e ) {
	if ( e.as != VK_NULL_HANDLE ) {
		pfn_vkDestroyAccelerationStructureKHR( device, e.as, NULL );
		e.as = VK_NULL_HANDLE;
	}
	if ( e.buffer != VK_NULL_HANDLE ) {
		vkDestroyBuffer( device, e.buffer, NULL ); e.buffer = VK_NULL_HANDLE;
	}
	if ( e.memory != VK_NULL_HANDLE ) {
		vkFreeMemory( device, e.memory, NULL ); e.memory = VK_NULL_HANDLE;
	}
	if ( e.vertBuf != VK_NULL_HANDLE ) {
		vkDestroyBuffer( device, e.vertBuf, NULL ); e.vertBuf = VK_NULL_HANDLE;
	}
	if ( e.vertMem != VK_NULL_HANDLE ) {
		vkFreeMemory( device, e.vertMem, NULL ); e.vertMem = VK_NULL_HANDLE;
	}
	if ( e.idxBuf != VK_NULL_HANDLE ) {
		vkDestroyBuffer( device, e.idxBuf, NULL ); e.idxBuf = VK_NULL_HANDLE;
	}
	if ( e.idxMem != VK_NULL_HANDLE ) {
		vkFreeMemory( device, e.idxMem, NULL ); e.idxMem = VK_NULL_HANDLE;
	}
}

void idVulkanAccelStructureMgr::FreeTLAS() {
	if ( tlas != VK_NULL_HANDLE ) {
		pfn_vkDestroyAccelerationStructureKHR( device, tlas, NULL );
		tlas = VK_NULL_HANDLE;
	}
	if ( tlasBuffer != VK_NULL_HANDLE ) {
		vkDestroyBuffer( device, tlasBuffer, NULL ); tlasBuffer = VK_NULL_HANDLE;
	}
	if ( tlasMem != VK_NULL_HANDLE ) {
		vkFreeMemory( device, tlasMem, NULL ); tlasMem = VK_NULL_HANDLE;
	}
	if ( instanceBuf != VK_NULL_HANDLE ) {
		vkDestroyBuffer( device, instanceBuf, NULL ); instanceBuf = VK_NULL_HANDLE;
	}
	if ( instanceMem != VK_NULL_HANDLE ) {
		vkFreeMemory( device, instanceMem, NULL ); instanceMem = VK_NULL_HANDLE;
	}
	if ( tlasScratchBuf != VK_NULL_HANDLE ) {
		vkDestroyBuffer( device, tlasScratchBuf, NULL ); tlasScratchBuf = VK_NULL_HANDLE;
	}
	if ( tlasScratchMem != VK_NULL_HANDLE ) {
		vkFreeMemory( device, tlasScratchMem, NULL ); tlasScratchMem = VK_NULL_HANDLE;
	}
	tlasMaxInstances = 0;
	tlasScratchSize  = 0;
}

// ---------------------------------------------------------------------------
// BLAS
// ---------------------------------------------------------------------------

VkDeviceAddress idVulkanAccelStructureMgr::GetOrBuildBLAS( const srfTriangles_t *tri ) {
	if ( !tri || tri->numVerts == 0 || tri->numIndexes == 0 ) {
		return 0;
	}

	// Check cache
	auto it = blasCache.find( tri );
	if ( it != blasCache.end() ) {
		return it->second.deviceAddress;
	}

	blasEntry_t entry = {};

	// --- 1. Upload vertex positions (vec3, 12 bytes each) ---
	// idDrawVert::xyz is the first 12 bytes of each vertex.
	static const int POS_STRIDE = 12;  // sizeof(idVec3)
	uint32_t numVerts   = (uint32_t)tri->numVerts;
	uint32_t numIndexes = (uint32_t)tri->numIndexes;

	// Build a tightly-packed position array
	std::vector<float> positions( numVerts * 3 );
	for ( uint32_t i = 0; i < numVerts; i++ ) {
		positions[i*3+0] = tri->verts[i].xyz.x;
		positions[i*3+1] = tri->verts[i].xyz.y;
		positions[i*3+2] = tri->verts[i].xyz.z;
	}

	VkDeviceSize vertBytes = numVerts * POS_STRIDE;
	VkDeviceSize idxBytes  = numIndexes * sizeof(uint32_t);

	// Staging buffers for upload
	VkBuffer  stagingVert, stagingIdx;
	VkDeviceMemory stagingVertMem, stagingIdxMem;

	stagingVert = AllocBuffer( vertBytes,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		stagingVertMem );
	if ( stagingVert == VK_NULL_HANDLE ) {
		common->Warning( "BLAS: failed to alloc vertex staging buffer" );
		return 0;
	}

	void *mapped;
	vkMapMemory( device, stagingVertMem, 0, vertBytes, 0, &mapped );
	memcpy( mapped, positions.data(), (size_t)vertBytes );
	vkUnmapMemory( device, stagingVertMem );

	// Upload indices (glIndex_t → uint32_t; may be uint16 in older builds)
	std::vector<uint32_t> indices32( numIndexes );
	for ( uint32_t i = 0; i < numIndexes; i++ ) {
		indices32[i] = (uint32_t)tri->indexes[i];
	}

	stagingIdx = AllocBuffer( idxBytes,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		stagingIdxMem );
	if ( stagingIdx == VK_NULL_HANDLE ) {
		vkDestroyBuffer( device, stagingVert, NULL );
		vkFreeMemory( device, stagingVertMem, NULL );
		return 0;
	}
	vkMapMemory( device, stagingIdxMem, 0, idxBytes, 0, &mapped );
	memcpy( mapped, indices32.data(), (size_t)idxBytes );
	vkUnmapMemory( device, stagingIdxMem );

	// Device-local vertex / index buffers
	VkDeviceAddress vertAddr, idxAddr;
	entry.vertBuf = AllocBuffer( vertBytes,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT |
		VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		entry.vertMem, &vertAddr );

	entry.idxBuf = AllocBuffer( idxBytes,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT |
		VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		entry.idxMem, &idxAddr );

	if ( entry.vertBuf == VK_NULL_HANDLE || entry.idxBuf == VK_NULL_HANDLE ) {
		vkDestroyBuffer( device, stagingVert, NULL );
		vkFreeMemory( device, stagingVertMem, NULL );
		vkDestroyBuffer( device, stagingIdx, NULL );
		vkFreeMemory( device, stagingIdxMem, NULL );
		FreeBLAS( entry );
		return 0;
	}

	// Copy staging → device local
	SubmitOneTime( [&]( VkCommandBuffer cmd ) {
		VkBufferCopy reg = {};
		reg.size = vertBytes;
		vkCmdCopyBuffer( cmd, stagingVert, entry.vertBuf, 1, &reg );
		reg.size = idxBytes;
		vkCmdCopyBuffer( cmd, stagingIdx, entry.idxBuf, 1, &reg );
	} );

	vkDestroyBuffer( device, stagingVert, NULL );
	vkFreeMemory( device, stagingVertMem, NULL );
	vkDestroyBuffer( device, stagingIdx, NULL );
	vkFreeMemory( device, stagingIdxMem, NULL );

	// --- 2. BLAS geometry description ---
	VkAccelerationStructureGeometryTrianglesDataKHR triData = {};
	triData.sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
	triData.vertexFormat  = VK_FORMAT_R32G32B32_SFLOAT;
	triData.vertexData.deviceAddress = vertAddr;
	triData.vertexStride  = POS_STRIDE;
	triData.maxVertex     = numVerts - 1;
	triData.indexType     = VK_INDEX_TYPE_UINT32;
	triData.indexData.deviceAddress = idxAddr;

	VkAccelerationStructureGeometryKHR geom = {};
	geom.sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
	geom.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
	geom.geometry.triangles = triData;
	geom.flags        = VK_GEOMETRY_OPAQUE_BIT_KHR;

	VkAccelerationStructureBuildGeometryInfoKHR buildInfo = {};
	buildInfo.sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	buildInfo.type          = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	buildInfo.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	buildInfo.mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	buildInfo.geometryCount = 1;
	buildInfo.pGeometries   = &geom;

	uint32_t primitiveCount = numIndexes / 3;
	VkAccelerationStructureBuildSizesInfoKHR sizeInfo = {};
	sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
	pfn_vkGetAccelerationStructureBuildSizesKHR( device,
		VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
		&buildInfo, &primitiveCount, &sizeInfo );

	// --- 3. Allocate BLAS buffer ---
	VkDeviceAddress blasBufAddr;
	entry.buffer = AllocBuffer( sizeInfo.accelerationStructureSize,
		VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		entry.memory, &blasBufAddr );

	if ( entry.buffer == VK_NULL_HANDLE ) {
		FreeBLAS( entry );
		return 0;
	}

	// --- 4. Create BLAS ---
	VkAccelerationStructureCreateInfoKHR createInfo = {};
	createInfo.sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
	createInfo.buffer = entry.buffer;
	createInfo.size   = sizeInfo.accelerationStructureSize;
	createInfo.type   = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

	if ( pfn_vkCreateAccelerationStructureKHR( device, &createInfo, NULL, &entry.as ) != VK_SUCCESS ) {
		FreeBLAS( entry );
		return 0;
	}

	// Scratch buffer
	VkDeviceMemory scratchMem;
	VkDeviceAddress scratchAddr;
	VkBuffer scratch = AllocBuffer( sizeInfo.buildScratchSize,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		scratchMem, &scratchAddr );

	if ( scratch == VK_NULL_HANDLE ) {
		FreeBLAS( entry );
		return 0;
	}

	// --- 5. Build BLAS ---
	buildInfo.dstAccelerationStructure  = entry.as;
	buildInfo.scratchData.deviceAddress = scratchAddr;

	VkAccelerationStructureBuildRangeInfoKHR rangeInfo = {};
	rangeInfo.primitiveCount  = primitiveCount;
	rangeInfo.primitiveOffset = 0;
	rangeInfo.firstVertex     = 0;
	rangeInfo.transformOffset = 0;
	const VkAccelerationStructureBuildRangeInfoKHR *pRangeInfo = &rangeInfo;

	SubmitOneTime( [&]( VkCommandBuffer cmd ) {
		pfn_vkCmdBuildAccelerationStructuresKHR( cmd, 1, &buildInfo, &pRangeInfo );
	} );

	vkDestroyBuffer( device, scratch, NULL );
	vkFreeMemory( device, scratchMem, NULL );

	// Get device address
	VkAccelerationStructureDeviceAddressInfoKHR addrInfo = {};
	addrInfo.sType                 = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
	addrInfo.accelerationStructure = entry.as;
	entry.deviceAddress = pfn_vkGetAccelerationStructureDeviceAddressKHR( device, &addrInfo );

	blasCache[tri] = entry;

	// --- Benchmark geometry export: write SECTION_MESH for new meshes ---
	if ( benchmarkFile && meshIdCache.count( tri ) == 0 ) {
		uint32_t mid = nextMeshId++;
		meshIdCache[tri] = mid;

		uint8_t  tag = 0x01;
		uint32_t nv  = numVerts;
		uint32_t ni  = numIndexes;
		fwrite( &tag, 1, 1, benchmarkFile );
		fwrite( &mid, 4, 1, benchmarkFile );
		fwrite( &nv,  4, 1, benchmarkFile );
		fwrite( &ni,  4, 1, benchmarkFile );

		// Vertex positions only (xyz float, 12 bytes each)
		for ( uint32_t vi = 0; vi < nv; vi++ ) {
			fwrite( &tri->verts[vi].xyz, sizeof(float), 3, benchmarkFile );
		}

		// Indices as uint32
		for ( uint32_t ii = 0; ii < ni; ii++ ) {
			uint32_t idx = (uint32_t)tri->indexes[ii];
			fwrite( &idx, 4, 1, benchmarkFile );
		}
	}

	return entry.deviceAddress;
}

// ---------------------------------------------------------------------------
// TLAS
// ---------------------------------------------------------------------------

VkAccelerationStructureKHR idVulkanAccelStructureMgr::BuildTLAS(
	VkCommandBuffer cmd,
	drawSurf_t **surfs, int numSurfs )
{
	if ( numSurfs <= 0 ) return VK_NULL_HANDLE;

	// Collect valid instances
	std::vector<VkAccelerationStructureInstanceKHR> instances;
	instances.reserve( numSurfs );
	lastInstances.clear();

	for ( int i = 0; i < numSurfs; i++ ) {
		drawSurf_t *surf = surfs[i];
		if ( !surf || !surf->geo ) continue;

		VkDeviceAddress blasAddr = GetOrBuildBLAS( surf->geo );
		if ( blasAddr == 0 ) continue;

		VkAccelerationStructureInstanceKHR inst = {};

		// Doom 3 modelMatrix is column-major, 4×4 (16 floats).
		// VkTransformMatrixKHR is row-major 3×4.
		// space->modelMatrix: columns are basis vectors; we need rows.
		const float *M = surf->space->modelMatrix;
		// Row i = (M[i], M[i+4], M[i+8], M[i+12])
		inst.transform.matrix[0][0] = M[0];  inst.transform.matrix[0][1] = M[4];
		inst.transform.matrix[0][2] = M[8];  inst.transform.matrix[0][3] = M[12];
		inst.transform.matrix[1][0] = M[1];  inst.transform.matrix[1][1] = M[5];
		inst.transform.matrix[1][2] = M[9];  inst.transform.matrix[1][3] = M[13];
		inst.transform.matrix[2][0] = M[2];  inst.transform.matrix[2][1] = M[6];
		inst.transform.matrix[2][2] = M[10]; inst.transform.matrix[2][3] = M[14];

		inst.instanceCustomIndex                    = (uint32_t)instances.size();
		inst.mask                                   = 0xFF;
		inst.instanceShaderBindingTableRecordOffset = 0;
		inst.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
		inst.accelerationStructureReference         = blasAddr;

		instances.push_back( inst );

		// Cache instance data for benchmark BVH export
		if ( benchmarkFile ) {
			BenchInstance bi;
			auto it = meshIdCache.find( surf->geo );
			bi.meshId = ( it != meshIdCache.end() ) ? it->second : 0xFFFFFFFFu;
			bi.transform[0] = M[0]; bi.transform[1] = M[4]; bi.transform[2]  = M[8];  bi.transform[3]  = M[12];
			bi.transform[4] = M[1]; bi.transform[5] = M[5]; bi.transform[6]  = M[9];  bi.transform[7]  = M[13];
			bi.transform[8] = M[2]; bi.transform[9] = M[6]; bi.transform[10] = M[10]; bi.transform[11] = M[14];
			lastInstances.push_back( bi );
		}
	}

	if ( instances.empty() ) return VK_NULL_HANDLE;

	uint32_t instanceCount = (uint32_t)instances.size();
	VkDeviceSize instBufSize = instanceCount * sizeof(VkAccelerationStructureInstanceKHR);

	// Reallocate per-frame instance buffer if needed (host-visible, coherent)
	if ( instanceCount > tlasMaxInstances ) {
		if ( instanceBuf != VK_NULL_HANDLE ) {
			vkDestroyBuffer( device, instanceBuf, NULL ); instanceBuf = VK_NULL_HANDLE;
			vkFreeMemory( device, instanceMem, NULL );    instanceMem = VK_NULL_HANDLE;
		}
		VkDeviceAddress addr;
		instanceBuf = AllocBuffer( instBufSize,
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			instanceMem, &addr );
		if ( instanceBuf == VK_NULL_HANDLE ) return VK_NULL_HANDLE;
		tlasMaxInstances = instanceCount + 64; // over-allocate
	}

	// Write instances to host-visible buffer
	void *mapped;
	vkMapMemory( device, instanceMem, 0, instBufSize, 0, &mapped );
	memcpy( mapped, instances.data(), (size_t)instBufSize );
	vkUnmapMemory( device, instanceMem );

	VkBufferDeviceAddressInfo addrInfo = {};
	addrInfo.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	addrInfo.buffer = instanceBuf;
	VkDeviceAddress instAddr = pfn_vkGetBufferDeviceAddressKHR( device, &addrInfo );

	// Memory barrier: ensure BLAS builds are visible before TLAS build
	VkMemoryBarrier barrier = {};
	barrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
	barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
	vkCmdPipelineBarrier( cmd,
		VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
		VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
		0, 1, &barrier, 0, NULL, 0, NULL );

	// TLAS geometry
	VkAccelerationStructureGeometryInstancesDataKHR instData = {};
	instData.sType           = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
	instData.arrayOfPointers = VK_FALSE;
	instData.data.deviceAddress = instAddr;

	VkAccelerationStructureGeometryKHR tlasGeom = {};
	tlasGeom.sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
	tlasGeom.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	tlasGeom.geometry.instances = instData;

	VkAccelerationStructureBuildGeometryInfoKHR tlasBuildInfo = {};
	tlasBuildInfo.sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	tlasBuildInfo.type          = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	tlasBuildInfo.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	tlasBuildInfo.mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	tlasBuildInfo.geometryCount = 1;
	tlasBuildInfo.pGeometries   = &tlasGeom;

	VkAccelerationStructureBuildSizesInfoKHR sizeInfo = {};
	sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
	pfn_vkGetAccelerationStructureBuildSizesKHR( device,
		VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
		&tlasBuildInfo, &instanceCount, &sizeInfo );

	// Destroy old TLAS before rebuilding
	FreeTLAS();

	// Allocate TLAS storage buffer
	VkDeviceAddress tlasBufAddr;
	tlasBuffer = AllocBuffer( sizeInfo.accelerationStructureSize,
		VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		tlasMem, &tlasBufAddr );
	if ( tlasBuffer == VK_NULL_HANDLE ) return VK_NULL_HANDLE;

	// Scratch (device address retrieved below via vkGetBufferDeviceAddressKHR)
	tlasScratchBuf = AllocBuffer( sizeInfo.buildScratchSize,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		tlasScratchMem, nullptr );
	tlasScratchSize = sizeInfo.buildScratchSize;
	if ( tlasScratchBuf == VK_NULL_HANDLE ) {
		FreeTLAS();
		return VK_NULL_HANDLE;
	}

	VkBufferDeviceAddressInfo scratchAddr = {};
	scratchAddr.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	scratchAddr.buffer = tlasScratchBuf;
	VkDeviceAddress scratchDevAddr = pfn_vkGetBufferDeviceAddressKHR( device, &scratchAddr );

	// Create TLAS
	VkAccelerationStructureCreateInfoKHR tlasCI = {};
	tlasCI.sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
	tlasCI.buffer = tlasBuffer;
	tlasCI.size   = sizeInfo.accelerationStructureSize;
	tlasCI.type   = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	if ( pfn_vkCreateAccelerationStructureKHR( device, &tlasCI, NULL, &tlas ) != VK_SUCCESS ) {
		FreeTLAS();
		return VK_NULL_HANDLE;
	}

	// Re-allocate instance buf with addr (was freed in FreeTLAS above)
	VkDeviceAddress iAddr;
	instanceBuf = AllocBuffer( instBufSize,
		VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		instanceMem, &iAddr );
	if ( instanceBuf == VK_NULL_HANDLE ) { FreeTLAS(); return VK_NULL_HANDLE; }
	tlasMaxInstances = instanceCount + 64;

	vkMapMemory( device, instanceMem, 0, instBufSize, 0, &mapped );
	memcpy( mapped, instances.data(), (size_t)instBufSize );
	vkUnmapMemory( device, instanceMem );

	instData.data.deviceAddress = iAddr;
	tlasGeom.geometry.instances = instData;
	tlasBuildInfo.pGeometries   = &tlasGeom;
	tlasBuildInfo.dstAccelerationStructure  = tlas;
	tlasBuildInfo.scratchData.deviceAddress = scratchDevAddr;

	VkAccelerationStructureBuildRangeInfoKHR rangeInfo = {};
	rangeInfo.primitiveCount = instanceCount;
	const VkAccelerationStructureBuildRangeInfoKHR *pRange = &rangeInfo;

	pfn_vkCmdBuildAccelerationStructuresKHR( cmd, 1, &tlasBuildInfo, &pRange );

	// Barrier: TLAS build → RT shader read
	VkMemoryBarrier tlasDone = {};
	tlasDone.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	tlasDone.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
	tlasDone.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
	vkCmdPipelineBarrier( cmd,
		VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
		VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
		0, 1, &tlasDone, 0, NULL, 0, NULL );

	return tlas;
}

// ---------------------------------------------------------------------------
// Benchmark BVH frame export
// ---------------------------------------------------------------------------

void idVulkanAccelStructureMgr::WriteBVHFrame( FILE *f, uint32_t frameNum,
                                                const float camPos[3],
                                                const float camAngles[3],
                                                uint32_t numRecords )
{
	if ( !f ) return;

	uint8_t  tag = 0x02;
	uint32_t ni  = (uint32_t)lastInstances.size();

	fwrite( &tag,        1,  1, f );
	fwrite( &frameNum,   4,  1, f );
	fwrite( &ni,         4,  1, f );
	fwrite( &numRecords, 4,  1, f );
	fwrite( camPos,      sizeof(float), 3, f );
	fwrite( camAngles,   sizeof(float), 3, f );

	for ( const BenchInstance &bi : lastInstances ) {
		fwrite( &bi.meshId,    4, 1,  f );
		fwrite( bi.transform,  4, 12, f );
	}
}

#endif // ID_VULKAN
