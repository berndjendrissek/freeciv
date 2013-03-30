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
#include "log.h"
#include "netintf.h"

/* client */
#include "chatline_common.h"
#include "client_main.h"
#include "clinet.h"
#include "editgui_g.h"
#include "ggz_g.h"
#include "options.h"

#include "gui_main.h"

struct closure {
  void (*fn)(void *);
  void *context;
};

struct idle_callback {
  struct closure callback;
  struct idle_callback *next;
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

  fc_fprintf(stderr, "Freeciv rules!\n");
  set_client_state(C_S_DISCONNECTED);

  stdin_flags = fcntl(0, F_GETFL);
  if (stdin_flags == -1) {
    freelog(LOG_ERROR, "Couldn't get stdin file flags. It may still be in blocking mode.");
  } else if (!(stdin_flags & O_NONBLOCK)) {
    if (fcntl(0, F_SETFL, stdin_flags | O_NONBLOCK) == -1) {
      freelog(LOG_ERROR, "Couldn't set stdin to non-blocking mode.");
    }
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
	  send_chat(buf);
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
