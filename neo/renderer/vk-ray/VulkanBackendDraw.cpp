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

#include "renderer/tr_local.h"
#include "renderer/VertexCache.h"
#include "renderer/RenderBackendDraw.h"
#include "renderer/vk/VulkanState.h"
#include "renderer/vk/VulkanFrameSync.h"
#include "renderer/vk/VulkanAllocator.h"
#include "renderer/vk/VulkanImage.h"
#include "renderer/vk-ray/VulkanRayPipeline.h"
#include "renderer/vk-ray/VulkanAccelStructure.h"
#include "renderer/vk-ray/VulkanRTFuncs.h"
#include "renderer/Image.h"
#include "framework/Common.h"
#include "sys/sys_public.h"
#include <cmath>
#include <cstring>
#include <cstdio>
#include <vector>

// ---------------------------------------------------------------------------
// 4x4 matrix utilities (column-major: M[col*4 + row])
// ---------------------------------------------------------------------------

static float Det3( float a, float b, float c,
                   float d, float e, float f,
                   float g, float h, float i ) {
	return a*(e*i - f*h) - b*(d*i - f*g) + c*(d*h - e*g);
}

static void Mat4Inverse( const float *m, float *inv ) {
	float c00 =  Det3(m[5],m[9],m[13], m[6],m[10],m[14], m[7],m[11],m[15]);
	float c10 = -Det3(m[1],m[9],m[13], m[2],m[10],m[14], m[3],m[11],m[15]);
	float c20 =  Det3(m[1],m[5],m[13], m[2],m[ 6],m[14], m[3],m[ 7],m[15]);
	float c30 = -Det3(m[1],m[5],m[ 9], m[2],m[ 6],m[10], m[3],m[ 7],m[11]);

	float det = m[0]*c00 + m[4]*c10 + m[8]*c20 + m[12]*c30;
	if ( fabsf(det) < 1e-10f ) {
		memset( inv, 0, 16*sizeof(float) );
		inv[0] = inv[5] = inv[10] = inv[15] = 1.0f;
		return;
	}
	float d = 1.0f / det;

	inv[ 0] = d * c00;
	inv[ 4] = d * (-Det3(m[4],m[8],m[12], m[6],m[10],m[14], m[7],m[11],m[15]));
	inv[ 8] = d * ( Det3(m[4],m[8],m[12], m[5],m[ 9],m[13], m[7],m[11],m[15]));
	inv[12] = d * (-Det3(m[4],m[8],m[12], m[5],m[ 9],m[13], m[6],m[10],m[14]));
	inv[ 1] = d * c10;
	inv[ 5] = d * ( Det3(m[0],m[8],m[12], m[2],m[10],m[14], m[3],m[11],m[15]));
	inv[ 9] = d * (-Det3(m[0],m[8],m[12], m[1],m[ 9],m[13], m[3],m[11],m[15]));
	inv[13] = d * ( Det3(m[0],m[8],m[12], m[1],m[ 9],m[13], m[2],m[10],m[14]));
	inv[ 2] = d * c20;
	inv[ 6] = d * (-Det3(m[0],m[4],m[12], m[2],m[ 6],m[14], m[3],m[ 7],m[15]));
	inv[10] = d * ( Det3(m[0],m[4],m[12], m[1],m[ 5],m[13], m[3],m[ 7],m[15]));
	inv[14] = d * (-Det3(m[0],m[4],m[12], m[1],m[ 5],m[13], m[2],m[ 6],m[14]));
	inv[ 3] = d * c30;
	inv[ 7] = d * ( Det3(m[0],m[4],m[ 8], m[2],m[ 6],m[10], m[3],m[ 7],m[11]));
	inv[11] = d * (-Det3(m[0],m[4],m[ 8], m[1],m[ 5],m[ 9], m[3],m[ 7],m[11]));
	inv[15] = d * ( Det3(m[0],m[4],m[ 8], m[1],m[ 5],m[ 9], m[2],m[ 6],m[10]));
}

// ---------------------------------------------------------------------------
// Ray tracing draw backend class
// ---------------------------------------------------------------------------

class idRenderBackendDrawVulkanRay : public idRenderBackendDraw {
public:
	idRenderBackendDrawVulkanRay();

	virtual void	Init();
	virtual void	Shutdown();
	virtual void	ExecuteBackEndCommands( const emptyCommand_t *cmds );

private:
	void	HandleSetBuffer( const emptyCommand_t *cmd );
	void	HandleDrawView( const emptyCommand_t *cmd );
	void	HandleSwapBuffers( const emptyCommand_t *cmd );
	void	HandleCopyRender( const emptyCommand_t *cmd );

	void	EnsureScreenshotBuffer( int width, int height );
	void	ReadPixels( int x, int y, int width, int height, byte *buffer );

	idVulkanFrameSync	frameSync;
	idVulkanAllocator	allocator;

	bool			initialized;
	bool			frameActive;

	VkBuffer		screenshotBuffer;
	VkDeviceMemory	screenshotMemory;
	int				screenshotBufferWidth;
	int				screenshotBufferHeight;
	bool			screenshotReady;

	VkCommandBuffer		currentCmdBuf;
	const viewDef_t *	currentViewDef;

	// Benchmark recording state
	FILE *   benchmarkFile       = nullptr;
	int      benchmarkFrameCount = 0;
	float    lastCamPos[3]       = {};
	float    lastCamAngles[3]    = {};
};

// ---------------------------------------------------------------------------

idRenderBackendDrawVulkanRay::idRenderBackendDrawVulkanRay()
	: initialized( false )
	, frameActive( false )
	, screenshotBuffer( VK_NULL_HANDLE )
	, screenshotMemory( VK_NULL_HANDLE )
	, screenshotBufferWidth( 0 )
	, screenshotBufferHeight( 0 )
	, screenshotReady( false )
	, currentCmdBuf( VK_NULL_HANDLE )
	, currentViewDef( NULL )
{
}

void idRenderBackendDrawVulkanRay::Init() {
	if ( vkState.device == VK_NULL_HANDLE ) {
		common->Warning( "VulkanRayBackend::Init: no Vulkan device" );
		return;
	}

	if ( !frameSync.Init( vkState.device, vkState.graphicsQueueFamily ) ) {
		common->Warning( "VulkanRayBackend::Init: frameSync failed" );
		return;
	}

	if ( !allocator.Init( vkState.device, vkState.physicalDevice ) ) {
		common->Warning( "VulkanRayBackend::Init: allocator failed" );
		frameSync.Shutdown();
		return;
	}

	if ( !vkTextureMgr.Init( vkState.device, vkState.physicalDevice,
		vkState.graphicsQueueFamily ) )
	{
		common->Warning( "VulkanRayBackend::Init: textureMgr failed" );
		allocator.Shutdown();
		frameSync.Shutdown();
		return;
	}

	if ( !vkRayAccelMgr.Init( vkState.device, vkState.physicalDevice,
		vkState.graphicsQueueFamily, vkState.graphicsQueue ) )
	{
		common->Warning( "VulkanRayBackend::Init: accelMgr failed" );
		vkTextureMgr.Shutdown();
		allocator.Shutdown();
		frameSync.Shutdown();
		return;
	}

	if ( !vkRayPipelineMgr.Init( vkState.device, vkState.physicalDevice,
		vkState.swapchainExtent, vkState.graphicsQueueFamily,
		vkState.graphicsQueue ) )
	{
		common->Warning( "VulkanRayBackend::Init: rtPipeline failed" );
		vkRayAccelMgr.Shutdown();
		vkTextureMgr.Shutdown();
		allocator.Shutdown();
		frameSync.Shutdown();
		return;
	}

	// No render pass in RT mode
	vkState.mainRenderPass      = VK_NULL_HANDLE;
	vkState.activeCommandBuffer = VK_NULL_HANDLE;

	VK_LoadAllImages();

	// Open benchmark output file
	if ( com_benchmark.GetBool() ) {
		char fname[256];
		snprintf( fname, sizeof(fname), "benchmark_%lld.bin",
		          (long long)Sys_Milliseconds() );
		benchmarkFile = fopen( fname, "wb" );
		if ( benchmarkFile ) {
			// Write 24-byte file header
			const char magic[8] = { 'R','T','B','E','N','C','H','1' };
			uint32_t hdr[4] = {
				1,                                               // version
				0,                                               // flags (reserved)
				(uint32_t)com_benchmarkPixels.GetInteger(),      // pixelsPerFrame
				0                                                // reserved
			};
			fwrite( magic, 1, 8, benchmarkFile );
			fwrite( hdr, 4, 4, benchmarkFile );

			vkRayAccelMgr.SetBenchmarkFile( benchmarkFile );
			vkRayPipelineMgr.ResetBenchmarkCounter();
			common->Printf( "Benchmark file opened: %s\n", fname );
		} else {
			common->Warning( "Failed to open benchmark file: %s", fname );
		}
	}

	initialized = true;
	common->Printf( "Vulkan ray tracing backend initialized\n" );
}

void idRenderBackendDrawVulkanRay::Shutdown() {
	if ( !initialized ) return;

	if ( vkState.device != VK_NULL_HANDLE ) {
		frameSync.WaitIdle();
	}

	if ( benchmarkFile ) {
		fflush( benchmarkFile );
		fclose( benchmarkFile );
		benchmarkFile = nullptr;
		vkRayAccelMgr.SetBenchmarkFile( nullptr );
		common->Printf( "Benchmark file closed (%d frames recorded)\n",
		                benchmarkFrameCount );
	}

	if ( screenshotBuffer != VK_NULL_HANDLE ) {
		vkDestroyBuffer( vkState.device, screenshotBuffer, NULL );
		screenshotBuffer = VK_NULL_HANDLE;
	}
	if ( screenshotMemory != VK_NULL_HANDLE ) {
		vkFreeMemory( vkState.device, screenshotMemory, NULL );
		screenshotMemory = VK_NULL_HANDLE;
	}

	vkRayPipelineMgr.Shutdown();
	vkRayAccelMgr.Shutdown();
	vkTextureMgr.Shutdown();
	allocator.Shutdown();
	frameSync.Shutdown();

	initialized = false;
	frameActive = false;
	common->Printf( "Vulkan ray tracing backend shut down\n" );
}

// ---------------------------------------------------------------------------

void idRenderBackendDrawVulkanRay::ExecuteBackEndCommands( const emptyCommand_t *cmds ) {
	if ( !initialized ) return;
	if ( cmds->commandId == RC_NOP && !cmds->next ) return;

	extern int backEndStartTime, backEndFinishTime;
	backEndStartTime = Sys_Milliseconds();

	for ( ; cmds; cmds = (const emptyCommand_t *)cmds->next ) {
		switch ( cmds->commandId ) {
		case RC_NOP:           break;
		case RC_SET_BUFFER:    HandleSetBuffer( cmds );    break;
		case RC_DRAW_VIEW:     HandleDrawView( cmds );     break;
		case RC_SWAP_BUFFERS:  HandleSwapBuffers( cmds );  break;
		case RC_COPY_RENDER:   HandleCopyRender( cmds );   break;
		default:
			common->Error( "VulkanRayBackend: bad commandId" );
			break;
		}
	}

	backEndFinishTime = Sys_Milliseconds();
	backEnd.pc.msec = backEndFinishTime - backEndStartTime;
}

// ---------------------------------------------------------------------------
// HandleSetBuffer: acquire swapchain image, begin command buffer.
// No render pass started — RT writes to a storage image directly.
// ---------------------------------------------------------------------------

void idRenderBackendDrawVulkanRay::HandleSetBuffer( const emptyCommand_t *cmd ) {
	if ( frameActive ) return;

	if ( !frameSync.BeginFrame( vkState.swapchain ) ) {
		common->Warning( "VulkanRayBackend: BeginFrame failed" );
		return;
	}

	int frameIdx = frameSync.GetCurrentFrameIndex();
	allocator.BeginFrame( frameIdx );

	currentCmdBuf               = frameSync.GetCurrentCommandBuffer();
	vkState.activeCommandBuffer = currentCmdBuf;
	frameActive                 = true;
	screenshotReady             = false;
}

// ---------------------------------------------------------------------------
// HandleDrawView: build accel structures, trace rays, blit to swapchain.
// ---------------------------------------------------------------------------

void idRenderBackendDrawVulkanRay::HandleDrawView( const emptyCommand_t *cmd ) {
	if ( !frameActive ) return;

	const drawSurfsCommand_t *drawCmd = (const drawSurfsCommand_t *)cmd;
	currentViewDef  = drawCmd->viewDef;
	backEnd.viewDef = currentViewDef;

	// --- Camera UBO: view→world and clip→view matrices ---
	CameraUBO camUBO;
	memset( &camUBO, 0, sizeof(camUBO) );
	{
		const idMat3 &axis = currentViewDef->renderView.viewaxis;
		const idVec3 &org  = currentViewDef->renderView.vieworg;
		// viewInverse (camera→world), column-major
		camUBO.viewInverse[ 0] = axis[0][0]; camUBO.viewInverse[ 1] = axis[0][1];
		camUBO.viewInverse[ 2] = axis[0][2]; camUBO.viewInverse[ 3] = 0.0f;
		camUBO.viewInverse[ 4] = axis[1][0]; camUBO.viewInverse[ 5] = axis[1][1];
		camUBO.viewInverse[ 6] = axis[1][2]; camUBO.viewInverse[ 7] = 0.0f;
		camUBO.viewInverse[ 8] = axis[2][0]; camUBO.viewInverse[ 9] = axis[2][1];
		camUBO.viewInverse[10] = axis[2][2]; camUBO.viewInverse[11] = 0.0f;
		camUBO.viewInverse[12] = org[0];     camUBO.viewInverse[13] = org[1];
		camUBO.viewInverse[14] = org[2];     camUBO.viewInverse[15] = 1.0f;
		// projInverse (clip→view)
		Mat4Inverse( currentViewDef->projectionMatrix, camUBO.projInverse );

		// Benchmark pixel sampling rate (0 = disabled)
		camUBO.benchmarkPixels = benchmarkFile
			? (uint32_t)com_benchmarkPixels.GetInteger() : 0u;

		// Cache cam position and angles for BVH frame header
		lastCamPos[0] = org[0]; lastCamPos[1] = org[1]; lastCamPos[2] = org[2];
		idAngles ang = axis.ToAngles();
		lastCamAngles[0] = ang.pitch; lastCamAngles[1] = ang.yaw; lastCamAngles[2] = ang.roll;
	}

	VkBuffer     camBuf;
	VkDeviceSize camOffset;
	void        *camMapped;
	if ( !allocator.AllocFrameTemp( sizeof(CameraUBO), 256, camBuf, camOffset, &camMapped ) ) {
		currentViewDef = NULL;
		return;
	}
	memcpy( camMapped, &camUBO, sizeof(CameraUBO) );

	// --- Lights UBO ---
	LightsUBO lightUBO;
	memset( &lightUBO, 0, sizeof(lightUBO) );
	for ( viewLight_t *vl = currentViewDef->viewLights;
	      vl && lightUBO.numLights < RT_MAX_LIGHTS;
	      vl = vl->next )
	{
		int li = lightUBO.numLights;
		lightUBO.positions[li][0] = vl->globalLightOrigin[0];
		lightUBO.positions[li][1] = vl->globalLightOrigin[1];
		lightUBO.positions[li][2] = vl->globalLightOrigin[2];
		// w = sphere radius: max semi-axis of the ellipsoid light volume
		if ( vl->lightDef ) {
			const idVec3 &lr = vl->lightDef->parms.lightRadius;
			float r = lr[0];
			if ( lr[1] > r ) r = lr[1];
			if ( lr[2] > r ) r = lr[2];
			lightUBO.positions[li][3] = r;
		} else {
			lightUBO.positions[li][3] = 50.0f;  // fallback: 50-inch sphere
		}
		if ( vl->shaderRegisters ) {
			lightUBO.colors[li][0] = vl->shaderRegisters[0];
			lightUBO.colors[li][1] = vl->shaderRegisters[1];
			lightUBO.colors[li][2] = vl->shaderRegisters[2];
		} else {
			lightUBO.colors[li][0] = lightUBO.colors[li][1] = lightUBO.colors[li][2] = 1.0f;
		}
		lightUBO.colors[li][3] = 1.0f;
		lightUBO.numLights++;
	}

	lightUBO.lightDensity = 0.1f;  // 1 ray per 10 inches of light radius

	VkBuffer     lightBuf;
	VkDeviceSize lightOffset;
	void        *lightMapped;
	if ( !allocator.AllocFrameTemp( sizeof(LightsUBO), 256, lightBuf, lightOffset, &lightMapped ) ) {
		currentViewDef = NULL;
		return;
	}
	memcpy( lightMapped, &lightUBO, sizeof(LightsUBO) );

	// --- Build TLAS (GetOrBuildBLAS called per surface internally) ---
	uint32_t w = vkState.swapchainExtent.width;
	uint32_t h = vkState.swapchainExtent.height;
	uint32_t imageIndex = frameSync.GetCurrentImageIndex();
	VkImage  swapchainImage = vkState.swapchainImages[imageIndex];

	VkAccelerationStructureKHR tlas = vkRayAccelMgr.BuildTLAS(
		currentCmdBuf,
		currentViewDef->drawSurfs,
		currentViewDef->numDrawSurfs );

	if ( tlas == VK_NULL_HANDLE ) {
		// No geometry — blit black frame
		vkRayPipelineMgr.BlitToSwapchain( currentCmdBuf, swapchainImage,
			VK_IMAGE_LAYOUT_UNDEFINED, w, h );
		currentViewDef = NULL;
		return;
	}

	// --- Update descriptors and dispatch ray tracing ---
	vkRayPipelineMgr.UpdateDescriptors(
		tlas, camBuf, camOffset, lightBuf, lightOffset );

	vkRayPipelineMgr.BindAndTrace( currentCmdBuf, w, h );

	// --- Blit output storage image → swapchain ---
	vkRayPipelineMgr.BlitToSwapchain( currentCmdBuf, swapchainImage,
		VK_IMAGE_LAYOUT_UNDEFINED, w, h );

	backEnd.currentSpace = NULL;
	currentViewDef       = NULL;
}

// ---------------------------------------------------------------------------
// HandleSwapBuffers: optional screenshot capture, present.
// ---------------------------------------------------------------------------

void idRenderBackendDrawVulkanRay::HandleSwapBuffers( const emptyCommand_t *cmd ) {
	if ( !frameActive ) return;

	screenshotReady = false;
	if ( tr.takingScreenshot ) {
		uint32_t imageIndex = frameSync.GetCurrentImageIndex();
		VkImage  swapchainImage = vkState.swapchainImages[imageIndex];
		int w = (int)vkState.swapchainExtent.width;
		int h = (int)vkState.swapchainExtent.height;

		EnsureScreenshotBuffer( w, h );
		if ( screenshotBuffer != VK_NULL_HANDLE ) {
			VkImageMemoryBarrier toSrc = {};
			toSrc.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			toSrc.oldLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
			toSrc.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			toSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			toSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			toSrc.image               = swapchainImage;
			toSrc.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			toSrc.srcAccessMask       = 0;
			toSrc.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
			vkCmdPipelineBarrier( currentCmdBuf,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				0, 0, NULL, 0, NULL, 1, &toSrc );

			VkBufferImageCopy region = {};
			region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
			region.imageExtent      = { (uint32_t)w, (uint32_t)h, 1 };
			vkCmdCopyImageToBuffer( currentCmdBuf,
				swapchainImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				screenshotBuffer, 1, &region );

			VkImageMemoryBarrier toPresent = toSrc;
			toPresent.oldLayout   = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			toPresent.newLayout   = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
			toPresent.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			toPresent.dstAccessMask = 0;
			vkCmdPipelineBarrier( currentCmdBuf,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
				0, 0, NULL, 0, NULL, 1, &toPresent );

			screenshotReady = true;
		}
	}

	frameSync.EndFrame( vkState.swapchain, vkState.graphicsQueue, vkState.presentQueue );
	vkState.activeCommandBuffer = VK_NULL_HANDLE;
	currentCmdBuf               = VK_NULL_HANDLE;
	frameActive                 = false;

	// Benchmark readback: GPU writes are visible after fence wait in EndFrame()
	if ( benchmarkFile ) {
		uint32_t count = 0;
		const TrainingRecord *recs = nullptr;
		vkRayPipelineMgr.ReadBenchmarkRecords( count, recs );

		// Write SECTION_FRAME header + instance list to the file
		vkRayAccelMgr.WriteBVHFrame( benchmarkFile, (uint32_t)benchmarkFrameCount,
		                             lastCamPos, lastCamAngles, count );

		// Append TrainingRecord array directly after the frame header
		if ( count > 0 && recs ) {
			fwrite( recs, sizeof(TrainingRecord), count, benchmarkFile );
		}

		vkRayPipelineMgr.ResetBenchmarkCounter();
		benchmarkFrameCount++;

		// Periodic flush every 100 frames to limit data loss on long runs
		if ( benchmarkFrameCount % 100 == 0 ) {
			fflush( benchmarkFile );
		}
	}
}

// ---------------------------------------------------------------------------
// HandleCopyRender: no-op (no framebuffer to copy in the RT path).
// ---------------------------------------------------------------------------

void idRenderBackendDrawVulkanRay::HandleCopyRender( const emptyCommand_t *cmd ) {
}

// ---------------------------------------------------------------------------
// Screenshot helpers
// ---------------------------------------------------------------------------

void idRenderBackendDrawVulkanRay::EnsureScreenshotBuffer( int width, int height ) {
	if ( screenshotBuffer != VK_NULL_HANDLE &&
	     screenshotBufferWidth >= width &&
	     screenshotBufferHeight >= height ) {
		return;
	}

	if ( screenshotBuffer != VK_NULL_HANDLE ) {
		vkDestroyBuffer( vkState.device, screenshotBuffer, NULL );
		screenshotBuffer = VK_NULL_HANDLE;
	}
	if ( screenshotMemory != VK_NULL_HANDLE ) {
		vkFreeMemory( vkState.device, screenshotMemory, NULL );
		screenshotMemory = VK_NULL_HANDLE;
	}

	VkDeviceSize bufSize = (VkDeviceSize)width * height * 4;

	VkBufferCreateInfo bci = {};
	bci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bci.size        = bufSize;
	bci.usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if ( vkCreateBuffer( vkState.device, &bci, NULL, &screenshotBuffer ) != VK_SUCCESS ) {
		return;
	}

	VkMemoryRequirements mr;
	vkGetBufferMemoryRequirements( vkState.device, screenshotBuffer, &mr );

	VkPhysicalDeviceMemoryProperties memProp;
	vkGetPhysicalDeviceMemoryProperties( vkState.physicalDevice, &memProp );
	uint32_t memType = UINT32_MAX;
	VkMemoryPropertyFlags wanted =
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	for ( uint32_t i = 0; i < memProp.memoryTypeCount; i++ ) {
		if ( (mr.memoryTypeBits & (1u<<i)) &&
		     (memProp.memoryTypes[i].propertyFlags & wanted) == wanted ) {
			memType = i; break;
		}
	}
	if ( memType == UINT32_MAX ) {
		vkDestroyBuffer( vkState.device, screenshotBuffer, NULL );
		screenshotBuffer = VK_NULL_HANDLE;
		return;
	}

	VkMemoryAllocateInfo mai = {};
	mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	mai.allocationSize  = mr.size;
	mai.memoryTypeIndex = memType;
	if ( vkAllocateMemory( vkState.device, &mai, NULL, &screenshotMemory ) != VK_SUCCESS ) {
		vkDestroyBuffer( vkState.device, screenshotBuffer, NULL );
		screenshotBuffer = VK_NULL_HANDLE;
		return;
	}
	vkBindBufferMemory( vkState.device, screenshotBuffer, screenshotMemory, 0 );
	screenshotBufferWidth  = width;
	screenshotBufferHeight = height;
}

void idRenderBackendDrawVulkanRay::ReadPixels( int x, int y,
                                               int width, int height,
                                               byte *buffer ) {
	if ( !screenshotReady || screenshotBuffer == VK_NULL_HANDLE ) {
		memset( buffer, 0, width * height * 3 );
		return;
	}
	frameSync.WaitIdle();

	void *mapped;
	VkDeviceSize sz = (VkDeviceSize)screenshotBufferWidth * screenshotBufferHeight * 4;
	if ( vkMapMemory( vkState.device, screenshotMemory, 0, sz, 0, &mapped ) != VK_SUCCESS ) {
		memset( buffer, 0, width * height * 3 );
		return;
	}
	const byte *src = (const byte *)mapped;
	int srcStride = screenshotBufferWidth * 4;
	for ( int row = 0; row < height; row++ ) {
		int srcRow  = y + row;
		int dstRow  = height - 1 - row;
		const byte *srcLine = src + srcRow * srcStride + x * 4;
		byte       *dstLine = buffer + dstRow * width * 3;
		for ( int col = 0; col < width; col++ ) {
			dstLine[col*3+0] = srcLine[col*4+2];
			dstLine[col*3+1] = srcLine[col*4+1];
			dstLine[col*3+2] = srcLine[col*4+0];
		}
	}
	vkUnmapMemory( vkState.device, screenshotMemory );
}

// ---------------------------------------------------------------------------
// Singleton factory
// ---------------------------------------------------------------------------

static idRenderBackendDrawVulkanRay s_vkRayBackendDraw;

idRenderBackendDraw * R_GetVulkanRayBackendDraw() {
	return &s_vkRayBackendDraw;
}

#endif // ID_VULKAN
