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
#include "renderer/vk/VulkanRenderPass.h"
#include "renderer/vk/VulkanImage.h"
#include "renderer/vk/VulkanPipeline.h"
#include "renderer/vk/VulkanDescriptorAllocator.h"
#include "renderer/Image.h"
#include "framework/Common.h"
#include "sys/sys_imgui.h"

/*
===============================================================================

	Vulkan draw backend — full rendering pipeline.

	Implements the depth pre-pass, stencil shadow volumes, bump-mapped
	light interactions, shader passes, and GUI overlay rendering
	equivalent to the GL backend's draw_common.cpp / draw_arb2.cpp.

===============================================================================
*/

// ---------------------------------------------------------------------------
// Vertex upload helpers
//
// The vertex cache uses virtual memory (CPU pointers) when Vulkan is active.
// These helpers copy vertex/index data into per-frame Vulkan staging buffers.
// ---------------------------------------------------------------------------

struct vkBufferRef_t {
	VkBuffer	buffer;
	VkDeviceSize offset;
};

// ---------------------------------------------------------------------------
// File-static context for the interaction callback.
// RB_CreateSingleDrawInteractions calls our static callback for each
// bump+diffuse+specular combination. These pointers let the callback
// access the Vulkan command buffer and allocators without being a member fn.
// ---------------------------------------------------------------------------
static VkCommandBuffer		s_interCmdBuf = VK_NULL_HANDLE;
static idVulkanAllocator *	s_interAlloc = NULL;
static idVulkanDescriptorAllocator * s_interDescAlloc = NULL;
static VkPipelineLayout		s_interLayout = VK_NULL_HANDLE;
static VkDevice				s_interDevice = VK_NULL_HANDLE;
static const srfTriangles_t * s_interTri = NULL;

static bool VK_UploadVertexData( idVulkanAllocator &allocator,
	const void *data, int size, int alignment, vkBufferRef_t &out )
{
	void *mapped;
	if ( !allocator.AllocFrameTemp( size, alignment, out.buffer, out.offset, &mapped ) ) {
		return false;
	}
	memcpy( mapped, data, size );
	return true;
}

// ---------------------------------------------------------------------------
// Vulkan draw backend class
// ---------------------------------------------------------------------------

class idRenderBackendDrawVulkan : public idRenderBackendDraw {
public:
						idRenderBackendDrawVulkan();

	virtual void		Init();
	virtual void		Shutdown();
	virtual void		ExecuteBackEndCommands( const emptyCommand_t *cmds );

private:
	// Command handlers
	void				HandleSetBuffer( const emptyCommand_t *cmd );
	void				HandleDrawView( const emptyCommand_t *cmd );
	void				HandleSwapBuffers( const emptyCommand_t *cmd );
	void				HandleCopyRender( const emptyCommand_t *cmd );

	// View rendering stages (mirrors GL backend's RB_STD_DrawView)
	void				VK_BeginDrawingView();
	void				VK_FillDepthBuffer();
	void				VK_DrawInteractions();
	void				VK_StencilShadowPass( const drawSurf_t *drawSurfs );
	void				VK_CreateDrawInteractions( const drawSurf_t *surf );
	int					VK_DrawShaderPasses( const drawSurf_t **drawSurfs, int numDrawSurfs );
	void				VK_FogAllLights();
	void				VK_FogPass( const drawSurf_t *drawSurfs, const drawSurf_t *drawSurfs2 );
	void				VK_FogSurfaceChain( const drawSurf_t *drawSurfs,
						const idPlane *fogPlanes, const float *fogColor,
						const vulkanTexture_t *fogTex,
						const vulkanTexture_t *fogEnterTex,
						VkPipelineLayout layout );

	// Helpers
	void				VK_SetScissor( VkCommandBuffer cmd_buf, const idScreenRect &rect );
	bool				VK_BindSurfaceVertices( const srfTriangles_t *tri,
						vkBufferRef_t &vertRef );
	bool				VK_BindSurfaceIndices( const srfTriangles_t *tri,
						vkBufferRef_t &idxRef );
	bool				VK_BindShadowVertices( const srfTriangles_t *tri,
						vkBufferRef_t &vertRef );

	// MVP matrix helpers
	void				VK_ComputeMVP( const float *modelView, const float *projection,
						float *mvp );

	// Subsystems
	idVulkanFrameSync			frameSync;
	idVulkanAllocator			allocator;
	idVulkanRenderPassManager	renderPassMgr;
	idVulkanDescriptorAllocator	descriptorAlloc;

	bool					initialized;
	bool					frameActive;
	bool					renderPassLoadActive;	// true after CopyRender restarts with load pass

	// Screenshot readback
	void				ReadPixels( int x, int y, int width, int height, byte *buffer );
	void				EnsureScreenshotBuffer( int width, int height );

	VkBuffer			screenshotBuffer;
	VkDeviceMemory		screenshotMemory;
	int					screenshotBufferWidth;
	int					screenshotBufferHeight;
	bool				screenshotReady;		// true if staging buffer has valid data

	// Per-view state
	VkCommandBuffer			currentCmdBuf;
	const viewDef_t *		currentViewDef;
};

// ---------------------------------------------------------------------------
// Init / Shutdown
// ---------------------------------------------------------------------------

idRenderBackendDrawVulkan::idRenderBackendDrawVulkan()
	: initialized( false )
	, frameActive( false )
	, renderPassLoadActive( false )
	, screenshotBuffer( VK_NULL_HANDLE )
	, screenshotMemory( VK_NULL_HANDLE )
	, screenshotBufferWidth( 0 )
	, screenshotBufferHeight( 0 )
	, screenshotReady( false )
	, currentCmdBuf( VK_NULL_HANDLE )
	, currentViewDef( NULL )
{
}

void idRenderBackendDrawVulkan::Init() {
	if ( vkState.device == VK_NULL_HANDLE ) {
		common->Warning( "VulkanBackendDraw::Init: no Vulkan device" );
		return;
	}

	if ( !frameSync.Init( vkState.device, vkState.graphicsQueueFamily ) ) {
		common->Warning( "VulkanBackendDraw::Init: frameSync init failed" );
		return;
	}

	if ( !allocator.Init( vkState.device, vkState.physicalDevice ) ) {
		common->Warning( "VulkanBackendDraw::Init: allocator init failed" );
		frameSync.Shutdown();
		return;
	}

	if ( !renderPassMgr.Init( vkState.device, vkState.physicalDevice,
		vkState.swapchainFormat, vkState.depthFormat,
		vkState.swapchainExtent,
		(uint32_t)vkState.swapchainImageViews.size(),
		vkState.swapchainImageViews.data() ) )
	{
		common->Warning( "VulkanBackendDraw::Init: renderPassMgr init failed" );
		allocator.Shutdown();
		frameSync.Shutdown();
		return;
	}

	if ( !vkTextureMgr.Init( vkState.device, vkState.physicalDevice,
		vkState.graphicsQueueFamily ) )
	{
		common->Warning( "VulkanBackendDraw::Init: textureMgr init failed" );
		renderPassMgr.Shutdown();
		allocator.Shutdown();
		frameSync.Shutdown();
		return;
	}

	if ( !vkPipelineMgr.Init( vkState.device,
		renderPassMgr.GetMainRenderPass(),
		vkState.swapchainExtent, NULL ) )
	{
		common->Warning( "VulkanBackendDraw::Init: pipelineMgr init failed" );
		vkTextureMgr.Shutdown();
		renderPassMgr.Shutdown();
		allocator.Shutdown();
		frameSync.Shutdown();
		return;
	}

	if ( !descriptorAlloc.Init( vkState.device,
		vkPipelineMgr.GetUBOLayout(),
		vkPipelineMgr.GetSamplerLayout(), 2 ) )
	{
		common->Warning( "VulkanBackendDraw::Init: descriptorAlloc init failed" );
		vkPipelineMgr.Shutdown();
		vkTextureMgr.Shutdown();
		renderPassMgr.Shutdown();
		allocator.Shutdown();
		frameSync.Shutdown();
		return;
	}

	// Expose render pass for cross-subsystem use (e.g. ImGui Vulkan backend)
	vkState.mainRenderPass = renderPassMgr.GetMainRenderPass();
	vkState.activeCommandBuffer = VK_NULL_HANDLE;

	// Now that vkTextureMgr is ready, load Vulkan textures for all
	// images that were registered during early init.
	VK_LoadAllImages();

	initialized = true;
	common->Printf( "Vulkan draw backend initialized\n" );
}

void idRenderBackendDrawVulkan::Shutdown() {
	if ( !initialized ) {
		return;
	}

	if ( vkState.device != VK_NULL_HANDLE ) {
		frameSync.WaitIdle();
	}

	if ( screenshotBuffer != VK_NULL_HANDLE ) {
		vkDestroyBuffer( vkState.device, screenshotBuffer, NULL );
		screenshotBuffer = VK_NULL_HANDLE;
	}
	if ( screenshotMemory != VK_NULL_HANDLE ) {
		vkFreeMemory( vkState.device, screenshotMemory, NULL );
		screenshotMemory = VK_NULL_HANDLE;
	}
	screenshotBufferWidth = 0;
	screenshotBufferHeight = 0;
	screenshotReady = false;

	descriptorAlloc.Shutdown();
	vkPipelineMgr.Shutdown();
	vkTextureMgr.Shutdown();
	renderPassMgr.Shutdown();
	allocator.Shutdown();
	frameSync.Shutdown();

	initialized = false;
	frameActive = false;
	renderPassLoadActive = false;
	common->Printf( "Vulkan draw backend shut down\n" );
}

// ---------------------------------------------------------------------------
// Command dispatch
// ---------------------------------------------------------------------------

void idRenderBackendDrawVulkan::ExecuteBackEndCommands( const emptyCommand_t *cmds ) {
	if ( !initialized ) {
		return;
	}

	if ( cmds->commandId == RC_NOP && !cmds->next ) {
		return;
	}

	extern int backEndStartTime, backEndFinishTime;
	backEndStartTime = Sys_Milliseconds();

	for ( ; cmds; cmds = (const emptyCommand_t *)cmds->next ) {
		switch ( cmds->commandId ) {
		case RC_NOP:
			break;
		case RC_DRAW_VIEW:
			HandleDrawView( cmds );
			break;
		case RC_SET_BUFFER:
			HandleSetBuffer( cmds );
			break;
		case RC_SWAP_BUFFERS:
			HandleSwapBuffers( cmds );
			break;
		case RC_COPY_RENDER:
			HandleCopyRender( cmds );
			break;
		default:
			common->Error( "VulkanBackendDraw: bad commandId" );
			break;
		}
	}

	backEndFinishTime = Sys_Milliseconds();
	backEnd.pc.msec = backEndFinishTime - backEndStartTime;
}

// ---------------------------------------------------------------------------
// Command handlers
// ---------------------------------------------------------------------------

void idRenderBackendDrawVulkan::HandleSetBuffer( const emptyCommand_t *cmd ) {
	if ( frameActive ) {
		return;
	}

	if ( !frameSync.BeginFrame( vkState.swapchain ) ) {
		common->Warning( "VulkanBackendDraw: BeginFrame failed (swapchain out of date?)" );
		return;
	}

	int frameIdx = frameSync.GetCurrentFrameIndex();
	allocator.BeginFrame( frameIdx );
	descriptorAlloc.BeginFrame( frameIdx );

	// Begin the render pass
	currentCmdBuf = frameSync.GetCurrentCommandBuffer();
	vkState.activeCommandBuffer = currentCmdBuf;
	uint32_t imageIndex = frameSync.GetCurrentImageIndex();

	VkRenderPassBeginInfo rpBegin = {};
	rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	rpBegin.renderPass = renderPassMgr.GetMainRenderPass();
	rpBegin.framebuffer = renderPassMgr.GetFramebuffer( imageIndex );
	rpBegin.renderArea.offset = { 0, 0 };
	rpBegin.renderArea.extent = renderPassMgr.GetExtent();

	VkClearValue clearValues[2] = {};
	clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
	clearValues[1].depthStencil = { 1.0f, 0 };
	rpBegin.clearValueCount = 2;
	rpBegin.pClearValues = clearValues;

	vkCmdBeginRenderPass( currentCmdBuf, &rpBegin, VK_SUBPASS_CONTENTS_INLINE );

	frameActive = true;
	renderPassLoadActive = false;
}

void idRenderBackendDrawVulkan::HandleDrawView( const emptyCommand_t *cmd ) {
	if ( !frameActive ) {
		return;
	}

	const drawSurfsCommand_t *drawCmd = (const drawSurfsCommand_t *)cmd;
	currentViewDef = drawCmd->viewDef;
	backEnd.viewDef = currentViewDef;

	// Mirrors GL backend's RB_STD_DrawView flow:
	// 1. Setup viewport/scissor
	VK_BeginDrawingView();

	// 2. Depth pre-pass
	VK_FillDepthBuffer();

	// 3. Light interactions (shadow + interaction per light)
	VK_DrawInteractions();

	// 4. Shader passes (ambient stages, GUI, decals, particles)
	VK_DrawShaderPasses( (const drawSurf_t **)currentViewDef->drawSurfs, currentViewDef->numDrawSurfs );

	// 5. Fog and blend lights
	VK_FogAllLights();

	// Reset state
	backEnd.currentSpace = NULL;
	currentViewDef = NULL;
}

void idRenderBackendDrawVulkan::HandleSwapBuffers( const emptyCommand_t *cmd ) {
	if ( !frameActive ) {
		return;
	}

	// Render ImGui overlay before ending the render pass — ImGui records
	// draw commands into vkState.activeCommandBuffer within the current
	// render pass, analogous to GL backend's call in RB_SwapBuffers.
	D3::ImGuiHooks::EndFrame();

	vkCmdEndRenderPass( currentCmdBuf );

	// If a screenshot is being taken, copy the framebuffer to a staging buffer
	// before presenting. The render pass just ended so the swapchain image
	// is in PRESENT_SRC_KHR layout.
	screenshotReady = false;
	if ( tr.takingScreenshot ) {
		uint32_t imageIndex = frameSync.GetCurrentImageIndex();
		VkImage swapchainImage = vkState.swapchainImages[imageIndex];
		int w = (int)vkState.swapchainExtent.width;
		int h = (int)vkState.swapchainExtent.height;

		EnsureScreenshotBuffer( w, h );

		if ( screenshotBuffer != VK_NULL_HANDLE ) {
			// Transition swapchain: PRESENT_SRC_KHR → TRANSFER_SRC_OPTIMAL
			VkImageMemoryBarrier toSrc = {};
			toSrc.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			toSrc.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
			toSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			toSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			toSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			toSrc.image = swapchainImage;
			toSrc.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			toSrc.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			toSrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

			vkCmdPipelineBarrier( currentCmdBuf,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				0, 0, NULL, 0, NULL, 1, &toSrc );

			// Copy image to buffer
			VkBufferImageCopy region = {};
			region.bufferOffset = 0;
			region.bufferRowLength = 0;
			region.bufferImageHeight = 0;
			region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
			region.imageOffset = { 0, 0, 0 };
			region.imageExtent = { (uint32_t)w, (uint32_t)h, 1 };

			vkCmdCopyImageToBuffer( currentCmdBuf,
				swapchainImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				screenshotBuffer, 1, &region );

			// Transition swapchain back: TRANSFER_SRC → PRESENT_SRC_KHR
			VkImageMemoryBarrier toPresent = toSrc;
			toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
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
	currentCmdBuf = VK_NULL_HANDLE;
	frameActive = false;
	renderPassLoadActive = false;
}

void idRenderBackendDrawVulkan::HandleCopyRender( const emptyCommand_t *cmd ) {
	if ( !frameActive ) {
		return;
	}

	extern idCVar r_skipCopyTexture;
	if ( r_skipCopyTexture.GetBool() ) {
		return;
	}

	const copyRenderCommand_t *copyCmd = (const copyRenderCommand_t *)cmd;
	if ( !copyCmd->image ) {
		return;
	}

	int x = copyCmd->x;
	int y = copyCmd->y;
	int imageWidth = copyCmd->imageWidth;
	int imageHeight = copyCmd->imageHeight;

	// Round up to power of two (matching GL backend's CopyFramebuffer behavior)
	int potWidth = imageWidth;
	int potHeight = imageHeight;
	for ( int pot = 1; pot < potWidth; pot <<= 1 ) {}
	potWidth = 1;
	while ( potWidth < imageWidth ) { potWidth <<= 1; }
	potHeight = 1;
	while ( potHeight < imageHeight ) { potHeight <<= 1; }

	// Ensure the target image has a Vulkan texture of the right size
	idImage *targetImage = copyCmd->image;
	bool needCreate = false;

	if ( targetImage->vkHandle == 0 ) {
		needCreate = true;
	} else {
		const vulkanTexture_t *existing = vkTextureMgr.GetTexture( targetImage->vkHandle );
		if ( !existing || !existing->allocated ||
			(int)existing->width != potWidth || (int)existing->height != potHeight ) {
			// Free old handle and recreate
			if ( targetImage->vkHandle != 0 ) {
				vkTextureMgr.FreeHandle( targetImage->vkHandle );
				targetImage->vkHandle = 0;
			}
			needCreate = true;
		}
	}

	if ( needCreate ) {
		unsigned int handle = vkTextureMgr.AllocHandle();
		if ( handle == 0 ) {
			common->Warning( "HandleCopyRender: out of texture handles" );
			return;
		}
		// Create with NULL data — we'll fill via copy command
		if ( !vkTextureMgr.CreateTexture2D( handle, NULL, potWidth, potHeight, 1,
			vkState.swapchainFormat,
			VK_FILTER_LINEAR, VK_FILTER_LINEAR,
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE ) ) {
			common->Warning( "HandleCopyRender: failed to create target texture" );
			vkTextureMgr.FreeHandle( handle );
			return;
		}
		targetImage->vkHandle = handle;
		targetImage->uploadWidth = potWidth;
		targetImage->uploadHeight = potHeight;
	}

	const vulkanTexture_t *targetTex = vkTextureMgr.GetTexture( targetImage->vkHandle );
	if ( !targetTex || !targetTex->allocated ) {
		return;
	}

	uint32_t imageIndex = frameSync.GetCurrentImageIndex();
	VkImage swapchainImage = vkState.swapchainImages[imageIndex];
	VkImage dstImage = targetTex->image;

	// ---- End the current render pass ----
	vkCmdEndRenderPass( currentCmdBuf );

	// ---- Transition swapchain image: PRESENT_SRC_KHR → TRANSFER_SRC_OPTIMAL ----
	// (main render pass finalLayout is PRESENT_SRC_KHR)
	{
		VkImageMemoryBarrier barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = swapchainImage;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;
		barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

		vkCmdPipelineBarrier( currentCmdBuf,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			0, 0, NULL, 0, NULL, 1, &barrier );
	}

	// ---- Transition target image: SHADER_READ_ONLY / UNDEFINED → TRANSFER_DST_OPTIMAL ----
	{
		VkImageMemoryBarrier barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = needCreate ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = dstImage;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;
		barrier.srcAccessMask = needCreate ? 0 : VK_ACCESS_SHADER_READ_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

		vkCmdPipelineBarrier( currentCmdBuf,
			needCreate ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			0, 0, NULL, 0, NULL, 1, &barrier );
	}

	// ---- Copy the framebuffer region to the target image ----
	// Use vkCmdBlitImage to handle potential format mismatch and POT sizing
	VkImageBlit blitRegion = {};
	blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blitRegion.srcSubresource.mipLevel = 0;
	blitRegion.srcSubresource.baseArrayLayer = 0;
	blitRegion.srcSubresource.layerCount = 1;
	blitRegion.srcOffsets[0] = { x, y, 0 };
	blitRegion.srcOffsets[1] = { x + imageWidth, y + imageHeight, 1 };

	blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blitRegion.dstSubresource.mipLevel = 0;
	blitRegion.dstSubresource.baseArrayLayer = 0;
	blitRegion.dstSubresource.layerCount = 1;
	blitRegion.dstOffsets[0] = { 0, 0, 0 };
	blitRegion.dstOffsets[1] = { imageWidth, imageHeight, 1 };

	vkCmdBlitImage( currentCmdBuf,
		swapchainImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1, &blitRegion, VK_FILTER_LINEAR );

	// If POT is larger than image, duplicate edge pixels (like GL backend)
	if ( potWidth > imageWidth ) {
		VkImageBlit edgeBlit = blitRegion;
		edgeBlit.srcOffsets[0] = { x + imageWidth - 1, y, 0 };
		edgeBlit.srcOffsets[1] = { x + imageWidth, y + imageHeight, 1 };
		edgeBlit.dstOffsets[0] = { imageWidth, 0, 0 };
		edgeBlit.dstOffsets[1] = { potWidth, imageHeight, 1 };
		vkCmdBlitImage( currentCmdBuf,
			swapchainImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &edgeBlit, VK_FILTER_NEAREST );
	}
	if ( potHeight > imageHeight ) {
		VkImageBlit edgeBlit = blitRegion;
		edgeBlit.srcOffsets[0] = { x, y + imageHeight - 1, 0 };
		edgeBlit.srcOffsets[1] = { x + imageWidth, y + imageHeight, 1 };
		edgeBlit.dstOffsets[0] = { 0, imageHeight, 0 };
		edgeBlit.dstOffsets[1] = { imageWidth, potHeight, 1 };
		vkCmdBlitImage( currentCmdBuf,
			swapchainImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &edgeBlit, VK_FILTER_NEAREST );
	}

	// ---- Transition target image: TRANSFER_DST → SHADER_READ_ONLY ----
	{
		VkImageMemoryBarrier barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = dstImage;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		vkCmdPipelineBarrier( currentCmdBuf,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			0, 0, NULL, 0, NULL, 1, &barrier );
	}

	// ---- Transition swapchain image: TRANSFER_SRC → COLOR_ATTACHMENT_OPTIMAL ----
	// (load render pass expects initialLayout = COLOR_ATTACHMENT_OPTIMAL)
	{
		VkImageMemoryBarrier barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = swapchainImage;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		vkCmdPipelineBarrier( currentCmdBuf,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			0, 0, NULL, 0, NULL, 1, &barrier );
	}

	// ---- Restart render pass with LOAD_OP_LOAD ----
	VkRenderPassBeginInfo rpBegin = {};
	rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	rpBegin.renderPass = renderPassMgr.GetLoadRenderPass();
	rpBegin.framebuffer = renderPassMgr.GetFramebuffer( imageIndex );
	rpBegin.renderArea.offset = { 0, 0 };
	rpBegin.renderArea.extent = renderPassMgr.GetExtent();
	rpBegin.clearValueCount = 0;
	rpBegin.pClearValues = NULL;

	vkCmdBeginRenderPass( currentCmdBuf, &rpBegin, VK_SUBPASS_CONTENTS_INLINE );

	renderPassLoadActive = true;

	backEnd.c_copyFrameBuffer++;
}

// ---------------------------------------------------------------------------
// View setup
// ---------------------------------------------------------------------------

void idRenderBackendDrawVulkan::VK_BeginDrawingView() {
	const viewDef_t *viewDef = currentViewDef;

	// Set viewport from viewDef.
	// Vulkan Y=0 is at the top, OpenGL Y=0 is at the bottom. Use a negative
	// viewport height (VK_KHR_maintenance1, core since Vulkan 1.1) to flip Y
	// so the engine's OpenGL-convention projection matrices work correctly.
	float vpWidth  = (float)( viewDef->viewport.x2 - viewDef->viewport.x1 + 1 );
	float vpHeight = (float)( viewDef->viewport.y2 - viewDef->viewport.y1 + 1 );

	VkViewport viewport = {};
	viewport.x = (float)viewDef->viewport.x1;
	viewport.y = (float)viewDef->viewport.y1 + vpHeight;	// start at bottom
	viewport.width = vpWidth;
	viewport.height = -vpHeight;	// negative height = flip Y
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport( currentCmdBuf, 0, 1, &viewport );

	// Set scissor from viewDef
	VK_SetScissor( currentCmdBuf, viewDef->viewport );

	// Clear depth and stencil for this view. The initial render pass
	// LOAD_OP_CLEAR handles the first view, but subsequent views after
	// a CopyRender (which restarts with LOAD_OP_LOAD) need an explicit
	// clear. Always clearing is safe and matches the GL backend behavior.
	VkClearAttachment clearAtts[1] = {};
	clearAtts[0].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	clearAtts[0].clearValue.depthStencil = { 1.0f, 0 };
	VkClearRect clearRect = {};
	clearRect.rect.offset = { 0, 0 };
	clearRect.rect.extent = renderPassMgr.GetExtent();
	clearRect.baseArrayLayer = 0;
	clearRect.layerCount = 1;
	vkCmdClearAttachments( currentCmdBuf, 1, clearAtts, 1, &clearRect );

	backEnd.currentSpace = NULL;
	backEnd.currentScissor = viewDef->viewport;
}

// ---------------------------------------------------------------------------
// Depth pre-pass
// ---------------------------------------------------------------------------

void idRenderBackendDrawVulkan::VK_FillDepthBuffer() {
	const viewDef_t *viewDef = currentViewDef;

	if ( viewDef->numDrawSurfs == 0 ) {
		return;
	}

	VkPipeline depthPipeline = vkPipelineMgr.GetPipeline( VK_PIPELINE_DEPTH_FILL );
	VkPipelineLayout depthLayout = vkPipelineMgr.GetPipelineLayout( VK_PIPELINE_DEPTH_FILL );
	if ( depthPipeline == VK_NULL_HANDLE ) {
		return;
	}

	vkCmdBindPipeline( currentCmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, depthPipeline );

	const drawSurf_t **drawSurfs = (const drawSurf_t **)viewDef->drawSurfs;
	int numDrawSurfs = viewDef->numDrawSurfs;

	for ( int i = 0; i < numDrawSurfs; i++ ) {
		const drawSurf_t *surf = drawSurfs[i];
		const srfTriangles_t *tri = surf->geo;
		const idMaterial *shader = surf->material;

		if ( !tri || !tri->ambientCache ) {
			continue;
		}
		if ( tri->numIndexes == 0 ) {
			continue;
		}
		if ( !shader ) {
			continue;
		}

		// Skip translucent surfaces in depth pass
		if ( shader->Coverage() == MC_TRANSLUCENT ) {
			continue;
		}

		// Set per-surface scissor
		if ( !backEnd.currentScissor.Equals( surf->scissorRect ) ) {
			VK_SetScissor( currentCmdBuf, surf->scissorRect );
			backEnd.currentScissor = surf->scissorRect;
		}

		// Compute MVP: projection * modelView
		float mvp[16];
		VK_ComputeMVP( surf->space->modelViewMatrix,
			viewDef->projectionMatrix, mvp );

		// Push MVP as push constant
		vkCmdPushConstants( currentCmdBuf, depthLayout,
			VK_SHADER_STAGE_VERTEX_BIT, 0, 64, mvp );

		// Upload and bind vertex data
		vkBufferRef_t vertRef;
		if ( !VK_BindSurfaceVertices( tri, vertRef ) ) {
			continue;
		}
		vkCmdBindVertexBuffers( currentCmdBuf, 0, 1, &vertRef.buffer, &vertRef.offset );

		// Upload and bind index data
		vkBufferRef_t idxRef;
		if ( !VK_BindSurfaceIndices( tri, idxRef ) ) {
			continue;
		}
		vkCmdBindIndexBuffer( currentCmdBuf, idxRef.buffer, idxRef.offset,
			VK_INDEX_TYPE_UINT32 );

		// Draw
		vkCmdDrawIndexed( currentCmdBuf, tri->numIndexes, 1, 0, 0, 0 );

		backEnd.pc.c_drawElements++;
		backEnd.pc.c_drawIndexes += tri->numIndexes;
	}
}

// ---------------------------------------------------------------------------
// Stencil shadow pass
// ---------------------------------------------------------------------------

void idRenderBackendDrawVulkan::VK_StencilShadowPass( const drawSurf_t *drawSurfs ) {
	if ( !drawSurfs ) {
		return;
	}

	VkPipeline shadowPipeline = vkPipelineMgr.GetPipeline( VK_PIPELINE_SHADOW );
	VkPipelineLayout shadowLayout = vkPipelineMgr.GetPipelineLayout( VK_PIPELINE_SHADOW );
	if ( shadowPipeline == VK_NULL_HANDLE ) {
		return;
	}

	vkCmdBindPipeline( currentCmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline );

	for ( const drawSurf_t *surf = drawSurfs; surf; surf = surf->nextOnLight ) {
		const srfTriangles_t *tri = surf->geo;

		if ( !tri || !tri->shadowCache ) {
			continue;
		}
		if ( tri->numIndexes == 0 ) {
			continue;
		}

		// Set scissor
		if ( !backEnd.currentScissor.Equals( surf->scissorRect ) ) {
			VK_SetScissor( currentCmdBuf, surf->scissorRect );
			backEnd.currentScissor = surf->scissorRect;
		}

		// Compute MVP
		float mvp[16];
		VK_ComputeMVP( surf->space->modelViewMatrix,
			currentViewDef->projectionMatrix, mvp );

		// Push constants: MVP (64 bytes) + light origin in local space (16 bytes)
		struct {
			float mvp[16];
			float lightOrigin[4];
		} pushData;
		memcpy( pushData.mvp, mvp, sizeof( mvp ) );

		// Transform global light origin to local surface space
		idVec4 localLight;
		R_GlobalPointToLocal( surf->space->modelMatrix,
			backEnd.vLight->globalLightOrigin, localLight.ToVec3() );
		localLight.w = 1.0f;
		memcpy( pushData.lightOrigin, &localLight, 16 );

		vkCmdPushConstants( currentCmdBuf, shadowLayout,
			VK_SHADER_STAGE_VERTEX_BIT, 0, 80, &pushData );

		// Upload and bind shadow vertex data (vec4 positions)
		vkBufferRef_t vertRef;
		if ( !VK_BindShadowVertices( tri, vertRef ) ) {
			continue;
		}
		vkCmdBindVertexBuffers( currentCmdBuf, 0, 1, &vertRef.buffer, &vertRef.offset );

		// Upload and bind index data
		vkBufferRef_t idxRef;
		if ( !VK_BindSurfaceIndices( tri, idxRef ) ) {
			continue;
		}
		vkCmdBindIndexBuffer( currentCmdBuf, idxRef.buffer, idxRef.offset,
			VK_INDEX_TYPE_UINT32 );

		// Determine index count based on shadow cap requirements.
		// Use full caps for safety (matches GL fallback behavior).
		int numIndexes = tri->numIndexes;

		vkCmdDrawIndexed( currentCmdBuf, numIndexes, 1, 0, 0, 0 );

		backEnd.pc.c_shadowElements++;
		backEnd.pc.c_shadowIndexes += numIndexes;
	}
}

// ---------------------------------------------------------------------------
// Light interaction rendering
// ---------------------------------------------------------------------------

void idRenderBackendDrawVulkan::VK_DrawInteractions() {
	const viewDef_t *viewDef = currentViewDef;

	for ( viewLight_t *vLight = viewDef->viewLights; vLight; vLight = vLight->next ) {
		backEnd.vLight = vLight;

		// Skip fog and blend lights for now
		const idMaterial *lightShader = vLight->lightShader;
		if ( lightShader->IsFogLight() || lightShader->IsBlendLight() ) {
			continue;
		}

		// Clear stencil to 0 before each light's shadow pass.
		// Each light needs a fresh stencil buffer so shadow volumes
		// from one light don't affect another.
		VkClearAttachment clearAtt = {};
		clearAtt.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
		clearAtt.clearValue.depthStencil = { 1.0f, 0 };
		VkClearRect clearRect = {};
		clearRect.rect.offset = { 0, 0 };
		clearRect.rect.extent = renderPassMgr.GetExtent();
		clearRect.baseArrayLayer = 0;
		clearRect.layerCount = 1;
		vkCmdClearAttachments( currentCmdBuf, 1, &clearAtt, 1, &clearRect );

		// Shadow passes
		if ( vLight->globalShadows || vLight->localShadows ) {
			VK_StencilShadowPass( vLight->globalShadows );
			VK_StencilShadowPass( vLight->localShadows );
		}

		// Interaction passes
		VK_CreateDrawInteractions( vLight->localInteractions );
		VK_CreateDrawInteractions( vLight->globalInteractions );
		VK_CreateDrawInteractions( vLight->translucentInteractions );
	}
}

// Ensure a texture is loaded for Vulkan. Mirrors GL's Bind() lazy-load behavior:
// if the image hasn't been loaded from disk yet, trigger ActuallyLoadImage which
// calls GenerateImage → creates the Vulkan texture via vkTextureMgr.
static void VK_EnsureImageLoaded( idImage *image ) {
	if ( image->texnum == idImage::TEXTURE_NOT_LOADED && image->vkHandle == 0 ) {
		image->ActuallyLoadImage( true, true );
	}
}

// Static callback for RB_CreateSingleDrawInteractions — called for
// each bump/diffuse/specular combination the engine decomposes.
// Uploads per-interaction UBOs, binds texture descriptor sets, and draws.
static void VK_DrawInteractionCallback( const drawInteraction_t *din ) {
	// Validate images
	if ( !din->bumpImage || !din->diffuseImage || !din->specularImage ||
		!din->lightImage || !din->lightFalloffImage ) {
		return;
	}

	// Trigger lazy loading for on-demand images (mirrors GL Bind() behavior)
	VK_EnsureImageLoaded( din->bumpImage );
	VK_EnsureImageLoaded( din->lightFalloffImage );
	VK_EnsureImageLoaded( din->lightImage );
	VK_EnsureImageLoaded( din->diffuseImage );
	VK_EnsureImageLoaded( din->specularImage );

	// Check all images have Vulkan handles
	if ( din->bumpImage->vkHandle == 0 || din->diffuseImage->vkHandle == 0 ||
		din->specularImage->vkHandle == 0 || din->lightImage->vkHandle == 0 ||
		din->lightFalloffImage->vkHandle == 0 ) {
		return;
	}

	idImage *specTableImg = globalImages->specularTableImage;
	if ( !specTableImg || specTableImg->vkHandle == 0 ) {
		return;
	}

	// Get Vulkan texture resources
	const vulkanTexture_t *bumpTex = vkTextureMgr.GetTexture( din->bumpImage->vkHandle );
	const vulkanTexture_t *falloffTex = vkTextureMgr.GetTexture( din->lightFalloffImage->vkHandle );
	const vulkanTexture_t *projTex = vkTextureMgr.GetTexture( din->lightImage->vkHandle );
	const vulkanTexture_t *diffuseTex = vkTextureMgr.GetTexture( din->diffuseImage->vkHandle );
	const vulkanTexture_t *specularTex = vkTextureMgr.GetTexture( din->specularImage->vkHandle );
	const vulkanTexture_t *specTableTex = vkTextureMgr.GetTexture( specTableImg->vkHandle );

	if ( !bumpTex || !bumpTex->allocated ||
		!falloffTex || !falloffTex->allocated ||
		!projTex || !projTex->allocated ||
		!diffuseTex || !diffuseTex->allocated ||
		!specularTex || !specularTex->allocated ||
		!specTableTex || !specTableTex->allocated ) {
		return;
	}

	// --- UBO: InteractionParams (set 0, binding 0) — 224 bytes ---
	struct InteractionParams {
		float lightOrigin[4];
		float viewOrigin[4];
		float lightProjectS[4];
		float lightProjectT[4];
		float lightProjectQ[4];
		float lightFalloffS[4];
		float bumpMatrixS[4];
		float bumpMatrixT[4];
		float diffuseMatrixS[4];
		float diffuseMatrixT[4];
		float specularMatrixS[4];
		float specularMatrixT[4];
		float colorModulate[4];
		float colorAdd[4];
	};

	InteractionParams interParams;
	memcpy( interParams.lightOrigin, din->localLightOrigin.ToFloatPtr(), 16 );
	memcpy( interParams.viewOrigin, din->localViewOrigin.ToFloatPtr(), 16 );
	memcpy( interParams.lightProjectS, din->lightProjection[0].ToFloatPtr(), 16 );
	memcpy( interParams.lightProjectT, din->lightProjection[1].ToFloatPtr(), 16 );
	memcpy( interParams.lightProjectQ, din->lightProjection[2].ToFloatPtr(), 16 );
	memcpy( interParams.lightFalloffS, din->lightProjection[3].ToFloatPtr(), 16 );
	memcpy( interParams.bumpMatrixS, din->bumpMatrix[0].ToFloatPtr(), 16 );
	memcpy( interParams.bumpMatrixT, din->bumpMatrix[1].ToFloatPtr(), 16 );
	memcpy( interParams.diffuseMatrixS, din->diffuseMatrix[0].ToFloatPtr(), 16 );
	memcpy( interParams.diffuseMatrixT, din->diffuseMatrix[1].ToFloatPtr(), 16 );
	memcpy( interParams.specularMatrixS, din->specularMatrix[0].ToFloatPtr(), 16 );
	memcpy( interParams.specularMatrixT, din->specularMatrix[1].ToFloatPtr(), 16 );

	// Vertex color handling
	static const float zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	static const float one[4]  = { 1.0f, 1.0f, 1.0f, 1.0f };
	static const float negOne[4] = { -1.0f, -1.0f, -1.0f, -1.0f };

	switch ( din->vertexColor ) {
	case SVC_IGNORE:
		memcpy( interParams.colorModulate, zero, 16 );
		memcpy( interParams.colorAdd, one, 16 );
		break;
	case SVC_MODULATE:
		memcpy( interParams.colorModulate, one, 16 );
		memcpy( interParams.colorAdd, zero, 16 );
		break;
	case SVC_INVERSE_MODULATE:
		memcpy( interParams.colorModulate, negOne, 16 );
		memcpy( interParams.colorAdd, one, 16 );
		break;
	}

	// --- UBO: FragParams (set 0, binding 1) — 48 bytes ---
	struct FragParams {
		float diffuseColor[4];
		float specularColor[4];
		float gammaBrightness[4];
	};

	FragParams fragParams;
	memcpy( fragParams.diffuseColor, din->diffuseColor.ToFloatPtr(), 16 );
	memcpy( fragParams.specularColor, din->specularColor.ToFloatPtr(), 16 );

	if ( r_gammaInShader.GetBool() ) {
		fragParams.gammaBrightness[0] = r_brightness.GetFloat();
		fragParams.gammaBrightness[1] = r_brightness.GetFloat();
		fragParams.gammaBrightness[2] = r_brightness.GetFloat();
		fragParams.gammaBrightness[3] = 1.0f / r_gamma.GetFloat();
	} else {
		fragParams.gammaBrightness[0] = 1.0f;
		fragParams.gammaBrightness[1] = 1.0f;
		fragParams.gammaBrightness[2] = 1.0f;
		fragParams.gammaBrightness[3] = 0.0f;	// 0 = skip gamma in shader
	}

	// Upload UBO data to frame-temp buffers (256-byte alignment for UBO offset)
	VkBuffer interParamsBuf;
	VkDeviceSize interParamsOff;
	void *interParamsMapped;
	if ( !s_interAlloc->AllocFrameTemp( sizeof(InteractionParams), 256,
		interParamsBuf, interParamsOff, &interParamsMapped ) ) {
		return;
	}
	memcpy( interParamsMapped, &interParams, sizeof(InteractionParams) );

	VkBuffer fragParamsBuf;
	VkDeviceSize fragParamsOff;
	void *fragParamsMapped;
	if ( !s_interAlloc->AllocFrameTemp( sizeof(FragParams), 256,
		fragParamsBuf, fragParamsOff, &fragParamsMapped ) ) {
		return;
	}
	memcpy( fragParamsMapped, &fragParams, sizeof(FragParams) );

	// Allocate + write UBO descriptor set (set 0)
	VkDescriptorSet uboSet = s_interDescAlloc->AllocUBOSet();
	if ( uboSet == VK_NULL_HANDLE ) {
		return;
	}

	VkDescriptorBufferInfo bufInfos[2] = {};
	bufInfos[0].buffer = interParamsBuf;
	bufInfos[0].offset = interParamsOff;
	bufInfos[0].range = sizeof(InteractionParams);
	bufInfos[1].buffer = fragParamsBuf;
	bufInfos[1].offset = fragParamsOff;
	bufInfos[1].range = sizeof(FragParams);

	VkWriteDescriptorSet uboWrites[2] = {};
	uboWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	uboWrites[0].dstSet = uboSet;
	uboWrites[0].dstBinding = 0;
	uboWrites[0].descriptorCount = 1;
	uboWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	uboWrites[0].pBufferInfo = &bufInfos[0];

	uboWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	uboWrites[1].dstSet = uboSet;
	uboWrites[1].dstBinding = 1;
	uboWrites[1].descriptorCount = 1;
	uboWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	uboWrites[1].pBufferInfo = &bufInfos[1];

	vkUpdateDescriptorSets( s_interDevice, 2, uboWrites, 0, NULL );

	// Allocate + write sampler descriptor set (set 1)
	VkDescriptorSet samplerSet = s_interDescAlloc->AllocSamplerSet();
	if ( samplerSet == VK_NULL_HANDLE ) {
		return;
	}

	VkDescriptorImageInfo imageInfos[6] = {};
	// binding 0: bumpMap
	imageInfos[0].sampler = bumpTex->sampler;
	imageInfos[0].imageView = bumpTex->imageView;
	imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	// binding 1: lightFalloff
	imageInfos[1].sampler = falloffTex->sampler;
	imageInfos[1].imageView = falloffTex->imageView;
	imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	// binding 2: lightProjection
	imageInfos[2].sampler = projTex->sampler;
	imageInfos[2].imageView = projTex->imageView;
	imageInfos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	// binding 3: diffuseMap
	imageInfos[3].sampler = diffuseTex->sampler;
	imageInfos[3].imageView = diffuseTex->imageView;
	imageInfos[3].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	// binding 4: specularMap
	imageInfos[4].sampler = specularTex->sampler;
	imageInfos[4].imageView = specularTex->imageView;
	imageInfos[4].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	// binding 5: specularTable
	imageInfos[5].sampler = specTableTex->sampler;
	imageInfos[5].imageView = specTableTex->imageView;
	imageInfos[5].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkWriteDescriptorSet samplerWrites[6] = {};
	for ( int i = 0; i < 6; i++ ) {
		samplerWrites[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		samplerWrites[i].dstSet = samplerSet;
		samplerWrites[i].dstBinding = i;
		samplerWrites[i].descriptorCount = 1;
		samplerWrites[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		samplerWrites[i].pImageInfo = &imageInfos[i];
	}

	vkUpdateDescriptorSets( s_interDevice, 6, samplerWrites, 0, NULL );

	// Bind both descriptor sets
	VkDescriptorSet sets[2] = { uboSet, samplerSet };
	vkCmdBindDescriptorSets( s_interCmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
		s_interLayout, 0, 2, sets, 0, NULL );

	// Draw — vertex/index buffers already bound by VK_CreateDrawInteractions
	vkCmdDrawIndexed( s_interCmdBuf, s_interTri->numIndexes, 1, 0, 0, 0 );
}

void idRenderBackendDrawVulkan::VK_CreateDrawInteractions( const drawSurf_t *surf ) {
	if ( !surf ) {
		return;
	}

	VkPipeline interactionPipeline = vkPipelineMgr.GetPipeline( VK_PIPELINE_INTERACTION );
	VkPipelineLayout interactionLayout = vkPipelineMgr.GetPipelineLayout( VK_PIPELINE_INTERACTION );
	if ( interactionPipeline == VK_NULL_HANDLE ) {
		return;
	}

	vkCmdBindPipeline( currentCmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, interactionPipeline );

	// Set file-static context for the interaction callback
	s_interCmdBuf = currentCmdBuf;
	s_interAlloc = &allocator;
	s_interDescAlloc = &descriptorAlloc;
	s_interLayout = interactionLayout;
	s_interDevice = vkState.device;

	for ( ; surf; surf = surf->nextOnLight ) {
		const srfTriangles_t *tri = surf->geo;

		if ( !tri || !tri->ambientCache ) {
			continue;
		}
		if ( tri->numIndexes == 0 ) {
			continue;
		}

		// Set scissor
		if ( !backEnd.currentScissor.Equals( surf->scissorRect ) ) {
			VK_SetScissor( currentCmdBuf, surf->scissorRect );
			backEnd.currentScissor = surf->scissorRect;
		}

		backEnd.currentSpace = surf->space;

		// Upload and bind vertex data for this surface
		vkBufferRef_t vertRef;
		if ( !VK_BindSurfaceVertices( tri, vertRef ) ) {
			continue;
		}
		vkCmdBindVertexBuffers( currentCmdBuf, 0, 1, &vertRef.buffer, &vertRef.offset );

		// Upload and bind index data
		vkBufferRef_t idxRef;
		if ( !VK_BindSurfaceIndices( tri, idxRef ) ) {
			continue;
		}
		vkCmdBindIndexBuffer( currentCmdBuf, idxRef.buffer, idxRef.offset,
			VK_INDEX_TYPE_UINT32 );

		// Push MVP
		float mvp[16];
		VK_ComputeMVP( surf->space->modelViewMatrix,
			currentViewDef->projectionMatrix, mvp );
		vkCmdPushConstants( currentCmdBuf, interactionLayout,
			VK_SHADER_STAGE_VERTEX_BIT, 0, 64, mvp );

		// Set tri for the callback to use in draw calls
		s_interTri = tri;

		// Engine decomposes this surface's material into individual
		// bump+diffuse+specular draw calls via our callback.
		RB_CreateSingleDrawInteractions( surf, VK_DrawInteractionCallback );
	}
}

// ---------------------------------------------------------------------------
// Shader pass rendering (ambient stages, GUI, decals, particles)
// ---------------------------------------------------------------------------

int idRenderBackendDrawVulkan::VK_DrawShaderPasses( const drawSurf_t **drawSurfs,
	int numDrawSurfs )
{
	if ( numDrawSurfs == 0 ) {
		return 0;
	}

	// Skip ambient passes in real 3D views when r_skipAmbient is set
	if ( currentViewDef->viewEntitys && r_skipAmbient.GetBool() ) {
		return numDrawSurfs;
	}

	backEnd.currentSpace = NULL;

	VkPipeline boundPipeline = VK_NULL_HANDLE;
	float currentMVP[16];

	// Push constant data: mat4 mvp + vec4 texMatrixS + vec4 texMatrixT
	struct ShaderPassPushConstants {
		float mvp[16];
		float texMatrixS[4];
		float texMatrixT[4];
	};

	// Identity texture matrix (no transform)
	static const float identityTexS[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
	static const float identityTexT[4] = { 0.0f, 1.0f, 0.0f, 0.0f };

	int i;
	for ( i = 0; i < numDrawSurfs; i++ ) {
		const drawSurf_t *surf = drawSurfs[i];
		const idMaterial *shader = surf->material;

		if ( !shader ) {
			continue;
		}

		// Skip subview-suppressed surfaces
		if ( shader->SuppressInSubview() ) {
			continue;
		}

		// Stop at post-process boundary
		if ( shader->GetSort() >= SS_POST_PROCESS ) {
			break;
		}

		// Skip if no ambient stages
		if ( !shader->HasAmbient() ) {
			continue;
		}

		// Skip portal sky
		if ( shader->IsPortalSky() ) {
			continue;
		}

		const srfTriangles_t *tri = surf->geo;
		if ( !tri || !tri->ambientCache || tri->numIndexes == 0 ) {
			continue;
		}

		const float *regs = surf->shaderRegisters;

		// Handle model space change — recompute MVP
		bool spaceChanged = ( surf->space != backEnd.currentSpace );
		if ( spaceChanged ) {
			backEnd.currentSpace = surf->space;
			VK_ComputeMVP( surf->space->modelViewMatrix,
				currentViewDef->projectionMatrix, currentMVP );

		}

		// Set scissor
		if ( !backEnd.currentScissor.Equals( surf->scissorRect ) ) {
			VK_SetScissor( currentCmdBuf, surf->scissorRect );
			backEnd.currentScissor = surf->scissorRect;
		}

		// Upload and bind vertex data (shared across all stages for this surface)
		vkBufferRef_t vertRef;
		if ( !VK_BindSurfaceVertices( tri, vertRef ) ) {
			continue;
		}
		vkCmdBindVertexBuffers( currentCmdBuf, 0, 1, &vertRef.buffer, &vertRef.offset );

		// Upload and bind index data
		vkBufferRef_t idxRef;
		if ( !VK_BindSurfaceIndices( tri, idxRef ) ) {
			continue;
		}
		vkCmdBindIndexBuffer( currentCmdBuf, idxRef.buffer, idxRef.offset,
			VK_INDEX_TYPE_UINT32 );

		int cullMode = shader->GetCullType();

		// Iterate material stages
		for ( int stage = 0; stage < shader->GetNumStages(); stage++ ) {
			const shaderStage_t *pStage = shader->GetStage( stage );

			// Filter: only draw ambient stages
			if ( pStage->lighting != SL_AMBIENT ) {
				continue;
			}

			// Filter: skip stages with failed condition register
			if ( regs[pStage->conditionRegister] == 0 ) {
				continue;
			}

			// Filter: skip (ZERO, ONE) blend (pure alpha mask, invisible)
			if ( ( pStage->drawStateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) )
				== ( GLS_SRCBLEND_ZERO | GLS_DSTBLEND_ONE ) ) {
				continue;
			}

			// Skip newStage (ARB2 custom vertex/fragment programs) for now
			if ( pStage->newStage ) {
				continue;
			}

			// Evaluate stage color from shader registers
			float color[4];
			color[0] = regs[pStage->color.registers[0]];
			color[1] = regs[pStage->color.registers[1]];
			color[2] = regs[pStage->color.registers[2]];
			color[3] = regs[pStage->color.registers[3]];

			// Skip degenerate additive stages (black + additive = invisible)
			int srcBits = pStage->drawStateBits & GLS_SRCBLEND_BITS;
			int dstBits = pStage->drawStateBits & GLS_DSTBLEND_BITS;
			if ( srcBits == GLS_SRCBLEND_ONE && dstBits == GLS_DSTBLEND_ONE ) {
				if ( color[0] <= 0 && color[1] <= 0 && color[2] <= 0 ) {
					continue;
				}
			}

			// Skip fully transparent alpha-blended stages
			if ( srcBits == GLS_SRCBLEND_SRC_ALPHA
				&& dstBits == GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA ) {
				if ( color[3] <= 0 ) {
					continue;
				}
			}

			// Get texture image
			idImage *image = pStage->texture.image;
			if ( !image ) {
				continue;
			}

			// Skip cinematics for now (requires full cinematic system)
			if ( pStage->texture.cinematic ) {
				continue;
			}

			// Skip non-explicit texgen for now (cube maps, screen-space, etc.)
			if ( pStage->texture.texgen != TG_EXPLICIT ) {
				continue;
			}

			// Ensure the image is loaded
			VK_EnsureImageLoaded( image );
			if ( image->vkHandle == 0 ) {
				continue;
			}

			const vulkanTexture_t *tex = vkTextureMgr.GetTexture( image->vkHandle );
			if ( !tex || !tex->allocated ) {
				continue;
			}

			// Get or create pipeline for this state + cull mode
			VkPipeline pipeline = vkPipelineMgr.GetShaderPassPipeline(
				pStage->drawStateBits, cullMode );
			if ( pipeline == VK_NULL_HANDLE ) {
				continue;
			}
			if ( pipeline != boundPipeline ) {
				vkCmdBindPipeline( currentCmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
				boundPipeline = pipeline;
			}

			// Push constants: MVP + texture matrix
			VkPipelineLayout layout = vkPipelineMgr.GetShaderPassLayout();

			ShaderPassPushConstants pc;
			memcpy( pc.mvp, currentMVP, sizeof( pc.mvp ) );

			if ( pStage->texture.hasMatrix ) {
				// Compute texture matrix rows from shader registers
				float s_scale = regs[pStage->texture.matrix[0][0]];
				float s_rot   = regs[pStage->texture.matrix[0][1]];
				float s_trans = regs[pStage->texture.matrix[0][2]];
				float t_rot   = regs[pStage->texture.matrix[1][0]];
				float t_scale = regs[pStage->texture.matrix[1][1]];
				float t_trans = regs[pStage->texture.matrix[1][2]];

				// Clamp large translations to prevent precision loss
				if ( s_trans < -40.0f || s_trans > 40.0f ) {
					s_trans -= (int)s_trans;
				}
				if ( t_trans < -40.0f || t_trans > 40.0f ) {
					t_trans -= (int)t_trans;
				}

				pc.texMatrixS[0] = s_scale;
				pc.texMatrixS[1] = s_rot;
				pc.texMatrixS[2] = 0.0f;
				pc.texMatrixS[3] = s_trans;
				pc.texMatrixT[0] = t_rot;
				pc.texMatrixT[1] = t_scale;
				pc.texMatrixT[2] = 0.0f;
				pc.texMatrixT[3] = t_trans;
			} else {
				memcpy( pc.texMatrixS, identityTexS, sizeof( pc.texMatrixS ) );
				memcpy( pc.texMatrixT, identityTexT, sizeof( pc.texMatrixT ) );
			}

			vkCmdPushConstants( currentCmdBuf, layout,
				VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof( pc ), &pc );

			// --- Upload FragParams UBO (set 0, binding 1) ---
			struct FragParams {
				float color[4];
				float alphaTestParams[4];	// x=ref, y=alphaMode, z=vertColorMode, w=0
			};

			FragParams fp;
			memcpy( fp.color, color, sizeof( fp.color ) );

			// Alpha test from drawStateBits
			int atestBits = pStage->drawStateBits & GLS_ATEST_BITS;
			if ( atestBits == GLS_ATEST_LT_128 ) {
				fp.alphaTestParams[0] = 0.5f;	// ref
				fp.alphaTestParams[1] = 1.0f;	// mode: LT
			} else if ( atestBits == GLS_ATEST_GE_128 ) {
				fp.alphaTestParams[0] = 0.5f;
				fp.alphaTestParams[1] = 2.0f;	// mode: GE
			} else if ( atestBits == GLS_ATEST_EQ_255 ) {
				fp.alphaTestParams[0] = 1.0f;
				fp.alphaTestParams[1] = 3.0f;	// mode: EQ
			} else {
				fp.alphaTestParams[0] = 0.0f;
				fp.alphaTestParams[1] = 0.0f;	// mode: none
			}

			// Vertex color mode (matches stageVertexColor_t enum values)
			fp.alphaTestParams[2] = (float)pStage->vertexColor;
			fp.alphaTestParams[3] = 0.0f;

			VkBuffer fpBuf;
			VkDeviceSize fpOff;
			void *fpMapped;
			if ( !allocator.AllocFrameTemp( sizeof(FragParams), 256,
				fpBuf, fpOff, &fpMapped ) ) {
				continue;
			}
			memcpy( fpMapped, &fp, sizeof(FragParams) );

			// Allocate + write UBO descriptor set (set 0)
			VkDescriptorSet uboSet = descriptorAlloc.AllocUBOSet();
			if ( uboSet == VK_NULL_HANDLE ) {
				continue;
			}

			// Write binding 0 (unused by gui shader, but must be valid) and binding 1 (FragParams)
			VkDescriptorBufferInfo bufInfos[2] = {};
			bufInfos[0].buffer = fpBuf;		// dummy — gui shader doesn't read binding 0
			bufInfos[0].offset = fpOff;
			bufInfos[0].range = sizeof(FragParams);
			bufInfos[1].buffer = fpBuf;
			bufInfos[1].offset = fpOff;
			bufInfos[1].range = sizeof(FragParams);

			VkWriteDescriptorSet uboWrites[2] = {};
			uboWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			uboWrites[0].dstSet = uboSet;
			uboWrites[0].dstBinding = 0;
			uboWrites[0].descriptorCount = 1;
			uboWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			uboWrites[0].pBufferInfo = &bufInfos[0];
			uboWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			uboWrites[1].dstSet = uboSet;
			uboWrites[1].dstBinding = 1;
			uboWrites[1].descriptorCount = 1;
			uboWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			uboWrites[1].pBufferInfo = &bufInfos[1];

			vkUpdateDescriptorSets( vkState.device, 2, uboWrites, 0, NULL );

			// Allocate + write sampler descriptor set (set 1)
			VkDescriptorSet samplerSet = descriptorAlloc.AllocSamplerSet();
			if ( samplerSet == VK_NULL_HANDLE ) {
				continue;
			}

			VkDescriptorImageInfo imageInfo = {};
			imageInfo.sampler = tex->sampler;
			imageInfo.imageView = tex->imageView;
			imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			VkWriteDescriptorSet samplerWrite = {};
			samplerWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			samplerWrite.dstSet = samplerSet;
			samplerWrite.dstBinding = 0;
			samplerWrite.descriptorCount = 1;
			samplerWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			samplerWrite.pImageInfo = &imageInfo;

			vkUpdateDescriptorSets( vkState.device, 1, &samplerWrite, 0, NULL );

			// Bind descriptor sets
			VkDescriptorSet sets[2] = { uboSet, samplerSet };
			vkCmdBindDescriptorSets( currentCmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
				layout, 0, 2, sets, 0, NULL );

			// Draw
			vkCmdDrawIndexed( currentCmdBuf, tri->numIndexes, 1, 0, 0, 0 );

			backEnd.pc.c_drawElements++;
			backEnd.pc.c_drawIndexes += tri->numIndexes;
		}
	}

	return i;
}

// ---------------------------------------------------------------------------
// Fog rendering
// ---------------------------------------------------------------------------

void idRenderBackendDrawVulkan::VK_FogAllLights() {
	extern idCVar r_skipFogLights;
	extern idCVar r_showOverDraw;

	if ( r_skipFogLights.GetBool() || r_showOverDraw.GetInteger() != 0
		|| currentViewDef->isXraySubview ) {
		return;
	}

	for ( viewLight_t *vLight = currentViewDef->viewLights; vLight; vLight = vLight->next ) {
		backEnd.vLight = vLight;

		const idMaterial *lightShader = vLight->lightShader;
		if ( !lightShader->IsFogLight() && !lightShader->IsBlendLight() ) {
			continue;
		}

		if ( lightShader->IsFogLight() ) {
			VK_FogPass( vLight->globalInteractions, vLight->localInteractions );
		}
		// TODO: blend lights (RB_BlendLight equivalent)
	}
}

void idRenderBackendDrawVulkan::VK_FogPass( const drawSurf_t *drawSurfs,
	const drawSurf_t *drawSurfs2 )
{
	const srfTriangles_t *frustumTris = backEnd.vLight->frustumTris;

	// If we ran out of vertex cache memory, skip
	if ( !frustumTris || !frustumTris->ambientCache ) {
		return;
	}

	// Get fog color and density from light shader
	const idMaterial *lightShader = backEnd.vLight->lightShader;
	const float *regs = backEnd.vLight->shaderRegisters;
	const shaderStage_t *stage = lightShader->GetStage( 0 );

	float fogColor[4];
	fogColor[0] = regs[stage->color.registers[0]];
	fogColor[1] = regs[stage->color.registers[1]];
	fogColor[2] = regs[stage->color.registers[2]];
	fogColor[3] = regs[stage->color.registers[3]];

	// Calculate falloff distance factor
	float a;
	if ( fogColor[3] <= 1.0f ) {
		a = -0.5f / DEFAULT_FOG_DISTANCE;
	} else {
		a = -0.5f / fogColor[3];
	}

	// Compute global fog planes (same as GL backend's RB_FogPass)
	idPlane fogPlanes[4];

	// Plane 0: view-Z depth scaled by fog density (for fogImage S)
	fogPlanes[0][0] = a * currentViewDef->worldSpace.modelViewMatrix[2];
	fogPlanes[0][1] = a * currentViewDef->worldSpace.modelViewMatrix[6];
	fogPlanes[0][2] = a * currentViewDef->worldSpace.modelViewMatrix[10];
	fogPlanes[0][3] = a * currentViewDef->worldSpace.modelViewMatrix[14];

	// Plane 1: unused globally (hardcoded to (0,0,0,0.5) per-surface)

	// Plane 2: fog terminator fade (for fogEnterImage T)
	fogPlanes[2][0] = 0.001f * backEnd.vLight->fogPlane[0];
	fogPlanes[2][1] = 0.001f * backEnd.vLight->fogPlane[1];
	fogPlanes[2][2] = 0.001f * backEnd.vLight->fogPlane[2];
	fogPlanes[2][3] = 0.001f * backEnd.vLight->fogPlane[3];

	// Plane 3: view-origin offset (for fogEnterImage S)
	float s = currentViewDef->renderView.vieworg * fogPlanes[2].Normal() + fogPlanes[2][3];
	fogPlanes[3][0] = 0;
	fogPlanes[3][1] = 0;
	fogPlanes[3][2] = 0;
	fogPlanes[3][3] = FOG_ENTER + s;

	// Get fog textures
	idImage *fogImg = globalImages->fogImage;
	idImage *fogEnterImg = globalImages->fogEnterImage;
	VK_EnsureImageLoaded( fogImg );
	VK_EnsureImageLoaded( fogEnterImg );
	if ( fogImg->vkHandle == 0 || fogEnterImg->vkHandle == 0 ) {
		return;
	}

	const vulkanTexture_t *fogTex = vkTextureMgr.GetTexture( fogImg->vkHandle );
	const vulkanTexture_t *fogEnterTex = vkTextureMgr.GetTexture( fogEnterImg->vkHandle );
	if ( !fogTex || !fogTex->allocated || !fogEnterTex || !fogEnterTex->allocated ) {
		return;
	}

	// --- Draw fog surfaces (depth equal, front-sided cull) ---
	VkPipeline fogPipeline = vkPipelineMgr.GetPipeline( VK_PIPELINE_FOG );
	VkPipelineLayout fogLayout = vkPipelineMgr.GetPipelineLayout( VK_PIPELINE_FOG );
	if ( fogPipeline == VK_NULL_HANDLE ) {
		return;
	}

	vkCmdBindPipeline( currentCmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, fogPipeline );

	VK_FogSurfaceChain( drawSurfs, fogPlanes, fogColor, fogTex, fogEnterTex, fogLayout );
	VK_FogSurfaceChain( drawSurfs2, fogPlanes, fogColor, fogTex, fogEnterTex, fogLayout );

	// --- Draw frustum caps (depth lequal, back-sided cull) ---
	VkPipeline fogCapsPipeline = vkPipelineMgr.GetPipeline( VK_PIPELINE_FOG_CAPS );
	VkPipelineLayout fogCapsLayout = vkPipelineMgr.GetPipelineLayout( VK_PIPELINE_FOG_CAPS );
	if ( fogCapsPipeline != VK_NULL_HANDLE ) {
		vkCmdBindPipeline( currentCmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, fogCapsPipeline );

		drawSurf_t ds;
		memset( &ds, 0, sizeof( ds ) );
		ds.space = &currentViewDef->worldSpace;
		ds.geo = frustumTris;
		ds.scissorRect = currentViewDef->scissor;

		VK_FogSurfaceChain( &ds, fogPlanes, fogColor, fogTex, fogEnterTex, fogCapsLayout );
	}
}

void idRenderBackendDrawVulkan::VK_FogSurfaceChain( const drawSurf_t *drawSurfs,
	const idPlane *fogPlanes, const float *fogColor,
	const vulkanTexture_t *fogTex, const vulkanTexture_t *fogEnterTex,
	VkPipelineLayout layout )
{
	struct FogParams {
		float fogPlane0[4];
		float fogPlane1[4];
		float fogPlane2[4];
		float fogPlane3[4];
		float color[4];
	};

	for ( const drawSurf_t *surf = drawSurfs; surf; surf = surf->nextOnLight ) {
		const srfTriangles_t *tri = surf->geo;

		if ( !tri || !tri->ambientCache || tri->numIndexes == 0 ) {
			continue;
		}

		// Set scissor
		if ( !backEnd.currentScissor.Equals( surf->scissorRect ) ) {
			VK_SetScissor( currentCmdBuf, surf->scissorRect );
			backEnd.currentScissor = surf->scissorRect;
		}

		// Compute local fog planes for this surface's model space
		idPlane localPlane;
		FogParams fp;

		// Fog plane 0: S for fogImage (distance falloff)
		R_GlobalPlaneToLocal( surf->space->modelMatrix, fogPlanes[0], localPlane );
		localPlane[3] += 0.5f;
		memcpy( fp.fogPlane0, localPlane.ToFloatPtr(), 16 );

		// Fog plane 1: T for fogImage (constant 0.5)
		fp.fogPlane1[0] = 0.0f;
		fp.fogPlane1[1] = 0.0f;
		fp.fogPlane1[2] = 0.0f;
		fp.fogPlane1[3] = 0.5f;

		// Fog plane 2: T for fogEnterImage (terminator fade)
		R_GlobalPlaneToLocal( surf->space->modelMatrix, fogPlanes[2], localPlane );
		localPlane[3] += FOG_ENTER;
		memcpy( fp.fogPlane2, localPlane.ToFloatPtr(), 16 );

		// Fog plane 3: S for fogEnterImage (view-origin offset)
		R_GlobalPlaneToLocal( surf->space->modelMatrix, fogPlanes[3], localPlane );
		memcpy( fp.fogPlane3, localPlane.ToFloatPtr(), 16 );

		// Fog color (RGB only, alpha unused)
		fp.color[0] = fogColor[0];
		fp.color[1] = fogColor[1];
		fp.color[2] = fogColor[2];
		fp.color[3] = 1.0f;

		// Upload UBO
		VkBuffer uboBuf;
		VkDeviceSize uboOff;
		void *uboMapped;
		if ( !allocator.AllocFrameTemp( sizeof( FogParams ), 256,
			uboBuf, uboOff, &uboMapped ) ) {
			continue;
		}
		memcpy( uboMapped, &fp, sizeof( FogParams ) );

		// Allocate + write UBO descriptor set (set 0)
		VkDescriptorSet uboSet = descriptorAlloc.AllocUBOSet();
		if ( uboSet == VK_NULL_HANDLE ) {
			continue;
		}

		VkDescriptorBufferInfo bufInfos[2] = {};
		bufInfos[0].buffer = uboBuf;
		bufInfos[0].offset = uboOff;
		bufInfos[0].range = sizeof( FogParams );
		bufInfos[1].buffer = uboBuf;		// dummy for binding 1
		bufInfos[1].offset = uboOff;
		bufInfos[1].range = sizeof( FogParams );

		VkWriteDescriptorSet uboWrites[2] = {};
		uboWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		uboWrites[0].dstSet = uboSet;
		uboWrites[0].dstBinding = 0;
		uboWrites[0].descriptorCount = 1;
		uboWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		uboWrites[0].pBufferInfo = &bufInfos[0];
		uboWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		uboWrites[1].dstSet = uboSet;
		uboWrites[1].dstBinding = 1;
		uboWrites[1].descriptorCount = 1;
		uboWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		uboWrites[1].pBufferInfo = &bufInfos[1];

		vkUpdateDescriptorSets( vkState.device, 2, uboWrites, 0, NULL );

		// Allocate + write sampler descriptor set (set 1)
		VkDescriptorSet samplerSet = descriptorAlloc.AllocSamplerSet();
		if ( samplerSet == VK_NULL_HANDLE ) {
			continue;
		}

		VkDescriptorImageInfo imageInfos[2] = {};
		imageInfos[0].sampler = fogTex->sampler;
		imageInfos[0].imageView = fogTex->imageView;
		imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageInfos[1].sampler = fogEnterTex->sampler;
		imageInfos[1].imageView = fogEnterTex->imageView;
		imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkWriteDescriptorSet samplerWrites[2] = {};
		for ( int j = 0; j < 2; j++ ) {
			samplerWrites[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			samplerWrites[j].dstSet = samplerSet;
			samplerWrites[j].dstBinding = j;
			samplerWrites[j].descriptorCount = 1;
			samplerWrites[j].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			samplerWrites[j].pImageInfo = &imageInfos[j];
		}

		vkUpdateDescriptorSets( vkState.device, 2, samplerWrites, 0, NULL );

		// Bind descriptor sets
		VkDescriptorSet sets[2] = { uboSet, samplerSet };
		vkCmdBindDescriptorSets( currentCmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
			layout, 0, 2, sets, 0, NULL );

		// Push MVP
		float mvp[16];
		VK_ComputeMVP( surf->space->modelViewMatrix,
			currentViewDef->projectionMatrix, mvp );
		vkCmdPushConstants( currentCmdBuf, layout,
			VK_SHADER_STAGE_VERTEX_BIT, 0, 64, mvp );

		// Upload and bind vertex data
		vkBufferRef_t vertRef;
		if ( !VK_BindSurfaceVertices( tri, vertRef ) ) {
			continue;
		}
		vkCmdBindVertexBuffers( currentCmdBuf, 0, 1, &vertRef.buffer, &vertRef.offset );

		// Upload and bind index data
		vkBufferRef_t idxRef;
		if ( !VK_BindSurfaceIndices( tri, idxRef ) ) {
			continue;
		}
		vkCmdBindIndexBuffer( currentCmdBuf, idxRef.buffer, idxRef.offset,
			VK_INDEX_TYPE_UINT32 );

		// Draw
		vkCmdDrawIndexed( currentCmdBuf, tri->numIndexes, 1, 0, 0, 0 );

		backEnd.pc.c_drawElements++;
		backEnd.pc.c_drawIndexes += tri->numIndexes;
	}
}

// ---------------------------------------------------------------------------
// Helper functions
// ---------------------------------------------------------------------------

void idRenderBackendDrawVulkan::VK_SetScissor( VkCommandBuffer cmd_buf,
	const idScreenRect &rect )
{
	VkRect2D scissor;
	scissor.offset.x = rect.x1;
	scissor.offset.y = rect.y1;
	scissor.extent.width = rect.x2 - rect.x1 + 1;
	scissor.extent.height = rect.y2 - rect.y1 + 1;

	// Clamp to valid ranges
	if ( scissor.offset.x < 0 ) scissor.offset.x = 0;
	if ( scissor.offset.y < 0 ) scissor.offset.y = 0;

	vkCmdSetScissor( cmd_buf, 0, 1, &scissor );
}

bool idRenderBackendDrawVulkan::VK_BindSurfaceVertices( const srfTriangles_t *tri,
	vkBufferRef_t &vertRef )
{
	// The vertex cache is in virtual memory mode when Vulkan is active.
	// Position() returns a real CPU pointer.
	void *vertData = vertexCache.Position( tri->ambientCache );
	if ( !vertData ) {
		return false;
	}

	int vertSize = tri->numVerts * sizeof( idDrawVert );
	return VK_UploadVertexData( allocator, vertData, vertSize, 16, vertRef );
}

bool idRenderBackendDrawVulkan::VK_BindSurfaceIndices( const srfTriangles_t *tri,
	vkBufferRef_t &idxRef )
{
	void *indexData;
	int indexSize;

	if ( tri->indexCache ) {
		indexData = vertexCache.Position( tri->indexCache );
		indexSize = tri->numIndexes * sizeof( glIndex_t );
	} else if ( tri->indexes ) {
		indexData = (void *)tri->indexes;
		indexSize = tri->numIndexes * sizeof( glIndex_t );
	} else {
		return false;
	}

	return VK_UploadVertexData( allocator, indexData, indexSize, 4, idxRef );
}

bool idRenderBackendDrawVulkan::VK_BindShadowVertices( const srfTriangles_t *tri,
	vkBufferRef_t &vertRef )
{
	void *vertData = vertexCache.Position( tri->shadowCache );
	if ( !vertData ) {
		return false;
	}

	// Shadow vertices: numVerts * sizeof(shadowCache_t) where shadowCache_t = idVec4 = 16 bytes
	int vertSize = tri->numVerts * sizeof( shadowCache_t );
	return VK_UploadVertexData( allocator, vertData, vertSize, 16, vertRef );
}

void idRenderBackendDrawVulkan::VK_ComputeMVP( const float *modelView,
	const float *projection, float *mvp )
{
	// Fix the projection matrix for Vulkan's coordinate conventions:
	// - OpenGL NDC Z range is [-1,1], Vulkan is [0,1]
	// - Remap: z_vk = 0.5 * z_gl + 0.5
	// This modifies row 2 (Z) of the column-major projection matrix:
	//   P_new[row2] = 0.5 * (P[row2] + P[row3])
	float proj[16];
	memcpy( proj, projection, sizeof( proj ) );

	// Column-major: row 2 is at indices [2],[6],[10],[14]; row 3 at [3],[7],[11],[15]
	proj[2]  = 0.5f * ( projection[2]  + projection[3] );
	proj[6]  = 0.5f * ( projection[6]  + projection[7] );
	proj[10] = 0.5f * ( projection[10] + projection[11] );
	proj[14] = 0.5f * ( projection[14] + projection[15] );

	// Column-major 4x4 matrix multiply: mvp = proj * modelView
	for ( int i = 0; i < 4; i++ ) {
		for ( int j = 0; j < 4; j++ ) {
			mvp[i * 4 + j] =
				proj[0 * 4 + j] * modelView[i * 4 + 0] +
				proj[1 * 4 + j] * modelView[i * 4 + 1] +
				proj[2 * 4 + j] * modelView[i * 4 + 2] +
				proj[3 * 4 + j] * modelView[i * 4 + 3];
		}
	}
}

// ---------------------------------------------------------------------------
// Screenshot readback
// ---------------------------------------------------------------------------

void idRenderBackendDrawVulkan::EnsureScreenshotBuffer( int width, int height ) {
	if ( screenshotBuffer != VK_NULL_HANDLE &&
		screenshotBufferWidth >= width && screenshotBufferHeight >= height ) {
		return;
	}

	// Free old buffer
	if ( screenshotBuffer != VK_NULL_HANDLE ) {
		vkDestroyBuffer( vkState.device, screenshotBuffer, NULL );
		screenshotBuffer = VK_NULL_HANDLE;
	}
	if ( screenshotMemory != VK_NULL_HANDLE ) {
		vkFreeMemory( vkState.device, screenshotMemory, NULL );
		screenshotMemory = VK_NULL_HANDLE;
	}

	// Swapchain format is B8G8R8A8 = 4 bytes per pixel
	VkDeviceSize bufSize = (VkDeviceSize)width * height * 4;

	VkBufferCreateInfo bufInfo = {};
	bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufInfo.size = bufSize;
	bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	if ( vkCreateBuffer( vkState.device, &bufInfo, NULL, &screenshotBuffer ) != VK_SUCCESS ) {
		common->Warning( "Failed to create screenshot staging buffer" );
		screenshotBuffer = VK_NULL_HANDLE;
		return;
	}

	VkMemoryRequirements memReqs;
	vkGetBufferMemoryRequirements( vkState.device, screenshotBuffer, &memReqs );

	VkPhysicalDeviceMemoryProperties memProps;
	vkGetPhysicalDeviceMemoryProperties( vkState.physicalDevice, &memProps );

	uint32_t memType = UINT32_MAX;
	VkMemoryPropertyFlags wantFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
		| VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	for ( uint32_t i = 0; i < memProps.memoryTypeCount; i++ ) {
		if ( ( memReqs.memoryTypeBits & ( 1 << i ) ) &&
			( memProps.memoryTypes[i].propertyFlags & wantFlags ) == wantFlags ) {
			memType = i;
			break;
		}
	}

	if ( memType == UINT32_MAX ) {
		common->Warning( "No host-visible memory for screenshot buffer" );
		vkDestroyBuffer( vkState.device, screenshotBuffer, NULL );
		screenshotBuffer = VK_NULL_HANDLE;
		return;
	}

	VkMemoryAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memReqs.size;
	allocInfo.memoryTypeIndex = memType;

	if ( vkAllocateMemory( vkState.device, &allocInfo, NULL, &screenshotMemory ) != VK_SUCCESS ) {
		common->Warning( "Failed to allocate screenshot buffer memory" );
		vkDestroyBuffer( vkState.device, screenshotBuffer, NULL );
		screenshotBuffer = VK_NULL_HANDLE;
		return;
	}

	vkBindBufferMemory( vkState.device, screenshotBuffer, screenshotMemory, 0 );

	screenshotBufferWidth = width;
	screenshotBufferHeight = height;
}

void idRenderBackendDrawVulkan::ReadPixels( int x, int y, int width, int height, byte *buffer ) {
	if ( !screenshotReady || screenshotBuffer == VK_NULL_HANDLE ) {
		memset( buffer, 0, width * height * 3 );
		return;
	}

	// Wait for the GPU to finish the copy command
	frameSync.WaitIdle();

	// Map the staging buffer
	void *mapped;
	VkDeviceSize bufSize = (VkDeviceSize)screenshotBufferWidth * screenshotBufferHeight * 4;
	if ( vkMapMemory( vkState.device, screenshotMemory, 0, bufSize, 0, &mapped ) != VK_SUCCESS ) {
		memset( buffer, 0, width * height * 3 );
		return;
	}

	const byte *src = (const byte *)mapped;
	int srcStride = screenshotBufferWidth * 4;

	// Convert BGRA → RGB and flip vertically (Vulkan is top-down, GL is bottom-up).
	// TakeScreenshot expects bottom-up data (it flips to get correct orientation).
	for ( int row = 0; row < height; row++ ) {
		// Vulkan row 0 is top → output as last row (bottom-up)
		int srcRow = y + row;
		int dstRow = height - 1 - row;
		const byte *srcLine = src + srcRow * srcStride + x * 4;
		byte *dstLine = buffer + dstRow * width * 3;

		for ( int col = 0; col < width; col++ ) {
			dstLine[col * 3 + 0] = srcLine[col * 4 + 2];	// R ← B
			dstLine[col * 3 + 1] = srcLine[col * 4 + 1];	// G ← G
			dstLine[col * 3 + 2] = srcLine[col * 4 + 0];	// B ← R
		}
	}

	vkUnmapMemory( vkState.device, screenshotMemory );
}

// ---------------------------------------------------------------------------
// Singleton instance
// ---------------------------------------------------------------------------

static idRenderBackendDrawVulkan s_vkBackendDraw;

idRenderBackendDraw * R_GetVulkanBackendDraw() {
	return &s_vkBackendDraw;
}

#endif // ID_VULKAN
