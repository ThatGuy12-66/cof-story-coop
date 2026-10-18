// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 haej (Cry of Fear: Enhanced contributors)
// Optional presentation only: never changes client messages or weapon state.
#include "common.h"
#include "client.h"
#include "cof_hud_health.h"

static CVAR_DEFINE_AUTO( cof_hud_style, "0", FCVAR_ARCHIVE, "Cry of Fear HUD: 0 Classic, 1 Remake (health, stamina and supported ammunition)" );

static CVAR_DEFINE_AUTO( cof_hud_ammo_compact, "1", FCVAR_ARCHIVE, "Remake ammo: compact outlined boxes (0 restores previous unboxed layout)" );

enum { HUD_HEALTH = 1, HUD_STAMINA = 2, HUD_WEAPONS = 28 };
typedef struct { int texture; float u1,v1,u2,v2,aspect; const char *path; double retry; } cof_hud_icon_t;
static cof_hud_icon_t hud_icons[] = {
 { 0,0,0,0,0,1,"gfx/shell/hud_remake/heart.png" },
 { 0,0,0,0,0,1,"gfx/shell/hud_remake/bullet.png" },
 { 0,0,0,0,0,1,"gfx/shell/hud_remake/magazine.png" },
 { 0,0,0,0,0,1,"gfx/shell/hud_remake/shell.png" },
 { 0,0,0,0,0,1,"gfx/shell/hud_remake/items.png" },
 { 0,0,0,0,0,1,"gfx/shell/hud_remake/items.png" },
 { 0,0,0,0,0,1,"gfx/shell/hud_remake/items.png" },
 { 0,0,0,0,0,1,"gfx/shell/hud_remake/items.png" },
 { 0,0,0,0,0,1,"gfx/shell/hud_remake/items.png" },
 { 0,0,0,0,0,1,"gfx/shell/hud_remake/items.png" },
 { 0,0,0,0,0,1,"gfx/shell/hud_remake/items.png" },
 { 0,0,0,0,0,1,"gfx/shell/hud_remake/items.png" },
 { 0,0,0,0,0,1,"gfx/shell/hud_remake/items.png" },
 { 0,0,0,0,0,1,"gfx/shell/hud_remake/items.png" },
 { 0,0,0,0,0,1,"gfx/shell/hud_remake/items.png" },
 { 0,0,0,0,0,1,"gfx/shell/hud_remake/items.png" },
 { 0,0,0,0,0,1,"gfx/shell/hud_remake/items.png" },
 { 0,0,0,0,0,1,"gfx/shell/hud_remake/items.png" },
};
// WpnInfo's divisor is sent by the server: these are the retail HUD switch cases,
// not guessed magazine capacities or division of an AmmoX reserve pool.
enum { HUD_NONE, HUD_MAGAZINES, HUD_ROUNDS, HUD_SHELLS, HUD_ITEMS, HUD_UNLIMITED };
static const struct { const char *label, *mode; int kind, icon; } hud_catalog[HUD_WEAPONS] = {
 {"","",HUD_NONE,0}, {"FLASHLIGHT","",HUD_NONE,6}, {"PHONE","",HUD_NONE,7},
 {"GLOCK","SEMI",HUD_MAGAZINES,1}, {"SHOTGUN","",HUD_SHELLS,3},
 {"CAMERA","",HUD_NONE,8}, {"KNIFE","",HUD_NONE,9},
 {"P345","SEMI",HUD_MAGAZINES,1}, {"SYRINGE","",HUD_ITEMS,4},
 {"M16","",HUD_MAGAZINES,1}, {"LANTERN","",HUD_NONE,10},
 {"RIFLE","SINGLE",HUD_ROUNDS,1}, {"NIGHTSTICK","",HUD_NONE,11},
 {"FLARE","",HUD_ITEMS,5}, {"TMP","AUTO",HUD_MAGAZINES,1},
 {"DUAL","",HUD_NONE,0}, {"BRANCH","",HUD_NONE,12},
 {"ACTION","",HUD_NONE,13}, {"HAMMER","",HUD_NONE,14},
 {"VP70","BURST",HUD_MAGAZINES,1}, {"BROWNING","SEMI",HUD_MAGAZINES,1},
 {"REVOLVER","SINGLE",HUD_ROUNDS,1}, {"RADIO","",HUD_NONE,15},
 {"FAMAS","AUTO",HUD_UNLIMITED,1}, {"AXE","",HUD_NONE,16},
 {"BOOK","",HUD_NONE,17}, {"G43","SEMI",HUD_MAGAZINES,1},
 {"MP5","AUTO",HUD_MAGAZINES,1},
};
static struct { int clip, reserve, ammo_type; } hud_weapons[HUD_WEAPONS];
static int hud_ammo[256];
static struct
{
 int weapon, clip, stamina, health_cap, previous_health, hide;
 int dual_id[2], dual_clip[2];
 qboolean collecting;
 int health_frame, stamina_frame;
 byte health_alpha, stamina_alpha;
 double damage_until, retry_icons;
} hud;

// Retail BossBar: visibility byte, empty/full image strings, current/max shorts.
// Exact stock skin plus decoded texture identity; custom art stays with the client.
enum { HUD_BOSS_FIRST = 100 };
static const char *hud_boss_skins[] = { "carcass","craig","david","doctor","hanger","holeboss","mace","pumpa","sawer","simon" };
static struct {
 int skin, health, maximum, frame;
 float x,y,w,h;
 byte alpha;
 double damage_until;
} hud_boss;

static void HudBossMessage( int size, const byte *data )
{
 const byte *empty, *full, *end;
 int skin=-1, hp, cap;
 if(!data || size<7 || data[0]!=1) goto invalid;
 empty=data+1;
 end=memchr(empty,0,size-1);
 if(!end) goto invalid;
 full=end+1;
 end=memchr(full,0,(data+size)-full);
 if(!end || data+size-end!=5) goto invalid;
 for(int i=0;i<ARRAYSIZE(hud_boss_skins);i++)
 {
  char expected[96];
  Q_snprintf(expected,sizeof(expected),"gfx/vgui/boss/%s_health_empty.tga",hud_boss_skins[i]);
  if(Q_stricmp((const char*)empty,expected)) continue;
  Q_snprintf(expected,sizeof(expected),"gfx/vgui/boss/%s_health_full.tga",hud_boss_skins[i]);
  if(!Q_stricmp((const char*)full,expected)) skin=i;
  break;
 }
 hp=(short)(end[1]|end[2]<<8);cap=(short)(end[3]|end[4]<<8);
 if(skin<0 || cap<=0) goto invalid;
 hp=bound(0,hp,cap);
 if(hud_boss.skin!=skin || hud_boss.maximum!=cap) {hud_boss.damage_until=0;hud_boss.frame=-1;}
 else if(hp<hud_boss.health) hud_boss.damage_until=host.realtime+0.4;
 hud_boss.skin=skin;hud_boss.health=hp;hud_boss.maximum=cap;
 return;
invalid:
 hud_boss.skin=-1;hud_boss.frame=-1;hud_boss.damage_until=0;
}

void CL_CoF_HudRemakeReset( void )
{
 memset( &hud, 0, sizeof( hud ));
 memset( &hud_boss, 0, sizeof( hud_boss ));
 hud_boss.skin=hud_boss.frame=-1;
 memset( hud_ammo, 0, sizeof( hud_ammo ));
 for( int i=0; i<HUD_WEAPONS; i++ ) { hud_weapons[i].clip=-1; hud_weapons[i].reserve=0; }
 hud.clip = hud.stamina = hud.previous_health = -1;
 hud.dual_clip[0] = hud.dual_clip[1] = -1;
 hud.health_frame = hud.stamina_frame = -1;
 hud.health_cap = 0; // HealthCap's type: none (cof_hud_health.h)
}

void CL_CoF_HudRemakeInit( void )
{
 Cvar_RegisterVariable( &cof_hud_style );
 Cvar_RegisterVariable( &cof_hud_ammo_compact );
 for( int i=0; i<HUD_WEAPONS; i++ ) hud_weapons[i].ammo_type=-1;
 CL_CoF_HudRemakeReset();
}

// Observe the exact retail protocol without consuming or changing client data.
void CL_CoF_HudRemakeMessage( const char *name, int size, const byte *data )
{
 if( !Q_stricmp(name,"ResetHUD") || !Q_stricmp(name,"InitHUD") )
  CL_CoF_HudRemakeReset();
 else if( !Q_stricmp(name,"BossBar") ) HudBossMessage(size,data);
 else if( !data || size<1 ) return;
 else if( !Q_stricmp(name,"Stamina") && size==3 ) hud.stamina=bound(0,data[0],100);
 else if( !Q_stricmp(name,"HealthCap") && size==1 ) hud.health_cap=data[0];
 else if( !Q_stricmp(name,"HideWeapon") && size==1 ) hud.hide=data[0];
 else if( !Q_stricmp(name,"WeaponList") )
 {
  const byte *end=memchr(data,0,size);
  if( end && data+size-end==9 )
  {
   const byte *fields=end+1;
   int id=(signed char)fields[6];
   if(id>0 && id<HUD_WEAPONS) hud_weapons[id].ammo_type=(signed char)fields[0];
  }
 }
 else if( !Q_stricmp(name,"AmmoX") && size==3 )
 {
  int amount=(short)(data[1] | data[2]<<8);
  hud_ammo[data[0]]=amount<0?-amount:amount;
 }
 else if( !Q_stricmp(name,"WpnInfo") && size==4 )
 {
  int id=data[0], divisor=data[1], amount=(short)(data[2] | data[3]<<8);
  if(id>0 && id<HUD_WEAPONS && amount>=0)
  {
   int kind=hud_catalog[id].kind;
   if(kind==HUD_MAGAZINES && divisor>0) hud_weapons[id].reserve=amount/divisor;
   else if(kind==HUD_ITEMS) hud_weapons[id].reserve=amount;
  }
 }
 else if( !Q_stricmp(name,"CurWeapon") && size==3 )
 {
  int id=(signed char)data[1], clip=(signed char)data[2];
  if(id<1) { hud.weapon=0; hud.clip=-1; }
  else if(id<HUD_WEAPONS)
  {
   if(clip < -1) clip=-clip;
   hud_weapons[id].clip=clip;
   if(data[0]) { hud.weapon=id; hud.clip=clip; }
  }
  else if(data[0]) { hud.weapon=0; hud.clip=-1; } // unknown custom weapon
 }
 else if( !Q_stricmp(name,"DualWield") )
 {
  const byte *end=memchr(data,0,size);
  if(end && data+size-end==6)
  {
   int first=end[2], second=end[3];
   hud.dual_id[0]=first>0 && first<HUD_WEAPONS && first!=15?first:0;
   hud.dual_id[1]=second>0 && second<HUD_WEAPONS && second!=15?second:0;
  }
 }
 else if( !Q_stricmp(name,"DualAmmo") && size==8 )
 {
  memcpy(&hud.dual_clip[0],data,4); memcpy(&hud.dual_clip[1],data+4,4);
 }
}

static qboolean HudEnabled( void )
{
	return cof_hud_style.value == 1 && GI && !Q_stricmp( GI->gamefolder, "cryoffear" ) &&
		cls.state == ca_active && !cl.background && !cl.intermission && cl.local.health > 0 && !CL_CoF_UIGateActive();
}

// One shared 4x4 texture, each icon cropped only within its own uniform cell.
static qboolean HudLoadItems( void )
{
 static const int cells[]={12,8,0,1,2,4,3,5,6,13,7,9,10,11};
 cof_hud_icon_t *first=&hud_icons[4];
 if(first->texture && ref.dllFuncs.GL_FindTexture(first->path)==first->texture) return true;
 if(host.realtime<first->retry) return false;
 rgbdata_t *pic=FS_LoadImage(first->path,NULL,0);
 if(!pic || pic->type!=PF_RGBA_32 || pic->width<4 || pic->height<4)
 { if(pic) FS_FreeImage(pic); first->retry=host.realtime+5; return false; }
 for(int i=0;i<ARRAYSIZE(cells);i++)
 {
  int column=cells[i]%4,row=cells[i]/4;
  int x1=column*pic->width/4,x2=(column+1)*pic->width/4;
  int y1=row*pic->height/4,y2=(row+1)*pic->height/4;
  int l=x2,t=y2,r=-1,b=-1;
  for(int y=y1;y<y2;y++) for(int x=x1;x<x2;x++)
   if(pic->buffer[(y*pic->width+x)*4+3]>=128)
   { l=Q_min(l,x);r=Q_max(r,x);t=Q_min(t,y);b=Q_max(b,y); }
  cof_hud_icon_t *icon=&hud_icons[i+4];
  icon->aspect=r>=l && b>=t?(float)(r-l+1)/(b-t+1):0;
  icon->u1=(float)l/pic->width; icon->v1=(float)t/pic->height;
  icon->u2=(float)(r+1)/pic->width; icon->v2=(float)(b+1)/pic->height;
 }
 int texture=GL_LoadTextureInternal(first->path,pic,TF_IMAGE);
 FS_FreeImage(pic);
 for(int i=4;i<ARRAYSIZE(hud_icons);i++) hud_icons[i].texture=texture;
 if(!texture) first->retry=host.realtime+5;
 return texture!=0;
}

static qboolean HudLoadIcon( int index )
{
 if(index>=4) return HudLoadItems() && hud_icons[index].aspect>0;
 cof_hud_icon_t *icon=&hud_icons[index];
 if(icon->texture && ref.dllFuncs.GL_FindTexture(icon->path)==icon->texture) return true;
 if(host.realtime<icon->retry) return false;
		icon->texture = 0;
		rgbdata_t *pic = FS_LoadImage( icon->path, NULL, 0 );
		if( !pic || pic->type != PF_RGBA_32 || pic->width <= 0 || pic->height <= 0 )
		{
			if( pic ) FS_FreeImage( pic );
			icon->retry = host.realtime + 5;
			return false; // missing art never hides the Classic HUD
		}
		int l = pic->width, t = pic->height, r = -1, b = -1;
		for( int y = 0; y < pic->height; y++ ) for( int x = 0; x < pic->width; x++ )
			if( pic->buffer[( y * pic->width + x ) * 4 + 3] >= 128 )
			{ l = Q_min(l,x); r = Q_max(r,x); t = Q_min(t,y); b = Q_max(b,y); }
		if( r < l || b < t ) { FS_FreeImage(pic); icon->retry = host.realtime + 5; return false; }
		icon->u1 = (float)l / pic->width; icon->v1 = (float)t / pic->height;
		icon->u2 = (float)(r+1) / pic->width; icon->v2 = (float)(b+1) / pic->height;
		icon->aspect = (float)(r-l+1) / (b-t+1);
		// No pixels are reused after upload; TF_IMAGE disables mipmap generation.
		icon->texture = GL_LoadTextureInternal( icon->path, pic, TF_IMAGE );
		FS_FreeImage( pic );
		if( !icon->texture ) { icon->retry = host.realtime + 5; return false; }
 return true;
}

static qboolean HudIcons( void )
{
 for(int i=0;i<4;i++) if(!HudLoadIcon(i)) return false;
 return true;
}

static float HudScale( void )
{
	// Compact reference dimensions at 1080p; retain the player's HUD multiplier.
	return bound( 0.5f, refState.height / 1080.0f * CL_CoF_UIScaleUser(), 4.0f );
}

static void HudIcon( int index, float x, float y, float size, const byte *color )
{
	cof_hud_icon_t *icon = &hud_icons[index];
	float w = size, h = size;
	if( icon->aspect > 1 ) h /= icon->aspect; else w *= icon->aspect;
	ref.dllFuncs.GL_SetRenderMode( kRenderTransTexture );
	ref.dllFuncs.Color4ub(color[0],color[1],color[2],color[3]);
	ref.dllFuncs.R_DrawStretchPic(x+(size-w)*0.5f,y+(size-h)*0.5f,w,h,icon->u1,icon->v1,icon->u2,icon->v2,icon->texture);
}

void CL_CoF_HudRemakeBegin( void )
{
 hud.collecting=HudEnabled() && !(hud.hide&13) && HudIcons() && cls.creditsFont.valid &&
  (hud.weapon!=15 || (hud.dual_id[0]>0 && hud.dual_id[1]>0));
 if(hud.previous_health>=0 && cl.local.health<hud.previous_health) hud.damage_until=host.realtime+0.4;
 hud.previous_health=cl.local.health;
}

// These sprites are exclusively the stock ammunition display. No digit decoding,
// batching, frame ordering, blend changes or geometry heuristics are necessary.
qboolean CL_CoF_HudRemakeSprite( const char *name, const wrect_t *rc, int mode, int texture,
 float x,float y,float w,float h,float s1,float t1,float s2,float t2,const byte *color )
{
 if(!hud.collecting || !name || hud.weapon<=0 || hud.weapon>=HUD_WEAPONS) return false;
 static const char *names[]={
  "cof_glock_mag","cof_glock_mag_icon","cof_shotgun_box","cof_shotgun_box_icon",
  "cof_p345_mag","cof_p345_mag_icon","cof_m16_mag","cof_m16_mag_icon",
  "cof_tmp_mag","cof_tmp_mag_icon","cof_rifle_mag","cof_rifle_mag_icon",
  "cof_browning_mag","cof_vp70_mag","cof_vp70_mag_icon","cof_famas_mag","cof_famas_mag_icon",
  "cof_g43_mag","cof_g43_mag_icon","cof_mp5_mag","cof_mp5_mag_icon",
  "cof_syringe_icon","cof_flare_icon","cof_mag_cross","cof_ammohud",
  "cof_revolver_cylinder","gm_inf"
 };
 for(int i=0;i<ARRAYSIZE(names);i++)
 {
  char path[96]; Q_snprintf(path,sizeof(path),"sprites/%s.spr",names[i]);
  if(!Q_stricmp(name,path)) return true;
 }
 return false;
}

static int HudReserve( int id )
{
 if(hud_catalog[id].kind==HUD_ROUNDS || hud_catalog[id].kind==HUD_SHELLS)
 {
  int type=hud_weapons[id].ammo_type;
  return type>=0 && type<ARRAYSIZE(hud_ammo)?hud_ammo[type]:0;
 }
 return hud_weapons[id].reserve;
}

static void HudWeaponRow( int id, int clip, float y, float scale, cl_font_t *font, qboolean label )
{
 if(id<=0 || id>=HUD_WEAPONS || id==15) return;
 int kind=hud_catalog[id].kind, icon=hud_catalog[id].icon;
 float size=24*scale, gap=6*scale, margin=28*scale;
 byte color[4]={245,245,245,255};
 char loaded[24]="", spare[24]="";
 int lw=0, sw=0;
 if(kind==HUD_ITEMS) Q_snprintf(loaded,sizeof(loaded),"%d",HudReserve(id));
 else if(kind!=HUD_NONE)
 {
  if(clip>=0) Q_snprintf(loaded,sizeof(loaded),"%d",clip);
  else Q_snprintf(loaded,sizeof(loaded),"-"); // unavailable, never a fabricated round count
  if(kind==HUD_UNLIMITED) Q_snprintf(spare,sizeof(spare),"INF");
  else Q_snprintf(spare,sizeof(spare),"%d",HudReserve(id));
 }
 CL_DrawStringLen(font,loaded,&lw,NULL,0); CL_DrawStringLen(font,spare,&sw,NULL,0);
 qboolean hasIcon=HudLoadIcon(icon);
 if(!hasIcon && kind==HUD_NONE) Q_snprintf(loaded,sizeof(loaded),"%s",hud_catalog[id].label);
 if(!hasIcon && kind==HUD_ITEMS) Q_snprintf(loaded,sizeof(loaded),"%s %d",hud_catalog[id].label,HudReserve(id));
 CL_DrawStringLen(font,loaded,&lw,NULL,0);
 float total=(hasIcon?size+gap:0)+lw;
 if(spare[0]) total+=size+gap*3+sw;
 float x=refState.width-margin-total;
 if(hasIcon) { HudIcon(icon,x,y,size,color); x+=size+gap; }
 if(loaded[0]) CL_DrawString(x,y,loaded,color,font,0);
 x+=lw+gap*2;
 if(spare[0])
 {
  int reserveIcon=kind==HUD_MAGAZINES || kind==HUD_UNLIMITED?2:icon;
  HudIcon(reserveIcon,x,y,size,color); x+=size+gap;
  CL_DrawString(x,y,spare,color,font,0);
 }
 char caption[64];
 if(label) Q_snprintf(caption,sizeof(caption),"%s%s%s",hud_catalog[id].label,hud_catalog[id].mode[0]?"  ":"",hud_catalog[id].mode);
 else Q_snprintf(caption,sizeof(caption),"%s",hud_catalog[id].mode);
 if(caption[0])
 {
  int width; CL_DrawStringLen(font,caption,&width,NULL,0);
  CL_DrawString(refState.width-margin-width,y-size-gap,caption,color,font,0);
 }
}

// Scale independent font copies from the original metrics; captions never
// inherit the larger numeric font. cl_font_t widths are byte-sized in engine.
static cl_font_t HudCompactFont( float pixels )
{
 cl_font_t font=cls.creditsFont;
 float ratio=pixels/Q_max(1,font.charHeight);
 font.scale*=ratio;
 font.charHeight=Q_max(1,(int)(font.charHeight*ratio));
 for(int i=0;i<ARRAYSIZE(font.charWidths);i++) font.charWidths[i]=bound(1,(int)(font.charWidths[i]*ratio),255);
 return font;
}

// bottom is the outside edge of this box; return the top for stacking dual rows.
static float HudCompactWeaponRow( int id, int clip, float bottom, float scale, qboolean label )
{
 if(id<=0 || id>=HUD_WEAPONS || id==15) return bottom;
 const int kind=hud_catalog[id].kind, icon=hud_catalog[id].icon;
 const qboolean hasIcon=HudLoadIcon(icon);
 const int reserveIcon=kind==HUD_MAGAZINES || kind==HUD_UNLIMITED?2:icon;
 char loaded[48]="",spare[24]="",caption[64]="";
 if(kind==HUD_ITEMS) Q_snprintf(loaded,sizeof(loaded),"%d",HudReserve(id));
 else if(kind!=HUD_NONE)
 {
  if(clip>=0) Q_snprintf(loaded,sizeof(loaded),"%d",clip);
  else Q_snprintf(loaded,sizeof(loaded),"-");
  if(kind==HUD_UNLIMITED) Q_snprintf(spare,sizeof(spare),"INF");
  else Q_snprintf(spare,sizeof(spare),"%d",HudReserve(id));
 }
 if(!hasIcon && kind==HUD_NONE) Q_snprintf(loaded,sizeof(loaded),"%s",hud_catalog[id].label);
 if(!hasIcon && kind==HUD_ITEMS) Q_snprintf(loaded,sizeof(loaded),"%s %d",hud_catalog[id].label,HudReserve(id));
 if(label) Q_snprintf(caption,sizeof(caption),"%s%s%s",hud_catalog[id].label,hud_catalog[id].mode[0]?"  ":"",hud_catalog[id].mode);
 else Q_snprintf(caption,sizeof(caption),"%s",hud_catalog[id].mode);

 const float margin=Q_min(28*scale,refState.width*0.1f);
 const float available=Q_max(1,refState.width-2*margin);
 cl_font_t numbers,small;
 int lw,sw,cw;
 float size,gap,pad,header,body,width,height;
 // Large reserves/dual labels and extreme HUD scale still fit the viewport.
 for(int pass=0;pass<3;pass++)
 {
  numbers=HudCompactFont(18*scale);small=HudCompactFont(11*scale);
  CL_DrawStringLen(&numbers,loaded,&lw,NULL,0);CL_DrawStringLen(&numbers,spare,&sw,NULL,0);
  CL_DrawStringLen(&small,caption,&cw,NULL,0);
  size=18*scale;gap=4*scale;pad=6*scale;
  header=caption[0]?small.charHeight+4*scale:0;
  body=(hasIcon?size+(loaded[0]?gap:0):0)+lw;
  if(spare[0])body+=gap*4+size+sw;
  width=Q_max(body,(float)cw)+pad*2;
  height=pad*2+header+Q_max(size,(float)numbers.charHeight);
  if(width<=available || pass==2)break;
  scale*=available/width;
 }
 const float left=refState.width-margin-width,top=bottom-height;
 const float line=Q_max(1,scale);
 ref.dllFuncs.FillRGBA(kRenderTransTexture,left,top,width,height,0,0,0,64);
 ref.dllFuncs.FillRGBA(kRenderTransTexture,left,top,width,line,150,150,150,160);
 ref.dllFuncs.FillRGBA(kRenderTransTexture,left,bottom-line,width,line,150,150,150,160);
 ref.dllFuncs.FillRGBA(kRenderTransTexture,left,top,line,height,150,150,150,160);
 ref.dllFuncs.FillRGBA(kRenderTransTexture,left+width-line,top,line,height,150,150,150,160);
 byte color[4]={245,245,245,255},dim[4]={185,185,185,255};
 if(caption[0])CL_DrawString(left+pad,top+pad,caption,dim,&small,0);
 float x=left+pad+(width-pad*2-body)*0.5f,y=top+pad+header;
 if(hasIcon){HudIcon(icon,x,y,size,color);x+=size+gap;}
 if(loaded[0])CL_DrawString(x,y,loaded,color,&numbers,0);
 x+=lw;
 if(spare[0])
 {
  x+=gap;
  ref.dllFuncs.FillRGBA(kRenderTransTexture,x,y,line,size,120,120,120,150);
  x+=gap*2;
  HudIcon(reserveIcon,x,y,size,color);x+=size+gap;
  CL_DrawString(x,y,spare,color,&numbers,0);
 }
 return top;
}

void CL_CoF_HudRemakeAmmo( void )
{
 qboolean draw=hud.collecting;
 hud.collecting=false;
 if(!draw || !HudEnabled() || (hud.hide&13) || !cls.creditsFont.valid || hud.weapon<=0) return;
 if(cof_hud_ammo_compact.value)
 {
  const float scale=Q_min(HudScale(),refState.height/(hud.weapon==15?128.0f:80.0f));
  const float bottom=refState.height-28*scale;
  if(hud.weapon==15)
  {
   float top=HudCompactWeaponRow(hud.dual_id[1],hud.dual_clip[1],bottom,scale,true);
   HudCompactWeaponRow(hud.dual_id[0],hud.dual_clip[0],top-6*scale,scale,true);
  }
  else HudCompactWeaponRow(hud.weapon,hud_weapons[hud.weapon].clip,bottom,scale,false);
  ref.dllFuncs.GL_SetRenderMode(kRenderNormal);ref.dllFuncs.Color4ub(255,255,255,255);
  return;
 }
 float scale=HudScale(), size=24*scale, y=refState.height-28*scale-size;
 cl_font_t font=cls.creditsFont;
 float ratio=24*scale/Q_max(1,font.charHeight);
 font.scale*=ratio; font.charHeight=Q_max(1,(int)(font.charHeight*ratio));
 for(int i=0;i<ARRAYSIZE(font.charWidths);i++) font.charWidths[i]=bound(1,(int)(font.charWidths[i]*ratio),255);
 if(hud.weapon==15)
 {
  HudWeaponRow(hud.dual_id[0],hud.dual_clip[0],y-64*scale,scale,&font,true);
  HudWeaponRow(hud.dual_id[1],hud.dual_clip[1],y,scale,&font,true);
 }
 else HudWeaponRow(hud.weapon,hud_weapons[hud.weapon].clip,y,scale,&font,false);
 ref.dllFuncs.GL_SetRenderMode(kRenderNormal); ref.dllFuncs.Color4ub(255,255,255,255);
}

// Fingerprints are of the client's decoded HUD TGAs, not shipped game artwork.
// Both alpha conventions used by BitmapTGA are recognized, and no dimensions-only match.
int CL_CoF_HudRemakeTexture( const byte *hash )
{
	static const struct { byte hash[16]; int kind; } known[] = {
		{ {0x4e, 0xca, 0x94, 0x00, 0xe2, 0x26, 0x49, 0x35, 0x3d, 0x32, 0xf4, 0x3f, 0xbf, 0xa7, 0xf1, 0xe2}, HUD_BOSS_FIRST+0 }, // carcass empty
		{ {0xc1, 0x33, 0x90, 0x09, 0x95, 0xde, 0xe1, 0xf2, 0x1b, 0xff, 0x34, 0x11, 0xe4, 0x88, 0x9d, 0xfa}, HUD_BOSS_FIRST+0 }, // carcass empty inverted alpha
		{ {0x8a, 0xb0, 0x12, 0xad, 0xd2, 0x8e, 0x75, 0x72, 0x46, 0xbb, 0x19, 0x12, 0x7a, 0x38, 0xdc, 0x55}, HUD_BOSS_FIRST+1 }, // carcass full
		{ {0xa3, 0xdd, 0xf7, 0xd9, 0x6f, 0x7e, 0x29, 0x5a, 0xa3, 0x66, 0xe3, 0x64, 0x47, 0xd6, 0x04, 0x03}, HUD_BOSS_FIRST+1 }, // carcass full inverted alpha
		{ {0x68, 0xed, 0x96, 0x12, 0x40, 0x56, 0x9a, 0x33, 0xd3, 0x92, 0x80, 0x7e, 0xc2, 0x95, 0x9b, 0x2f}, HUD_BOSS_FIRST+2 }, // craig empty
		{ {0xad, 0xaa, 0xda, 0x48, 0xab, 0x58, 0x92, 0x3a, 0xa4, 0x1b, 0xfb, 0x8a, 0x27, 0x12, 0xe0, 0x2d}, HUD_BOSS_FIRST+2 }, // craig empty inverted alpha
		{ {0x8e, 0x72, 0xfa, 0xd0, 0x25, 0x20, 0xa8, 0xa5, 0x0d, 0x00, 0xeb, 0x8a, 0x0a, 0x13, 0xf1, 0x87}, HUD_BOSS_FIRST+3 }, // craig full
		{ {0x84, 0x8b, 0x60, 0xba, 0xbe, 0xe2, 0x07, 0x09, 0x35, 0x92, 0x7e, 0x34, 0x2b, 0x1f, 0xec, 0x48}, HUD_BOSS_FIRST+3 }, // craig full inverted alpha
		{ {0xea, 0x94, 0x52, 0xb3, 0x50, 0xdc, 0x81, 0x01, 0x02, 0xf7, 0xfd, 0xa3, 0xe3, 0xb6, 0xb2, 0x7d}, HUD_BOSS_FIRST+4 }, // david empty
		{ {0x95, 0x8a, 0xfe, 0x4b, 0xa6, 0xfe, 0x60, 0xcb, 0xd3, 0x22, 0x14, 0xf3, 0xb7, 0x4e, 0x02, 0xd3}, HUD_BOSS_FIRST+4 }, // david empty inverted alpha
		{ {0x60, 0xe9, 0x05, 0x3f, 0x9a, 0x9d, 0x3a, 0xf1, 0x11, 0x8d, 0xe2, 0xb9, 0xe9, 0x0f, 0x70, 0x39}, HUD_BOSS_FIRST+5 }, // david full
		{ {0x79, 0x81, 0xe0, 0xcb, 0xc5, 0xa0, 0x61, 0x60, 0xc0, 0x42, 0x27, 0xcc, 0x8a, 0x24, 0x7c, 0xf8}, HUD_BOSS_FIRST+5 }, // david full inverted alpha
		{ {0x03, 0x35, 0x48, 0x5f, 0xaf, 0xad, 0xa0, 0xbf, 0x2a, 0x72, 0xb1, 0xf5, 0xa7, 0xbb, 0xcc, 0x82}, HUD_BOSS_FIRST+6 }, // doctor empty
		{ {0xb4, 0xf4, 0x5f, 0xea, 0xe0, 0xa5, 0xe7, 0xe9, 0xb2, 0x43, 0x84, 0x6b, 0x3d, 0x71, 0x86, 0x07}, HUD_BOSS_FIRST+6 }, // doctor empty inverted alpha
		{ {0x80, 0xf6, 0xc3, 0xe2, 0x5f, 0x19, 0x13, 0x0a, 0x99, 0x17, 0x20, 0x81, 0x5f, 0x25, 0x09, 0x58}, HUD_BOSS_FIRST+7 }, // doctor full
		{ {0xbe, 0x69, 0xf8, 0x61, 0x85, 0xa4, 0xcc, 0xf0, 0xc7, 0xc6, 0x30, 0x01, 0xc6, 0x3a, 0x2b, 0xd0}, HUD_BOSS_FIRST+7 }, // doctor full inverted alpha
		{ {0x6a, 0x32, 0x77, 0x4c, 0x4a, 0x36, 0xe0, 0x41, 0xb2, 0x88, 0x76, 0xf5, 0xb2, 0x5d, 0x2a, 0xb0}, HUD_BOSS_FIRST+8 }, // hanger empty
		{ {0x7a, 0x95, 0x84, 0x00, 0xac, 0xbc, 0xe6, 0x8d, 0x80, 0xb9, 0x54, 0x38, 0x9d, 0xdf, 0xe6, 0x5c}, HUD_BOSS_FIRST+8 }, // hanger empty inverted alpha
		{ {0x24, 0xfb, 0x8e, 0xa6, 0x55, 0x6f, 0x59, 0xec, 0xeb, 0x77, 0x70, 0x06, 0x44, 0xc2, 0x24, 0x88}, HUD_BOSS_FIRST+9 }, // hanger full
		{ {0x3d, 0x9d, 0xeb, 0x66, 0xdf, 0x7c, 0x9e, 0xd0, 0xf9, 0x7e, 0xa8, 0xed, 0xca, 0xb1, 0x57, 0xd7}, HUD_BOSS_FIRST+9 }, // hanger full inverted alpha
		{ {0x9d, 0xe7, 0x94, 0x9e, 0x92, 0x8d, 0x12, 0x8e, 0x49, 0x11, 0xd7, 0x19, 0x85, 0xfe, 0xf1, 0x65}, HUD_BOSS_FIRST+10 }, // holeboss empty
		{ {0x46, 0x82, 0x67, 0x2f, 0x06, 0x99, 0xbc, 0x1a, 0x5d, 0xad, 0xd1, 0x3d, 0x09, 0x59, 0x19, 0xef}, HUD_BOSS_FIRST+10 }, // holeboss empty inverted alpha
		{ {0xa5, 0xfd, 0x25, 0xc0, 0x6a, 0x66, 0x91, 0x38, 0x41, 0x28, 0xd6, 0x24, 0xa6, 0xe8, 0xbb, 0x7b}, HUD_BOSS_FIRST+11 }, // holeboss full
		{ {0x17, 0x81, 0xae, 0x25, 0xdf, 0xee, 0xa7, 0x70, 0x83, 0x0a, 0x42, 0x1b, 0x83, 0xbe, 0x69, 0x04}, HUD_BOSS_FIRST+11 }, // holeboss full inverted alpha
		{ {0xa5, 0xcd, 0xdf, 0xaf, 0x3c, 0xe6, 0xcf, 0xd6, 0x7e, 0xaf, 0x94, 0xf3, 0x61, 0x27, 0xd0, 0x6d}, HUD_BOSS_FIRST+12 }, // mace empty
		{ {0xa9, 0xad, 0xdf, 0xc4, 0x46, 0xbb, 0x13, 0x66, 0x15, 0xa5, 0xa4, 0x8d, 0xf0, 0xa6, 0xc8, 0xba}, HUD_BOSS_FIRST+12 }, // mace empty inverted alpha
		{ {0x42, 0x4a, 0xb8, 0x0a, 0xcd, 0xdf, 0x11, 0x84, 0x68, 0x35, 0xc2, 0x21, 0x6f, 0xd0, 0xbc, 0xed}, HUD_BOSS_FIRST+13 }, // mace full
		{ {0x0f, 0xb0, 0x71, 0x82, 0x2a, 0x7f, 0x43, 0x2c, 0xa7, 0xac, 0xb3, 0xd9, 0x50, 0x38, 0xdc, 0x8d}, HUD_BOSS_FIRST+13 }, // mace full inverted alpha
		{ {0xae, 0x7d, 0x96, 0xd6, 0x95, 0x3d, 0x60, 0x1a, 0x1a, 0xfe, 0xe4, 0x3b, 0xf5, 0x98, 0xcc, 0xd9}, HUD_BOSS_FIRST+14 }, // pumpa empty
		{ {0xa9, 0xb7, 0x32, 0x2f, 0xf3, 0xcd, 0x8d, 0x47, 0xca, 0xf7, 0x7a, 0xaf, 0xfe, 0x7f, 0x02, 0xf8}, HUD_BOSS_FIRST+14 }, // pumpa empty inverted alpha
		{ {0xa9, 0x84, 0x05, 0x0c, 0x77, 0xc4, 0xcf, 0x8a, 0xf1, 0xa9, 0xab, 0x5d, 0x1b, 0x04, 0x1d, 0x47}, HUD_BOSS_FIRST+15 }, // pumpa full
		{ {0x89, 0xdc, 0x37, 0x3c, 0x46, 0xda, 0x34, 0x31, 0x92, 0x37, 0x71, 0x36, 0xba, 0x10, 0x5c, 0x91}, HUD_BOSS_FIRST+15 }, // pumpa full inverted alpha
		{ {0x5b, 0x2f, 0xb0, 0x98, 0x5f, 0x77, 0xb3, 0x3d, 0x9c, 0x49, 0xa6, 0xbe, 0x3e, 0xd4, 0xef, 0x35}, HUD_BOSS_FIRST+16 }, // sawer empty
		{ {0x57, 0x77, 0x28, 0x96, 0x72, 0xe9, 0x20, 0x20, 0x73, 0x1f, 0x9a, 0xc0, 0x51, 0x31, 0xcf, 0x4c}, HUD_BOSS_FIRST+16 }, // sawer empty inverted alpha
		{ {0xc5, 0xf5, 0x73, 0x62, 0x39, 0xaf, 0x61, 0x39, 0x43, 0xe0, 0x4c, 0x63, 0x2c, 0x39, 0x92, 0x3f}, HUD_BOSS_FIRST+17 }, // sawer full
		{ {0x2e, 0x26, 0xef, 0x30, 0x3e, 0xa6, 0x99, 0x6f, 0x8e, 0x4d, 0xcb, 0x2a, 0xa4, 0xdc, 0xa8, 0x8e}, HUD_BOSS_FIRST+17 }, // sawer full inverted alpha
		{ {0xf5, 0x88, 0x6e, 0xad, 0x24, 0x71, 0x88, 0x95, 0x17, 0x37, 0x2e, 0x44, 0x96, 0x7e, 0xe0, 0xf4}, HUD_BOSS_FIRST+18 }, // simon empty
		{ {0x50, 0x3e, 0x7c, 0x91, 0x7f, 0x23, 0x12, 0xfa, 0xeb, 0x1a, 0xf4, 0xfd, 0x0a, 0xf4, 0x49, 0xa8}, HUD_BOSS_FIRST+18 }, // simon empty inverted alpha
		{ {0x6f, 0x90, 0x39, 0x62, 0x28, 0x5b, 0xad, 0xc9, 0x7b, 0x94, 0x53, 0x95, 0x63, 0xe5, 0xb7, 0xb4}, HUD_BOSS_FIRST+19 }, // simon full
		{ {0x97, 0x42, 0x70, 0x0c, 0x8a, 0x39, 0xe7, 0x77, 0xaf, 0x1e, 0xcc, 0x5a, 0x71, 0xa9, 0x69, 0x62}, HUD_BOSS_FIRST+19 }, // simon full inverted alpha
		{ {0x0e, 0x8e, 0x9d, 0x1e, 0x14, 0xaa, 0x8b, 0xac, 0xef, 0xe6, 0x52, 0x0c, 0xfa, 0xeb, 0x16, 0x63}, HUD_HEALTH }, // 640_healthbar.tga
		{ {0xb3, 0x99, 0x0d, 0x5b, 0x33, 0x3a, 0x1f, 0x57, 0x69, 0x7e, 0xfa, 0x23, 0x08, 0x17, 0x6d, 0x8d}, HUD_HEALTH }, // 640_healthbar.tga inverted alpha
		{ {0x4c, 0x08, 0x9c, 0xe9, 0x28, 0x60, 0x35, 0x3f, 0x13, 0xbb, 0x9f, 0x12, 0xe4, 0x96, 0x6d, 0xb6}, HUD_HEALTH }, // 640_healthbar2.tga
		{ {0x09, 0xad, 0xdb, 0xd9, 0xc1, 0xfe, 0xaa, 0x6f, 0x2c, 0xc7, 0xdd, 0x59, 0x20, 0x3e, 0xce, 0x84}, HUD_HEALTH }, // 640_healthbar2.tga inverted alpha
		{ {0x6d, 0xf2, 0xb6, 0xf2, 0x73, 0x42, 0xa8, 0xf5, 0x9e, 0x33, 0xc0, 0x7d, 0x42, 0x64, 0x70, 0xd9}, HUD_HEALTH }, // 640_healthbar3.tga
		{ {0x13, 0x6d, 0xb6, 0x35, 0x68, 0xd1, 0x9e, 0x3b, 0x7f, 0x27, 0xa7, 0x1f, 0x1b, 0x2a, 0x65, 0x79}, HUD_HEALTH }, // 640_healthbar3.tga inverted alpha
		{ {0xf5, 0xda, 0xf6, 0x9a, 0xae, 0x69, 0x13, 0xbf, 0xd1, 0x65, 0xdc, 0xa7, 0x0a, 0x60, 0xe0, 0xbc}, HUD_HEALTH }, // 640_healthbar_bar.tga
		{ {0xcc, 0xc8, 0x90, 0x59, 0xd8, 0x45, 0x78, 0x02, 0xc6, 0x3f, 0xb1, 0x2a, 0x86, 0xae, 0xa8, 0x86}, HUD_HEALTH }, // 640_healthbar_bar.tga inverted alpha
		{ {0x70, 0x22, 0x52, 0x93, 0x0b, 0xef, 0x9d, 0x65, 0x68, 0xbf, 0x24, 0xf5, 0xe6, 0x0e, 0x5b, 0x27}, HUD_HEALTH }, // 640_healthbar_bar2.tga
		{ {0x28, 0x64, 0xe1, 0x1a, 0x2a, 0xd8, 0x6a, 0xe4, 0xd2, 0x5e, 0x40, 0x87, 0xe6, 0x45, 0x50, 0xd0}, HUD_HEALTH }, // 640_healthbar_bar2.tga inverted alpha
		{ {0x05, 0xd7, 0xba, 0x88, 0x15, 0x39, 0xe9, 0xc7, 0x65, 0x6f, 0x2e, 0x24, 0x9f, 0xc2, 0x60, 0xe3}, HUD_HEALTH }, // 640_healthbar_bar3.tga
		{ {0xc3, 0xe3, 0x0c, 0xe7, 0x9d, 0x14, 0xec, 0xcc, 0xcc, 0x49, 0xa8, 0xe9, 0x0e, 0xe5, 0xcf, 0x3b}, HUD_HEALTH }, // 640_healthbar_bar3.tga inverted alpha
		{ {0x7c, 0x8c, 0x9a, 0x5c, 0x42, 0xe9, 0x8d, 0xd9, 0xa0, 0x50, 0x5a, 0xc2, 0xe5, 0x6e, 0x3f, 0x11}, HUD_HEALTH }, // 640_healthbar_barempty.tga
		{ {0x18, 0xd2, 0x77, 0xe8, 0x9b, 0x24, 0x1e, 0xa7, 0x47, 0x10, 0x88, 0x0a, 0x91, 0x2a, 0x64, 0xff}, HUD_HEALTH }, // 640_healthbar_barempty.tga inverted alpha
		{ {0x41, 0x12, 0x8c, 0x59, 0x97, 0xe6, 0xfe, 0x5b, 0x49, 0x7e, 0x57, 0xde, 0x86, 0x63, 0xcd, 0x57}, HUD_HEALTH }, // 640_healthbar_barempty2.tga
		{ {0x4c, 0x8f, 0x1d, 0x90, 0x85, 0xfc, 0x54, 0x7d, 0xf8, 0xb2, 0xfd, 0xe7, 0x01, 0x81, 0xf7, 0x07}, HUD_HEALTH }, // 640_healthbar_barempty2.tga inverted alpha
		{ {0xd5, 0x3b, 0xcd, 0x18, 0x3b, 0x6c, 0x28, 0x62, 0x16, 0xa4, 0x0d, 0xe8, 0x8b, 0xa8, 0x73, 0xf5}, HUD_HEALTH }, // 640_healthbar_barempty3.tga
		{ {0xb4, 0x44, 0x96, 0xaf, 0xf1, 0xd6, 0xbe, 0x67, 0x3d, 0x63, 0xe2, 0x8b, 0xc3, 0xaa, 0x00, 0x6d}, HUD_HEALTH }, // 640_healthbar_barempty3.tga inverted alpha
		{ {0x7e, 0xf8, 0x29, 0xf6, 0x56, 0x92, 0x42, 0x1d, 0x60, 0xcc, 0x45, 0xfd, 0x5f, 0xd0, 0xcf, 0x49}, HUD_STAMINA }, // 640_staminabar.tga
		{ {0x71, 0x7b, 0xb1, 0x06, 0xfd, 0x4a, 0x15, 0xa5, 0x9b, 0x15, 0x5f, 0xd2, 0xe0, 0x36, 0x12, 0xd8}, HUD_STAMINA }, // 640_staminabar.tga inverted alpha
		{ {0x32, 0xc9, 0x84, 0x44, 0xd7, 0xc2, 0x96, 0x92, 0x25, 0x7c, 0x84, 0xb8, 0xf2, 0x86, 0x05, 0x12}, HUD_STAMINA }, // 640_staminabar_bar.tga
		{ {0x8c, 0x37, 0x16, 0x56, 0x53, 0x8c, 0x09, 0x4b, 0xbc, 0x11, 0x41, 0x01, 0xe5, 0x3a, 0x5c, 0x7c}, HUD_STAMINA }, // 640_staminabar_bar.tga inverted alpha
		{ {0x6e, 0x5f, 0x26, 0x37, 0xa4, 0xd0, 0x01, 0x37, 0xa7, 0xa9, 0x9b, 0xd6, 0xd7, 0x05, 0x07, 0x8b}, HUD_STAMINA }, // 640_staminabar_barempty.tga
		{ {0x76, 0x85, 0xa7, 0x23, 0xee, 0xee, 0x0a, 0xa4, 0x71, 0x75, 0x71, 0xd4, 0x8d, 0x98, 0x88, 0x7a}, HUD_STAMINA }, // 640_staminabar_barempty.tga inverted alpha
	};
	for(int i=0;i<ARRAYSIZE(known);i++) if(!memcmp(hash,known[i].hash,16)) return known[i].kind;
	return 0;
}

qboolean CL_CoF_HudRemakeQuad( int kind, int alpha )
{
	if(!kind || !HudEnabled() || !HudIcons()) return false;
	if(kind==HUD_HEALTH) { hud.health_frame=host.framecount; hud.health_alpha=alpha; return true; }
	if(kind==HUD_STAMINA && hud.stamina>=0) { hud.stamina_frame=host.framecount; hud.stamina_alpha=alpha; return true; }
	return false;
}

// Retail stamina image's child Panel paints its red recovery-delay background
// immediately after the parent image, at identical bounds (client 10036FC5).
// Match that measured pair, not arbitrary red UI rectangles or screen regions.
static struct {
    qboolean pending;
    int frame, flash_frame;
    float x, y, w, h;
    byte flash_alpha;
} stamina_overlay;

void CL_CoF_HudRemakeVguiBegin( void )
{
    memset( &stamina_overlay, 0, sizeof( stamina_overlay ));
    stamina_overlay.frame = stamina_overlay.flash_frame = -1;
    hud_boss.frame = -1;
}

qboolean CL_CoF_HudRemakeVguiQuad( int kind, qboolean textured,
    float x, float y, float w, float h, const byte *color )
{
    const qboolean paired = stamina_overlay.pending && stamina_overlay.frame == host.framecount;
    stamina_overlay.pending = false; // any intervening quad breaks the pair
    if( !textured && paired && HudEnabled() && HudIcons() && hud.stamina >= 0 &&
        color[0] == 255 && color[1] == 0 && color[2] == 0 &&
        fabsf( x - stamina_overlay.x ) < 0.01f && fabsf( y - stamina_overlay.y ) < 0.01f &&
        fabsf( w - stamina_overlay.w ) < 0.01f && fabsf( h - stamina_overlay.h ) < 0.01f )
    {
        stamina_overlay.flash_frame = host.framecount;
        stamina_overlay.flash_alpha = color[3]; // already converted from VGUI transparency
        return true;
    }
    if(textured && hud_boss.skin>=0 && HudEnabled() && HudIcons() &&
        (kind==HUD_BOSS_FIRST+hud_boss.skin*2 || kind==HUD_BOSS_FIRST+hud_boss.skin*2+1))
    {
        // The empty panel retains full bounds; the final full panel is clipped.
        if(kind==HUD_BOSS_FIRST+hud_boss.skin*2 && w>0 && h>0)
        {
            hud_boss.frame=host.framecount;hud_boss.x=x;hud_boss.y=y;
            hud_boss.w=w;hud_boss.h=h;hud_boss.alpha=color[3];
        }
        return true;
    }
    if( !textured || !CL_CoF_HudRemakeQuad( kind, color[3] )) return false;
    if( kind == HUD_STAMINA && w > 0 && h > 0 )
    {
        stamina_overlay.pending = true;
        stamina_overlay.frame = host.framecount;
        stamina_overlay.x = x; stamina_overlay.y = y;
        stamina_overlay.w = w; stamina_overlay.h = h;
    }
    return true;
}

void CL_CoF_HudRemakeBars( void )
{
	if(!HudEnabled() || !HudIcons()) return;
	float scale=HudScale(), margin=28*scale, bottom=refState.height-margin;
    if(hud_boss.skin>=0 && hud_boss.frame==host.framecount)
    {
        const float fraction=(float)hud_boss.health/hud_boss.maximum;
        const float edge=Q_min(scale,hud_boss.w*0.01f), height=4*edge;
        const float x=hud_boss.x, y=hud_boss.y+hud_boss.h*0.5f-height*0.5f;
        byte color[4]={245,245,245,hud_boss.alpha};
        if(fraction<=0.25f) {color[0]=255;color[1]=48;color[2]=48;}
        else if(host.realtime<hud_boss.damage_until) {color[0]=255;color[1]=160;color[2]=160;}
        // Keep the client's position/width and visibility; only presentation changes.
        ref.dllFuncs.FillRGBA(kRenderTransTexture,x,y,hud_boss.w,height,96,96,96,color[3]);
        ref.dllFuncs.FillRGBA(kRenderTransTexture,x+edge,y+edge,hud_boss.w-2*edge,height-2*edge,24,24,24,color[3]);
        ref.dllFuncs.FillRGBA(kRenderTransTexture,x+edge,y+edge,(hud_boss.w-2*edge)*fraction,height-2*edge,color[0],color[1],color[2],color[3]);
    }
	if(hud.health_frame==host.framecount)
	{
		byte color[4]={245,245,245,hud.health_alpha};
		// out of 100, as the game's bar; what a HealthCap takes away is drawn darker
		float fraction=CoF_HudHealthFill(cl.local.health,hud.health_cap);
		float maximum=CoF_HudHealthMaximum(hud.health_cap)/100.0f;
		if(fraction<=0.25f) { color[0]=255;color[1]=48;color[2]=48; }
		else if(host.realtime<hud.damage_until) { color[0]=255;color[1]=160;color[2]=160; }
		HudIcon(0,margin,bottom-28*scale,28*scale,color);
		float x=margin+38*scale,y=bottom-15*scale,w=128*scale,h=3*scale;
		ref.dllFuncs.FillRGBA(kRenderTransTexture,x,y,w*maximum,h,32,32,32,color[3]/2);
		if(maximum<1.0f) ref.dllFuncs.FillRGBA(kRenderTransTexture,x+w*maximum,y,w*(1.0f-maximum),h,8,8,8,color[3]/4);
		ref.dllFuncs.FillRGBA(kRenderTransTexture,x,y,w*fraction,h,color[0],color[1],color[2],color[3]);
	}
	if(hud.stamina_frame==host.framecount && (hud.stamina<100 ||
		(stamina_overlay.flash_frame==host.framecount && stamina_overlay.flash_alpha>0)))
	{
		float w=280*scale,x=(refState.width-w)*0.5f,y=bottom-4*scale,h=4*scale;
		ref.dllFuncs.FillRGBA(kRenderTransTexture,x,y,w,h,32,32,32,hud.stamina_alpha/2);
		ref.dllFuncs.FillRGBA(kRenderTransTexture,x,y,w*hud.stamina/100.0f,h,245,245,245,hud.stamina_alpha);
		if(stamina_overlay.flash_frame==host.framecount && stamina_overlay.flash_alpha>0)
			ref.dllFuncs.FillRGBA(kRenderTransTexture,x,y,w,h,255,0,0,stamina_overlay.flash_alpha);
	}
	ref.dllFuncs.GL_SetRenderMode(kRenderNormal); ref.dllFuncs.Color4ub(255,255,255,255);
}
