#include "renderer.hpp"
#include <chrono>

using namespace pxe;

int main(int, char **) 
{
	SDL_assert(SDL_Init(SDL_INIT_EVERYTHING) == 0);

	FLOAT color[4] = {0.1f, 0.1f, 0.1f, 1.0f};

	constexpr int width = 1024;
	constexpr int height = 768;

	auto window = PixiePTR<SDL_Window>(SDL_CreateWindow("D3D12", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, 0));
	auto renderer = PixieRenderer(window.get(), width, height);

	auto begin = std::chrono::steady_clock::now();

	constexpr int FPS = 60;
	const double delay = 1000.0 / FPS;

	SDL_Event ev;
	bool shouldRun = true;
	while (shouldRun) {
		SDL_PollEvent(&ev);
		switch (ev.type) {
			case SDL_QUIT:
				shouldRun = false;
				break;
		}
		auto end = std::chrono::steady_clock::now();
		auto deltaTime = std::chrono::duration<double, std::milli>(end - begin);
		end = begin;

		renderer.beginFrame(color);
		
		renderer.endFrame();

		if (delay > deltaTime.count())
			SDL_Delay(delay - deltaTime.count());
	}

	SDL_Quit();

	return 0;
}