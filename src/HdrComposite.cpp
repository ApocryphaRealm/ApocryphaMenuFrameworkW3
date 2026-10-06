// The menu on an HDR screen - see include/HdrComposite.h.
#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>

#include "HdrComposite.h"

#include "Logger.h"

#include <d3dcompiler.h>

#include <cstring>

namespace hdr
{
	namespace
	{
		const char* kShader = R"(
struct Constants { uint mode; float paperWhite; float2 pad; };
ConstantBuffer<Constants> c : register(b0);
Texture2D gUi : register(t0);

float4 VSMain(uint id : SV_VertexID) : SV_Position
{
	float2 uv = float2((id << 1) & 2, id & 2);
	return float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float3 SrgbToLinear(float3 v)
{
	return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
}

float3 Pq(float3 nits)
{
	const float m1 = 0.1593017578125, m2 = 78.84375, c1 = 0.8359375, c2 = 18.8515625, c3 = 18.6875;
	float3 y = pow(saturate(nits / 10000.0), m1);
	return pow((c1 + c2 * y) / (1.0 + c3 * y), m2);
}

float4 PSMain(float4 pos : SV_Position) : SV_Target
{
	float4 ui = gUi.Load(int3(pos.xy, 0));   // premultiplied (ImGui blended over a transparent clear)
	if (ui.a <= 0.0) discard;
	float3 col = saturate(ui.rgb / ui.a);
	if (c.mode == 1)
	{
		const float3x3 toBt2020 = float3x3(0.6274040, 0.3292820, 0.0433136,
		                                   0.0690970, 0.9195400, 0.0113612,
		                                   0.0163916, 0.0880132, 0.8955950);
		col = Pq(mul(toBt2020, SrgbToLinear(col)) * c.paperWhite);
	}
	else if (c.mode == 2)
	{
		col = SrgbToLinear(col) * (c.paperWhite / 80.0);
	}
	return float4(col * ui.a, ui.a);   // blended ONE / INV_SRC_ALPHA over the game's picture
}
)";

		ID3D12Device*               g_device = nullptr;
		ID3D12RootSignature*        g_root = nullptr;
		ID3D12PipelineState*        g_pso = nullptr;
		DXGI_FORMAT                 g_backFormat = DXGI_FORMAT_UNKNOWN;
		ID3D12Resource*             g_target = nullptr;
		ID3D12DescriptorHeap*       g_rtvHeap = nullptr;
		D3D12_CPU_DESCRIPTOR_HANDLE g_srvCpu{};
		D3D12_GPU_DESCRIPTOR_HANDLE g_srvGpu{};
		bool                        g_hasSrv = false;
		UINT                        g_w = 0, g_h = 0;
		std::function<bool(D3D12_CPU_DESCRIPTOR_HANDLE&, D3D12_GPU_DESCRIPTOR_HANDLE&)> g_allocSrv;

		template <class T>
		void Rel(T*& p)
		{
			if (p) {
				p->Release();
				p = nullptr;
			}
		}

		void Barrier(ID3D12GraphicsCommandList* a_list, ID3D12Resource* a_res, D3D12_RESOURCE_STATES a_from, D3D12_RESOURCE_STATES a_to)
		{
			D3D12_RESOURCE_BARRIER b{};
			b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			b.Transition.pResource = a_res;
			b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			b.Transition.StateBefore = a_from;
			b.Transition.StateAfter = a_to;
			a_list->ResourceBarrier(1, &b);
		}

		ID3DBlob* Compile(const char* a_entry, const char* a_target)
		{
			ID3DBlob* code = nullptr;
			ID3DBlob* err = nullptr;
			if (FAILED(D3DCompile(kShader, std::strlen(kShader), "AMFHdr", nullptr, nullptr, a_entry, a_target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
					&code, &err))) {
				logger::error("hdr: shader {} did not compile: {}", a_entry, err ? static_cast<const char*>(err->GetBufferPointer()) : "?");
				Rel(err);
				Rel(code);
				return nullptr;
			}
			Rel(err);
			return code;
		}
	}

	DXGI_FORMAT UiFormat(DXGI_FORMAT a_back)
	{
		switch (a_back) {
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8A8_UNORM:
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
			return a_back;
		default:
			return DXGI_FORMAT_R8G8B8A8_UNORM;
		}
	}

	Mode Decide(DXGI_FORMAT a_back, int a_setting, int a_colorSpace, int a_gameHdr)
	{
		const bool fp16 = a_back == DXGI_FORMAT_R16G16B16A16_FLOAT;
		const Mode hdrMode = fp16 ? Mode::kScRgb : Mode::kPQ;
		if (!NeedsComposite(a_back) || a_setting == 1) {
			return Mode::kSdr;
		}
		if (a_setting == 2) {
			return hdrMode;
		}
		if (a_colorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020) return Mode::kPQ;
		if (a_colorSpace == DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709) return Mode::kScRgb;
		if (a_colorSpace == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709) return Mode::kSdr;
		return a_gameHdr == 1 ? hdrMode : Mode::kSdr;   // the colour space was never seen: the game's own switch decides
	}

	const char* ModeName(Mode a_mode)
	{
		switch (a_mode) {
		case Mode::kPQ: return "HDR10 (PQ, BT.2020)";
		case Mode::kScRgb: return "scRGB (FP16)";
		default: return "SDR";
		}
	}

	bool Init(ID3D12Device* a_device, DXGI_FORMAT a_back,
		const std::function<bool(D3D12_CPU_DESCRIPTOR_HANDLE&, D3D12_GPU_DESCRIPTOR_HANDLE&)>& a_allocSrv)
	{
		if (g_pso && a_device == g_device && a_back == g_backFormat) {
			return true;
		}
		g_device = a_device;
		g_allocSrv = a_allocSrv;
		Rel(g_pso);
		if (!g_root) {
			D3D12_DESCRIPTOR_RANGE range{};
			range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
			range.NumDescriptors = 1;
			D3D12_ROOT_PARAMETER params[2]{};
			params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
			params[0].Constants.Num32BitValues = 4;
			params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
			params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			params[1].DescriptorTable.NumDescriptorRanges = 1;
			params[1].DescriptorTable.pDescriptorRanges = &range;
			params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
			D3D12_ROOT_SIGNATURE_DESC rs{};
			rs.NumParameters = 2;
			rs.pParameters = params;
			ID3DBlob* blob = nullptr;
			ID3DBlob* err = nullptr;
			const bool ok = SUCCEEDED(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err)) &&
			                SUCCEEDED(g_device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&g_root)));
			Rel(blob);
			Rel(err);
			if (!ok) {
				logger::error("hdr: the root signature failed");
				return false;
			}
		}
		ID3DBlob* vs = Compile("VSMain", "vs_5_1");
		ID3DBlob* ps = Compile("PSMain", "ps_5_1");
		if (!vs || !ps) {
			Rel(vs);
			Rel(ps);
			return false;
		}
		D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
		pd.pRootSignature = g_root;
		pd.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
		pd.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
		pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		pd.NumRenderTargets = 1;
		pd.RTVFormats[0] = a_back;
		pd.SampleDesc.Count = 1;
		pd.SampleMask = UINT_MAX;
		pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		auto& bl = pd.BlendState.RenderTarget[0];
		bl.BlendEnable = TRUE;
		bl.SrcBlend = D3D12_BLEND_ONE;
		bl.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		bl.BlendOp = D3D12_BLEND_OP_ADD;
		bl.SrcBlendAlpha = D3D12_BLEND_ONE;
		bl.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
		bl.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		bl.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		const HRESULT hr = g_device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&g_pso));
		Rel(vs);
		Rel(ps);
		if (FAILED(hr)) {
			logger::error("hdr: the pipeline for back buffer format {} failed (0x{:08X})", static_cast<int>(a_back), static_cast<unsigned>(hr));
			return false;
		}
		g_backFormat = a_back;
		logger::info("hdr: the menu composite is ready for back buffer format {}", static_cast<int>(a_back));
		return true;
	}

	void* DebugTarget() { return g_target; }   // tools/hdr_test.cpp

	bool TargetSizeDiffers(UINT a_width, UINT a_height) { return g_target && (g_w != a_width || g_h != a_height); }

	bool EnsureTarget(UINT a_width, UINT a_height)
	{
		if (!g_device || a_width == 0 || a_height == 0) {
			return false;
		}
		if (g_target && g_w == a_width && g_h == a_height) {
			return true;
		}
		Rel(g_target);
		if (!g_rtvHeap) {
			D3D12_DESCRIPTOR_HEAP_DESC rd{ D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0 };
			if (FAILED(g_device->CreateDescriptorHeap(&rd, IID_PPV_ARGS(&g_rtvHeap)))) {
				return false;
			}
		}
		if (!g_hasSrv) {
			if (!g_allocSrv || !g_allocSrv(g_srvCpu, g_srvGpu)) {
				logger::error("hdr: no descriptor left for the menu's intermediate");
				return false;
			}
			g_hasSrv = true;
		}
		D3D12_HEAP_PROPERTIES hp{};
		hp.Type = D3D12_HEAP_TYPE_DEFAULT;
		D3D12_RESOURCE_DESC rd{};
		rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		rd.Width = a_width;
		rd.Height = a_height;
		rd.DepthOrArraySize = 1;
		rd.MipLevels = 1;
		rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		rd.SampleDesc.Count = 1;
		rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		D3D12_CLEAR_VALUE cv{};
		cv.Format = rd.Format;
		if (FAILED(g_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &cv,
				IID_PPV_ARGS(&g_target)))) {
			logger::error("hdr: the {}x{} intermediate could not be created", a_width, a_height);
			return false;
		}
		g_device->CreateRenderTargetView(g_target, nullptr, g_rtvHeap->GetCPUDescriptorHandleForHeapStart());
		D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
		sv.Format = rd.Format;
		sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		sv.Texture2D.MipLevels = 1;
		g_device->CreateShaderResourceView(g_target, &sv, g_srvCpu);
		g_w = a_width;
		g_h = a_height;
		logger::info("hdr: menu intermediate {}x{}", a_width, a_height);
		return true;
	}

	void BeginUi(ID3D12GraphicsCommandList* a_list)
	{
		Barrier(a_list, g_target, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
		const D3D12_CPU_DESCRIPTOR_HANDLE rtv = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
		const float clear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		a_list->ClearRenderTargetView(rtv, clear, 0, nullptr);
		a_list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
	}

	void Composite(ID3D12GraphicsCommandList* a_list, D3D12_CPU_DESCRIPTOR_HANDLE a_backRtv, ID3D12DescriptorHeap* a_srvHeap, Mode a_mode,
		float a_paperWhite)
	{
		Barrier(a_list, g_target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		a_list->OMSetRenderTargets(1, &a_backRtv, FALSE, nullptr);
		const D3D12_VIEWPORT vp{ 0.0f, 0.0f, static_cast<float>(g_w), static_cast<float>(g_h), 0.0f, 1.0f };
		const D3D12_RECT     sc{ 0, 0, static_cast<LONG>(g_w), static_cast<LONG>(g_h) };
		a_list->RSSetViewports(1, &vp);
		a_list->RSSetScissorRects(1, &sc);
		a_list->SetDescriptorHeaps(1, &a_srvHeap);
		a_list->SetGraphicsRootSignature(g_root);
		a_list->SetPipelineState(g_pso);
		struct { std::uint32_t mode; float paperWhite; float pad[2]; } k{ static_cast<std::uint32_t>(a_mode), a_paperWhite, { 0, 0 } };
		a_list->SetGraphicsRoot32BitConstants(0, 4, &k, 0);
		a_list->SetGraphicsRootDescriptorTable(1, g_srvGpu);
		a_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		a_list->DrawInstanced(3, 1, 0, 0);
	}
}
