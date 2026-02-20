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

#include "renderer/vk/VulkanImage.h"
#include "renderer/vk/VulkanState.h"
#include "framework/Common.h"
#include <cstring>

idVulkanTextureManager vkTextureMgr;

// ---------------------------------------------------------------------------

idVulkanTextureManager::idVulkanTextureManager()
	: device( VK_NULL_HANDLE )
	, physicalDevice( VK_NULL_HANDLE )
	, graphicsQueue( VK_NULL_HANDLE )
	, uploadCommandPool( VK_NULL_HANDLE )
	, nextHandle( 1 )		// 0 is reserved as "no texture"
	, initialized( false )
{
	memset( textures, 0, sizeof( textures ) );
}

bool idVulkanTextureManager::Init( VkDevice device_, VkPhysicalDevice physicalDevice_,
	uint32_t graphicsQueueFamily )
{
	device = device_;
	physicalDevice = physicalDevice_;

	vkGetDeviceQueue( device, graphicsQueueFamily, 0, &graphicsQueue );

	// Create command pool for upload operations
	VkCommandPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.queueFamilyIndex = graphicsQueueFamily;
	poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

	VkResult result = vkCreateCommandPool( device, &poolInfo, NULL, &uploadCommandPool );
	if ( result != VK_SUCCESS ) {
		common->Warning( "VulkanTextureManager: failed to create upload command pool: %d", (int)result );
		return false;
	}

	nextHandle = 1;
	initialized = true;
	common->Printf( "Vulkan texture manager initialized\n" );
	return true;
}

void idVulkanTextureManager::Shutdown() {
	if ( device == VK_NULL_HANDLE ) {
		return;
	}

	// Destroy all textures
	for ( int i = 0; i < VK_MAX_TEXTURES; i++ ) {
		if ( textures[i].allocated ) {
			if ( textures[i].sampler != VK_NULL_HANDLE ) {
				vkDestroySampler( device, textures[i].sampler, NULL );
			}
			if ( textures[i].imageView != VK_NULL_HANDLE ) {
				vkDestroyImageView( device, textures[i].imageView, NULL );
			}
			if ( textures[i].image != VK_NULL_HANDLE ) {
				vkDestroyImage( device, textures[i].image, NULL );
			}
			if ( textures[i].memory != VK_NULL_HANDLE ) {
				vkFreeMemory( device, textures[i].memory, NULL );
			}
			memset( &textures[i], 0, sizeof( vulkanTexture_t ) );
		}
	}

	if ( uploadCommandPool != VK_NULL_HANDLE ) {
		vkDestroyCommandPool( device, uploadCommandPool, NULL );
		uploadCommandPool = VK_NULL_HANDLE;
	}

	device = VK_NULL_HANDLE;
	initialized = false;
}

unsigned int idVulkanTextureManager::AllocHandle() {
	// Find next available slot
	for ( unsigned int i = nextHandle; i < VK_MAX_TEXTURES; i++ ) {
		if ( !textures[i].allocated ) {
			nextHandle = i + 1;
			return i;
		}
	}
	// Wrap around
	for ( unsigned int i = 1; i < nextHandle; i++ ) {
		if ( !textures[i].allocated ) {
			nextHandle = i + 1;
			return i;
		}
	}
	common->Warning( "VulkanTextureManager: out of texture handles" );
	return 0;
}

void idVulkanTextureManager::FreeHandle( unsigned int handle ) {
	if ( handle == 0 || handle >= VK_MAX_TEXTURES ) {
		return;
	}

	vulkanTexture_t &tex = textures[handle];
	if ( !tex.allocated ) {
		return;
	}

	if ( tex.sampler != VK_NULL_HANDLE ) {
		vkDestroySampler( device, tex.sampler, NULL );
	}
	if ( tex.imageView != VK_NULL_HANDLE ) {
		vkDestroyImageView( device, tex.imageView, NULL );
	}
	if ( tex.image != VK_NULL_HANDLE ) {
		vkDestroyImage( device, tex.image, NULL );
	}
	if ( tex.memory != VK_NULL_HANDLE ) {
		vkFreeMemory( device, tex.memory, NULL );
	}
	memset( &tex, 0, sizeof( vulkanTexture_t ) );
}

// ---------------------------------------------------------------------------
// Single-time command buffer helpers
// ---------------------------------------------------------------------------

VkCommandBuffer idVulkanTextureManager::BeginSingleTimeCommands() {
	VkCommandBufferAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = uploadCommandPool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = 1;

	VkCommandBuffer cmd;
	vkAllocateCommandBuffers( device, &allocInfo, &cmd );

	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer( cmd, &beginInfo );

	return cmd;
}

void idVulkanTextureManager::EndSingleTimeCommands( VkCommandBuffer cmd ) {
	vkEndCommandBuffer( cmd );

	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &cmd;

	vkQueueSubmit( graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE );
	vkQueueWaitIdle( graphicsQueue );

	vkFreeCommandBuffers( device, uploadCommandPool, 1, &cmd );
}

void idVulkanTextureManager::TransitionImageLayout( VkCommandBuffer cmd, VkImage image,
	VkImageLayout oldLayout, VkImageLayout newLayout,
	uint32_t mipLevels, uint32_t layerCount )
{
	VkImageMemoryBarrier barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = mipLevels;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = layerCount;

	VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

	if ( oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL ) {
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
	} else if ( oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ) {
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	}

	vkCmdPipelineBarrier( cmd, srcStage, dstStage, 0,
		0, NULL, 0, NULL, 1, &barrier );
}

// ---------------------------------------------------------------------------
// Image + View creation
// ---------------------------------------------------------------------------

bool idVulkanTextureManager::CreateImageAndView( unsigned int handle,
	VkImageCreateInfo &imageInfo, VkImageViewType viewType, VkFormat format )
{
	vulkanTexture_t &tex = textures[handle];

	VkResult result = vkCreateImage( device, &imageInfo, NULL, &tex.image );
	if ( result != VK_SUCCESS ) {
		common->Warning( "VulkanTextureManager: vkCreateImage failed: %d", (int)result );
		return false;
	}

	// Allocate memory
	VkMemoryRequirements memReqs;
	vkGetImageMemoryRequirements( device, tex.image, &memReqs );

	VkPhysicalDeviceMemoryProperties memProps;
	vkGetPhysicalDeviceMemoryProperties( physicalDevice, &memProps );

	uint32_t memTypeIndex = UINT32_MAX;
	for ( uint32_t i = 0; i < memProps.memoryTypeCount; i++ ) {
		if ( ( memReqs.memoryTypeBits & ( 1 << i ) ) &&
			( memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT ) ) {
			memTypeIndex = i;
			break;
		}
	}
	if ( memTypeIndex == UINT32_MAX ) {
		common->Warning( "VulkanTextureManager: no device-local memory for image" );
		vkDestroyImage( device, tex.image, NULL );
		tex.image = VK_NULL_HANDLE;
		return false;
	}

	VkMemoryAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memReqs.size;
	allocInfo.memoryTypeIndex = memTypeIndex;

	result = vkAllocateMemory( device, &allocInfo, NULL, &tex.memory );
	if ( result != VK_SUCCESS ) {
		common->Warning( "VulkanTextureManager: vkAllocateMemory failed: %d", (int)result );
		vkDestroyImage( device, tex.image, NULL );
		tex.image = VK_NULL_HANDLE;
		return false;
	}

	vkBindImageMemory( device, tex.image, tex.memory, 0 );

	// Create image view
	VkImageViewCreateInfo viewInfo = {};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = tex.image;
	viewInfo.viewType = viewType;
	viewInfo.format = format;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = tex.mipLevels;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = tex.layerCount;

	result = vkCreateImageView( device, &viewInfo, NULL, &tex.imageView );
	if ( result != VK_SUCCESS ) {
		common->Warning( "VulkanTextureManager: vkCreateImageView failed: %d", (int)result );
		return false;
	}

	return true;
}

bool idVulkanTextureManager::CreateSampler( unsigned int handle,
	VkFilter minFilter, VkFilter magFilter,
	VkSamplerAddressMode addressMode, uint32_t mipLevels )
{
	vulkanTexture_t &tex = textures[handle];

	VkSamplerCreateInfo samplerInfo = {};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = magFilter;
	samplerInfo.minFilter = minFilter;
	samplerInfo.mipmapMode = ( minFilter == VK_FILTER_LINEAR ) ?
		VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
	samplerInfo.addressModeU = addressMode;
	samplerInfo.addressModeV = addressMode;
	samplerInfo.addressModeW = addressMode;
	samplerInfo.mipLodBias = 0.0f;
	samplerInfo.anisotropyEnable = VK_TRUE;
	samplerInfo.maxAnisotropy = 8.0f;
	samplerInfo.compareEnable = VK_FALSE;
	samplerInfo.minLod = 0.0f;
	samplerInfo.maxLod = (float)mipLevels;
	samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
	samplerInfo.unnormalizedCoordinates = VK_FALSE;

	VkResult result = vkCreateSampler( device, &samplerInfo, NULL, &tex.sampler );
	if ( result != VK_SUCCESS ) {
		common->Warning( "VulkanTextureManager: vkCreateSampler failed: %d", (int)result );
		return false;
	}

	return true;
}

// ---------------------------------------------------------------------------
// 2D texture creation
// ---------------------------------------------------------------------------

bool idVulkanTextureManager::CreateTexture2D( unsigned int handle,
	const byte *pic, int width, int height,
	int mipLevels, VkFormat format,
	VkFilter minFilter, VkFilter magFilter,
	VkSamplerAddressMode addressMode )
{
	if ( !initialized || handle == 0 || handle >= VK_MAX_TEXTURES ) {
		return false;
	}

	vulkanTexture_t &tex = textures[handle];
	tex.format = format;
	tex.width = width;
	tex.height = height;
	tex.depth = 1;
	tex.mipLevels = mipLevels;
	tex.layerCount = 1;
	tex.isCubeMap = false;
	tex.is3D = false;
	tex.allocated = true;

	VkImageCreateInfo imageInfo = {};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.format = format;
	imageInfo.extent.width = width;
	imageInfo.extent.height = height;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = mipLevels;
	imageInfo.arrayLayers = 1;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	if ( !CreateImageAndView( handle, imageInfo, VK_IMAGE_VIEW_TYPE_2D, format ) ) {
		tex.allocated = false;
		return false;
	}

	if ( !CreateSampler( handle, minFilter, magFilter, addressMode, mipLevels ) ) {
		FreeHandle( handle );
		return false;
	}

	// Upload pixel data via staging buffer
	if ( pic != NULL ) {
		VkDeviceSize imageSize = (VkDeviceSize)width * height * 4;	// RGBA8

		// Create staging buffer
		VkBuffer stagingBuffer;
		VkDeviceMemory stagingMemory;

		VkBufferCreateInfo bufInfo = {};
		bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufInfo.size = imageSize;
		bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		vkCreateBuffer( device, &bufInfo, NULL, &stagingBuffer );

		VkMemoryRequirements memReqs;
		vkGetBufferMemoryRequirements( device, stagingBuffer, &memReqs );

		VkPhysicalDeviceMemoryProperties memProps;
		vkGetPhysicalDeviceMemoryProperties( physicalDevice, &memProps );

		uint32_t memType = UINT32_MAX;
		for ( uint32_t i = 0; i < memProps.memoryTypeCount; i++ ) {
			if ( ( memReqs.memoryTypeBits & ( 1 << i ) ) &&
				( memProps.memoryTypes[i].propertyFlags &
					( VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT ) ) ==
					( VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT ) ) {
				memType = i;
				break;
			}
		}

		VkMemoryAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memReqs.size;
		allocInfo.memoryTypeIndex = memType;
		vkAllocateMemory( device, &allocInfo, NULL, &stagingMemory );
		vkBindBufferMemory( device, stagingBuffer, stagingMemory, 0 );

		// Copy data to staging buffer
		void *mapped;
		vkMapMemory( device, stagingMemory, 0, imageSize, 0, &mapped );
		memcpy( mapped, pic, (size_t)imageSize );
		vkUnmapMemory( device, stagingMemory );

		// Transfer: transition, copy, transition
		VkCommandBuffer cmd = BeginSingleTimeCommands();

		TransitionImageLayout( cmd, tex.image,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			mipLevels, 1 );

		VkBufferImageCopy region = {};
		region.bufferOffset = 0;
		region.bufferRowLength = 0;
		region.bufferImageHeight = 0;
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.mipLevel = 0;
		region.imageSubresource.baseArrayLayer = 0;
		region.imageSubresource.layerCount = 1;
		region.imageOffset = { 0, 0, 0 };
		region.imageExtent = { (uint32_t)width, (uint32_t)height, 1 };

		vkCmdCopyBufferToImage( cmd, stagingBuffer, tex.image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region );

		TransitionImageLayout( cmd, tex.image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			mipLevels, 1 );

		EndSingleTimeCommands( cmd );

		// Cleanup staging
		vkDestroyBuffer( device, stagingBuffer, NULL );
		vkFreeMemory( device, stagingMemory, NULL );
	}

	return true;
}

// ---------------------------------------------------------------------------
// 3D texture creation
// ---------------------------------------------------------------------------

bool idVulkanTextureManager::CreateTexture3D( unsigned int handle,
	const byte *pic, int width, int height, int depth,
	int mipLevels, VkFormat format,
	VkFilter minFilter, VkFilter magFilter,
	VkSamplerAddressMode addressMode )
{
	if ( !initialized || handle == 0 || handle >= VK_MAX_TEXTURES ) {
		return false;
	}

	vulkanTexture_t &tex = textures[handle];
	tex.format = format;
	tex.width = width;
	tex.height = height;
	tex.depth = depth;
	tex.mipLevels = mipLevels;
	tex.layerCount = 1;
	tex.isCubeMap = false;
	tex.is3D = true;
	tex.allocated = true;

	VkImageCreateInfo imageInfo = {};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_3D;
	imageInfo.format = format;
	imageInfo.extent.width = width;
	imageInfo.extent.height = height;
	imageInfo.extent.depth = depth;
	imageInfo.mipLevels = mipLevels;
	imageInfo.arrayLayers = 1;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	if ( !CreateImageAndView( handle, imageInfo, VK_IMAGE_VIEW_TYPE_3D, format ) ) {
		tex.allocated = false;
		return false;
	}

	if ( !CreateSampler( handle, minFilter, magFilter, addressMode, mipLevels ) ) {
		FreeHandle( handle );
		return false;
	}

	// Upload is similar to 2D but with depth extent
	if ( pic != NULL ) {
		VkDeviceSize imageSize = (VkDeviceSize)width * height * depth * 4;

		VkBuffer stagingBuffer;
		VkDeviceMemory stagingMemory;

		VkBufferCreateInfo bufInfo = {};
		bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufInfo.size = imageSize;
		bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		vkCreateBuffer( device, &bufInfo, NULL, &stagingBuffer );

		VkMemoryRequirements memReqs;
		vkGetBufferMemoryRequirements( device, stagingBuffer, &memReqs );

		VkPhysicalDeviceMemoryProperties memProps;
		vkGetPhysicalDeviceMemoryProperties( physicalDevice, &memProps );
		uint32_t memType = UINT32_MAX;
		for ( uint32_t i = 0; i < memProps.memoryTypeCount; i++ ) {
			if ( ( memReqs.memoryTypeBits & ( 1 << i ) ) &&
				( memProps.memoryTypes[i].propertyFlags &
					( VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT ) ) ==
					( VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT ) ) {
				memType = i;
				break;
			}
		}

		VkMemoryAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memReqs.size;
		allocInfo.memoryTypeIndex = memType;
		vkAllocateMemory( device, &allocInfo, NULL, &stagingMemory );
		vkBindBufferMemory( device, stagingBuffer, stagingMemory, 0 );

		void *mapped;
		vkMapMemory( device, stagingMemory, 0, imageSize, 0, &mapped );
		memcpy( mapped, pic, (size_t)imageSize );
		vkUnmapMemory( device, stagingMemory );

		VkCommandBuffer cmd = BeginSingleTimeCommands();

		TransitionImageLayout( cmd, tex.image,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			mipLevels, 1 );

		VkBufferImageCopy region = {};
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.mipLevel = 0;
		region.imageSubresource.baseArrayLayer = 0;
		region.imageSubresource.layerCount = 1;
		region.imageExtent = { (uint32_t)width, (uint32_t)height, (uint32_t)depth };

		vkCmdCopyBufferToImage( cmd, stagingBuffer, tex.image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region );

		TransitionImageLayout( cmd, tex.image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			mipLevels, 1 );

		EndSingleTimeCommands( cmd );

		vkDestroyBuffer( device, stagingBuffer, NULL );
		vkFreeMemory( device, stagingMemory, NULL );
	}

	return true;
}

// ---------------------------------------------------------------------------
// Cubemap creation
// ---------------------------------------------------------------------------

bool idVulkanTextureManager::CreateTextureCube( unsigned int handle,
	const byte *faces[6], int size,
	int mipLevels, VkFormat format,
	VkFilter minFilter, VkFilter magFilter )
{
	if ( !initialized || handle == 0 || handle >= VK_MAX_TEXTURES ) {
		return false;
	}

	vulkanTexture_t &tex = textures[handle];
	tex.format = format;
	tex.width = size;
	tex.height = size;
	tex.depth = 1;
	tex.mipLevels = mipLevels;
	tex.layerCount = 6;
	tex.isCubeMap = true;
	tex.is3D = false;
	tex.allocated = true;

	VkImageCreateInfo imageInfo = {};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.format = format;
	imageInfo.extent.width = size;
	imageInfo.extent.height = size;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = mipLevels;
	imageInfo.arrayLayers = 6;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

	if ( !CreateImageAndView( handle, imageInfo, VK_IMAGE_VIEW_TYPE_CUBE, format ) ) {
		tex.allocated = false;
		return false;
	}

	if ( !CreateSampler( handle, minFilter, magFilter,
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, mipLevels ) )
	{
		FreeHandle( handle );
		return false;
	}

	// Upload all 6 faces
	if ( faces != NULL && faces[0] != NULL ) {
		VkDeviceSize faceSize = (VkDeviceSize)size * size * 4;
		VkDeviceSize totalSize = faceSize * 6;

		VkBuffer stagingBuffer;
		VkDeviceMemory stagingMemory;

		VkBufferCreateInfo bufInfo = {};
		bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufInfo.size = totalSize;
		bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		vkCreateBuffer( device, &bufInfo, NULL, &stagingBuffer );

		VkMemoryRequirements memReqs;
		vkGetBufferMemoryRequirements( device, stagingBuffer, &memReqs );

		VkPhysicalDeviceMemoryProperties memProps;
		vkGetPhysicalDeviceMemoryProperties( physicalDevice, &memProps );
		uint32_t memType = UINT32_MAX;
		for ( uint32_t i = 0; i < memProps.memoryTypeCount; i++ ) {
			if ( ( memReqs.memoryTypeBits & ( 1 << i ) ) &&
				( memProps.memoryTypes[i].propertyFlags &
					( VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT ) ) ==
					( VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT ) ) {
				memType = i;
				break;
			}
		}

		VkMemoryAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memReqs.size;
		allocInfo.memoryTypeIndex = memType;
		vkAllocateMemory( device, &allocInfo, NULL, &stagingMemory );
		vkBindBufferMemory( device, stagingBuffer, stagingMemory, 0 );

		void *mapped;
		vkMapMemory( device, stagingMemory, 0, totalSize, 0, &mapped );
		for ( int face = 0; face < 6; face++ ) {
			memcpy( (byte *)mapped + face * faceSize, faces[face], (size_t)faceSize );
		}
		vkUnmapMemory( device, stagingMemory );

		VkCommandBuffer cmd = BeginSingleTimeCommands();

		TransitionImageLayout( cmd, tex.image,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			mipLevels, 6 );

		VkBufferImageCopy regions[6] = {};
		for ( int face = 0; face < 6; face++ ) {
			regions[face].bufferOffset = face * faceSize;
			regions[face].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			regions[face].imageSubresource.mipLevel = 0;
			regions[face].imageSubresource.baseArrayLayer = face;
			regions[face].imageSubresource.layerCount = 1;
			regions[face].imageExtent = { (uint32_t)size, (uint32_t)size, 1 };
		}

		vkCmdCopyBufferToImage( cmd, stagingBuffer, tex.image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 6, regions );

		TransitionImageLayout( cmd, tex.image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			mipLevels, 6 );

		EndSingleTimeCommands( cmd );

		vkDestroyBuffer( device, stagingBuffer, NULL );
		vkFreeMemory( device, stagingMemory, NULL );
	}

	return true;
}

// ---------------------------------------------------------------------------

const vulkanTexture_t * idVulkanTextureManager::GetTexture( unsigned int handle ) const {
	if ( handle == 0 || handle >= VK_MAX_TEXTURES || !textures[handle].allocated ) {
		return NULL;
	}
	return &textures[handle];
}

void idVulkanTextureManager::UploadImageData( VkImage image, const byte *data,
	uint32_t width, uint32_t height, uint32_t depth,
	uint32_t mipLevel, uint32_t layer,
	VkImageLayout oldLayout, VkImageLayout newLayout )
{
	// Placeholder for incremental mip uploads
}

void idVulkanTextureManager::FlushUploads() {
	// Currently uploads are synchronous via single-time commands.
	// Future optimization: batch uploads in a single command buffer.
}

#endif // ID_VULKAN
