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

#include "renderer/vk/VulkanAllocator.h"
#include "framework/Common.h"
#include <cstring>

idVulkanAllocator::idVulkanAllocator()
	: device( VK_NULL_HANDLE )
	, currentFrameIndex( 0 )
	, initialized( false )
{
	memset( &memProperties, 0, sizeof( memProperties ) );
	memset( frameTempBuffers, 0, sizeof( frameTempBuffers ) );
}

bool idVulkanAllocator::Init( VkDevice device_, VkPhysicalDevice physicalDevice ) {
	device = device_;

	vkGetPhysicalDeviceMemoryProperties( physicalDevice, &memProperties );

	// Create per-frame temporary buffers
	for ( int i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; i++ ) {
		frameTempBuffer_t &ftb = frameTempBuffers[i];

		VkBufferCreateInfo bufInfo = {};
		bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufInfo.size = FRAME_TEMP_BUFFER_SIZE;
		bufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
			| VK_BUFFER_USAGE_INDEX_BUFFER_BIT
			| VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
			| VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VkResult result = vkCreateBuffer( device, &bufInfo, NULL, &ftb.buffer );
		if ( result != VK_SUCCESS ) {
			common->Warning( "vkCreateBuffer failed for frame temp %d: %d", i, (int)result );
			Shutdown();
			return false;
		}

		VkMemoryRequirements memReqs;
		vkGetBufferMemoryRequirements( device, ftb.buffer, &memReqs );

		VkMemoryAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memReqs.size;
		allocInfo.memoryTypeIndex = FindMemoryType( memReqs.memoryTypeBits,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT );

		if ( allocInfo.memoryTypeIndex == UINT32_MAX ) {
			common->Warning( "No suitable memory type for frame temp buffer" );
			Shutdown();
			return false;
		}

		result = vkAllocateMemory( device, &allocInfo, NULL, &ftb.memory );
		if ( result != VK_SUCCESS ) {
			common->Warning( "vkAllocateMemory failed for frame temp %d: %d", i, (int)result );
			Shutdown();
			return false;
		}

		vkBindBufferMemory( device, ftb.buffer, ftb.memory, 0 );

		result = vkMapMemory( device, ftb.memory, 0, FRAME_TEMP_BUFFER_SIZE, 0, &ftb.mappedData );
		if ( result != VK_SUCCESS ) {
			common->Warning( "vkMapMemory failed for frame temp %d: %d", i, (int)result );
			Shutdown();
			return false;
		}

		ftb.currentOffset = 0;
	}

	initialized = true;
	common->Printf( "Vulkan allocator initialized (%d MB per-frame temp buffers)\n",
		(int)( FRAME_TEMP_BUFFER_SIZE / ( 1024 * 1024 ) ) );
	return true;
}

void idVulkanAllocator::Shutdown() {
	if ( device == VK_NULL_HANDLE ) {
		return;
	}

	for ( int i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; i++ ) {
		frameTempBuffer_t &ftb = frameTempBuffers[i];
		if ( ftb.mappedData != NULL && ftb.memory != VK_NULL_HANDLE ) {
			vkUnmapMemory( device, ftb.memory );
			ftb.mappedData = NULL;
		}
		if ( ftb.buffer != VK_NULL_HANDLE ) {
			vkDestroyBuffer( device, ftb.buffer, NULL );
			ftb.buffer = VK_NULL_HANDLE;
		}
		if ( ftb.memory != VK_NULL_HANDLE ) {
			vkFreeMemory( device, ftb.memory, NULL );
			ftb.memory = VK_NULL_HANDLE;
		}
		ftb.currentOffset = 0;
	}

	device = VK_NULL_HANDLE;
	initialized = false;
}

bool idVulkanAllocator::AllocBuffer( VkDeviceSize size, vulkanBufferUsage_t usage, vulkanBuffer_t &out ) {
	memset( &out, 0, sizeof( out ) );

	VkBufferCreateInfo bufInfo = {};
	bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufInfo.size = size;
	bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VkMemoryPropertyFlags memFlags = 0;

	switch ( usage ) {
	case VK_BUFFER_USAGE_VERTEX:
		bufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		memFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		break;
	case VK_BUFFER_USAGE_INDEX:
		bufInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		memFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		break;
	case VK_BUFFER_USAGE_UNIFORM:
		bufInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
		memFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		break;
	case VK_BUFFER_USAGE_STAGING:
		bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		memFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		break;
	case VK_BUFFER_USAGE_FRAME_TEMP:
		// Should use AllocFrameTemp instead
		common->Warning( "AllocBuffer: use AllocFrameTemp for frame-temporary data" );
		return false;
	}

	VkResult result = vkCreateBuffer( device, &bufInfo, NULL, &out.buffer );
	if ( result != VK_SUCCESS ) {
		common->Warning( "vkCreateBuffer failed: %d", (int)result );
		return false;
	}

	VkMemoryRequirements memReqs;
	vkGetBufferMemoryRequirements( device, out.buffer, &memReqs );

	VkMemoryAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memReqs.size;
	allocInfo.memoryTypeIndex = FindMemoryType( memReqs.memoryTypeBits, memFlags );

	if ( allocInfo.memoryTypeIndex == UINT32_MAX ) {
		// Fall back to host-visible if device-local not available
		if ( memFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT ) {
			allocInfo.memoryTypeIndex = FindMemoryType( memReqs.memoryTypeBits,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT );
		}
		if ( allocInfo.memoryTypeIndex == UINT32_MAX ) {
			common->Warning( "No suitable memory type for buffer" );
			vkDestroyBuffer( device, out.buffer, NULL );
			out.buffer = VK_NULL_HANDLE;
			return false;
		}
	}

	result = vkAllocateMemory( device, &allocInfo, NULL, &out.allocation.memory );
	if ( result != VK_SUCCESS ) {
		common->Warning( "vkAllocateMemory failed: %d", (int)result );
		vkDestroyBuffer( device, out.buffer, NULL );
		out.buffer = VK_NULL_HANDLE;
		return false;
	}

	out.allocation.offset = 0;
	out.allocation.size = memReqs.size;

	vkBindBufferMemory( device, out.buffer, out.allocation.memory, 0 );

	// Map host-visible buffers
	if ( memFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT ) {
		vkMapMemory( device, out.allocation.memory, 0, size, 0, &out.allocation.mappedData );
	}

	return true;
}

void idVulkanAllocator::FreeBuffer( vulkanBuffer_t &buf ) {
	if ( device == VK_NULL_HANDLE ) {
		return;
	}

	if ( buf.allocation.mappedData != NULL && buf.allocation.memory != VK_NULL_HANDLE ) {
		vkUnmapMemory( device, buf.allocation.memory );
	}
	if ( buf.buffer != VK_NULL_HANDLE ) {
		vkDestroyBuffer( device, buf.buffer, NULL );
	}
	if ( buf.allocation.memory != VK_NULL_HANDLE ) {
		vkFreeMemory( device, buf.allocation.memory, NULL );
	}

	memset( &buf, 0, sizeof( buf ) );
}

bool idVulkanAllocator::CreateStagingBuffer( VkDeviceSize size, const void *data, vulkanBuffer_t &out ) {
	if ( !AllocBuffer( size, VK_BUFFER_USAGE_STAGING, out ) ) {
		return false;
	}

	if ( data != NULL && out.allocation.mappedData != NULL ) {
		memcpy( out.allocation.mappedData, data, (size_t)size );
	}

	return true;
}

void idVulkanAllocator::BeginFrame( int frameIndex ) {
	if ( !initialized ) {
		return;
	}

	currentFrameIndex = frameIndex;
	frameTempBuffers[frameIndex].currentOffset = 0;
}

bool idVulkanAllocator::AllocFrameTemp( VkDeviceSize size, VkDeviceSize alignment,
	VkBuffer &outBuffer, VkDeviceSize &outOffset, void **outData )
{
	if ( !initialized ) {
		return false;
	}

	frameTempBuffer_t &ftb = frameTempBuffers[currentFrameIndex];

	// Align the offset
	VkDeviceSize alignedOffset = ( ftb.currentOffset + alignment - 1 ) & ~( alignment - 1 );

	if ( alignedOffset + size > FRAME_TEMP_BUFFER_SIZE ) {
		common->Warning( "Frame temp buffer overflow (requested %llu at offset %llu)",
			(unsigned long long)size, (unsigned long long)alignedOffset );
		return false;
	}

	outBuffer = ftb.buffer;
	outOffset = alignedOffset;
	*outData = (byte *)ftb.mappedData + alignedOffset;

	ftb.currentOffset = alignedOffset + size;
	return true;
}

uint32_t idVulkanAllocator::FindMemoryType( uint32_t typeFilter, VkMemoryPropertyFlags properties ) const {
	for ( uint32_t i = 0; i < memProperties.memoryTypeCount; i++ ) {
		if ( ( typeFilter & ( 1 << i ) ) &&
			( memProperties.memoryTypes[i].propertyFlags & properties ) == properties ) {
			return i;
		}
	}
	return UINT32_MAX;
}

#endif // ID_VULKAN
