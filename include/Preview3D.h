#pragma once

// ============================================================================================================
// THE 3D PREVIEW (Witcher 3 1.0.5+, for Item Explorer's item card - the owner, 2026-10-06: "a simple window that pops
// up over AMF with a frame. Displays the item's name and its appearance with rotation on controller and keyboard and
// mouse"). A consumer hands one model (sdk/include/AMFPreview.h); the framework uploads it, and every frame the page
// asks for it, draws it into a texture of its own - before ImGui, in the same command list - lit, normal-mapped, with a
// soft specular, on a clear background, from the camera the page asks for. The page shows that texture with
// ImGui::Image. The game's renderer is not involved: nothing here can stall or hang the game's frame.
//
// It is written against a small Host (the device, the queue, the shader-visible descriptor heap and its slots, a
// synchronous upload) so tools/preview_test.cpp runs the very same code on a device of its own and writes PNGs.
// ============================================================================================================

#include <functional>

#include <d3d12.h>

#include "../sdk/include/AMFPreview.h"

namespace preview3d
{
	struct Host
	{
		ID3D12Device*         device = nullptr;
		ID3D12DescriptorHeap* srvHeap = nullptr;   // shader-visible; the one ImGui draws with
		// a free slot in srvHeap (false when full)
		std::function<bool(D3D12_CPU_DESCRIPTOR_HANDLE&, D3D12_GPU_DESCRIPTOR_HANDLE&)> allocSrv;
		std::function<void(std::uint64_t a_gpuPtr)>                                     freeSrv;
		// record copies into a list, run it and wait for it
		std::function<bool(const std::function<void(ID3D12GraphicsCommandList*)>&)> upload;
		// wait until the GPU has finished every frame in flight (before a resource is released)
		std::function<void()> waitIdle;
	};

	// Once the host is up. False when the shaders or the pipeline could not be made (logged); every other call then
	// returns null / does nothing.
	bool Init(const Host& a_host);
	bool Ready();

	// Any thread. Uploads the model (copied - the caller's memory may go). Null on failure (logged).
	void* Create(const AMF_PreviewMesh* a_mesh);

	// Render thread, while the page draws: draw a_handle at a_width x a_height (display pixels) from a_view this
	// frame. Returns the ImTextureID to show, or null.
	void* Request(void* a_handle, int a_width, int a_height, const AMF_PreviewView* a_view);

	// The overlay's frame: record every requested draw into a_list (before ImGui), leaving each target readable.
	void Record(ID3D12GraphicsCommandList* a_list);

	// Any thread. Waits for the GPU, then frees everything the model holds.
	void Release(void* a_handle);

	// tools/preview_test.cpp: the model's colour target (an ID3D12Resource*), to read it back.
	void* DebugTarget(void* a_handle);
}
