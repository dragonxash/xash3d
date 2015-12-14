/*
touchscreen.c - touchscreen support prototype
Copyright (C) 2015 a1batross

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "common.h"
#include "gl_local.h"
#include "input.h"
#include "client.h"
#ifdef XASH_SDL
#include <SDL_hints.h>
#endif

typedef enum
{
	touch_command,		// Just tap a button
	touch_move,		// Like a joystick stick.
	touch_look		// Like a joystick stick.
} touchButtonType;

typedef enum
{
	event_down = 0,
	event_up,
	event_motion
} touchEventType;

typedef enum
{
	state_none = 0,
	state_edit,
	state_edit_move
} touchState;

typedef enum
{
	round_none = 0,
	round_grid,
	round_circle
} touchRound;

typedef enum
{
	game_all = 0,
	game_sp,
	game_mp
} gameMode;

#define TOUCH_FL_HIDE BIT( 0 )
#define TOUCH_FL_NOEDIT BIT( 1 )
#define TOUCH_FL_CLIENT BIT( 2 )
#define TOUCH_FL_MP BIT( 3 )
#define TOUCH_FL_SP BIT( 4 )

typedef struct touchbutton2_s
{
	// Touch button type: tap, stick or slider
	touchButtonType type;
	// Field of button in pixels
	float x1, y1, x2, y2;
	// Button texture
	int texture;
	rgba_t color;
	char texturefile[256];
	char command[256];
	char name[32];
	int finger;
	int flags;
	// Double-linked list
	struct touchbutton2_s *next;
	struct touchbutton2_s *prev;
} touchbutton2_t;

struct touch_s
{
	void *mempool;
	touchbutton2_t *first;
	touchbutton2_t *last;
	touchState state;
	int look_finger;
	int move_finger;
	float move_start_x;
	float move_start_y;
	float forward;
	float side;
	float yaw;
	float pitch;
	// editing
	touchbutton2_t *edit;
	touchbutton2_t *selection;
	int resize_finger;
	qboolean showbuttons;
} touch;

typedef struct touchdefaultbutton_s
{
	char name[32];
	char texturefile[256];
	char command[256];
	float x1, y1, x2, y2;
	rgba_t color;
	touchRound round;
	gameMode mode;
} touchdefaultbutton_t;

touchdefaultbutton_t g_DefaultButtons[256] = {
{"look", "", "_look", 0.5, 0.0, 1.0, 1.0, { 255, 255, 255, 255 }, round_none, 0 },
{"move", "", "_move", 0.0, 0.0, 0.5, 1.0, { 255, 255, 255, 255 }, round_none, 0 },
{"invnext", "touch2/next_weap.tga", "invnext", 0.000000, 0.541083, 0.120000, 0.743989, { 255, 255, 255, 255 }, round_circle, 0 },
{"invprev", "touch2/prev_weap.tga", "invprev", 0.000000, 0.135271, 0.120000, 0.338177, { 255, 255, 255, 255 }, round_circle, 0 },
{"edit", "touch2/settings.tga", "touch_enableedit", 0.420000, 0.000000, 0.500000, 0.135271, { 255, 255, 255, 255 }, round_circle, 0 },
{"use", "touch2/use.tga", "+use", 0.880000, 0.405812, 1.000000, 0.608719, { 255, 255, 255, 255 }, round_circle, 0 },
{"jump", "touch2/jump.tga", "+jump", 0.880000, 0.202906, 1.000000, 0.405812, { 255, 255, 255, 255 }, round_circle, 0 },
{"attack", "touch2/shoot.tga", "+attack", 0.760000, 0.473448, 0.880000, 0.676354, { 255, 255, 255, 255 }, round_circle, 0 },
{"attack2", "touch2/shoot_alt.tga", "+attack2", 0.760000, 0.270542, 0.880000, 0.473448, { 255, 255, 255, 255 }, round_circle, 0 },
{"loadquick", "touch2/load.tga", "loadquick", 0.760000, 0.000000, 0.840000, 0.135271, { 255, 255, 255, 255 }, round_circle, game_sp },
{"savequick", "touch2/save.tga", "savequick", 0.840000, 0.000000, 0.920000, 0.135271, { 255, 255, 255, 255 }, round_circle, game_sp },
{"duck", "touch2/crouch.tga", "+duck", 0.880000, 0.777807, 1.000000, 0.980713, { 255, 255, 255, 255 }, round_circle, 0 },
{"messagemode", "touch2/keyboard.tga", "messagemode", 0.840000, 0.000000, 0.920000, 0.135271, { 255, 255, 255, 255 }, round_circle, game_mp },
{"reload", "touch2/reload.tga", "+reload", 0.000000, 0.338177, 0.120000, 0.541083, { 255, 255, 255, 255 }, round_circle, 0 },
{"flashlight", "touch2/flash_light_filled.tga", "impulse 100", 0.920000, 0.000000, 1.000000, 0.135271, { 255, 255, 255, 255 }, round_circle, 0 },
};
int g_LastDefaultButton = 15;

convar_t *touch_pitch;
convar_t *touch_yaw;
convar_t *touch_forwardzone;
convar_t *touch_sidezone;
convar_t *touch_grid_enable;
convar_t *touch_grid_count;
convar_t *touch_config_file;

// code looks smaller with it
#define B(x) button->x
#define SCR_W (scr_width->value)
#define SCR_H (scr_height->value)
#define TO_SCRN_Y(x) (scr_height->integer * (x))
#define TO_SCRN_X(x) (scr_width->integer * (x))

int pfnDrawCharacter( int x, int y, int number, int r, int g, int b );
static void IN_TouchCheckCoords( float *x1, float *y1, float *x2, float *y2  );
void IN_TouchEditClear();

void IN_TouchWriteConfig( void )
{
	file_t	*f;

	if( !touch.first ) return;

	MsgDev( D_NOTE, "IN_TouchWriteConfig()\n" );
	f = FS_Open( touch_config_file->string, "w", false );
	if( f )
	{
		touchbutton2_t *button;
		FS_Printf( f, "//=======================================================================\n");
		FS_Printf( f, "//\t\t\tCopyright XashXT Group %s ©\n", Q_timestamp( TIME_YEAR_ONLY ));
		FS_Printf( f, "//\t\t\ttouch.cfg - touchscreen config\n" );
		FS_Printf( f, "//=======================================================================\n" );
		FS_Printf( f, "\ntouch_config_file \"%s\"\n", touch_config_file->string );
		FS_Printf( f, "\n// touch cvars\n" );
		FS_Printf( f, "touch_forwardzone \"%f\"\n", touch_forwardzone->value );
		FS_Printf( f, "touch_sidezone \"%f\"\n", touch_sidezone->value );
		FS_Printf( f, "touch_pitch \"%f\"\n", touch_pitch->value );
		FS_Printf( f, "touch_yaw \"%f\"\n", touch_yaw->value );
		FS_Printf( f, "touch_grid_count \"%d\"\n", touch_grid_count->integer );
		FS_Printf( f, "touch_grid_enable \"%d\"\n", touch_grid_enable->integer );
		FS_Printf( f, "\n// touch buttons\n" );
		FS_Printf( f, "touch_removeall\n" );
		for( button = touch.first; button; button = button->next )
		{
			if( button->flags & TOUCH_FL_CLIENT )
				continue; //skip temporary buttons
			FS_Printf( f, "touch_addbutton \"%s\" \"%s\" \"%s\" %f %f %f %f %d %d %d %d %d\n", 
				B(name), B(texturefile), B(command),
				B(x1), B(y1), B(x2), B(y2),
				B(color[0]), B(color[1]), B(color[2]), B(color[3]), B(flags) );
			if( button->flags & TOUCH_FL_HIDE )
				FS_Printf( f, "touch_hide \"%s\"\n", button->name );
		}

		FS_Close( f );
	}
	else MsgDev( D_ERROR, "Couldn't write touch.cfg.\n" );
}

void IN_TouchListButtons_f( void )
{
	touchbutton2_t *button;
	for( button = touch.first; button; button = button->next )
		Msg( "%s %s %s %f %f %f %f %d %d %d %d %d\n", 
			B(name), B(texturefile), B(command),
			B(x1), B(y1), B(x2), B(y2),
			B(color[0]), B(color[1]), B(color[2]), B(color[3]), B(flags) );
}

touchbutton2_t *IN_TouchFindButton( const char *name )
{
	touchbutton2_t *button;
	if( !touch.first )
		return NULL;
	for ( button = touch.first; button; button = button->next )
		if( !Q_strncmp( button->name, name, 32 ) )
			return button;
	return NULL;
}

void IN_TouchRemoveButton( const char *name )
{
	touchbutton2_t *button = IN_TouchFindButton( name );
	if( !button )
		return;
	IN_TouchEditClear();
	if( button->prev )
		button->prev->next = button->next;
	else
		touch.first = button->next;
	if( button->next )
		button->next->prev = button->prev;
	else
		touch.last = button->prev;
	Mem_Free( button );
}

void IN_TouchRemoveButton_f()
{
	IN_TouchRemoveButton( Cmd_Argv( 1 ) );
}

void IN_TouchRemoveAll_f()
{
	IN_TouchEditClear();
	while( touch.first )
	{
		touchbutton2_t *remove = touch.first;
		touch.first = touch.first->next;
		Mem_Free ( remove );
	}
	touch.last = NULL;
}

void IN_TouchSetColor( const char *name, byte r, byte g, byte b, byte a )
{
	touchbutton2_t *button = IN_TouchFindButton( name );
	if( !button )
		return;
	MakeRGBA( button->color, r, g, b, a );
}

void IN_TouchSetTexture( const char *name, const char *texture )
{
	touchbutton2_t *button = IN_TouchFindButton( name );
	if( !button )
		return;
	button->texture = -1; // mark for texture load
	Q_strncpy( button->texturefile, texture, sizeof( button->texturefile ) );
}

void IN_TouchSetCommand( const char *name, const char *command )
{
	touchbutton2_t *button = IN_TouchFindButton( name );
	if( !button )
		return;
	Q_strncpy( button->command, command, sizeof( button->command ) );
}

void IN_TouchHide( const char *name, qboolean hide )
{
	touchbutton2_t *button;
	for( button = touch.first; button; button = button->next )
	{
		if( Q_stricmpext( name, button->name ) )
		{
			if( hide )
				button->flags |= TOUCH_FL_HIDE;
			else
				button->flags &= ~TOUCH_FL_HIDE;
		}
	}
	
}
void IN_TouchHide_f( void )
{
	IN_TouchHide( Cmd_Argv( 1 ), true );
}

void IN_TouchShow_f( void )
{
	IN_TouchHide( Cmd_Argv( 1 ), false );
}

void IN_TouchSetColor_f( void )
{
	if( Cmd_Argc() == 5 )
	{
		IN_TouchSetColor( Cmd_Argv(1), Q_atoi( Cmd_Argv(2) ), Q_atoi( Cmd_Argv(3) ), Q_atoi( Cmd_Argv(4) ), Q_atoi( Cmd_Argv(5) ) );
		return;
	}
	Msg( "Usage: touch_setcolor <name> <r> <g> <b> <a>\n" );
}

void IN_TouchSetTexture_f( void )
{
	if( Cmd_Argc() == 3 )
	{
		IN_TouchSetTexture( Cmd_Argv( 1 ), Cmd_Argv( 2 ) );
		return;
	}
	Msg( "Usage: touch_settexture <name> <file>\n" );
}

void IN_TouchSetCommand_f( void )
{
	if( Cmd_Argc() == 3 )
	{
		IN_TouchSetTexture( Cmd_Argv( 1 ), Cmd_Argv( 2 ) );
		return;
	}
	Msg( "Usage: touch_command <name> <command>\n" );
}

touchbutton2_t *IN_AddButton( const char *name,  const char *texture, const char *command, float x1, float y1, float x2, float y2, byte *color )
{
	touchbutton2_t *button = Mem_Alloc( touch.mempool, sizeof( touchbutton2_t ) );
	button->texture = -1;
	Q_strncpy( button->texturefile, texture, sizeof( button->texturefile ) );
	Q_strncpy( button->name, name, 32 );
	IN_TouchRemoveButton( name ); //replace if exist
	button->x1 = x1;
	button->y1 = y1;
	button->x2 = x2;
	button->y2 = y2;
	MakeRGBA( button->color, color[0], color[1], color[2], color[3] );
	button->command[0] = 0;
	button->flags = 0;
	// check keywords
	if( !Q_strcmp( command, "_look" ) )
		button->type = touch_look;
	if( !Q_strcmp( command, "_move" ) )
		button->type = touch_move;
	Q_strncpy( button->command, command, sizeof( button->command ) );
	button->finger = -1;
	button->next = NULL;
	button->prev = touch.last;
	if( touch.last )
		touch.last->next = button;
	touch.last = button;
	if( !touch.first )
		touch.first = button;
	return button;
}

void IN_TouchAddClientButton( const char *name, const char *texture, const char *command, float x1, float y1, float x2, float y2, byte *color, int round )
{
	touchbutton2_t *button;
	if( round )
		IN_TouchCheckCoords( &x1, &y1, &x2, &y2 );
	if( round == round_circle )
		y2 = y1 + ( x2 - x1 ) * (SCR_W/SCR_H);
	button = IN_AddButton( name, texture, command, x1, y1, x2, y2, color );
	button->flags |= TOUCH_FL_CLIENT;
}

void IN_TouchLoadDefaults_f()
{
	int i;
	for( i = 0; i < g_LastDefaultButton; i++ )
	{
		touchbutton2_t *button;
		float x1 = g_DefaultButtons[i].x1, 
			  y1 = g_DefaultButtons[i].y1,
			  x2 = g_DefaultButtons[i].x2,
			  y2 = g_DefaultButtons[i].y2; 
		
		IN_TouchCheckCoords( &x1, &y1, &x2, &y2 );
		if( g_DefaultButtons[i].round == round_circle )
			y2 = y1 + ( x2 - x1 ) * (SCR_W/SCR_H);
		IN_TouchCheckCoords( &x1, &y1, &x2, &y2 );
		button = IN_AddButton( g_DefaultButtons[i].name, g_DefaultButtons[i].texturefile, g_DefaultButtons[i].command, x1, y1, x2, y2, g_DefaultButtons[i].color );
		if( g_DefaultButtons[i].mode == game_sp )
			button->flags |= TOUCH_FL_SP;
		if( g_DefaultButtons[i].mode == game_mp )
			button->flags |= TOUCH_FL_MP;
	}
}

void IN_TouchAddDefaultButton( const char *name, const char *texturefile, const char *command, float x1, float y1, float x2, float y2, byte *color, int round, int mode )
{
	if( g_LastDefaultButton >= 255 )
		return;
	Q_strncpy( g_DefaultButtons[g_LastDefaultButton].name, name, 32 );
	Q_strncpy( g_DefaultButtons[g_LastDefaultButton].texturefile, texturefile, 256 );
	Q_strncpy( g_DefaultButtons[g_LastDefaultButton].command, command, 256 );
	g_DefaultButtons[g_LastDefaultButton].x1 = x1;
	g_DefaultButtons[g_LastDefaultButton].y1 = y1;
	g_DefaultButtons[g_LastDefaultButton].x2 = x2;
	g_DefaultButtons[g_LastDefaultButton].y2 = y2;
	MakeRGBA( g_DefaultButtons[g_LastDefaultButton].color, color[0], color[1], color[2], color[3] );
	g_DefaultButtons[g_LastDefaultButton].round = round;
	g_DefaultButtons[g_LastDefaultButton].mode = mode;
	g_LastDefaultButton++;
}

void IN_TouchAddButton_f()
{
	rgba_t color;
	int argc = Cmd_Argc( );

	if( argc >= 12 )
	{
		touchbutton2_t *button;
		MakeRGBA( color, Q_atoi( Cmd_Argv(8) ), Q_atoi( Cmd_Argv(9) ), 
			Q_atoi( Cmd_Argv(10) ), Q_atoi( Cmd_Argv(11) ) );
		button = IN_AddButton( Cmd_Argv(1), Cmd_Argv(2), Cmd_Argv(3),
			Q_atof( Cmd_Argv(4) ), Q_atof( Cmd_Argv(5) ), 
			Q_atof( Cmd_Argv(6) ), Q_atof( Cmd_Argv(7) ) ,
			color );
		if( argc == 13 )
			button->flags = Q_atoi( Cmd_Argv(12) );
		return;
	}	
	if( argc == 8 )
	{
		MakeRGBA( color, 255, 255, 255, 255 );
		IN_AddButton( Cmd_Argv(1), Cmd_Argv(2), Cmd_Argv(3),
			Q_atof( Cmd_Argv(4) ), Q_atof( Cmd_Argv(5) ), 
			Q_atof( Cmd_Argv(6) ), Q_atof( Cmd_Argv(7) ),
			color );
		return;
	}
	if( argc == 4 )
	{
		MakeRGBA( color, 255, 255, 255, 255 );
		IN_AddButton( Cmd_Argv(1), Cmd_Argv(2), Cmd_Argv(3), 0.4, 0.4, 0.6, 0.6, color );
		return;
	}
	Msg( "Usage: touch_addbutton <name> <texture> <command> [<x1> <y1> <x2> <y2> [ r g b a] ]\n" );
}

void IN_TouchEnableEdit_f()
{
	if( touch.state == state_none )
		touch.state = state_edit;
	touch.resize_finger = touch.move_finger = touch.look_finger = -1;
}

void IN_TouchDisableEdit_f()
{
	touch.state = state_none;
	if( touch.edit )
		touch.edit->finger = -1;
	touch.resize_finger = touch.move_finger = touch.look_finger = -1;
	IN_TouchWriteConfig();
}

void IN_TouchInit( void )
{
	touch.mempool = Mem_AllocPool( "Touch" );
	touch.first = NULL;
	MsgDev( D_NOTE, "IN_TouchInit()\n");
	touch.move_finger = touch.resize_finger = touch.look_finger = -1;
	touch.state = state_none;
	touch.showbuttons = false;
	Cmd_AddCommand( "touch_addbutton", IN_TouchAddButton_f, "Add native touch button" );
	Cmd_AddCommand( "touch_removebutton", IN_TouchRemoveButton_f, "Remove native touch button" );
	Cmd_AddCommand( "touch_enableedit", IN_TouchEnableEdit_f, "Enable button editing mode" );
	Cmd_AddCommand( "touch_disableedit", IN_TouchDisableEdit_f, "Disable button editing mode" );
	Cmd_AddCommand( "touch_settexture", IN_TouchSetTexture_f, "Change button texture" );
	Cmd_AddCommand( "touch_setcolor", IN_TouchSetColor_f, "Change button color" );
	Cmd_AddCommand( "touch_show", IN_TouchShow_f, "show button" );
	Cmd_AddCommand( "touch_hide", IN_TouchHide_f, "hide button" );
	Cmd_AddCommand( "touch_list", IN_TouchListButtons_f, "list buttons" );
	Cmd_AddCommand( "touch_removeall", IN_TouchRemoveAll_f, "Remove all buttons" );
	Cmd_AddCommand( "touch_loaddefaults", IN_TouchLoadDefaults_f, "Generate config from defaults" );
	touch_forwardzone = Cvar_Get( "touch_forwardzone", "0.1", 0, "forward touch zone" );
	touch_sidezone = Cvar_Get( "touch_sidezone", "0.07", 0, "side touch zone" );
	touch_pitch = Cvar_Get( "touch_pitch", "20", 0, "touch pitch sensitivity" );
	touch_yaw = Cvar_Get( "touch_yaw", "50", 0, "touch yaw sensitivity" );
	touch_grid_count = Cvar_Get( "touch_grid_count", "50", 0, "touch grid count" );
	touch_grid_enable = Cvar_Get( "touch_grid_enable", "1", 0, "enable touch grid" );
	touch_config_file = Cvar_Get( "touch_config_file", "touch.cfg", CVAR_ARCHIVE, "current touch profile file" );
#ifdef XASH_SDL
	SDL_SetHint( SDL_HINT_ANDROID_SEPARATE_MOUSE_AND_TOUCH, "1" );
#endif
	if( FS_FileExists( touch_config_file->string, true ) )
		Cbuf_AddText( va( "exec %s\n", touch_config_file->string ) );
	else IN_TouchLoadDefaults_f( );
}

qboolean IN_TouchIsVisible( touchbutton2_t *button )
{
	return ( !( button->flags & TOUCH_FL_HIDE ) || ( touch.state >= state_edit ) )
	&& ( !(button->flags & TOUCH_FL_SP) || ( CL_GetMaxClients() == 1 ==  1 ) ) 
	&& ( !(button->flags & TOUCH_FL_MP) || ( CL_GetMaxClients() == 1 !=  1 ) );
}

void IN_TouchDrawTexture ( float x1, float y1, float x2, float y2, int texture, byte r, byte g, byte b, byte a )
{
	pglColor4ub( r, g, b, a );
	if( x1 >= x2 )
		return;
	if( y1 >= y2 )
		return;
	pglColor4ub( r, g, b, a );
	R_DrawStretchPic( TO_SCRN_X(x1),
		TO_SCRN_Y(y1),
		TO_SCRN_X(x2 - x1),
		TO_SCRN_Y(y2 - y1),
		0, 0, 1, 1, texture );
}

#define GRID_COUNT_X (touch_grid_count->integer)
#define GRID_COUNT_Y (touch_grid_count->integer * SCR_H / SCR_W)
#define GRID_X (1.0/GRID_COUNT_X)
#define GRID_Y (SCR_W/SCR_H/GRID_COUNT_X)
#define GRID_ROUND_X(x) ((float)round( x * GRID_COUNT_X ) / GRID_COUNT_X)
#define GRID_ROUND_Y(x) ((float)round( x * GRID_COUNT_Y ) / GRID_COUNT_Y)

static void IN_TouchCheckCoords( float *x1, float *y1, float *x2, float *y2  )
{
	/// TODO: grid check here
	if( *x2 - *x1 < GRID_X * 2 )
		*x2 = *x1 + GRID_X * 2;
	if( *y2 - *y1 < GRID_Y * 2)
		*y2 = *y1 + GRID_Y * 2;
	if( *x1 < 0 )
		*x2 -= *x1, *x1 = 0;
	if( *y1 < 0 )
		*y2 -= *y1, *y1 = 0;
	if( *y2 > 1 )
		*y1 -= *y2 - 1, *y2 = 1;
	if( *x2 > 1 )
		*x1 -= *x2 - 1, *x2 = 1;
	if ( touch_grid_enable->value )
	{
		*x1 = GRID_ROUND_X( *x1 );
		*x2 = GRID_ROUND_X( *x2 );
		*y1 = GRID_ROUND_Y( *y1 );
		*y2 = GRID_ROUND_Y( *y2 );
	}
}

float IN_TouchDrawText( float x1, float y1, const char *s, byte *color)
{
	float x = x1 * clgame.scrInfo.iWidth;
	if( !clgame.scrInfo.iWidth || !clgame.scrInfo.iHeight )
		return GRID_X * 2;
	Con_UtfProcessChar( 0 );
	while( *s )
		x += pfnDrawCharacter( x, y1 * clgame.scrInfo.iHeight, *s++, color[0], color[1], color[2] );
	GL_SetRenderMode( kRenderTransTexture );
	return ( x / clgame.scrInfo.iWidth );
}

void IN_TouchDraw( void )
{
	touchbutton2_t *button;
	if( cls.key_dest != key_game )
		return;
	GL_SetRenderMode( kRenderTransTexture );
	if( touch.state >= state_edit && touch_grid_enable->value )
	{
		float x;
		IN_TouchDrawTexture( 0, 0, 1, 1, cls.fillImage, 0, 0, 0, 112 );
		pglColor4ub( 0, 224, 224, 112 );
		for ( x = 0; x < 1 ; x += GRID_X )
			R_DrawStretchPic( TO_SCRN_X(x),
				0,
				1,
				TO_SCRN_Y(1),
				0, 0, 1, 1, cls.fillImage );
		for ( x = 0; x < 1 ; x += GRID_Y )
			R_DrawStretchPic( 0,
				TO_SCRN_Y(x),
				TO_SCRN_X(1),
				1,
				0, 0, 1, 1, cls.fillImage );
	}
	for( button = touch.first; button; button = button->next )
	{
		if( IN_TouchIsVisible( button ) )
		{
			if( button->texturefile[0] == '#' )
			{
				if( clgame.scrInfo.iHeight )
					button->y2 = button->y1 + ( (float)clgame.scrInfo.iCharHeight / (float)clgame.scrInfo.iHeight );
				button->x2 = IN_TouchDrawText( button->x1, button->y1, button->texturefile + 1, button->color );
				
			}
			else if( button->texturefile[0] )
			{
				if( button->texture == -1 )
				{
					button->texture = GL_LoadTexture( button->texturefile, NULL, 0, 0, NULL );
				}
				IN_TouchDrawTexture( B(x1), B(y1), B(x2), B(y2), B(texture), B(color[0]), B(color[1]), B(color[2]), B(color[3]) );
			}
		}
		if( touch.state >= state_edit )
		{
			rgba_t color;
			if( !( button->flags & TOUCH_FL_HIDE ) )
				IN_TouchDrawTexture( B(x1), B(y1), B(x2), B(y2), cls.fillImage, 255, 255, 0, 32 );
			else
				IN_TouchDrawTexture( B(x1), B(y1), B(x2), B(y2), cls.fillImage, 128, 128, 128, 128 );
			MakeRGBA( color, 255, 255, 0, 128 );
			Con_DrawString( TO_SCRN_X( B(x1) ), TO_SCRN_Y( B(y1) ), B(name), color );
		}
	}
	if( touch.state >= state_edit )
	{
		rgba_t color;
		MakeRGBA( color, 255, 255, 255, 255 );
		if( touch.edit )
		{
			float	x1 = touch.edit->x1,
					y1 = touch.edit->y1,
					x2 = touch.edit->x2,
					y2 = touch.edit->y2;
			IN_TouchCheckCoords( &x1, &y1, &x2, &y2 );
			IN_TouchDrawTexture( x1, y1, x2, y2, cls.fillImage, 0, 255, 0, 32 );
		}
		IN_TouchDrawTexture( 0, 0, GRID_X, GRID_Y, cls.fillImage, 255, 255, 255, 64 );
		if( touch.selection )
		{
			button = touch.selection;
			IN_TouchDrawTexture( B(x1), B(y1), B(x2), B(y2), cls.fillImage, 255, 0, 0, 64 );
			if( touch.showbuttons )
			{
				IN_TouchDrawTexture( 0, GRID_Y * 8, GRID_X * 2, GRID_Y * 10, cls.fillImage, 0, 0, 255, 224 );
				if( button->flags & TOUCH_FL_HIDE )
					//Con_DrawString( TO_SCRN_X(GRID_X * 2.5), TO_SCRN_Y(GRID_Y * 8.5), "Show", color );
					IN_TouchDrawText( GRID_X * 2.5, GRID_Y * 8.5, "Show", color );
				else
					IN_TouchDrawText( GRID_X * 2.5, GRID_Y * 8.5, "Hide", color );
			}
			Con_DrawString( 0, TO_SCRN_Y(GRID_Y * 11), "Selection:", color );
			Con_DrawString( Con_DrawString( 0, TO_SCRN_Y(GRID_Y*12), "Name: ", color ),
											   TO_SCRN_Y(GRID_Y*12), B(name), color );
			Con_DrawString( Con_DrawString( 0, TO_SCRN_Y(GRID_Y*13), "Texture: ", color ),
											   TO_SCRN_Y(GRID_Y*13), B(texturefile), color );
			Con_DrawString( Con_DrawString( 0, TO_SCRN_Y(GRID_Y*14), "Command: ", color ),
											   TO_SCRN_Y(GRID_Y*14), B(command), color );
		}
		if( touch.showbuttons )
		{
			// close
			IN_TouchDrawTexture( 0, GRID_Y * 2, GRID_X * 2, GRID_Y * 4, cls.fillImage, 0, 255, 0, 224 );
			//Con_DrawString( TO_SCRN_X( GRID_X * 2.5 ), TO_SCRN_Y( GRID_Y * 2.5 ), "Close", color );
			IN_TouchDrawText( GRID_X * 2.5, GRID_Y * 2.5, "Close", color );
			// reset
			IN_TouchDrawTexture( 0, GRID_Y * 5, GRID_X * 2, GRID_Y * 7, cls.fillImage, 255, 0, 0, 224 );
			//Con_DrawString( TO_SCRN_X( GRID_X * 2.5 ), TO_SCRN_Y( GRID_Y * 5.5 ), "Reset", color );
			IN_TouchDrawText( GRID_X * 2.5, GRID_Y * 5.5, "Reset", color );
		}
	}
	pglColor4ub( 255, 255, 255, 255 );
}

// clear move and selection state
void IN_TouchEditClear()
{
	if( touch.state < state_edit )
		return;
	touch.state = state_edit;
	if( touch.edit )
		touch.edit->finger = -1;
	touch.resize_finger = -1;
	touch.edit = NULL;
	touch.selection = NULL;
}

static void IN_TouchEditMove( touchEventType type, int fingerID, float x, float y, float dx, float dy )
{
	if( touch.edit->finger == fingerID )
	{
		if( type == event_up ) // shutdown button move
		{
			touchbutton2_t *button = touch.edit;
			IN_TouchCheckCoords( &button->x1, &button->y1,
				&button->x2, &button->y2 );
			IN_TouchEditClear();
			if( button->type == touch_command )
				touch.selection = button;
		}
		if( type == event_motion ) // shutdown button move
		{
			touch.edit->y1 += dy;
			touch.edit->y2 += dy;
			touch.edit->x1 += dx;
			touch.edit->x2 += dx;
		}
	}
	else 
	{
		if( type == event_down ) // enable resizing
		{
			if( touch.resize_finger == -1 )
			{
				touch.resize_finger = fingerID;
			}
		}
		if( type == event_up ) // disable resizing
		{
			if( touch.resize_finger == fingerID )
			{
				touch.resize_finger = -1;
			}
		}
		if( type == event_motion ) // perform resizing
		{
			if( touch.resize_finger == fingerID )
			{
				touch.edit->y2 += dy;
				touch.edit->x2 += dx;
			}
		}
	}
}

int IN_TouchEvent( touchEventType type, int fingerID, float x, float y, float dx, float dy )
{
	touchbutton2_t *button;
	
	// simulate menu mouse click
	if( cls.key_dest != key_game )
	{
		UI_MouseMove( TO_SCRN_X(x), TO_SCRN_Y(y) );
		if( type == event_down )
			Key_Event(241, 1);
		if( type == event_up )
			Key_Event(241, 0);
		return 0;
	}

	if( touch.state == state_edit_move )
	{
		IN_TouchEditMove( type, fingerID, x, y, dx, dy );
		return 1;
	}

	// edit buttons are on y1
	if( ( type == event_down ) && ( touch.state == state_edit ) )
	{
		if( (x < GRID_X) && (y < GRID_Y) )
		{
			touch.showbuttons ^= true;
			return 1;
		}
		if( touch.showbuttons && ( x < GRID_X * 2 ) )
		{
			if( ( y > GRID_Y * 2 ) && ( y < GRID_Y * 4 )  ) // close button
			{
				IN_TouchDisableEdit_f();
			}
			if( ( y > GRID_Y * 5 ) && ( y < GRID_Y * 7 ) ) // reset button
			{
				Cbuf_AddText( va("exec %s\n", touch_config_file->string ) );
			}
			if( ( y > GRID_Y * 8 ) && ( y < GRID_Y * 10 ) && touch.selection ) // hide button
				touch.selection->flags ^= TOUCH_FL_HIDE;
			return 1;
		}
		
	}
	for( button = touch.last; button  ; button = button->prev )
	{
		if( type == event_down )
		{
			if( ( x > button->x1 &&
				 x < button->x2 ) &&
				( y < button->y2 &&
				  y > button->y1 ) )
			{
				button->finger = fingerID;
				if( touch.state == state_edit )
				{
					// do not edit NOEDIT buttons
					if( button->flags & TOUCH_FL_NOEDIT )
						continue;
					touch.edit = button;
					touch.selection = NULL;
					// Make button last to bring it up
					if( ( button->next ) && ( button->type == touch_command ) )
					{
						if( button->prev )
							button->prev->next = button->next;
						else 
							touch.first = button->next;
						button->next->prev = button->prev;
						touch.last->next = button;
						button->prev = touch.last;
						button->next = NULL;
						touch.last = button;
					}
					touch.state = state_edit_move;
					return 1;
				}
				if( !IN_TouchIsVisible( button ) )
					continue;
				if( button->type == touch_command )
				{
					char command[256];
					Q_snprintf( command, 256, "%s\n", button->command, 256 );
					Cbuf_AddText( command );
				}
				if( button->type == touch_move )
				{
					if( touch.look_finger == fingerID )
					{
						touch.move_finger = touch.look_finger = -1;
						return 1;
					}
					if( touch.move_finger !=-1 )
						button->finger = -1;
					else
					{
						touch.move_finger = fingerID;
						touch.move_start_x = x;
						touch.move_start_y = y;
					}
				}
				if( button->type == touch_look )
				{
					if( touch.move_finger == fingerID )
					{
						touch.move_finger = touch.look_finger = -1;
						return 1;
					}
					if( touch.look_finger !=-1 )
						button->finger = -1;
					else
						touch.look_finger = fingerID;
				}
			}
		}
		if( type == event_up )
		{
			if( fingerID == button->finger )
			{
				button->finger = -1;
				if( ( button->type == touch_command ) && ( button->command[0] == '+' ) )
				{
					char command[256];
					Q_snprintf( command, 256, "%s\n", button->command, 256 );
					command[0] = '-';
					Cbuf_AddText( command );
				}
				if( button->type == touch_move )
				{
					touch.move_finger = -1;
					touch.forward = touch.side = 0;
				}
				if( button->type == touch_look )
				{
					touch.look_finger = -1;
				}
			}
		}
	}
	if( ( type == event_down ) && ( touch.state == state_edit ) )
		touch.selection = NULL;
	if( type == event_motion )
	{
		if( fingerID == touch.move_finger )
		{
			if( !touch_forwardzone->value )
				Cvar_SetFloat( "touch_forwardzone", 0.5 );
			if( !touch_sidezone->value )
				Cvar_SetFloat( "touch_sidezone", 0.3 );
			touch.forward = (touch.move_start_y - y) / touch_forwardzone->value;
			touch.side = (x - touch.move_start_x) / touch_sidezone->value ;
		}
		if( fingerID == touch.look_finger )
			touch.yaw -=dx * touch_yaw->value, touch.pitch +=dy * touch_pitch->value;
	}
	return 1;
}

void IN_TouchMove( float * forward, float *side, float *yaw, float *pitch )
{
	*forward += touch.forward;
	*side += touch.side;
	*yaw += touch.yaw;
	*pitch += touch.pitch;
	touch.yaw = touch.pitch = 0;
}