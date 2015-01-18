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

#ifndef FC__GUI_MAIN_H
#define FC__GUI_MAIN_H

#include "gui_main_g.h"

char *next_word(char const **s, char const *ifs);
void console_command(char const *s);

void console_city(int argc, char *argv[], void *context);
void console_endturn(int argc, char *argv[], void *context);
void console_focus(int argc, char *argv[], void *context);
void console_goto(int argc, char *argv[], void *context);
void console_hover(int argc, char *argv[], void *context);
void console_lsc(int argc, char *argv[], void *context);
void console_lsu(int argc, char *argv[], void *context);
void console_move(int argc, char *argv[], void *context);
void console_politics(int argc, char *argv[], void *context);
void console_quit(int argc, char *argv[], void *context);
void console_fullmap(int argc, char *argv[], void *context);
void console_statc(int argc, char *argv[], void *context);
void console_statu(int argc, char *argv[], void *context);

#endif				/* FC__GUI_MAIN_H */
