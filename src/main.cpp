#include "Frontend.h"
#include "DemoCore.h" // Replace with your "PS1Core.h" or other fantasy core headers

int main(int argc, char** argv) {
    // 1. Create your fantasy console core
    DemoCore core;

    // 2. Pass it to the generic frontend engine and run
    Frontend frontend(core);
    return frontend.run(640, 480);
}