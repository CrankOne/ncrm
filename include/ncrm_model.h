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

#ifndef H_NCRM_MONITOR_MODEL_H
#define H_NCRM_MONITOR_MODEL_H

#include "ncrm_defs.h"

#include <pthread.h>

/**\brief Information about monitored process, updated periodically
 * */
struct ncrm_Model {
    /** Guards data modification */
    pthread_mutex_t lock;

    /** Current progress */
    unsigned long currentProgress;
    /** Max progress */
    unsigned long maxProgress;
    /** Elapsed time in msec */
    unsigned long elapsedTime;
    /** Service message */
    char serviceMsg[64];
    /** Application status */
    char appMsg[64];
    /** Special field to indicate color pair to display status. Usual meanings:
     *  0 - normal mode
     *  1 - disconnected / darmant / idle
     *  2 - warning
     *  3 - error
     *  4 - requires immediate attention / fatal error
     * */
    int statusMode;

    /** Null-terminated array of errors occured */
    char ** errors;
};

/** Adds an error to the list of errors */
void ncrm_mdl_error( struct ncrm_Model *, const char * );

#endif  /* H_NCRM_MONITOR_MODEL_H */
