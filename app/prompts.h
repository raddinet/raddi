#ifndef PROMPTS_H
#define PROMPTS_H

#include <windows.h>
#include <string>

bool DeleteListPrompt (HWND hWnd, const std::wstring & list); // true/false
UINT CloseWindowPrompt (HWND hWnd); // IDYES/IDNO/IDCANCEL

#endif


