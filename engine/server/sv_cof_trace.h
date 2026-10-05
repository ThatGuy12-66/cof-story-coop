/* Opt-in Cry of Fear story co-op: verbose story log (cvar cof_story_trace).
 * GPL-3.0-or-later. Observes only; nothing here changes what the game sees. */
#ifndef SV_COF_TRACE_H
#define SV_COF_TRACE_H
void SV_CoF_TraceInit( void );
// Before the game DLL's ClientCommand; the command is already tokenized.
void SV_CoF_TraceCommand( sv_client_t *cl );
// Player state changes, once per server frame.
void SV_CoF_TraceFrame( void );
// A watched user message, from the engine's message functions.
void SV_CoF_TraceMsgBegin( const char *name, int dest, edict_t *ent );
void SV_CoF_TraceMsgByte( int value );
void SV_CoF_TraceMsgShort( int value );
void SV_CoF_TraceMsgString( const char *s );
void SV_CoF_TraceMsgOther( void ); // long, angle, coord, entity: keeps the field order honest
void SV_CoF_TraceMsgEnd( void );
#endif
