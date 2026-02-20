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

#include "renderer/vk/VulkanDescriptorAllocator.h"
#include "framework/Common.h"
#include <cstring>

idVulkanDescriptorAllocator::idVulkanDescriptorAllocator()
	: device( VK_NULL_HANDLE )
	, uboLayout( VK_NULL_HANDLE )
	, samplerLayout( VK_NULL_HANDLE )
	, numFrames( 0 )
	, currentFrame( 0 )
	, initialized( false )
{
	memset( pools, 0, sizeof( pools ) );
}

bool idVulkanDescriptorAllocator::Init( VkDevice device_, VkDescriptorSetLayout uboLayout_,
	VkDescriptorSetLayout samplerLayout_, int framesInFlight )
{
	device = device_;
	uboLayout = uboLayout_;
	samplerLayout = samplerLayout_;
	numFrames = framesInFlight;

	if ( numFrames > MAX_FRAMES ) {
		numFrames = MAX_FRAMES;
	}

	for ( int i = 0; i < numFrames; i++ ) {
		VkDescriptorPoolSize poolSizes[] = {
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MAX_SETS_PER_FRAME * 2 },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_SETS_PER_FRAME * 8 }
		};

		VkDescriptorPoolCreateInfo poolInfo = {};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.poolSizeCount = 2;
		poolInfo.pPoolSizes = poolSizes;
		poolInfo.maxSets = MAX_SETS_PER_FRAME;

		VkResult result = vkCreateDescriptorPool( device, &poolInfo, NULL, &pools[i] );
		if ( result != VK_SUCCESS ) {
			common->Warning( "VulkanDescriptorAllocator: pool %d creation failed: %d", i, (int)result );
			Shutdown();
			return false;
		}
	}

	initialized = true;
	return true;
}

void idVulkanDescriptorAllocator::Shutdown() {
	for ( int i = 0; i < numFrames; i++ ) {
		if ( pools[i] != VK_NULL_HANDLE ) {
			vkDestroyDescriptorPool( device, pools[i], NULL );
			pools[i] = VK_NULL_HANDLE;
		}
	}
	initialized = false;
}

void idVulkanDescriptorAllocator::BeginFrame( int frameIndex ) {
	currentFrame = frameIndex % numFrames;
	if ( pools[currentFrame] != VK_NULL_HANDLE ) {
		vkResetDescriptorPool( device, pools[currentFrame], 0 );
	}
}

VkDescriptorSet idVulkanDescriptorAllocator::AllocUBOSet() {
	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = pools[currentFrame];
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &uboLayout;

	VkDescriptorSet set;
	VkResult result = vkAllocateDescriptorSets( device, &allocInfo, &set );
	if ( result != VK_SUCCESS ) {
		common->Warning( "VulkanDescriptorAllocator: UBO set alloc failed: %d", (int)result );
		return VK_NULL_HANDLE;
	}
	return set;
}

VkDescriptorSet idVulkanDescriptorAllocator::AllocSamplerSet() {
	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = pools[currentFrame];
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &samplerLayout;

	VkDescriptorSet set;
	VkResult result = vkAllocateDescriptorSets( device, &allocInfo, &set );
	if ( result != VK_SUCCESS ) {
		common->Warning( "VulkanDescriptorAllocator: sampler set alloc failed: %d", (int)result );
		return VK_NULL_HANDLE;
	}
	return set;
}

#endif // ID_VULKAN
