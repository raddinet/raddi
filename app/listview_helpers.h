#ifndef LISTVIEW_HELPERS_H
#define LISTVIEW_HELPERS_H

#include <windows.h>

#include <string>
#include <vector>

LPARAM ListView_GetItemParam (HWND hListView, int index);
std::vector <std::wstring> ListView_GetGroupNames (HWND);

int ListView_GetFocusedGroupId (HWND, std::wstring * title = nullptr, RECT * r = nullptr);
int ListView_MoveItemsToGroup (HWND, int groupID, const std::vector <int> & indexes);
int ListView_GetItemGroupId (HWND hListView, int index);
int ListView_GetGroupId (HWND hListView, int groupIndex);
int ListView_GetGroupIdIndex (HWND hListView, int groupID);
int ListView_SetGroupTitle (HWND hListView, int groupID, const std::wstring &);
int ListView_DeleteGroupAndItems (HWND hListView, int groupID);
int ListView_DeleteEmptyGroups (HWND hListView);
int ListView_CopyGroupToListView (HWND hListView, int groupID, HWND hTargetListView);

#endif
