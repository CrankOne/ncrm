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

