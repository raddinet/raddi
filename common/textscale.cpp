#include "textscale.h"
#include <VersionHelpers.h>
// #include <shellapi.h>

bool TextScale::Initialize () {
    if (IsWindows10OrGreater ()) {
        this->hEvent = CreateEvent (NULL, FALSE, FALSE, NULL);
        if (this->hEvent)
            return this->ReOpenKeys ();
    }
    return false;
}

bool TextScale::OnEvent () {
    if (this->parent) {
        // "Accessibility" subkey might have been created, try to acces it again
        if (this->ReOpenKeys ()) {
            // if the subkey was created, we now have new scale factor, so report that as change
            return this->parent == false;
        } else
            return false;

    } else {
        bool changed = false;
        // some value inside "Accessibility" subkey has changed, see if it was "TextScaleFactor"
        auto updated = this->GetCurrentTextScaleFactor ();
        if (this->current != updated) {
            this->current = updated;
            changed = true;
        }
        // re-register for next event
        RegNotifyChangeKeyValue (this->hKey, FALSE, REG_NOTIFY_CHANGE_LAST_SET | REG_NOTIFY_THREAD_AGNOSTIC, this->hEvent, TRUE);
        return changed;
    }
}

bool TextScale::ReOpenKeys () {
    if (this->hKey) {
        RegCloseKey (this->hKey);
        this->hKey = NULL;
    }
    if (RegOpenKeyEx (HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Accessibility", 0, KEY_NOTIFY | KEY_QUERY_VALUE, &this->hKey) == ERROR_SUCCESS) {
        if (RegNotifyChangeKeyValue (this->hKey, FALSE, REG_NOTIFY_CHANGE_LAST_SET | REG_NOTIFY_THREAD_AGNOSTIC, this->hEvent, TRUE) == ERROR_SUCCESS) {
            this->parent = false;
            this->current = this->GetCurrentTextScaleFactor ();
            return true;
        }
    } else {
        if (RegOpenKeyEx (HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft", 0, KEY_NOTIFY, &this->hKey) == ERROR_SUCCESS) {
            if (RegNotifyChangeKeyValue (this->hKey, FALSE, REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_THREAD_AGNOSTIC, this->hEvent, TRUE) == ERROR_SUCCESS) {
                this->parent = true;
                return true;
            }
        }
    }
    return false;
}

DWORD TextScale::GetCurrentTextScaleFactor () const {
    DWORD scale = 100;
    DWORD cb = sizeof scale;

    if ((this->parent == false) && (this->hKey != NULL) && (RegQueryValueEx (this->hKey, L"TextScaleFactor", NULL, NULL, (LPBYTE) &scale, &cb) == ERROR_SUCCESS)) {
        return scale;
    } else
        return 100;
}
