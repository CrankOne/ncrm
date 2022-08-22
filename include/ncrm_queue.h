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

#ifndef H_NCRM_QUEUE_H
#define H_NCRM_QUEUE_H

/**\file
 * \brief Defines event queue objects and API of ncrm. */

/** Max size of the event queue */
#define NCRM_MAX_EVENTS_IN_QUEUE 1024

enum ncrm_EventType {
    ncrm_kEventUnknown = 0x0,
    ncrm_kEventIncrementUpdateCount = 0x1,
    ncrm_kEventKeypress = 0x2,
    ncrm_kEventExtension = 0x3,
    ncrm_kEventHeaderUpdate = 0x4,
    ncrm_kEventFooterUpdate = 0x5,
};

/**\brief Representation of asynchroneous event (subject of event queue) */
struct ncrm_Event {
    /* Codes:
     *  0x0  -- unidentified (reserved)
     *  0x1  -- increment update count
     *  0x2  -- keypress
     *  0x3  -- to be dispatched to an extension
     *  0x4  -- header update
     *  0x5  -- footer update
     *  ...
     * */
    enum ncrm_EventType type;
    // ...
    union Payload {
        unsigned int keycode;
        struct {
            char extensionName[16];
            void * data;
        } forExtension;
    } payload;
};

/** Initializes global internal event queue object */
void ncrm_queue_init();
/** Properly releases event queue object */
void ncrm_queue_free();

/** Adds event to queue; event instance is copied once call (ownership not
 * transferred). */
int ncrm_enqueue( struct ncrm_Event * );
/** For non-empty queue, a callback will be called for each event (with user
 * data provided). Event queue is cleared. */
int ncrm_do_with_events( void(*)(struct ncrm_Event *, void*), void *);

#endif  /* H_NCRM_QUEUE_H */
