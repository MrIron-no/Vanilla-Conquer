//
// Password edit control for the Westwood Online login dialog.
//
// The original PASSEDIT.CPP/H was not part of the released source. This is a
// reconstruction: an edit box that echoes every character as an asterisk and
// clears itself the first time it gains focus when it was pre-filled with a
// saved password.
//
// This file is part of Vanilla Conquer and is licensed under the GPLv3.
//
#ifdef WOLAPI_INTEGRATION

#ifndef PASSEDIT_H
#define PASSEDIT_H

#include "woledit.h"

class PassEditClass : public WOLEditClass
{
public:
    PassEditClass(int id, char* text, int max_len, TextPrintType flags, int x, int y, int w, int h, EditStyle style)
        : WOLEditClass(id, text, max_len, flags, x, y, w, h, style)
        , bClearOnNextSetFocus(false)
    {
    }

    virtual void Set_Focus(void);

    // Set when the field holds a saved (already mangled) password: typing
    // into it must start from an empty field.
    bool bClearOnNextSetFocus;

protected:
    virtual void Draw_Text(char const* text);
};

#endif // PASSEDIT_H
#endif // WOLAPI_INTEGRATION
