#include "Application.hpp"

// SDL obtains UTF-8 arguments from the native Windows command line, including
// installation paths with characters outside the process ANSI code page.
#include <SDL3/SDL_main.h>

int main(int argc, char** argv)
{
    monopoly::Application app;
    return app.run(argc, argv);
}
