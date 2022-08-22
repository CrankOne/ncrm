/* Copyright (C) 2022, Renat R. Dusaev
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef H_NCRM_DEFINITIONS_H
#define H_NCRM_DEFINITIONS_H

#include <curses.h>

#define ncrm_for_every_colorpair(m, ...) \
    m(1, COLOR_GREEN,             -1, __VA_ARGS__) \
    m(2, COLOR_BLUE,              -1, __VA_ARGS__) \
    m(3, COLOR_WHITE,   COLOR_YELLOW, __VA_ARGS__) \
    m(4, COLOR_RED,      COLOR_WHITE, __VA_ARGS__) \
    /* ... */

/* These values are used by syslog(3) and log4cpp (number * 100) */
#define ncrm_for_every_special_attribute(m, ...) \
    m( 0, 'E',  EMERG,     "fatal", A_BOLD | COLOR_PAIR(4) | A_REVERSE, __VA_ARGS__ ) \
    m( 1, '!',  ALERT,     "alert", A_BOLD | COLOR_PAIR(4) | A_BLINK, __VA_ARGS__ ) \
    m( 2, 'E',   CRIT,  "critical", A_BOLD | COLOR_PAIR(4), __VA_ARGS__ ) \
    m( 3, 'e',  ERROR,     "error", A_BOLD | COLOR_PAIR(4), __VA_ARGS__ ) \
    m( 4, 'w',   WARN,   "warning", A_BOLD | COLOR_PAIR(3), __VA_ARGS__ ) \
    m( 5, '!', NOTICE,    "notice", COLOR_PAIR(2) | A_REVERSE | A_BOLD, __VA_ARGS__ ) \
    m( 6, 'i',   INFO,      "info", COLOR_PAIR(2), __VA_ARGS__ ) \
    m( 7, 'D',  DEBUG,     "debug", A_DIM | A_REVERSE | COLOR_PAIR(2), __VA_ARGS__ ) \
    m( 8, '?', NOTSET,    "notset", A_NORMAL | A_REVERSE, __VA_ARGS__ ) \
    /* ... */

extern const attr_t gSpecialAttrs[];
extern const int gNSpecialAttrs;

#endif  /* H_NCRM_DEFINITIONS_H */
