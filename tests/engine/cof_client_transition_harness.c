#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef int qboolean;
#define true 1
#define false 0
#define ca_active 3
#define ca_connected 2
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
    puts("story client transition entity lifetime passed");
    return 0;
}
