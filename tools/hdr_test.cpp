// The HDR composite outside the game: `xmake build hdr_test` then `xmake run hdr_test` runs src/HdrComposite.cpp on a
// D3D12 device of its own: four known menu colours (premultiplied, as ImGui leaves them) go through the pass into an
// R10G10B10A2 back buffer in each mode, and the 10-bit values read back are printed beside the expected ones
// (tools/hdr_expected.py computes those independently).

#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>
#include <d3d12.h>

#include "HdrComposite.h"

#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstring>
#include <functional>

namespace hdr { void* DebugTarget(); }

namespace
{
	ID3D12Device*              g_dev = nullptr;
	ID3D12CommandQueue*        g_q = nullptr;
	ID3D12CommandAllocator*    g_alloc = nullptr;
	ID3D12GraphicsCommandList* g_list = nullptr;
	ID3D12Fence*               g_fence = nullptr;
	UINT64                     g_fv = 0;
	HANDLE                     g_ev = nullptr;

	void Run(const std::function<void(ID3D12GraphicsCommandList*)>& a_fn)
	{
		g_alloc->Reset();
		g_list->Reset(g_alloc, nullptr);
		a_fn(g_list);
		g_list->Close();
		ID3D12CommandList* l[] = { g_list };
		g_q->ExecuteCommandLists(1, l);
		g_q->Signal(g_fence, ++g_fv);
		g_fence->SetEventOnCompletion(g_fv, g_ev);
		WaitForSingleObject(g_ev, INFINITE);
	}

	void Barrier(ID3D12GraphicsCommandList* l, ID3D12Resource* r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b)
	{
		D3D12_RESOURCE_BARRIER x{};
		x.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		x.Transition.pResource = r;
		x.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		x.Transition.StateBefore = a;
		x.Transition.StateAfter = b;
		l->ResourceBarrier(1, &x);
	}

	ID3D12Resource* Buffer(UINT64 size, D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES st)
	{
		D3D12_HEAP_PROPERTIES hp{};
		hp.Type = type;
		D3D12_RESOURCE_DESC d{};
		d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		d.Width = size;
		d.Height = 1;
		d.DepthOrArraySize = 1;
		d.MipLevels = 1;
		d.SampleDesc.Count = 1;
		d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		ID3D12Resource* r = nullptr;
		g_dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, st, nullptr, IID_PPV_ARGS(&r));
		return r;
	}
}

int main(int argc, char** argv)
{
	spdlog::set_default_logger(spdlog::stdout_logger_mt("test"));
	if (argc > 1) {
		ID3D12Debug* dbg = nullptr;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg)))) dbg->EnableDebugLayer();
	}
	D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&g_dev));
	D3D12_COMMAND_QUEUE_DESC qd{};
	g_dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&g_q));
	g_dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_alloc));
	g_dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_alloc, nullptr, IID_PPV_ARGS(&g_list));
	g_list->Close();
	g_dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence));
	g_ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	ID3D12DescriptorHeap* heap = nullptr;
	D3D12_DESCRIPTOR_HEAP_DESC hd{ D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0 };
	g_dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap));

	const DXGI_FORMAT back = DXGI_FORMAT_R10G10B10A2_UNORM;
	if (!hdr::Init(g_dev, back, [&](D3D12_CPU_DESCRIPTOR_HANDLE& c, D3D12_GPU_DESCRIPTOR_HANDLE& g) {
			c = heap->GetCPUDescriptorHandleForHeapStart();
			g = heap->GetGPUDescriptorHandleForHeapStart();
			return true;
		}) || !hdr::EnsureTarget(4, 1)) {
		std::puts("hdr init failed");
		return 1;
	}
	auto* ui = static_cast<ID3D12Resource*>(hdr::DebugTarget());

	// the back buffer: R10G10B10A2, 4x1
	D3D12_HEAP_PROPERTIES hp{};
	hp.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_DESC bd{};
	bd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	bd.Width = 4;
	bd.Height = 1;
	bd.DepthOrArraySize = 1;
	bd.MipLevels = 1;
	bd.Format = back;
	bd.SampleDesc.Count = 1;
	bd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	ID3D12Resource* bb = nullptr;
	g_dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&bb));
	ID3D12DescriptorHeap* rtvHeap = nullptr;
	D3D12_DESCRIPTOR_HEAP_DESC rd{ D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0 };
	g_dev->CreateDescriptorHeap(&rd, IID_PPV_ARGS(&rtvHeap));
	const auto rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
	g_dev->CreateRenderTargetView(bb, nullptr, rtv);

	// the menu's pixels: AMF's green toggle, its red track, white, mid grey - all opaque
	const std::uint8_t px[16] = { 76, 175, 80, 255, 191, 68, 68, 255, 255, 255, 255, 255, 128, 128, 128, 255 };
	ID3D12Resource* up = Buffer(256, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
	void*           m = nullptr;
	up->Map(0, nullptr, &m);
	std::memcpy(m, px, sizeof(px));
	up->Unmap(0, nullptr);
	ID3D12Resource* rb = Buffer(256, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);

	for (int mode = 0; mode < 2; ++mode) {
		Run([&](ID3D12GraphicsCommandList* l) {
			const float black[4] = { 0, 0, 0, 0 };
			l->ClearRenderTargetView(rtv, black, 0, nullptr);
			Barrier(l, ui, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
			D3D12_TEXTURE_COPY_LOCATION s{}, d{};
			s.pResource = up;
			s.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
			s.PlacedFootprint.Footprint = { DXGI_FORMAT_R8G8B8A8_UNORM, 4, 1, 1, 256 };
			d.pResource = ui;
			d.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			l->CopyTextureRegion(&d, 0, 0, 0, &s, nullptr);
			Barrier(l, ui, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
			hdr::Composite(l, rtv, heap, static_cast<hdr::Mode>(mode), 250.0f);
			Barrier(l, bb, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
			D3D12_TEXTURE_COPY_LOCATION s2{}, d2{};
			s2.pResource = bb;
			s2.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			d2.pResource = rb;
			d2.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
			d2.PlacedFootprint.Footprint = { back, 4, 1, 1, 256 };
			l->CopyTextureRegion(&d2, 0, 0, 0, &s2, nullptr);
			Barrier(l, bb, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
		});
		std::uint32_t* out = nullptr;
		rb->Map(0, nullptr, reinterpret_cast<void**>(&out));
		std::printf("%s:", hdr::ModeName(static_cast<hdr::Mode>(mode)));
		for (int i = 0; i < 4; ++i) {
			const std::uint32_t v = out[i];
			std::printf("  (%u,%u,%u)", v & 1023u, (v >> 10) & 1023u, (v >> 20) & 1023u);
		}
		std::printf("\n");
		rb->Unmap(0, nullptr);
	}
	std::printf("device %s\n", g_dev->GetDeviceRemovedReason() == S_OK ? "ok" : "REMOVED");
	return 0;
}
