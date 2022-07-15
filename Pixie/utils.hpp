#pragma once

#include <SDL.h>
#include <chrono>
#include <memory>
#include <stdexcept>

namespace pxe {
	/*
	namespace limits {
		std::chrono::steady_clock::time_point begin {};
		std::chrono::steady_clock::time_point end {};
		std::chrono::duration<double, std::milli> deltaTime {};
	} 
	*/

	struct PixieMemory final {
		void operator()(SDL_Window *x) const {SDL_DestroyWindow(x);}
		void operator()(SDL_Renderer *x) const {SDL_DestroyRenderer(x);}
		void operator()(SDL_Texture *x) const {SDL_DestroyTexture(x);}
	};

	template <typename T> using PixiePTR = std::unique_ptr<T, PixieMemory>;

	class PixieException final {
	public:
		PixieException(HRESULT hr) : result(hr) {
			if (FAILED(hr)) {
				constexpr int msgLength = 265;
				//char errorMsg[msgLength];
				auto errorMsg = std::make_unique<int[]>(msgLength);
				std::memset(errorMsg.get(), 0, msgLength);
				FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, hr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)errorMsg.get(), msgLength - 1, NULL);
				MessageBox(NULL, (LPWSTR)errorMsg.get(), L"Error", MB_OK);
				exit(EXIT_FAILURE);
			}
		}

	private:
		const HRESULT result;
	};

	inline void throwIfFailed(HRESULT hr) {
		if (FAILED(hr)) {
			throw PixieException(hr);
		}
	}
} // namespace pxe