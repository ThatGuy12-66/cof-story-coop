/* Offline engine doubles: no game DLL, networking, or game process. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdarg.h>
#include <stddef.h>
#include <math.h>
typedef unsigned char byte;
typedef int qboolean;
typedef int string_t;
typedef float vec3_t[3];
#define true 1
#define false 0
#define MAX_CLIENTS 4
#define cs_spawned 4
#define ss_active 2
#define DEAD_NO 0
#define MOVETYPE_WALK 3
#define FL_FROZEN (1<<12)
#define FL_NOTARGET (1<<7)
#define DAMAGE_NO 0
#define DAMAGE_AIM 2
#define FL_FAKECLIENT (1<<13)
#define FCL_FAKECLIENT (1<<7)
#define FL_ONGROUND (1<<9)
#define FL_KILLME (1<<30)
#define EF_NODRAW 128
#define MOVE_NORMAL 0
#define MOVE_NOMONSTERS 1
#define ARRAYSIZE(x) (sizeof(x)/sizeof((x)[0]))
#define Q_strcmp strcmp
#define Q_strncmp strncmp
#define Q_atoi atoi
#define Q_snprintf snprintf
#define Q_strlen strlen
static void Q_strncpy(char *d,const char *s,size_t n) { size_t i; for(i=0;i+1<n && s[i];i++) d[i]=s[i]; d[i]=0; }
#define VectorCopy(a,b) memcpy((b),(a),sizeof(vec3_t))
#define VectorAdd(a,b,c) ((c)[0]=(a)[0]+(b)[0],(c)[1]=(a)[1]+(b)[1],(c)[2]=(a)[2]+(b)[2])
#define VectorClear(a) memset((a),0,sizeof(vec3_t))
typedef struct edict_s edict_t;
typedef struct {
    string_t classname,target,targetname,model,message;
    vec3_t origin,mins,maxs,angles,v_angle,view_ofs,velocity,basevelocity,absmin,absmax;
    float health,takedamage;
    int deadflag,movetype,flags,effects,fixangle,spawnflags;
    edict_t *owner,*aiment;
} entvars_t;
struct edict_s { int free,serialnumber; void *pvPrivateData; entvars_t v; };
typedef struct { int state,userid; unsigned int flags; char name[32]; edict_t *edict,*pViewEntity; } sv_client_t;
typedef struct { qboolean allsolid,startsolid; float fraction; vec3_t endpos;
    struct {vec3_t normal;} plane; edict_t *ent; } trace_t;
static struct { int state,paused; double time; const char *name; } sv;
static struct { int maxclients,spawncount; sv_client_t *clients; } svs;
static struct { int numEntities; edict_t *edicts; } svgame;
static edict_t entities[32];
static sv_client_t clients[MAX_CLIENTS];
static byte private_data[MAX_CLIENTS][0x2600];
static const char *strings[] = {"", "player", "weapon_mobile", "weapon_switchblade",
    "inter_door", "inter_door_exit", "exit-a", "weapon_glock", "other",
    "inventoryitems/weapons/weapon_mobile.txt",
    "inventoryitems/weapons/weapon_switchblade.txt",
    "inventoryitems/weapons/weapon_glock.txt",
    "inventoryitems/other/weapon_mobile.txt",
    "inventoryitems/weapons/weapon_mobile.txt.bak",
    "inventoryitems/weapons/../weapon_mobile.txt", "info_landmark", "hahaland",
    "info_teleport_destination", "cof_teleport", "tp-dest", "trigger_teleport",
    "trigger_changelevel", "trigger_multiple", "cl-exit", "*1", "*2",
    "endingcheck", "ending_exit4", "ending_exit3", "cof_giveitems"};
static char status_log[16384];
static size_t status_length;
static int gate, give_ok, give_actual, give_calls, links, blocked, narrow_direction;
static int solid_probe; static float solid_x; /* one point of solid world for hull tests */
static int no_floor; static float no_floor_x; /* a shaft: nothing below this x */
static float front_only_x; /* walls everywhere short of this x: only spots past it can be reached */
static char prompts[MAX_CLIENTS][192]; /* the last ProFont line each player was sent */
static const char *command_name=""; static int exit_key,exit_waiter,fades;
typedef struct { float value; } convar_t;
static convar_t respawn_delay={10};
static convar_t *Cvar_Get(const char *name,const char *value,int flags,const char *description) {
    assert(!strcmp(name,"cof_story_respawn_delay") && !strcmp(value,"10")); return &respawn_delay;
}
static qboolean SV_CoF_StoryCoopEnabled(void) { return gate; }
static qboolean SV_CoF_RetailDLLVerified(void) { return gate; }
static qboolean SV_IsValidEdict(edict_t *e) {
    return e && e>=entities && e<entities+32 && !e->free;
}
static edict_t *SV_EdictNum(int i) { return &entities[i]; }
static const char *SV_GetString(string_t s) {
    assert(s>=0 && s<(int)ARRAYSIZE(strings)); return strings[s];
}
static byte *SV_CoF_StoryEntityPrivate(edict_t *e,size_t need) {
    if(!SV_IsValidEdict(e) || !e->pvPrivateData || need>0x2600) return NULL;
    return e->pvPrivateData;
}
static qboolean SV_CoF_StoryGiveItem(sv_client_t *cl,const char *name) {
    int i;
    string_t item=!strcmp(name,"weapon_mobile") ? 9 : !strcmp(name,"weapon_glock") ? 11 : 10;
    assert(!strcmp(name,"weapon_mobile") || !strcmp(name,"weapon_switchblade") || !strcmp(name,"weapon_glock"));
    give_calls++;
    if(give_ok && give_actual) for(i=1;i<7;i++) {
        string_t old;
        byte *p=(byte *)cl->edict->pvPrivateData+0x21FC+i*sizeof(item);
        memcpy(&old,p,sizeof(old));
        if(!old) {memcpy(p,&item,sizeof(item)); break;}
    }
    return give_ok;
}
static void Con_Printf(const char *fmt,...) {
    int n; va_list args;
    va_start(args,fmt);
    n=vsnprintf(status_log+status_length,sizeof(status_log)-status_length,fmt,args);
    va_end(args);
    assert(n>=0 && (size_t)n<sizeof(status_log)-status_length);
    status_length+=(size_t)n;
}
static void SV_LinkEdict(edict_t *e,qboolean touch) { assert(!touch); links++; }
static qboolean SV_CoF_RetailProFont(sv_client_t *cl,int colour,const char *text,qboolean reliable) {
    snprintf(prompts[cl-clients],sizeof(prompts[0]),"%d:%s",colour,text); return true;
}
static const char *Cmd_Argv(int i) { return i==0 ? command_name : ""; }
static qboolean SV_CoF_ExitWarpKey(sv_client_t *cl) { return exit_key; }
static int scene_watching; /* userid of the partner watching userid 100's cutscene */
static qboolean SV_CoF_SceneWatching(int owner,int userid) { return owner==100 && userid==scene_watching; }
static int SV_CoF_ExitWaiter(void) { return exit_waiter; }
static char gift_log[256]; /* the story gifts reported to the test gun, "userid:class;" */
static void SV_CoF_TestGunGift(sv_client_t *cl,const char *name) {
    size_t n=strlen(gift_log); snprintf(gift_log+n,sizeof(gift_log)-n,"%d:%s;",cl->userid,name);
}
/* the companion module told that the party code moved a player (a bot starts afresh beside its person) */
static int companion_moved;
static void SV_CoF_CompanionMoved(sv_client_t *cl) { companion_moved++; }
static int ending_fades_out,ending_fades_in;
static qboolean SV_CoF_RetailScreenFade(sv_client_t *cl,float duration,float hold,int flags,int r,int g,int b,int a) {
    assert(duration==0.5f && r==0 && g==0 && b==0 && a==255);
    if(hold==2.0f) { assert(flags==5); ending_fades_out++; return true; } /* the retail door's black-out */
    assert(hold==0.0f && flags==0); fades++; ending_fades_in++; return true;
}
static trace_t SV_Move(const vec3_t start,vec3_t mins,vec3_t maxs,
    const vec3_t end,int type,edict_t *ignore,qboolean monsterclip) {
    trace_t t; memset(&t,0,sizeof(t)); t.fraction=1; VectorCopy(end,t.endpos);
    if(blocked) {t.startsolid=true; return t;}
    if(solid_probe && start[0]==solid_x && end[0]==solid_x) {t.startsolid=true; return t;}
    /* Retail hahaland staging rooms only fit the follower at +/-34 Y.
       The world rejects all 40/72 candidates; retain the occupied leader. */
    if(narrow_direction && type==MOVE_NORMAL &&
       (end[0]!=512 || end[1]!=(float)(34*narrow_direction))) {
        t.startsolid=true; return t;
    }
    if(front_only_x && end[0]<front_only_x && end[2]>=start[2]) {t.fraction=0.5f; return t;}
    if(end[2]<start[2] && !(no_floor && start[0]==no_floor_x)) {t.fraction=0.1f; t.plane.normal[2]=1; t.endpos[2]=start[2]-1;}
    return t;
}

#include "party_under_test.c"
#include "status_under_test.c"

static void setup(void) {
    int i;
    memset(entities,0,sizeof(entities)); memset(clients,0,sizeof(clients));
    memset(private_data,0,sizeof(private_data)); memset(prompts,0,sizeof(prompts));
    exit_key=exit_waiter=fades=scene_watching=companion_moved=0; ending_fades_out=ending_fades_in=0; gift_log[0]=0;
    gate=true; give_ok=give_actual=true; give_calls=links=blocked=narrow_direction=solid_probe=no_floor=0; front_only_x=0;
    status_log[0]=0; status_length=0; sv.name="c_start";
    sv.state=ss_active; sv.time=10; svs.clients=clients; svs.maxclients=2;
    svs.spawncount++; svgame.edicts=entities; svgame.numEntities=12;
    for(i=0;i<2;i++) {
        clients[i].state=cs_spawned; clients[i].userid=100+i;
        snprintf(clients[i].name,sizeof(clients[i].name),"Little Rascal %d",i+1);
        clients[i].edict=&entities[i+1]; entities[i+1].serialnumber=10+i;
        entities[i+1].pvPrivateData=private_data[i]; entities[i+1].v.classname=1;
        entities[i+1].v.health=100; entities[i+1].v.movetype=MOVETYPE_WALK;
        entities[i+1].v.mins[0]=entities[i+1].v.mins[1]=-16;
        entities[i+1].v.maxs[0]=entities[i+1].v.maxs[1]=16;
        entities[i+1].v.mins[2]=-36; entities[i+1].v.maxs[2]=36;
        entities[i+1].v.origin[0]=(float)i*64;
    }
    SV_CoF_PartyShutdown();
}
static void tick(double dt) { sv.time+=dt; SV_CoF_PartyFrame(); }
static void warp(int p) { command_name="cof_warp"; assert(SV_CoF_PartyClientCommand(&clients[p])); }
static int logged(const char *phase) {
    int n=0; const char *at=status_log;
    while((at=strstr(at,phase))!=NULL) {n++; at++;}
    return n;
}
static void absbox(edict_t *e,float x,float half) {
    e->v.absmin[0]=x-half; e->v.absmin[1]=e->v.absmin[2]=-half;
    e->v.absmax[0]=x+half; e->v.absmax[1]=e->v.absmax[2]=half;
}
/* Leader at x=0 inside a walk-in exit (entity 12); partner at x=px. */
static void exit_setup(float px) {
    setup(); tick(0.1); svgame.numEntities=16;
    entities[12].v.classname=21; absbox(&entities[12],0,8);
    entities[2].v.origin[0]=px; absbox(&entities[1],0,17); absbox(&entities[2],px,17);
}
/* The level changes: both clients reconnect, restored at translated spots. */
static void arrive(float leader_x,float partner_x) {
    svs.spawncount++; clients[0].state=clients[1].state=3;
    entities[1].v.origin[0]=leader_x; entities[2].v.origin[0]=partner_x;
    SV_CoF_PartyTransitionArrive((unsigned int)svs.spawncount);
}
static void inventory(int player,int slot,string_t item) {
    string_t descriptor=item==2 ? 9 : item==3 ? 10 : item==7 ? 11 : item;
    assert(slot>=0 && slot<6);
    memcpy(private_data[player]+0x2200+slot*sizeof(string_t),&descriptor,sizeof(descriptor));
}
static void start_door_without_baseline(void) {
    edict_t *door=&entities[5],*exit=&entities[6];
    door->v.classname=4; door->v.target=6; door->v.owner=&entities[1];
    door->serialnumber=15;
    exit->v.classname=5; exit->v.targetname=6; exit->v.origin[0]=512;
    private_data[0][0x1EA4]=1; entities[1].v.flags|=FL_FROZEN;
    tick(0.1);
}
static void start_door(void) { tick(0.01); start_door_without_baseline(); }
static void finish_door(void) {
    private_data[0][0x1EA4]=0; entities[1].v.flags&=~FL_FROZEN;
    VectorCopy(entities[6].v.origin,entities[1].v.origin);
    entities[1].v.origin[2]-=entities[1].v.mins[2];
    entities[1].v.origin[2]+=1;
}
int main(void) {
    vec3_t before,previous;
    /* The retail classname mirror remains zero while descriptor slots work. */
    setup(); inventory(0,0,2); inventory(0,1,3); inventory(0,2,7);
    assert(CoF_PartyInventory(&clients[0])==3);
    assert(CoF_PartyInventory(&clients[1])==0);
    entities[7].v.classname=15; entities[7].v.targetname=16;
    entities[7].v.origin[0]=2424; entities[7].v.origin[1]=-908; entities[7].v.origin[2]=-694;
    CoF_StoryStatus_f();
    assert(strstr(status_log,"index=0 descriptor=\"\""));
    assert(strstr(status_log,"index=1 descriptor=\"inventoryitems/weapons/weapon_mobile.txt\""));
    assert(strstr(status_log,"index=3 descriptor=\"inventoryitems/weapons/weapon_glock.txt\""));
	assert(strstr(status_log,"index=12 descriptor=\"\"") && !strstr(status_log,"index=13 "));
    puts(status_log); /* The Python verifier consumes actual native status output. */
    /* A stale legacy mirror, unused slot0, and lookalike paths prove nothing. */
    setup(); {string_t legacy=2,unused=9;
        memcpy(private_data[0]+0x2234,&legacy,sizeof(legacy));
        memcpy(private_data[0]+0x21FC,&unused,sizeof(unused));}
    assert(CoF_PartyInventory(&clients[0])==0);
    inventory(0,0,12); inventory(0,1,13); inventory(0,2,14);
    assert(CoF_PartyInventory(&clients[0])==0);
    setup(); inventory(0,5,2); assert(CoF_PartyInventory(&clients[0])==1);
    setup(); gate=false; inventory(0,0,2); tick(2); assert(give_calls==0);
    setup(); tick(2); tick(2); assert(give_calls==0); /* no invented equipment */
    setup(); inventory(0,0,2); inventory(0,1,3); inventory(0,2,7);
    tick(2); tick(2); assert(give_calls==2); tick(3); assert(give_calls==2);
    setup(); inventory(0,0,2); inventory(1,0,2); tick(2);
    inventory(1,0,0); tick(3); assert(give_calls==0); /* intentional drop */
    setup(); inventory(0,0,2); give_ok=false; tick(2); tick(2);
    {int n=give_calls; tick(0.1); assert(give_calls==n);}
    give_ok=true; tick(5); assert(give_calls>=2);
    setup(); inventory(0,0,2); clients[1].pViewEntity=&entities[9];
    tick(2); tick(2); assert(give_calls==0);
    setup(); inventory(0,0,2); entities[2].v.health=0;
    tick(2); tick(2); assert(give_calls==0);
    setup(); inventory(0,0,2); tick(2); tick(2); assert(give_calls==1);
    clients[1].userid++; inventory(1,0,0); SV_CoF_PartyPlayerReady(&clients[1],false); tick(2); assert(give_calls==2);
    setup(); entities[8].v.classname=2; entities[8].v.owner=&entities[1];
    tick(2); tick(2); assert(give_calls==0); /* ownership alone is not a slot */
    setup(); start_door(); assert(links==0); VectorCopy(entities[2].v.origin,before);
    entities[5].free=1; finish_door(); tick(0.1); assert(links==1);
    assert(memcmp(before,entities[2].v.origin,sizeof(before)));
    memcpy(previous,private_data[1]+0x618,sizeof(previous));
    assert(!memcmp(previous,entities[2].v.origin,sizeof(previous)));
    tick(0.1); assert(links==1); /* one-shot after source deletion */
    setup(); start_door(); private_data[0][0x1EA4]=0;
    entities[1].v.flags=0; tick(0.1); assert(links==0); /* cancelled */
    setup(); start_door(); finish_door(); blocked=1;
    tick(0.1); tick(8); blocked=0; tick(0.1); assert(links==0);
    setup(); entities[2].v.effects|=EF_NODRAW; start_door(); finish_door(); tick(0.1);
    assert(links==0 && (entities[2].v.effects&EF_NODRAW));
    setup(); start_door(); clients[1].userid++; finish_door(); tick(0.1); assert(links==0);
    setup(); start_door(); svs.spawncount++; finish_door(); tick(0.1); assert(links==0);
    setup(); start_door(); entities[2].v.health=0; finish_door(); tick(0.1); assert(links==0);
    setup(); start_door(); private_data[0][0x1EA4]=2; tick(0.1); assert(links==0);
    setup(); start_door(); finish_door(); entities[1].v.effects|=EF_NODRAW; tick(0.1);
    assert(links==1 && !(entities[1].v.effects&EF_NODRAW)); /* owned door hide */
    setup(); entities[1].v.effects|=EF_NODRAW; start_door(); finish_door(); tick(0.1);
    assert(!(entities[1].v.effects&EF_NODRAW)); /* stale hide from an earlier door */
    setup(); start_door_without_baseline(); finish_door(); entities[1].v.effects|=EF_NODRAW;
    tick(0.1); assert(!(entities[1].v.effects&EF_NODRAW)); /* retail door hide, no baseline needed */
    setup(); start_door(); finish_door(); entities[1].v.effects|=EF_NODRAW;
    clients[0].pViewEntity=&entities[9]; tick(0.1); assert(entities[1].v.effects&EF_NODRAW);
    clients[0].pViewEntity=NULL; tick(0.1); assert(!(entities[1].v.effects&EF_NODRAW)); /* after scene */
    setup(); start_door(); private_data[0][0x1EA4]=0; entities[1].v.effects|=EF_NODRAW;
    tick(0.1); assert(entities[1].v.effects&EF_NODRAW); /* still frozen by the door scene */
    entities[1].v.flags&=~FL_FROZEN; tick(0.1);
    assert(!(entities[1].v.effects&EF_NODRAW)); /* cancelled door still owes its hide back */
    setup(); inventory(0,0,2); give_actual=false; tick(2); tick(2); assert(give_calls==1);
    tick(6); assert(give_calls==2); /* helper return alone is not proof */
    setup(); start_door(); finish_door(); entities[2].v.flags|=FL_FROZEN;
    tick(0.1); assert(links==0); entities[2].v.flags&=~FL_FROZEN;
    tick(1); assert(links==1); /* temporary scene must not abandon follower */
    setup(); start_door(); finish_door(); clients[1].pViewEntity=&entities[9];
    tick(0.1); assert(links==0); clients[1].pViewEntity=NULL;
    tick(1); assert(links==1);
    setup(); start_door(); finish_door(); entities[2].v.flags|=FL_FROZEN;
    tick(0.1); tick(6); entities[2].v.flags&=~FL_FROZEN;
    tick(0.1); assert(links==0); /* scene wait still expires */
    setup(); start_door(); finish_door(); entities[2].v.health=0;
    tick(0.1); entities[2].v.health=100; tick(1);
    assert(!strstr(status_log,"door-party-moved")); /* death cancels the door follow */
    assert(strstr(status_log,"spawn-placed userid=101")); /* the respawn is placed instead */
    setup(); start_door(); finish_door(); narrow_direction=1; tick(0.1);
    assert(links==1 && entities[2].v.origin[0]==512 && entities[2].v.origin[1]==34);
    setup(); start_door(); finish_door(); narrow_direction=-1; tick(0.1);
    assert(links==1 && entities[2].v.origin[0]==512 && entities[2].v.origin[1]==-34);
    /* Followers land 35 units out: padded absboxes (+/-17) must not touch, or
       retail PreThink makes both players SOLID_NOT and triggers stop firing. */
    setup(); start_door(); finish_door(); tick(0.1);
    assert(links==1 && entities[2].v.origin[0]==512+35 && entities[2].v.origin[1]==0);
    assert(!CoF_PartyBoxesTouch(&entities[1],&entities[2]));
    {edict_t a,b; memset(&a,0,sizeof(a)); memset(&b,0,sizeof(b));
     a.v.mins[0]=a.v.mins[1]=b.v.mins[0]=b.v.mins[1]=-16; a.v.maxs[0]=a.v.maxs[1]=b.v.maxs[0]=b.v.maxs[1]=16;
     a.v.mins[2]=b.v.mins[2]=-36; a.v.maxs[2]=b.v.maxs[2]=36;
     b.v.origin[1]=34; assert(CoF_PartyBoxesTouch(&a,&b)); /* the live 34-unit deadlock */
     b.v.origin[1]=35; assert(!CoF_PartyBoxesTouch(&a,&b));}
    /* Live 19:13:34: door used while standing on its own exit (start==exit).
       It completed; the follower must still come and the hide must go. */
    setup(); tick(0.01); entities[1].v.origin[0]=512; entities[1].v.origin[2]=37; entities[2].v.origin[0]=448; /* beside the opener, who is already at the exit */
    start_door_without_baseline(); finish_door(); entities[1].v.effects|=EF_NODRAW; tick(0.1);
    assert(!strstr(status_log,"door-cancelled")); assert(links==1);
    assert(!(entities[1].v.effects&EF_NODRAW));
    /* A fresh join spawned far away is placed beside the available partner. */
    setup(); tick(0.1); entities[2].v.origin[0]=3000; SV_CoF_PartyPlayerReady(&clients[1],true);
    tick(0.1); assert(links==1 && entities[2].v.origin[0]==35);
    assert(strstr(status_log,"spawn-placed userid=101 partner=100"));
    /* Spawned inside the partner (the live c_start2 case): also placed. */
    setup(); tick(0.1); entities[2].v.origin[0]=5; SV_CoF_PartyPlayerReady(&clients[1],true);
    tick(0.1); assert(links==1 && !CoF_PartyBoxesTouch(&entities[1],&entities[2]));
    /* Already beside the partner without overlap: left alone. */
    setup(); tick(0.1); SV_CoF_PartyPlayerReady(&clients[1],true); tick(0.1);
    assert(links==0 && strstr(status_log,"spawn-kept userid=101 reason=partner-near"));
    /* The engine calls PlayerReady before cs_spawned; adopt later by userid. */
    setup(); tick(0.1); entities[2].v.origin[0]=3000; clients[1].state=3;
    SV_CoF_PartyPlayerReady(&clients[1],true); tick(0.1); assert(links==0);
    clients[1].state=cs_spawned; tick(0.1); assert(links==1 && entities[2].v.origin[0]==35);
    /* Partner in an intro camera: wait, then place when it is released. */
    setup(); tick(0.1); entities[2].v.origin[0]=3000; clients[0].pViewEntity=&entities[9];
    SV_CoF_PartyPlayerReady(&clients[1],true); tick(1); assert(links==0);
    clients[0].pViewEntity=NULL; tick(0.1); assert(links==1);
    /* ...but never after the wait expires, nor once the joiner walked off. */
    setup(); tick(0.1); entities[2].v.origin[0]=3000; entities[1].v.flags|=FL_FROZEN;
    SV_CoF_PartyPlayerReady(&clients[1],true); tick(31); entities[1].v.flags&=~FL_FROZEN;
    tick(0.1); assert(links==0);
    setup(); tick(0.1); entities[2].v.origin[0]=3000; entities[1].v.flags|=FL_FROZEN;
    SV_CoF_PartyPlayerReady(&clients[1],true); tick(0.1); entities[2].v.origin[0]=3100;
    entities[1].v.flags&=~FL_FROZEN; tick(0.1);
    assert(links==0 && strstr(status_log,"spawn-kept userid=101 reason=player-moved"));
    /* A transition restore keeps its translated position. */
    setup(); tick(0.1); entities[2].v.origin[0]=3000; SV_CoF_PartyPlayerReady(&clients[1],false);
    tick(0.1); tick(1); assert(links==0);
    /* A respawn at a far spawn point is placed; a mere revival in place too. */
    setup(); tick(0.1); entities[2].v.health=0; entities[2].v.deadflag=2; tick(0.1);
    entities[2].v.health=100; entities[2].v.deadflag=DEAD_NO; entities[2].v.origin[0]=3000;
    tick(0.1); assert(links==1 && entities[2].v.origin[0]==35);
    /* A cof_teleport moving one player brings the far partner along. */
    setup(); entities[10].v.classname=17; entities[10].v.targetname=19;
    entities[10].v.origin[0]=2000; entities[10].v.origin[2]=0;
    entities[11].v.classname=18; entities[11].v.target=19;
    entities[2].v.origin[0]=64; tick(0.1);
    VectorCopy(entities[1].v.origin,before); entities[1].v.origin[0]=2000; entities[1].v.angles[1]=90;
    SV_CoF_PartyNoteOrigin(&entities[1],before); tick(0.1);
    assert(strstr(status_log,"teleport-begin userid=100 target=tp-dest companions=1") && companion_moved==1);
    assert(links==1 && entities[2].v.origin[0]==2035 && entities[2].v.angles[1]==90);
    assert(strstr(status_log,"teleport-party-moved userid=101 owner=100"));
    /* ...into a room only one fits (c_intro `kunitei`, live 2026-10-12): the partner
       shares the spot instead of being left behind when the wait runs out. */
    setup(); entities[10].v.classname=17; entities[10].v.targetname=19;
    entities[10].v.origin[0]=2000; entities[10].v.origin[2]=0;
    entities[11].v.classname=18; entities[11].v.target=19;
    entities[2].v.origin[0]=64; tick(0.1);
    VectorCopy(entities[1].v.origin,before); entities[1].v.origin[0]=2000;
    SV_CoF_PartyNoteOrigin(&entities[1],before); blocked=1; tick(0.1); blocked=0;
    assert(links==1 && entities[2].v.origin[0]==entities[1].v.origin[0] && entities[2].v.origin[1]==entities[1].v.origin[1]);
    assert(strstr(status_log,"teleport-party-moved userid=101 owner=100") && !strstr(status_log,"teleport-expired"));
    /* ...the shared spot is where the owner landed, though they walked on meanwhile. */
    setup(); entities[10].v.classname=17; entities[10].v.targetname=19;
    entities[10].v.origin[0]=2000; entities[10].v.origin[2]=0;
    entities[11].v.classname=18; entities[11].v.target=19;
    entities[2].v.origin[0]=64; tick(0.1);
    entities[2].v.flags|=FL_FROZEN; VectorCopy(entities[1].v.origin,before); entities[1].v.origin[0]=2000;
    SV_CoF_PartyNoteOrigin(&entities[1],before); tick(0.1); assert(links==0);
    entities[1].v.origin[0]=2100; entities[2].v.flags&=~FL_FROZEN; blocked=1; tick(0.1); blocked=0;
    assert(links==1 && entities[2].v.origin[0]==2000 && entities[2].v.origin[2]==entities[1].v.origin[2]);
    /* ...waits for a partner busy in its own scene, bounded by the deadline. */
    setup(); entities[10].v.classname=17; entities[10].v.targetname=19; entities[10].v.origin[0]=2000;
    entities[11].v.classname=18; entities[11].v.target=19; entities[2].v.origin[0]=64; tick(0.1);
    entities[2].v.flags|=FL_FROZEN; VectorCopy(entities[1].v.origin,before); entities[1].v.origin[0]=2000;
    SV_CoF_PartyNoteOrigin(&entities[1],before); tick(0.1); assert(links==0);
    entities[2].v.flags&=~FL_FROZEN; tick(0.1); assert(links==1);
    /* A walk-in trigger_teleport destination is not followed. */
    setup(); entities[10].v.classname=17; entities[10].v.targetname=19; entities[10].v.origin[0]=2000;
    entities[11].v.classname=20; entities[11].v.target=19; entities[2].v.origin[0]=64; tick(0.1);
    VectorCopy(entities[1].v.origin,before); entities[1].v.origin[0]=2000;
    SV_CoF_PartyNoteOrigin(&entities[1],before); tick(0.1); assert(links==0);
    /* Nor is a short move, a door's own relocation, or a partner already there. */
    setup(); entities[10].v.classname=17; entities[10].v.targetname=19; entities[10].v.origin[0]=60;
    entities[11].v.classname=18; entities[11].v.target=19; entities[2].v.origin[0]=64; tick(0.1);
    VectorCopy(entities[1].v.origin,before); entities[1].v.origin[0]=60;
    SV_CoF_PartyNoteOrigin(&entities[1],before); tick(0.1); assert(links==0);
    assert(!strstr(status_log,"teleport-begin"));
    setup(); entities[10].v.classname=17; entities[10].v.targetname=19; entities[10].v.origin[0]=2000;
    entities[11].v.classname=18; entities[11].v.target=19; entities[2].v.origin[0]=64; tick(0.1);
    private_data[0][0x1EA4]=1; VectorCopy(entities[1].v.origin,before); entities[1].v.origin[0]=2000;
    SV_CoF_PartyNoteOrigin(&entities[1],before); tick(0.1); assert(!strstr(status_log,"teleport-begin"));
    setup(); entities[10].v.classname=17; entities[10].v.targetname=19; entities[10].v.origin[0]=2000;
    entities[11].v.classname=18; entities[11].v.target=19; entities[2].v.origin[0]=2060; tick(0.1);
    VectorCopy(entities[1].v.origin,before); entities[1].v.origin[0]=2000;
    SV_CoF_PartyNoteOrigin(&entities[1],before); tick(0.1);
    assert(links==0 && strstr(status_log,"companions=0"));
    /* Review: the activator may walk on before the post-frame hook; match the
       landing recorded at SetOrigin, not the later origin. */
    setup(); entities[10].v.classname=17; entities[10].v.targetname=19; entities[10].v.origin[0]=2000;
    entities[11].v.classname=18; entities[11].v.target=19; entities[2].v.origin[0]=64; tick(0.1);
    VectorCopy(entities[1].v.origin,before); entities[1].v.origin[0]=2000;
    SV_CoF_PartyNoteOrigin(&entities[1],before); entities[1].v.origin[0]=2050; tick(0.1);
    assert(strstr(status_log,"teleport-begin") && links==1);
    /* Review: a door taken while a teleport still waits on a busy partner is
       not swallowed by the pending event. */
    setup(); entities[10].v.classname=17; entities[10].v.targetname=19; entities[10].v.origin[0]=2000;
    entities[11].v.classname=18; entities[11].v.target=19; entities[2].v.origin[0]=64; tick(0.1);
    entities[2].v.flags|=FL_FROZEN; VectorCopy(entities[1].v.origin,before); entities[1].v.origin[0]=2000;
    SV_CoF_PartyNoteOrigin(&entities[1],before); tick(0.1); assert(links==0);
    start_door_without_baseline(); assert(strstr(status_log,"door-begin userid=100"));
    /* Review: a door straight into a level change still owes its hide back. */
    setup(); tick(0.1); private_data[0][0x1EA4]=1; entities[1].v.flags|=FL_FROZEN; tick(0.1);
    svs.spawncount++; private_data[0][0x1EA4]=0; entities[1].v.effects|=EF_NODRAW; tick(0.1);
    assert(entities[1].v.effects&EF_NODRAW); /* still frozen after arrival */
    entities[1].v.flags&=~FL_FROZEN; tick(0.1); assert(!(entities[1].v.effects&EF_NODRAW));
    /* Non-player SetOrigin calls are ignored cheaply. */
    setup(); tick(0.1); VectorCopy(entities[10].v.origin,before); entities[10].v.origin[0]=5000;
    SV_CoF_PartyNoteOrigin(&entities[10],before); tick(0.1); assert(!strstr(status_log,"teleport"));
    /* Story exit: the partner was across the map when the leader walked into a
       trigger_changelevel. Carried at the landmark offset it would land in the
       void; it is placed beside the leader before either client signs on. */
    {vec3_t landmark={3000,0,0}; /* nearer the partner: the exit decides */
    exit_setup(3000); SV_CoF_PartyTransitionCapture((unsigned int)svs.spawncount+1,landmark,NULL,0);
    assert(strstr(status_log,"exit-leader userid=100 in_exit=1 strays=1"));
    arrive(1000,4000);
    assert(links==1 && entities[2].v.origin[0]==1035 && entities[2].v.origin[1]==0);
    assert(strstr(status_log,"arrival-placed userid=101 beside=100"));
    tick(0.1); clients[0].state=clients[1].state=cs_spawned; tick(0.1); tick(1);
    assert(links==1); /* not moved again after sign-on */
    /* A scripted exit (nobody in a trigger): the player nearest the landmark leads. */
    exit_setup(3000); absbox(&entities[12],-9000,8);
    SV_CoF_PartyTransitionCapture((unsigned int)svs.spawncount+1,landmark,NULL,0);
    assert(strstr(status_log,"exit-leader userid=101 in_exit=0 strays=1"));
    arrive(-2000,1000); assert(links==1 && entities[1].v.origin[0]==1035);
    /* A trigger_multiple that fires a use-only changelevel is an exit too. */
    exit_setup(3000); entities[12].v.classname=22; entities[12].v.target=23; absbox(&entities[12],3000,8);
    entities[13].v.classname=21; entities[13].v.targetname=23; absbox(&entities[13],-9000,8);
    {vec3_t near_leader={0,0,0};
    int exits[1]={1}; entities[13].v.model=24;
    SV_CoF_PartyTransitionCapture((unsigned int)svs.spawncount+1,near_leader,exits,1);}
    assert(strstr(status_log,"exit-leader userid=101 in_exit=1 strays=1"));
    /* A partner walking with the leader keeps its natural arrival spot. */
    exit_setup(64); SV_CoF_PartyTransitionCapture((unsigned int)svs.spawncount+1,landmark,NULL,0);
    assert(strstr(status_log,"strays=0")); arrive(1000,1064);
    assert(links==0 && entities[2].v.origin[0]==1064 && !strstr(status_log,"arrival-"));
    /* ...unless the new map is solid there. */
    exit_setup(64); SV_CoF_PartyTransitionCapture((unsigned int)svs.spawncount+1,landmark,NULL,0);
    solid_probe=1; solid_x=1064; arrive(1000,1064);
    assert(links==1 && entities[2].v.origin[0]==1035);
    /* A record from another transition is ignored. */
    exit_setup(3000); SV_CoF_PartyTransitionCapture((unsigned int)svs.spawncount+1,landmark,NULL,0);
    svs.spawncount++; arrive(1000,4000); assert(links==0);
    /* A dead partner is left to the respawn placement. */
    exit_setup(3000); SV_CoF_PartyTransitionCapture((unsigned int)svs.spawncount+1,landmark,NULL,0);
    entities[2].v.health=0; arrive(1000,4000); assert(links==0);
    /* Feature off: nothing recorded, nothing moved. */
    exit_setup(3000); gate=false; SV_CoF_PartyTransitionCapture((unsigned int)svs.spawncount+1,landmark,NULL,0);
    gate=true; arrive(1000,4000); assert(links==0 && !strstr(status_log,"exit-leader"));
    /* No room beside the leader, but the carried spot is fine: keep it. */
    exit_setup(3000); SV_CoF_PartyTransitionCapture((unsigned int)svs.spawncount+1,landmark,NULL,0);
    narrow_direction=1; arrive(1000,4000);
    assert(links==0 && strstr(status_log,"arrival-kept userid=101 reason=no-room-beside-leader"));
    /* Review: kept is not final; a far partner still regroups once both play. */
    narrow_direction=0; clients[0].state=clients[1].state=cs_spawned; tick(0.1);
    assert(links==1 && entities[2].v.origin[0]==1035 && strstr(status_log,"spawn-placed userid=101 partner=100"));
    /* No room anywhere and the carried spot is solid: share the leader's spot,
       then separate beside the partner once both are in play. */
    exit_setup(3000); SV_CoF_PartyTransitionCapture((unsigned int)svs.spawncount+1,landmark,NULL,0);
    narrow_direction=1; solid_probe=1; solid_x=4000; arrive(1000,4000);
    assert(links==1 && entities[2].v.origin[0]==1000 && strstr(status_log,"arrival-overlapped userid=101"));
    narrow_direction=solid_probe=0; clients[0].state=clients[1].state=cs_spawned; tick(0.1);
    assert(links==2 && entities[2].v.origin[0]==1035 && strstr(status_log,"spawn-placed userid=101 partner=100"));
    /* ...also when the engine reports sign-on before the next frame. */
    exit_setup(3000); SV_CoF_PartyTransitionCapture((unsigned int)svs.spawncount+1,landmark,NULL,0);
    narrow_direction=1; solid_probe=1; solid_x=4000; arrive(1000,4000); narrow_direction=solid_probe=0;
    clients[0].state=clients[1].state=cs_spawned;
    SV_CoF_PartyPlayerReady(&clients[0],false); SV_CoF_PartyPlayerReady(&clients[1],false);
    tick(0.1); assert(links==2 && entities[2].v.origin[0]==1035);}
    /* Review: only this transition's exit counts. The partner stands in an
       unrelated exit (*2); the leader fired a scripted exit (*1) from nowhere. */
    {vec3_t near_leader={0,0,0}; int exits[1]={1};
    exit_setup(3000); entities[12].v.model=24; absbox(&entities[12],-9000,8);
    entities[13].v.classname=21; entities[13].v.model=25; absbox(&entities[13],3000,8);
    SV_CoF_PartyTransitionCapture((unsigned int)svs.spawncount+1,near_leader,exits,1);
    assert(strstr(status_log,"exit-leader userid=100 in_exit=0 strays=1 exits=1"));
    /* Without entity data every exit counts (the looser fallback). */
    exit_setup(3000); entities[12].v.model=24; absbox(&entities[12],-9000,8);
    entities[13].v.classname=21; entities[13].v.model=25; absbox(&entities[13],3000,8);
    SV_CoF_PartyTransitionCapture((unsigned int)svs.spawncount+1,near_leader,NULL,0);
    assert(strstr(status_log,"exit-leader userid=101 in_exit=1"));}
    /* Final review F1: can anything still ask for this exit by touch? */
    {int exits[1]={1};
    exit_setup(3000); entities[12].v.model=24;
    assert(SV_CoF_PartyExitRefires(exits,1)); /* its own touch */
    assert(SV_CoF_PartyExitRefires(NULL,0)); /* without entity data, any trigger_changelevel */
    entities[12].v.spawnflags=2; assert(!SV_CoF_PartyExitRefires(exits,1)); /* use-only, nothing fires it */
    entities[12].v.targetname=23; entities[13].v.classname=22; entities[13].v.target=23;
    assert(SV_CoF_PartyExitRefires(exits,1)); /* a trigger_multiple fires it */
    entities[13].free=1; assert(!SV_CoF_PartyExitRefires(exits,1)); /* a once-only trigger removed itself */
    entities[13].free=0; entities[13].v.flags|=FL_KILLME; assert(!SV_CoF_PartyExitRefires(exits,1));
    entities[13].v.flags=0; entities[12].v.flags|=FL_KILLME; assert(!SV_CoF_PartyExitRefires(exits,1));
    entities[12].v.flags=0; entities[12].v.model=25; assert(!SV_CoF_PartyExitRefires(exits,1));} /* another exit */
    /* Review: the leader itself landed in a wall; gather at the partner instead. */
    {vec3_t landmark={3000,0,0};
    exit_setup(3000); SV_CoF_PartyTransitionCapture((unsigned int)svs.spawncount+1,landmark,NULL,0);
    solid_probe=1; solid_x=1000; arrive(1000,4000);
    assert(links==1 && entities[1].v.origin[0]==4035 && entities[2].v.origin[0]==4000);
    assert(strstr(status_log,"arrival-placed userid=100 beside=101"));
    /* A leader falling down a shaft (no floor, not solid) stays the anchor. */
    exit_setup(3000); SV_CoF_PartyTransitionCapture((unsigned int)svs.spawncount+1,landmark,NULL,0);
    no_floor=1; no_floor_x=1000; arrive(1000,4000);
    assert(entities[1].v.origin[0]==1000 && entities[2].v.origin[0]==1035);}
    /* Re-review: a regrouping arrival that paces around while the leader is
       held by an arrival scene is still brought over afterwards. */
    {vec3_t landmark={3000,0,0};
    exit_setup(3000); SV_CoF_PartyTransitionCapture((unsigned int)svs.spawncount+1,landmark,NULL,0);
    narrow_direction=1; arrive(1000,4000); narrow_direction=0;
    entities[1].v.flags|=FL_FROZEN; clients[0].state=clients[1].state=cs_spawned; tick(0.1);
    entities[2].v.origin[0]=4200; tick(1); assert(links==0 && !strstr(status_log,"reason=player-moved"));
    entities[1].v.flags&=~FL_FROZEN; tick(0.1);
    assert(links==1 && entities[2].v.origin[0]==1035);
    /* ...whereas a fresh join that walked off keeps its own way (unchanged). */
    /* Re-review: never overlap a partner onto a leader stuck in solid. */
    exit_setup(3000); SV_CoF_PartyTransitionCapture((unsigned int)svs.spawncount+1,landmark,NULL,0);
    solid_probe=1; solid_x=1000; no_floor=1; no_floor_x=4000; narrow_direction=1; arrive(1000,4000);
    assert(links==0 && entities[2].v.origin[0]==4000 && strstr(status_log,"arrival-stranded userid=101"));}
    /* Split play: a door taken with the partner across the room pulls nobody. */
    setup(); entities[2].v.origin[0]=300; start_door(); finish_door(); tick(0.1);
    assert(links==0 && !strstr(status_log,"door-party-moved") && !prompts[1][0]);
    /* ...and when the door removed itself, the one left behind is told how to follow. */
    setup(); entities[2].v.origin[0]=300; start_door(); entities[5].free=1; finish_door(); tick(0.1);
    assert(links==0 && !strcmp(prompts[1],"1:Little Rascal 1 went somewhere you can't follow. Press J to join."));
    assert(strstr(status_log,"cofstory phase=cut-off userid=101 owner=100 kind=door") && !prompts[0][0]);
    /* A partner who went through the removed door with the opener is not told. */
    setup(); start_door(); entities[5].free=1; finish_door(); tick(0.1);
    assert(links==1 && !prompts[1][0] && !strstr(status_log,"cut-off"));
    /* A dead partner is not told either: they cannot press J. */
    setup(); entities[2].v.origin[0]=300; entities[2].v.health=0; start_door(); entities[5].free=1; finish_door(); tick(0.1);
    assert(!prompts[1][0]);
    /* A scripted teleport leaves a far partner where they are, and tells them. */
    setup(); entities[10].v.classname=17; entities[10].v.targetname=19; entities[10].v.origin[0]=2000;
    entities[11].v.classname=18; entities[11].v.target=19; entities[2].v.origin[0]=-500; tick(0.1);
    VectorCopy(entities[1].v.origin,before); entities[1].v.origin[0]=2000;
    SV_CoF_PartyNoteOrigin(&entities[1],before); tick(0.1);
    assert(links==0 && entities[2].v.origin[0]==-500 && strstr(status_log,"companions=0"));
    assert(strstr(status_log,"cofstory phase=cut-off userid=101 owner=100 kind=teleport"));
    assert(!strcmp(prompts[1],"1:Little Rascal 1 went somewhere you can't follow. Press J to join."));
    /* ...unless they were watching the activator's cutscene with them: one
     * character in the story, so they go along at once, frozen and watching. */
    setup(); entities[10].v.classname=17; entities[10].v.targetname=19; entities[10].v.origin[0]=2000;
    entities[11].v.classname=18; entities[11].v.target=19; entities[2].v.origin[0]=-500; tick(0.1);
    scene_watching=101; entities[2].v.flags|=FL_FROZEN; clients[1].pViewEntity=&entities[9];
    VectorCopy(entities[1].v.origin,before); entities[1].v.origin[0]=2000;
    SV_CoF_PartyNoteOrigin(&entities[1],before); tick(0.1);
    assert(links==1 && entities[2].v.origin[0]==2035 && strstr(status_log,"companions=1"));
    assert(!prompts[1][0] && !strstr(status_log,"cut-off"));
    /* watching someone else's scene is not enough */
    setup(); entities[10].v.classname=17; entities[10].v.targetname=19; entities[10].v.origin[0]=2000;
    entities[11].v.classname=18; entities[11].v.target=19; entities[2].v.origin[0]=-500; tick(0.1);
    scene_watching=999; VectorCopy(entities[1].v.origin,before); entities[1].v.origin[0]=2000;
    SV_CoF_PartyNoteOrigin(&entities[1],before); tick(0.1);
    assert(links==0 && strstr(status_log,"companions=0"));
    /* The ending door (c_village3): everyone, however far, goes to the opener's
     * room (chosen from the shared ending flags), blacked out and held like the
     * opener, then faded back in beside them: one ending for the party. */
    setup(); entities[2].v.origin[0]=3000; entities[2].v.takedamage=DAMAGE_AIM; tick(0.01);
    entities[5].v.classname=4; entities[5].v.target=26; entities[5].v.owner=&entities[1]; entities[5].serialnumber=15;
    entities[6].v.classname=5; entities[6].v.targetname=27; entities[6].v.origin[0]=512; /* ending_exit4 */
    entities[8].v.classname=5; entities[8].v.targetname=28; entities[8].v.origin[0]=-512; /* ending_exit3 */
    private_data[0][0x4CD]=1; private_data[0][0x4CE]=1; /* roof boss killed, P345 given: room 4 */
    private_data[0][0x1EA4]=1; entities[1].v.flags|=FL_FROZEN; tick(0.1);
    assert(strstr(status_log,"door-begin userid=100 target=endingcheck ending=1"));
    assert(ending_fades_out==1 && (entities[2].v.flags&FL_FROZEN) && entities[2].v.origin[0]==3000);
    /* review minor 5: held and blacked out, they are out of the fight like the opener */
    assert((entities[2].v.flags&FL_NOTARGET) && entities[2].v.takedamage==DAMAGE_NO && SV_CoF_PartyHolding());
    finish_door(); tick(0.1);
    assert(links==1 && entities[2].v.origin[0]==547 && !(entities[2].v.flags&FL_FROZEN) && ending_fades_in==1);
    assert(!(entities[2].v.flags&FL_NOTARGET) && entities[2].v.takedamage==DAMAGE_AIM && !SV_CoF_PartyHolding());
    assert(strstr(status_log,"door-party-moved userid=101 owner=100"));
    /* the delivered package overrides both: room 5; flags that are not 0/1 find no room (as the game) */
    setup(); entities[5].v.classname=4; entities[5].v.target=26; entities[5].v.owner=&entities[1];
    private_data[0][0x4CD]=2; private_data[0][0x1EA4]=1; tick(0.1);
    assert(!strstr(status_log,"door-begin") && !(entities[2].v.flags&FL_FROZEN));
    /* a companion who dies on the way is let go, never left held */
    setup(); entities[2].v.origin[0]=3000; tick(0.01);
    entities[5].v.classname=4; entities[5].v.target=26; entities[5].v.owner=&entities[1]; entities[5].serialnumber=15;
    entities[6].v.classname=5; entities[6].v.targetname=27; entities[6].v.origin[0]=512;
    private_data[0][0x4CD]=1; private_data[0][0x4CE]=1; private_data[0][0x1EA4]=1; tick(0.1);
    assert(entities[2].v.flags&FL_FROZEN);
    entities[2].v.health=0; finish_door(); tick(0.1);
    assert(!(entities[2].v.flags&FL_FROZEN) && ending_fades_in==1 && links==0);
    /* the opener never arrives: the deadline lets everyone go */
    setup(); entities[2].v.origin[0]=3000; tick(0.01);
    entities[5].v.classname=4; entities[5].v.target=26; entities[5].v.owner=&entities[1]; entities[5].serialnumber=15;
    entities[6].v.classname=5; entities[6].v.targetname=27; entities[6].v.origin[0]=512;
    private_data[0][0x4CD]=1; private_data[0][0x4CE]=1; private_data[0][0x1EA4]=1; tick(0.1);
    tick(9); assert(!(entities[2].v.flags&FL_FROZEN) && strstr(status_log,"door-expired"));
    /* a level change while they are held: released before the transition save */
    setup(); entities[2].v.origin[0]=3000; tick(0.01);
    entities[5].v.classname=4; entities[5].v.target=26; entities[5].v.owner=&entities[1]; entities[5].serialnumber=15;
    entities[6].v.classname=5; entities[6].v.targetname=27; entities[6].v.origin[0]=512;
    private_data[0][0x4CD]=1; private_data[0][0x4CE]=1; private_data[0][0x1EA4]=1; tick(0.1);
    assert(entities[2].v.flags&FL_FROZEN);
    {vec3_t landmark={0,0,0}; SV_CoF_PartyTransitionCapture((unsigned int)svs.spawncount+1,landmark,NULL,0);}
    assert(!(entities[2].v.flags&(FL_FROZEN|FL_NOTARGET)) && !SV_CoF_PartyHolding());
    /* review minor 6: no room beside the opener (a third or fourth player): share the opener's spot */
    setup(); entities[2].v.origin[0]=3000; tick(0.01);
    entities[5].v.classname=4; entities[5].v.target=26; entities[5].v.owner=&entities[1]; entities[5].serialnumber=15;
    entities[6].v.classname=5; entities[6].v.targetname=27; entities[6].v.origin[0]=512;
    private_data[0][0x4CD]=1; private_data[0][0x4CE]=1; private_data[0][0x1EA4]=1; tick(0.1);
    blocked=1; finish_door(); tick(0.1);
    assert(entities[2].v.origin[0]==entities[1].v.origin[0] && !(entities[2].v.flags&FL_FROZEN) && strstr(status_log,"door-party-moved userid=101"));
    blocked=0;
    /* A partner already at the destination is neither moved nor told. */
    setup(); entities[10].v.classname=17; entities[10].v.targetname=19; entities[10].v.origin[0]=2000;
    entities[11].v.classname=18; entities[11].v.target=19; entities[2].v.origin[0]=2060; tick(0.1);
    VectorCopy(entities[1].v.origin,before); entities[1].v.origin[0]=2000;
    SV_CoF_PartyNoteOrigin(&entities[1],before); tick(0.1);
    assert(links==0 && !prompts[1][0]);
    /* Warp: J puts the caller beside the partner, facing the same way, with a blink. */
    setup(); tick(0.1); entities[2].v.origin[0]=3000; entities[1].v.angles[1]=90; warp(1);
    assert(links==1 && entities[2].v.origin[0]==35 && entities[2].v.angles[1]==90 && fades==1);
    assert(strstr(status_log,"cofstory phase=warp userid=101 partner=100"));
    /* ...not again inside the 2 s cooldown, and silently */
    entities[2].v.origin[0]=3000; warp(1);
    assert(links==1 && entities[2].v.origin[0]==3000 && !strstr(status_log,"warp-refused"));
    tick(2.1); warp(1); assert(links==2 && entities[2].v.origin[0]==35);
    /* Review Focus 4: never while the caller is dead or in a door sequence... */
    setup(); tick(0.1); entities[2].v.origin[0]=3000; entities[2].v.health=0; warp(1);
    assert(links==0 && strstr(status_log,"warp-refused userid=101 reason=dead"));
    setup(); tick(0.1); entities[2].v.origin[0]=3000; private_data[1][0x1EA4]=1; warp(1);
    assert(links==0 && strstr(status_log,"warp-refused userid=101 reason=door"));
    /* ...nor onto a partner who is in a door sequence or mid level change */
    setup(); tick(0.1); entities[2].v.origin[0]=3000; private_data[0][0x1EA4]=1; warp(1);
    assert(links==0 && strstr(status_log,"warp-refused userid=101 reason=no-partner"));
    assert(!strcmp(prompts[1],"1:Nobody to join right now."));
    setup(); tick(0.1); entities[2].v.origin[0]=3000; clients[0].state=3; warp(1);
    assert(links==0 && entities[2].v.origin[0]==3000 && strstr(status_log,"reason=no-partner"));
    /* No room beside the partner: stay put and say so. */
    setup(); tick(0.1); entities[2].v.origin[0]=3000; blocked=1; warp(1);
    assert(links==0 && entities[2].v.origin[0]==3000 && fades==0);
    assert(!strcmp(prompts[1],"1:No room next to Little Rascal 1.") && strstr(status_log,"reason=no-room"));
    /* Final review F4: a refusal also starts the cooldown, so mashing J logs one line per 2 s */
    warp(1); assert(logged("warp-refused")==1);
    tick(2.1); warp(1); assert(logged("warp-refused")==2);
    setup(); tick(0.1); entities[2].v.health=0; warp(1); warp(1);
    assert(logged("warp-refused userid=101 reason=dead")==1);
    /* During an exit hold the waiter's J belongs to the exit, not the warp. */
    setup(); tick(0.1); entities[2].v.origin[0]=3000; exit_key=1; warp(1);
    assert(links==0 && !strstr(status_log,"phase=warp"));
    /* A death brings the jump-respawn and its prompt forward to the delay; the
     * retail minute stays when the delay is 60 or more, and a sooner time stays. */
    {
        float at;
        setup(); SV_CoF_PartyInit(); respawn_delay.value=10; tick(0.1);
        at=(float)sv.time+60; memcpy(private_data[1]+0x3D4,&at,4); memcpy(private_data[1]+0x3D0,&at,4);
        entities[2].v.health=0; entities[2].v.deadflag=2; tick(0.1);
        memcpy(&at,private_data[1]+0x3D4,4); assert(at>(float)sv.time+9.8f && at<(float)sv.time+10.1f);
        memcpy(&at,private_data[1]+0x3D0,4); assert(at>(float)sv.time+9.8f && at<(float)sv.time+10.1f);
        assert(logged("respawn-sooner userid=101 delay=10")==1);
        tick(0.1); assert(logged("respawn-sooner")==1); /* once per death */
        setup(); SV_CoF_PartyInit(); respawn_delay.value=60; tick(0.1);
        at=(float)sv.time+60; memcpy(private_data[1]+0x3D4,&at,4);
        entities[2].v.health=0; tick(0.1);
        memcpy(&at,private_data[1]+0x3D4,4); assert(at>(float)sv.time+59.5f && !logged("respawn-sooner"));
        setup(); SV_CoF_PartyInit(); respawn_delay.value=10; tick(0.1);
        at=(float)sv.time+3; memcpy(private_data[1]+0x3D4,&at,4);
        entities[2].v.health=0; tick(0.1);
        memcpy(&at,private_data[1]+0x3D4,4); assert(at<(float)sv.time+3.0f && !logged("respawn-sooner"));
        respawn_delay.value=10;
    }
    /* A weapon a story script hands one player (cof_giveitems with a classname:
     * the c_apartment1 glock) is given to the partner too, once, and remembered
     * for the rest of the session; a story-item gift (spawnflag 1) is not. */
    setup(); entities[9].v.classname=29; entities[9].v.message=7; svs.spawncount++; tick(0.1);
    assert(strstr(status_log,"gift-shared class=weapon_glock"));
    inventory(0,0,11); tick(1.2); /* a joiner gets a second to settle first */
    assert(CoF_PartyInventory(&clients[1])&(1U<<2));
    assert(strstr(status_log,"starter-shared userid=101 class=weapon_glock"));
    give_calls=0; tick(0.6); assert(give_calls==0); /* once */
    entities[9].free=1; svs.spawncount++; tick(0.1); /* the next map */
    assert(logged("gift-shared class=weapon_glock")==1 && CoF_PartyInventory(&clients[0])&(1U<<2));
    setup(); entities[9].v.classname=29; entities[9].v.message=7; entities[9].v.spawnflags=1; svs.spawncount++; tick(0.1);
    assert(!strstr(status_log,"gift-shared"));
    /* Review I-4: the guest drops the glock on purpose; neither this map nor the
     * next hands it back while the host still carries one. */
    setup(); entities[9].v.classname=29; entities[9].v.message=7; svs.spawncount++; tick(0.1);
    inventory(0,0,11); tick(1.2); assert(CoF_PartyInventory(&clients[1])&(1U<<2));
    memset(private_data[1]+0x2200,0,6*sizeof(string_t)); give_calls=0;
    tick(1.2); assert(give_calls==0);
    svs.spawncount++; tick(0.1); tick(1.2); tick(1.2);
    assert(give_calls==0 && !(CoF_PartyInventory(&clients[1])&(1U<<2)));
    /* ...but a respawn may give it back (the death took everything) */
    entities[2].v.health=0; entities[2].v.deadflag=2; tick(0.1);
    entities[2].v.health=100; entities[2].v.deadflag=DEAD_NO; tick(1.2); tick(1.2);
    assert(CoF_PartyInventory(&clients[1])&(1U<<2));
    /* The AI companion catching up lands out of the player's sight: behind them, facing their
     * way (a second bot one row further back); a dead companion is not moved. */
    setup(); tick(0.1); entities[1].v.origin[0]=1000; entities[1].v.angles[1]=0; entities[2].v.origin[0]=-3000;
    assert(SV_CoF_PartyPlaceUnseen(&clients[1],&entities[1],NULL,0,false) && entities[2].v.origin[0]==936 && entities[2].v.angles[1]==0);
    entities[2].v.origin[0]=-3000;
    assert(SV_CoF_PartyPlaceUnseen(&clients[1],&entities[1],NULL,1,false) && entities[2].v.origin[0]==896);
    entities[1].v.angles[1]=90; entities[2].v.origin[0]=-3000;
    assert(SV_CoF_PartyPlaceUnseen(&clients[1],&entities[1],NULL,0,false) && entities[2].v.origin[0]==1000 && entities[2].v.origin[1]==-64);
    entities[2].v.health=0; entities[2].v.origin[0]=-3000;
    assert(!SV_CoF_PartyPlaceUnseen(&clients[1],&entities[1],NULL,0,false) && entities[2].v.origin[0]==-3000);
    /* Walls all round but in front (a door exit: live, 54 of 76 exits put the bot dead ahead of
     * the player): no spot in sight, unless it has waited long enough; then never in the
     * middle of the view. */
    setup(); tick(0.1); entities[1].v.origin[0]=1000; entities[2].v.origin[0]=-3000; front_only_x=1030;
    assert(!SV_CoF_PartyPlaceUnseen(&clients[1],&entities[1],NULL,0,false) && entities[2].v.origin[0]==-3000);
    assert(SV_CoF_PartyPlaceUnseen(&clients[1],&entities[1],NULL,0,true) && entities[2].v.origin[0]>=1030);
    {
        float x=entities[2].v.origin[0]-1000, y=entities[2].v.origin[1];
        assert(fabsf(atan2f(y,x))*57.3f>=30.0f && x*x+y*y>=48.0f*48.0f);
    }
    /* Through a door after the leader, who walked on: out of the door (behind them), not on them. */
    setup(); tick(0.1); entities[1].v.origin[0]=2300; entities[2].v.origin[0]=-3000;
    {
        vec3_t exit={2000,0,0};
        assert(SV_CoF_PartyPlaceUnseen(&clients[1],&entities[1],exit,0,false) && entities[2].v.origin[0]==1936);
    }
    /* going through a door is being held by the game */
    setup(); tick(0.1); assert(!SV_CoF_PartyInDoor(&clients[0]));
    private_data[0][COF_PARTY_DOOR_FLAG]=1; assert(SV_CoF_PartyInDoor(&clients[0])); private_data[0][COF_PARTY_DOOR_FLAG]=0;
    /* Frozen in a cutscene, on a ladder or in a puzzle view: J does not tear the
     * body away from that camera; the game releases them itself. */
    setup(); tick(0.1); entities[2].v.origin[0]=3000; entities[2].v.flags|=FL_FROZEN; warp(1);
    assert(links==0 && entities[2].v.origin[0]==3000 && logged("warp-refused userid=101 reason=frozen")==1 && !prompts[1][0]);
    /* With three players the nearest partner is the target, unless someone waits at a held exit. */
    setup(); svs.maxclients=3; clients[2]=clients[1]; clients[2].userid=102; clients[2].edict=&entities[3];
    entities[3]=entities[2]; entities[3].serialnumber=12; entities[3].pvPrivateData=private_data[2];
    entities[2].v.origin[0]=3000; entities[3].v.origin[0]=2900; tick(0.1);
    warp(1); assert(entities[2].v.origin[0]==2935);
    tick(2.1); exit_waiter=100; warp(1); assert(entities[2].v.origin[0]==35);
    /* Every other command goes on to the game; so does this one when the story layer is off. */
    setup(); tick(0.1); command_name="say"; assert(!SV_CoF_PartyClientCommand(&clients[1]));
    gate=false; command_name="cof_warp"; assert(!SV_CoF_PartyClientCommand(&clients[1])); gate=true;
    /* A player still joining presses J: handled, nothing moves. */
    setup(); tick(0.1); clients[1].state=3; warp(1); assert(links==0 && !strstr(status_log,"warp"));
    /* A merged map's section move whose landing (the landmark offset) is deep in a wall with nothing free beside
     * it (a door entered off its usual spot; live 2026-10-16 refused it 140 times a second): the player lands at
     * the destination landmark instead. Nowhere free at all: refused, the player stays where they were. */
    setup(); tick(0.1); solid_probe=1; solid_x=-500; front_only_x=0.001f;
    {
        vec3_t landing={-500,0,0}, landmark={300,0,0};
        assert(SV_CoF_PartySectionMove(&clients[0],landing,NULL,landmark));
        assert(entities[1].v.origin[0]==300);
        entities[1].v.origin[0]=0; blocked=1;
        assert(!SV_CoF_PartySectionMove(&clients[0],landing,NULL,landmark));
        assert(entities[1].v.origin[0]==0);
        /* no landmark to fall back on (a section entered at its start spot): refused as before */
        blocked=0;
        assert(!SV_CoF_PartySectionMove(&clients[0],landing,NULL,NULL));
        assert(entities[1].v.origin[0]==0);
    }
    solid_probe=0; front_only_x=0; blocked=0;
    puts("party scenarios passed"); return 0;
}
