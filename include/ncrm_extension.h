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

#ifndef H_NCRM_EXTENSION_H
#define H_NCRM_EXTENSION_H

#include <stdint.h>

struct ncrm_Event;

/**\brief Represents extension of monitoring app.
 *
 * Extension are shown as switchable tabs composed in multiple windows+panels.
 * Besides of this, extension some lifetime logic (possibly asynchronious):
 *  1. init() -- allocates resources based on app's configuration
 *  2. redraw() -- shall update content of the windows/panels
 *  3. shutdown() -- frees resources at the end of lifetime
 * */
struct ncrm_Extension {
    /** An unique name to be shown in header as a tab name */
    char * name;

    /** A key switch to be used in combination with <ctrl> to switch to the
     * tab of this extension. */
    char keyswitch;

    /** Own extension's data */
    void * userData;

    /** Invoked at startup. May create listener thread's, allocate datas, etc. */
    int (*init)( struct ncrm_Extension *, struct ncrm_Model *
               , uint16_t top, uint16_t left, uint16_t nLines, uint16_t nCols );
    /** Invoked to update GUI content of a tab */
    int (*update)(struct ncrm_Extension *, struct ncrm_Event *);
    /** Invoked at application shutdown */
    int (*shutdown)(struct ncrm_Extension *);
};

#endif  /* H_NCRM_EXTENSION_H */
