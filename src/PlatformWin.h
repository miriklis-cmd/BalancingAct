// PlatformWin.h - the one place that sets Windows API version/character-set
// macros and includes <windows.h>, so every .cpp/.h that needs Windows types
// gets identical settings (main.cpp used to do this inline at its top).
#pragma once

#ifndef WINVER
#define WINVER 0x0601
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
// <windows.h> defines min/max as macros unless this is set first, which
// silently mangles std::min/std::max calls - see the note in main.cpp.
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <commctrl.h>
