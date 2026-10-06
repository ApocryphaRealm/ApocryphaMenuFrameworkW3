// The 3D preview outside the game: `xmake build preview_test` then
// `xmake run preview_test <file.amfprev> <out prefix> [debug]` runs src/Preview3D.cpp on a D3D12 device of its own (the
// debug layer with "debug"), draws the model from three sides and writes each as <out prefix>_<n>.rgba (raw RGBA8, the
// requested size, after the same 2x2 average ImGui's sampling gives in game). tools/preview_export.py makes .amfprev
// files from the item-model prototype.

#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include "Preview3D.h"

#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstring>
#include <functional>
#include <fstream>
#include <string>
#include <vector>

namespace
{
	ID3D12Device*              g_dev = nullptr;
	ID3D12CommandQueue*        g_q = nullptr;
	ID3D12CommandAllocator*    g_alloc = nullptr;
	ID3D12GraphicsCommandList* g_list = nullptr;
	ID3D12Fence*               g_fence = nullptr;
	UINT64                     g_fv = 0;
	HANDLE                     g_ev = nullptr;
	ID3D12DescriptorHeap*      g_heap = nullptr;
	UINT                       g_next = 0, g_step = 0;

	void Wait()
	{
		g_q->Signal(g_fence, ++g_fv);
		if (g_fence->GetCompletedValue() < g_fv) {
			g_fence->SetEventOnCompletion(g_fv, g_ev);
			WaitForSingleObject(g_ev, INFINITE);
		}
	}

	bool Run(const std::function<void(ID3D12GraphicsCommandList*)>& a_fn)
	{
		g_alloc->Reset();
		g_list->Reset(g_alloc, nullptr);
		a_fn(g_list);
		if (FAILED(g_list->Close())) return false;
		ID3D12CommandList* l[] = { g_list };
		g_q->ExecuteCommandLists(1, l);
		Wait();
		return g_dev->GetDeviceRemovedReason() == S_OK;
	}

	template <class T>
	T Get(std::ifstream& in)
	{
		T v{};
		in.read(reinterpret_cast<char*>(&v), sizeof(T));
		return v;
	}
}

int main(int argc, char** argv)
{
	if (argc < 3) {
		std::puts("usage: preview_test <file.amfprev> <out prefix> [debug]");
		return 2;
	}
	spdlog::set_default_logger(spdlog::stdout_logger_mt("test"));
	spdlog::set_level(spdlog::level::debug);
	const bool debug = argc > 3 && std::string(argv[3]) == "debug";
	if (debug) {
		ID3D12Debug* dbg = nullptr;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg)))) {
			dbg->EnableDebugLayer();
			dbg->Release();
		}
	}
	if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&g_dev)))) {
		std::puts("no D3D12 device");
		return 1;
	}
	D3D12_COMMAND_QUEUE_DESC qd{};
	qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	g_dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&g_q));
	g_dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_alloc));
	g_dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_alloc, nullptr, IID_PPV_ARGS(&g_list));
	g_list->Close();
	g_dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence));
	g_ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	D3D12_DESCRIPTOR_HEAP_DESC hd{ D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 64, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0 };
	g_dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&g_heap));
	g_step = g_dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	preview3d::Host host;
	host.device = g_dev;
	host.srvHeap = g_heap;
	host.allocSrv = [](D3D12_CPU_DESCRIPTOR_HANDLE& c, D3D12_GPU_DESCRIPTOR_HANDLE& g) {
		if (g_next >= 64) return false;
		c = g_heap->GetCPUDescriptorHandleForHeapStart();
		g = g_heap->GetGPUDescriptorHandleForHeapStart();
		c.ptr += static_cast<SIZE_T>(g_next) * g_step;
		g.ptr += static_cast<UINT64>(g_next) * g_step;
		++g_next;
		return true;
	};
	host.freeSrv = [](std::uint64_t) {};
	host.upload = [](const std::function<void(ID3D12GraphicsCommandList*)>& fn) { return Run(fn); };
	host.waitIdle = [] { Wait(); };
	if (!preview3d::Init(host)) {
		std::puts("Init failed");
		return 1;
	}

	std::ifstream in(argv[1], std::ios::binary);
	char magic[4]{};
	in.read(magic, 4);
	if (std::memcmp(magic, "AMFP", 4) != 0) {
		std::puts("not an .amfprev file");
		return 1;
	}
	Get<std::uint32_t>(in);
	const auto nv = Get<std::uint32_t>(in), ni = Get<std::uint32_t>(in), nd = Get<std::uint32_t>(in), nt = Get<std::uint32_t>(in);
	AMF_PreviewMesh mesh{};
	in.read(reinterpret_cast<char*>(mesh.boundsMin), 12);
	in.read(reinterpret_cast<char*>(mesh.boundsMax), 12);
	std::vector<AMF_PreviewVertex> verts(nv);
	std::vector<std::uint32_t>     idx(ni);
	std::vector<AMF_PreviewDraw>   draws(nd);
	in.read(reinterpret_cast<char*>(verts.data()), nv * sizeof(AMF_PreviewVertex));
	in.read(reinterpret_cast<char*>(idx.data()), ni * 4ull);
	in.read(reinterpret_cast<char*>(draws.data()), nd * sizeof(AMF_PreviewDraw));
	std::vector<AMF_PreviewTexture>          texs(nt);
	std::vector<std::vector<std::uint8_t>>   texData(nt);
	for (std::uint32_t i = 0; i < nt; ++i) {
		texs[i].format = Get<std::int32_t>(in);
		texs[i].width = Get<std::int32_t>(in);
		texs[i].height = Get<std::int32_t>(in);
		texs[i].mips = Get<std::int32_t>(in);
		texs[i].size = Get<std::uint32_t>(in);
		texData[i].resize(texs[i].size);
		in.read(reinterpret_cast<char*>(texData[i].data()), texs[i].size);
		texs[i].data = texData[i].data();
	}
	mesh.vertices = verts.data();
	mesh.vertexCount = nv;
	mesh.indices = idx.data();
	mesh.indexCount = ni;
	mesh.draws = draws.data();
	mesh.drawCount = nd;
	mesh.textures = texs.data();
	mesh.textureCount = nt;
	void* h = preview3d::Create(&mesh);
	if (!h) {
		std::puts("Create failed");
		return 1;
	}

	const int   size = 512;
	const float yaws[] = { 30.0f, 120.0f, 250.0f };
	for (int v = 0; v < 3; ++v) {
		AMF_PreviewView view{ yaws[v], 15.0f, 1.0f, { 0.12f, 0.11f, 0.10f, 1.0f } };
		void* tex = preview3d::Request(h, size, size, &view);
		if (!tex) {
			std::puts("Request failed");
			return 1;
		}
		// render into the model's target, then copy it out through a readback buffer
		ID3D12Resource* target = nullptr;
		const int   W = size * 2, H = size * 2;
		const UINT  pitch = (W * 4 + 255) & ~255u;
		D3D12_HEAP_PROPERTIES rp{};
		rp.Type = D3D12_HEAP_TYPE_READBACK;
		D3D12_RESOURCE_DESC bd{};
		bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		bd.Width = static_cast<UINT64>(pitch) * H;
		bd.Height = 1;
		bd.DepthOrArraySize = 1;
		bd.MipLevels = 1;
		bd.SampleDesc.Count = 1;
		bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		ID3D12Resource* rb = nullptr;
		g_dev->CreateCommittedResource(&rp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&rb));
		target = static_cast<ID3D12Resource*>(preview3d::DebugTarget(h));
		const bool ok = Run([&](ID3D12GraphicsCommandList* l) {
			preview3d::Record(l);
			D3D12_RESOURCE_BARRIER b{};
			b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			b.Transition.pResource = target;
			b.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
			l->ResourceBarrier(1, &b);
			D3D12_TEXTURE_COPY_LOCATION s{}, d{};
			s.pResource = target;
			s.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			d.pResource = rb;
			d.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
			d.PlacedFootprint.Footprint = { DXGI_FORMAT_R8G8B8A8_UNORM, static_cast<UINT>(W), static_cast<UINT>(H), 1, pitch };
			l->CopyTextureRegion(&d, 0, 0, 0, &s, nullptr);
			std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
			l->ResourceBarrier(1, &b);
		});
		if (!ok) {
			std::printf("the GPU said no (device removed 0x%08X)\n", static_cast<unsigned>(g_dev->GetDeviceRemovedReason()));
			return 1;
		}
		std::uint8_t* p = nullptr;
		rb->Map(0, nullptr, reinterpret_cast<void**>(&p));
		std::vector<std::uint8_t> out(static_cast<std::size_t>(size) * size * 4);
		for (int y = 0; y < size; ++y) {
			for (int x = 0; x < size; ++x) {
				for (int ch = 0; ch < 4; ++ch) {
					int s = 0;
					for (int dy = 0; dy < 2; ++dy)
						for (int dx = 0; dx < 2; ++dx) s += p[static_cast<std::size_t>(y * 2 + dy) * pitch + (x * 2 + dx) * 4 + ch];
					out[(static_cast<std::size_t>(y) * size + x) * 4 + ch] = static_cast<std::uint8_t>(s / 4);
				}
			}
		}
		rb->Unmap(0, nullptr);
		rb->Release();
		const std::string name = std::string(argv[2]) + "_" + std::to_string(v) + ".rgba";
		std::ofstream(name, std::ios::binary).write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
		std::printf("wrote %s (%dx%d)\n", name.c_str(), size, size);
	}
	preview3d::Release(h);
	std::puts("done");
	return 0;
}
