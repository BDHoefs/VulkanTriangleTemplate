#define SDL_MAIN_HANDLED

#include <iostream>

#include <SDL.h>

#include <Application.hpp>
#include <Renderer.hpp>

int main()
{
    try {
        Application app;
        app.init("VulkanTriangle", std::make_tuple(640, 480));
        Renderer renderer(app.getWindow());
        renderer.init();

        while (!app.shouldExit()) {
            app.processEvents();
            renderer.update();
        }

        renderer.exit();
        app.exit();
    } catch (std::runtime_error e) {
        std::string message = "Unhandled exception: \n\n";
        message += e.what();

        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Unhandled exception", message.c_str(), NULL);
    }
    return 0;
}