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

#include "ncrm_model.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

void
ncrm_mdl_error( struct ncrm_Model * mdl, const char * newErr ) {
    assert(newErr);
    pthread_mutex_lock(&mdl->lock);
    /* Count current number of errors */
    int nErrors = 1;
    for( char ** cErr = mdl->errors
       ; cErr && *cErr
       ; ++cErr, ++nErrors ) {}
    assert(nErrors);
    --nErrors;
    if( 0 == nErrors ) {
        mdl->errors = malloc(2*sizeof(char*));
    } else {
        mdl->errors = realloc(mdl->errors, (nErrors + 2)*sizeof(char*));
    }
    mdl->errors[nErrors] = strdup(newErr);
    mdl->errors[nErrors + 1] = NULL;
    pthread_mutex_unlock(&mdl->lock);
}

