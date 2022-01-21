#pragma once

#include <string>
#include <tuple>

#include <SDL.h>

class Application {
public:
    void init(std::string windowName, std::tuple<int, int> windowSize);
    void processEvents();
    void exit();

    bool shouldExit() { return exitEvent; }
    SDL_Window* getWindow() { return window; }

private:
    SDL_Window* window;

    bool exitEvent = false;
};