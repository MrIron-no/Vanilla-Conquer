//
// Win32 and COM compatibility shims for the Westwood Online lobby code.
//
// The lobby UI was written against the Win32 API. Rather than rewrite every
// call site, the handful of types and functions it uses are provided here for
// non-Windows builds. On Windows the real headers are used.
//
// This file is part of Vanilla Conquer and is licensed under the GPLv3.
//
#ifndef WOLCOMPAT_H
#define WOLCOMPAT_H

#include <stddef.h>
#include <string.h>
#include <strings.h>

#ifdef _WIN32
#include <windows.h>
#include <unknwn.h>
#else

typedef long HRESULT;
typedef unsigned int DWORD;
typedef unsigned short WORD;
typedef unsigned char BYTE;
typedef int BOOL;
typedef unsigned int UINT;
typedef long LONG;
typedef unsigned long ULONG;
typedef const char* LPCSTR;
typedef char* LPSTR;
typedef unsigned char* LPBYTE;
typedef unsigned int* LPDWORD;
typedef void* HANDLE;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define __stdcall
#define STDMETHOD(method)        virtual HRESULT method
#define STDMETHOD_(type, method) virtual type method
#define STDMETHODIMP             HRESULT
#define STDMETHODIMP_(type)      type

#define S_OK             ((HRESULT)0L)
#define S_FALSE          ((HRESULT)1L)
#define E_FAIL           ((HRESULT)0x80004005L)
#define E_INVALIDARG     ((HRESULT)0x80070057L)
#define E_OUTOFMEMORY    ((HRESULT)0x8007000EL)
#define E_NOINTERFACE    ((HRESULT)0x80004002L)
#define E_NOTIMPL        ((HRESULT)0x80004001L)
#define SUCCEEDED(hr)    (((HRESULT)(hr)) >= 0)
#define FAILED(hr)       (((HRESULT)(hr)) < 0)
#define SEVERITY_SUCCESS 0
#define SEVERITY_ERROR   1
#define FACILITY_ITF     4
#define MAKE_HRESULT(sev, fac, code)                                                                                   \
    ((HRESULT)(((unsigned long)(sev) << 31) | ((unsigned long)(fac) << 16) | ((unsigned long)(code))))

#define _stricmp  strcasecmp
#define _strnicmp strncasecmp
#define stricmp   strcasecmp
#define strnicmp  strncasecmp

struct SYSTEMTIME
{
    WORD wYear;
    WORD wMonth;
    WORD wDayOfWeek;
    WORD wDay;
    WORD wHour;
    WORD wMinute;
    WORD wSecond;
    WORD wMilliseconds;
};
void GetSystemTime(SYSTEMTIME* st);

/* Milliseconds since an arbitrary epoch, monotonic. */
DWORD timeGetTime(void);
void Sleep(DWORD ms);

/*
** Key state query the dialogs poll. Takes a VK_ / KN_ code and returns the
** Win32 style bit mask: 0x8000 while the key is held.
*/
short GetAsyncKeyState(int key);

inline LONG InterlockedIncrement(LONG* v)
{
    return ++(*v);
}
inline LONG InterlockedDecrement(LONG* v)
{
    return --(*v);
}

#endif // !_WIN32

#endif // WOLCOMPAT_H
