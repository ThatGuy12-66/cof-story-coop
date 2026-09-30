/* Session-local restore tickets. Deliberately independent of engine types so
 * identity and lifetime rules can be exercised by a native regression test.
 * A slot is an address, never an identity: both userid and epoch must match. */
#ifndef SV_STORY_TRANSITION_STATE_H
#define SV_STORY_TRANSITION_STATE_H

#include <string.h>
#define COF_TRANSITION_MAX_CLIENTS 32
enum { COF_T_EMPTY, COF_T_CAPTURED, COF_T_RESTORED, COF_T_READY, COF_T_DROPPED };
typedef struct { int userid, phase; } cof_transition_ticket_t;
typedef struct {
	unsigned int epoch;
	int active;
	cof_transition_ticket_t players[COF_TRANSITION_MAX_CLIENTS];
} cof_transition_state_t;

static void CoF_TransitionReset(cof_transition_state_t *s)
{
	memset(s, 0, sizeof(*s));
}
static void CoF_TransitionBegin(cof_transition_state_t *s, unsigned int epoch)
{
	CoF_TransitionReset(s);
	s->epoch = epoch;
	s->active = 1;
}
static int CoF_TransitionCapture(cof_transition_state_t *s, int slot, int userid)
{
	if(!s->active || slot < 0 || slot >= COF_TRANSITION_MAX_CLIENTS || userid <= 0 || s->players[slot].phase != COF_T_EMPTY) return 0;
	s->players[slot].userid = userid;
	s->players[slot].phase = COF_T_CAPTURED;
	return 1;
}
static int CoF_TransitionMatches(const cof_transition_state_t *s, unsigned int epoch, int slot, int userid)
{
	return s->active && s->epoch == epoch && slot >= 0 && slot < COF_TRANSITION_MAX_CLIENTS && s->players[slot].userid == userid && s->players[slot].phase >= COF_T_CAPTURED && s->players[slot].phase <= COF_T_READY;
}
static int CoF_TransitionRestored(cof_transition_state_t *s, unsigned int epoch, int slot, int userid)
{
	if(!CoF_TransitionMatches(s, epoch, slot, userid) || s->players[slot].phase != COF_T_CAPTURED) return 0;
	s->players[slot].phase = COF_T_RESTORED;
	return 1;
}
static int CoF_TransitionNeedsSignon(const cof_transition_state_t *s, unsigned int epoch, int slot, int userid)
{
	return CoF_TransitionMatches(s, epoch, slot, userid) && s->players[slot].phase == COF_T_RESTORED;
}
static int CoF_TransitionReady(cof_transition_state_t *s, unsigned int epoch, int slot, int userid)
{
	if(!CoF_TransitionNeedsSignon(s, epoch, slot, userid)) return 0;
	s->players[slot].phase = COF_T_READY;
	return 1;
}
static int CoF_TransitionDrop(cof_transition_state_t *s, int slot, int userid)
{
	if(!CoF_TransitionMatches(s, s->epoch, slot, userid)) return 0;
	s->players[slot].phase = COF_T_DROPPED;
	return 1;
}
static int CoF_TransitionPending(const cof_transition_state_t *s)
{
	int i, pending = 0;
	if(!s->active) return 0;
	for(i = 0; i < COF_TRANSITION_MAX_CLIENTS; ++i)
		if(s->players[i].phase == COF_T_CAPTURED || s->players[i].phase == COF_T_RESTORED) ++pending;
	return pending;
}
static int CoF_TransitionInventoryOwner(const cof_transition_state_t *s, int owner, int attachment)
{
	int candidates[2], i;
	candidates[0] = owner;
	candidates[1] = attachment;
	for(i = 0; i < 2; ++i) {
		int slot = candidates[i] - 1;
		if(s->active && slot >= 0 && slot < COF_TRANSITION_MAX_CLIENTS && s->players[slot].phase >= COF_T_CAPTURED && s->players[slot].phase <= COF_T_READY) return slot + 1;
	}
	return 0;
}
static int CoF_TransitionSkipSavedEntity(int world, int incoming, int player, int inventory)
{
	return (world || !incoming) && (player || inventory);
}
#endif
