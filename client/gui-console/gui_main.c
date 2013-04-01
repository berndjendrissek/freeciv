/********************************************************************** 
 Freeciv - Copyright (C) 1996 - A Kjeldberg, L Gregersen, P Unold
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef AUDIO_SDL
#include "SDL.h"
#endif

#include <stdio.h>
#include <stdlib.h>

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

/* utility */
#include "fciconv.h"
#include "game.h"
#include "log.h"
#include "mem.h"
#include "netintf.h"
#include "support.h"

/* client */
#include "chatline_common.h"
#include "client_main.h"
#include "climap.h"
#include "clinet.h"
#include "control.h"
#include "editgui_g.h"
#include "ggz_g.h"
#include "options.h"
#include "tilespec.h"

#include "gui_main.h"

struct closure {
  void (*fn)(void *);
  void *context;
};

struct idle_callback {
  struct closure callback;
  struct idle_callback *next;
};

struct command_handler {
  char const *command;
  void (*handler)(int argc, char *argv[], void *context);
  void *context;
};

struct idle_callback *idle_callbacks = NULL;

const char *client_string = "gui-console";

const char * const gui_character_encoding = "UTF-8";
const bool gui_use_transliteration = FALSE;

struct {
  int server;
  bool server_writable;
} sockets = {
  .server = -1,
  .server_writable = FALSE,
};

char *next_word(char const **s, char const *ifs)
{
  char const *first_delimiter;
  char *word;

  if (!*s) {
    return NULL;
  }

  *s += strspn(*s, ifs);
  if (!**s) {
    return NULL;
  }

  first_delimiter = strpbrk(*s, ifs);
  if (first_delimiter) {
    word = fc_malloc(first_delimiter - *s + 1);
    mystrlcpy(word, *s, first_delimiter - *s + 1);
    *s = first_delimiter;
  } else {
    word = mystrdup(*s);
    *s = NULL;
  }

  return word;
}

static char const *console_activity_name(enum unit_activity activity)
{
  switch (activity) {
  case ACTIVITY_IDLE:
    return "Idle";
  case ACTIVITY_POLLUTION:
    return "Pollution";
  case ACTIVITY_ROAD:
    return "Road";
  case ACTIVITY_MINE:
    return "Mine";
  case ACTIVITY_IRRIGATE:
    return "Irrigate";
  case ACTIVITY_FORTIFIED:
    return "Fortified";
  case ACTIVITY_FORTRESS:
    return "Fortress";
  case ACTIVITY_SENTRY:
    return "Sentry";
  case ACTIVITY_RAILROAD:
    return "Railroad";
  case ACTIVITY_PILLAGE:
    return "Pillage";
  case ACTIVITY_GOTO:
    return "Goto";
  case ACTIVITY_EXPLORE:
    return "Explore";
  case ACTIVITY_TRANSFORM:
    return "Transform";
  case ACTIVITY_UNKNOWN:
    return "Unknown";
  case ACTIVITY_AIRBASE:
    return "Airbase";
  case ACTIVITY_FORTIFYING:
    return "Fortify";
  case ACTIVITY_FALLOUT:
    return "Fallout";
  case ACTIVITY_PATROL_UNUSED:
    return "Patrol (unused)";
  case ACTIVITY_BASE:
    return "Base";
  case ACTIVITY_LAST:
    return "Last (invalid)";
  default:
    return NULL;
  }
}

static char const *console_orders_name(enum unit_orders order)
{
  switch (order) {
  case ORDER_MOVE:
    return "Move";
  case ORDER_ACTIVITY:
    return "Activity";
  case ORDER_FULL_MP:
    return "Full MP";
  case ORDER_BUILD_CITY:
    return "City";
  case ORDER_DISBAND:
    return "Disband";
  case ORDER_BUILD_WONDER:
    return "Wonder";
  case ORDER_TRADE_ROUTE:
    return "Trade route";
  case ORDER_HOMECITY:
    return "Rehome";
  case ORDER_LAST:
    return "Last (invalid)";
  default:
    return NULL;
  }
}

static char const *console_direction_name(enum direction8 dir)
{
  switch (dir) {
  case DIR8_NORTHWEST:
    return "NW";
  case DIR8_NORTH:
    return "N";
  case DIR8_NORTHEAST:
    return "NE";
  case DIR8_WEST:
    return "W";
  case DIR8_EAST:
    return "E";
  case DIR8_SOUTHWEST:
    return "SW";
  case DIR8_SOUTH:
    return "S";
  case DIR8_SOUTHEAST:
    return "SE";
  default:
    return NULL;
  }
}

static bool console_get_tile(char const *word, struct tile **pptile)
{
  int x, y;
  char const *comma = strchr(word, ',');

  if (!comma) {
    return FALSE;
  }

  x = atoi(word);
  y = atoi(comma + 1);

  *pptile = map_pos_to_tile(x, y);

  return TRUE;
}

static void console_unit_summary(struct unit *punit, char const *prefix)
{
  fc_printf("%s- %d. (%d, %d) %s (%s) %d/%d [%s]\n", prefix,
	    punit->id, TILE_XY(punit->tile),
	    unit_name_translation(punit),
	    punit->utype->veteran[punit->veteran].name,
	    punit->moves_left, punit->utype->move_rate,
	    nation_plural_for_player(punit->owner));
}

static void console_unit_orders(struct unit *punit, char const *prefix)
{
  int order_index;

  for (order_index = 0; order_index < punit->orders.length; order_index++) {
    char const *cursor = (order_index == punit->orders.index ? " <--" : "");

    switch (punit->orders.list[order_index].order) {
    case ORDER_ACTIVITY:
      switch (punit->orders.list[order_index].activity) {
      case ACTIVITY_BASE:
	fc_printf("%s- Base %d%s\n", prefix,
		  punit->orders.list[order_index].base, cursor);
	break;
      default:
	fc_printf("%s- %s%s\n", prefix,
		  console_activity_name(punit->orders.list[order_index].activity),
		  cursor);
	break;
      }
      break;
    case ORDER_MOVE:
      fc_printf("%s- Move %s%s\n", prefix,
		console_direction_name(punit->orders.list[order_index].dir),
		cursor);
      break;
    default:
      fc_printf("%s- %s%s\n", prefix,
		console_orders_name(punit->orders.list[order_index].order),
		cursor);
      break;
    }
  }
  if (punit->orders.length) {
    if (punit->orders.repeat) {
      fc_printf("%s- repeat\n", prefix);
    } else {
      fc_printf("%s- end\n", prefix);
    }
  }
  if (punit->goto_tile) {
    fc_printf("%s- goto (%d, %d)\n", prefix, TILE_XY(punit->goto_tile));
  }
}

void console_command(char const *s)
{
  const char SERVER_COMMAND_PREFIX = '/';
  struct command_handler const handlers[] = {
    { "endturn", &console_endturn, NULL },
    { "focus", &console_focus, NULL },
    { "goto", &console_goto, NULL },
    { "hover", &console_hover, NULL },
    { "lsc", &console_lsc, NULL },
    { "lsu", &console_lsu, NULL },
    { "fullmap", &console_fullmap, NULL },
    { "statu", &console_statu, NULL },
    { "n", &console_move, NULL },
    { "s", &console_move, NULL },
    { "w", &console_move, NULL },
    { "e", &console_move, NULL },
    { "nw", &console_move, NULL },
    { "ne", &console_move, NULL },
    { "sw", &console_move, NULL },
    { "se", &console_move, NULL },
    { NULL, NULL, NULL }
  };
  char **words;
  int i, n_words, words_available;

  if (s[0] == SERVER_COMMAND_PREFIX) {
    send_chat(s);
    return;
  }

  words_available = 1;
  words = fc_malloc(words_available * sizeof (*words));
  n_words = 0;

  do {
    if (n_words >= words_available) {
      words_available *= 2;
      words = fc_realloc(words, words_available * sizeof (*words));
    }
    words[n_words++] = next_word(&s, " \t");
  } while (words[n_words-1]);

  /* Back up to before the NULL, to give argc/argv semantics. */
  n_words--;

  if (n_words) {
    for (i = 0; handlers[i].command; i++) {
      if (mystrcasecmp(handlers[i].command, words[0]) == 0) {
	(*handlers[i].handler)(n_words, words, handlers[i].context);
	break;
      }
    }
  }

  for (i = 0; i < n_words; i++) {
    FC_FREE(words[i]);
  }

  FC_FREE(words);
}

void console_endturn(int argc, char *argv[], void *context)
{
  user_ended_turn();
}

void console_focus(int argc, char *argv[], void *context)
{
  void (*focus_fn)(struct unit *);

  if (argc < 2) {
    fc_printf("500 Usage: focus add|list|set ARGS...\n");
    return;
  }

  switch (argv[1][0]) {
  case 'a':
  case 'A':
    if (argc < 3) {
      fc_printf("500 Usage: focus add TILE|UNITID...\n");
      return;
    }
    focus_fn = &add_unit_focus;
    break;
  case 's':
  case 'S':
    if (argc < 3) {
      fc_printf("500 Usage: focus set TILE|UNITID...\n");
      return;
    }
    focus_fn = &set_unit_focus;
    break;
  case 'l':
  case 'L':
    break;
  default:
    fc_printf("500 Usage: focus add|list|set ARGS...\n");
    return;
  }

  /* FIXME: Find and use a helper to interpret abbreviations. */
  switch (argv[1][0]) {
    struct unit_list *units;
    struct unit *punit;
    int unit_id;
    int i;
  case 'a':
  case 'A':
  case 's':
  case 'S':
    units = unit_list_new();

    /* Check that all the named units/tiles exist. */
    for (i = 2; i < argc; i++) {
      struct tile *ptile;

      if (console_get_tile(argv[i], &ptile)) {
	if (!ptile) {
	  fc_printf("404 No known tile for \"%s\"\n", argv[i]);
	  unit_list_free(units);
	  return;
	}

	if (!unit_list_size(ptile->units)) {
	  fc_printf("404 No units at (%d, %d)\n", TILE_XY(ptile));
	  unit_list_free(units);
	  return;
	}

	unit_list_iterate(ptile->units, punit) {
	  unit_list_append(units, punit);
	} unit_list_iterate_end;
      } else {
	unit_id = atoi(argv[i]);
	punit = game_find_unit_by_number(unit_id);
	if (!punit) {
	  fc_printf("404 Unit %d not found\n", unit_id);
	  unit_list_free(units);
	  return;
	}
	unit_list_append(units, punit);
      }
    }

    /* Once we know they all exist, focus them. */
    unit_list_iterate(units, punit) {
      (*focus_fn)(punit);
      focus_fn = &add_unit_focus;
    } unit_list_iterate_end;

    unit_list_free(units);
    break;
  case 'l':
  case 'L':
    unit_list_iterate(get_units_in_focus(), punit) {
      console_unit_summary(punit, "250");
    } unit_list_iterate_end;
    fc_printf("250 focus list = %d\n", unit_list_size(get_units_in_focus()));
    break;
  }
}

void console_goto(int argc, char *argv[], void *context)
{
  struct tile *ptile;
  int n_units;

  if (argc < 2 || !console_get_tile(argv[1], &ptile)) {
    fc_printf("500 Usage: goto TILE\n");
    return;
  }

  if (!ptile) {
    fc_printf("404 No known tile for \"%s\"\n", argv[1]);
    return;
  }

  key_unit_goto();

  do_map_click(ptile, SELECT_APPEND);

  n_units = 0;
  unit_list_iterate(get_units_in_focus(), punit) {
    console_unit_orders(punit, "250");
    n_units++;
  } unit_list_iterate_end;
  fc_printf("250 %d units sent to (%d, %d)\n",
	    n_units, TILE_XY(ptile));
}

void console_hover(int argc, char *argv[], void *context)
{
  struct tile *ptile;

  if (argc < 2 || !console_get_tile(argv[1], &ptile)) {
    fc_printf("500 Usage: hover TILE\n");
    return;
  }

  if (!ptile) {
    fc_printf("404 No known tile for \"%s\"\n", argv[1]);
    return;
  }

  control_mouse_cursor(ptile);

  if (ptile->terrain) {
    fc_printf("250 Hover (%d, %d) %s\n",
	      TILE_XY(ptile), terrain_name_translation(ptile->terrain));
  } else {
    fc_printf("250 Hover (%d, %d) [Unknown]\n",
	      TILE_XY(ptile));
  }
}

void console_lsc(int argc, char *argv[], void *context)
{
  int n;

  n = 0;
  cities_iterate(pcity) {
    fc_printf("250- %d. %d (%d, %d) %s%s%s %s [%s]\n",
	      pcity->id, pcity->size, TILE_XY(pcity->tile),
	      pcity->client.walls ? "[" : "",
	      pcity->client.occupied ? "*" : "_",
	      pcity->client.walls ? "]" : "",
	      pcity->name, nation_plural_for_player(pcity->owner));
    n++;
  } cities_iterate_end;
  fc_printf("250 %d cities\n", n);
}

void console_lsu(int argc, char *argv[], void *context)
{
  int n_player, n_total;

  n_total = 0;
  players_iterate(pplayer) {
    n_player = 0;
    unit_list_iterate(pplayer->units, punit) {
      console_unit_summary(punit, "250");
      n_player++;
      n_total++;
    } unit_list_iterate_end;
    fc_printf("250- %d %s units\n", n_player, nation_adjective_for_player(pplayer));
  } players_iterate_end;
  fc_printf("250 %d total units\n", n_total);
}

void console_move(int argc, char *argv[], void *context)
{
  struct {
    char const *name;
    enum direction8 dir;
  } const directions[] = {
    { "n", DIR8_NORTH },
    { "s", DIR8_SOUTH },
    { "w", DIR8_WEST },
    { "e", DIR8_EAST },
    { "nw", DIR8_NORTHWEST },
    { "ne", DIR8_NORTHEAST },
    { "sw", DIR8_SOUTHWEST },
    { "se", DIR8_SOUTHEAST },
    { NULL, DIR8_LAST }
  };
  int i;

  for (i = 0; directions[i].name; i++) {
    if (mystrcasecmp(directions[i].name, argv[0]) == 0) {
      key_unit_move(map_to_gui_dir(directions[i].dir));
      return;
    }
  }

  fc_printf("500 No such direction: %s\n", argv[0]);
}

void console_fullmap(int argc, char *argv[], void *context)
{
  static char *pixels = NULL;
  static int max_x = 0, max_y = 0;
  const int rows_per_tile = 1, columns_per_tile = 1;
  size_t n_pixels;
  int x, y;

  if (!pixels) {
    whole_map_iterate(ptile) {
      if (ptile->x > max_x) {
	max_x = ptile->x;
      }
      if (ptile->y > max_y) {
	max_y = ptile->y;
      }
    } whole_map_iterate_end;
    fc_printf("250- Allocating pixels %d/%d %d/%d\n", max_x, map.xsize, max_y, map.ysize);
  }

  n_pixels = (max_x+1)*(max_y+1) * columns_per_tile*rows_per_tile;

  if (!pixels) {
    pixels = fc_malloc(n_pixels * sizeof (*pixels));
  }

  memset(pixels, ' ', n_pixels);

  whole_map_iterate(ptile) {
    char identifier = ptile->terrain ? terrain_name_translation(ptile->terrain)[0] : ' ';
    int xy = columns_per_tile*(max_x+1)*(rows_per_tile*ptile->y) + columns_per_tile*ptile->x + columns_per_tile/2;
    pixels[xy] = (identifier ? identifier : ' ');
  } whole_map_iterate_end;

  fc_printf("250-     ");
  for (x = 0; x < (max_x+1) * columns_per_tile; x++) {
    fc_printf("%01d", x % 10);
  }
  fc_printf("\n");
  for (y = 0; y < (max_y+1) * rows_per_tile; y++) {
    fc_printf("250- %3d %.*s\n", y/rows_per_tile, columns_per_tile*(max_x+1), pixels + columns_per_tile*(max_x+1)*y);
  }
  fc_printf("250-     ");
  for (x = 0; x < (max_x+1) * columns_per_tile; x++) {
    fc_printf("%01d", x % 10);
  }
  fc_printf("\n");
  fc_printf("250 fullmap\n");
}

void console_statu(int argc, char *argv[], void *context)
{
  struct unit *punit;
  char const *activity_name;
  int id;

  if (argc < 2) {
    fc_printf("500 statu needs unit ID\n");
    return;
  }

  id = atoi(argv[1]);
  punit = game_find_unit_by_number(id);
  if (!punit) {
    fc_printf("404 Unit %d not found\n", id);
    return;
  }

  fc_printf("250- (%d, %d) %s (%s) %d/%d A%d D%d H%d/%d F%d [%s]\n",
	    TILE_XY(punit->tile),
	    unit_name_translation(punit),
	    punit->utype->veteran[punit->veteran].name,
	    punit->moves_left, punit->utype->move_rate,
	    punit->utype->attack_strength,
	    punit->utype->defense_strength,
	    punit->hp, punit->utype->hp,
	    punit->utype->firepower,
	    nation_plural_for_player(punit->owner));
  switch (punit->activity) {
  case ACTIVITY_IDLE:
    break;
  default:
    activity_name = console_activity_name(punit->activity);
    if (activity_name) {
      fc_printf("250- %s/%d\n", activity_name, punit->activity_count);
    }
    break;
  }
  console_unit_orders(punit, "250");
  fc_printf("250 statu %d\n", id);
}

/****************************************************************************
  Called by the tileset code to set the font size that should be used to
  draw the city names and productions.
****************************************************************************/
void set_city_names_font_sizes(int my_city_names_font_size,
			       int my_city_productions_font_size)
{
}

/**************************************************************************
  Do any necessary pre-initialization of the UI, if necessary.
**************************************************************************/
void ui_init(void)
{
}

/**************************************************************************
  Entry point for whole freeciv client program.
**************************************************************************/
int main(int argc, char **argv)
{
  return client_main(argc, argv);
}

/**************************************************************************
  The main loop for the UI.  This is called from main(), and when it
  exits the client will exit.
**************************************************************************/
void ui_main(int argc, char *argv[])
{
  fd_set rfds, wfds;
  struct timeval timeout;
  double seconds;
  int stdin_flags;

  tileset_init(tileset);
  tileset_load_tiles(tileset);

  fc_fprintf(stderr, "Freeciv rules!\n");
  set_client_state(C_S_DISCONNECTED);

  stdin_flags = fcntl(0, F_GETFL);
  if (stdin_flags == -1) {
    freelog(LOG_ERROR, "Couldn't get stdin file flags. It may still be in blocking mode.");
  } else if (!(stdin_flags & O_NONBLOCK)) {
#if 0
    if (fcntl(0, F_SETFL, stdin_flags | O_NONBLOCK) == -1) {
      freelog(LOG_ERROR, "Couldn't set stdin to non-blocking mode.");
    }
#endif
  }

  while (1) {
    int max_fd = 0;
    int n_ready;

    MY_FD_ZERO(&rfds);
    MY_FD_ZERO(&wfds);
    FD_SET(0, &rfds);
    if (sockets.server != -1) {
      FD_SET(sockets.server, &rfds);
      max_fd = sockets.server;
      if (sockets.server_writable) {
	FD_SET(sockets.server, &wfds);
      }
    }

    /* TODO: Poll sockets and tty to see if we're really idle. */
    while (idle_callbacks) {
      struct idle_callback *idle = idle_callbacks;

      idle_callbacks = idle->next;
      (idle->callback.fn)(idle->callback.context);
      FC_FREE(idle);
    }

    seconds = real_timer_callback();
    timeout.tv_sec = (long) seconds;
    timeout.tv_usec = fmod(seconds, 1) * 1000000UL;

    n_ready = fc_select(max_fd + 1, &rfds, &wfds, NULL, &timeout);
    if (n_ready > 0) {
      if (FD_ISSET(sockets.server, &rfds)) {
	input_from_server(sockets.server);
      }
      if (FD_ISSET(0, &rfds)) {
	static char *buf = NULL;
	static size_t buf_used = 0, buf_size = 0;
	ssize_t n_read;
	char *newline;

	/* Make room for more console input. */
	if (buf == NULL || buf_used >= buf_size) {
	  size_t newsize = buf_size * 2 + 4096;
	  buf = fc_realloc(buf, newsize);
	  buf_size = newsize;
	}

	n_read = read(0, buf + buf_used, buf_size - buf_used);
	switch (n_read) {
	case -1:
	  freelog(LOG_ERROR, "Couldn't read from stdin.");
	  client_exit();
	  break;
	case 0:
	  tileset_free_tiles(tileset);
	  client_exit();
	  break;
	default:
	  buf_used += n_read;
	  break;
	}

	newline = strchr(buf, '\n');
	if (newline) {
	  size_t line_len = newline+1 - buf;

	  *newline = 0;
	  console_command(buf);
	  memmove(buf, buf + line_len, buf_used - line_len);
	  buf_used -= line_len;
	}
      }
    }
  }
}

/****************************************************************************
  Extra initializers for client options.
****************************************************************************/
void gui_options_extra_init(void)
{
  /* Nothing to do. */
}

/**************************************************************************
  Do any necessary UI-specific cleanup
**************************************************************************/
void ui_exit()
{
  /* PORTME */
}

/**************************************************************************
  Return our GUI type
**************************************************************************/
enum gui_type get_gui_type(void)
{
  return GUI_CONSOLE;
}

/**************************************************************************
 Update the connected users list at pregame state.
**************************************************************************/
void update_conn_list_dialog(void)
{
  /* PORTME */
}

/**************************************************************************
  Make a bell noise (beep).  This provides low-level sound alerts even
  if there is no real sound support.
**************************************************************************/
void sound_bell(void)
{
  /* PORTME */
}

static void set_wait_for_writable_socket(struct connection *pc,
					 bool socket_writable)
{
  sockets.server_writable = socket_writable;
}
/**************************************************************************
  Wait for data on the given socket.  Call input_from_server() when data
  is ready to be read.

  This function is called after the client succesfully has connected
  to the server.
**************************************************************************/
void add_net_input(int sock)
{
  sockets.server = sock;
  client.conn.notify_of_writable_data = set_wait_for_writable_socket;
}

/**************************************************************************
  Stop waiting for any server network data.  See add_net_input().

  This function is called if the client disconnects from the server.
**************************************************************************/
void remove_net_input(void)
{
  sockets.server = -1;
}

/**************************************************************************
  Called to monitor a GGZ socket.
**************************************************************************/
void add_ggz_input(int sock)
{
  /* PORTME */
}

/**************************************************************************
  Called on disconnection to remove monitoring on the GGZ socket.  Only
  call this if we're actually in GGZ mode.
**************************************************************************/
void remove_ggz_input(void)
{
  /* PORTME */
}

/**************************************************************************
  Set one of the unit icons (specified by idx) in the information area
  based on punit.

  punit is the unit the information should be taken from. Use NULL to
  clear the icon.

  idx specified which icon should be modified. Use idx==-1 to indicate
  the icon for the active unit. Or idx in [0..num_units_below-1] for
  secondary (inactive) units on the same tile.
**************************************************************************/
void set_unit_icon(int idx, struct unit *punit)
{
  /* PORTME */
}

/**************************************************************************
  Most clients use an arrow (e.g., sprites.right_arrow) to indicate when
  the units_below will not fit. This function is called to activate or
  deactivate the arrow.

  Is disabled by default.
**************************************************************************/
void set_unit_icons_more_arrow(bool onoff)
{
  /* PORTME */
}

/****************************************************************************
  Enqueue a callback to be called during an idle moment.  The 'callback'
  function should be called sometimes soon, and passed the 'data' pointer
  as its data.
****************************************************************************/
void add_idle_callback(void (callback)(void *), void *data)
{
  struct idle_callback *idle = fc_malloc(sizeof (*idle));

  idle->callback.fn = callback;
  idle->callback.context = data;
  idle->next = idle_callbacks;
  idle_callbacks = idle;
}

/****************************************************************************
  Stub for editor function
****************************************************************************/
void editgui_tileset_changed(void)
{}

/****************************************************************************
  Stub for editor function
****************************************************************************/
void editgui_refresh(void)
{}

/****************************************************************************
  Stub for editor function
****************************************************************************/
void editgui_popup_properties(const struct tile_list *tiles, int objtype)
{}

/****************************************************************************
  Stub for editor function
****************************************************************************/
void editgui_popdown_all(void)
{}

/****************************************************************************
  Stub for editor function
****************************************************************************/
void editgui_notify_object_changed(int objtype, int object_id, bool remove)
{}

/****************************************************************************
  Stub for editor function
****************************************************************************/
void editgui_notify_object_created(int tag, int id)
{}

/****************************************************************************
  Stub for ggz function
****************************************************************************/
void gui_ggz_embed_leave_table(void)
{}

/****************************************************************************
  Stub for ggz function
****************************************************************************/
void gui_ggz_embed_ensure_server(void)
{}


/**************************************************************************
  Updates a gui font style.
**************************************************************************/
void gui_update_font(const char *font_name, const char *font_value)
{
  /* PORTME */
}
