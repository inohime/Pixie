#include <iostream>
#include <SDL.h>
#include <memory>
#include "sound.hpp"
#include "entity.hpp"

//HRESULT findChunk(HANDLE file)

int main(int, char **)
{
	SDL_assert(SDL_Init(SDL_INIT_EVERYTHING) == 0);

	HRESULT hr;
	hr = CoInitializeEx(nullptr, COINITBASE_MULTITHREADED);
	if (FAILED(hr))
		return hr;

	IXAudio2 *xaudio = nullptr;
	if (FAILED(hr = XAudio2Create(&xaudio, 0, XAUDIO2_DEFAULT_PROCESSOR)))
		return hr;

	// create mastering voice which encapsulates an audio device
	IXAudio2MasteringVoice *masterVoice = nullptr;
	if (FAILED(hr = xaudio->CreateMasteringVoice(&masterVoice)))
		return hr;

	auto window = std::shared_ptr<SDL_Window>(SDL_CreateWindow("", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1024, 768, 0), SDL_DestroyWindow);
	auto renderer = std::shared_ptr<SDL_Renderer>(SDL_CreateRenderer(window.get(), -1, SDL_RENDERER_ACCELERATED), SDL_DestroyRenderer);

	bool shouldRun = true;
	while (shouldRun) {
		SDL_Event ev;
		SDL_PollEvent(&ev);
		switch (ev.type) {
			case SDL_QUIT:
				shouldRun = false;
				break;
		}

		SDL_SetRenderDrawColor(renderer.get(), 255, 0, 0, 255);
		SDL_RenderClear(renderer.get());

		SDL_RenderPresent(renderer.get());
	}

	masterVoice->DestroyVoice();
	xaudio->StopEngine();
	xaudio->Release();

	SDL_Quit();

	return 0;
}