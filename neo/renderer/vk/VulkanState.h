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

#ifndef __VULKAN_STATE_H__
#define __VULKAN_STATE_H__

#ifdef ID_VULKAN

#include <vulkan/vulkan.h>
#include <vector>

// Global Vulkan state shared between the platform layer, draw backend,
// and subsystems (allocator, render pass, frame sync, etc.).
//
// Populated by the platform during Init(), read by all other Vulkan code.
struct vulkanState_t {
	VkInstance					instance;
	VkPhysicalDevice			physicalDevice;
	VkDevice					device;
	VkQueue						graphicsQueue;
	VkQueue						presentQueue;
	uint32_t					graphicsQueueFamily;
	uint32_t					presentQueueFamily;
	VkSwapchainKHR				swapchain;
	VkFormat					swapchainFormat;
	VkExtent2D					swapchainExtent;
	VkFormat					depthFormat;
	std::vector<VkImage>		swapchainImages;
	std::vector<VkImageView>	swapchainImageViews;

	// Set by draw backend for cross-subsystem access (e.g. ImGui backend)
	VkRenderPass			mainRenderPass;			// valid after draw backend Init()
	VkCommandBuffer			activeCommandBuffer;	// valid between BeginFrame/EndFrame
};

// The single global instance, defined in VulkanBackendPlatform.cpp.
extern vulkanState_t vkState;

#endif // ID_VULKAN
#endif // __VULKAN_STATE_H__
