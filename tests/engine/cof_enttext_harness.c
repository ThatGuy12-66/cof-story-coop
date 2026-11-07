/* 1.5: the pack-wide map text (CoF_EntText_Apply in engine/common/cof_entpatch.c). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "cof_entpatch.c"

static const char *Lookup( void *ctx, const char *value, int len )
{
	(void)ctx;
	if( len == 19 && !memcmp( value, "The door is locked.", 19 )) return "Die T\xFCr ist zu.";
	if( len == 4 && !memcmp( value, "Home", 4 )) return "Zuhause";
	if( len == 9 && !memcmp( value, "poofpride", 9 )) return "XX";
	if( len == 3 && !memcmp( value, "bad", 3 )) return "a \"quote";
	return NULL;
}

static int failures;

static void Check( int ok, const char *what )
{
	printf( "%s %s\n", ok ? "ok" : "FAIL", what );
	if( !ok ) failures++;
}

int main( void )
{
	const char *base =
		"{\n\"classname\" \"worldspawn\"\n\"message\" \"The door is locked.\"\n}\n"
		"{\n\"lockedmsg\" \"The door is locked.\"\n\"classname\" \"inter_door\"\n\"target\" \"The door is locked.\"\n}\n"
		"{\n\"classname\" \"tape_recorder\"\n\"message\" \"Home\"\n}\n"
		"{\n\"classname\" \"game_text\"\n\"message\" \"bad\"\n}\n"
		"{\n\"classname\" \"cof_hint\"\n\"message\" \"sprint\"\n}\n";
	const char *want =
		"{\n\"classname\" \"worldspawn\"\n\"message\" \"The door is locked.\"\n}\n"
		"{\n\"lockedmsg\" \"Die T\xFCr ist zu.\"\n\"classname\" \"inter_door\"\n\"target\" \"The door is locked.\"\n}\n"
		"{\n\"classname\" \"tape_recorder\"\n\"message\" \"Zuhause\"\n}\n"
		"{\n\"classname\" \"game_text\"\n\"message\" \"bad\"\n}\n"
		"{\n\"classname\" \"cof_hint\"\n\"message\" \"sprint\"\n}\n";
	size_t len = 0;
	int n = 0;
	char *r = CoF_EntText_Apply( base, strlen( base ) + 1, Lookup, NULL, &len, &n );

	Check( r != NULL && n == 2, "two values replaced: the door line and the save location" );
	Check( r && len == strlen( want ) && !strcmp( r, want ),
		"only text keys of text classnames; worldspawn, a target and a value with a quote stay" );
	free( r );

	r = CoF_EntText_Apply( "{\n\"classname\" \"info_target\"\n}\n", 30, Lookup, NULL, &len, &n );
	Check( r == NULL && n == 0, "nothing to replace: NULL" );

	r = CoF_EntText_Apply( "{\n\"classname\" \"inter_door\"\n\"message\" \"poofpride\"", 47, Lookup, NULL, &len, &n );
	Check( r == NULL, "a base that does not parse: NULL" );

	Check( CoF_EntText_Translatable( "inter_door", 10, "lockedmsg", 9 ), "inter_door lockedmsg" );
	Check( CoF_EntText_Translatable( "inter_door", 10, "message", 7 ), "inter_door message" );
	Check( !CoF_EntText_Translatable( "inter_door", 10, "locked", 6 ), "not a key that only starts one" );
	Check( !CoF_EntText_Translatable( "inter_door", 10, "target", 6 ), "not target" );
	Check( CoF_EntText_Translatable( "tape_recorder", 13, "custommsg", 9 ), "tape_recorder custommsg" );
	Check( !CoF_EntText_Translatable( "info_target", 11, "message", 7 ), "not other classnames" );
	Check( !CoF_EntText_Translatable( "game_tex", 8, "message", 7 ), "not a classname that only starts one" );

	return failures != 0;
}
