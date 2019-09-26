#ifndef PROMPTS_H
#define PROMPTS_H

#include <windows.h>
#include <string>
#include <vector>

UINT CloseWindowPrompt (HWND hWnd); // IDYES/IDNO/IDCANCEL

bool DeleteListPrompt (HWND hWnd, const std::wstring & list); // true/false
bool DeleteListGroupPrompt (HWND hWnd, const std::wstring & list, const std::wstring & group); // true/false
bool DeleteListItemsPrompt (HWND hWnd, std::size_t n); // true/false

#endif


