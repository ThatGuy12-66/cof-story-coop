#include <assert.h>
#include <stdio.h>
#include "../engine/server/sv_story_transition_state.h"

static void signon_order_and_identity(void)
{
	cof_transition_state_t s;
	CoF_TransitionReset(&s);
	CoF_TransitionBegin(&s, 41);
	assert(CoF_TransitionCapture(&s, 0, 101));
	assert(CoF_TransitionCapture(&s, 1, 202));
	assert(!CoF_TransitionCapture(&s, 1, 303));
	assert(CoF_TransitionPending(&s) == 2);
	assert(!CoF_TransitionRestored(&s, 42, 0, 101));
	assert(!CoF_TransitionRestored(&s, 41, 0, 202));
	assert(!CoF_TransitionReady(&s, 41, 0, 101));
	assert(CoF_TransitionRestored(&s, 41, 0, 101));
	assert(CoF_TransitionRestored(&s, 41, 1, 202));
	assert(CoF_TransitionReady(&s, 41, 1, 202));
	assert(CoF_TransitionNeedsSignon(&s, 41, 0, 101));
	assert(CoF_TransitionPending(&s) == 1);
	assert(CoF_TransitionReady(&s, 41, 0, 101));
	assert(CoF_TransitionPending(&s) == 0);
	assert(!CoF_TransitionReady(&s, 41, 0, 101));
}

static void disconnect_and_slot_reuse(void)
{
	cof_transition_state_t s;
	int i;
	CoF_TransitionBegin(&s, 9);
	for(i = 0; i < 4; ++i) assert(CoF_TransitionCapture(&s, i, 50 + i));
	assert(!CoF_TransitionDrop(&s, 1, 999));
	assert(CoF_TransitionDrop(&s, 1, 51));
	assert(!CoF_TransitionRestored(&s, 9, 1, 51));
	assert(!CoF_TransitionRestored(&s, 9, 1, 99));
	assert(!CoF_TransitionCapture(&s, 1, 99));
	for(i = 3; i >= 0; --i) if(i != 1) {
		assert(CoF_TransitionRestored(&s, 9, i, 50 + i));
		assert(CoF_TransitionReady(&s, 9, i, 50 + i));
	}
	assert(CoF_TransitionPending(&s) == 0);
	CoF_TransitionReset(&s);
	assert(!CoF_TransitionNeedsSignon(&s, 9, 0, 50));
	assert(!CoF_TransitionRestored(&s, 9, 0, 50));
	CoF_TransitionBegin(&s, 10);
	assert(CoF_TransitionCapture(&s, 0, 50));
	assert(!CoF_TransitionRestored(&s, 9, 0, 50));
	assert(!CoF_TransitionCapture(&s, -1, 1));
	assert(!CoF_TransitionCapture(&s, COF_TRANSITION_MAX_CLIENTS, 1));
}

static void inventory_owner_and_hub_policy(void)
{
	cof_transition_state_t s;
	CoF_TransitionBegin(&s, 3);
	assert(CoF_TransitionCapture(&s, 0, 11));
	assert(CoF_TransitionCapture(&s, 1, 22));
	assert(CoF_TransitionInventoryOwner(&s, 0, 2) == 2); /* world owner, player attachment */
	assert(CoF_TransitionInventoryOwner(&s, 500, 1) == 1);
	assert(CoF_TransitionInventoryOwner(&s, 2, 0) == 2);
	assert(CoF_TransitionInventoryOwner(&s, 0, 0) == 0);
	assert(CoF_TransitionDrop(&s, 1, 22));
	assert(CoF_TransitionInventoryOwner(&s, 2, 0) == 0);
	assert(CoF_TransitionSkipSavedEntity(1, 0, 1, 0)); /* stale hub player */
	assert(CoF_TransitionSkipSavedEntity(1, 0, 0, 1)); /* stale hub inventory */
	assert(CoF_TransitionSkipSavedEntity(0, 0, 1, 0)); /* other adjacent map */
	assert(!CoF_TransitionSkipSavedEntity(0, 1, 1, 0)); /* current party */
	assert(!CoF_TransitionSkipSavedEntity(1, 0, 0, 0)); /* hub world */
}

int main(void)
{
	signon_order_and_identity();
	disconnect_and_slot_reuse();
	inventory_owner_and_hub_policy();
	puts("story transition state: identity, sign-on order, drop, slot reuse and epoch tests passed");
	return 0;
}
