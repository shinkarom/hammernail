#include <string>
#include "Frontend.h"
#include "DemoCore.h" // Replace with your actual core header (e.g., MiniRV64Core.h)

int main(int argc, char** argv) {
    // 1. Create your fantasy console core
    DemoCore core;

    // 2. Extract optional ROM path from command-line arguments
    std::string romPath = (argc > 1) ? argv[1] : "";

    // 3. Pass the core and initial ROM path to the frontend and run
    Frontend frontend(core);
    return frontend.run(romPath, 640, 480);
}