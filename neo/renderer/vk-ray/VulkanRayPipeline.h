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

#ifndef __VULKAN_RAY_PIPELINE_H__
#define __VULKAN_RAY_PIPELINE_H__

#ifdef ID_VULKAN

#include <vulkan/vulkan.h>
#include <functional>

// UBO structures passed into the raygen shader.
#define RT_MAX_LIGHTS 32

struct CameraUBO {
	float    viewInverse[16];   // column-major inverse-view matrix
	float    projInverse[16];   // column-major inverse-projection matrix
	uint32_t benchmarkPixels;   // 0=disabled, N=sample approximately N pixels/frame
	uint32_t pad[3];
};

// Benchmark training record written per sampled light-ray decision.
// Layout matches GLSL std430; size = 64 bytes.
struct TrainingRecord {
	float    camPos[3];        // camera world position          (12)
	float    bounce;           // bounce depth: 0=primary        (4)
	float    hitPos[3];        // surface point being shaded     (12)
	float    pad0;             //                                (4)
	float    hitNormal[3];     // surface normal (approx)        (12)
	float    pad1;             //                                (4)
	float    lightCenter[3];   // light sphere centre            (12)
	float    lightRadius;      //                                (4)
	float    sampleOrigin[3];  // point on light sphere fired from (12)
	uint32_t contributed;      // 1=unoccluded→contributed, 0=blocked (4)
};  // 64 bytes total

struct LightsUBO {
	float positions[RT_MAX_LIGHTS][4];  // xyz=world position, w=sphere radius (world units)
	float colors[RT_MAX_LIGHTS][4];     // rgb + intensity scale in w
	int   numLights;
	float lightDensity;                 // light rays per unit of sphere radius
	float pad[2];
};

// Maximum TrainingRecords per frame (≈12.8 MB SSBO).
static constexpr uint32_t MAX_BENCH_RECORDS = 200000;

// Manages the Vulkan ray tracing pipeline, descriptor layout, shader binding
// table (SBT), and output storage image for the vulkan-ray backend.
class idVulkanRayPipelineManager {
public:
	idVulkanRayPipelineManager();

	bool Init( VkDevice device, VkPhysicalDevice physicalDevice,
	           VkExtent2D extent, uint32_t graphicsQueueFamily, VkQueue graphicsQueue );
	void Shutdown();

	// Call each frame before TraceRays: updates the dynamic descriptor bindings
	// (TLAS and UBO offsets). Set binding 1 (output image) stays constant.
	void UpdateDescriptors( VkAccelerationStructureKHR tlas,
	                        VkBuffer cameraUBO, VkDeviceSize cameraOffset,
	                        VkBuffer lightsUBO, VkDeviceSize lightsOffset );

	void BindAndTrace( VkCommandBuffer cmd, uint32_t width, uint32_t height );

	// Benchmark SSBO helpers (no-ops when buffer is not allocated).
	void ResetBenchmarkCounter();
	void ReadBenchmarkRecords( uint32_t &outCount, const TrainingRecord *&outRecords );

	// After TraceRays: transition output image → TRANSFER_SRC, copy to swapchain,
	// transition back to GENERAL, transition swapchain to PRESENT_SRC.
	void BlitToSwapchain( VkCommandBuffer cmd, VkImage swapchainImage,
	                      VkImageLayout swapchainInitialLayout,
	                      uint32_t width, uint32_t height );

	VkDescriptorSetLayout GetDescriptorSetLayout() const { return descSetLayout; }

private:
	bool CreateOutputImage( uint32_t width, uint32_t height );
	bool CreateDescriptorPool();
	bool CreateDescriptorSetLayout();
	bool CreateDescriptorSet();
	bool CreatePipeline();
	bool CreateSBT();
	bool CreateBenchmarkBuffer();

	VkShaderModule LoadShaderModule( const uint32_t *spv, uint32_t bytes );

	VkDevice           device;
	VkPhysicalDevice   physicalDevice;
	VkExtent2D         extent;
	VkCommandPool      oneTimePool;
	VkQueue            graphicsQueue;

	// Output storage image
	VkImage            outputImage;
	VkDeviceMemory     outputMemory;
	VkImageView        outputView;

	// Descriptor resources
	VkDescriptorSetLayout descSetLayout;
	VkDescriptorPool      descPool;
	VkDescriptorSet       descSet;

	// Pipeline
	VkPipelineLayout   pipelineLayout;
	VkPipeline         rtPipeline;

	// Shader binding table
	VkBuffer           sbtBuffer;
	VkDeviceMemory     sbtMemory;
	VkStridedDeviceAddressRegionKHR raygenRegion;
	VkStridedDeviceAddressRegionKHR missRegion;
	VkStridedDeviceAddressRegionKHR hitRegion;
	VkStridedDeviceAddressRegionKHR callableRegion;

	// Benchmark recording SSBO (only allocated when com_benchmark is set)
	VkBuffer       benchmarkBuffer  = VK_NULL_HANDLE;
	VkDeviceMemory benchmarkMemory  = VK_NULL_HANDLE;
	void *         benchmarkMapped  = nullptr;

	uint32_t FindMemoryType( uint32_t filter, VkMemoryPropertyFlags props ) const;
	void SubmitOneTime( std::function<void(VkCommandBuffer)> fn );

	bool initialized;
};

extern idVulkanRayPipelineManager vkRayPipelineMgr;

#endif // ID_VULKAN
#endif // __VULKAN_RAY_PIPELINE_H__
