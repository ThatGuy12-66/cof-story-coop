#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef int qboolean;
#define true 1
#define false 0
#define ca_active 3
#define ca_connected 2
#define ca_validate 5
#define FWORLD_WATERALPHA 1
#define FBitSet(value, flag) ((value) & (flag))
#define Q_strcmp strcmp
typedef struct { int unused; } sizebuf_t;
typedef struct { float wateralpha, waveHeight; char skyName[32]; int features; } movevars_t;
typedef struct { struct { float scale; } curstate; } entity_t;
static struct { int state, demoplayback, changelevel; } cls;
static struct { int maxclients, video_prepped; } cl;
static struct { entity_t *entities; movevars_t oldmovevars, movevars; } clgame;
static struct { int flags; } world;
static struct { int features; } host;
static entity_t entities[2];
static int clears, inits, reconnects, loading, preserved_clients;
static void Delta_InitClient(void) {}
static void MSG_ReadDeltaMovevars(sizebuf_t *msg, movevars_t *old, movevars_t *value)
{ (void)msg; (void)old; value->waveHeight = 7; }
static void R_SetupSky(const char *name) { (void)name; }
#include "movevars_under_test.c"
static void SCR_BeginLoadingPlaque(qboolean background) { assert(!background); loading++; }
static void CL_ClearState(void)
{ clears++; clgame.entities = NULL; memset(&cl, 0, sizeof(cl)); cl.maxclients = 1; }
static void CL_InitEdicts(int clients)
{ assert(clgame.entities == NULL); inits++; preserved_clients = clients; clgame.entities = entities; }
static void Con_Reportf(const char *format, ...) { (void)format; }
/* 1.1.8: the notice carries the spawncount of the map left */
static int cl_cof_story_epoch = -1;
static int argc = 1;
static const char *argv1 = "";
static int Cmd_Argc(void) { return argc; }
static const char *Cmd_Argv(int i) { return i == 1 ? argv1 : "cof_story_changelevel"; }
static int Q_atoi(const char *s) { int v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0'); return v; }
static void CL_Reconnect(qboolean setup_netchan)
{
    sizebuf_t message = {0};
    assert(!setup_netchan); /* Reuse this session, not a fresh userid. */
    reconnects++;
    cls.state = ca_connected;
    /* Actual movevars parser runs before signon: it requires world edict 0. */
    assert(clgame.entities != NULL);
    CL_ParseMovevars(&message);
    assert(clgame.entities->curstate.scale == 7);
}
#include "transition_under_test.c"
int main(void)
{
    cls.state = ca_active; cl.maxclients = 2; clgame.entities = entities;
    CL_CoF_StoryChangeLevel_f();
    assert(clears == 1 && inits == 1 && reconnects == 1 && loading == 1);
    assert(preserved_clients == 2 && cls.changelevel);
    CL_CoF_StoryChangeLevel_f(); /* The server sends its final message twice. */
    assert(clears == 1 && reconnects == 1);
    cls.state = ca_active; cls.demoplayback = true;
    CL_CoF_StoryChangeLevel_f();
    assert(clears == 1 && reconnects == 1);
    /* 1.1.8: a notice with the spawncount of the map left: acted on once, its second copy ignored */
    cls.demoplayback = false; cls.state = ca_active; cl.maxclients = 2; clgame.entities = entities;
    clears = inits = reconnects = loading = 0; argc = 2; argv1 = "7";
    CL_CoF_StoryChangeLevel_f();
    assert(clears == 1 && reconnects == 1 && cl_cof_story_epoch == 7);
    CL_CoF_StoryChangeLevel_f();
    assert(clears == 1 && reconnects == 1);
    /* ...a guest still signing on (it was loading when the party left) starts again on the new map */
    cls.state = ca_connected; argv1 = "8";
    CL_CoF_StoryChangeLevel_f();
    assert(clears == 1 && reconnects == 2 && cl_cof_story_epoch == 8);
    CL_CoF_StoryChangeLevel_f(); /* told again before its "new" arrived: already acted on */
    assert(reconnects == 2);
    /* ...an old server's notice (no count) still needs a client in game */
    argc = 1; argv1 = "";
    CL_CoF_StoryChangeLevel_f();
    assert(reconnects == 2);
    puts("story client transition entity lifetime passed");
    return 0;
}
