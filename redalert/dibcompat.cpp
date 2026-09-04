//
// Portable DIB helpers. See dibcompat.h.
//
// This file is part of Vanilla Conquer and is licensed under the GPLv3.
//
#include "dibcompat.h"

#include <stdlib.h>
#include <string.h>

WORD DIBNumColors(LPCSTR dib)
{
    const BITMAPINFOHEADER* bi = (const BITMAPINFOHEADER*)dib;
    if (bi->biClrUsed != 0) {
        return (WORD)bi->biClrUsed;
    }
    switch (bi->biBitCount) {
    case 1:
        return 2;
    case 4:
        return 16;
    case 8:
        return 256;
    default:
        return 0;
    }
}

WORD PaletteSize(LPCSTR dib)
{
    return (WORD)(DIBNumColors(dib) * sizeof(RGBQUAD));
}

LPSTR FindDIBBits(LPCSTR dib)
{
    const BITMAPINFOHEADER* bi = (const BITMAPINFOHEADER*)dib;
    return (LPSTR)dib + bi->biSize + PaletteSize(dib);
}

/*
** A null dib means the icon was not found in the mix files, which is normal:
** the lobby icons shipped with the online patch data and are absent from
** retail data. Callers treat a zero size as "no icon".
*/
DWORD DIBWidth(LPCSTR dib)
{
    if (dib == NULL) {
        return 0;
    }

    return (DWORD)((const BITMAPINFOHEADER*)dib)->biWidth;
}

DWORD DIBHeight(LPCSTR dib)
{
    if (dib == NULL) {
        return 0;
    }

    LONG h = ((const BITMAPINFOHEADER*)dib)->biHeight;
    return (DWORD)(h < 0 ? -h : h);
}

/*
** Parses a .bmp file image in memory into a packed DIB: info header, colour
** table, then the pixel rows exactly as stored in the file. Only uncompressed
** 8-bit images are accepted, which is all the lobby icons use.
*/
HDIB LoadDIB_FromMemory(const unsigned char* data, DWORD size)
{
    if (data == NULL || size < sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)) {
        return NULL;
    }
    BITMAPFILEHEADER fh;
    memcpy(&fh, data, sizeof(fh));
    if (fh.bfType != 0x4d42) {
        return NULL;
    }
    BITMAPINFOHEADER bi;
    memcpy(&bi, data + sizeof(fh), sizeof(bi));
    if (bi.biSize < sizeof(BITMAPINFOHEADER) || bi.biBitCount != 8 || bi.biCompression != BI_RGB || bi.biWidth <= 0
        || bi.biHeight == 0) {
        return NULL;
    }
    bi.biSize = sizeof(BITMAPINFOHEADER);
    DWORD colors = bi.biClrUsed ? bi.biClrUsed : 256;
    if (colors > 256) {
        return NULL;
    }
    DWORD pitch = ((DWORD)bi.biWidth + 3) & ~3u;
    DWORD height = (DWORD)(bi.biHeight < 0 ? -bi.biHeight : bi.biHeight);
    DWORD bits_size = pitch * height;
    DWORD palette_offset = sizeof(fh) + ((const BITMAPINFOHEADER*)(data + sizeof(fh)))->biSize;
    DWORD bits_offset = fh.bfOffBits ? fh.bfOffBits : palette_offset + colors * sizeof(RGBQUAD);
    if (palette_offset + colors * sizeof(RGBQUAD) > size || bits_offset + bits_size > size) {
        return NULL;
    }
    bi.biClrUsed = 256;
    bi.biSizeImage = bits_size;

    unsigned char* dib = (unsigned char*)malloc(sizeof(bi) + 256 * sizeof(RGBQUAD) + bits_size);
    if (dib == NULL) {
        return NULL;
    }
    memcpy(dib, &bi, sizeof(bi));
    memset(dib + sizeof(bi), 0, 256 * sizeof(RGBQUAD));
    memcpy(dib + sizeof(bi), data + palette_offset, colors * sizeof(RGBQUAD));
    memcpy(dib + sizeof(bi) + 256 * sizeof(RGBQUAD), data + bits_offset, bits_size);
    return dib;
}

void DestroyDIB(HDIB h)
{
    free(h);
}
