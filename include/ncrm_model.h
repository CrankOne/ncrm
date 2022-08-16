#ifndef H_NCRM_MONITOR_MODEL_H
#define H_NCRM_MONITOR_MODEL_H

#include "ncrm_journalEntries.h"

#include <pthread.h>

/**\brief Information about monitored process, updated periodically
 * */
struct ncrm_Model {
    /** Guards data modification */
    //pthread_mutex_t lock;

    /** Collected journal entries */
    struct ncrm_JournalEntries * journalEntries;
    /** Current progress */
    unsigned long currentProgress;
    /** Max progress */
    unsigned long maxProgress;
    /** Elapsed time in msec */
    unsigned long elapsedTime;
    /** Application status */
    char * statusMessage;
    /** Special field to indicate color pair to display status. Usual meanings:
     *  0 - normal mode
     *  1 - disconnected / darmant / idle
     *  2 - warning
     *  3 - error
     *  4 - requires immediate attention / fatal error
     * */
    int statusMode;
};



#endif  /* H_NCRM_MONITOR_MODEL_H */
