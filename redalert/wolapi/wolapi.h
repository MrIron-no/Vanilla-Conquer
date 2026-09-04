//
// Portable replacement for the Westwood Online COM API header (wolapi.h).
//
// The original lobby code talked to wolapi.dll through three COM interfaces
// with connection-point event sinks. This header keeps the same names, types
// and method signatures so the lobby UI compiles unchanged, but the
// interfaces are plain abstract C++ classes implemented by the lobby client
// in wollobby.cpp. Nothing here depends on COM.
//
// This file is part of Vanilla Conquer and is licensed under the GPLv3.
//
#ifndef WOLAPI_WOLAPI_H
#define WOLAPI_WOLAPI_H

#include "../wolcompat.h"

#include <time.h>

/*
** Data structures shared with the lobby UI. Layout matches the original IDL
** so the UI's field access and list handling keep working.
*/
struct Ladder
{
    unsigned int sku;
    unsigned int team_no;
    unsigned int wins;
    unsigned int losses;
    unsigned int points;
    unsigned int kills;
    unsigned int rank;
    unsigned int rung;
    unsigned int disconnects;
    unsigned int team_rung;
    unsigned int provisional;
    unsigned int last_game_date;
    unsigned int win_streak;
    unsigned int reserved1;
    unsigned int reserved2;
    struct Ladder* next;
    unsigned char login_name[40];
};

typedef int GroupID;

struct WOLServer
{
    int gametype;
    int chattype;
    int timezone;
    float longitude;
    float lattitude;
    struct WOLServer* next;
    unsigned char name[71];
    unsigned char connlabel[5];
    unsigned char conndata[128];
    unsigned char login[10];
    unsigned char password[10];
};

struct Channel
{
    int type;
    unsigned int minUsers;
    unsigned int maxUsers;
    unsigned int currentUsers;
    unsigned int official;
    unsigned int tournament;
    unsigned int ingame;
    unsigned int flags;
    unsigned long reserved;
    unsigned long ipaddr;
    int latency;
    int hidden;
    struct Channel* next;
    unsigned char name[17];
    unsigned char topic[81];
    unsigned char location[65];
    unsigned char key[9];
    unsigned char exInfo[41];
};

struct User
{
    unsigned int flags;
    GroupID group;
    unsigned long reserved;
    unsigned long reserved2;
    unsigned long reserved3;
    unsigned long reserved4;
    unsigned long ipaddr;
    unsigned long squad_icon;
    struct User* next;
    unsigned char name[10];
    unsigned char squadname[41];
};

struct Group
{
    GroupID ident;
    int type;
    unsigned int members;
    struct Group* next;
    unsigned char name[65];
};

struct Update
{
    unsigned long SKU;
    unsigned long version;
    int required;
    struct Update* next;
    unsigned char server[65];
    unsigned char patchpath[256];
    unsigned char patchfile[33];
    unsigned char login[33];
    unsigned char password[65];
    unsigned char localpath[256];
};

/*
** Interface identifiers. Only used by the sinks' QueryInterface, which the
** lobby client never calls, but kept so that code compiles unchanged.
*/
#ifndef _WIN32
struct IID
{
    int value;
    bool operator==(const IID& other) const
    {
        return value == other.value;
    }
    bool operator!=(const IID& other) const
    {
        return value != other.value;
    }
};
typedef const IID& REFIID;

struct IUnknown
{
    virtual ~IUnknown()
    {
    }
    virtual HRESULT QueryInterface(const IID& iid, void** ppv) = 0;
    virtual ULONG AddRef() = 0;
    virtual ULONG Release() = 0;
};
#endif

#define WOL_IID(name, num) static const IID name = WOL_IID_INIT(num)
#ifdef _WIN32
#define WOL_IID_INIT(num) {0x4DD3BAF0 + (num), 0x7579, 0x11D1, {0xB1, 0xC6, 0x00, 0x60, 0x97, 0x17, 0x65, 0x56}}
#else
#define WOL_IID_INIT(num) {num}
#endif
WOL_IID(IID_IChat, 1);
WOL_IID(IID_IChatEvent, 2);
WOL_IID(IID_INetUtil, 3);
WOL_IID(IID_INetUtilEvent, 4);
WOL_IID(IID_IDownload, 5);
WOL_IID(IID_IDownloadEvent, 6);
WOL_IID(IID_IConnectionPointContainer, 7);
#ifndef _WIN32
WOL_IID(IID_IUnknown, 0);
#endif

/*
** Event sinks implemented by the game (rawolapi.cpp).
*/
struct IChatEvent : public IUnknown
{
    STDMETHOD(OnServerList)(HRESULT res, WOLServer* servers) = 0;
    STDMETHOD(OnUpdateList)(HRESULT res, Update* updates) = 0;
    STDMETHOD(OnServerError)(HRESULT res, LPCSTR ircmsg) = 0;
    STDMETHOD(OnConnection)(HRESULT res, LPCSTR motd) = 0;
    STDMETHOD(OnMessageOfTheDay)(HRESULT res, LPCSTR motd) = 0;
    STDMETHOD(OnChannelList)(HRESULT res, Channel* channels) = 0;
    STDMETHOD(OnChannelCreate)(HRESULT res, Channel* channel) = 0;
    STDMETHOD(OnChannelJoin)(HRESULT res, Channel* channel, User* user) = 0;
    STDMETHOD(OnChannelLeave)(HRESULT res, Channel* channel, User* user) = 0;
    STDMETHOD(OnChannelTopic)(HRESULT res, Channel* channel, LPCSTR topic) = 0;
    STDMETHOD(OnPrivateAction)(HRESULT res, User* user, LPCSTR action) = 0;
    STDMETHOD(OnPublicAction)(HRESULT res, Channel* channel, User* user, LPCSTR action) = 0;
    STDMETHOD(OnUserList)(HRESULT res, Channel* channel, User* users) = 0;
    STDMETHOD(OnPublicMessage)(HRESULT res, Channel* channel, User* user, LPCSTR message) = 0;
    STDMETHOD(OnPrivateMessage)(HRESULT res, User* user, LPCSTR message) = 0;
    STDMETHOD(OnSystemMessage)(HRESULT res, LPCSTR message) = 0;
    STDMETHOD(OnNetStatus)(HRESULT res) = 0;
    STDMETHOD(OnLogout)(HRESULT status, User* user) = 0;
    STDMETHOD(OnPrivateGameOptions)(HRESULT res, User* user, LPCSTR options) = 0;
    STDMETHOD(OnPublicGameOptions)(HRESULT res, Channel* channel, User* user, LPCSTR options) = 0;
    STDMETHOD(OnGameStart)(HRESULT res, Channel* channel, User* users, int gameid) = 0;
    STDMETHOD(OnUserKick)(HRESULT res, Channel* channel, User* kicked, User* kicker) = 0;
    STDMETHOD(OnUserIP)(HRESULT res, User* user) = 0;
    STDMETHOD(OnFind)(HRESULT res, Channel* chan) = 0;
    STDMETHOD(OnPageSend)(HRESULT res) = 0;
    STDMETHOD(OnPaged)(HRESULT res, User* user, LPCSTR message) = 0;
    STDMETHOD(OnServerBannedYou)(HRESULT res, time_t bannedTill) = 0;
    STDMETHOD(OnUserFlags)(HRESULT res, LPCSTR name, unsigned int flags, unsigned int mask) = 0;
    STDMETHOD(OnChannelBan)(HRESULT res, LPCSTR name, int banned) = 0;
};

struct INetUtilEvent : public IUnknown
{
    STDMETHOD(OnPing)(HRESULT res, int time, unsigned long ip, int handle) = 0;
    STDMETHOD(OnLadderList)(HRESULT res, Ladder* list, int totalCount, long timeStamp, int keyRung) = 0;
    STDMETHOD(OnGameresSent)(HRESULT res) = 0;
};

struct IDownloadEvent : public IUnknown
{
    STDMETHOD(OnEnd)() = 0;
    STDMETHOD(OnError)(int error) = 0;
    STDMETHOD(OnProgressUpdate)(int bytesread, int totalsize, int timetaken, int timeleft) = 0;
    STDMETHOD(OnQueryResume)() = 0;
    STDMETHOD(OnStatusUpdate)(int status) = 0;
};

/*
** Services the lobby UI calls. Implemented by the lobby client.
*/
struct IChat : public IUnknown
{
    STDMETHOD(PumpMessages)() = 0;
    STDMETHOD(RequestServerList)
    (unsigned long SKU, unsigned long current_version, LPCSTR loginname, LPCSTR password, int timeout) = 0;
    STDMETHOD(RequestConnection)(WOLServer* server, int timeout, int domangle) = 0;
    STDMETHOD(RequestChannelList)(int channelType, int autoping) = 0;
    STDMETHOD(RequestChannelCreate)(Channel* channel) = 0;
    STDMETHOD(RequestChannelJoin)(Channel* channel) = 0;
    STDMETHOD(RequestChannelLeave)() = 0;
    STDMETHOD(RequestUserList)() = 0;
    STDMETHOD(RequestPublicMessage)(LPCSTR message) = 0;
    STDMETHOD(RequestPrivateMessage)(User* users, LPCSTR message) = 0;
    STDMETHOD(RequestLogout)() = 0;
    STDMETHOD(RequestPrivateGameOptions)(User* users, LPCSTR options) = 0;
    STDMETHOD(RequestPublicGameOptions)(LPCSTR options) = 0;
    STDMETHOD(RequestPublicAction)(LPCSTR action) = 0;
    STDMETHOD(RequestPrivateAction)(User* users, LPCSTR action) = 0;
    STDMETHOD(RequestGameStart)(User* users) = 0;
    STDMETHOD(RequestChannelTopic)(LPCSTR topic) = 0;
    STDMETHOD(GetVersion)(unsigned long* version) = 0;
    STDMETHOD(RequestUserKick)(User* user) = 0;
    STDMETHOD(RequestUserIP)(User* user) = 0;
    STDMETHOD(GetGametypeInfo)
    (unsigned int gtype, int icon_size, unsigned char** bitmap, int* bmp_bytes, LPCSTR* name, LPCSTR* URL) = 0;
    STDMETHOD(RequestFind)(User* user) = 0;
    STDMETHOD(RequestPage)(User* user, LPCSTR message) = 0;
    STDMETHOD(SetFindPage)(int findOn, int pageOn) = 0;
    STDMETHOD(SetSquelch)(User* user, int squelch) = 0;
    STDMETHOD(GetSquelch)(User* user) = 0;
    STDMETHOD(SetChannelFilter)(int channelType) = 0;
    STDMETHOD(RequestGameEnd)() = 0;
    STDMETHOD(SetLangFilter)(int onoff) = 0;
    STDMETHOD(RequestChannelBan)(LPCSTR name, int ban) = 0;
    STDMETHOD(GetGametypeList)(LPCSTR* list) = 0;
    STDMETHOD(GetHelpURL)(LPCSTR* url) = 0;
    STDMETHOD(SetProductSKU)(unsigned long SKU) = 0;
    STDMETHOD(GetNick)(int num, LPCSTR* nick, LPCSTR* pass) = 0;
    STDMETHOD(SetNick)(int num, LPCSTR nick, LPCSTR pass, int domangle) = 0;
    STDMETHOD(GetLobbyCount)(int* count) = 0;
    STDMETHOD(RequestRawMessage)(LPCSTR ircmsg) = 0;
    STDMETHOD(GetAttributeValue)(LPCSTR attrib, LPCSTR* value) = 0;
    STDMETHOD(SetAttributeValue)(LPCSTR attrib, LPCSTR value) = 0;
    STDMETHOD(SetChannelExInfo)(LPCSTR info) = 0;
    STDMETHOD(StopAutoping)() = 0;
};

struct INetUtil : public IUnknown
{
    STDMETHOD(RequestGameresSend)(LPCSTR host, int port, unsigned char* data, int length) = 0;
    STDMETHOD(RequestLadderList)
    (LPCSTR host, int port, LPCSTR keys, unsigned long SKU, int team, int cond, int sort) = 0;
    STDMETHOD(RequestPing)(LPCSTR host, int timeout, int* handle) = 0;
    STDMETHOD(PumpMessages)() = 0;
    STDMETHOD(GetAvgPing)(unsigned long ip, int* avg) = 0;
};

struct IDownload : public IUnknown
{
    STDMETHOD(DownloadFile)
    (LPCSTR server, LPCSTR login, LPCSTR password, LPCSTR localfile, LPCSTR remotefile, LPCSTR regkey) = 0;
    STDMETHOD(Abort)() = 0;
    STDMETHOD(PumpMessages)() = 0;
};

#endif // WOLAPI_WOLAPI_H
