/* Story changelevel keeps each client's connection (cof_story_changelevel).
 * Runs the actual SV_ChangeLevel -> SV_DeactivateServer -> SV_ReadPackets path:
 * a kept connection's "new" must reach SV_ExecuteClientMessage, not just be
 * acknowledged by the netchan and then dropped for lack of delta frames. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int qboolean;
#define true 1
#define false 0
#define MAX_QPATH 64
#define S_ERROR "Error: "
#define NS_SERVER 1
#define FCL_SEND_NET_MESSAGE (1 << 5)
#define FCL_FAKECLIENT (1 << 7)
#define FBitSet(value, flag) ((value) & (flag))
#define SetBits(value, flag) ((value) |= (flag))
#define Mem_Free(ptr) free(ptr)
#define Mem_Calloc(pool, size) ((void)(pool), calloc(1, (size)))
enum { cs_free = 0, cs_zombie, cs_connected, cs_spawning, cs_spawned };
enum { ss_dead, ss_loading, ss_active };
typedef struct { int type, port; } netadr_t;
typedef struct { netadr_t remote_address; char incomingfilename[MAX_QPATH]; } netchan_t;
typedef struct { double senttime; int marker; } client_frame_t;
typedef struct { int state, flags; client_frame_t *frames; netchan_t netchan; } sv_client_t;
typedef struct { unsigned char *pData; size_t size; } sizebuf_t;
typedef struct { int unused; } SAVERESTOREDATA;
typedef struct { float value; } convar_t;
typedef struct { float time, frametime; int changelevel, maxEntities, maxClients, startspot, mapname; } globalvars_t;

static int SV_UPDATE_BACKUP = 64;
static globalvars_t globals;
static void DLL_Noop(void) {}
static struct {
    globalvars_t *globals;
    struct { void (*pfnServerDeactivate)(void); void (*pfnResetGlobalState)(void); } dllFuncs;
    void *pmove, *stringspool;
    int numEntities;
} svgame = { &globals, { DLL_Noop, DLL_Noop }, NULL, NULL, 0 };
static struct { int state; char name[MAX_QPATH]; float time, frametime; sv_client_t *current_client; } sv;
static sv_client_t clients[4];
static struct { int initialized, maxclients; sv_client_t *clients; } svs;
static struct { int max_edicts; } gameinfo = { 900 }, *GI = &gameinfo;
static struct { void *mempool; } host;
static convar_t host_limitlocal, sv_newunit;
static char cof_story_maps[256][MAX_QPATH];
static int cof_story_map_count;
static int story_enabled, transition_active, final_reconnects, channel_processed, executed[4];
static SAVERESTOREDATA save_data;
static qboolean CoF_SessionRunLoad( void ) { return false; }

static void Q_strncpy(char *dst, const char *src, size_t size) { strncpy(dst, src, size - 1); dst[size - 1] = 0; }
static void Con_Printf(const char *format, ...) { (void)format; }
static void Sys_Warn(const char *format, ...) { (void)format; }
static void Host_Error(const char *format, ...) { (void)format; abort(); }
static void Cbuf_AddTextf(const char *format, ...) { (void)format; }
static const char *Cvar_VariableString(const char *name) { (void)name; return ""; }
static qboolean COM_StringEmpty(const char *s) { return !*s; }
static qboolean COM_StringEmptyOrNULL(const char *s) { return !s || !*s; }

/* SV_DeactivateServer dependencies */
static void SV_InactivateClients(void)
{
    for (int i = 0; i < svs.maxclients; i++)
        if (svs.clients[i].state > cs_connected) svs.clients[i].state = cs_connected;
}
static void Host_SetServerState(int state) { sv.state = state; }
static void SV_FreeEdicts(void) {}
static void PM_ClearPhysEnts(void *pmove) { (void)pmove; }
static void SV_EmptyStringPool(qboolean all) { (void)all; }
static void Mem_EmptyPool(void *pool) { (void)pool; }
#include "deactivate_under_test.c"

/* SV_ReadPackets dependencies: a queue of sequenced packets, one per client */
static netadr_t net_from, queued[4];
static int queued_count;
static unsigned char net_message_buffer[16];
static sizebuf_t net_message;
static qboolean NET_GetPacket(int sock, netadr_t *from, unsigned char *data, size_t *size)
{
    (void)sock;
    if (!queued_count) return false;
    *from = queued[--queued_count];
    memset(data, 0, 8); /* sequenced, not connectionless */
    *size = 8;
    return true;
}
static void MSG_Init(sizebuf_t *msg, const char *name, unsigned char *data, size_t size) { (void)name; msg->pData = data; msg->size = size; }
static size_t MSG_GetMaxBytes(sizebuf_t *msg) { return msg->size; }
static void SV_ConnectionlessPacket(netadr_t from, sizebuf_t *msg) { (void)from; (void)msg; assert(!"story clients reuse the netchan"); }
static qboolean NET_CompareBaseAdr(netadr_t a, netadr_t b) { return a.type == b.type; }
static qboolean Netchan_Process(netchan_t *chan, sizebuf_t *msg) { (void)chan; (void)msg; channel_processed++; return true; }
static void SV_ExecuteClientMessage(sv_client_t *cl, sizebuf_t *msg) { (void)msg; executed[cl - svs.clients]++; }
static qboolean Netchan_IncomingReady(netchan_t *chan) { (void)chan; return false; }
static qboolean Netchan_CopyNormalFragments(netchan_t *chan, sizebuf_t *msg, size_t *size) { (void)chan; (void)msg; (void)size; return false; }
static qboolean Netchan_CopyFileFragments(netchan_t *chan, sizebuf_t *msg) { (void)chan; (void)msg; return false; }
static void SV_ProcessFile(sv_client_t *cl, const char *name) { (void)cl; (void)name; }
#include "readpackets_under_test.c"

/* SV_ChangeLevel dependencies */
static qboolean SV_CoF_StoryCoopEnabled(void) { return story_enabled; }
/* 1.3.5: a game of Cry of Fear's own co-op, its maps, and the flag SV_FinalMessage reads (sv_main.c) */
static int official_coop, kept_during_final;
static qboolean sv_cof_keep_connections;
static qboolean SV_CoF_StoryOfficialCoop(void) { return official_coop; }
static qboolean SV_CoF_StoryOfficialMap(const char *map) { return map && (!strncmp(map, "cof_", 4) || !strncmp(map, "c_cof_", 6)); }
static qboolean CoF_BeginStoryTransition(const char *map, const char *landmark) { (void)map; (void)landmark; transition_active = true; return true; }
static void SV_CoF_TransitionReset(qboolean clear_history) { (void)clear_history; transition_active = false; }
static void SV_CoF_BlurLeaving(const char *map) { (void)map; }
static SAVERESTOREDATA *SaveGameState(qboolean changelevel) { (void)changelevel; return &save_data; }
static SAVERESTOREDATA *LoadSaveData(const char *level) { (void)level; return &save_data; }
static void SaveFinish(SAVERESTOREDATA *data) { (void)data; }
static qboolean CoF_StoryMapSaved(const char *map)
{
    for (int i = 0; i < cof_story_map_count; i++) if (!strcmp(map, cof_story_maps[i])) return true;
    return false;
}
static void SV_FinalMessage(const char *message, qboolean reconnect) { (void)message; final_reconnects += reconnect; kept_during_final += sv_cof_keep_connections; }
static qboolean SV_SpawnServer(const char *level, const char *start, qboolean background)
{
    (void)start; (void)background;
    assert(sv.state == ss_dead);
    for (int i = 0; i < svs.maxclients; i++)
        if (svs.clients[i].state > cs_connected) svs.clients[i].state = cs_connected;
    Q_strncpy(sv.name, level, sizeof(sv.name));
    sv.state = ss_loading;
    return true;
}
static qboolean LoadGameState(const char *level, qboolean changelevel) { (void)level; (void)changelevel; return true; }
static void SV_SpawnEntities(const char *level) { (void)level; }
static void LoadAdjacentEnts(const char *old, const char *start) { (void)old; (void)start; }
static void ClearSaveDir(void) {}
static void SV_ActivateServer(qboolean physics) { (void)physics; sv.state = ss_active; }
#include "changelevel_under_test.c"

static void Setup(void)
{
    memset(clients, 0, sizeof(clients));
    svs.initialized = true; svs.maxclients = 4; svs.clients = clients;
    sv.state = ss_active; Q_strncpy(sv.name, "c_start", sizeof(sv.name));
    for (int i = 0; i < 4; i++) clients[i].netchan.remote_address.type = 10 + i;
    for (int i = 0; i < 3; i++)
    {
        clients[i].state = i == 2 ? cs_connected : cs_spawned; /* slot 2 is mid-connect */
        clients[i].frames = calloc((size_t)SV_UPDATE_BACKUP, sizeof(client_frame_t));
        clients[i].frames[3].marker = 99; /* stale delta history from the old map */
    }
    clients[3].state = cs_spawned; clients[3].flags = FCL_FAKECLIENT; /* bots own no frames */
    memset(executed, 0, sizeof(executed)); channel_processed = 0; final_reconnects = 0; kept_during_final = 0;
}

static void DeliverFromEveryConnection(void)
{
    queued_count = 0;
    for (int i = 0; i < 3; i++) queued[queued_count++] = clients[i].netchan.remote_address;
    SV_ReadPackets();
}

int main(void)
{
    Setup();
    story_enabled = true;
    SV_ChangeLevel(true, "c_start2", "hahaland", false);
    assert(transition_active && final_reconnects == 1 && sv.state == ss_active);
    for (int i = 0; i < 3; i++)
    {
        assert(clients[i].state == cs_connected);
        assert(clients[i].frames != NULL);         /* else SV_ReadPackets never runs "new" */
        assert(clients[i].frames[3].marker == 0);  /* fresh, as SV_ConnectClient would give */
    }
    assert(clients[3].frames == NULL);
    DeliverFromEveryConnection(); /* "new" sent on the kept netchan */
    assert(channel_processed == 3);
    assert(executed[0] == 1 && executed[1] == 1 && executed[2] == 1 && executed[3] == 0);

    /* A non-story changelevel is unchanged: frames are released and only the
     * client's fresh "connect" (SV_ConnectClient) reallocates them. */
    Setup();
    story_enabled = false;
    SV_ChangeLevel(true, "c_start2", "hahaland", false);
    assert(!transition_active && final_reconnects == 1);
    for (int i = 0; i < 4; i++) assert(clients[i].frames == NULL);
    DeliverFromEveryConnection();
    assert(executed[0] == 0 && executed[1] == 0 && executed[2] == 0);

    /* 1.3.5: Cry of Fear's own co-op: its doors' classic level change to another of its maps keeps the connections
     * as the story does (SV_FinalMessage told to keep them, the frames back), from the map left only */
    Setup();
    story_enabled = false; official_coop = true;
    Q_strncpy(sv.name, "cof_campaign_01_p2", sizeof(sv.name));
    SV_ChangeLevel(false, "cof_campaign_01_p3", NULL, false);
    assert(final_reconnects == 1 && kept_during_final == 1 && !sv_cof_keep_connections && sv.state == ss_active);
    for (int i = 0; i < 3; i++)
        assert(clients[i].state == cs_connected && clients[i].frames != NULL && clients[i].frames[3].marker == 0);
    assert(clients[3].frames == NULL);
    DeliverFromEveryConnection();
    assert(executed[0] == 1 && executed[1] == 1 && executed[2] == 1 && executed[3] == 0);
    /* to a map that is not one of its own: the generic reconnect, as before */
    Setup();
    Q_strncpy(sv.name, "cof_campaign_01_p2", sizeof(sv.name));
    SV_ChangeLevel(false, "c_start", NULL, false);
    assert(final_reconnects == 1 && kept_during_final == 0);
    for (int i = 0; i < 4; i++) assert(clients[i].frames == NULL);
    /* no game of its own co-op running: the same */
    Setup();
    official_coop = false;
    Q_strncpy(sv.name, "cof_campaign_01_p2", sizeof(sv.name));
    SV_ChangeLevel(false, "cof_campaign_01_p3", NULL, false);
    assert(final_reconnects == 1 && kept_during_final == 0);
    for (int i = 0; i < 4; i++) assert(clients[i].frames == NULL);

    puts("story server transition keeps client connections passed");
    return 0;
}
