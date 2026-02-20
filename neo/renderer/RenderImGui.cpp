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

#ifndef IMGUI_DISABLE

#include "../libs/imgui/imgui.h"
#include "../libs/imgui/backends/imgui_impl_opengl2.h"

#include "renderer/qgl.h"
#include "renderer/tr_local.h"
#include "renderer/RenderImGui.h"

// ============================================================================
// OpenGL2 ImGui backend
// ============================================================================

class idRenderImGuiBackendGL2 : public idRenderImGuiBackend {
public:
	bool Init() override {
		return ImGui_ImplOpenGL2_Init();
	}

	void Shutdown() override {
		ImGui_ImplOpenGL2_Shutdown();
	}

	void NewFrame() override {
		ImGui_ImplOpenGL2_NewFrame();
	}

	void RenderDrawData() override {
		// Doom3 uses the OpenGL ARB shader extensions, for most things it renders.
		// disable those shaders, the OpenGL classic integration of ImGui doesn't use shaders
		qglDisable( GL_VERTEX_PROGRAM_ARB );
		qglDisable( GL_FRAGMENT_PROGRAM_ARB );

		// Doom3 uses OpenGL's ARB_vertex_buffer_object extension to use VBOs on the GPU
		// as buffers for glDrawElements() (instead of passing userspace buffers to that function)
		// ImGui however uses userspace buffers, so remember the currently bound VBO
		// and unbind it (after drawing, bind it again)
		GLint curArrayBuffer = 0;
		if ( glConfig.ARBVertexBufferObjectAvailable ) {
			qglGetIntegerv( GL_ARRAY_BUFFER_BINDING_ARB, &curArrayBuffer );
			qglBindBufferARB( GL_ARRAY_BUFFER_ARB, 0 );
		}

		// disable all texture units, ImGui_ImplOpenGL2_RenderDrawData() will enable texture 0
		// and bind its own textures to it as needed
		for ( int i = glConfig.maxTextureUnits - 1 ; i >= 0 ; i-- ) {
			GL_SelectTexture( i );
			qglDisable( GL_TEXTURE_2D );
			if ( glConfig.texture3DAvailable ) {
				qglDisable( GL_TEXTURE_3D );
			}
			if ( glConfig.cubeMapAvailable ) {
				qglDisable( GL_TEXTURE_CUBE_MAP_EXT );
			}
		}

		ImGui_ImplOpenGL2_RenderDrawData( ImGui::GetDrawData() );

		if ( curArrayBuffer != 0 ) {
			qglBindBufferARB( GL_ARRAY_BUFFER_ARB, curArrayBuffer );
		}
	}
};

// ============================================================================
// Vulkan ImGui backend
// ============================================================================

#ifdef ID_VULKAN

#include "../libs/imgui/backends/imgui_impl_vulkan.h"
#include "renderer/vk/VulkanState.h"

static void VkImGui_CheckResult( VkResult err ) {
	if ( err != VK_SUCCESS ) {
		common->Warning( "ImGui Vulkan: VkResult = %d", (int)err );
	}
}

class idRenderImGuiBackendVulkan : public idRenderImGuiBackend {
public:
	idRenderImGuiBackendVulkan() : vkInitialized( false ) {}
	~idRenderImGuiBackendVulkan() override { Shutdown(); }

	bool Init() override {
		if ( vkState.device == VK_NULL_HANDLE || vkState.mainRenderPass == VK_NULL_HANDLE ) {
			common->Warning( "ImGui Vulkan: device or render pass not ready" );
			return false;
		}

		ImGui_ImplVulkan_InitInfo initInfo = {};
		initInfo.ApiVersion = VK_API_VERSION_1_0;
		initInfo.Instance = vkState.instance;
		initInfo.PhysicalDevice = vkState.physicalDevice;
		initInfo.Device = vkState.device;
		initInfo.QueueFamily = vkState.graphicsQueueFamily;
		initInfo.Queue = vkState.graphicsQueue;
		initInfo.DescriptorPoolSize = 16;  // let ImGui create its own pool
		initInfo.MinImageCount = 2;
		initInfo.ImageCount = (uint32_t)vkState.swapchainImages.size();
		if ( initInfo.ImageCount < 2 ) {
			initInfo.ImageCount = 2;
		}
		initInfo.PipelineInfoMain.RenderPass = vkState.mainRenderPass;
		initInfo.PipelineInfoMain.Subpass = 0;
		initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
		initInfo.CheckVkResultFn = VkImGui_CheckResult;
		initInfo.MinAllocationSize = 1024 * 1024;

		if ( !ImGui_ImplVulkan_Init( &initInfo ) ) {
			common->Warning( "ImGui Vulkan: ImGui_ImplVulkan_Init failed" );
			return false;
		}

		vkInitialized = true;
		common->Printf( "ImGui Vulkan backend initialized\n" );
		return true;
	}

	void Shutdown() override {
		if ( vkInitialized ) {
			if ( vkState.device != VK_NULL_HANDLE ) {
				vkDeviceWaitIdle( vkState.device );
			}
			ImGui_ImplVulkan_Shutdown();
			vkInitialized = false;
			common->Printf( "ImGui Vulkan backend shut down\n" );
		}
	}

	void NewFrame() override {
		if ( vkInitialized ) {
			ImGui_ImplVulkan_NewFrame();
		}
	}

	void RenderDrawData() override {
		if ( !vkInitialized ) {
			return;
		}

		VkCommandBuffer cmdBuf = vkState.activeCommandBuffer;
		if ( cmdBuf == VK_NULL_HANDLE ) {
			return;
		}

		ImDrawData *drawData = ImGui::GetDrawData();
		if ( drawData != NULL ) {
			ImGui_ImplVulkan_RenderDrawData( drawData, cmdBuf );
		}
	}

private:
	bool vkInitialized;
};

#endif // ID_VULKAN

// ============================================================================
// Factory function
// ============================================================================

idRenderImGuiBackend* CreateRenderImGuiBackend() {
#ifdef ID_VULKAN
	extern idCVar r_renderBackend;
	if ( idStr::Icmp( r_renderBackend.GetString(), "vulkan" ) == 0 ) {
		return new idRenderImGuiBackendVulkan();
	}
#endif
	return new idRenderImGuiBackendGL2();
}

#else // IMGUI_DISABLE

#include "renderer/RenderImGui.h"

idRenderImGuiBackend* CreateRenderImGuiBackend() {
	return NULL;
}

#endif // !IMGUI_DISABLE
