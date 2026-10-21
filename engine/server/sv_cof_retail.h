/* Opt-in Cry of Fear story co-op: the retail game DLL's inventory, commands
 * and user messages. GPL-3.0-or-later. */
#ifndef SV_COF_RETAIL_H
#define SV_COF_RETAIL_H
#include "sv_cof_state_data.h"
#define COF_RETAIL_SLOTS 12 // inventory slots 1..12 the engine reads and carries (index 0 is never used); SV_CoF_InventorySlots() says how many this map uses
#define COF_RETAIL_ITEM 64 // descriptor path buffer

void SV_CoF_RetailInit( void );
// The descriptors in usable slots 1..12 ("" when empty); false without a verified, spawned player.
qboolean SV_CoF_RetailReadSlots( sv_client_t *cl, char slots[COF_RETAIL_SLOTS][COF_RETAIL_ITEM] );
// Empty usable slots (only 1..3 count in the game's three-slot mode), or -1 without a verified player.
int SV_CoF_RetailFreeSlots( sv_client_t *cl );
// The game's three-slot mode (cof_clearitems sf 4): only slots 1..3 usable. False without a verified player.
qboolean SV_CoF_RetailInThreeSlots( sv_client_t *cl );
// Hand a story item over through the game's own pickup (KeyitemUse -> AddInventoryItem).
qboolean SV_CoF_RetailGiveItem( sv_client_t *cl, const char *descriptor );
// Remove one story item through the game's own inventorydeletenamed command.
qboolean SV_CoF_RetailRemoveItem( sv_client_t *cl, const char *descriptor );
// The descriptor's nice_name ("Hall key"); "item" and false when unknown.
qboolean SV_CoF_RetailItemName( const char *descriptor, char *out, size_t size );
// A line in the game's own message strip: colour 0/3 yellow, 1 azure, 2/6 red, 4 green, 5 grey.
qboolean SV_CoF_RetailProFont( sv_client_t *cl, int colour, const char *text, qboolean reliable );
// The game's ScreenFade: seconds go out as x4096 shorts; flags 0 fades in from the colour.
qboolean SV_CoF_RetailScreenFade( sv_client_t *cl, float duration, float hold, int flags, int r, int g, int b, int a );
// The cof_document this player is reading right now (m_hDocument), or NULL.
edict_t *SV_CoF_RetailOpenDocument( sv_client_t *cl );
// Archive a document for a player through the game's own AddDocument: it
// dedupes, saves, tells the player and sends AddNote.
qboolean SV_CoF_RetailAddDocument( sv_client_t *cl, edict_t *doc );
// Give a player a code note as cof_addcodenote does: placeholder slot, then AddNote.
qboolean SV_CoF_RetailCodeNote( sv_client_t *cl, int code, int style );
// Used document slots (of 16), or -1 without a verified, spawned player.
int SV_CoF_RetailDocumentCount( sv_client_t *cl );
qboolean SV_CoF_RetailStoryRead( sv_client_t *cl, cof_story_values_t *values );
qboolean SV_CoF_RetailStoryWrite( sv_client_t *cl, const cof_story_values_t *values );
qboolean SV_CoF_RetailArchiveNote( sv_client_t *cl, int slot, const byte *payload, int size );
int SV_CoF_RetailNoteSlot( sv_client_t *cl, const char *note, int start );
qboolean SV_CoF_RetailThreeSlots( sv_client_t *cl );

// The AI companion (docs/research/2026-10-12-companion-*.md, project repo).
enum { COF_ARMS_NONE, COF_ARMS_MELEE, COF_ARMS_GUN, COF_ARMS_HEAL };
typedef struct
{
	int weapon; // COF_ARMS_*: what the player holds (a gun with no rounds at all counts as none)
	int ready; // the held weapon can act now: not switching, not between shots
	int sights, reloading;
	int struggle; // a monster turned the held gun on its holder: the game wants attack pressed again and again
	int clip, reserve; // the held gun's rounds, loaded and in reserve
	int has_gun, has_melee, has_heal; // carries a gun with rounds, a melee weapon, a syringe
	char gun[32], melee[32], heal[32]; // the best of each to take in hand
} cof_retail_arms_t;
qboolean SV_CoF_RetailArms( sv_client_t *cl, cof_retail_arms_t *arms );
// Take a carried weapon in hand through the game's own SelectItem (refused mid-shot, on a ladder...).
qboolean SV_CoF_RetailEquip( sv_client_t *cl, const char *classname );
// After `inventorydualwield`: switch to the weapon_dualweapon the game just built
// for this player (multiplayer rules never do). True when a switch was started.
qboolean SV_CoF_RetailDualDeploy( sv_client_t *cl );
// The item in hand (its m_iId) and, for weapon_dualweapon, the weapon id in each hand.
qboolean SV_CoF_RetailHeld( sv_client_t *cl, int *id, int *right_hand, int *left_hand );
// A companion bot never runs dry: the game's own infinite-ammo switch, and a
// magazine in reserve for every gun it carries (a syringe for the syringe).
void SV_CoF_RetailSupply( sv_client_t *cl );
// Set the game's infinite-ammo switch alone (no shot, syringe or flare spent): what it was
// before (0 or 1), or -1 without a verified player (nothing written).
int SV_CoF_RetailInfiniteAmmo( sv_client_t *cl, qboolean on );
// A weapon of this kind (main classname; aliases count): 0 not carried, 1 carried, 2 in hand;
// -1 without a verified player or for a weapon the table does not know.
int SV_CoF_RetailCarrying( sv_client_t *cl, const char *classname );
// Riding the c_ending4b wheelchair, which took every weapon and gave its own.
qboolean SV_CoF_RetailInWheelchair( sv_client_t *cl );
// Rowing the c_lake boat: m_pBoat names a live boat (the rider's own body stays frozen where they got in).
qboolean SV_CoF_RetailInBoat( sv_client_t *cl );
// The player's m_iFlashlightFlags (which lights are on), or -1 without a verified player.
int SV_CoF_RetailFlashFlags( sv_client_t *cl );
// A trigger_camera holds the player's view (m_hCurrentCamera, and the view is not the player's own).
qboolean SV_CoF_RetailCameraHolds( sv_client_t *cl );
// Night vision on F as in single player: 1 a switch asked for, 0 refused now (a scene, the wheelchair, the game's
// delay), -1 the player does not own it (or no verified player).
int SV_CoF_RetailNightVision( sv_client_t *cl );
// On a phone call, which refuses every weapon switch.
qboolean SV_CoF_RetailInPhoneCall( sv_client_t *cl );
// 1.1.9: the phone broken (m_iPhoneDisabled: 1 broken, 0 whole, -1 without a verified player) and its body; break a
// player's phone as cof_phonedisable does (false when they carry none).
int SV_CoF_RetailPhoneBroken( sv_client_t *cl, int *body );
qboolean SV_CoF_RetailBreakPhone( sv_client_t *cl, int body );
// 1.1.9: the health cap (m_iHealthCap 0-2, -1 without a verified player); set it as cof_maxhealthchange does.
int SV_CoF_RetailHealthCap( sv_client_t *cl );
qboolean SV_CoF_RetailSetHealthCap( sv_client_t *cl, int cap );
// 1.1.9: the cof_telephone this player opened answers someone else now (user: that one, or NULL when nobody).
qboolean SV_CoF_RetailTelephoneTaken( sv_client_t *cl, edict_t **user );
// 1.1.9: a player still in c_asylumb's bar puzzle after it was solved is let out; true when a view was given back.
qboolean SV_CoF_RetailPuzzleRelease( sv_client_t *cl );
// The story's screen blur (m_iBlurActive) and whether a morphine trip holds it; setting it, the game sends "Blur"
// itself (UpdateClientData). False without a verified player.
qboolean SV_CoF_RetailBlur( sv_client_t *cl, int *blur, int *trip );
qboolean SV_CoF_RetailSetBlur( sv_client_t *cl, int blur );
// A new player object whose HUD the game counts as set up: the multiplayer rules' InitHUD, which announces
// "<name> has joined the game" to everyone, is skipped (for the AI companions, added again at every level change).
qboolean SV_CoF_RetailHudInitialized( edict_t *ent );
// CPR: doing it on someone; having it done; a body it can start on now.
qboolean SV_CoF_RetailReviving( sv_client_t *cl );
float SV_CoF_RetailStamina( sv_client_t *cl );
qboolean SV_CoF_RetailRevived( sv_client_t *cl );
qboolean SV_CoF_RetailRevivable( sv_client_t *cl );
// Start the game's CPR (the player's own Use) on a body; true once it runs.
qboolean SV_CoF_RetailRevive( sv_client_t *reviver, sv_client_t *body );
// A reviver who died mid-CPR leaves the body linked to them, refusing every later
// CPR until it respawns: unlink such bodies. A reviver whose body came alive is
// left in its CPR state: let it get up. A trip or knock-down whose fall flag an
// EnableControl cleared: let it get up too (1.1.8).
void SV_CoF_RetailCprRepair( void );
// A hard landing holds the player down until the getting-up command.
qboolean SV_CoF_RetailFallen( sv_client_t *cl );
// The game plays a whole-body animation of its own on the player now (m_flSequenceOverrideTime not passed: a get-up,
// a dodge, a CPR, a death...). False without a verified player.
qboolean SV_CoF_RetailOwnAnimation( sv_client_t *cl );
// One of the game's client commands, as if the player sent it ("flashlight", "tapgetup").
qboolean SV_CoF_RetailCommand( sv_client_t *cl, const char *command );
// The game's ClientCommand for the command already tokenized, as cl, even in a phone call or on a computer/phone
// screen (where the game drops every command).
void SV_CoF_RetailGameCommand( sv_client_t *cl );
// 0: leave it be (a friend, a scripted actor, a boss before its entrance);
// 1: a monster the companion may attack; 2: one already after a player.
int SV_CoF_RetailHostile( edict_t *monster );
// A monster's m_hEnemy (+0x184), or NULL.
edict_t *SV_CoF_RetailMonsterEnemy( edict_t *monster );
#endif
