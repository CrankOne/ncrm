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

#include "ncrm_defs.h"

/* Array of special attributes */
const attr_t gSpecialAttrs[] = { A_NORMAL /* 0 - Normal mode */
    , A_DIM  /* 1 - disconnected / darmant / idle / not important */
    , A_BOLD | COLOR_PAIR(1) /* 2 - warning */
    , A_BOLD | COLOR_PAIR(2) /* 3 - error */
    , A_BLINK  /* 4 - requires (immediate) attention / fatal error */
};

const int gNSpecialAttrs = sizeof(gSpecialAttrs)/sizeof(attr_t);

