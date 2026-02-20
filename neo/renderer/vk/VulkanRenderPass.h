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

#ifndef __VULKAN_RENDER_PASS_H__
#define __VULKAN_RENDER_PASS_H__

#ifdef ID_VULKAN

#include <vulkan/vulkan.h>
#include <vector>

// Maximum number of swapchain images we support framebuffers for.
static const int VK_MAX_SWAPCHAIN_IMAGES = 8;

// Manages the main render pass, depth buffer, and per-swapchain framebuffers.
//
// The dhewm3 pipeline stages are:
//   1. Depth pre-pass (writes depth)
//   2. Stencil shadow volumes (writes stencil, reads depth)
//   3. Light interaction passes (reads depth/stencil, writes color)
//   4. Shader/translucent passes (reads depth, writes color with blending)
//   5. Fog passes (reads depth, writes color with blending)
//   6. 2D GUI overlay
//
// We use a single render pass with all these stages. Vulkan's render pass
// loadOp=CLEAR for the first use, and the stencil/depth are preserved across
// all stages within the same render pass instance.
class idVulkanRenderPassManager {
public:
					idVulkanRenderPassManager();

	bool			Init( VkDevice device, VkPhysicalDevice physicalDevice,
						VkFormat colorFormat, VkFormat depthFormat,
						VkExtent2D extent, uint32_t swapchainImageCount,
						const VkImageView *swapchainImageViews );
	void			Shutdown();

	// Recreate framebuffers after swapchain resize.
	bool			Recreate( VkFormat colorFormat, VkFormat depthFormat,
						VkExtent2D extent, uint32_t swapchainImageCount,
						const VkImageView *swapchainImageViews );

	VkRenderPass	GetMainRenderPass() const { return mainRenderPass; }
	VkRenderPass	GetLoadRenderPass() const { return loadRenderPass; }
	VkFramebuffer	GetFramebuffer( uint32_t imageIndex ) const { return framebuffers[imageIndex]; }
	VkImageView		GetDepthImageView() const { return depthImageView; }
	VkExtent2D		GetExtent() const { return extent; }

private:
	bool			CreateRenderPass( VkFormat colorFormat, VkFormat depthFormat );
	bool			CreateLoadRenderPass( VkFormat colorFormat, VkFormat depthFormat );
	bool			CreateDepthResources( VkFormat depthFormat, VkExtent2D extent );
	bool			CreateFramebuffers( VkExtent2D extent, uint32_t imageCount,
						const VkImageView *swapchainImageViews );
	void			DestroyFramebuffers();
	void			DestroyDepthResources();

	VkDevice		device;
	VkPhysicalDevice physicalDevice;

	VkRenderPass	mainRenderPass;
	VkRenderPass	loadRenderPass;		// LOAD_OP_LOAD variant for mid-frame restart after CopyRender

	VkImage			depthImage;
	VkDeviceMemory	depthImageMemory;
	VkImageView		depthImageView;

	std::vector<VkFramebuffer> framebuffers;
	VkExtent2D		extent;

	bool			initialized;
};

#endif // ID_VULKAN
#endif // __VULKAN_RENDER_PASS_H__
