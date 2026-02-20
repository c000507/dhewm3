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

#include "renderer/vk/VulkanPipeline.h"
#include "framework/Common.h"
#include <cstring>

// Embedded SPIR-V bytecode (generated at build time from GLSL sources)
#include "generated/depth_fill_vert_spv.h"
#include "generated/depth_fill_frag_spv.h"
#include "generated/shadow_vert_spv.h"
#include "generated/shadow_frag_spv.h"
#include "generated/interaction_vert_spv.h"
#include "generated/interaction_frag_spv.h"
#include "generated/gui_vert_spv.h"
#include "generated/gui_frag_spv.h"
#include "generated/environment_vert_spv.h"
#include "generated/environment_frag_spv.h"
#include "generated/fog_vert_spv.h"
#include "generated/fog_frag_spv.h"

idVulkanPipelineManager vkPipelineMgr;

// ---------------------------------------------------------------------------
// idDrawVert layout (from DrawVert.h):
//   vec3 xyz       (offset 0,  location 0)
//   vec2 st        (offset 12, location 1)
//   vec3 normal    (offset 20, location 4)
//   vec3 tangent0  (offset 32, location 2)
//   vec3 tangent1  (offset 44, location 3)
//   byte color[4]  (offset 56, location 5)
// Total stride: 60 bytes
// ---------------------------------------------------------------------------

static const int DRAWVERT_STRIDE = 60;

// Vertex input for the full idDrawVert (used by interaction, gui, environment)
static VkVertexInputBindingDescription GetFullVertexBinding() {
	VkVertexInputBindingDescription binding = {};
	binding.binding = 0;
	binding.stride = DRAWVERT_STRIDE;
	binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	return binding;
}

// Full vertex attributes: position, texcoord, tangent, bitangent, normal, color
static void GetFullVertexAttributes( VkVertexInputAttributeDescription *attrs, uint32_t &count ) {
	count = 6;

	// location 0: position (vec3)
	attrs[0].binding = 0;
	attrs[0].location = 0;
	attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
	attrs[0].offset = 0;

	// location 1: texcoord (vec2)
	attrs[1].binding = 0;
	attrs[1].location = 1;
	attrs[1].format = VK_FORMAT_R32G32_SFLOAT;
	attrs[1].offset = 12;

	// location 2: tangent (vec3)
	attrs[2].binding = 0;
	attrs[2].location = 2;
	attrs[2].format = VK_FORMAT_R32G32B32_SFLOAT;
	attrs[2].offset = 32;

	// location 3: bitangent (vec3)
	attrs[3].binding = 0;
	attrs[3].location = 3;
	attrs[3].format = VK_FORMAT_R32G32B32_SFLOAT;
	attrs[3].offset = 44;

	// location 4: normal (vec3)
	attrs[4].binding = 0;
	attrs[4].location = 4;
	attrs[4].format = VK_FORMAT_R32G32B32_SFLOAT;
	attrs[4].offset = 20;

	// location 5: color (R8G8B8A8 normalized)
	attrs[5].binding = 0;
	attrs[5].location = 5;
	attrs[5].format = VK_FORMAT_R8G8B8A8_UNORM;
	attrs[5].offset = 56;
}

// Position-only vertex attributes for depth fill
static void GetPositionOnlyAttributes( VkVertexInputAttributeDescription *attrs, uint32_t &count ) {
	count = 1;
	attrs[0].binding = 0;
	attrs[0].location = 0;
	attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
	attrs[0].offset = 0;
}

// GUI vertex attributes: position (0), texcoord (1), color (5).
// Used by gui.vert which does not read tangent/bitangent/normal.
static void GetGuiVertexAttributes( VkVertexInputAttributeDescription *attrs, uint32_t &count ) {
	count = 3;
	// location 0: position (vec3)
	attrs[0].binding = 0;  attrs[0].location = 0;
	attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;  attrs[0].offset = 0;
	// location 1: texcoord (vec2)
	attrs[1].binding = 0;  attrs[1].location = 1;
	attrs[1].format = VK_FORMAT_R32G32_SFLOAT;  attrs[1].offset = 12;
	// location 5: color (R8G8B8A8)
	attrs[2].binding = 0;  attrs[2].location = 5;
	attrs[2].format = VK_FORMAT_R8G8B8A8_UNORM;  attrs[2].offset = 56;
}

// Environment vertex attributes: position (0), texcoord (1), normal (4).
// Used by environment.vert which does not read tangent/bitangent/color.
static void GetEnvironmentVertexAttributes( VkVertexInputAttributeDescription *attrs, uint32_t &count ) {
	count = 3;
	// location 0: position (vec3)
	attrs[0].binding = 0;  attrs[0].location = 0;
	attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;  attrs[0].offset = 0;
	// location 1: texcoord (vec2)
	attrs[1].binding = 0;  attrs[1].location = 1;
	attrs[1].format = VK_FORMAT_R32G32_SFLOAT;  attrs[1].offset = 12;
	// location 4: normal (vec3)
	attrs[2].binding = 0;  attrs[2].location = 4;
	attrs[2].format = VK_FORMAT_R32G32B32_SFLOAT;  attrs[2].offset = 20;
}

// Shadow vertex: vec4 (xyz + w for extrusion flag)
static VkVertexInputBindingDescription GetShadowVertexBinding() {
	VkVertexInputBindingDescription binding = {};
	binding.binding = 0;
	binding.stride = 16;	// vec4 = 4 floats = 16 bytes
	binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	return binding;
}

static void GetShadowVertexAttributes( VkVertexInputAttributeDescription *attrs, uint32_t &count ) {
	count = 1;
	attrs[0].binding = 0;
	attrs[0].location = 0;
	attrs[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
	attrs[0].offset = 0;
}

// ---------------------------------------------------------------------------

idVulkanPipelineManager::idVulkanPipelineManager()
	: device( VK_NULL_HANDLE )
	, uboSetLayout( VK_NULL_HANDLE )
	, samplerSetLayout( VK_NULL_HANDLE )
	, initialized( false )
{
	memset( pipelines, 0, sizeof( pipelines ) );
}

bool idVulkanPipelineManager::Init( VkDevice device_, VkRenderPass renderPass,
	VkExtent2D extent, const char *shaderDir )
{
	device = device_;
	cachedRenderPass = renderPass;
	cachedExtent = extent;

	if ( !CreateDescriptorSetLayouts() ) {
		Shutdown();
		return false;
	}

	if ( !CreatePipelineLayouts() ) {
		Shutdown();
		return false;
	}

	if ( !CreatePipelines( renderPass, extent, shaderDir ) ) {
		Shutdown();
		return false;
	}

	initialized = true;
	common->Printf( "Vulkan pipeline manager initialized (%d pipelines)\n", (int)VK_PIPELINE_COUNT );
	return true;
}

void idVulkanPipelineManager::Shutdown() {
	if ( device == VK_NULL_HANDLE ) {
		return;
	}

	for ( int i = 0; i < VK_PIPELINE_COUNT; i++ ) {
		if ( pipelines[i].pipeline != VK_NULL_HANDLE ) {
			vkDestroyPipeline( device, pipelines[i].pipeline, NULL );
		}
		if ( pipelines[i].layout != VK_NULL_HANDLE ) {
			vkDestroyPipelineLayout( device, pipelines[i].layout, NULL );
		}
		if ( pipelines[i].vertModule != VK_NULL_HANDLE ) {
			vkDestroyShaderModule( device, pipelines[i].vertModule, NULL );
		}
		if ( pipelines[i].fragModule != VK_NULL_HANDLE ) {
			vkDestroyShaderModule( device, pipelines[i].fragModule, NULL );
		}
	}
	memset( pipelines, 0, sizeof( pipelines ) );

	for ( auto &entry : shaderPassCache ) {
		if ( entry.second != VK_NULL_HANDLE ) {
			vkDestroyPipeline( device, entry.second, NULL );
		}
	}
	shaderPassCache.clear();

	if ( samplerSetLayout != VK_NULL_HANDLE ) {
		vkDestroyDescriptorSetLayout( device, samplerSetLayout, NULL );
		samplerSetLayout = VK_NULL_HANDLE;
	}
	if ( uboSetLayout != VK_NULL_HANDLE ) {
		vkDestroyDescriptorSetLayout( device, uboSetLayout, NULL );
		uboSetLayout = VK_NULL_HANDLE;
	}

	device = VK_NULL_HANDLE;
	initialized = false;
}

VkPipeline idVulkanPipelineManager::GetPipeline( vulkanPipelineType_t type ) const {
	return pipelines[type].pipeline;
}

VkPipelineLayout idVulkanPipelineManager::GetPipelineLayout( vulkanPipelineType_t type ) const {
	return pipelines[type].layout;
}

VkDescriptorPool idVulkanPipelineManager::CreateDescriptorPool( uint32_t maxSets ) const {
	VkDescriptorPoolSize poolSizes[] = {
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxSets * 2 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxSets * 8 }
	};

	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.poolSizeCount = 2;
	poolInfo.pPoolSizes = poolSizes;
	poolInfo.maxSets = maxSets;
	poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

	VkDescriptorPool pool;
	VkResult result = vkCreateDescriptorPool( device, &poolInfo, NULL, &pool );
	if ( result != VK_SUCCESS ) {
		common->Warning( "VulkanPipelineManager: vkCreateDescriptorPool failed: %d", (int)result );
		return VK_NULL_HANDLE;
	}
	return pool;
}

// ---------------------------------------------------------------------------
// Shader loading (from embedded SPIR-V bytecode)
// ---------------------------------------------------------------------------

VkShaderModule idVulkanPipelineManager::LoadShaderModule( const uint32_t *code, uint32_t size, const char *name ) {
	if ( code == NULL || size == 0 ) {
		common->Warning( "VulkanPipelineManager: null shader data for '%s'", name );
		return VK_NULL_HANDLE;
	}

	VkShaderModuleCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.codeSize = size;
	createInfo.pCode = code;

	VkShaderModule module;
	VkResult result = vkCreateShaderModule( device, &createInfo, NULL, &module );

	if ( result != VK_SUCCESS ) {
		common->Warning( "VulkanPipelineManager: vkCreateShaderModule failed for '%s': %d",
			name, (int)result );
		return VK_NULL_HANDLE;
	}

	return module;
}

// ---------------------------------------------------------------------------
// Descriptor set layouts
// ---------------------------------------------------------------------------

bool idVulkanPipelineManager::CreateDescriptorSetLayouts() {
	// Set 0: Uniform buffers (interaction params, fragment params)
	VkDescriptorSetLayoutBinding uboBindings[2] = {};
	uboBindings[0].binding = 0;
	uboBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	uboBindings[0].descriptorCount = 1;
	uboBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	uboBindings[1].binding = 1;
	uboBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	uboBindings[1].descriptorCount = 1;
	uboBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo uboLayoutInfo = {};
	uboLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	uboLayoutInfo.bindingCount = 2;
	uboLayoutInfo.pBindings = uboBindings;

	VkResult result = vkCreateDescriptorSetLayout( device, &uboLayoutInfo, NULL, &uboSetLayout );
	if ( result != VK_SUCCESS ) {
		common->Warning( "VulkanPipelineManager: UBO descriptor set layout failed: %d", (int)result );
		return false;
	}

	// Set 1: Combined image samplers (up to 8 textures)
	VkDescriptorSetLayoutBinding samplerBindings[8] = {};
	for ( int i = 0; i < 8; i++ ) {
		samplerBindings[i].binding = i;
		samplerBindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		samplerBindings[i].descriptorCount = 1;
		samplerBindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	}

	VkDescriptorSetLayoutCreateInfo samplerLayoutInfo = {};
	samplerLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	samplerLayoutInfo.bindingCount = 8;
	samplerLayoutInfo.pBindings = samplerBindings;

	result = vkCreateDescriptorSetLayout( device, &samplerLayoutInfo, NULL, &samplerSetLayout );
	if ( result != VK_SUCCESS ) {
		common->Warning( "VulkanPipelineManager: sampler descriptor set layout failed: %d", (int)result );
		return false;
	}

	return true;
}

// ---------------------------------------------------------------------------
// Pipeline layouts
// ---------------------------------------------------------------------------

bool idVulkanPipelineManager::CreatePipelineLayouts() {
	// Depth fill: push constants only (mat4 mvp = 64 bytes)
	{
		VkPushConstantRange pushRange = {};
		pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		pushRange.offset = 0;
		pushRange.size = 64;	// mat4

		VkPipelineLayoutCreateInfo layoutInfo = {};
		layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layoutInfo.pushConstantRangeCount = 1;
		layoutInfo.pPushConstantRanges = &pushRange;

		VkResult result = vkCreatePipelineLayout( device, &layoutInfo, NULL,
			&pipelines[VK_PIPELINE_DEPTH_FILL].layout );
		if ( result != VK_SUCCESS ) return false;
	}

	// Shadow: push constants (mat4 mvp + vec4 lightOrigin = 80 bytes)
	{
		VkPushConstantRange pushRange = {};
		pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		pushRange.offset = 0;
		pushRange.size = 80;

		VkPipelineLayoutCreateInfo layoutInfo = {};
		layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layoutInfo.pushConstantRangeCount = 1;
		layoutInfo.pPushConstantRanges = &pushRange;

		VkResult result = vkCreatePipelineLayout( device, &layoutInfo, NULL,
			&pipelines[VK_PIPELINE_SHADOW].layout );
		if ( result != VK_SUCCESS ) return false;
	}

	// Interaction: push constants (mat4) + descriptor sets (UBO + samplers)
	{
		VkDescriptorSetLayout sets[] = { uboSetLayout, samplerSetLayout };

		VkPushConstantRange pushRange = {};
		pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		pushRange.offset = 0;
		pushRange.size = 64;

		VkPipelineLayoutCreateInfo layoutInfo = {};
		layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layoutInfo.setLayoutCount = 2;
		layoutInfo.pSetLayouts = sets;
		layoutInfo.pushConstantRangeCount = 1;
		layoutInfo.pPushConstantRanges = &pushRange;

		VkResult result = vkCreatePipelineLayout( device, &layoutInfo, NULL,
			&pipelines[VK_PIPELINE_INTERACTION].layout );
		if ( result != VK_SUCCESS ) return false;
	}

	// GUI: push constants (mat4 mvp + vec4 texMatrixS + vec4 texMatrixT = 96 bytes)
	//       + descriptor sets (UBO + samplers)
	{
		VkDescriptorSetLayout sets[] = { uboSetLayout, samplerSetLayout };

		VkPushConstantRange pushRange = {};
		pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		pushRange.offset = 0;
		pushRange.size = 96;	// mat4(64) + vec4(16) + vec4(16)

		VkPipelineLayoutCreateInfo layoutInfo = {};
		layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layoutInfo.setLayoutCount = 2;
		layoutInfo.pSetLayouts = sets;
		layoutInfo.pushConstantRangeCount = 1;
		layoutInfo.pPushConstantRanges = &pushRange;

		VkResult result = vkCreatePipelineLayout( device, &layoutInfo, NULL,
			&pipelines[VK_PIPELINE_GUI].layout );
		if ( result != VK_SUCCESS ) return false;
	}

	// Environment: push constants (mat4) + descriptor sets (UBO + samplers)
	{
		VkDescriptorSetLayout sets[] = { uboSetLayout, samplerSetLayout };

		VkPushConstantRange pushRange = {};
		pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		pushRange.offset = 0;
		pushRange.size = 64;

		VkPipelineLayoutCreateInfo layoutInfo = {};
		layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layoutInfo.setLayoutCount = 2;
		layoutInfo.pSetLayouts = sets;
		layoutInfo.pushConstantRangeCount = 1;
		layoutInfo.pPushConstantRanges = &pushRange;

		VkResult result = vkCreatePipelineLayout( device, &layoutInfo, NULL,
			&pipelines[VK_PIPELINE_ENVIRONMENT].layout );
		if ( result != VK_SUCCESS ) return false;
	}

	// Fog + Fog caps: push constants (mat4) + descriptor sets (UBO + samplers)
	for ( int fogIdx = VK_PIPELINE_FOG; fogIdx <= VK_PIPELINE_FOG_CAPS; fogIdx++ ) {
		VkDescriptorSetLayout sets[] = { uboSetLayout, samplerSetLayout };

		VkPushConstantRange pushRange = {};
		pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		pushRange.offset = 0;
		pushRange.size = 64;

		VkPipelineLayoutCreateInfo layoutInfo = {};
		layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layoutInfo.setLayoutCount = 2;
		layoutInfo.pSetLayouts = sets;
		layoutInfo.pushConstantRangeCount = 1;
		layoutInfo.pPushConstantRanges = &pushRange;

		VkResult result = vkCreatePipelineLayout( device, &layoutInfo, NULL,
			&pipelines[fogIdx].layout );
		if ( result != VK_SUCCESS ) return false;
	}

	return true;
}

// ---------------------------------------------------------------------------
// Pipeline creation helper
// ---------------------------------------------------------------------------

static void FillDefaultPipelineState(
	VkPipelineInputAssemblyStateCreateInfo &ia,
	VkPipelineRasterizationStateCreateInfo &rast,
	VkPipelineMultisampleStateCreateInfo &ms,
	VkPipelineColorBlendAttachmentState &blendAtt,
	VkPipelineColorBlendStateCreateInfo &blend,
	VkPipelineDepthStencilStateCreateInfo &depthStencil,
	VkPipelineViewportStateCreateInfo &vp,
	VkPipelineDynamicStateCreateInfo &dyn,
	VkDynamicState *dynStates,
	VkViewport &viewport,
	VkRect2D &scissor,
	VkExtent2D extent )
{
	memset( &ia, 0, sizeof( ia ) );
	ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	memset( &rast, 0, sizeof( rast ) );
	rast.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rast.polygonMode = VK_POLYGON_MODE_FILL;
	rast.cullMode = VK_CULL_MODE_BACK_BIT;
	// Clockwise front face because the negative viewport height (Y flip)
	// reverses the triangle winding in framebuffer space.
	rast.frontFace = VK_FRONT_FACE_CLOCKWISE;
	rast.lineWidth = 1.0f;

	memset( &ms, 0, sizeof( ms ) );
	ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	memset( &blendAtt, 0, sizeof( blendAtt ) );
	blendAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
		| VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	blendAtt.blendEnable = VK_FALSE;

	memset( &blend, 0, sizeof( blend ) );
	blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blend.attachmentCount = 1;
	blend.pAttachments = &blendAtt;

	memset( &depthStencil, 0, sizeof( depthStencil ) );
	depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = VK_TRUE;
	depthStencil.depthWriteEnable = VK_TRUE;
	depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	depthStencil.stencilTestEnable = VK_FALSE;

	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = (float)extent.width;
	viewport.height = (float)extent.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	scissor.offset = { 0, 0 };
	scissor.extent = extent;

	memset( &vp, 0, sizeof( vp ) );
	vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	vp.viewportCount = 1;
	vp.pViewports = &viewport;
	vp.scissorCount = 1;
	vp.pScissors = &scissor;

	dynStates[0] = VK_DYNAMIC_STATE_VIEWPORT;
	dynStates[1] = VK_DYNAMIC_STATE_SCISSOR;

	memset( &dyn, 0, sizeof( dyn ) );
	dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dyn.dynamicStateCount = 2;
	dyn.pDynamicStates = dynStates;
}

// ---------------------------------------------------------------------------
// Pipeline creation
// ---------------------------------------------------------------------------

bool idVulkanPipelineManager::CreatePipelines( VkRenderPass renderPass,
	VkExtent2D extent, const char *shaderDir )
{
	struct shaderPair_t {
		const uint32_t	*vertData;
		uint32_t		vertSize;
		const uint32_t	*fragData;
		uint32_t		fragSize;
		const char		*name;
	};

	shaderPair_t shaderData[VK_PIPELINE_COUNT] = {
		{ depth_fill_vert_spv, depth_fill_vert_spv_size, depth_fill_frag_spv, depth_fill_frag_spv_size, "depth_fill" },
		{ shadow_vert_spv, shadow_vert_spv_size, shadow_frag_spv, shadow_frag_spv_size, "shadow" },
		{ interaction_vert_spv, interaction_vert_spv_size, interaction_frag_spv, interaction_frag_spv_size, "interaction" },
		{ gui_vert_spv, gui_vert_spv_size, gui_frag_spv, gui_frag_spv_size, "gui" },
		{ environment_vert_spv, environment_vert_spv_size, environment_frag_spv, environment_frag_spv_size, "environment" },
		{ fog_vert_spv, fog_vert_spv_size, fog_frag_spv, fog_frag_spv_size, "fog" },
		{ fog_vert_spv, fog_vert_spv_size, fog_frag_spv, fog_frag_spv_size, "fog_caps" },
	};

	// Load all shader modules from embedded SPIR-V
	for ( int i = 0; i < VK_PIPELINE_COUNT; i++ ) {
		pipelines[i].vertModule = LoadShaderModule( shaderData[i].vertData, shaderData[i].vertSize, shaderData[i].name );
		pipelines[i].fragModule = LoadShaderModule( shaderData[i].fragData, shaderData[i].fragSize, shaderData[i].name );

		if ( pipelines[i].vertModule == VK_NULL_HANDLE || pipelines[i].fragModule == VK_NULL_HANDLE ) {
			common->Warning( "VulkanPipelineManager: failed to load shaders for '%s'", shaderData[i].name );
			// Don't fail init — just skip this pipeline
			continue;
		}
	}

	// Common state objects
	VkPipelineInputAssemblyStateCreateInfo ia;
	VkPipelineRasterizationStateCreateInfo rast;
	VkPipelineMultisampleStateCreateInfo ms;
	VkPipelineColorBlendAttachmentState blendAtt;
	VkPipelineColorBlendStateCreateInfo blend;
	VkPipelineDepthStencilStateCreateInfo depthStencil;
	VkPipelineViewportStateCreateInfo vp;
	VkPipelineDynamicStateCreateInfo dyn;
	VkDynamicState dynStates[2];
	VkViewport viewport;
	VkRect2D scissor;

	FillDefaultPipelineState( ia, rast, ms, blendAtt, blend, depthStencil,
		vp, dyn, dynStates, viewport, scissor, extent );

	// Create each pipeline
	for ( int i = 0; i < VK_PIPELINE_COUNT; i++ ) {
		if ( pipelines[i].vertModule == VK_NULL_HANDLE || pipelines[i].layout == VK_NULL_HANDLE ) {
			continue;
		}

		VkPipelineShaderStageCreateInfo stages[2] = {};
		stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		stages[0].module = pipelines[i].vertModule;
		stages[0].pName = "main";
		stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		stages[1].module = pipelines[i].fragModule;
		stages[1].pName = "main";

		// Vertex input
		VkVertexInputBindingDescription binding;
		VkVertexInputAttributeDescription attrs[6];
		uint32_t attrCount;

		if ( i == VK_PIPELINE_DEPTH_FILL || i == VK_PIPELINE_FOG || i == VK_PIPELINE_FOG_CAPS ) {
			binding = GetFullVertexBinding();	// still reads from full vertex buffer
			GetPositionOnlyAttributes( attrs, attrCount );
		} else if ( i == VK_PIPELINE_SHADOW ) {
			binding = GetShadowVertexBinding();
			GetShadowVertexAttributes( attrs, attrCount );
		} else if ( i == VK_PIPELINE_GUI ) {
			// gui.vert only reads position, texcoord, color (not tangent/bitangent/normal)
			binding = GetFullVertexBinding();
			GetGuiVertexAttributes( attrs, attrCount );
		} else if ( i == VK_PIPELINE_ENVIRONMENT ) {
			// environment.vert only reads position, texcoord, normal (not tangent/bitangent/color)
			binding = GetFullVertexBinding();
			GetEnvironmentVertexAttributes( attrs, attrCount );
		} else {
			binding = GetFullVertexBinding();
			GetFullVertexAttributes( attrs, attrCount );
		}

		VkPipelineVertexInputStateCreateInfo vertInput = {};
		vertInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertInput.vertexBindingDescriptionCount = 1;
		vertInput.pVertexBindingDescriptions = &binding;
		vertInput.vertexAttributeDescriptionCount = attrCount;
		vertInput.pVertexAttributeDescriptions = attrs;

		// Per-pipeline overrides
		VkPipelineDepthStencilStateCreateInfo ds = depthStencil;
		VkPipelineRasterizationStateCreateInfo rs = rast;
		VkPipelineColorBlendAttachmentState ba = blendAtt;
		VkPipelineColorBlendStateCreateInfo bl = blend;
		bl.pAttachments = &ba;

		if ( i == VK_PIPELINE_DEPTH_FILL ) {
			// Depth write, no color
			ba.colorWriteMask = 0;
		} else if ( i == VK_PIPELINE_SHADOW ) {
			// No color write, stencil test enabled
			ba.colorWriteMask = 0;
			ds.depthWriteEnable = VK_FALSE;
			ds.stencilTestEnable = VK_TRUE;
			ds.front.failOp = VK_STENCIL_OP_KEEP;
			ds.front.depthFailOp = VK_STENCIL_OP_INCREMENT_AND_WRAP;
			ds.front.passOp = VK_STENCIL_OP_KEEP;
			ds.front.compareOp = VK_COMPARE_OP_ALWAYS;
			ds.front.compareMask = 0xFF;
			ds.front.writeMask = 0xFF;
			ds.back = ds.front;
			ds.back.depthFailOp = VK_STENCIL_OP_DECREMENT_AND_WRAP;
			rs.cullMode = VK_CULL_MODE_NONE;	// both faces needed
		} else if ( i == VK_PIPELINE_INTERACTION ) {
			// Additive blending for light accumulation
			ba.blendEnable = VK_TRUE;
			ba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
			ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
			ba.colorBlendOp = VK_BLEND_OP_ADD;
			ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			ba.alphaBlendOp = VK_BLEND_OP_ADD;
			ds.depthWriteEnable = VK_FALSE;
			// Stencil test: only draw where stencil == 0 (lit areas).
			// Shadow volumes increment/decrement around 0; shadowed pixels != 0.
			ds.stencilTestEnable = VK_TRUE;
			ds.front.failOp = VK_STENCIL_OP_KEEP;
			ds.front.depthFailOp = VK_STENCIL_OP_KEEP;
			ds.front.passOp = VK_STENCIL_OP_KEEP;
			ds.front.compareOp = VK_COMPARE_OP_EQUAL;
			ds.front.compareMask = 0xFF;
			ds.front.writeMask = 0x00;
			ds.front.reference = 0;
			ds.back = ds.front;
		} else if ( i == VK_PIPELINE_GUI ) {
			// Alpha blending for GUI
			ba.blendEnable = VK_TRUE;
			ba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			ba.colorBlendOp = VK_BLEND_OP_ADD;
			ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
			ba.alphaBlendOp = VK_BLEND_OP_ADD;
			ds.depthTestEnable = VK_FALSE;
			ds.depthWriteEnable = VK_FALSE;
			rs.cullMode = VK_CULL_MODE_NONE;
		} else if ( i == VK_PIPELINE_FOG ) {
			// Fog surfaces: alpha blend, depth equal, no depth write
			ba.blendEnable = VK_TRUE;
			ba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			ba.colorBlendOp = VK_BLEND_OP_ADD;
			ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			ba.alphaBlendOp = VK_BLEND_OP_ADD;
			ds.depthWriteEnable = VK_FALSE;
			ds.depthCompareOp = VK_COMPARE_OP_EQUAL;
			ds.stencilTestEnable = VK_FALSE;
			rs.cullMode = VK_CULL_MODE_BACK_BIT;	// CT_FRONT_SIDED
		} else if ( i == VK_PIPELINE_FOG_CAPS ) {
			// Fog frustum caps: alpha blend, depth lequal, no depth write, back-sided
			ba.blendEnable = VK_TRUE;
			ba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			ba.colorBlendOp = VK_BLEND_OP_ADD;
			ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			ba.alphaBlendOp = VK_BLEND_OP_ADD;
			ds.depthWriteEnable = VK_FALSE;
			ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
			ds.stencilTestEnable = VK_FALSE;
			rs.cullMode = VK_CULL_MODE_FRONT_BIT;	// CT_BACK_SIDED
		}

		VkGraphicsPipelineCreateInfo pipeInfo = {};
		pipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipeInfo.stageCount = 2;
		pipeInfo.pStages = stages;
		pipeInfo.pVertexInputState = &vertInput;
		pipeInfo.pInputAssemblyState = &ia;
		pipeInfo.pViewportState = &vp;
		pipeInfo.pRasterizationState = &rs;
		pipeInfo.pMultisampleState = &ms;
		pipeInfo.pDepthStencilState = &ds;
		pipeInfo.pColorBlendState = &bl;
		pipeInfo.pDynamicState = &dyn;
		pipeInfo.layout = pipelines[i].layout;
		pipeInfo.renderPass = renderPass;
		pipeInfo.subpass = 0;

		VkResult result = vkCreateGraphicsPipelines( device, VK_NULL_HANDLE, 1, &pipeInfo, NULL,
			&pipelines[i].pipeline );
		if ( result != VK_SUCCESS ) {
			common->Warning( "VulkanPipelineManager: pipeline '%s' creation failed: %d",
				shaderData[i].name, (int)result );
		} else {
			common->Printf( "  Vulkan pipeline '%s' created\n", shaderData[i].name );
		}
	}

	return true;
}

// ---------------------------------------------------------------------------
// Shader pass pipeline cache
// ---------------------------------------------------------------------------

#include "renderer/tr_local.h"	// GLS_* constants, CT_* cull types

static VkBlendFactor VK_MapSrcBlend( int glsBits ) {
	switch ( glsBits & GLS_SRCBLEND_BITS ) {
	case GLS_SRCBLEND_ZERO:					return VK_BLEND_FACTOR_ZERO;
	case GLS_SRCBLEND_ONE:					return VK_BLEND_FACTOR_ONE;
	case GLS_SRCBLEND_DST_COLOR:			return VK_BLEND_FACTOR_DST_COLOR;
	case GLS_SRCBLEND_ONE_MINUS_DST_COLOR:	return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
	case GLS_SRCBLEND_SRC_ALPHA:			return VK_BLEND_FACTOR_SRC_ALPHA;
	case GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA:	return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	case GLS_SRCBLEND_DST_ALPHA:			return VK_BLEND_FACTOR_DST_ALPHA;
	case GLS_SRCBLEND_ONE_MINUS_DST_ALPHA:	return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
	case GLS_SRCBLEND_ALPHA_SATURATE:		return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
	default:								return VK_BLEND_FACTOR_ONE;
	}
}

static VkBlendFactor VK_MapDstBlend( int glsBits ) {
	switch ( glsBits & GLS_DSTBLEND_BITS ) {
	case GLS_DSTBLEND_ZERO:					return VK_BLEND_FACTOR_ZERO;
	case GLS_DSTBLEND_ONE:					return VK_BLEND_FACTOR_ONE;
	case GLS_DSTBLEND_SRC_COLOR:			return VK_BLEND_FACTOR_SRC_COLOR;
	case GLS_DSTBLEND_ONE_MINUS_SRC_COLOR:	return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
	case GLS_DSTBLEND_SRC_ALPHA:			return VK_BLEND_FACTOR_SRC_ALPHA;
	case GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA:	return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	case GLS_DSTBLEND_DST_ALPHA:			return VK_BLEND_FACTOR_DST_ALPHA;
	case GLS_DSTBLEND_ONE_MINUS_DST_ALPHA:	return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
	default:								return VK_BLEND_FACTOR_ZERO;
	}
}

VkPipelineLayout idVulkanPipelineManager::GetShaderPassLayout() const {
	// Shader pass pipelines reuse the gui pipeline layout
	return pipelines[VK_PIPELINE_GUI].layout;
}

VkPipeline idVulkanPipelineManager::GetShaderPassPipeline( uint64_t glState, int cullMode ) {
	// Build cache key: GLS_* bits in lower 32, cull in upper bits
	uint64_t key = ( glState & 0xFFFFFFFF ) | ( (uint64_t)cullMode << 32 );

	auto it = shaderPassCache.find( key );
	if ( it != shaderPassCache.end() ) {
		return it->second;
	}

	// Create a new pipeline for this state combination
	VkPipelineShaderStageCreateInfo stages[2] = {};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = pipelines[VK_PIPELINE_GUI].vertModule;
	stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = pipelines[VK_PIPELINE_GUI].fragModule;
	stages[1].pName = "main";

	VkVertexInputBindingDescription binding = GetFullVertexBinding();
	VkVertexInputAttributeDescription attrs[3];
	uint32_t attrCount;
	// gui.vert only reads position (0), texcoord (1), color (5)
	GetGuiVertexAttributes( attrs, attrCount );

	VkPipelineVertexInputStateCreateInfo vertInput = {};
	vertInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertInput.vertexBindingDescriptionCount = 1;
	vertInput.pVertexBindingDescriptions = &binding;
	vertInput.vertexAttributeDescriptionCount = attrCount;
	vertInput.pVertexAttributeDescriptions = attrs;

	VkPipelineInputAssemblyStateCreateInfo ia = {};
	ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkViewport viewport = {};
	viewport.width = (float)cachedExtent.width;
	viewport.height = (float)cachedExtent.height;
	viewport.maxDepth = 1.0f;
	VkRect2D scissor = {};
	scissor.extent = cachedExtent;

	VkPipelineViewportStateCreateInfo vp = {};
	vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	vp.viewportCount = 1;
	vp.pViewports = &viewport;
	vp.scissorCount = 1;
	vp.pScissors = &scissor;

	VkPipelineRasterizationStateCreateInfo rast = {};
	rast.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rast.lineWidth = 1.0f;
	rast.polygonMode = ( glState & GLS_POLYMODE_LINE ) ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;

	switch ( cullMode ) {
	case CT_FRONT_SIDED:	rast.cullMode = VK_CULL_MODE_BACK_BIT; break;
	case CT_BACK_SIDED:		rast.cullMode = VK_CULL_MODE_FRONT_BIT; break;
	case CT_TWO_SIDED:
	default:				rast.cullMode = VK_CULL_MODE_NONE; break;
	}
	// Clockwise front face because the negative viewport height (Y flip)
	// reverses the triangle winding in framebuffer space.
	rast.frontFace = VK_FRONT_FACE_CLOCKWISE;

	VkPipelineMultisampleStateCreateInfo ms = {};
	ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	// Depth/stencil
	VkPipelineDepthStencilStateCreateInfo ds = {};
	ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	ds.depthTestEnable = VK_TRUE;
	ds.depthWriteEnable = ( glState & GLS_DEPTHMASK ) ? VK_FALSE : VK_TRUE;

	if ( glState & GLS_DEPTHFUNC_ALWAYS ) {
		ds.depthCompareOp = VK_COMPARE_OP_ALWAYS;
		ds.depthTestEnable = VK_FALSE;
	} else if ( glState & GLS_DEPTHFUNC_EQUAL ) {
		ds.depthCompareOp = VK_COMPARE_OP_EQUAL;
	} else {
		ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	}

	// Blend state
	VkPipelineColorBlendAttachmentState ba = {};
	ba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
		| VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	if ( glState & GLS_REDMASK )	ba.colorWriteMask &= ~VK_COLOR_COMPONENT_R_BIT;
	if ( glState & GLS_GREENMASK )	ba.colorWriteMask &= ~VK_COLOR_COMPONENT_G_BIT;
	if ( glState & GLS_BLUEMASK )	ba.colorWriteMask &= ~VK_COLOR_COMPONENT_B_BIT;
	if ( glState & GLS_ALPHAMASK )	ba.colorWriteMask &= ~VK_COLOR_COMPONENT_A_BIT;

	int srcBits = glState & GLS_SRCBLEND_BITS;
	int dstBits = glState & GLS_DSTBLEND_BITS;
	if ( srcBits != GLS_SRCBLEND_ONE || dstBits != GLS_DSTBLEND_ZERO ) {
		ba.blendEnable = VK_TRUE;
		ba.srcColorBlendFactor = VK_MapSrcBlend( (int)glState );
		ba.dstColorBlendFactor = VK_MapDstBlend( (int)glState );
		ba.colorBlendOp = VK_BLEND_OP_ADD;
		ba.srcAlphaBlendFactor = ba.srcColorBlendFactor;
		ba.dstAlphaBlendFactor = ba.dstColorBlendFactor;
		ba.alphaBlendOp = VK_BLEND_OP_ADD;
	}

	VkPipelineColorBlendStateCreateInfo blend = {};
	blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blend.attachmentCount = 1;
	blend.pAttachments = &ba;

	VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dyn = {};
	dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dyn.dynamicStateCount = 2;
	dyn.pDynamicStates = dynStates;

	VkGraphicsPipelineCreateInfo pipeInfo = {};
	pipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipeInfo.stageCount = 2;
	pipeInfo.pStages = stages;
	pipeInfo.pVertexInputState = &vertInput;
	pipeInfo.pInputAssemblyState = &ia;
	pipeInfo.pViewportState = &vp;
	pipeInfo.pRasterizationState = &rast;
	pipeInfo.pMultisampleState = &ms;
	pipeInfo.pDepthStencilState = &ds;
	pipeInfo.pColorBlendState = &blend;
	pipeInfo.pDynamicState = &dyn;
	pipeInfo.layout = GetShaderPassLayout();
	pipeInfo.renderPass = cachedRenderPass;
	pipeInfo.subpass = 0;

	VkPipeline pipeline = VK_NULL_HANDLE;
	VkResult result = vkCreateGraphicsPipelines( device, VK_NULL_HANDLE, 1,
		&pipeInfo, NULL, &pipeline );
	if ( result != VK_SUCCESS ) {
		common->Warning( "Shader pass pipeline creation failed for state 0x%llx: %d",
			(unsigned long long)key, (int)result );
		pipeline = VK_NULL_HANDLE;
	}

	shaderPassCache[key] = pipeline;
	return pipeline;
}

#endif // ID_VULKAN
