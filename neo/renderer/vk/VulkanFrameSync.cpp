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

#include "renderer/vk/VulkanFrameSync.h"
#include "framework/Common.h"
#include <cstring>

idVulkanFrameSync::idVulkanFrameSync()
	: device( VK_NULL_HANDLE )
	, currentFrame( 0 )
	, currentImageIndex( 0 )
	, initialized( false )
{
	memset( frames, 0, sizeof( frames ) );
}

bool idVulkanFrameSync::Init( VkDevice device_, uint32_t graphicsQueueFamily ) {
	device = device_;

	for ( int i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; i++ ) {
		vulkanFrameData_t &f = frames[i];

		// Command pool (resettable per frame)
		VkCommandPoolCreateInfo poolInfo = {};
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.queueFamilyIndex = graphicsQueueFamily;
		poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

		VkResult result = vkCreateCommandPool( device, &poolInfo, NULL, &f.commandPool );
		if ( result != VK_SUCCESS ) {
			common->Warning( "vkCreateCommandPool failed for frame %d: %d", i, (int)result );
			Shutdown();
			return false;
		}

		// Command buffer
		VkCommandBufferAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.commandPool = f.commandPool;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = 1;

		result = vkAllocateCommandBuffers( device, &allocInfo, &f.commandBuffer );
		if ( result != VK_SUCCESS ) {
			common->Warning( "vkAllocateCommandBuffers failed for frame %d: %d", i, (int)result );
			Shutdown();
			return false;
		}

		// Semaphores
		VkSemaphoreCreateInfo semInfo = {};
		semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		result = vkCreateSemaphore( device, &semInfo, NULL, &f.imageAvailableSemaphore );
		if ( result != VK_SUCCESS ) {
			common->Warning( "vkCreateSemaphore (imageAvailable) failed for frame %d: %d", i, (int)result );
			Shutdown();
			return false;
		}

		result = vkCreateSemaphore( device, &semInfo, NULL, &f.renderFinishedSemaphore );
		if ( result != VK_SUCCESS ) {
			common->Warning( "vkCreateSemaphore (renderFinished) failed for frame %d: %d", i, (int)result );
			Shutdown();
			return false;
		}

		// Fence (created signaled so the first WaitForFences doesn't block forever)
		VkFenceCreateInfo fenceInfo = {};
		fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

		result = vkCreateFence( device, &fenceInfo, NULL, &f.inFlightFence );
		if ( result != VK_SUCCESS ) {
			common->Warning( "vkCreateFence failed for frame %d: %d", i, (int)result );
			Shutdown();
			return false;
		}
	}

	currentFrame = 0;
	currentImageIndex = 0;
	initialized = true;

	common->Printf( "Vulkan frame sync initialized (%d frames in flight)\n", VK_MAX_FRAMES_IN_FLIGHT );
	return true;
}

void idVulkanFrameSync::Shutdown() {
	if ( device == VK_NULL_HANDLE ) {
		return;
	}

	for ( int i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; i++ ) {
		vulkanFrameData_t &f = frames[i];

		if ( f.inFlightFence != VK_NULL_HANDLE ) {
			vkDestroyFence( device, f.inFlightFence, NULL );
			f.inFlightFence = VK_NULL_HANDLE;
		}
		if ( f.renderFinishedSemaphore != VK_NULL_HANDLE ) {
			vkDestroySemaphore( device, f.renderFinishedSemaphore, NULL );
			f.renderFinishedSemaphore = VK_NULL_HANDLE;
		}
		if ( f.imageAvailableSemaphore != VK_NULL_HANDLE ) {
			vkDestroySemaphore( device, f.imageAvailableSemaphore, NULL );
			f.imageAvailableSemaphore = VK_NULL_HANDLE;
		}
		if ( f.commandPool != VK_NULL_HANDLE ) {
			vkDestroyCommandPool( device, f.commandPool, NULL );
			f.commandPool = VK_NULL_HANDLE;
		}
		f.commandBuffer = VK_NULL_HANDLE;
	}

	device = VK_NULL_HANDLE;
	initialized = false;
}

bool idVulkanFrameSync::BeginFrame( VkSwapchainKHR swapchain ) {
	if ( !initialized ) {
		return false;
	}

	vulkanFrameData_t &f = frames[currentFrame];

	// Wait for this frame's previous work to complete
	vkWaitForFences( device, 1, &f.inFlightFence, VK_TRUE, UINT64_MAX );

	// Acquire the next swapchain image
	VkResult result = vkAcquireNextImageKHR( device, swapchain, UINT64_MAX,
		f.imageAvailableSemaphore, VK_NULL_HANDLE, &currentImageIndex );

	if ( result == VK_ERROR_OUT_OF_DATE_KHR ) {
		return false;
	}
	if ( result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR ) {
		common->Warning( "vkAcquireNextImageKHR failed: %d", (int)result );
		return false;
	}

	// Reset fence only after we know we'll submit work
	vkResetFences( device, 1, &f.inFlightFence );

	// Reset command pool (frees all command buffers allocated from it)
	vkResetCommandPool( device, f.commandPool, 0 );

	// Begin command buffer
	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	result = vkBeginCommandBuffer( f.commandBuffer, &beginInfo );
	if ( result != VK_SUCCESS ) {
		common->Warning( "vkBeginCommandBuffer failed: %d", (int)result );
		return false;
	}

	return true;
}

bool idVulkanFrameSync::EndFrame( VkSwapchainKHR swapchain, VkQueue graphicsQueue, VkQueue presentQueue ) {
	if ( !initialized ) {
		return false;
	}

	vulkanFrameData_t &f = frames[currentFrame];

	// End command buffer
	VkResult result = vkEndCommandBuffer( f.commandBuffer );
	if ( result != VK_SUCCESS ) {
		common->Warning( "vkEndCommandBuffer failed: %d", (int)result );
		return false;
	}

	// Submit command buffer
	VkSemaphore waitSemaphores[] = { f.imageAvailableSemaphore };
	VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
	VkSemaphore signalSemaphores[] = { f.renderFinishedSemaphore };

	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = waitSemaphores;
	submitInfo.pWaitDstStageMask = waitStages;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &f.commandBuffer;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = signalSemaphores;

	result = vkQueueSubmit( graphicsQueue, 1, &submitInfo, f.inFlightFence );
	if ( result != VK_SUCCESS ) {
		common->Warning( "vkQueueSubmit failed: %d", (int)result );
		return false;
	}

	// Present
	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = signalSemaphores;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &swapchain;
	presentInfo.pImageIndices = &currentImageIndex;

	result = vkQueuePresentKHR( presentQueue, &presentInfo );
	if ( result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ) {
		// Swapchain needs recreation, but this frame was still submitted
		currentFrame = ( currentFrame + 1 ) % VK_MAX_FRAMES_IN_FLIGHT;
		return false;
	}
	if ( result != VK_SUCCESS ) {
		common->Warning( "vkQueuePresentKHR failed: %d", (int)result );
		return false;
	}

	// Advance to next frame
	currentFrame = ( currentFrame + 1 ) % VK_MAX_FRAMES_IN_FLIGHT;
	return true;
}

void idVulkanFrameSync::WaitIdle() {
	if ( !initialized || device == VK_NULL_HANDLE ) {
		return;
	}

	// Wait for all in-flight fences
	VkFence fences[VK_MAX_FRAMES_IN_FLIGHT];
	for ( int i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; i++ ) {
		fences[i] = frames[i].inFlightFence;
	}
	vkWaitForFences( device, VK_MAX_FRAMES_IN_FLIGHT, fences, VK_TRUE, UINT64_MAX );
}

VkCommandBuffer idVulkanFrameSync::GetCurrentCommandBuffer() const {
	return frames[currentFrame].commandBuffer;
}

#endif // ID_VULKAN
