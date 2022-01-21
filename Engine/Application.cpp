#include "Application.hpp"

void Application::init(std::string windowName, std::tuple<int, int> windowSize)
{
    window = SDL_CreateWindow(windowName.c_str(), SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        std::get<0>(windowSize), std::get<1>(windowSize), SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
}

void Application::processEvents()
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            exitEvent = true;
        }
    }
}

void Application::exit()
{
    SDL_DestroyWindow(window);
}
