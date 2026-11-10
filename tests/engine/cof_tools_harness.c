/* sv_cof_tools.h: the player tools' rules (host settings, cooldowns, unstuck, the view's forward, ammo boxes, sprays). */
#include <stdio.h>
#include "tools_under_test.h"

static void cpr( float v ) { printf( "cpr %g: %g\n", v, CoF_ToolsClampCprTime( v )); }
static void respawn( float v ) { printf( "respawn %g: %g\n", v, CoF_ToolsClampRespawnDelay( v )); }
static void outlines( int v ) { printf( "outlines %d: %d\n", v, CoF_ToolsClampOutlines( v )); }

static void ready( double now, double next ) { printf( "ready now=%g next=%g: %d\n", now, next, CoF_ToolsReady( now, next )); }

static void refused( int down, int scene, int ladder, int camera )
{
	printf( "unstuck refused down=%d scene=%d ladder=%d camera=%d: %d\n", down, scene, ladder, camera,
		CoF_ToolsUnstuckRefused( down, scene, ladder, camera ));
}

static void plan( int stuck, int partner )
{
	printf( "unstuck plan stuck=%d partner=%d: %d\n", stuck, partner, CoF_ToolsUnstuckPlan( stuck, partner ));
}

static double tidy( float v ) { return v > -0.0005f && v < 0.0005f ? 0.0 : (double)v; } // no "-0.000"

static void forward( float pitch, float yaw )
{
	float out[3];
	CoF_ToolsForward( pitch, yaw, out );
	printf( "forward pitch=%g yaw=%g: %.3f %.3f %.3f\n", pitch, yaw, tidy( out[0] ), tidy( out[1] ), tidy( out[2] ));
}

static void ammo( int id )
{
	const char *entity = CoF_ToolsAmmoEntity( id );
	printf( "ammo %d: %s box=%d refused=%d\n", id, entity ? entity : "-", CoF_ToolsAmmoBox( id ), CoF_ToolsDropRefused( id ));
}

static void spray( const char *label, const char *path ) { printf( "spray %s: %d\n", label, CoF_ToolsSprayAllowed( path )); }

int main( void )
{
	cpr( 5.0f ); cpr( 1.0f ); cpr( 20.0f ); cpr( 2.5f ); // in range: kept
	cpr( 0.0f ); cpr( 21.0f ); cpr( -3.0f );           // out of range: the default
	respawn( 10.0f ); respawn( 5.0f ); respawn( 90.0f );
	respawn( 4.0f ); respawn( 91.0f ); respawn( 384.0f ); respawn( 0.0f ); // retail's 384 is not this project's delay
	outlines( 0 ); outlines( 1 ); outlines( 2 ); outlines( -1 );
	printf( "defaults cpr=%g respawn=%g outlines=%d\n", COF_TOOLS_CPR_DEFAULT, COF_TOOLS_RESPAWN_DEFAULT, COF_TOOLS_OUTLINES_DEFAULT );

	printf( "cooldowns unstuck=%g ping=%g scare=%g drop=%g\n", COF_TOOLS_COOLDOWN_UNSTUCK, COF_TOOLS_COOLDOWN_PING,
		COF_TOOLS_COOLDOWN_SCARE, COF_TOOLS_COOLDOWN_DROP );
	ready( 5.0, 4.0 ); ready( 4.0, 4.0 ); ready( 3.9, 4.0 ); ready( 0.0, 0.0 ); // never used: ready

	refused( 0, 0, 0, 0 ); refused( 1, 0, 0, 0 ); refused( 0, 1, 0, 0 ); refused( 0, 0, 1, 0 ); refused( 0, 0, 0, 1 );
	plan( 1, 0 ); plan( 1, 1 ); plan( 0, 1 ); plan( 0, 0 );
	printf( "plans free=%d search=%d beside=%d\n", COF_TOOLS_UNSTUCK_FREE, COF_TOOLS_UNSTUCK_SEARCH, COF_TOOLS_UNSTUCK_BESIDE );

	forward( 0.0f, 0.0f ); forward( 0.0f, 90.0f ); forward( 0.0f, 180.0f ); forward( 0.0f, 270.0f );
	forward( 90.0f, 0.0f ); forward( -90.0f, 0.0f ); // looking down, up
	forward( 45.0f, 0.0f ); forward( 30.0f, 60.0f );

	printf( "offsets ammo=0x%X objective=0x%X/0x%X blackwhite=0x%X/%d glowshell=%d\n", COF_TOOLS_AMMO_OFFSET,
		COF_TOOLS_OBJECTIVE_OFFSET, COF_TOOLS_OBJECTIVE_RESEND, COF_TOOLS_BLACKWHITE_OFFSET, COF_TOOLS_BLACKWHITE_SIZE,
		COF_TOOLS_RENDERFX_GLOWSHELL );
	printf( "slot 0: %d, 2: %d, 31: %d, 32: %d, -1: %d\n", CoF_ToolsAmmoSlotOffset( 0 ), CoF_ToolsAmmoSlotOffset( 2 ),
		CoF_ToolsAmmoSlotOffset( 31 ), CoF_ToolsAmmoSlotOffset( 32 ), CoF_ToolsAmmoSlotOffset( -1 ));

	ammo( 3 ); ammo( 7 ); ammo( 21 ); ammo( 4 ); ammo( 11 ); ammo( 9 ); ammo( 19 ); ammo( 14 ); // the guns with a box
	ammo( COF_TOOLS_WEAPON_DUAL ); ammo( COF_TOOLS_WEAPON_MP5 ); // refused
	ammo( 23 ); ammo( 20 ); ammo( 13 ); ammo( 8 ); ammo( 6 ); ammo( 1 ); ammo( 0 ); ammo( -1 ); // no box

	spray( "a.png", "a.png" ); spray( "A.PNG", "A.PNG" ); spray( "logos/spray.bmp", "logos/spray.bmp" );
	spray( "me.Bmp", "C:\\sprays\\me.Bmp" );
	spray( "x.jpg", "x.jpg" ); spray( "x.tga", "x.tga" ); spray( "png", "png" ); spray( ".png", ".png" );
	spray( "dir/.png", "dir/.png" ); spray( "dir.png/file", "dir.png/file" ); spray( "x.png.exe", "x.png.exe" );
	spray( "x.pngx", "x.pngx" ); spray( "empty", "" ); spray( "null", NULL );
	return 0;
}
