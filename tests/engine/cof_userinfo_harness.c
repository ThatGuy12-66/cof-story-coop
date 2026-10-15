/* Synthetic records only. No game DLL, process, network or dump data. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif
typedef int qboolean;
typedef unsigned char byte;
#define true 1
#define false 0
#define GAME_EXPORT
#define XASH_DEDICATED 1
#define MAX_VA_STRING 1024
#define MAX_QPATH 64
#define S_ERROR ""
#define Q_strlen(s) ((int)strlen(s))
#define Q_strcmp strcmp
#define Q_strncmp strncmp
#define Q_stricmp strcasecmp
#define Q_strchr strchr
#define Q_strstr strstr
#define Q_tolower tolower
#define Q_snprintf snprintf
#define Q_vsnprintf vsnprintf
#define COM_StringEmpty(s) (!(s)[0])
#define COM_StringEmptyOrNULL(s) (!(s) || !(s)[0])
static void Q_strncpy(char *out, const char *in, size_t n) { if(n) { size_t k=strlen(in); if(k>=n) k=n-1; memcpy(out,in,k);out[k]=0; } }
static size_t Q_strnlen(const char *s,size_t n) {size_t i;for(i=0;i<n&&s[i];i++);return i;}
static void Con_Printf(const char *fmt,...) {(void)fmt;}
typedef struct {int unused;} sv_client_t;
static sv_client_t clients[2];
static struct {int maxclients;sv_client_t *clients;} svs={2,clients};
static int enabled=1,asset_exists=1;
static qboolean SV_CoF_StoryCoopEnabled(void) {return enabled;}
static qboolean FS_FileExists(const char *path,qboolean local) {(void)path;(void)local;return asset_exists;}
#include "userinfo_under_test.c"
#include "model_hook_under_test.c"

#define CHECK(expr) do {if(!(expr)) {printf("FAIL line %d: %s\n",__LINE__,#expr);return 1;}} while(0)
typedef struct {char info[256];byte canary[16];} guarded_t;
static void record(guarded_t *g,int length,const char *model)
{
    char a[101],b[101],c[128];int rest;
    memset(g,0xA5,sizeof(*g));memset(a,'a',100);a[100]=0;memset(b,'b',100);b[100]=0;
    rest=length-209-7-(int)strlen(model);
    if(rest<1||rest>127) abort();
    memset(c,'c',(size_t)rest);c[rest]=0;
    snprintf(g->info,sizeof(g->info),"\\a\\%s\\b\\%s\\c\\%s\\model\\%s",a,b,c,model);
}
static int guarded(const guarded_t *g) {int i;for(i=0;i<16;i++)if(g->canary[i]!=0xA5)return 0;return 1;}

int main(void)
{
    guarded_t g;char original[256];
    /* Reproduce the exact 249 + (13 - 6) = 256 character overflow. */
    record(&g,249,"gordon");CHECK(strlen(g.info)==249);
    Info_SetValueForKey(g.info,"model","cof_simon_alt",sizeof(g.info));
    CHECK(guarded(&g));CHECK(strlen(g.info)<sizeof(g.info));
    /* Nonessential insertion: 255 characters plus NUL fits; 256 does not. */
    record(&g,247,"gordon");Info_SetValueForKey(g.info,"extra","x",sizeof(g.info));
    CHECK(strlen(g.info)==255);CHECK(guarded(&g));CHECK(!strcmp(Info_ValueForKey(g.info,"extra"),"x"));
    record(&g,248,"gordon");Info_SetValueForKey(g.info,"extra","x",sizeof(g.info));
    CHECK(strlen(g.info)==248);CHECK(guarded(&g));CHECK(!Info_ValueForKey(g.info,"extra")[0]);
    /* The model hook must preserve every field, including the old model. */
    record(&g,251,"x");memcpy(original,g.info,sizeof(original));
    SV_CoF_StoryUserinfo(&clients[1],g.info,sizeof(g.info));
    CHECK(!memcmp(g.info,original,sizeof(original)));CHECK(guarded(&g));
    record(&g,250,"x");SV_CoF_StoryUserinfo(&clients[1],g.info,sizeof(g.info));
    CHECK(strlen(g.info)==255);CHECK(!strcmp(Info_ValueForKey(g.info,"model"),"cof_s2"));CHECK(guarded(&g));
    record(&g,249,"gordon");SV_CoF_StoryUserinfo(&clients[1],g.info,sizeof(g.info));
    CHECK(strlen(g.info)==249);CHECK(!strcmp(Info_ValueForKey(g.info,"model"),"cof_s2"));CHECK(guarded(&g));
    strcpy(g.info,"\\model_note\\keep\\model\\gordon\\name\\synthetic");
    SV_CoF_StoryUserinfo(&clients[0],g.info,sizeof(g.info));
    CHECK(!strcmp(g.info,"\\model_note\\keep\\model\\cof_s1\\name\\synthetic"));CHECK(guarded(&g));
    strcpy(g.info,"\\name\\synthetic");SV_CoF_StoryUserinfo(&clients[0],g.info,sizeof(g.info));
    CHECK(!strcmp(g.info,"\\name\\synthetic\\model\\cof_s1"));CHECK(guarded(&g));
    strcpy(g.info,"\\model\\gordon");enabled=0;
    SV_CoF_StoryUserinfo(&clients[0],g.info,sizeof(g.info));CHECK(!strcmp(g.info,"\\model\\gordon"));
    enabled=1;asset_exists=0;
    SV_CoF_StoryUserinfo(&clients[0],g.info,sizeof(g.info));CHECK(!strcmp(g.info,"\\model\\gordon"));
    /* Live 2026-09-28 guest userinfo (248 chars, from `userinfo`): the retail
       client.dll sky/brightness keys left no room, so "password" was silently
       dropped and the host answered "invalid password". It must now evict a
       cosmetic key instead, keeping every engine-important key intact. */
    memset(&g,0xA5,sizeof(g));strcpy(g.info,"\\cl_nopred\\0\\cl_lw\\1\\cl_lc\\1\\hud_classautokill\\1"
        "\\skybrightcolourr\\0.0\\skybrightcolourg\\0.0\\skybrightcolourb\\0.0"
        "\\bottomcolor\\6\\brightvalue\\1\\cl_dlmax\\1400\\cl_updaterate\\60"
        "\\model\\police1\\name\\Little Rascal 2\\rate\\50000"
        "\\skybrightvalue\\0.35\\topcolor\\30");
    CHECK(strlen(g.info)==248);CHECK(guarded(&g));
    CHECK(Info_SetValueForKey(g.info,"password","0123456789abcdef",sizeof(g.info)));
    CHECK(!strcmp(Info_ValueForKey(g.info,"password"),"0123456789abcdef"));
    CHECK(strlen(g.info)<sizeof(g.info));CHECK(guarded(&g));
    CHECK(!strcmp(Info_ValueForKey(g.info,"name"),"Little Rascal 2"));
    CHECK(!strcmp(Info_ValueForKey(g.info,"model"),"police1"));
    CHECK(!strcmp(Info_ValueForKey(g.info,"cl_updaterate"),"60"));
    /* The 12-slot inventory's capability key must survive the same full retail
       userinfo, or a 12-slot host turns the player away. */
    CHECK(Info_SetValueForKey(g.info,"_cofinv","12",sizeof(g.info)));
    CHECK(!strcmp(Info_ValueForKey(g.info,"_cofinv"),"12"));
    CHECK(!strcmp(Info_ValueForKey(g.info,"password"),"0123456789abcdef"));
    CHECK(!strcmp(Info_ValueForKey(g.info,"name"),"Little Rascal 2"));
    CHECK(strlen(g.info)<sizeof(g.info));CHECK(guarded(&g));
    puts("userinfo capacity and atomic replacement passed");return 0;
}
