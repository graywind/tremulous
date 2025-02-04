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

// g_maprotation.c -- the map rotation system

#include "g_local.h"

#define MAX_MAP_ROTATIONS       64
#define MAX_MAP_ROTATION_MAPS   256

#define NOT_ROTATING            -1

typedef enum
{
  CV_ERR,
  CV_RANDOM,
  CV_NUMCLIENTS,
  CV_LASTWIN
} conditionVariable_t;

typedef enum
{
  CO_LT,
  CO_EQ,
  CO_GT
} conditionOp_t;

typedef struct condition_s
{
  struct node_s       *target;

  conditionVariable_t lhs;
  conditionOp_t       op;

  int                 numClients;
  team_t              lastWin;
} condition_t;

typedef struct map_s
{
  char  name[ MAX_QPATH ];

  char  postCommand[ MAX_STRING_CHARS ];
  char  layouts[ MAX_CVAR_VALUE_STRING ];
} map_t;

typedef struct label_s
{
  char name[ MAX_QPATH ];
} label_t;

typedef enum
{
  NT_MAP,
  NT_CONDITION,
  NT_GOTO,
  NT_RESUME,
  NT_LABEL,
  NT_RETURN
} nodeType_t;

typedef struct node_s
{
  nodeType_t    type;

  union
  {
    map_t       map;
    condition_t condition;
    label_t     label;
  } u;

} node_t;

typedef struct mapRotation_s
{
  char    name[ MAX_QPATH ];

  node_t  *nodes[ MAX_MAP_ROTATION_MAPS ];
  int     numNodes;
  int     currentNode;
} mapRotation_t;

typedef struct mapRotations_s
{
  mapRotation_t   rotations[ MAX_MAP_ROTATIONS ];
  int             numRotations;
} mapRotations_t;

static mapRotations_t mapRotations;

static int G_NodeIndexAfter( int currentNode, int rotation );

/*
===============
G_MapExists

Check if a map exists
===============
*/
qboolean G_MapExists( const char *name )
{
  return trap_FS_FOpenFile( va( "maps/%s.bsp", name ), NULL, FS_READ ) > 0;
}

/*
===============
G_LayoutExists

Check if a layout exists for a map
===============
*/
qboolean G_LayoutExists( const char *map, const char *layout )
{
  return !Q_stricmp( layout, "*BUILTIN*" ) || trap_FS_FOpenFile( va( "layouts/%s/%s.dat", map, layout ), NULL, FS_READ ) > 0;
}

/*
===============
G_RotationExists

Check if a rotation exists
===============
*/
static qboolean G_RotationExists( char *name )
{
  int i;

  for( i = 0; i < mapRotations.numRotations; i++ )
  {
    if( Q_strncmp( mapRotations.rotations[ i ].name, name, MAX_QPATH ) == 0 )
      return qtrue;
  }

  return qfalse;
}

/*
===============
G_LabelExists

Check if a label exists in a rotation
===============
*/
static qboolean G_LabelExists( int rotation, char *name )
{
  mapRotation_t *mr = &mapRotations.rotations[ rotation ];
  int           i;

  for( i = 0; i < mr->numNodes; i++ )
  {
    node_t *node = mr->nodes[ i ];

    if( node->type == NT_LABEL &&
        !Q_stricmp( name, node->u.label.name ) )
      return qtrue;

    if( node->type == NT_MAP &&
        !Q_stricmp( name, node->u.map.name ) )
      return qtrue;
  }

  return qfalse;
}

/*
===============
G_AllocateNode

Allocate memory for a node_t
===============
*/
static node_t *G_AllocateNode( void )
{
  node_t *node = BG_Alloc( sizeof( node_t ) );

  return node;
}

/*
===============
G_ParseMapCommandSection

Parse a map rotation command section
===============
*/
static qboolean G_ParseMapCommandSection( node_t *node, char **text_p )
{
  char  *token;
  map_t *map = &node->u.map;
  int   commandLength = 0;

  // read optional parameters
  while( 1 )
  {
    token = COM_Parse( text_p );

    if( !*token )
      break;

    if( !Q_stricmp( token, "" ) )
      return qfalse;

    if( !Q_stricmp( token, "}" ) )
    {
      if( commandLength > 0 )
      {
        // Replace last ; with \n
        map->postCommand[ commandLength - 1 ] = '\n';
      }

      return qtrue; //reached the end of this command section
    }

    if( !Q_stricmp( token, "layouts" ) )
    {
      token = COM_ParseExt( text_p, qfalse );
      map->layouts[ 0 ] = '\0';

      while( token[ 0 ] != 0 )
      {
        Q_strcat( map->layouts, sizeof( map->layouts ), token );
        Q_strcat( map->layouts, sizeof( map->layouts ), " " );
        token = COM_ParseExt( text_p, qfalse );
      }

      continue;
    }

    // Parse the rest of the line into map->postCommand
    Q_strcat( map->postCommand, sizeof( map->postCommand ), token );
    Q_strcat( map->postCommand, sizeof( map->postCommand ), " " );

    token = COM_ParseExt( text_p, qfalse );

    while( token[ 0 ] != 0 )
    {
      Q_strcat( map->postCommand, sizeof( map->postCommand ), token );
      Q_strcat( map->postCommand, sizeof( map->postCommand ), " " );
      token = COM_ParseExt( text_p, qfalse );
    }

    commandLength = strlen( map->postCommand );
    map->postCommand[ commandLength - 1 ] = ';';
  }

  return qfalse;
}

/*
===============
G_ParseNode

Parse a node
===============
*/
static qboolean G_ParseNode( node_t **node, char *token, char **text_p, qboolean conditional )
{
  if( !Q_stricmp( token, "if" ) )
  {
    condition_t *condition;

    (*node)->type = NT_CONDITION;
    condition = &(*node)->u.condition;

    token = COM_Parse( text_p );

    if( !*token )
      return qfalse;

    if( !Q_stricmp( token, "numClients" ) )
    {
      condition->lhs = CV_NUMCLIENTS;

      token = COM_Parse( text_p );

      if( !*token )
        return qfalse;

      if( !Q_stricmp( token, "<" ) )
        condition->op = CO_LT;
      else if( !Q_stricmp( token, ">" ) )
        condition->op = CO_GT;
      else if( !Q_stricmp( token, "=" ) )
        condition->op = CO_EQ;
      else
      {
        G_Printf( S_COLOR_RED "ERROR: invalid operator in expression: %s\n", token );
        return qfalse;
      }

      token = COM_Parse( text_p );

      if( !*token )
        return qfalse;

      condition->numClients = atoi( token );
    }
    else if( !Q_stricmp( token, "lastWin" ) )
    {
      condition->lhs = CV_LASTWIN;

      token = COM_Parse( text_p );

      if( !*token )
        return qfalse;

      if( !Q_stricmp( token, "aliens" ) )
        condition->lastWin = TEAM_ALIENS;
      else if( !Q_stricmp( token, "humans" ) )
        condition->lastWin = TEAM_HUMANS;
      else
      {
        G_Printf( S_COLOR_RED "ERROR: invalid right hand side in expression: %s\n", token );
        return qfalse;
      }
    }
    else if( !Q_stricmp( token, "random" ) )
      condition->lhs = CV_RANDOM;
    else
    {
      G_Printf( S_COLOR_RED "ERROR: invalid left hand side in expression: %s\n", token );
      return qfalse;
    }

    token = COM_Parse( text_p );

    if( !*token )
      return qfalse;

    condition->target = G_AllocateNode( );
    *node = condition->target;

    return G_ParseNode( node, token, text_p, qtrue );
  }
  else if( !Q_stricmp( token, "return" ) )
  {
    (*node)->type = NT_RETURN;
  }
  else if( !Q_stricmp( token, "goto" ) ||
           !Q_stricmp( token, "resume" ) )
  {
    label_t *label;

    if( !Q_stricmp( token, "goto" ) )
      (*node)->type = NT_GOTO;
    else
      (*node)->type = NT_RESUME;
    label = &(*node)->u.label;

    token = COM_Parse( text_p );

    if( !*token )
    {
      G_Printf( S_COLOR_RED "ERROR: goto or resume without label\n" );
      return qfalse;
    }

    Q_strncpyz( label->name, token, sizeof( label->name ) );
  }
  else if( *token == '#' || conditional )
  {
    label_t *label;

    (*node)->type = ( conditional ) ? NT_GOTO : NT_LABEL;
    label = &(*node)->u.label;

    Q_strncpyz( label->name, token, sizeof( label->name ) );
  }
  else
  {
    map_t *map;

    (*node)->type = NT_MAP;
    map = &(*node)->u.map;

    Q_strncpyz( map->name, token, sizeof( map->name ) );
    map->postCommand[ 0 ] = '\0';
  }

  return qtrue;
}

/*
===============
G_ParseMapRotation

Parse a map rotation section
===============
*/
static qboolean G_ParseMapRotation( mapRotation_t *mr, char **text_p )
{
  char      *token;
  node_t    *node = NULL;

  // read optional parameters
  while( 1 )
  {
    token = COM_Parse( text_p );

    if( !*token )
      break;

    if( !Q_stricmp( token, "" ) )
      return qfalse;

    if( !Q_stricmp( token, "{" ) )
    {
      if( node == NULL )
      {
        G_Printf( S_COLOR_RED "ERROR: map command section with no associated map\n" );
        return qfalse;
      }

      if( !G_ParseMapCommandSection( node, text_p ) )
      {
        G_Printf( S_COLOR_RED "ERROR: failed to parse map command section\n" );
        return qfalse;
      }

      continue;
    }
    else if( !Q_stricmp( token, "}" ) )
    {
      // Reached the end of this map rotation
      return qtrue;
    }

    if( mr->numNodes == MAX_MAP_ROTATION_MAPS )
    {
      G_Printf( S_COLOR_RED "ERROR: maximum number of maps in one rotation (%d) reached\n",
                MAX_MAP_ROTATION_MAPS );
      return qfalse;
    }

    node = G_AllocateNode( );
    mr->nodes[ mr->numNodes++ ] = node;

    if( !G_ParseNode( &node, token, text_p, qfalse ) )
      return qfalse;
  }

  return qfalse;
}

/*
===============
G_ParseMapRotationFile

Load the map rotations from a map rotation file
===============
*/
static qboolean G_ParseMapRotationFile( const char *fileName )
{
  char          *text_p;
  int           i, j;
  int           len;
  char          *token;
  char          text[ 20000 ];
  char          mrName[ MAX_QPATH ];
  qboolean      mrNameSet = qfalse;
  fileHandle_t  f;

  // load the file
  len = trap_FS_FOpenFile( fileName, &f, FS_READ );
  if( len < 0 )
    return qfalse;

  if( len == 0 || len >= sizeof( text ) - 1 )
  {
    trap_FS_FCloseFile( f );
    G_Printf( S_COLOR_RED "ERROR: map rotation file %s is %s\n", fileName,
      len == 0 ? "empty" : "too long" );
    return qfalse;
  }

  trap_FS_Read( text, len, f );
  text[ len ] = 0;
  trap_FS_FCloseFile( f );

  // parse the text
  text_p = text;

  // read optional parameters
  while( 1 )
  {
    token = COM_Parse( &text_p );

    if( !*token )
      break;

    if( !Q_stricmp( token, "" ) )
      break;

    if( !Q_stricmp( token, "{" ) )
    {
      if( mrNameSet )
      {
        //check for name space clashes
        if( G_RotationExists( mrName ) )
        {
          G_Printf( S_COLOR_RED "ERROR: a map rotation is already named %s\n", mrName );
          return qfalse;
        }

        if( mapRotations.numRotations == MAX_MAP_ROTATIONS )
        {
          G_Printf( S_COLOR_RED "ERROR: maximum number of map rotations (%d) reached\n",
                    MAX_MAP_ROTATIONS );
          return qfalse;
        }

        Q_strncpyz( mapRotations.rotations[ mapRotations.numRotations ].name, mrName, MAX_QPATH );

        if( !G_ParseMapRotation( &mapRotations.rotations[ mapRotations.numRotations ], &text_p ) )
        {
          G_Printf( S_COLOR_RED "ERROR: %s: failed to parse map rotation %s\n", fileName, mrName );
          return qfalse;
        }

        mapRotations.numRotations++;

        //start parsing map rotations again
        mrNameSet = qfalse;

        continue;
      }
      else
      {
        G_Printf( S_COLOR_RED "ERROR: unnamed map rotation\n" );
        return qfalse;
      }
    }

    if( !mrNameSet )
    {
      Q_strncpyz( mrName, token, sizeof( mrName ) );
      mrNameSet = qtrue;
    }
    else
    {
      G_Printf( S_COLOR_RED "ERROR: map rotation already named\n" );
      return qfalse;
    }
  }

  for( i = 0; i < mapRotations.numRotations; i++ )
  {
    mapRotation_t *mr = &mapRotations.rotations[ i ];
    int           mapCount = 0;

    for( j = 0; j < mr->numNodes; j++ )
    {
      node_t        *node = mr->nodes[ j ];

      if( node->type == NT_MAP )
      {
        mapCount++;
        if( !G_MapExists( node->u.map.name ) )
        {
          G_Printf( S_COLOR_RED "ERROR: rotation map \"%s\" doesn't exist\n",
                    node->u.map.name );
          return qfalse;
        }
        continue;
      }
      else if( node->type == NT_RETURN )
        continue;
      else if( node->type == NT_LABEL )
        continue;
      else while( node->type == NT_CONDITION )
        node = node->u.condition.target;

      if( ( node->type == NT_GOTO || node->type == NT_RESUME ) &&
          !G_LabelExists( i, node->u.label.name ) &&
          !G_RotationExists( node->u.label.name ) )
      {
        G_Printf( S_COLOR_RED "ERROR: goto destination named \"%s\" doesn't exist\n",
                  node->u.label.name );
        return qfalse;
      }
    }

    if( mapCount == 0 )
    {
      G_Printf( S_COLOR_RED "ERROR: rotation \"%s\" needs at least one map entry\n",
        mr->name );
      return qfalse;
    }
  }

  return qtrue;
}

/*
===============
G_PrintSpaces
===============
*/
static void G_PrintSpaces( int spaces )
{
  int i;

  for( i = 0; i < spaces; i++ )
    G_Printf( " " );
}

/*
===============
G_PrintRotations

Print the parsed map rotations
===============
*/
void G_PrintRotations( void )
{
  int i, j;
  int size = sizeof( mapRotations );

  G_Printf( "Map rotations as parsed:\n\n" );

  for( i = 0; i < mapRotations.numRotations; i++ )
  {
    mapRotation_t *mr = &mapRotations.rotations[ i ];

    G_Printf( "rotation: %s\n{\n", mr->name );

    size += mr->numNodes * sizeof( node_t );

    for( j = 0; j < mr->numNodes; j++ )
    {
      node_t *node = mr->nodes[ j ];
      int indentation = 0;

      while( node->type == NT_CONDITION )
      {
        G_PrintSpaces( indentation );
        G_Printf( "  condition\n" );
        node = node->u.condition.target;

        size += sizeof( node_t );

        indentation += 2;
      }

      G_PrintSpaces( indentation );

      switch( node->type )
      {
        case NT_MAP:
          G_Printf( "  %s\n", node->u.map.name );

          if( strlen( node->u.map.postCommand ) > 0 )
            G_Printf( "    command: %s", node->u.map.postCommand );

          break;

        case NT_RETURN:
          G_Printf( "  return\n" );
          break;

        case NT_LABEL:
          G_Printf( "  label: %s\n", node->u.label.name );
          break;

        case NT_GOTO:
          G_Printf( "  goto: %s\n", node->u.label.name );
          break;

        case NT_RESUME:
          G_Printf( "  resume: %s\n", node->u.label.name );
          break;

        default:
          break;
      }
    }

    G_Printf( "}\n" );
  }

  G_Printf( "Total memory used: %d bytes\n", size );
}

/*
===============
G_ClearRotationStack

Clear the rotation stack
===============
*/
void G_ClearRotationStack( void )
{
  trap_Cvar_Set( "g_mapRotationStack", "" );
  trap_Cvar_Update( &g_mapRotationStack );
}

/*
===============
G_PushRotationStack

Push the rotation stack
===============
*/
static void G_PushRotationStack( int rotation )
{
  char text[ MAX_CVAR_VALUE_STRING ];

  Com_sprintf( text, sizeof( text ), "%d %s",
               rotation, g_mapRotationStack.string );
  trap_Cvar_Set( "g_mapRotationStack", text );
  trap_Cvar_Update( &g_mapRotationStack );
}

/*
===============
G_PopRotationStack

Pop the rotation stack
===============
*/
static int G_PopRotationStack( void )
{
  int   value = -1;
  char  text[ MAX_CVAR_VALUE_STRING ];
  char  *text_p, *token;

  Q_strncpyz( text, g_mapRotationStack.string, sizeof( text ) );

  text_p = text;
  token = COM_Parse( &text_p );

  if( *token )
    value = atoi( token );

  if( text_p )
  {
    while ( *text_p == ' ' )
      text_p++;
    trap_Cvar_Set( "g_mapRotationStack", text_p );
    trap_Cvar_Update( &g_mapRotationStack );
  }
  else
    G_ClearRotationStack( );

  return value;
}

/*
===============
G_RotationNameByIndex

Returns the name of a rotation by its index
===============
*/
static char *G_RotationNameByIndex( int index )
{
  if( index >= 0 && index < mapRotations.numRotations )
    return mapRotations.rotations[ index ].name;
  return NULL;
}

/*
===============
G_CurrentNodeIndexArray

Fill a static array with the current node of each rotation
===============
*/
static int *G_CurrentNodeIndexArray( void )
{
  static int  currentNode[ MAX_MAP_ROTATIONS ];
  int         i = 0;
  char        text[ MAX_MAP_ROTATIONS * 2 ];
  char        *text_p, *token;

  Q_strncpyz( text, g_mapRotationNodes.string, sizeof( text ) );

  text_p = text;

  while( 1 )
  {
    token = COM_Parse( &text_p );

    if( !*token )
      break;

    currentNode[ i++ ] = atoi( token );
  }

  return currentNode;
}

/*
===============
G_SetCurrentNodeByIndex

Set the current map in some rotation
===============
*/
static void G_SetCurrentNodeByIndex( int currentNode, int rotation )
{
  char  text[ MAX_MAP_ROTATIONS * 4 ] = { 0 };
  int   *p = G_CurrentNodeIndexArray( );
  int   i;

  p[ rotation ] = currentNode;

  for( i = 0; i < mapRotations.numRotations; i++ )
    Q_strcat( text, sizeof( text ), va( "%d ", p[ i ] ) );

  trap_Cvar_Set( "g_mapRotationNodes", text );
  trap_Cvar_Update( &g_mapRotationNodes );
}

/*
===============
G_CurrentNodeIndex

Return the current node index in some rotation
===============
*/
static int G_CurrentNodeIndex( int rotation )
{
  int   *p = G_CurrentNodeIndexArray( );

  return p[ rotation ];
}

/*
===============
G_NodeByIndex

Return a node in a rotation by its index
===============
*/
static node_t *G_NodeByIndex( int index, int rotation )
{
  if( rotation >= 0 && rotation < mapRotations.numRotations &&
      index >= 0 && index < mapRotations.rotations[ rotation ].numNodes )
    return mapRotations.rotations[ rotation ].nodes[ index ];

  return NULL;
}

/*
===============
G_IssueMapChange

Send commands to the server to actually change the map
===============
*/
static void G_IssueMapChange( int index, int rotation )
{
  node_t *node = mapRotations.rotations[ rotation ].nodes[ index ];
  map_t  *map = &node->u.map;

  // allow a manually defined g_nextLayout setting to override the maprotation
  if( !g_nextLayout.string[ 0 ] && map->layouts[ 0 ] )
  {
    trap_Cvar_Set( "g_nextLayout", map->layouts );
  }

  G_MapConfigs( map->name );

  trap_SendConsoleCommand( EXEC_APPEND, va( "map \"%s\"\n", map->name ) );

  if( strlen( map->postCommand ) > 0 )
    trap_SendConsoleCommand( EXEC_APPEND, map->postCommand );
}

/*
===============
G_GotoLabel

Resolve the label of some condition
===============
*/
static qboolean G_GotoLabel( int rotation, int nodeIndex, char *name,
                             qboolean reset_index, int depth )
{
  node_t *node;
  int    i;

  // Search the rotation names...
  if( G_StartMapRotation( name, qtrue, qtrue, reset_index, depth ) )
    return qtrue;

  // ...then try labels in the rotation
  for( i = 0; i < mapRotations.rotations[ rotation ].numNodes; i++ )
  {
    node = mapRotations.rotations[ rotation ].nodes[ i ];

    if( node->type == NT_LABEL && !Q_stricmp( node->u.label.name, name ) )
    {
      G_SetCurrentNodeByIndex( G_NodeIndexAfter( i, rotation ), rotation );
      G_AdvanceMapRotation( depth );
      return qtrue;
    }
  }

  // finally check for a map by name
  for( i = 0; i < mapRotations.rotations[ rotation ].numNodes; i++ )
  {
    nodeIndex = G_NodeIndexAfter( nodeIndex, rotation );
    node = mapRotations.rotations[ rotation ].nodes[ nodeIndex ];

    if( node->type == NT_MAP && !Q_stricmp( node->u.map.name, name ) )
    {
      G_SetCurrentNodeByIndex( nodeIndex, rotation );
      G_AdvanceMapRotation( depth );
      return qtrue;
    }
  }

  return qfalse;
}

/*
===============
G_EvaluateMapCondition

Evaluate a map condition
===============
*/
static qboolean G_EvaluateMapCondition( condition_t **condition )
{
  qboolean    result = qfalse;
  condition_t *localCondition = *condition;

  switch( localCondition->lhs )
  {
    case CV_RANDOM:
      result = rand( ) / ( RAND_MAX / 2 + 1 );
      break;

    case CV_NUMCLIENTS:
      switch( localCondition->op )
      {
        case CO_LT:
          result = level.numConnectedClients < localCondition->numClients;
          break;

        case CO_GT:
          result = level.numConnectedClients > localCondition->numClients;
          break;

        case CO_EQ:
          result = level.numConnectedClients == localCondition->numClients;
          break;
      }
      break;

    case CV_LASTWIN:
      result = level.lastWin == localCondition->lastWin;
      break;

    default:
    case CV_ERR:
      G_Printf( S_COLOR_RED "ERROR: malformed map switch localCondition\n" );
      break;
  }

  if( localCondition->target->type == NT_CONDITION )
  {
    *condition = &localCondition->target->u.condition;

    return result && G_EvaluateMapCondition( condition );
  }

  return result;
}

/*
===============
G_NodeIndexAfter
===============
*/
static int G_NodeIndexAfter( int currentNode, int rotation )
{
  mapRotation_t *mr = &mapRotations.rotations[ rotation ];

  return ( currentNode + 1 ) % mr->numNodes;
}

/*
===============
G_StepMapRotation

Run one node of a map rotation
===============
*/
qboolean G_StepMapRotation( int rotation, int nodeIndex, int depth )
{
  node_t        *node;
  condition_t   *condition;
  int           returnRotation;
  qboolean      step = qtrue;

  node = G_NodeByIndex( nodeIndex, rotation );
  depth++;

  // guard against inifinite loop in conditional code
  if( depth > 32 && node->type != NT_MAP )
  {
    if( depth > 64 )
    {
      G_Printf( S_COLOR_RED "ERROR: infinite loop protection stopped at map rotation %s\n",
              G_RotationNameByIndex( rotation ) );
      return qfalse;
    }

    G_Printf( S_COLOR_YELLOW "WARNING: possible infinite loop in map rotation %s\n",
              G_RotationNameByIndex( rotation ) );
    return qtrue;
  }

  while( step )
  {
    step = qfalse;
    switch( node->type )
    {
      case NT_CONDITION:
        condition = &node->u.condition;

        if( G_EvaluateMapCondition( &condition ) )
        {
          node = condition->target;
          step = qtrue;
          continue;
        }
        break;

      case NT_RETURN:
        returnRotation = G_PopRotationStack( );
        if( returnRotation >= 0 )
        {
          G_SetCurrentNodeByIndex(
            G_NodeIndexAfter( nodeIndex, rotation ), rotation );
          if( G_StartMapRotation( G_RotationNameByIndex( returnRotation ),
                                  qtrue, qfalse, qfalse, depth ) )
          {
            return qfalse;
          }
        }
        break;

      case NT_MAP:
        if( G_MapExists( node->u.map.name ) )
        {
          G_SetCurrentNodeByIndex(
            G_NodeIndexAfter( nodeIndex, rotation ), rotation );
          G_IssueMapChange( nodeIndex, rotation );
          return qfalse;
        }

        G_Printf( S_COLOR_YELLOW "WARNING: skipped missing map %s in rotation %s\n",
                  node->u.map.name, G_RotationNameByIndex( rotation ) );
        break;

      case NT_LABEL:
        break;

      case NT_GOTO:
      case NT_RESUME:
        G_SetCurrentNodeByIndex(
          G_NodeIndexAfter( nodeIndex, rotation ), rotation );
        if ( G_GotoLabel( rotation, nodeIndex, node->u.label.name,
                          ( node->type == NT_GOTO ), depth ) )
          return qfalse;

        G_Printf( S_COLOR_YELLOW "WARNING: label, map, or rotation %s not found in %s\n",
                  node->u.label.name, G_RotationNameByIndex( rotation ) );
        break;
    }
  }

  return qtrue;
}

/*
===============
G_AdvanceMapRotation

Increment the current map rotation
===============
*/
void G_AdvanceMapRotation( int depth )
{
  node_t *node;
  int    rotation;
  int    nodeIndex;

  rotation = g_currentMapRotation.integer;
  if( rotation < 0 || rotation >= MAX_MAP_ROTATIONS )
    return;

  nodeIndex = G_CurrentNodeIndex( rotation );
  node = G_NodeByIndex( nodeIndex, rotation );
  if( !node )
  {
    G_Printf( S_COLOR_YELLOW "WARNING: index incorrect for map rotation %s, trying 0\n",
              G_RotationNameByIndex( rotation) );
    nodeIndex = 0;
    node = G_NodeByIndex( nodeIndex, rotation );
  }

  while( node && G_StepMapRotation( rotation, nodeIndex, depth ) )
  {
    nodeIndex = G_NodeIndexAfter( nodeIndex, rotation );
    node = G_NodeByIndex( nodeIndex, rotation );
    depth++;
  }

  if( !node )
    G_Printf( S_COLOR_RED "ERROR: unexpected end of maprotation '%s'\n",
              G_RotationNameByIndex( rotation) );
}

/*
===============
G_StartMapRotation

Switch to a new map rotation
===============
*/
qboolean G_StartMapRotation( char *name, qboolean advance,
                             qboolean putOnStack, qboolean reset_index, int depth )
{
  int i;
  int currentRotation = g_currentMapRotation.integer;

  for( i = 0; i < mapRotations.numRotations; i++ )
  {
    if( !Q_stricmp( mapRotations.rotations[ i ].name, name ) )
    {
      if( putOnStack && currentRotation >= 0 )
        G_PushRotationStack( currentRotation );

      trap_Cvar_Set( "g_currentMapRotation", va( "%d", i ) );
      trap_Cvar_Update( &g_currentMapRotation );

      if( advance )
      {
        if( reset_index )
          G_SetCurrentNodeByIndex( 0, i );

        G_AdvanceMapRotation( depth );
      }

      break;
    }
  }

  if( i == mapRotations.numRotations )
    return qfalse;
  else
    return qtrue;
}

/*
===============
G_StopMapRotation

Stop the current map rotation
===============
*/
void G_StopMapRotation( void )
{
  trap_Cvar_Set( "g_currentMapRotation", va( "%d", NOT_ROTATING ) );
  trap_Cvar_Update( &g_currentMapRotation );
}

/*
===============
G_MapRotationActive

Test if any map rotation is currently active
===============
*/
qboolean G_MapRotationActive( void )
{
  return ( g_currentMapRotation.integer != NOT_ROTATING );
}

/*
===============
G_InitMapRotations

Load and initialise the map rotations
===============
*/
void G_InitMapRotations( void )
{
  const char  *fileName = "maprotation.cfg";

  // Load the file if it exists
  if( trap_FS_FOpenFile( fileName, NULL, FS_READ ) )
  {
    if( !G_ParseMapRotationFile( fileName ) )
      G_Printf( S_COLOR_RED "ERROR: failed to parse %s file\n", fileName );
  }
  else
    G_Printf( "%s file not found.\n", fileName );

  if( g_currentMapRotation.integer == NOT_ROTATING )
  {
    if( g_initialMapRotation.string[ 0 ] != 0 )
    {
      G_StartMapRotation( g_initialMapRotation.string, qfalse, qtrue, qfalse, 0 );

      trap_Cvar_Set( "g_initialMapRotation", "" );
      trap_Cvar_Update( &g_initialMapRotation );
    }
  }
}

/*
===============
G_FreeNode

Free up memory used by a node
===============
*/
void G_FreeNode( node_t *node )
{
  if( node->type == NT_CONDITION )
    G_FreeNode( node->u.condition.target );

  BG_Free( node );
}

/*
===============
G_ShutdownMapRotations

Free up memory used by map rotations
===============
*/
void G_ShutdownMapRotations( void )
{
  int i, j;

  for( i = 0; i < mapRotations.numRotations; i++ )
  {
    mapRotation_t *mr = &mapRotations.rotations[ i ];

    for( j = 0; j < mr->numNodes; j++ )
    {
      node_t *node = mr->nodes[ j ];

      G_FreeNode( node );
    }
  }
}
