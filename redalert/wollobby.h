//
// Lobby client: implements the IChat / INetUtil / IDownload interfaces the
// Westwood Online lobby UI talks to, against the ra-lobby WebSocket protocol.
//
// This file is part of Vanilla Conquer and is licensed under the GPLv3.
//
#ifdef WOLAPI_INTEGRATION

#ifndef WOLLOBBY_H
#define WOLLOBBY_H

#include "wolapi/wolapi.h"

/*
** Creates the client objects and attaches the game's event sinks. Returns
** false if the client could not be set up. The objects are owned by the
** caller and freed with Release().
*/
bool WOL_Create_Clients(IChat** chat, INetUtil** netutil, IChatEvent* chat_sink, INetUtilEvent* netutil_sink);

/* Address of the lobby service, e.g. wss://lobby.example.org/ws */
const char* WOL_Server_URL(void);

#endif // WOLLOBBY_H
#endif // WOLAPI_INTEGRATION
