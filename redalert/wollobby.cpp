//
// Lobby client. See wollobby.h and the protocol in the ra-lobby repository
// (docs/protocol.md).
//
// One WebSocket carries JSON requests, responses and events. IXWebSocket
// runs the socket on its own thread; everything it delivers is queued and
// only turned into sink callbacks from PumpMessages(), which the lobby UI
// calls from the game thread. That keeps the 1998 UI code single threaded.
//
// This file is part of Vanilla Conquer and is licensed under the GPLv3.
//
#ifdef WOLAPI_INTEGRATION

// Standard library and third-party headers first: the game's headers define
// a global operator| for enums that breaks libstdc++ if it comes earlier.
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>
#include <nlohmann/json.hpp>

#include "function.h"
#include "wollobby.h"
#include "wolapi/chatdefs.h"
#include "wolapi/netutildefs.h"
#include "common/sockets.h"
#include "common/internet.h"

using json = nlohmann::json;

namespace
{

    const char* const WOL_SECTION = "WOL";
    const int NICK_SLOTS = 2;
    const int REQUEST_TIMEOUT_MS = 15000;
    const int GAME_TYPE_RA = 21;

    /*
** ---------------------------------------------------------------------------
** Small helpers to move between JSON and the structs the UI understands.
** ---------------------------------------------------------------------------
*/
    void Copy_Str(unsigned char* dst, size_t size, const std::string& src)
    {
        size_t n = src.size();
        if (n >= size) {
            n = size - 1;
        }
        memcpy(dst, src.data(), n);
        dst[n] = 0;
    }

    unsigned long IP_From_String(const std::string& ip)
    {
        if (ip.empty()) {
            return 0;
        }
        return (unsigned long)inet_addr(ip.c_str());
    }

    int JInt(const json& j, const char* key, int def = 0)
    {
        auto it = j.find(key);
        if (it == j.end() || !it->is_number()) {
            return def;
        }
        return it->get<int>();
    }

    std::string JStr(const json& j, const char* key)
    {
        auto it = j.find(key);
        if (it == j.end() || !it->is_string()) {
            return std::string();
        }
        return it->get<std::string>();
    }

    /*
** Builds a Channel from JSON. The struct has no constructor, so zero it.
*/
    Channel Channel_From_JSON(const json& j)
    {
        Channel c;
        memset(&c, 0, sizeof(c));
        c.type = JInt(j, "type");
        c.minUsers = JInt(j, "min_users");
        c.maxUsers = JInt(j, "max_users");
        c.currentUsers = JInt(j, "current_users");
        c.official = JInt(j, "official");
        c.tournament = JInt(j, "tournament");
        c.ingame = JInt(j, "ingame");
        c.flags = JInt(j, "flags");
        auto r = j.find("reserved");
        c.reserved = (r != j.end() && r->is_number()) ? (unsigned long)r->get<long long>() : 0;
        c.ipaddr = IP_From_String(JStr(j, "host_ip"));
        c.latency = JInt(j, "latency");
        c.hidden = JInt(j, "hidden");
        Copy_Str(c.name, sizeof(c.name), JStr(j, "name"));
        Copy_Str(c.topic, sizeof(c.topic), JStr(j, "topic"));
        Copy_Str(c.location, sizeof(c.location), JStr(j, "location"));
        Copy_Str(c.exInfo, sizeof(c.exInfo), JStr(j, "ex_info"));
        return c;
    }

    User User_From_JSON(const json& j)
    {
        User u;
        memset(&u, 0, sizeof(u));
        u.flags = JInt(j, "flags");
        u.ipaddr = IP_From_String(JStr(j, "ip"));
        u.reserved2 = JInt(j, "port"); // UDP port travels here; the transport reads it.
        Copy_Str(u.name, sizeof(u.name), JStr(j, "name"));
        Copy_Str(u.squadname, sizeof(u.squadname), JStr(j, "squad"));
        return u;
    }

    User User_Named(const std::string& name, unsigned flags)
    {
        User u;
        memset(&u, 0, sizeof(u));
        u.flags = flags;
        Copy_Str(u.name, sizeof(u.name), name);
        return u;
    }

    Ladder Ladder_From_JSON(const json& j)
    {
        Ladder l;
        memset(&l, 0, sizeof(l));
        l.sku = JInt(j, "sku");
        l.wins = JInt(j, "wins");
        l.losses = JInt(j, "losses");
        l.points = JInt(j, "points");
        l.kills = JInt(j, "kills");
        l.rank = JInt(j, "rank");
        l.rung = JInt(j, "rung");
        l.disconnects = JInt(j, "disconnects");
        l.provisional = JInt(j, "provisional");
        l.last_game_date = JInt(j, "last_game");
        l.win_streak = JInt(j, "win_streak");
        Copy_Str(l.login_name, sizeof(l.login_name), JStr(j, "name"));
        return l;
    }

    /*
** Linked lists the sinks expect. They copy what they need, so the storage
** only has to live for the duration of the callback.
*/
    template <class T> struct LinkedList
    {
        std::vector<T> items;
        T* head()
        {
            if (items.empty()) {
                return NULL;
            }
            for (size_t i = 0; i < items.size(); i++) {
                items[i].next = (i + 1 < items.size()) ? &items[i + 1] : NULL;
            }
            return &items[0];
        }
    };

    HRESULT Error_To_HRESULT(const std::string& code)
    {
        if (code == "bad_pass")
            return CHAT_E_BADPASS;
        if (code == "nick_in_use")
            return CHAT_E_NICKINUSE;
        if (code == "banned")
            return CHAT_E_BANNED;
        if (code == "must_patch")
            return CHAT_E_MUSTPATCH;
        if (code == "channel_full")
            return CHAT_E_CHANNELFULL;
        if (code == "channel_exists")
            return CHAT_E_CHANNELEXISTS;
        if (code == "no_such_channel")
            return CHAT_E_CHANNELDOESNOTEXIST;
        if (code == "bad_key")
            return CHAT_E_BADCHANNELPASSWORD;
        if (code == "not_owner")
            return CHAT_E_NOT_OPER;
        if (code == "not_logged_in")
            return CHAT_E_NOTCONNECTED;
        if (code == "no_channel")
            return CHAT_E_NOCHANNEL;
        if (code == "ingame")
            return CHAT_E_JOINCHANNEL;
        if (code == "timeout")
            return CHAT_E_TIMEOUT;
        return CHAT_E_UNKNOWNRESPONSE;
    }

    /*
** ---------------------------------------------------------------------------
** The connection. Owns the socket, the inbound queue and the table of
** requests waiting for a response.
** ---------------------------------------------------------------------------
*/
    enum PendingKind
    {
        PK_NONE,
        PK_HELLO,
        PK_LOGIN,
        PK_REGISTER,
        PK_LOGOUT,
        PK_CHANNELS,
        PK_CREATE,
        PK_JOIN,
        PK_LEAVE,
        PK_USERS,
        PK_SIMPLE, // no callback, only error reporting
        PK_TOPIC,
        PK_KICK,
        PK_BAN,
        PK_GAMESTART,
        PK_USERIP,
        PK_FIND,
        PK_PAGE,
        PK_LADDER,
        PK_RESULTS,
    };

    struct Pending
    {
        PendingKind kind;
        DWORD sent;
        std::string name;
        std::string text;
        Channel channel;
    };

    class LobbyLink
    {
    public:
        LobbyLink()
            : ChatSink(NULL)
            , NetSink(NULL)
            , NextID(1)
            , Open(false)
            , Failed(false)
            , LoggedIn(false)
            , Lobbies(0)
        {
            memset(&CurrentChannel, 0, sizeof(CurrentChannel));
            ix::initNetSystem();
            Socket.setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) { On_Socket_Message(msg); });
            Socket.disableAutomaticReconnection();
            Socket.setPingInterval(25);
        }

        ~LobbyLink()
        {
            Socket.stop();
            ix::uninitNetSystem();
        }

        IChatEvent* ChatSink;
        INetUtilEvent* NetSink;
        std::string MyName;
        std::string MOTD;
        int Lobbies;
        Channel CurrentChannel;

        bool Is_Open() const
        {
            return Open;
        }
        bool Is_Logged_In() const
        {
            return LoggedIn;
        }

        /*
        ** The server only marks CHAT_USER_MYSELF on request responses, not on
        ** the broadcast events, so our own join/leave has to be recognised by
        ** name. The sinks branch entirely on that flag.
        */
        bool Is_Me(const unsigned char* name) const
        {
            return name != nullptr && !MyName.empty() && strcasecmp((const char*)name, MyName.c_str()) == 0;
        }

        /*
    ** Opens the socket and sends hello. Completion arrives through Pump().
    */
        void Connect(const char* url)
        {
            Failed = false;
            Open = false;
            LoggedIn = false;
            Socket.stop();
            Socket.setUrl(url);
            Socket.start();
            Pending p;
            p.kind = PK_HELLO;
            p.sent = timeGetTime();
            Queue_Request(p, json{{"op", "hello"}, {"client", "vanillara"}, {"version", "3.03"}, {"sku", 0x1500}});
        }

        void Disconnect()
        {
            Socket.stop();
            Open = false;
            LoggedIn = false;
            MyName.clear();
        }

        /*
    ** Sends a request and remembers what to do with the answer. Requests made
    ** before the socket is open are held until it opens.
    */
        HRESULT Request(PendingKind kind,
                        json req,
                        const std::string& name = "",
                        const std::string& text = "",
                        const Channel* channel = NULL)
        {
            if (Failed) {
                return CHAT_E_NOTCONNECTED;
            }
            Pending p;
            p.kind = kind;
            p.sent = timeGetTime();
            p.name = name;
            p.text = text;
            if (channel) {
                p.channel = *channel;
            } else {
                memset(&p.channel, 0, sizeof(p.channel));
            }
            Queue_Request(p, req);
            return S_OK;
        }

        /*
    ** Turns queued socket traffic into sink callbacks. Game thread only.
    */
        void Pump()
        {
            std::deque<std::string> batch;
            {
                std::lock_guard<std::mutex> lock(Mutex);
                batch.swap(Inbox);
            }
            for (const std::string& text : batch) {
                if (text == "\x01open") {
                    Open = true;
                    Flush_Held();
                    continue;
                }
                if (text == "\x01closed" || text == "\x01error") {
                    On_Connection_Lost(text == "\x01error");
                    continue;
                }
                json j = json::parse(text, nullptr, false);
                if (j.is_discarded() || !j.is_object()) {
                    continue;
                }
                if (j.contains("id")) {
                    On_Response(j);
                } else if (j.contains("ev")) {
                    On_Event(j);
                }
            }
            Expire_Requests();
        }

    private:
        ix::WebSocket Socket;
        std::mutex Mutex;
        std::deque<std::string> Inbox;
        std::map<long long, Pending> Waiting;
        std::vector<std::pair<Pending, json>> Held;
        long long NextID;
        bool Open;
        bool Failed;
        bool LoggedIn;

        void On_Socket_Message(const ix::WebSocketMessagePtr& msg)
        {
            std::lock_guard<std::mutex> lock(Mutex);
            switch (msg->type) {
            case ix::WebSocketMessageType::Message:
                Inbox.push_back(msg->str);
                break;
            case ix::WebSocketMessageType::Open:
                Inbox.push_back("\x01open");
                break;
            case ix::WebSocketMessageType::Close:
                Inbox.push_back("\x01closed");
                break;
            case ix::WebSocketMessageType::Error:
                Inbox.push_back("\x01error");
                break;
            default:
                break;
            }
        }

        void Queue_Request(Pending p, json req)
        {
            if (!Open) {
                Held.push_back(std::make_pair(p, req));
                return;
            }
            Send_Now(p, req);
        }

        void Send_Now(Pending p, json req)
        {
            long long id = NextID++;
            req["id"] = id;
            p.sent = timeGetTime();
            Waiting[id] = p;
            Socket.send(req.dump());
        }

        void Flush_Held()
        {
            std::vector<std::pair<Pending, json>> held;
            held.swap(Held);
            for (auto& h : held) {
                Send_Now(h.first, h.second);
            }
        }

        void Fail_Pending(HRESULT hr)
        {
            std::map<long long, Pending> waiting;
            waiting.swap(Waiting);
            std::vector<std::pair<Pending, json>> held;
            held.swap(Held);
            for (auto& w : waiting) {
                Deliver_Error(w.second, hr, "connection");
            }
            for (auto& h : held) {
                Deliver_Error(h.first, hr, "connection");
            }
        }

        void On_Connection_Lost(bool error)
        {
            bool was_logged_in = LoggedIn;
            bool was_open = Open;
            Open = false;
            LoggedIn = false;
            Failed = true;
            Fail_Pending(was_open ? CHAT_E_CON_NETDOWN : CHAT_E_CON_ERROR);
            if (was_logged_in && ChatSink) {
                ChatSink->OnNetStatus(CHAT_S_CON_DISCONNECTED);
            }
            (void)error;
        }

        void Expire_Requests()
        {
            DWORD now = timeGetTime();
            std::vector<long long> expired;
            for (auto& w : Waiting) {
                if (now - w.second.sent > (DWORD)REQUEST_TIMEOUT_MS) {
                    expired.push_back(w.first);
                }
            }
            for (long long id : expired) {
                Pending p = Waiting[id];
                Waiting.erase(id);
                Deliver_Error(p, CHAT_E_TIMEOUT, "timeout");
            }
            if (!Open && !Failed && !Held.empty()) {
                // Socket never opened within the timeout: report to whoever asked.
                for (auto& h : Held) {
                    if (now - h.first.sent > (DWORD)REQUEST_TIMEOUT_MS) {
                        On_Connection_Lost(true);
                        break;
                    }
                }
            }
        }

        /*
    ** Error delivery: each request kind has a callback the UI waits on.
    */
        void Deliver_Error(const Pending& p, HRESULT hr, const std::string& code)
        {
            if (!ChatSink) {
                return;
            }
            switch (p.kind) {
            case PK_HELLO:
                ChatSink->OnServerList(hr, NULL);
                break;
            case PK_LOGIN:
            case PK_REGISTER:
                ChatSink->OnConnection(hr, "");
                break;
            case PK_LOGOUT:
                ChatSink->OnNetStatus(CHAT_S_CON_DISCONNECTED);
                break;
            case PK_CHANNELS:
                ChatSink->OnChannelList(hr, NULL);
                break;
            case PK_CREATE:
                ChatSink->OnChannelCreate(hr, const_cast<Channel*>(&p.channel));
                break;
            case PK_JOIN: {
                User me = User_Named(MyName, CHAT_USER_MYSELF);
                ChatSink->OnChannelJoin(hr, const_cast<Channel*>(&p.channel), &me);
                break;
            }
            case PK_GAMESTART:
                ChatSink->OnGameStart(hr, &CurrentChannel, NULL, 0);
                break;
            case PK_USERIP: {
                User u = User_Named(p.name, 0);
                ChatSink->OnUserIP(hr, &u);
                break;
            }
            case PK_FIND:
                ChatSink->OnFind(hr, NULL);
                break;
            case PK_PAGE:
                ChatSink->OnPageSend(hr);
                break;
            case PK_LADDER:
                if (NetSink) {
                    NetSink->OnLadderList(hr, NULL, 0, 0, 0);
                }
                break;
            case PK_RESULTS:
                if (NetSink) {
                    NetSink->OnGameresSent(hr);
                }
                break;
            default:
                if (code != "connection" && code != "timeout") {
                    ChatSink->OnServerError(hr, code.c_str());
                }
                break;
            }
        }

        void Send_User_List(const json& users)
        {
            LinkedList<User> list;
            if (users.is_array()) {
                for (const json& u : users) {
                    list.items.push_back(User_From_JSON(u));
                }
            }
            ChatSink->OnUserList(S_OK, &CurrentChannel, list.head());
        }

        void On_Response(const json& j)
        {
            long long id = j["id"].get<long long>();
            auto it = Waiting.find(id);
            if (it == Waiting.end()) {
                return;
            }
            Pending p = it->second;
            Waiting.erase(it);
            bool ok = j.value("ok", false);
            std::string code = ok ? "" : JStr(j, "error");

            if (!ok && p.kind == PK_LOGIN && code == "no_such_user") {
                // First use of this nickname: claim it.
                Request(PK_REGISTER, json{{"op", "register"}, {"name", p.name}, {"password", p.text}}, p.name, p.text);
                return;
            }
            if (!ok) {
                if (code == "banned" && ChatSink) {
                    ChatSink->OnServerBannedYou(S_OK, (time_t)j.value("until", 0LL));
                }
                Deliver_Error(p, Error_To_HRESULT(code), code);
                return;
            }
            if (!ChatSink) {
                return;
            }

            switch (p.kind) {
            case PK_HELLO: {
                MOTD = JStr(j, "motd");
                Lobbies = JInt(j, "lobbies", 1);
                LinkedList<WOLServer> servers;
                WOLServer s;
                memset(&s, 0, sizeof(s));
                Copy_Str(s.name, sizeof(s.name), "Lobby");
                Copy_Str(s.connlabel, sizeof(s.connlabel), "IRC");
                Copy_Str(s.conndata, sizeof(s.conndata), WOL_Server_URL());
                servers.items.push_back(s);
                ChatSink->OnServerList(S_OK, servers.head());
                break;
            }
            case PK_LOGIN:
            case PK_REGISTER: {
                LoggedIn = true;
                MyName = JStr(j["user"], "name");
                if (MyName.empty()) {
                    MyName = p.name;
                }
                std::string motd = JStr(j, "motd");
                if (motd.empty()) {
                    motd = MOTD;
                }
                ChatSink->OnConnection(S_OK, motd.c_str());
                if (!motd.empty()) {
                    ChatSink->OnMessageOfTheDay(S_OK, motd.c_str());
                }
                break;
            }
            case PK_LOGOUT:
                LoggedIn = false;
                ChatSink->OnNetStatus(CHAT_S_CON_DISCONNECTED);
                Disconnect();
                break;
            case PK_CHANNELS: {
                LinkedList<Channel> list;
                const json& chans = j["channels"];
                if (chans.is_array()) {
                    for (const json& c : chans) {
                        list.items.push_back(Channel_From_JSON(c));
                    }
                }
                ChatSink->OnChannelList(S_OK, list.head());
                break;
            }
            case PK_CREATE:
            case PK_JOIN: {
                CurrentChannel = Channel_From_JSON(j["channel"]);
                if (p.kind == PK_CREATE) {
                    ChatSink->OnChannelCreate(S_OK, &CurrentChannel);
                }
                User me = User_Named(MyName, CHAT_USER_MYSELF | (p.kind == PK_CREATE ? CHAT_USER_CHANNELOWNER : 0));
                ChatSink->OnChannelJoin(S_OK, &CurrentChannel, &me);
                Send_User_List(j["users"]);
                break;
            }
            case PK_LEAVE: {
                User me = User_Named(MyName, CHAT_USER_MYSELF);
                Channel left = CurrentChannel;
                memset(&CurrentChannel, 0, sizeof(CurrentChannel));
                ChatSink->OnChannelLeave(S_OK, &left, &me);
                break;
            }
            case PK_USERS:
                Send_User_List(j["users"]);
                break;
            case PK_GAMESTART:
                // The game_start event carries the player list for everyone,
                // including the host; nothing to do with the response itself.
                break;
            case PK_USERIP: {
                User u = User_From_JSON(j);
                ChatSink->OnUserIP(S_OK, &u);
                break;
            }
            case PK_FIND: {
                std::string status = JStr(j, "status");
                if (status == "here") {
                    Channel c = Channel_From_JSON(j["channel"]);
                    ChatSink->OnFind(S_OK, &c);
                } else if (status == "no_chan") {
                    ChatSink->OnFind(CHAT_S_FIND_NOCHAN, NULL);
                } else if (status == "off") {
                    ChatSink->OnFind(CHAT_S_FIND_OFF, NULL);
                } else {
                    ChatSink->OnFind(CHAT_S_FIND_NOTHERE, NULL);
                }
                break;
            }
            case PK_PAGE: {
                std::string status = JStr(j, "status");
                if (status == "sent") {
                    ChatSink->OnPageSend(S_OK);
                } else if (status == "off") {
                    ChatSink->OnPageSend(CHAT_S_PAGE_OFF);
                } else {
                    ChatSink->OnPageSend(CHAT_S_PAGE_NOTHERE);
                }
                break;
            }
            case PK_LADDER: {
                if (NetSink) {
                    LinkedList<Ladder> list;
                    const json& ls = j["ladders"];
                    if (ls.is_array()) {
                        for (const json& l : ls) {
                            list.items.push_back(Ladder_From_JSON(l));
                        }
                    }
                    NetSink->OnLadderList(S_OK, list.head(), (int)list.items.size(), 0, 0);
                }
                break;
            }
            case PK_RESULTS:
                if (NetSink) {
                    NetSink->OnGameresSent(S_OK);
                }
                break;
            default:
                break;
            }
        }

        void On_Event(const json& j)
        {
            if (!ChatSink) {
                return;
            }
            std::string ev = JStr(j, "ev");
            if (ev == "ping") {
                Socket.send(json{{"op", "pong"}}.dump());
                return;
            }
            if (ev == "channel_joined") {
                User u = User_From_JSON(j["user"]);
                if ((u.flags & CHAT_USER_MYSELF) || Is_Me(u.name)) {
                    return; // our own join is reported through the response
                }
                Channel c = Channel_From_JSON(j["channel"]);
                CurrentChannel.currentUsers = c.currentUsers;
                ChatSink->OnChannelJoin(S_OK, &c, &u);
            } else if (ev == "channel_left") {
                User u = User_From_JSON(j["user"]);
                Channel c = Channel_From_JSON(j["channel"]);
                if (Is_Me(u.name)) {
                    u.flags |= CHAT_USER_MYSELF;
                }
                if (u.flags & CHAT_USER_MYSELF) {
                    // Removed by the server (kick, ban): the response path did not run.
                    memset(&CurrentChannel, 0, sizeof(CurrentChannel));
                } else {
                    CurrentChannel.currentUsers = c.currentUsers;
                }
                ChatSink->OnChannelLeave(S_OK, &c, &u);
            } else if (ev == "msg" || ev == "action") {
                User u = User_Named(JStr(j, "from"), 0);
                std::string text = JStr(j, "text");
                if (ev == "msg") {
                    ChatSink->OnPublicMessage(S_OK, &CurrentChannel, &u, text.c_str());
                } else {
                    ChatSink->OnPublicAction(S_OK, &CurrentChannel, &u, text.c_str());
                }
            } else if (ev == "pm" || ev == "paction") {
                User u = User_Named(JStr(j, "from"), 0);
                std::string text = JStr(j, "text");
                if (ev == "pm") {
                    ChatSink->OnPrivateMessage(S_OK, &u, text.c_str());
                } else {
                    ChatSink->OnPrivateAction(S_OK, &u, text.c_str());
                }
            } else if (ev == "topic") {
                std::string text = JStr(j, "text");
                Copy_Str(CurrentChannel.topic, sizeof(CurrentChannel.topic), text);
                ChatSink->OnChannelTopic(S_OK, &CurrentChannel, text.c_str());
            } else if (ev == "user_flags") {
                std::string name = JStr(j, "name");
                ChatSink->OnUserFlags(S_OK, name.c_str(), JInt(j, "flags"), JInt(j, "mask"));
            } else if (ev == "kicked") {
                User kicked = User_Named(JStr(j, "name"), 0);
                User by = User_Named(JStr(j, "by"), CHAT_USER_CHANNELOWNER);
                if (strcasecmp((const char*)kicked.name, MyName.c_str()) == 0) {
                    kicked.flags |= CHAT_USER_MYSELF;
                }
                ChatSink->OnUserKick(S_OK, &CurrentChannel, &kicked, &by);
            } else if (ev == "banned") {
                std::string name = JStr(j, "name");
                ChatSink->OnChannelBan(S_OK, name.c_str(), j.value("banned", false) ? 1 : 0);
            } else if (ev == "game_options") {
                User u = User_Named(JStr(j, "from"), 0);
                std::string text = JStr(j, "text");
                ChatSink->OnPublicGameOptions(S_OK, &CurrentChannel, &u, text.c_str());
            } else if (ev == "game_options_pm") {
                User u = User_Named(JStr(j, "from"), 0);
                std::string text = JStr(j, "text");
                ChatSink->OnPrivateGameOptions(S_OK, &u, text.c_str());
            } else if (ev == "game_start") {
                LinkedList<User> list;
                const json& users = j["users"];
                if (users.is_array()) {
                    for (const json& u : users) {
                        list.items.push_back(User_From_JSON(u));
                    }
                }
                CurrentChannel.ingame = 1;
                ChatSink->OnGameStart(S_OK, &CurrentChannel, list.head(), JInt(j, "game_id"));
            } else if (ev == "paged") {
                User u = User_Named(JStr(j, "from"), 0);
                std::string text = JStr(j, "text");
                ChatSink->OnPaged(S_OK, &u, text.c_str());
            } else if (ev == "system" || ev == "motd") {
                std::string text = JStr(j, "text");
                ChatSink->OnSystemMessage(S_OK, text.c_str());
            } else if (ev == "server_banned") {
                ChatSink->OnServerBannedYou(S_OK, (time_t)j.value("until", 0LL));
            } else if (ev == "disconnect") {
                Failed = true;
                Disconnect();
                ChatSink->OnNetStatus(CHAT_S_CON_DISCONNECTED);
            }
        }
    };

    /*
** ---------------------------------------------------------------------------
** Reference-counted base shared by the client objects.
** ---------------------------------------------------------------------------
*/
    class RefCounted
    {
    public:
        RefCounted()
            : Refs(1)
        {
        }
        virtual ~RefCounted()
        {
        }
        ULONG Add()
        {
            return ++Refs;
        }
        ULONG Drop()
        {
            ULONG r = --Refs;
            if (r == 0) {
                delete this;
            }
            return r;
        }

    private:
        ULONG Refs;
    };

    json To_List(User* users)
    {
        json to = json::array();
        for (User* u = users; u != NULL; u = u->next) {
            to.push_back(std::string((const char*)u->name));
        }
        return to;
    }

    class ChatClient : public IChat, public RefCounted
    {
    public:
        ChatClient(std::shared_ptr<LobbyLink> link)
            : Link(link)
            , SKU(0)
        {
            memset(Nicks, 0, sizeof(Nicks));
            Load_Nicks();
        }

        // IUnknown
        HRESULT QueryInterface(const IID& iid, void** ppv)
        {
            if (iid == IID_IChat) {
                *ppv = (IChat*)this;
                Add();
                return S_OK;
            }
            *ppv = NULL;
            return E_NOINTERFACE;
        }
        ULONG AddRef()
        {
            return Add();
        }
        ULONG Release()
        {
            return Drop();
        }

        // IChat
        HRESULT PumpMessages()
        {
            Link->Pump();
            return S_OK;
        }
        HRESULT RequestServerList(unsigned long sku, unsigned long, LPCSTR, LPCSTR, int)
        {
            SKU = sku;
            Link->Connect(WOL_Server_URL());
            return S_OK;
        }
        HRESULT RequestConnection(WOLServer* server, int, int)
        {
            if (!server) {
                return E_INVALIDARG;
            }
            std::string name((const char*)server->login);
            std::string pass((const char*)server->password);
            if (!Link->Is_Open()) {
                // Connection dropped since the server list; reconnect first.
                Link->Connect(WOL_Server_URL());
            }
            return Link->Request(PK_LOGIN, json{{"op", "login"}, {"name", name}, {"password", pass}}, name, pass);
        }
        HRESULT RequestChannelList(int channelType, int)
        {
            if (!Link->Is_Logged_In()) {
                return CHAT_E_NOTCONNECTED;
            }
            json req{{"op", "channels"}};
            if (channelType == 0) {
                req["filter"] = "all";
                req["type"] = 0;
            } else {
                req["filter"] = "games";
                req["type"] = channelType;
            }
            return Link->Request(PK_CHANNELS, req);
        }
        HRESULT RequestChannelCreate(Channel* channel)
        {
            if (!Link->Is_Logged_In() || !channel) {
                return CHAT_E_NOTCONNECTED;
            }
            json req{{"op", "channel_create"},
                     {"name", std::string((const char*)channel->name)},
                     {"type", channel->type},
                     {"max_users", (int)channel->maxUsers},
                     {"tournament", (int)channel->tournament},
                     {"reserved", (long long)channel->reserved},
                     {"key", std::string((const char*)channel->key)},
                     {"ex_info", std::string((const char*)channel->exInfo)},
                     {"topic", std::string((const char*)channel->topic)}};
            return Link->Request(PK_CREATE, req, "", "", channel);
        }
        HRESULT RequestChannelJoin(Channel* channel)
        {
            if (!Link->Is_Logged_In() || !channel) {
                return CHAT_E_NOTCONNECTED;
            }
            json req{{"op", "channel_join"},
                     {"name", std::string((const char*)channel->name)},
                     {"key", std::string((const char*)channel->key)}};
            return Link->Request(PK_JOIN, req, "", "", channel);
        }
        HRESULT RequestChannelLeave()
        {
            return Link->Request(PK_LEAVE, json{{"op", "channel_leave"}});
        }
        HRESULT RequestUserList()
        {
            return Link->Request(PK_USERS, json{{"op", "users"}});
        }
        HRESULT RequestPublicMessage(LPCSTR message)
        {
            return Link->Request(PK_SIMPLE, json{{"op", "msg"}, {"text", std::string(message ? message : "")}});
        }
        HRESULT RequestPrivateMessage(User* users, LPCSTR message)
        {
            return Link->Request(
                PK_SIMPLE, json{{"op", "pm"}, {"to", To_List(users)}, {"text", std::string(message ? message : "")}});
        }
        HRESULT RequestLogout()
        {
            if (!Link->Is_Open()) {
                return CHAT_E_NOTCONNECTED;
            }
            return Link->Request(PK_LOGOUT, json{{"op", "logout"}});
        }
        HRESULT RequestPrivateGameOptions(User* users, LPCSTR options)
        {
            return Link->Request(
                PK_SIMPLE,
                json{{"op", "game_options_to"}, {"to", To_List(users)}, {"text", std::string(options ? options : "")}});
        }
        HRESULT RequestPublicGameOptions(LPCSTR options)
        {
            return Link->Request(PK_SIMPLE,
                                 json{{"op", "game_options"}, {"text", std::string(options ? options : "")}});
        }
        HRESULT RequestPublicAction(LPCSTR action)
        {
            return Link->Request(PK_SIMPLE, json{{"op", "action"}, {"text", std::string(action ? action : "")}});
        }
        HRESULT RequestPrivateAction(User* users, LPCSTR action)
        {
            return Link->Request(
                PK_SIMPLE,
                json{{"op", "paction"}, {"to", To_List(users)}, {"text", std::string(action ? action : "")}});
        }
        HRESULT RequestGameStart(User*)
        {
            return Link->Request(PK_GAMESTART, json{{"op", "game_start"}});
        }
        HRESULT RequestChannelTopic(LPCSTR topic)
        {
            return Link->Request(PK_TOPIC, json{{"op", "topic"}, {"text", std::string(topic ? topic : "")}});
        }
        HRESULT GetVersion(unsigned long* version)
        {
            *version = 1;
            return S_OK;
        }
        HRESULT RequestUserKick(User* user)
        {
            if (!user) {
                return E_INVALIDARG;
            }
            return Link->Request(PK_KICK, json{{"op", "kick"}, {"name", std::string((const char*)user->name)}});
        }
        HRESULT RequestUserIP(User* user)
        {
            if (!user) {
                return E_INVALIDARG;
            }
            std::string name((const char*)user->name);
            return Link->Request(PK_USERIP, json{{"op", "user_ip"}, {"name", name}}, name);
        }
        HRESULT
        GetGametypeInfo(unsigned int gtype, int, unsigned char** bitmap, int* bmp_bytes, LPCSTR* name, LPCSTR* url)
        {
            *bitmap = NULL;
            *bmp_bytes = 0;
            *name = (gtype == GAME_TYPE_RA) ? "Red Alert" : "";
            *url = "";
            return S_OK;
        }
        HRESULT RequestFind(User* user)
        {
            if (!user) {
                return E_INVALIDARG;
            }
            std::string name((const char*)user->name);
            return Link->Request(PK_FIND, json{{"op", "find"}, {"name", name}}, name);
        }
        HRESULT RequestPage(User* user, LPCSTR message)
        {
            if (!user) {
                return E_INVALIDARG;
            }
            std::string name((const char*)user->name);
            return Link->Request(
                PK_PAGE, json{{"op", "page"}, {"name", name}, {"text", std::string(message ? message : "")}}, name);
        }
        HRESULT SetFindPage(int, int)
        {
            return S_OK;
        }
        HRESULT SetSquelch(User*, int)
        {
            return S_OK;
        }
        HRESULT GetSquelch(User*)
        {
            return S_FALSE;
        }
        HRESULT SetChannelFilter(int)
        {
            return S_OK;
        }
        HRESULT RequestGameEnd()
        {
            return S_OK;
        }
        HRESULT SetLangFilter(int)
        {
            return S_OK;
        }
        HRESULT RequestChannelBan(LPCSTR name, int ban)
        {
            return Link->Request(PK_BAN,
                                 json{{"op", "ban"}, {"name", std::string(name ? name : "")}, {"ban", ban != 0}});
        }
        HRESULT GetGametypeList(LPCSTR* list)
        {
            *list = "21";
            return S_OK;
        }
        HRESULT GetHelpURL(LPCSTR* url)
        {
            *url = "https://github.com/MrIron-no/Vanilla-Conquer";
            return S_OK;
        }
        HRESULT SetProductSKU(unsigned long sku)
        {
            SKU = sku;
            return S_OK;
        }
        HRESULT GetNick(int num, LPCSTR* nick, LPCSTR* pass)
        {
            if (num < 1 || num > NICK_SLOTS) {
                return E_INVALIDARG;
            }
            *nick = Nicks[num - 1].nick;
            *pass = Nicks[num - 1].pass;
            return S_OK;
        }
        HRESULT SetNick(int num, LPCSTR nick, LPCSTR pass, int)
        {
            if (num < 1 || num > NICK_SLOTS) {
                return E_INVALIDARG;
            }
            snprintf(Nicks[num - 1].nick, sizeof(Nicks[num - 1].nick), "%s", nick ? nick : "");
            snprintf(Nicks[num - 1].pass, sizeof(Nicks[num - 1].pass), "%s", pass ? pass : "");
            Save_Nicks();
            return S_OK;
        }
        HRESULT GetLobbyCount(int* count)
        {
            *count = Link->Lobbies;
            return S_OK;
        }
        HRESULT RequestRawMessage(LPCSTR)
        {
            return E_NOTIMPL;
        }
        HRESULT GetAttributeValue(LPCSTR, LPCSTR* value)
        {
            *value = "";
            return S_OK;
        }
        HRESULT SetAttributeValue(LPCSTR, LPCSTR)
        {
            return S_OK;
        }
        HRESULT SetChannelExInfo(LPCSTR info)
        {
            return Link->Request(PK_SIMPLE, json{{"op", "ex_info"}, {"text", std::string(info ? info : "")}});
        }
        HRESULT StopAutoping()
        {
            return S_OK;
        }

    private:
        struct NickSlot
        {
            char nick[16];
            char pass[16];
        };

        void Load_Nicks()
        {
            INIClass ini;
            CCFileClass file(CONFIG_FILE_NAME);
            if (file.Is_Available()) {
                ini.Load(file);
            }
            for (int i = 0; i < NICK_SLOTS; i++) {
                char key[16];
                snprintf(key, sizeof(key), "Nick%d", i + 1);
                ini.Get_String(WOL_SECTION, key, "", Nicks[i].nick, sizeof(Nicks[i].nick));
                snprintf(key, sizeof(key), "Pass%d", i + 1);
                ini.Get_String(WOL_SECTION, key, "", Nicks[i].pass, sizeof(Nicks[i].pass));
            }
        }

        void Save_Nicks()
        {
            INIClass ini;
            CCFileClass file(CONFIG_FILE_NAME);
            if (file.Is_Available()) {
                ini.Load(file);
            }
            for (int i = 0; i < NICK_SLOTS; i++) {
                char key[16];
                snprintf(key, sizeof(key), "Nick%d", i + 1);
                ini.Put_String(WOL_SECTION, key, Nicks[i].nick);
                snprintf(key, sizeof(key), "Pass%d", i + 1);
                ini.Put_String(WOL_SECTION, key, Nicks[i].pass);
            }
            ini.Save(file);
        }

        std::shared_ptr<LobbyLink> Link;
        unsigned long SKU;
        NickSlot Nicks[NICK_SLOTS];
    };

    class NetUtilClient : public INetUtil, public RefCounted
    {
    public:
        NetUtilClient(std::shared_ptr<LobbyLink> link)
            : Link(link)
            , NextPingHandle(1)
        {
        }

        HRESULT QueryInterface(const IID& iid, void** ppv)
        {
            if (iid == IID_INetUtil) {
                *ppv = (INetUtil*)this;
                Add();
                return S_OK;
            }
            *ppv = NULL;
            return E_NOINTERFACE;
        }
        ULONG AddRef()
        {
            return Add();
        }
        ULONG Release()
        {
            return Drop();
        }

        HRESULT RequestGameresSend(LPCSTR, int, unsigned char* data, int length)
        {
            if (!Link->Is_Logged_In()) {
                return CHAT_E_NOTCONNECTED;
            }
            json req{{"op", "results"},
                     {"game_id", (long long)PlanetWestwoodGameID},
                     {"sku", 1005},
                     {"data", Base64(data, length)}};
            return Link->Request(PK_RESULTS, req);
        }
        HRESULT RequestLadderList(LPCSTR, int, LPCSTR keys, unsigned long sku, int, int, int)
        {
            if (!Link->Is_Logged_In()) {
                return CHAT_E_NOTCONNECTED;
            }
            json names = json::array();
            std::string k(keys ? keys : "");
            size_t start = 0;
            while (start <= k.size()) {
                size_t colon = k.find(':', start);
                std::string one = k.substr(start, colon == std::string::npos ? std::string::npos : colon - start);
                if (!one.empty()) {
                    names.push_back(one);
                }
                if (colon == std::string::npos) {
                    break;
                }
                start = colon + 1;
            }
            return Link->Request(PK_LADDER, json{{"op", "ladder"}, {"names", names}, {"sku", (long long)sku}});
        }
        HRESULT RequestPing(LPCSTR host, int, int* handle)
        {
            // Latency measurement is not implemented yet: report the host as
            // reachable with an unknown time so tournament checks pass.
            *handle = NextPingHandle++;
            if (Link->NetSink) {
                Link->NetSink->OnPing(S_OK, 0, host ? inet_addr(host) : 0, *handle);
            }
            return S_OK;
        }
        HRESULT PumpMessages()
        {
            Link->Pump();
            return S_OK;
        }
        HRESULT GetAvgPing(unsigned long, int* avg)
        {
            *avg = -1;
            return S_FALSE;
        }

    private:
        static std::string Base64(const unsigned char* data, int length)
        {
            static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string out;
            int i = 0;
            while (i + 2 < length) {
                unsigned v = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
                out += tbl[(v >> 18) & 63];
                out += tbl[(v >> 12) & 63];
                out += tbl[(v >> 6) & 63];
                out += tbl[v & 63];
                i += 3;
            }
            if (i < length) {
                unsigned v = data[i] << 16;
                if (i + 1 < length) {
                    v |= data[i + 1] << 8;
                }
                out += tbl[(v >> 18) & 63];
                out += tbl[(v >> 12) & 63];
                out += (i + 1 < length) ? tbl[(v >> 6) & 63] : '=';
                out += '=';
            }
            return out;
        }

        std::shared_ptr<LobbyLink> Link;
        int NextPingHandle;
    };

    std::shared_ptr<LobbyLink> TheLink;

} // namespace

bool WOL_Create_Clients(IChat** chat, INetUtil** netutil, IChatEvent* chat_sink, INetUtilEvent* netutil_sink)
{
    if (!TheLink) {
        TheLink = std::make_shared<LobbyLink>();
    }
    TheLink->ChatSink = chat_sink;
    TheLink->NetSink = netutil_sink;
    *chat = new ChatClient(TheLink);
    *netutil = new NetUtilClient(TheLink);
    return true;
}

const char* WOL_Server_URL(void)
{
    static char url[256];
    INIClass ini;
    CCFileClass file(CONFIG_FILE_NAME);
    if (file.Is_Available()) {
        ini.Load(file);
    }
    ini.Get_String(WOL_SECTION, "Server", "ws://127.0.0.1:8080/ws", url, sizeof(url));
    return url;
}

#endif // WOLAPI_INTEGRATION
