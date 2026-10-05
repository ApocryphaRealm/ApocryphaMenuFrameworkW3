#include "Screenshot.h"

#include "Settings.h"

#include <ShlObj.h>
#include <wincodec.h>

#include <thread>

namespace screenshot
{
	namespace
	{
		std::atomic_bool g_busy{ false };

		HWND GameWindow()
		{
			HWND front = ::GetForegroundWindow();
			if (!front || ::IsIconic(front)) { return nullptr; }
			DWORD pid = 0;
			::GetWindowThreadProcessId(front, &pid);
			return pid == ::GetCurrentProcessId() ? front : nullptr;
		}

		std::filesystem::path Folder()
		{
			const auto& folder = settings::Get().screenshotFolder;
			if (!folder.empty()) { return std::filesystem::path(folder); }
			// Witcher 3 keeps no Data folder: pictures go beside the game's own documents (saves and settings), where
			// they survive a game update and are easy to find.
			PWSTR docs = nullptr;
			std::filesystem::path out = "AMF Screenshots";
			if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs)) && docs)
			{
				out = std::filesystem::path(docs) / "The Witcher 3" / "AMF Screenshots";
			}
			if (docs) { ::CoTaskMemFree(docs); }
			return out;
		}

		std::filesystem::path NextName(const std::filesystem::path& a_folder)
		{
			SYSTEMTIME t{};
			::GetLocalTime(&t);
			const std::string stem = std::format("Oblivion Remastered {:04}-{:02}-{:02} {:02}-{:02}-{:02}",
				t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
			std::filesystem::path p = a_folder / (stem + ".png");
			for (int n = 2; std::filesystem::exists(p) && n < 100; ++n)
			{
				p = a_folder / std::format("{} ({}).png", stem, n);
			}
			return p;
		}

		bool SavePng(HBITMAP a_bitmap, const std::filesystem::path& a_path)
		{
			IWICImagingFactory*    factory = nullptr;
			IWICBitmap*            bitmap = nullptr;
			IWICStream*            stream = nullptr;
			IWICBitmapEncoder*     encoder = nullptr;
			IWICBitmapFrameEncode* frame = nullptr;
			bool ok = SUCCEEDED(::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
					  SUCCEEDED(factory->CreateBitmapFromHBITMAP(a_bitmap, nullptr, WICBitmapIgnoreAlpha, &bitmap)) &&
					  SUCCEEDED(factory->CreateStream(&stream)) &&
					  SUCCEEDED(stream->InitializeFromFilename(a_path.c_str(), GENERIC_WRITE)) &&
					  SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
					  SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache)) &&
					  SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) &&
					  SUCCEEDED(frame->Initialize(nullptr)) &&
					  SUCCEEDED(frame->WriteSource(bitmap, nullptr)) &&
					  SUCCEEDED(frame->Commit()) &&
					  SUCCEEDED(encoder->Commit());
			if (frame) { frame->Release(); }
			if (encoder) { encoder->Release(); }
			if (stream) { stream->Release(); }
			if (bitmap) { bitmap->Release(); }
			if (factory) { factory->Release(); }
			return ok;
		}

		// Worker thread: copy the screen area the game covers, as Windows shows it, and write it out.
		void Shoot(RECT a_area)
		{
			const int w = a_area.right - a_area.left;
			const int h = a_area.bottom - a_area.top;
			bool ok = false;
			std::filesystem::path path;
			if (w > 0 && h > 0)
			{
				const HRESULT co = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
				HDC screen = ::GetDC(nullptr);
				HDC mem = ::CreateCompatibleDC(screen);
				HBITMAP bmp = ::CreateCompatibleBitmap(screen, w, h);
				HGDIOBJ old = ::SelectObject(mem, bmp);
				const bool copied = ::BitBlt(mem, 0, 0, w, h, screen, a_area.left, a_area.top, SRCCOPY | CAPTUREBLT) != FALSE;
				::SelectObject(mem, old);
				::DeleteDC(mem);
				::ReleaseDC(nullptr, screen);
				if (copied)
				{
					std::error_code ec;
					const auto folder = Folder();
					std::filesystem::create_directories(folder, ec);
					path = NextName(folder);
					ok = SavePng(bmp, path);
				}
				::DeleteObject(bmp);
				if (SUCCEEDED(co)) { ::CoUninitialize(); }
			}
			if (ok)
			{
				logger::info("screenshot: saved {} ({}x{})", path.string(), w, h);
				::Beep(880, 60);
				::Beep(1320, 80);
			}
			else
			{
				logger::warn("screenshot: not saved ({}x{} at {},{}; path '{}')", w, h, a_area.left, a_area.top, path.string());
				::Beep(330, 180);
			}
			g_busy.store(false);
		}

	}

	void Take()
	{
		HWND game = GameWindow();
		if (!game)
		{
			logger::info("screenshot: not taken - the game is not the window in front");
			return;
		}
		RECT area{};
		POINT origin{ 0, 0 };
		if (!::GetClientRect(game, &area) || !::ClientToScreen(game, &origin)) { return; }
		::OffsetRect(&area, origin.x, origin.y);
		if (g_busy.exchange(true)) { return; }   // one at a time
		std::thread(Shoot, area).detach();
	}
}
