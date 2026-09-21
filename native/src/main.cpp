#include "integration/app.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    helltime::integration::NativeApp app(instance, showCommand);
    return app.Run();
}
