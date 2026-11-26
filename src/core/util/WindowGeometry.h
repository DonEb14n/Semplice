#pragma once

namespace semplice {

// Window position/size in device-independent pixels, as stored in settings.ini.
struct WindowGeometry {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool maximised = false;
    bool valid = false;  // false = nothing saved yet
};

}  // namespace semplice
