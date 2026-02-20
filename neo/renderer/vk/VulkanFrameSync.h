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

#ifndef __VULKAN_FRAME_SYNC_H__
#define __VULKAN_FRAME_SYNC_H__

#ifdef ID_VULKAN

#include <vulkan/vulkan.h>

static const int VK_MAX_FRAMES_IN_FLIGHT = 2;

// Per-frame resources for Vulkan rendering.
// Each frame-in-flight has its own command pool, command buffer,
// and synchronization primitives.
struct vulkanFrameData_t {
	VkCommandPool		commandPool;
	VkCommandBuffer		commandBuffer;
	VkSemaphore			imageAvailableSemaphore;
	VkSemaphore			renderFinishedSemaphore;
	VkFence				inFlightFence;
};

// Manages the frame-in-flight resources and the acquire/present lifecycle.
class idVulkanFrameSync {
public:
					idVulkanFrameSync();

	bool			Init( VkDevice device, uint32_t graphicsQueueFamily );
	void			Shutdown();

	// Begin a new frame: waits on the current frame fence, resets the
	// command pool, acquires the next swapchain image, and begins the
	// command buffer.
	// Returns false if the swapchain is out of date (caller should recreate).
	bool			BeginFrame( VkSwapchainKHR swapchain );

	// End the current frame: ends the command buffer, submits it to the
	// graphics queue, and presents the swapchain image.
	// Returns false if the swapchain is out of date.
	bool			EndFrame( VkSwapchainKHR swapchain, VkQueue graphicsQueue, VkQueue presentQueue );

	// Wait for all frames in flight to complete (e.g. before shutdown or resize).
	void			WaitIdle();

	// Access the current frame's command buffer (valid between BeginFrame/EndFrame).
	VkCommandBuffer	GetCurrentCommandBuffer() const;

	// Index of the swapchain image acquired this frame.
	uint32_t		GetCurrentImageIndex() const { return currentImageIndex; }

	// Current frame-in-flight index (0..VK_MAX_FRAMES_IN_FLIGHT-1).
	int				GetCurrentFrameIndex() const { return currentFrame; }

private:
	VkDevice		device;
	vulkanFrameData_t frames[VK_MAX_FRAMES_IN_FLIGHT];
	int				currentFrame;
	uint32_t		currentImageIndex;
	bool			initialized;
};

#endif // ID_VULKAN
#endif // __VULKAN_FRAME_SYNC_H__
