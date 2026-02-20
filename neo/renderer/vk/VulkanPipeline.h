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

#ifndef __VULKAN_PIPELINE_H__
#define __VULKAN_PIPELINE_H__

#ifdef ID_VULKAN

#include <vulkan/vulkan.h>
#include <unordered_map>

// Pipeline types matching dhewm3's rendering stages.
typedef enum {
	VK_PIPELINE_DEPTH_FILL,			// depth pre-pass
	VK_PIPELINE_SHADOW,				// stencil shadow volumes
	VK_PIPELINE_INTERACTION,		// bump-mapped lighting
	VK_PIPELINE_GUI,				// 2D/GUI overlay
	VK_PIPELINE_ENVIRONMENT,		// reflection mapping
	VK_PIPELINE_FOG,				// fog volumes (depth equal, cull back)
	VK_PIPELINE_FOG_CAPS,			// fog frustum caps (depth lequal, cull front)
	VK_PIPELINE_COUNT
} vulkanPipelineType_t;

// Manages Vulkan descriptor set layouts, pipeline layouts, shader modules,
// and graphics pipelines for all rendering stages.
class idVulkanPipelineManager {
public:
					idVulkanPipelineManager();

	bool			Init( VkDevice device, VkRenderPass renderPass,
						VkExtent2D extent, const char *shaderDir );
	void			Shutdown();

	VkPipeline			GetPipeline( vulkanPipelineType_t type ) const;
	VkPipelineLayout	GetPipelineLayout( vulkanPipelineType_t type ) const;

	VkDescriptorSetLayout	GetUBOLayout() const { return uboSetLayout; }
	VkDescriptorSetLayout	GetSamplerLayout() const { return samplerSetLayout; }

	// Create a descriptor pool for allocating descriptor sets.
	VkDescriptorPool	CreateDescriptorPool( uint32_t maxSets ) const;

	// Get or create a cached pipeline for shader pass rendering.
	// glState is the GLS_* state bits, cullMode is CT_FRONT_SIDED/CT_BACK_SIDED/CT_TWO_SIDED.
	VkPipeline		GetShaderPassPipeline( uint64_t glState, int cullMode );
	VkPipelineLayout	GetShaderPassLayout() const;

private:
	VkShaderModule		LoadShaderModule( const uint32_t *code, uint32_t size, const char *name );
	bool				CreateDescriptorSetLayouts();
	bool				CreatePipelineLayouts();
	bool				CreatePipelines( VkRenderPass renderPass, VkExtent2D extent,
							const char *shaderDir );

	VkDevice			device;

	// Descriptor set layouts
	VkDescriptorSetLayout uboSetLayout;		// set 0: uniform buffers
	VkDescriptorSetLayout samplerSetLayout;	// set 1: texture samplers

	// Per-pipeline resources
	struct pipelineEntry_t {
		VkPipelineLayout	layout;
		VkPipeline			pipeline;
		VkShaderModule		vertModule;
		VkShaderModule		fragModule;
	};
	pipelineEntry_t		pipelines[VK_PIPELINE_COUNT];

	// Shader pass pipeline cache: keyed by (GLS_* bits | cull << 32)
	std::unordered_map<uint64_t, VkPipeline> shaderPassCache;
	VkRenderPass		cachedRenderPass;
	VkExtent2D			cachedExtent;

	bool				initialized;
};

// Global pipeline manager, defined in VulkanPipeline.cpp.
extern idVulkanPipelineManager vkPipelineMgr;

#endif // ID_VULKAN
#endif // __VULKAN_PIPELINE_H__
