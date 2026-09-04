//
// Password edit control. See passedit.h.
//
// This file is part of Vanilla Conquer and is licensed under the GPLv3.
//
#ifdef WOLAPI_INTEGRATION

#include "function.h"
#include "passedit.h"

#include <string.h>

void PassEditClass::Set_Focus(void)
{
    if (bClearOnNextSetFocus) {
        bClearOnNextSetFocus = false;
        char* text = Get_Text();
        if (text != NULL) {
            *text = '\0';
        }
        Length = 0;
        Flag_To_Redraw();
    }
    WOLEditClass::Set_Focus();
}

void PassEditClass::Draw_Text(char const* text)
{
    char masked[128];
    size_t len = text ? strlen(text) : 0;
    if (len >= sizeof(masked)) {
        len = sizeof(masked) - 1;
    }
    memset(masked, '*', len);
    masked[len] = '\0';
    WOLEditClass::Draw_Text(masked);
}

#endif // WOLAPI_INTEGRATION
