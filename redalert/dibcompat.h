//
// Portable device-independent bitmap helpers for the Westwood Online lobby.
//
// The lobby's icon list draws 8-bit .bmp icons kept in WOLAPI.MIX. The
// original code loaded them through the Win32 DIB helpers; this provides the
// subset it uses on every platform, keeping the packed Windows DIB memory
// layout (info header, 256 RGBQUADs, bottom-up rows padded to 4 bytes) so the
// drawing code is unchanged.
//
// This file is part of Vanilla Conquer and is licensed under the GPLv3.
//
#ifndef DIBCOMPAT_H
#define DIBCOMPAT_H

#include "wolcompat.h"

#ifdef _WIN32
#include <windows.h>
#else

#pragma pack(push, 1)
struct BITMAPFILEHEADER
{
    WORD bfType;
    DWORD bfSize;
    WORD bfReserved1;
    WORD bfReserved2;
    DWORD bfOffBits;
};
#pragma pack(pop)

struct BITMAPINFOHEADER
{
    DWORD biSize;
    LONG biWidth;
    LONG biHeight;
    WORD biPlanes;
    WORD biBitCount;
    DWORD biCompression;
    DWORD biSizeImage;
    LONG biXPelsPerMeter;
    LONG biYPelsPerMeter;
    DWORD biClrUsed;
    DWORD biClrImportant;
};
typedef BITMAPINFOHEADER* LPBITMAPINFOHEADER;

struct RGBQUAD
{
    BYTE rgbBlue;
    BYTE rgbGreen;
    BYTE rgbRed;
    BYTE rgbReserved;
};

#define BI_RGB 0

#endif // !_WIN32

typedef void* HDIB;

/* A DIB "handle" is simply a pointer to the packed DIB; locking is a no-op. */
inline void* GlobalLock(HDIB h)
{
    return h;
}
inline int GlobalUnlock(HDIB)
{
    return 0;
}

HDIB LoadDIB_FromMemory(const unsigned char* data, DWORD size);
void DestroyDIB(HDIB h);
DWORD DIBWidth(LPCSTR dib);
DWORD DIBHeight(LPCSTR dib);
WORD DIBNumColors(LPCSTR dib);
WORD PaletteSize(LPCSTR dib);
LPSTR FindDIBBits(LPCSTR dib);

#endif // DIBCOMPAT_H
