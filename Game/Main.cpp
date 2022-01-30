#define SDL_MAIN_HANDLED

#include <iostream>

#include <SDL.h>

#include <Application.hpp>
#include <ECS/ECS.hpp>
#include <Renderer/Renderer.hpp>

using namespace ECS;

class MeshRotate : ECS::System {
public:
    void init() override { }
    void update(double dt_ms) override
    {
        EntityManager em;
        em.each_component<Transform>([](ECS::Entity& e, Transform* t) {
            t->rot.y += 0.5f;
        });
    }
    void exit() override { }
};

int main()
{
    try {
        Application app;
        app.init("VulkanTriangle", std::make_tuple(640, 480));
        Renderer renderer(app.getWindow());
        renderer.init();

        EntityManager em;
        em.add_system<MeshRotate>();

        Entity triangle = em.add_entity();
        triangle.add_component<Mesh>();
        Mesh* triMesh = triangle.get_component<Mesh>().value();

        std::vector<Vertex> triVerts(3);
        triVerts[0].pos = glm::vec3(1.f, 1.f, 0.f);
        triVerts[1].pos = glm::vec3(-1.f, 1.f, 0.f);
        triVerts[2].pos = glm::vec3(0.f, -1.f, 0.f);

        triVerts[0].color = glm::vec3(1.f, 0.f, 0.f);
        triVerts[1].color = glm::vec3(0.f, 1.f, 0.f);
        triVerts[2].color = glm::vec3(0.f, 0.f, 1.f);

        triMesh->set_vertices(triVerts);

        triangle.add_component<Transform>();

        while (!app.shouldExit()) {
            app.processEvents();
            em.update(0.01f);
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