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

#ifndef __VULKAN_IMAGE_H__
#define __VULKAN_IMAGE_H__

#ifdef ID_VULKAN

#include <vulkan/vulkan.h>
#include <vector>

// Maximum number of Vulkan textures we track.
static const int VK_MAX_TEXTURES = 4096;

// A single Vulkan texture with its resources.
struct vulkanTexture_t {
	VkImage				image;
	VkDeviceMemory		memory;
	VkImageView			imageView;
	VkSampler			sampler;
	VkFormat			format;
	uint32_t			width;
	uint32_t			height;
	uint32_t			depth;
	uint32_t			mipLevels;
	uint32_t			layerCount;		// 6 for cubemaps, 1 otherwise
	bool				isCubeMap;
	bool				is3D;
	bool				allocated;
};

// Manages Vulkan texture resources for the renderer.
//
// Each idImage has a texnum field (unsigned int). When Vulkan is active,
// texnum is used as an index into this manager's texture array.
// The Generate* functions in Image_load.cpp call into this manager
// instead of making GL calls.
class idVulkanTextureManager {
public:
					idVulkanTextureManager();

	bool			Init( VkDevice device, VkPhysicalDevice physicalDevice,
						uint32_t graphicsQueueFamily );
	void			Shutdown();

	// Allocate a texture handle (returns texnum-compatible index).
	unsigned int	AllocHandle();

	// Free a texture handle.
	void			FreeHandle( unsigned int handle );

	// Create a 2D texture with mipmap data.
	// pic is RGBA8, uploadWidth x uploadHeight.
	bool			CreateTexture2D( unsigned int handle,
						const unsigned char *pic, int width, int height,
						int mipLevels, VkFormat format,
						VkFilter minFilter, VkFilter magFilter,
						VkSamplerAddressMode addressMode );

	// Create a 3D (volume) texture.
	bool			CreateTexture3D( unsigned int handle,
						const unsigned char *pic, int width, int height, int depth,
						int mipLevels, VkFormat format,
						VkFilter minFilter, VkFilter magFilter,
						VkSamplerAddressMode addressMode );

	// Create a cubemap texture.
	bool			CreateTextureCube( unsigned int handle,
						const unsigned char *faces[6], int size,
						int mipLevels, VkFormat format,
						VkFilter minFilter, VkFilter magFilter );

	// Get the texture resources for binding.
	const vulkanTexture_t *	GetTexture( unsigned int handle ) const;

	bool			IsInitialized() const { return initialized; }

	// Utility: upload mip data to an existing image via staging buffer.
	void			UploadImageData( VkImage image, const unsigned char *data,
						uint32_t width, uint32_t height, uint32_t depth,
						uint32_t mipLevel, uint32_t layer,
						VkImageLayout oldLayout, VkImageLayout newLayout );

	// Execute pending uploads (call after all textures are created).
	void			FlushUploads();

private:
	bool			CreateImageAndView( unsigned int handle, VkImageCreateInfo &imageInfo,
						VkImageViewType viewType, VkFormat format );
	bool			CreateSampler( unsigned int handle, VkFilter minFilter, VkFilter magFilter,
						VkSamplerAddressMode addressMode, uint32_t mipLevels );
	VkCommandBuffer	BeginSingleTimeCommands();
	void			EndSingleTimeCommands( VkCommandBuffer commandBuffer );
	void			TransitionImageLayout( VkCommandBuffer cmd, VkImage image,
						VkImageLayout oldLayout, VkImageLayout newLayout,
						uint32_t mipLevels, uint32_t layerCount );

	VkDevice		device;
	VkPhysicalDevice physicalDevice;
	VkQueue			graphicsQueue;
	VkCommandPool	uploadCommandPool;

	vulkanTexture_t	textures[VK_MAX_TEXTURES];
	unsigned int	nextHandle;
	bool			initialized;
};

// Global texture manager, defined in VulkanImage.cpp.
extern idVulkanTextureManager vkTextureMgr;

#endif // ID_VULKAN
#endif // __VULKAN_IMAGE_H__
