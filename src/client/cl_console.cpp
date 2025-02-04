/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.
Copyright (C) 2000-2013 Darklegion Development
Copyright (C) 2015-2019 GrangerHub

This file is part of Tremulous.

Tremulous is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 3 of the License,
or (at your option) any later version.

Tremulous is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Tremulous; if not, see <https://www.gnu.org/licenses/>

===========================================================================
*/
// console.c

#include "client.h"

#include "qcommon/cdefs.h"

int g_console_field_width = 78;


#define	NUM_CON_TIMES 4

#define		CON_TEXTSIZE	163840
typedef struct {
	bool		initialized;

	char		text[CON_TEXTSIZE];
	vec4_t	text_color[CON_TEXTSIZE];
	int			current;		// line where next message will be printed
	int			x;				// offset in current line for next print
	int			display;		// bottom of console displays this line

	int 		linewidth;		// characters across screen
	int			totallines;		// total lines in console scrollback

	float		xadjust;		// for wide aspect screens

	float		displayFrac;	// aproaches finalFrac at scr_conspeed
	float		finalFrac;		// 0.0 to 1.0 lines of console to display

	int			vislines;		// in scanlines

	vec4_t	color;
} console_t;

console_t	con;

cvar_t		*con_conspeed;
cvar_t      *con_height;
cvar_t      *con_useShader;
cvar_t      *con_colorRed;
cvar_t      *con_colorGreen;
cvar_t      *con_colorBlue;
cvar_t      *con_colorAlpha;
cvar_t      *con_versionStr;

#define	DEFAULT_CONSOLE_WIDTH	78


/*
================
Con_ToggleConsole_f
================
*/
void Con_ToggleConsole_f (void) {
	// Can't toggle the console when it's the only thing available
	if ( clc.state == CA_DISCONNECTED && Key_GetCatcher( ) == KEYCATCH_CONSOLE ) {
		return;
	}

	Field_Clear( &g_consoleField );
	g_consoleField.widthInChars = g_console_field_width;

	Key_SetCatcher( Key_GetCatcher( ) ^ KEYCATCH_CONSOLE );
}

/*
===================
Con_ToggleMenu_f
===================
*/
void Con_ToggleMenu_f( void ) {
	CL_KeyEvent( K_ESCAPE, true, Sys_Milliseconds() );
	CL_KeyEvent( K_ESCAPE, false, Sys_Milliseconds() );
}

/*
===================
Con_MessageMode_f
===================
-*/
void Con_MessageMode_f (void) {
	chat_playerNum = -1;
	chat_team = false;
	chat_admins = false;
	chat_clans = false;
	Field_Clear( &chatField );
	chatField.widthInChars = 30;

	Key_SetCatcher( Key_GetCatcher( ) ^ KEYCATCH_MESSAGE );
}

/*
====================
Con_MessageMode2_f
====================
*/
void Con_MessageMode2_f (void) {
	chat_playerNum = -1;
	chat_team = true;
	chat_admins = false;
	chat_clans = false;
	Field_Clear( &chatField );
	chatField.widthInChars = 25;
	Key_SetCatcher( Key_GetCatcher( ) ^ KEYCATCH_MESSAGE );
}

/*
===================
Con_MessageMode3_f
===================
*/
void Con_MessageMode3_f (void) {
	chat_playerNum = VM_Call( cls.cgame, CG_CROSSHAIR_PLAYER );
	if ( chat_playerNum < 0 || chat_playerNum >= MAX_CLIENTS ) {
		chat_playerNum = -1;
		return;
	}
	chat_team = false;
	chat_admins = false;
	chat_clans = false;
	Field_Clear( &chatField );
	chatField.widthInChars = 30;
	Key_SetCatcher( Key_GetCatcher( ) ^ KEYCATCH_MESSAGE );
}

/*
=====================
Con_MessageMode4_f
=====================
*/
void Con_MessageMode4_f (void) {
	chat_playerNum = VM_Call( cls.cgame, CG_LAST_ATTACKER );
	if ( chat_playerNum < 0 || chat_playerNum >= MAX_CLIENTS ) {
		chat_playerNum = -1;
		return;
	}
	chat_team = false;
	chat_admins = false;
	chat_clans = false;
	Field_Clear( &chatField );
	chatField.widthInChars = 30;
	Key_SetCatcher( Key_GetCatcher( ) ^ KEYCATCH_MESSAGE );
}

/*
================
Con_MessageMode5_f
================
*/
void Con_MessageMode5_f (void) {
	int i;
	chat_playerNum = -1;
	chat_team = false;
	chat_admins = true;
	chat_clans = false;
	Field_Clear( &chatField );
	chatField.widthInChars = 25;
	Key_SetCatcher( Key_GetCatcher( ) ^ KEYCATCH_MESSAGE );
}

/*
================
Con_MessageMode6_f
================
*/
void Con_MessageMode6_f (void) {
	int i;
	chat_playerNum = -1;
	chat_team = false;
	chat_admins = false;
	chat_clans = true;
	Field_Clear( &chatField );
	chatField.widthInChars = 25;
	Key_SetCatcher( Key_GetCatcher( ) ^ KEYCATCH_MESSAGE );
}

/*
================
Con_Clear_f
================
*/
void Con_Clear_f (void) {
	int		i;

	for ( i = 0 ; i < CON_TEXTSIZE ; i++ ) {
		con.text[i] = ' ';
		Vector4Copy(g_color_table[ColorIndex(COLOR_WHITE)], con.text_color[i]);
	}

	Con_Bottom();		// go to end
}


/*
================
Con_Dump_f

Save the console contents out to a file
================
*/
void Con_Dump_f (void)
{
	int		l, x, i;
	char	*line;
	fileHandle_t	f;
	int		bufferlen;
	char	*buffer;
	char	filename[MAX_QPATH];

	if (Cmd_Argc() != 2)
	{
		Com_Printf ("usage: condump <filename>\n");
		return;
	}

	Q_strncpyz( filename, Cmd_Argv( 1 ), sizeof( filename ) );
	COM_DefaultExtension( filename, sizeof( filename ), ".txt" );

    if (!COM_CompareExtension(filename, ".txt"))
    {
        Com_Printf("Con_Dump_f: Only the \".txt\" extension is supported by this command!\n");
        return;
    }

	f = FS_FOpenFileWrite( filename );
	if (!f)
	{
		Com_Printf("ERROR: couldn't open %s.\n", filename);
		return;
	}

	Com_Printf("Dumped console text to %s.\n", filename );

	// skip empty lines
	for (l = con.current - con.totallines + 1 ; l <= con.current ; l++)
	{
		line = con.text + (l%con.totallines)*con.linewidth;
		for (x=0 ; x<con.linewidth ; x++)
			if (line[x] != ' ')
				break;
		if (x != con.linewidth)
			break;
	}

#ifdef _WIN32
	bufferlen = con.linewidth + 3 * sizeof ( char );
#else
	bufferlen = con.linewidth + 2 * sizeof ( char );
#endif

	buffer = (char*)Hunk_AllocateTempMemory( bufferlen );

	// write the remaining lines
	buffer[bufferlen-1] = 0;
	for ( ; l <= con.current ; l++)
	{
		line = con.text + (l%con.totallines)*con.linewidth;
		for(i=0; i<con.linewidth; i++)
			buffer[i] = line[i];
		for (x=con.linewidth-1 ; x>=0 ; x--)
		{
			if (buffer[x] == ' ')
				buffer[x] = 0;
			else
				break;
		}
#ifdef _WIN32
		Q_strcat(buffer, bufferlen, "\r\n");
#else
		Q_strcat(buffer, bufferlen, "\n");
#endif
		FS_Write(buffer, strlen(buffer), f);
	}

	Hunk_FreeTempMemory( buffer );
	FS_FCloseFile( f );
}


/*
================
Con_ClearNotify
================
*/
void Con_ClearNotify( void ) {
	Cmd_TokenizeString( NULL );
	CL_GameConsoleText( );
}



/*
================
Con_CheckResize

If the line width has changed, reformat the buffer.
================
*/
void Con_CheckResize (void)
{
	int			i, j, width, oldwidth, oldtotallines, numlines, numchars;
	char		tbuf[CON_TEXTSIZE];
	vec4_t	tcbuf[CON_TEXTSIZE];

	if (cls.glconfig.vidWidth) {
		width = cls.glconfig.vidWidth / SMALLCHAR_WIDTH -2;
		g_consoleField.widthInChars = width -2;
	} else {
		width = (SCREEN_WIDTH / SMALLCHAR_WIDTH) - 2;
	}

	if (width == con.linewidth)
		return;

	if (width < 1)			// video hasn't been initialized yet
	{
		width = DEFAULT_CONSOLE_WIDTH;
		con.linewidth = width;
		con.totallines = CON_TEXTSIZE / con.linewidth;
		for(i=0; i<CON_TEXTSIZE; i++) {
			con.text[i] = ' ';
			Vector4Copy(g_color_table[ColorIndex(COLOR_WHITE)], con.text_color[i]);
		}
	}
	else
	{
		oldwidth = con.linewidth;
		con.linewidth = width;
		oldtotallines = con.totallines;
		con.totallines = CON_TEXTSIZE / con.linewidth;
		numlines = oldtotallines;

		if (con.totallines < numlines)
			numlines = con.totallines;

		numchars = oldwidth;

		if (con.linewidth < numchars)
			numchars = con.linewidth;

			::memcpy(tbuf, con.text, CON_TEXTSIZE * sizeof(char));
			::memcpy(tcbuf, con.text_color, CON_TEXTSIZE * sizeof(vec4_t));
			for(i=0; i<CON_TEXTSIZE; i++) {
				con.text[i] = ' ';
				Vector4Copy(g_color_table[ColorIndex(COLOR_WHITE)], con.text_color[i]);
			}


		for (i=0 ; i<numlines ; i++)
		{
			for (j=0 ; j<numchars ; j++)
			{
				con.text[(con.totallines - 1 - i) * con.linewidth + j] =
						tbuf[((con.current - i + oldtotallines) %
							  oldtotallines) * oldwidth + j];
				Vector4Copy(
					tcbuf[((con.current - i + oldtotallines) % oldtotallines) * oldwidth + j],
					con.text_color[(con.totallines - 1 - i) * con.linewidth + j]);
			}
		}
	}

	con.current = con.totallines - 1;
	con.display = con.current;
}

/*
==================
Cmd_CompleteTxtName
==================
*/
void Cmd_CompleteTxtName( char *args UNUSED, int argNum ) {
	if( argNum == 2 ) {
		Field_CompleteFilename( "", "txt", false, true );
	}
}

/*
================
Con_MessageModesInit
================
*/
void Con_MessageModesInit(void) {
	if( clc.netchan.alternateProtocol == 2 )
	{
		// add the client side message modes for 1.1 servers
		if( !Cmd_CommadExists( "messagemode" ) )
			Cmd_AddCommand ("messagemode", Con_MessageMode_f);
		if( !Cmd_CommadExists( "messagemode2" ) )
			Cmd_AddCommand ("messagemode2", Con_MessageMode2_f);
		if( !Cmd_CommadExists( "messagemode3" ) )
			Cmd_AddCommand ("messagemode3", Con_MessageMode3_f);
		if( !Cmd_CommadExists( "messagemode4" ) )
			Cmd_AddCommand ("messagemode4", Con_MessageMode4_f);
		if( !Cmd_CommadExists( "messagemode5" ) )
			Cmd_AddCommand ("messagemode5", Con_MessageMode5_f);
		if( !Cmd_CommadExists( "messagemode6" ) )
			Cmd_AddCommand ("messagemode6", Con_MessageMode6_f);
	} else
	{
		// remove the client side message modes for non-1.1 servers
		Cmd_RemoveCommand("messagemode");
		Cmd_RemoveCommand("messagemode2");
		Cmd_RemoveCommand("messagemode3");
		Cmd_RemoveCommand("messagemode4");
		Cmd_RemoveCommand("messagemode5");
		Cmd_RemoveCommand("messagemode6");
	}
}

/*
================
Con_Init
================
*/
void Con_Init (void) {
	int		i;

	con_conspeed    = Cvar_Get ("scr_conspeed", "3", 0);
	con_useShader   = Cvar_Get ("scr_useShader", "1", CVAR_ARCHIVE);
	con_height      = Cvar_Get ("scr_height", "50", CVAR_ARCHIVE);
	con_colorRed    = Cvar_Get ("scr_colorRed", "0", CVAR_ARCHIVE);
	con_colorBlue   = Cvar_Get ("scr_colorBlue", "0", CVAR_ARCHIVE);
	con_colorGreen  = Cvar_Get ("scr_colorGreen", "0", CVAR_ARCHIVE);
	con_colorAlpha  = Cvar_Get ("scr_colorAlpha", ".8", CVAR_ARCHIVE);
    con_versionStr  = Cvar_Get ("scr_versionString", Q3_VERSION, CVAR_ARCHIVE);

	Field_Clear( &g_consoleField );
	g_consoleField.widthInChars = g_console_field_width;
	for ( i = 0 ; i < COMMAND_HISTORY ; i++ ) {
		Field_Clear( &historyEditLines[i] );
		historyEditLines[i].widthInChars = g_console_field_width;
	}
	CL_LoadConsoleHistory( );
	if( clc.netchan.alternateProtocol == 2 )
	{
		Cmd_AddCommand ("messagemode", Con_MessageMode_f);
		Cmd_AddCommand ("messagemode2", Con_MessageMode2_f);
		Cmd_AddCommand ("messagemode3", Con_MessageMode3_f);
		Cmd_AddCommand ("messagemode4", Con_MessageMode4_f);
		Cmd_AddCommand ("messagemode5", Con_MessageMode5_f);
		Cmd_AddCommand ("messagemode6", Con_MessageMode6_f);
	}

	Cmd_AddCommand ("toggleconsole", Con_ToggleConsole_f);
	Cmd_AddCommand ("togglemenu", Con_ToggleMenu_f);
	Cmd_AddCommand ("clear", Con_Clear_f);
	Cmd_AddCommand ("condump", Con_Dump_f);
	Cmd_SetCommandCompletionFunc( "condump", Cmd_CompleteTxtName );
}

/*
================
Con_Shutdown
================
*/
void Con_Shutdown(void)
{
	Cmd_RemoveCommand("toggleconsole");
	Cmd_RemoveCommand("togglemenu");
	Cmd_RemoveCommand("clear");
	Cmd_RemoveCommand("condump");
}

/*
===============
Con_Linefeed
===============
*/
void Con_Linefeed(void)
{
	int	i;

	con.x = 0;

	if (con.display == con.current)
		con.display++;

	con.current++;

	for(i=0; i<con.linewidth; i++) {
		con.text[(con.current%con.totallines)*con.linewidth+i] = ' ';
		Vector4Copy(
			g_color_table[ColorIndex(COLOR_WHITE)],
			con.text_color[(con.current%con.totallines)*con.linewidth+i]);
	}
}

/*
================
CL_ConsolePrint

Handles cursor positioning, line wrapping, etc
All console printing must go through this in order to be logged to disk
If no console is visible, the text will appear at the top of the game window
================
*/
void CL_ConsolePrint( const char *txt ) {
	int						y, l;
	unsigned char	c;
	vec4_t				color;
	bool 					skipnotify = false;		// NERVE - SMF
	bool					skip_color_string_check = false;

	// TTimo - prefix for text that shows up in console but not in notify
	// backported from RTCW
	if ( !Q_strncmp( txt, "[skipnotify]", 12 ) ) {
		skipnotify = true;
		txt += 12;
	}
	
	// for some demos we don't want to ever show anything on the console
	if ( cl_noprint && cl_noprint->integer ) {
		return;
	}
	
	if (!con.initialized) {
		con.color[0] =
		con.color[1] =
		con.color[2] =
		con.color[3] = 1.0f;
		con.linewidth = -1;
		Con_CheckResize ();
		con.initialized = true;
	}

	if( !skipnotify && !( Key_GetCatcher( ) & KEYCATCH_CONSOLE ) ) {
		Cmd_SaveCmdContext( );

		// feed the text to cgame
		Cmd_TokenizeString( txt );
		CL_GameConsoleText( );

		Cmd_RestoreCmdContext( );
	}

	Vector4Copy(g_color_table[ColorIndex(COLOR_WHITE)], color);

	while ( (c = *((unsigned char *) txt)) != 0 ) {
		if(skip_color_string_check) {
			skip_color_string_check = false;
		} else if ( Q_IsColorString( txt ) ) {
			if(Q_IsHardcodedColor(txt)) {
				Vector4Copy(g_color_table[ColorIndex(*(txt+1))], color);
			} else {
				Q_GetVectFromHexColor(txt, color);
			}
			txt += Q_ColorStringLength(txt);
			continue;
		} else if(Q_IsColorEscapeEscape(txt)) {
			skip_color_string_check = true;
			txt++;
			continue;
		}

		// count word length
		for (l=0 ; l< con.linewidth ; l++) {
			if ( txt[l] <= ' ') {
				break;
			}

		}

		// word wrap
		if (l != con.linewidth && (con.x + l >= con.linewidth) ) {
			Con_Linefeed( );

		}

		txt++;

		switch (c)
		{
		case INDENT_MARKER:
			break;
		case '\n':
			Con_Linefeed( );
			break;
		case '\r':
			con.x = 0;
			break;
		default:	// display character and advance
			y = con.current % con.totallines;
			con.text[y*con.linewidth+con.x] = c;
			Vector4Copy(color, con.text_color[y*con.linewidth+con.x]);
			con.x++;
			if(con.x >= con.linewidth)
				Con_Linefeed( );
			break;
		}
	}
}


/*
==============================================================================

DRAWING

==============================================================================
*/


/*
================
Con_DrawInput

Draw the editline after a ] prompt
================
*/
static void Con_DrawInput (void)
{
	int		y;

	if ( clc.state != CA_DISCONNECTED && !(Key_GetCatcher( ) & KEYCATCH_CONSOLE ) ) {
		return;
	}

	y = con.vislines - ( SMALLCHAR_HEIGHT * 2 );

	re.SetColor( con.color );

	SCR_DrawSmallChar( con.xadjust + 1 * SMALLCHAR_WIDTH, y, ']' );

	Field_Draw( &g_consoleField,
            con.xadjust + 2 * SMALLCHAR_WIDTH,
            y,
            SCREEN_WIDTH - 3 * SMALLCHAR_WIDTH,
            true, true );
}

/*
================
Con_DrawSolidConsole

Draws the console with the solid background
================
*/
void Con_DrawSolidConsole( float frac ) {
	int				i, x, y;
	int				rows;
	char			*text;
	vec4_t    *text_color;
	int				row;
	int				lines;
	float			currentLuminance = 1.0;
	bool			currentColorChanged = false;
//	qhandle_t		conShader;
	vec4_t			currentColor;
	vec4_t			color;

	lines = cls.glconfig.vidHeight * frac;
	if (lines <= 0)
		return;

	if (lines > cls.glconfig.vidHeight )
		lines = cls.glconfig.vidHeight;

	// on wide screens, we will center the text
	con.xadjust = 0;
	SCR_AdjustFrom640( &con.xadjust, NULL, NULL, NULL );

	// draw the background
	y = frac * SCREEN_HEIGHT;
	if ( y < 1 )
	{
		y = 0;
	}
	else if (con_useShader->integer)
	{
		SCR_DrawPic(0, 0, SCREEN_WIDTH, y, cls.consoleShader);
	}
	else
	{
		color[0] = con_colorRed->value;
		color[1] = con_colorGreen->value;
		color[2] = con_colorBlue->value;
		color[3] = con_colorAlpha->value;
		SCR_FillRect(0, 0, SCREEN_WIDTH, y, color);
	}

	color[0] = 1;
	color[1] = 0;
	color[2] = 0;
	color[3] = 1;
	SCR_FillRect(0, y, SCREEN_WIDTH, 2, color);


	// draw the version number

	re.SetColor( g_color_table[ColorIndex(COLOR_RED)] );

	i = strlen( Q3_VERSION );

	for (x=0 ; x<i ; x++) {
		SCR_DrawSmallChar( cls.glconfig.vidWidth - ( i - x + 1 ) * SMALLCHAR_WIDTH,
			lines - SMALLCHAR_HEIGHT, Q3_VERSION[x] );
	}


	// draw the text
	con.vislines = lines;
	rows = (lines-SMALLCHAR_HEIGHT)/SMALLCHAR_HEIGHT;		// rows of text to draw

	y = lines - (SMALLCHAR_HEIGHT*3);

	// draw from the bottom up
	if (con.display != con.current)
	{
	// draw arrows to show the buffer is backscrolled
		re.SetColor( g_color_table[ColorIndex(COLOR_RED)] );
		for (x=0 ; x<con.linewidth ; x+=4)
			SCR_DrawSmallChar( con.xadjust + (x+1)*SMALLCHAR_WIDTH, y, '^' );
		y -= SMALLCHAR_HEIGHT;
		rows--;
	}
	
	row = con.display;

	if ( con.x == 0 ) {
		row--;
	}

	Vector4Copy(g_color_table[ColorIndex(COLOR_WHITE)], currentColor);
	re.SetColor( currentColor );

	for (i=0 ; i<rows ; i++, y -= SMALLCHAR_HEIGHT, row--)
	{
		if (row < 0)
			break;
		if (con.current - row >= con.totallines) {
			// past scrollback wrap point
			continue;	
		}

		text = con.text + (row % con.totallines)*con.linewidth;
		text_color = con.text_color + (row % con.totallines)*con.linewidth;

		for (x=0 ; x<con.linewidth ; x++) {
			if ( ( text[x] & 0xff ) == ' ' ) {
				continue;
			}

			if(!Vector4Compare(currentColor, text_color[x])) {
				currentColorChanged = true;
				Vector4Copy(text_color[x], currentColor);
				currentLuminance =
					sqrt(
						(0.299 * currentColor[0] * currentColor[0]) +
						(0.587 * currentColor[1] * currentColor[1]) +
						(0.114 * currentColor[2] * currentColor[2]));
			}

			if(currentLuminance <= 0.24) {
				vec4_t hsl;

				//make the color brighter
				Com_rgb_to_hsl(currentColor, hsl);
				hsl[2] = 0.25;
				Com_hsl_to_rgb(hsl, currentColor);
				re.SetColor(currentColor);
				currentColorChanged = false;
			} else if(currentColorChanged) {
				re.SetColor(currentColor);
				currentColorChanged = false;
			}

			SCR_DrawSmallChar(con.xadjust + (x+1)*SMALLCHAR_WIDTH, y, text[x] & 0xff);
		}
	}

	// draw the input prompt, user text, and cursor if desired
	Con_DrawInput ();

	re.SetColor( NULL );
}



/*
==================
Con_DrawConsole
==================
*/
void Con_DrawConsole( void ) {
	// check for console width changes from a vid mode change
	Con_CheckResize ();

	// if disconnected, render console full screen
	if ( clc.state == CA_DISCONNECTED ) {
		if ( !( Key_GetCatcher( ) & (KEYCATCH_UI | KEYCATCH_CGAME)) ) {
			Con_DrawSolidConsole( 1.0 );
			return;
		}
	}

	// draw the chat line
  if( clc.netchan.alternateProtocol == 2 &&
      ( Key_GetCatcher( ) & KEYCATCH_MESSAGE ) )
  {
    int skip;

		if( chatField.buffer[0] == '/' ||
				chatField.buffer[0] == '\\' )
		{
			SCR_DrawBigString( 8, 232, "Command:", 1.0f, false );
			skip = 10;
		}
    else if( chat_team )
    {
      SCR_DrawBigString( 8, 232, "Team Say:", 1.0f, false );
      skip = 11;
    }
    else if( chat_admins )
    {
      SCR_DrawBigString( 8, 232, "Admin Say:", 1.0f, false );
      skip = 11;
    }
    else if( chat_clans )
    {
      SCR_DrawBigString( 8, 232, "Clan Say:", 1.0f, false );
      skip = 11;
    }
    else
    {
      SCR_DrawBigString( 8, 232, "Say:", 1.0f, false );
      skip = 5;
    }

    Field_BigDraw( &chatField, skip * BIGCHAR_WIDTH, 232,
                   SCREEN_WIDTH - ( skip + 1 ) * BIGCHAR_WIDTH, true, true );
	}

	if ( con.displayFrac ) {
		Con_DrawSolidConsole( con.displayFrac );
	}

	if( Key_GetCatcher( ) & ( KEYCATCH_UI | KEYCATCH_CGAME ) )
		return;
}
//================================================================

/*
==================
Con_RunConsole

Scroll it up or down
==================
*/
void Con_RunConsole (void) {
	// decide on the destination height of the console
	if ( Key_GetCatcher( ) & KEYCATCH_CONSOLE )
		con.finalFrac = MAX(0.10, 0.01 * con_height->integer);
	else
		con.finalFrac = 0;				// none visible

	// scroll towards the destination height
	if (con.finalFrac < con.displayFrac)
	{
		con.displayFrac -= con_conspeed->value*cls.realFrametime*0.001;
		if (con.finalFrac > con.displayFrac)
			con.displayFrac = con.finalFrac;

	}
	else if (con.finalFrac > con.displayFrac)
	{
		con.displayFrac += con_conspeed->value*cls.realFrametime*0.001;
		if (con.finalFrac < con.displayFrac)
			con.displayFrac = con.finalFrac;
	}

}


void Con_PageUp( void ) {
	con.display -= 2;
	if ( con.current - con.display >= con.totallines ) {
		con.display = con.current - con.totallines + 1;
	}
}

void Con_PageDown( void ) {
	con.display += 2;
	if (con.display > con.current) {
		con.display = con.current;
	}
}

void Con_Top( void ) {
	con.display = con.totallines;
	if ( con.current - con.display >= con.totallines ) {
		con.display = con.current - con.totallines + 1;
	}
}

void Con_Bottom( void ) {
	con.display = con.current;
}


void Con_Close( void ) {
	if ( !com_cl_running->integer ) {
		return;
	}
	Field_Clear( &g_consoleField );
	Key_SetCatcher( Key_GetCatcher( ) & ~KEYCATCH_CONSOLE );
	con.finalFrac = 0;				// none visible
	con.displayFrac = 0;
}
