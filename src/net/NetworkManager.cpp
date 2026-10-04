#include "NetworkManager.h"

#include <iostream>
#include <cstring>
#include <string>
#include <thread>
#include <chrono>
#include <algorithm>

#ifdef KITBASHER_ENABLE_STEAM
#include <steam/steam_api.h>

namespace {

struct SteamLobbyState {
    CSteamID lobbyId;
    bool isHost = false;
    bool pendingCreate = false;
    bool pendingJoin = false;
    std::string outLobbyCode;
    bool lobbyReady = false;
};

static SteamLobbyState g_lobbyState;


// Lobby Created Callback

class CLobbyCreatedCallback {
public:
    CCallResult<CLobbyCreatedCallback, LobbyCreated_t> m_callResult;

    void OnLobbyCreated(LobbyCreated_t* pCallback, bool bIOFailure) {
        g_lobbyState.pendingCreate = false;

        if (bIOFailure || pCallback->m_eResult != k_EResultOK) {
            std::cerr << "[Network] Lobby creation failed: "
                      << pCallback->m_eResult << std::endl;
            return;
        }

        g_lobbyState.lobbyId = pCallback->m_ulSteamIDLobby;
        g_lobbyState.isHost = true;
        g_lobbyState.lobbyReady = true;

        // Configure lobby config
        // Private
        // SteamMatchmaking()->SetLobbyType(
        //     g_lobbyState.lobbyId,
        //     k_ELobbyTypeFriendsOnly
        // );
        // Public
        SteamMatchmaking()->SetLobbyType(
            g_lobbyState.lobbyId,
            k_ELobbyTypePublic
        );
        SteamMatchmaking()->SetLobbyMemberLimit(
            g_lobbyState.lobbyId,
            net::MaxPlayers
        );

        g_lobbyState.outLobbyCode =
            std::to_string(g_lobbyState.lobbyId.ConvertToUint64());

        std::cout << "[Network] Lobby created: "
                  << g_lobbyState.outLobbyCode << std::endl;
    }
};
static CLobbyCreatedCallback g_lobbyCreatedCallback;


// Lobby Enter Callback

class CLobbyEnterCallback {
public:
    CCallResult<CLobbyEnterCallback, LobbyEnter_t> m_callResult;

    void OnLobbyEnter(LobbyEnter_t* pCallback, bool bIOFailure) {
        g_lobbyState.pendingJoin = false;

        if (bIOFailure ||
            pCallback->m_EChatRoomEnterResponse !=
                k_EChatRoomEnterResponseSuccess) {
            std::cerr << "[Network] Lobby enter failed: "
                    << pCallback->m_EChatRoomEnterResponse << std::endl;
            return;
        }

        g_lobbyState.lobbyId = pCallback->m_ulSteamIDLobby;
        g_lobbyState.isHost = false;
        g_lobbyState.lobbyReady = true;

        std::cout << "[Network] Entered lobby: "
                << g_lobbyState.lobbyId.ConvertToUint64() << std::endl;

        net::NetworkManager::getInstance().finalizeLobbyJoin();
    }
};
static CLobbyEnterCallback g_lobbyEnterCallback;


// Session Request Callback (auto-accept P2P connections)

class CSessionRequestCallback {
public:
    CCallback<CSessionRequestCallback,
              SteamNetworkingMessagesSessionRequest_t,
              true>
        m_callback;

    CSessionRequestCallback()
        : m_callback(this, &CSessionRequestCallback::OnSessionRequest) {}

    void OnSessionRequest(SteamNetworkingMessagesSessionRequest_t* pRequest) {
        // Automatically accept connections from lobby members.
        SteamNetworkingMessages()->AcceptSessionWithUser(
            pRequest->m_identityRemote
        );
    }
};
static CSessionRequestCallback g_sessionRequestCallback;


// Lobby Chat Update Callback (player join / leave)

class CLobbyChatUpdateCallback {
public:
    CCallback<CLobbyChatUpdateCallback, LobbyChatUpdate_t, true> m_callback;

    CLobbyChatUpdateCallback()
        : m_callback(this, &CLobbyChatUpdateCallback::OnLobbyChatUpdate) {}

    void OnLobbyChatUpdate(LobbyChatUpdate_t* pCallback) {
        // Only process if this is our lobby.
        if (pCallback->m_ulSteamIDLobby !=
            g_lobbyState.lobbyId.ConvertToUint64()) {
            return;
        }

        uint32_t changeFlags = pCallback->m_rgfChatMemberStateChange;

        if (changeFlags & k_EChatMemberStateChangeEntered) {
            std::cout << "[Network] Player joined lobby" << std::endl;
        }

        if (changeFlags & k_EChatMemberStateChangeLeft ||
            changeFlags & k_EChatMemberStateChangeDisconnected ||
            changeFlags & k_EChatMemberStateChangeKicked ||
            changeFlags & k_EChatMemberStateChangeBanned) {
            std::cout << "[Network] Player left lobby" << std::endl;
        }
    }
};
static CLobbyChatUpdateCallback g_lobbyChatUpdateCallback;


// Game Lobby Join Requested Callback (Handles Steam Invites)

class CGameLobbyJoinRequestedCallback {
public:
    CCallback<CGameLobbyJoinRequestedCallback, GameLobbyJoinRequested_t, true> m_callback;

    CGameLobbyJoinRequestedCallback()
        : m_callback(this, &CGameLobbyJoinRequestedCallback::OnGameLobbyJoinRequested) {}

    void OnGameLobbyJoinRequested(GameLobbyJoinRequested_t* pCallback) {
        std::cout << "[Network] Invite accepted, joining lobby "
                  << pCallback->m_steamIDLobby.ConvertToUint64() << std::endl;
        
        net::NetworkManager::getInstance().joinLobbyBySteamId(
            pCallback->m_steamIDLobby.ConvertToUint64()
        );
    }
};
static CGameLobbyJoinRequestedCallback g_gameLobbyJoinRequestedCallback;

} // anonymous namespace
#endif // KITBASHER_ENABLE_STEAM

namespace net {


// Singleton

NetworkManager& NetworkManager::getInstance() {
    static NetworkManager instance;
    return instance;
}

NetworkManager::NetworkManager()
    : m_role(Role::Disconnected),
      m_localSlot(InvalidSlot),
      m_initialized(false),
      m_connected(false) {}

NetworkManager::~NetworkManager() {
    shutdown();
}


// Lifecycle

bool NetworkManager::init() {
    if (m_initialized) {
        return true;
    }

#ifdef KITBASHER_ENABLE_STEAM
    if (!SteamAPI_Init()) {
        std::cerr << "[Network] SteamAPI_Init failed. "
                  << "Ensure Steam is running and steam_appid.txt exists.\n";
        return false;
    }

    std::cout << "[Network] Steam initialized. User: "
              << SteamFriends()->GetPersonaName() << std::endl;
#else
    std::cout << "[Network] NetworkManager initialized (Stub mode)\n";
#endif

    m_initialized = true;
    m_connected = false;
    m_role = Role::Disconnected;
    m_localSlot = InvalidSlot;
    m_players.clear();

    return true;
}

void NetworkManager::shutdown() {
    if (!m_initialized) {
        return;
    }

#ifdef KITBASHER_ENABLE_STEAM
    if (g_lobbyState.lobbyReady) {
        SteamMatchmaking()->LeaveLobby(g_lobbyState.lobbyId);
        g_lobbyState.lobbyReady = false;
        g_lobbyState.lobbyId = CSteamID();
    }
    SteamAPI_Shutdown();
#endif

    m_players.clear();
    m_connected = false;
    m_role = Role::Disconnected;
    m_localSlot = InvalidSlot;
    m_initialized = false;

    std::cout << "[Network] NetworkManager shut down\n";
}


// Lobby Management

bool NetworkManager::hostPrivateLobby(std::string& outLobbyCode) {
    if (!m_initialized) {
        std::cerr << "[Network] Cannot host lobby: not initialized\n";
        return false;
    }

#ifdef KITBASHER_ENABLE_STEAM
    // If already hosting, just return the existing code.
    if (g_lobbyState.lobbyReady && g_lobbyState.isHost) {
        outLobbyCode = g_lobbyState.outLobbyCode;

        m_role = Role::Host;
        m_localSlot = HostSlot;
        m_connected = true;

        // Ensure we are in the player list.
        bool found = false;
        SteamPlayerId localId =
            SteamUser()->GetSteamID().ConvertToUint64();
        for (const auto& p : m_players) {
            if (p.steamId == localId) {
                found = true;
                break;
            }
        }
        if (!found) {
            RemotePlayer self;
            self.slot = HostSlot;
            self.steamId = localId;
            self.name = SteamFriends()->GetPersonaName();
            m_players.push_back(self);
        }

        return true;
    }

    // Start async lobby creation.
    if (!g_lobbyState.pendingCreate && !g_lobbyState.pendingJoin) {
        g_lobbyState.pendingCreate = true;
        SteamAPICall_t hSteamAPICall =
            // Private -> SteamMatchmaking()->CreateLobby(k_ELobbyTypeFriendsOnly, MaxPlayers);
            SteamMatchmaking()->CreateLobby(k_ELobbyTypePublic, MaxPlayers);
        g_lobbyCreatedCallback.m_callResult.Set(
            hSteamAPICall,
            &g_lobbyCreatedCallback,
            &CLobbyCreatedCallback::OnLobbyCreated
        );
    }

    // Spin-wait for the async callback to complete.
    for (int i = 0; i < 100 && g_lobbyState.pendingCreate; ++i) {
        SteamAPI_RunCallbacks();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    if (g_lobbyState.lobbyReady && g_lobbyState.isHost) {
        outLobbyCode = g_lobbyState.outLobbyCode;

        m_role = Role::Host;
        m_localSlot = HostSlot;
        m_connected = true;

        m_players.clear();
        RemotePlayer self;
        self.slot = HostSlot;
        self.steamId = SteamUser()->GetSteamID().ConvertToUint64();
        self.name = SteamFriends()->GetPersonaName();
        m_players.push_back(self);

        return true;
    }

    return false;
#else
    // Stub fallback for offline / non-Steam builds.
    m_role = Role::Host;
    m_localSlot = HostSlot;
    m_connected = true;

    RemotePlayer self;
    self.slot = HostSlot;
    self.steamId = 1;
    self.name = "Host";
    m_players.clear();
    m_players.push_back(self);

    outLobbyCode = "STUB-HOST-001";

    std::cout << "[Network] Hosting stub lobby: " << outLobbyCode
              << std::endl;
    return true;
#endif
}

bool NetworkManager::joinPrivateLobby(const std::string& lobbyCode) {
    if (!m_initialized) {
        std::cerr << "[Network] Cannot join lobby: not initialized\n";
        return false;
    }

#ifdef KITBASHER_ENABLE_STEAM
    uint64_t lobbyIdNum = 0;
    try {
        lobbyIdNum = std::stoull(lobbyCode);
    } catch (...) {
        std::cerr << "[Network] Invalid lobby code: " << lobbyCode
                  << std::endl;
        return false;
    }

    CSteamID lobbyId(lobbyIdNum);

    // Start async lobby join.
    if (!g_lobbyState.pendingJoin && !g_lobbyState.pendingCreate) {
        g_lobbyState.pendingJoin = true;
        SteamAPICall_t hSteamAPICall =
            SteamMatchmaking()->JoinLobby(lobbyId);
        g_lobbyEnterCallback.m_callResult.Set(
            hSteamAPICall,
            &g_lobbyEnterCallback,
            &CLobbyEnterCallback::OnLobbyEnter
        );
    }

    // Spin-wait for the async callback to complete.
    for (int i = 0; i < 100 && g_lobbyState.pendingJoin; ++i) {
        SteamAPI_RunCallbacks();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    if (g_lobbyState.lobbyReady && !g_lobbyState.isHost) {
        m_role = Role::Client;
        m_connected = true;

        // Sync lobby members to assign slots deterministically.
        syncPlayersFromLobby();

        return true;
    }

    return false;
#else
    // Stub fallback for offline / non-Steam builds.
    m_role = Role::Client;
    m_localSlot = 1;
    m_connected = true;

    std::cout << "[Network] Joining stub lobby: " << lobbyCode << std::endl;
    return true;
#endif
}

void NetworkManager::showInviteDialog() {
#ifdef KITBASHER_ENABLE_STEAM
    if (!g_lobbyState.lobbyReady || !g_lobbyState.isHost) {
        std::cerr << "[Network] Cannot show invite dialog: not hosting a lobby\n";
        return;
    }
    // Opens the Steam Overlay to the friends list
    SteamFriends()->ActivateGameOverlayInviteDialog(g_lobbyState.lobbyId);
#else
    std::cerr << "[Network] Steam invites are only available in Steam builds.\n";
#endif
}

void NetworkManager::joinLobbyBySteamId(uint64_t steamLobbyId) {
#ifdef KITBASHER_ENABLE_STEAM
    if (!m_initialized) {
        return;
    }

    // Do not allow the host to leave their lobby by accepting an invite.
    if (m_connected && m_role == Role::Host) {
        std::cerr << "[Network] Ignoring invite because we are already hosting a lobby." << std::endl;
        return;
    }

    if (g_lobbyState.pendingJoin || g_lobbyState.pendingCreate) {
        return;
    }

    CSteamID lobbyId(steamLobbyId);

    g_lobbyState.pendingJoin = true;

    SteamAPICall_t hSteamAPICall = SteamMatchmaking()->JoinLobby(lobbyId);

    g_lobbyEnterCallback.m_callResult.Set(
        hSteamAPICall,
        &g_lobbyEnterCallback,
        &CLobbyEnterCallback::OnLobbyEnter
    );
#else
    (void)steamLobbyId;
#endif
}


// Per-frame Update


void NetworkManager::update(float deltaTime) {
    (void)deltaTime;

    if (!m_initialized) {
        return;
    }

#ifdef KITBASHER_ENABLE_STEAM
    SteamAPI_RunCallbacks();

    if (!m_connected) {
        return;
    }

    // Periodically refresh the player list in case someone joined/left.
    syncPlayersFromLobby();

    // Poll Reliable Channel.
    SteamNetworkingMessage_t* msg = nullptr;
    while (SteamNetworkingMessages()->ReceiveMessagesOnChannel(
               static_cast<int>(Channel::Reliable), &msg, 1) > 0) {
        handleReceivedData(
            msg->m_identityPeer.GetSteamID64(),
            msg->m_pData,
            msg->m_cbSize,
            true
        );
        msg->Release();
    }

    // Poll Unreliable Channel.
    while (SteamNetworkingMessages()->ReceiveMessagesOnChannel(
               static_cast<int>(Channel::Unreliable), &msg, 1) > 0) {
        handleReceivedData(
            msg->m_identityPeer.GetSteamID64(),
            msg->m_pData,
            msg->m_cbSize,
            false
        );
        msg->Release();
    }
#endif
}


// Sending Data


void NetworkManager::broadcastReliable(const void* data, size_t size) {
    if (!m_initialized || !m_connected || data == nullptr || size == 0) {
        return;
    }

#ifdef KITBASHER_ENABLE_STEAM
    for (const auto& player : m_players) {
        if (player.slot == m_localSlot) {
            continue;
        }

        SteamNetworkingIdentity identity;
        identity.SetSteamID64(player.steamId);

        SteamNetworkingMessages()->SendMessageToUser(
            identity,
            data,
            static_cast<uint32>(size),
            k_nSteamNetworkingSend_Reliable,
            static_cast<int>(Channel::Reliable)
        );
    }
#else
    (void)data;
    (void)size;
#endif
}

void NetworkManager::broadcastUnreliable(const void* data, size_t size) {
    if (!m_initialized || !m_connected || data == nullptr || size == 0) {
        return;
    }

#ifdef KITBASHER_ENABLE_STEAM
    for (const auto& player : m_players) {
        if (player.slot == m_localSlot) {
            continue;
        }

        SteamNetworkingIdentity identity;
        identity.SetSteamID64(player.steamId);

        SteamNetworkingMessages()->SendMessageToUser(
            identity,
            data,
            static_cast<uint32>(size),
            k_nSteamNetworkingSend_Unreliable,
            static_cast<int>(Channel::Unreliable)
        );
    }
#else
    (void)data;
    (void)size;
#endif
}

void NetworkManager::sendReliable(
    PlayerSlot slot,
    const void* data,
    size_t size
) {
    if (!m_initialized || !m_connected || data == nullptr || size == 0) {
        return;
    }

#ifdef KITBASHER_ENABLE_STEAM
    SteamPlayerId steamId = findSteamIdBySlot(slot);
    if (steamId == InvalidSteamId) {
        return;
    }

    SteamNetworkingIdentity identity;
    identity.SetSteamID64(steamId);

    SteamNetworkingMessages()->SendMessageToUser(
        identity,
        data,
        static_cast<uint32>(size),
        k_nSteamNetworkingSend_Reliable,
        static_cast<int>(Channel::Reliable)
    );
#else
    (void)slot;
    (void)data;
    (void)size;
#endif
}

void NetworkManager::sendUnreliable(
    PlayerSlot slot,
    const void* data,
    size_t size
) {
    if (!m_initialized || !m_connected || data == nullptr || size == 0) {
        return;
    }

#ifdef KITBASHER_ENABLE_STEAM
    SteamPlayerId steamId = findSteamIdBySlot(slot);
    if (steamId == InvalidSteamId) {
        return;
    }

    SteamNetworkingIdentity identity;
    identity.SetSteamID64(steamId);

    SteamNetworkingMessages()->SendMessageToUser(
        identity,
        data,
        static_cast<uint32>(size),
        k_nSteamNetworkingSend_Unreliable,
        static_cast<int>(Channel::Unreliable)
    );
#else
    (void)slot;
    (void)data;
    (void)size;
#endif
}


// Internal Helpers


void NetworkManager::handleReceivedData(
    SteamPlayerId fromSteamId,
    const void* data,
    size_t size,
    bool reliable
) {
    if (data == nullptr || size == 0) {
        return;
    }

    PlayerSlot fromSlot = findSlotBySteamId(fromSteamId);

    if (m_messageCallback) {
        m_messageCallback(fromSlot, data, size, reliable);
    }
}

void NetworkManager::syncPlayersFromLobby() {
#ifdef KITBASHER_ENABLE_STEAM
    if (!g_lobbyState.lobbyReady) {
        return;
    }

    int numMembers = SteamMatchmaking()->GetNumLobbyMembers(
        g_lobbyState.lobbyId
    );

    // Only rebuild if the count changed.
    if (static_cast<size_t>(numMembers) == m_players.size()) {
        return;
    }

    m_players.clear();

    for (int i = 0; i < numMembers && i < static_cast<int>(MaxPlayers);
         ++i) {
        CSteamID memberId = SteamMatchmaking()->GetLobbyMemberByIndex(
            g_lobbyState.lobbyId,
            i
        );

        RemotePlayer player;
        player.slot = static_cast<PlayerSlot>(i);
        player.steamId = memberId.ConvertToUint64();
        player.name = SteamFriends()->GetFriendPersonaName(memberId);

        m_players.push_back(player);

        if (memberId == SteamUser()->GetSteamID()) {
            m_localSlot = player.slot;
        }
    }

    std::cout << "[Network] Synced " << m_players.size()
              << " player(s) from lobby\n";
#endif
}

void NetworkManager::checkForPendingInvite(int argc, char** argv) {
#ifdef KITBASHER_ENABLE_STEAM
    if (!m_initialized) {
        return;
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if ((arg == "+connect_lobby" || arg == "-connect_lobby") &&
            i + 1 < argc) {
            try {
                uint64_t lobbyId = std::stoull(argv[i + 1]);

                std::cout << "[Network] Launch invite detected, joining lobby "
                          << lobbyId << std::endl;

                joinLobbyBySteamId(lobbyId);
            } catch (...) {
                std::cerr << "[Network] Failed to parse +connect_lobby argument" << std::endl;
            }

            return;
        }
    }
#else
    (void)argc;
    (void)argv;
#endif
}

PlayerSlot NetworkManager::findSlotBySteamId(SteamPlayerId steamId) const {
    for (const auto& player : m_players) {
        if (player.steamId == steamId) {
            return player.slot;
        }
    }
    return InvalidSlot;
}

SteamPlayerId NetworkManager::findSteamIdBySlot(PlayerSlot slot) const {
    for (const auto& player : m_players) {
        if (player.slot == slot) {
            return player.steamId;
        }
    }
    return InvalidSteamId;
}

PlayerSlot NetworkManager::allocateFreeSlot() const {
    for (PlayerSlot candidate = 1; candidate < MaxPlayers; ++candidate) {
        bool taken = false;
        for (const auto& player : m_players) {
            if (player.slot == candidate) {
                taken = true;
                break;
            }
        }
        if (!taken) {
            return candidate;
        }
    }
    return InvalidSlot;
}

void NetworkManager::finalizeLobbyJoin() {
#ifdef KITBASHER_ENABLE_STEAM
    if (!g_lobbyState.lobbyReady) {
        return;
    }

    if (g_lobbyState.isHost) {
        return;
    }

    m_role = Role::Client;
    m_connected = true;

    syncPlayersFromLobby();

    std::cout << "[Network] Lobby join finalized. Local slot: "
              << static_cast<int>(m_localSlot) << std::endl;
#endif
}

}