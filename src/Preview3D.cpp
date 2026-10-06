// The 3D preview - see include/Preview3D.h.
#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>

#include "Preview3D.h"

#include "Logger.h"

#include <DirectXMath.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace preview3d
{
	namespace
	{
		using namespace DirectX;

		// Rendered at twice the asked size; ImGui's bilinear sampling at half size then averages each 2x2 - smooth edges
		// without MSAA resolve work.
		constexpr int kSuper = 2;
		constexpr int kMaxSide = 2048;

		const char* kShader = R"(
struct Constants
{
	row_major float4x4 viewProj;
	float4 eye;          // xyz
	float4 keyDir;       // xyz: towards the key light
	float4 specular;     // rgb tint, a strength
	float4 tint;
	float4 shift1;       // hue (deg), saturation, luminance (-100..100)
	float4 shift2;
	uint4  flags;        // x: draw flags, y: has diffuse, z: has normal, w: has a dye mask texture
};
ConstantBuffer<Constants> c : register(b0);
Texture2D    gDiffuse : register(t0);
Texture2D    gNormal  : register(t1);
Texture2D    gMask    : register(t2);   // the dye mask (its green channel)
SamplerState gSampler : register(s0);

struct VIn  { float3 pos : POSITION; float2 uv : TEXCOORD; float4 n : NORMAL; float4 t : TANGENT; };
struct VOut { float4 pos : SV_Position; float3 world : WORLDPOS; float2 uv : TEXCOORD; float3 n : NORMAL; float4 t : TANGENT; };

VOut VSMain(VIn v)
{
	VOut o;
	o.pos = mul(float4(v.pos, 1.0), c.viewProj);
	o.world = v.pos;
	o.uv = v.uv;
	o.n = v.n.xyz * 2.0 - 1.0;
	o.t = float4(v.t.xyz * 2.0 - 1.0, v.t.w > 0.5 ? 1.0 : -1.0);
	return o;
}

float3 RgbToHsl(float3 c)
{
	float mx = max(c.r, max(c.g, c.b)), mn = min(c.r, min(c.g, c.b));
	float l = (mx + mn) * 0.5, d = mx - mn, h = 0.0, s = 0.0;
	if (d > 1e-5)
	{
		s = l > 0.5 ? d / (2.0 - mx - mn) : d / (mx + mn);
		if (mx == c.r)      h = (c.g - c.b) / d + (c.g < c.b ? 6.0 : 0.0);
		else if (mx == c.g) h = (c.b - c.r) / d + 2.0;
		else                h = (c.r - c.g) / d + 4.0;
		h /= 6.0;
	}
	return float3(h, s, l);
}
float HueToRgb(float p, float q, float t)
{
	t = frac(t);
	if (t < 1.0 / 6.0) return p + (q - p) * 6.0 * t;
	if (t < 0.5) return q;
	if (t < 2.0 / 3.0) return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
	return p;
}
float3 HslToRgb(float3 hsl)
{
	if (hsl.y <= 0.0) return hsl.zzz;
	float q = hsl.z < 0.5 ? hsl.z * (1.0 + hsl.y) : hsl.z + hsl.y - hsl.z * hsl.y;
	float p = 2.0 * hsl.z - q;
	return float3(HueToRgb(p, q, hsl.x + 1.0 / 3.0), HueToRgb(p, q, hsl.x), HueToRgb(p, q, hsl.x - 1.0 / 3.0));
}
// DYE (AMF_PREVIEW_DYE): REDengine's colour shift, read from REDkit's own material graphs and shader generator (Item
// Explorer's model-reader.md section 4). A texel in the red zone (r >= b) takes shift 1; one in the blue zone takes shift
// 2 on its red/blue-swapped colour. M = L * B * H * A * S (luminance, CDPR's colour basis, hue rotation, its inverse,
// saturation about the average), worked in gamma space. The dye mask (green channel) says where; KeepGray leaves grey,
// black and white as they are. colorShift2.w: >= 0 KeepGray on and the constant mask, < 0 off and mask = -1 - value.
static const float3x3 CS_B = {  0.8165, -0.4082,  0.4082,
                                0.0,     0.7071,  0.7071,
                               -0.8539, -0.7405,  0.1377 };
static const float3x3 CS_A = {  0.6210, -0.2461, -0.5774,
                               -0.6038,  0.4610, -0.5774,
                                0.6038,  0.9532,  0.5774 };
float3x3 ColorShiftMatrix(float hueDeg, float saturation, float luminance)
{
	float s = 1.0 + saturation * 0.01;
	float l = 1.0 + luminance * 0.01;
	float a = (1.0 - s) / 3.0;
	float3x3 S = { s + a, a, a,   a, s + a, a,   a, a, s + a };
	float sn, cs;
	sincos(radians(hueDeg), sn, cs);
	float3x3 H = { cs, -sn, 0,   sn, cs, 0,   0, 0, 1 };
	return l * mul(mul(mul(CS_B, H), CS_A), S);
}
float3 Dye(float3 linearRgb, float2 uv)
{
	float3 col = pow(saturate(linearRgb), 1.0 / 2.2);
	bool   keepGray = c.shift2.w >= 0.0;
	float  constMask = keepGray ? c.shift2.w : -1.0 - c.shift2.w;
	float  mask = c.flags.w ? gMask.Sample(gSampler, uv).g : constMask;
	float3x3 M1 = ColorShiftMatrix(c.shift1.x, c.shift1.y, c.shift1.z);
	float3x3 M2 = ColorShiftMatrix(c.shift2.x, c.shift2.y, c.shift2.z);
	float3 shifted = (col.r >= col.b) ? mul(col, M1) : mul(col.bgr, M2);
	float  mx = max(col.r, max(col.g, col.b));
	float  mn = min(col.r, min(col.g, col.b));
	float  sat = (mx - mn) / max(mx, 1e-5);
	float  k = keepGray ? mask * saturate((sat - 0.1) / 0.2) : mask;
	return pow(saturate(lerp(col, shifted, k)), 2.2);
}

float4 PSMain(VOut i, bool front : SV_IsFrontFace) : SV_Target
{
	float4 base = c.flags.y ? gDiffuse.Sample(gSampler, i.uv) : float4(0.55, 0.55, 0.55, 1.0);
	if ((c.flags.x & 2u) && base.a < 0.5) discard;
	if (c.flags.x & 4u) base.rgb = Dye(base.rgb, i.uv);   // dye on the stored colour, before any tint
	base.rgb *= c.tint.rgb;

	float3 n = normalize(i.n);
	float  gloss = 0.35;
	if (c.flags.z)
	{
		float4 nm = gNormal.Sample(gSampler, i.uv);
		float3 tn = nm.xyz * 2.0 - 1.0;
		float3 t = normalize(i.t.xyz - n * dot(n, i.t.xyz));
		float3 b = cross(n, t) * i.t.w;
		n = normalize(t * tn.x + b * tn.y + n * tn.z);
		gloss = nm.a;
	}
	float3 v = normalize(c.eye.xyz - i.world);
	if (!front) n = -n;
	if (dot(n, v) < 0.0) n = -n;   // cards and two-sided pieces light from the side we see

	float3 key = normalize(c.keyDir.xyz);
	float3 fill = normalize(float3(-key.x, -key.y, 0.25));
	float  diff = saturate(dot(n, key)) * 0.95 + saturate(dot(n, fill)) * 0.30 + 0.30;
	float3 h = normalize(key + v);
	float  power = exp2(gloss * 9.0 + 2.0);
	float3 spec = c.specular.rgb * c.specular.a * pow(saturate(dot(n, h)), power) * (power + 8.0) / 25.0;
	// a soft sky above and a dark floor below, reflected by what shines (metal blades are dark grey in their colour map:
	// without this they read as black)
	float3 r = reflect(-v, n);
	float  sky = saturate(r.z * 0.5 + 0.5);
	float  fres = 0.25 + 0.75 * pow(1.0 - saturate(dot(n, v)), 4.0);
	float3 env = c.specular.rgb * c.specular.a * lerp(0.05, 0.9, sky) * fres * (0.4 + 0.6 * gloss);
	float3 rim = 0.08 * pow(1.0 - saturate(dot(n, v)), 3.0);
	float3 col = base.rgb * diff + spec + env + rim;
	return float4(pow(saturate(col), 1.0 / 2.2), 1.0);
}
)";

		struct Constants
		{
			XMFLOAT4X4 viewProj;
			float      eye[4];
			float      keyDir[4];
			float      specular[4];
			float      tint[4];
			float      shift1[4];
			float      shift2[4];
			std::uint32_t flags[4];
		};
		static_assert(sizeof(Constants) % 4 == 0 && sizeof(Constants) / 4 <= 64);

		struct Gpu
		{
			ID3D12Resource*             res = nullptr;
			D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
		};

		struct Model
		{
			ID3D12Resource*               vb = nullptr;
			ID3D12Resource*               ib = nullptr;
			D3D12_VERTEX_BUFFER_VIEW      vbv{};
			D3D12_INDEX_BUFFER_VIEW       ibv{};
			std::vector<Gpu>              textures;
			std::vector<AMF_PreviewDraw>  draws;
			float                         bmin[3]{}, bmax[3]{};
			// the target
			ID3D12Resource*               color = nullptr;
			ID3D12Resource*               depth = nullptr;
			ID3D12DescriptorHeap*         rtvHeap = nullptr;
			ID3D12DescriptorHeap*         dsvHeap = nullptr;
			D3D12_CPU_DESCRIPTOR_HANDLE   srvCpu{};
			D3D12_GPU_DESCRIPTOR_HANDLE   srvGpu{};
			bool                          hasSrv = false;
			int                           w = 0, h = 0;
			bool                          wanted = false;
			AMF_PreviewView               view{};
		};

		Host                     g_host;
		std::atomic<bool>        g_ready{ false };
		std::mutex               g_lock;
		ID3D12RootSignature*     g_root = nullptr;
		ID3D12PipelineState*     g_pso = nullptr;
		Gpu                      g_white, g_flat;
		std::unordered_set<Model*> g_models;

		template <class T>
		void Rel(T*& p)
		{
			if (p) {
				p->Release();
				p = nullptr;
			}
		}

		DXGI_FORMAT Dxgi(std::int32_t f)
		{
			switch (f) {
			case AMF_PREVIEW_BC1_SRGB: return DXGI_FORMAT_BC1_UNORM_SRGB;
			case AMF_PREVIEW_BC1_UNORM: return DXGI_FORMAT_BC1_UNORM;
			case AMF_PREVIEW_BC3_SRGB: return DXGI_FORMAT_BC3_UNORM_SRGB;
			case AMF_PREVIEW_BC3_UNORM: return DXGI_FORMAT_BC3_UNORM;
			case AMF_PREVIEW_RGBA8_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
			case AMF_PREVIEW_RGBA8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
			case AMF_PREVIEW_BC7_SRGB: return DXGI_FORMAT_BC7_UNORM_SRGB;
			case AMF_PREVIEW_BC7_UNORM: return DXGI_FORMAT_BC7_UNORM;
			default: return DXGI_FORMAT_UNKNOWN;
			}
		}

		// tightly packed bytes of one mip and its row count / row bytes
		void MipShape(std::int32_t f, int w, int h, std::size_t& rowBytes, int& rows)
		{
			const bool bc = f != AMF_PREVIEW_RGBA8_SRGB && f != AMF_PREVIEW_RGBA8_UNORM;
			if (!bc) {
				rowBytes = static_cast<std::size_t>(w) * 4;
				rows = h;
				return;
			}
			const std::size_t block = (f == AMF_PREVIEW_BC1_SRGB || f == AMF_PREVIEW_BC1_UNORM) ? 8 : 16;
			rowBytes = static_cast<std::size_t>(std::max(1, (w + 3) / 4)) * block;
			rows = std::max(1, (h + 3) / 4);
		}

		ID3D12Resource* MakeBuffer(UINT64 a_size, D3D12_HEAP_TYPE a_heap, D3D12_RESOURCE_STATES a_state)
		{
			D3D12_HEAP_PROPERTIES hp{};
			hp.Type = a_heap;
			D3D12_RESOURCE_DESC rd{};
			rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			rd.Width = std::max<UINT64>(a_size, 1);
			rd.Height = 1;
			rd.DepthOrArraySize = 1;
			rd.MipLevels = 1;
			rd.SampleDesc.Count = 1;
			rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			ID3D12Resource* r = nullptr;
			if (FAILED(g_host.device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, a_state, nullptr, IID_PPV_ARGS(&r)))) {
				return nullptr;
			}
			return r;
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

		// A texture with its mips uploaded and an SRV in the host's heap.
		bool UploadTexture(const AMF_PreviewTexture& a_t, Gpu& a_out)
		{
			const DXGI_FORMAT fmt = Dxgi(a_t.format);
			if (fmt == DXGI_FORMAT_UNKNOWN || a_t.width <= 0 || a_t.height <= 0 || a_t.mips <= 0 || !a_t.data) {
				logger::warn("preview: texture format {} {}x{} x{} not usable", a_t.format, a_t.width, a_t.height, a_t.mips);
				return false;
			}
			// the mips the data really holds
			int         mips = 0;
			std::size_t need = 0;
			for (int m = 0; m < a_t.mips; ++m) {
				std::size_t rb;
				int         rows;
				MipShape(a_t.format, std::max(1, a_t.width >> m), std::max(1, a_t.height >> m), rb, rows);
				if (need + rb * rows > a_t.size) break;
				need += rb * rows;
				++mips;
			}
			if (mips == 0) {
				logger::warn("preview: texture {}x{} - {} bytes is less than its first mip", a_t.width, a_t.height, a_t.size);
				return false;
			}
			D3D12_HEAP_PROPERTIES hp{};
			hp.Type = D3D12_HEAP_TYPE_DEFAULT;
			D3D12_RESOURCE_DESC rd{};
			rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			rd.Width = static_cast<UINT64>(a_t.width);
			rd.Height = static_cast<UINT>(a_t.height);
			rd.DepthOrArraySize = 1;
			rd.MipLevels = static_cast<UINT16>(mips);
			rd.Format = fmt;
			rd.SampleDesc.Count = 1;
			ID3D12Resource* tex = nullptr;
			if (FAILED(g_host.device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex)))) {
				logger::warn("preview: texture {}x{} format {} could not be created", a_t.width, a_t.height, a_t.format);
				return false;
			}
			std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> fp(static_cast<std::size_t>(mips));
			std::vector<UINT>                               rows(static_cast<std::size_t>(mips));
			std::vector<UINT64>                             rowSize(static_cast<std::size_t>(mips));
			UINT64                                          total = 0;
			g_host.device->GetCopyableFootprints(&rd, 0, static_cast<UINT>(mips), 0, fp.data(), rows.data(), rowSize.data(), &total);
			ID3D12Resource* up = MakeBuffer(total, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
			std::uint8_t*   mapped = nullptr;
			if (!up || FAILED(up->Map(0, nullptr, reinterpret_cast<void**>(&mapped)))) {
				Rel(up);
				Rel(tex);
				return false;
			}
			const auto* src = static_cast<const std::uint8_t*>(a_t.data);
			for (int m = 0; m < mips; ++m) {
				std::size_t rb;
				int         nrows;
				MipShape(a_t.format, std::max(1, a_t.width >> m), std::max(1, a_t.height >> m), rb, nrows);
				for (int r = 0; r < nrows; ++r) {
					std::memcpy(mapped + fp[m].Offset + static_cast<UINT64>(r) * fp[m].Footprint.RowPitch, src, rb);
					src += rb;
				}
			}
			up->Unmap(0, nullptr);
			const bool ok = g_host.upload([&](ID3D12GraphicsCommandList* l) {
				for (int m = 0; m < mips; ++m) {
					D3D12_TEXTURE_COPY_LOCATION s{}, d{};
					s.pResource = up;
					s.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
					s.PlacedFootprint = fp[m];
					d.pResource = tex;
					d.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
					d.SubresourceIndex = static_cast<UINT>(m);
					l->CopyTextureRegion(&d, 0, 0, 0, &s, nullptr);
				}
				Barrier(l, tex, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			});
			Rel(up);
			D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
			if (!ok || !g_host.allocSrv(cpu, a_out.gpu)) {
				logger::warn("preview: texture upload or descriptor failed");
				Rel(tex);
				return false;
			}
			D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
			sv.Format = fmt;
			sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			sv.Texture2D.MipLevels = static_cast<UINT>(mips);
			g_host.device->CreateShaderResourceView(tex, &sv, cpu);
			a_out.res = tex;
			return true;
		}

		void FreeGpu(Gpu& a_g)
		{
			if (a_g.res) {
				g_host.freeSrv(a_g.gpu.ptr);
			}
			Rel(a_g.res);
		}

		void FreeTarget(Model& m)
		{
			Rel(m.color);
			Rel(m.depth);
			m.w = m.h = 0;
		}

		// (re)makes the model's colour and depth targets at a_w x a_h; the SRV keeps its slot
		bool EnsureTarget(Model& m, int a_w, int a_h)
		{
			if (m.color && m.w == a_w && m.h == a_h) {
				return true;
			}
			if (m.color) {
				g_host.waitIdle();   // the old target may still be read by a frame in flight
				FreeTarget(m);
			}
			if (!m.rtvHeap) {
				D3D12_DESCRIPTOR_HEAP_DESC rd{ D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0 };
				D3D12_DESCRIPTOR_HEAP_DESC dd{ D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0 };
				if (FAILED(g_host.device->CreateDescriptorHeap(&rd, IID_PPV_ARGS(&m.rtvHeap))) ||
					FAILED(g_host.device->CreateDescriptorHeap(&dd, IID_PPV_ARGS(&m.dsvHeap)))) {
					return false;
				}
			}
			if (!m.hasSrv) {
				if (!g_host.allocSrv(m.srvCpu, m.srvGpu)) {
					logger::warn("preview: no descriptor left for the target");
					return false;
				}
				m.hasSrv = true;
			}
			D3D12_HEAP_PROPERTIES hp{};
			hp.Type = D3D12_HEAP_TYPE_DEFAULT;
			D3D12_RESOURCE_DESC cd{};
			cd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			cd.Width = static_cast<UINT64>(a_w);
			cd.Height = static_cast<UINT>(a_h);
			cd.DepthOrArraySize = 1;
			cd.MipLevels = 1;
			cd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			cd.SampleDesc.Count = 1;
			cd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
			D3D12_CLEAR_VALUE cc{};
			cc.Format = cd.Format;
			D3D12_RESOURCE_DESC dd = cd;
			dd.Format = DXGI_FORMAT_D32_FLOAT;
			dd.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
			D3D12_CLEAR_VALUE dc{};
			dc.Format = dd.Format;
			dc.DepthStencil.Depth = 1.0f;
			if (FAILED(g_host.device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &cd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &cc,
					IID_PPV_ARGS(&m.color))) ||
				FAILED(g_host.device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &dd, D3D12_RESOURCE_STATE_DEPTH_WRITE, &dc,
					IID_PPV_ARGS(&m.depth)))) {
				logger::warn("preview: a {}x{} target could not be created", a_w, a_h);
				FreeTarget(m);
				return false;
			}
			g_host.device->CreateRenderTargetView(m.color, nullptr, m.rtvHeap->GetCPUDescriptorHandleForHeapStart());
			g_host.device->CreateDepthStencilView(m.depth, nullptr, m.dsvHeap->GetCPUDescriptorHandleForHeapStart());
			D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
			sv.Format = cd.Format;
			sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			sv.Texture2D.MipLevels = 1;
			g_host.device->CreateShaderResourceView(m.color, &sv, m.srvCpu);
			m.w = a_w;
			m.h = a_h;
			return true;
		}

		ID3DBlob* Compile(const char* a_entry, const char* a_target)
		{
			ID3DBlob* code = nullptr;
			ID3DBlob* err = nullptr;
			const HRESULT hr = D3DCompile(kShader, std::strlen(kShader), "AMFPreview", nullptr, nullptr, a_entry, a_target,
				D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &err);
			if (FAILED(hr)) {
				logger::error("preview: shader {} did not compile: {}", a_entry, err ? static_cast<const char*>(err->GetBufferPointer()) : "?");
				Rel(err);
				Rel(code);
				return nullptr;
			}
			Rel(err);
			return code;
		}
	}

	bool Ready() { return g_ready; }

	bool Init(const Host& a_host)
	{
		std::scoped_lock l(g_lock);
		if (g_ready) {
			return true;
		}
		g_host = a_host;
		if (!g_host.device || !g_host.srvHeap || !g_host.allocSrv || !g_host.freeSrv || !g_host.upload || !g_host.waitIdle) {
			return false;
		}

		D3D12_DESCRIPTOR_RANGE ranges[3]{};
		for (UINT i = 0; i < 3; ++i) {
			ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
			ranges[i].NumDescriptors = 1;
			ranges[i].BaseShaderRegister = i;
			ranges[i].OffsetInDescriptorsFromTableStart = 0;
		}
		D3D12_ROOT_PARAMETER params[4]{};
		params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		params[0].Constants.ShaderRegister = 0;
		params[0].Constants.Num32BitValues = sizeof(Constants) / 4;
		params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		for (UINT i = 0; i < 3; ++i) {
			params[1 + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			params[1 + i].DescriptorTable.NumDescriptorRanges = 1;
			params[1 + i].DescriptorTable.pDescriptorRanges = &ranges[i];
			params[1 + i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		}
		D3D12_STATIC_SAMPLER_DESC samp{};
		samp.Filter = D3D12_FILTER_ANISOTROPIC;
		samp.MaxAnisotropy = 8;
		samp.AddressU = samp.AddressV = samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samp.MaxLOD = D3D12_FLOAT32_MAX;
		samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		D3D12_ROOT_SIGNATURE_DESC rs{};
		rs.NumParameters = 4;
		rs.pParameters = params;
		rs.NumStaticSamplers = 1;
		rs.pStaticSamplers = &samp;
		rs.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
		ID3DBlob* blob = nullptr;
		ID3DBlob* err = nullptr;
		if (FAILED(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err)) ||
			FAILED(g_host.device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&g_root)))) {
			logger::error("preview: the root signature failed: {}", err ? static_cast<const char*>(err->GetBufferPointer()) : "?");
			Rel(blob);
			Rel(err);
			return false;
		}
		Rel(blob);
		Rel(err);

		ID3DBlob* vs = Compile("VSMain", "vs_5_1");
		ID3DBlob* ps = Compile("PSMain", "ps_5_1");
		if (!vs || !ps) {
			Rel(vs);
			Rel(ps);
			return false;
		}
		D3D12_INPUT_ELEMENT_DESC layout[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "NORMAL", 0, DXGI_FORMAT_R10G10B10A2_UNORM, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TANGENT", 0, DXGI_FORMAT_R10G10B10A2_UNORM, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};
		D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
		pd.pRootSignature = g_root;
		pd.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
		pd.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
		pd.InputLayout = { layout, 4 };
		pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		pd.NumRenderTargets = 1;
		pd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		pd.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		pd.SampleDesc.Count = 1;
		pd.SampleMask = UINT_MAX;
		pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;   // skinned pieces and cards are open meshes; depth sorts it
		pd.RasterizerState.DepthClipEnable = TRUE;
		pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		pd.DepthStencilState.DepthEnable = TRUE;
		pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		pd.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		const HRESULT hr = g_host.device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&g_pso));
		Rel(vs);
		Rel(ps);
		if (FAILED(hr)) {
			logger::error("preview: the pipeline state failed (0x{:08X})", static_cast<unsigned>(hr));
			return false;
		}

		// stand-ins for a missing colour or normal map
		const std::uint32_t white = 0xFFFFFFFFu, flat = 0xFFFF8080u;
		const AMF_PreviewTexture tw{ AMF_PREVIEW_RGBA8_SRGB, 1, 1, 1, &white, 4 };
		const AMF_PreviewTexture tf{ AMF_PREVIEW_RGBA8_UNORM, 1, 1, 1, &flat, 4 };
		if (!UploadTexture(tw, g_white) || !UploadTexture(tf, g_flat)) {
			logger::error("preview: the default textures could not be uploaded");
			return false;
		}
		g_ready = true;
		logger::info("preview: the 3D preview renderer is ready");
		return true;
	}

	void* Create(const AMF_PreviewMesh* a_mesh)
	{
		if (!g_ready || !a_mesh || !a_mesh->vertices || !a_mesh->indices || a_mesh->vertexCount == 0 || a_mesh->indexCount == 0) {
			return nullptr;
		}
		const auto t0 = std::chrono::steady_clock::now();
		auto*      m = new Model();
		const UINT64 vbBytes = static_cast<UINT64>(a_mesh->vertexCount) * sizeof(AMF_PreviewVertex);
		const UINT64 ibBytes = static_cast<UINT64>(a_mesh->indexCount) * 4;
		m->vb = MakeBuffer(vbBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
		m->ib = MakeBuffer(ibBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
		ID3D12Resource* up = MakeBuffer(vbBytes + ibBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
		std::uint8_t*   mapped = nullptr;
		bool            ok = m->vb && m->ib && up && SUCCEEDED(up->Map(0, nullptr, reinterpret_cast<void**>(&mapped)));
		if (ok) {
			std::memcpy(mapped, a_mesh->vertices, static_cast<std::size_t>(vbBytes));
			std::memcpy(mapped + vbBytes, a_mesh->indices, static_cast<std::size_t>(ibBytes));
			up->Unmap(0, nullptr);
			ok = g_host.upload([&](ID3D12GraphicsCommandList* l) {
				l->CopyBufferRegion(m->vb, 0, up, 0, vbBytes);
				l->CopyBufferRegion(m->ib, 0, up, vbBytes, ibBytes);
				Barrier(l, m->vb, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
				Barrier(l, m->ib, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER);
			});
		}
		Rel(up);
		if (!ok) {
			logger::warn("preview: the model's buffers could not be uploaded");
			Rel(m->vb);
			Rel(m->ib);
			delete m;
			return nullptr;
		}
		m->vbv = { m->vb->GetGPUVirtualAddress(), static_cast<UINT>(vbBytes), sizeof(AMF_PreviewVertex) };
		m->ibv = { m->ib->GetGPUVirtualAddress(), static_cast<UINT>(ibBytes), DXGI_FORMAT_R32_UINT };
		m->textures.resize(a_mesh->textureCount);
		std::uint32_t texOk = 0;
		for (std::uint32_t i = 0; i < a_mesh->textureCount; ++i) {
			texOk += UploadTexture(a_mesh->textures[i], m->textures[i]) ? 1u : 0u;
		}
		m->draws.assign(a_mesh->draws, a_mesh->draws + a_mesh->drawCount);
		std::memcpy(m->bmin, a_mesh->boundsMin, sizeof(m->bmin));
		std::memcpy(m->bmax, a_mesh->boundsMax, sizeof(m->bmax));
		{
			std::scoped_lock l(g_lock);
			g_models.insert(m);
		}
		logger::info("preview: model uploaded - {} vertices, {} triangles, {} draws, {} of {} textures, {:.1f} ms", a_mesh->vertexCount,
			a_mesh->indexCount / 3, a_mesh->drawCount, texOk, a_mesh->textureCount,
			std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
		return m;
	}

	void* Request(void* a_handle, int a_width, int a_height, const AMF_PreviewView* a_view)
	{
		if (!g_ready || !a_handle || !a_view || a_width <= 0 || a_height <= 0) {
			return nullptr;
		}
		std::scoped_lock l(g_lock);
		auto* m = static_cast<Model*>(a_handle);
		if (!g_models.contains(m)) {
			return nullptr;
		}
		const int w = std::min(a_width * kSuper, kMaxSide), h = std::min(a_height * kSuper, kMaxSide);
		if (!EnsureTarget(*m, w, h)) {
			return nullptr;
		}
		m->wanted = true;
		m->view = *a_view;
		return reinterpret_cast<void*>(m->srvGpu.ptr);
	}

	void Record(ID3D12GraphicsCommandList* a_list)
	{
		if (!g_ready || !a_list) {
			return;
		}
		std::scoped_lock l(g_lock);
		for (Model* m : g_models) {
			if (!m->wanted || !m->color) {
				continue;
			}
			m->wanted = false;
			Barrier(a_list, m->color, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
			const D3D12_CPU_DESCRIPTOR_HANDLE rtv = m->rtvHeap->GetCPUDescriptorHandleForHeapStart();
			const D3D12_CPU_DESCRIPTOR_HANDLE dsv = m->dsvHeap->GetCPUDescriptorHandleForHeapStart();
			a_list->ClearRenderTargetView(rtv, m->view.background, 0, nullptr);
			a_list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
			a_list->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
			const D3D12_VIEWPORT vp{ 0.0f, 0.0f, static_cast<float>(m->w), static_cast<float>(m->h), 0.0f, 1.0f };
			const D3D12_RECT     sc{ 0, 0, m->w, m->h };
			a_list->RSSetViewports(1, &vp);
			a_list->RSSetScissorRects(1, &sc);
			a_list->SetDescriptorHeaps(1, &g_host.srvHeap);
			a_list->SetGraphicsRootSignature(g_root);
			a_list->SetPipelineState(g_pso);
			a_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			a_list->IASetVertexBuffers(0, 1, &m->vbv);
			a_list->IASetIndexBuffer(&m->ibv);

			// the camera orbits the box's centre (Z up) at the distance that fits its bounding sphere
			const XMVECTOR lo = XMVectorSet(m->bmin[0], m->bmin[1], m->bmin[2], 0.0f);
			const XMVECTOR hi = XMVectorSet(m->bmax[0], m->bmax[1], m->bmax[2], 0.0f);
			const XMVECTOR centre = XMVectorScale(XMVectorAdd(lo, hi), 0.5f);
			const float    radius = std::max(0.01f, XMVectorGetX(XMVector3Length(XMVectorSubtract(hi, lo))) * 0.5f);
			const float    fov = XMConvertToRadians(30.0f);
			const float    aspect = static_cast<float>(m->w) / static_cast<float>(m->h);
			// the bounding sphere fits exactly at radius / sin(fov/2): the whole item stays in the picture from every side
			// (0.8 of it cut long swords off when seen side-on - the offline check, 2026-10-06); the player zooms in
			const float    fit = radius / std::sin(fov * 0.5f * std::min(1.0f, aspect));
			const float    dist = fit * std::clamp(m->view.zoom, 0.15f, 4.0f);
			const float    yaw = XMConvertToRadians(m->view.yaw), pitch = XMConvertToRadians(std::clamp(m->view.pitch, -89.0f, 89.0f));
			const XMVECTOR dir = XMVectorSet(std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch), 0.0f);
			const XMVECTOR eye = XMVectorAdd(centre, XMVectorScale(dir, dist));
			const XMMATRIX viewM = XMMatrixLookAtRH(eye, centre, XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f));
			const XMMATRIX proj = XMMatrixPerspectiveFovRH(fov, aspect, std::max(0.001f, dist - radius * 2.0f), dist + radius * 2.0f);
			// the key light: from above and to the camera's left
			const XMVECTOR right = XMVector3Normalize(XMVector3Cross(dir, XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f)));
			const XMVECTOR key = XMVector3Normalize(XMVectorAdd(XMVectorAdd(dir, XMVectorScale(right, -0.8f)), XMVectorSet(0.0f, 0.0f, 0.9f, 0.0f)));

			Constants cb{};
			XMStoreFloat4x4(&cb.viewProj, XMMatrixMultiply(viewM, proj));
			XMStoreFloat4(reinterpret_cast<XMFLOAT4*>(cb.eye), eye);
			XMStoreFloat4(reinterpret_cast<XMFLOAT4*>(cb.keyDir), key);
			for (const auto& d : m->draws) {
				if (d.indexCount == 0) {
					continue;
				}
				const bool hasD = d.diffuse >= 0 && static_cast<std::size_t>(d.diffuse) < m->textures.size() && m->textures[d.diffuse].res;
				const bool hasN = d.normal >= 0 && static_cast<std::size_t>(d.normal) < m->textures.size() && m->textures[d.normal].res;
				std::memcpy(cb.specular, d.specular, sizeof(cb.specular));
				std::memcpy(cb.tint, d.tint, sizeof(cb.tint));
				if (cb.tint[0] == 0.0f && cb.tint[1] == 0.0f && cb.tint[2] == 0.0f) {
					cb.tint[0] = cb.tint[1] = cb.tint[2] = cb.tint[3] = 1.0f;   // an unset tint means "as is"
				}
				std::memcpy(cb.shift1, d.colorShift1, sizeof(cb.shift1));
				std::memcpy(cb.shift2, d.colorShift2, sizeof(cb.shift2));
				cb.flags[0] = d.flags;
				cb.flags[1] = hasD ? 1u : 0u;
				cb.flags[2] = hasN ? 1u : 0u;
				// the dye mask: colorShift1.w is its texture index (-1 = none: the constant in colorShift2.w)
				const int  maskIdx = static_cast<int>(d.colorShift1[3]);
				const bool hasM = (d.flags & AMF_PREVIEW_DYE) && maskIdx >= 0 && static_cast<std::size_t>(maskIdx) < m->textures.size() &&
				                  m->textures[maskIdx].res;
				cb.flags[3] = hasM ? 1u : 0u;
				a_list->SetGraphicsRoot32BitConstants(0, sizeof(Constants) / 4, &cb, 0);
				a_list->SetGraphicsRootDescriptorTable(1, hasD ? m->textures[d.diffuse].gpu : g_white.gpu);
				a_list->SetGraphicsRootDescriptorTable(2, hasN ? m->textures[d.normal].gpu : g_flat.gpu);
				a_list->SetGraphicsRootDescriptorTable(3, hasM ? m->textures[maskIdx].gpu : g_white.gpu);
				a_list->DrawIndexedInstanced(d.indexCount, 1, d.firstIndex, d.baseVertex, 0);
			}
			Barrier(a_list, m->color, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		}
	}

	void* DebugTarget(void* a_handle)
	{
		std::scoped_lock l(g_lock);
		auto* m = static_cast<Model*>(a_handle);
		return g_models.contains(m) ? m->color : nullptr;
	}

	void Release(void* a_handle)
	{
		if (!a_handle) {
			return;
		}
		auto* m = static_cast<Model*>(a_handle);
		{
			std::scoped_lock l(g_lock);
			if (!g_models.erase(m)) {
				return;
			}
		}
		g_host.waitIdle();
		for (auto& t : m->textures) {
			FreeGpu(t);
		}
		if (m->hasSrv) {
			g_host.freeSrv(m->srvGpu.ptr);
		}
		FreeTarget(*m);
		Rel(m->rtvHeap);
		Rel(m->dsvHeap);
		Rel(m->vb);
		Rel(m->ib);
		delete m;
	}
}
