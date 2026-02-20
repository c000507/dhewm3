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

#include "renderer/vk/VulkanRenderPass.h"
#include "renderer/vk/VulkanAllocator.h"
#include "framework/Common.h"
#include <cstring>

idVulkanRenderPassManager::idVulkanRenderPassManager()
	: device( VK_NULL_HANDLE )
	, physicalDevice( VK_NULL_HANDLE )
	, mainRenderPass( VK_NULL_HANDLE )
	, loadRenderPass( VK_NULL_HANDLE )
	, depthImage( VK_NULL_HANDLE )
	, depthImageMemory( VK_NULL_HANDLE )
	, depthImageView( VK_NULL_HANDLE )
	, initialized( false )
{
	extent.width = 0;
	extent.height = 0;
}

bool idVulkanRenderPassManager::Init( VkDevice device_, VkPhysicalDevice physicalDevice_,
	VkFormat colorFormat, VkFormat depthFormat,
	VkExtent2D extent_, uint32_t swapchainImageCount,
	const VkImageView *swapchainImageViews )
{
	device = device_;
	physicalDevice = physicalDevice_;
	extent = extent_;

	if ( !CreateRenderPass( colorFormat, depthFormat ) ) {
		return false;
	}

	if ( !CreateLoadRenderPass( colorFormat, depthFormat ) ) {
		Shutdown();
		return false;
	}

	if ( !CreateDepthResources( depthFormat, extent ) ) {
		Shutdown();
		return false;
	}

	if ( !CreateFramebuffers( extent, swapchainImageCount, swapchainImageViews ) ) {
		Shutdown();
		return false;
	}

	initialized = true;
	common->Printf( "Vulkan render pass created (%dx%d, %d framebuffers)\n",
		extent.width, extent.height, swapchainImageCount );
	return true;
}

void idVulkanRenderPassManager::Shutdown() {
	if ( device == VK_NULL_HANDLE ) {
		return;
	}

	DestroyFramebuffers();
	DestroyDepthResources();

	if ( loadRenderPass != VK_NULL_HANDLE ) {
		vkDestroyRenderPass( device, loadRenderPass, NULL );
		loadRenderPass = VK_NULL_HANDLE;
	}

	if ( mainRenderPass != VK_NULL_HANDLE ) {
		vkDestroyRenderPass( device, mainRenderPass, NULL );
		mainRenderPass = VK_NULL_HANDLE;
	}

	initialized = false;
}

bool idVulkanRenderPassManager::Recreate( VkFormat colorFormat, VkFormat depthFormat,
	VkExtent2D extent_, uint32_t swapchainImageCount,
	const VkImageView *swapchainImageViews )
{
	DestroyFramebuffers();
	DestroyDepthResources();

	extent = extent_;

	if ( mainRenderPass == VK_NULL_HANDLE ) {
		if ( !CreateRenderPass( colorFormat, depthFormat ) ) {
			return false;
		}
	}

	if ( loadRenderPass == VK_NULL_HANDLE ) {
		if ( !CreateLoadRenderPass( colorFormat, depthFormat ) ) {
			return false;
		}
	}

	if ( !CreateDepthResources( depthFormat, extent ) ) {
		return false;
	}

	if ( !CreateFramebuffers( extent, swapchainImageCount, swapchainImageViews ) ) {
		return false;
	}

	common->Printf( "Vulkan render pass recreated (%dx%d, %d framebuffers)\n",
		extent.width, extent.height, swapchainImageCount );
	return true;
}

// ---------------------------------------------------------------------------
// Render pass creation
// ---------------------------------------------------------------------------

bool idVulkanRenderPassManager::CreateRenderPass( VkFormat colorFormat, VkFormat depthFormat ) {
	// Color attachment (swapchain image)
	VkAttachmentDescription colorAttachment = {};
	colorAttachment.format = colorFormat;
	colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	// Depth/stencil attachment
	VkAttachmentDescription depthAttachment = {};
	depthAttachment.format = depthFormat;
	depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference colorAttachmentRef = {};
	colorAttachmentRef.attachment = 0;
	colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depthAttachmentRef = {};
	depthAttachmentRef.attachment = 1;
	depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	// Single subpass that handles all pipeline stages.
	// Depth/stencil writes and reads happen within the same subpass,
	// and state changes (depth write enable, stencil test, etc.)
	// are controlled by dynamic state or pipeline objects.
	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorAttachmentRef;
	subpass.pDepthStencilAttachment = &depthAttachmentRef;

	// Dependency: ensure the color attachment output stage waits for
	// the image to be available from the presentation engine.
	VkSubpassDependency dependency = {};
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
		| VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	dependency.srcAccessMask = 0;
	dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
		| VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
		| VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

	VkAttachmentDescription attachments[] = { colorAttachment, depthAttachment };

	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = 2;
	renderPassInfo.pAttachments = attachments;
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;
	renderPassInfo.dependencyCount = 1;
	renderPassInfo.pDependencies = &dependency;

	VkResult result = vkCreateRenderPass( device, &renderPassInfo, NULL, &mainRenderPass );
	if ( result != VK_SUCCESS ) {
		common->Warning( "vkCreateRenderPass failed: %d", (int)result );
		return false;
	}

	return true;
}

// ---------------------------------------------------------------------------
// Load render pass (for mid-frame restart after CopyRender)
//
// Identical to main pass except LOAD_OP_LOAD and initialLayout matching
// the state of attachments after ending the previous render pass instance.
// ---------------------------------------------------------------------------

bool idVulkanRenderPassManager::CreateLoadRenderPass( VkFormat colorFormat, VkFormat depthFormat ) {
	// Color: load existing contents (from before the copy)
	VkAttachmentDescription colorAttachment = {};
	colorAttachment.format = colorFormat;
	colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	colorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	// Depth/stencil: load existing contents
	VkAttachmentDescription depthAttachment = {};
	depthAttachment.format = depthFormat;
	depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthAttachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference colorAttachmentRef = {};
	colorAttachmentRef.attachment = 0;
	colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depthAttachmentRef = {};
	depthAttachmentRef.attachment = 1;
	depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorAttachmentRef;
	subpass.pDepthStencilAttachment = &depthAttachmentRef;

	VkSubpassDependency dependency = {};
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
		| VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	dependency.srcAccessMask = 0;
	dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
		| VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
		| VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

	VkAttachmentDescription attachments[] = { colorAttachment, depthAttachment };

	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = 2;
	renderPassInfo.pAttachments = attachments;
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;
	renderPassInfo.dependencyCount = 1;
	renderPassInfo.pDependencies = &dependency;

	VkResult result = vkCreateRenderPass( device, &renderPassInfo, NULL, &loadRenderPass );
	if ( result != VK_SUCCESS ) {
		common->Warning( "vkCreateRenderPass (load) failed: %d", (int)result );
		return false;
	}

	return true;
}

// ---------------------------------------------------------------------------
// Depth buffer
// ---------------------------------------------------------------------------

bool idVulkanRenderPassManager::CreateDepthResources( VkFormat depthFormat, VkExtent2D ext ) {
	VkImageCreateInfo imageInfo = {};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.format = depthFormat;
	imageInfo.extent.width = ext.width;
	imageInfo.extent.height = ext.height;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VkResult result = vkCreateImage( device, &imageInfo, NULL, &depthImage );
	if ( result != VK_SUCCESS ) {
		common->Warning( "vkCreateImage (depth) failed: %d", (int)result );
		return false;
	}

	VkMemoryRequirements memReqs;
	vkGetImageMemoryRequirements( device, depthImage, &memReqs );

	// Find device-local memory type
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
		common->Warning( "No suitable memory type for depth image" );
		return false;
	}

	VkMemoryAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memReqs.size;
	allocInfo.memoryTypeIndex = memTypeIndex;

	result = vkAllocateMemory( device, &allocInfo, NULL, &depthImageMemory );
	if ( result != VK_SUCCESS ) {
		common->Warning( "vkAllocateMemory (depth) failed: %d", (int)result );
		return false;
	}

	vkBindImageMemory( device, depthImage, depthImageMemory, 0 );

	// Determine aspect flags based on format
	VkImageAspectFlags aspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT;
	if ( depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT ||
		depthFormat == VK_FORMAT_D24_UNORM_S8_UINT ) {
		aspectFlags |= VK_IMAGE_ASPECT_STENCIL_BIT;
	}

	VkImageViewCreateInfo viewInfo = {};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = depthImage;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = depthFormat;
	viewInfo.subresourceRange.aspectMask = aspectFlags;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = 1;

	result = vkCreateImageView( device, &viewInfo, NULL, &depthImageView );
	if ( result != VK_SUCCESS ) {
		common->Warning( "vkCreateImageView (depth) failed: %d", (int)result );
		return false;
	}

	return true;
}

void idVulkanRenderPassManager::DestroyDepthResources() {
	if ( depthImageView != VK_NULL_HANDLE ) {
		vkDestroyImageView( device, depthImageView, NULL );
		depthImageView = VK_NULL_HANDLE;
	}
	if ( depthImage != VK_NULL_HANDLE ) {
		vkDestroyImage( device, depthImage, NULL );
		depthImage = VK_NULL_HANDLE;
	}
	if ( depthImageMemory != VK_NULL_HANDLE ) {
		vkFreeMemory( device, depthImageMemory, NULL );
		depthImageMemory = VK_NULL_HANDLE;
	}
}

// ---------------------------------------------------------------------------
// Framebuffers
// ---------------------------------------------------------------------------

bool idVulkanRenderPassManager::CreateFramebuffers( VkExtent2D ext, uint32_t imageCount,
	const VkImageView *swapchainImageViews )
{
	framebuffers.resize( imageCount );

	for ( uint32_t i = 0; i < imageCount; i++ ) {
		VkImageView attachments[] = {
			swapchainImageViews[i],
			depthImageView
		};

		VkFramebufferCreateInfo fbInfo = {};
		fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fbInfo.renderPass = mainRenderPass;
		fbInfo.attachmentCount = 2;
		fbInfo.pAttachments = attachments;
		fbInfo.width = ext.width;
		fbInfo.height = ext.height;
		fbInfo.layers = 1;

		VkResult result = vkCreateFramebuffer( device, &fbInfo, NULL, &framebuffers[i] );
		if ( result != VK_SUCCESS ) {
			common->Warning( "vkCreateFramebuffer failed for image %d: %d", i, (int)result );
			return false;
		}
	}

	return true;
}

void idVulkanRenderPassManager::DestroyFramebuffers() {
	for ( size_t i = 0; i < framebuffers.size(); i++ ) {
		if ( framebuffers[i] != VK_NULL_HANDLE ) {
			vkDestroyFramebuffer( device, framebuffers[i], NULL );
		}
	}
	framebuffers.clear();
}

#endif // ID_VULKAN
