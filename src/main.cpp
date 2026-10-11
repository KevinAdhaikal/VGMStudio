#include "app/app.hpp"
#include <iostream>
#include <SDL.h>

int main(int argc, char* argv[]) {
    const char* initialFile = (argc > 1) ? argv[1] : nullptr;
    vgmstudio::App app;
    if (!app.Init("VGM Studio v1.0.0 - Retro Sound Player & Piano Roll", 1280, 768, initialFile)) {
        std::cerr << "[VGM Studio] Failed to initialize application!" << std::endl;
        return 1;
    }

    std::cout << "[VGM Studio] Initialized successfully. Running main loop." << std::endl;
    app.Run();

    std::cout << "[VGM Studio] Exiting cleanly." << std::endl;
    return 0;
}
