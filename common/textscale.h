#ifndef RADDI_TEXTSCALE_H
#define RADDI_TEXTSCALE_H

#include <Windows.h>

// TextScale
//  - singleton tracking the per-user "Settings > Accessibility > Text size" feature for UWP Apps
//  - there is no documented Win32 API to get this value, so we read it from registry
//  - we craft this notification facility because:
//     - the OS doesn't always broadcast WM_SETTINGCHANGE message when the scale factor changes
//     - the "Accessibility" key and "TextScaleFactor" value may not exist at first
//  - there is no destructor, the object lives for the lifetime of the process, OS cleans up
//
class TextScale {
    HKEY    hKey = NULL; // HKCU\SOFTWARE\Microsoft[\Accessibility]
    bool    parent = false; // if true, we are waiting for Accessibilty subkey to be created first

public:
    DWORD   current = 100;
    HANDLE  hEvent = NULL;

public:
    bool Initialize ();

    // OnEvent
    //  - should be called whenever this->hEvent gets signalled
    //  - returns 'true' if the scale factor might have changed, and application should redraw the GUI
    //
    bool OnEvent ();

    // Apply 
    //  - adjusts font height according to current text scale factor
    //  - NOTE: if all fonts are to be scaled, this can be called from Window::Font::update
    //
    inline void Apply (LOGFONT & lf) const {
        lf.lfHeight = MulDiv (lf.lfHeight, this->current, 100);
    }

private:
    bool ReOpenKeys ();
    DWORD GetCurrentTextScaleFactor () const;
};

#endif
