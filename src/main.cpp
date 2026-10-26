// Entry point only - per spec Rule 1, no business logic lives here.
// Everything real happens inside app::Application.
//
// Plain int main(argc, argv) is the standard Qt6 entry point: linking
// Qt6::Widgets on a WIN32 add_executable() target pulls in Qt's own
// WinMain shim automatically, so we never write WinMain/wWinMain by hand.

#include "app/Application.hpp"

int main(int argc, char* argv[])
{
    app::Application application;
    return application.Run(argc, argv);
}
