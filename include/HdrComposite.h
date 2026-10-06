#pragma once

// ============================================================================================================
// THE MENU ON AN HDR SCREEN (Witcher 3 1.0.2; found 2026-10-06 in Item Explorer's item card: AMF's own green toggle
// IM_COL32(76,175,80) reached the owner's HDR screen as pure (0,255,0), every icon garish). The game's swap chain is then
// R10G10B10A2 in the HDR10 colour space (ST.2084 "PQ", BT.2020 primaries) - or FP16 scRGB - and values written for an
// ordinary screen mean something else there.
//
// So when the back buffer is not an 8-bit format, ImGui draws into an 8-bit intermediate of the screen's size, and one
// full-screen pass puts it on the back buffer:
//   SDR    as it is (the 10-bit screen in the ordinary colour space);
//   PQ     sRGB -> linear -> BT.709 to BT.2020 -> x paper white (the game's [Visuals] HdrPaperWhite, nits) / 10000 -> PQ;
//   scRGB  sRGB -> linear -> x paper white / 80 (scRGB's 1.0 is 80 nits), BT.709 primaries.
// The intermediate holds premultiplied colour (ImGui's blending over a transparent clear), so the pass un-premultiplies,
// converts, and blends the result over the game's picture.
//
// Written against the device alone so tools/hdr_test.cpp runs the same pass offline and checks the numbers.
// ============================================================================================================

#include <functional>

#include <d3d12.h>
#include <dxgi1_6.h>

namespace hdr
{
	enum class Mode : std::int32_t
	{
		kSdr = 0,
		kPQ = 1,
		kScRgb = 2,
	};

	// The 8-bit format ImGui should draw in for a back buffer format: the back buffer's own when it is 8-bit RGBA/BGRA
	// (drawn straight in, as before), else R8G8B8A8_UNORM (drawn into the intermediate, then composited).
	DXGI_FORMAT UiFormat(DXGI_FORMAT a_backBuffer);
	inline bool NeedsComposite(DXGI_FORMAT a_backBuffer) { return UiFormat(a_backBuffer) != a_backBuffer; }

	// The mode for this back buffer: a_setting 0 auto / 1 SDR / 2 HDR; a_colorSpace the one the game set with
	// SetColorSpace1 (-1 = never seen); a_gameHdr the game's own HDR switch (-1 = unknown).
	Mode Decide(DXGI_FORMAT a_backBuffer, int a_setting, int a_colorSpace, int a_gameHdr);
	const char* ModeName(Mode a_mode);

	// Builds the pass for a back buffer format (again when it changes). a_allocSrv gives a slot in the shader-visible
	// heap ImGui draws with.
	bool Init(ID3D12Device* a_device, DXGI_FORMAT a_backBuffer,
		const std::function<bool(D3D12_CPU_DESCRIPTOR_HANDLE&, D3D12_GPU_DESCRIPTOR_HANDLE&)>& a_allocSrv);

	// The intermediate at the screen's size, made (again) when needed; the caller waits for the GPU first on a resize.
	bool EnsureTarget(UINT a_width, UINT a_height);

	// Frame: clear the intermediate and make it the render target (ImGui draws next) ...
	void BeginUi(ID3D12GraphicsCommandList* a_list);
	// ... then put it on the back buffer (a_backRtv, already in RENDER_TARGET state).
	void Composite(ID3D12GraphicsCommandList* a_list, D3D12_CPU_DESCRIPTOR_HANDLE a_backRtv, ID3D12DescriptorHeap* a_srvHeap, Mode a_mode,
		float a_paperWhiteNits);

	// Tells the caller it must wait for the GPU before EnsureTarget releases the old intermediate.
	bool TargetSizeDiffers(UINT a_width, UINT a_height);
}
