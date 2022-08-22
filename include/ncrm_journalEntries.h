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

#ifndef H_NCRM_MONITOR_JOURNAL_ENTRIES_H
#define H_NCRM_MONITOR_JOURNAL_ENTRIES_H

/**\file
 *\brief Contains declarations related to steering of the logging journal shown.

 * NCRM app maintains a small in-memory storage for log messages with
 * some basic querying capabilities (filtering by timestamp, categories,
 * wildcard, priority, etc). This file defines common routines to manage and
 * query this storage.
 *
 * The API is built around a single journal entry (a message,
 * `ncrm_JournalEntry`) that has common properties: message text, its
 * priority (aka level, severity, etc), a timestamp and a category label.
 *
 * This messages are then grouped into blocks (of arbitrary size), contiguous
 * in memory -- to save some performance while retrieving and sorting them at a
 * runtime. This is rather an internal type, `ncrm_JournalEntries` usually
 * existing as a single-linked list.
 * */

#include <stdint.h>

/** Reallocation stride in case of new block needed */
#define NCRM_NENTRIES_INC 1024
/** Size of static destination buffer to recieve the entries */
#define NCRM_JOURNAL_MAX_BUFFER_LENGTH (5*1024*1024)
/** Name of extension */
#define NCRM_JOURNAL_EXTENSION_NAME "log"
/** Maximum log entries shown in window */
#define NCRM_JOURNAL_MAX_LINES_SHOWN 256
/** Max length of timestamp string */
#define NCRM_JOURNAL_MAX_TIMESTAMP_LEN 64
/** Max length of a single message shown in window */
#define NCRM_JOURNAL_MAX_LEN (5*1024)

/** A journal message timestamp type */
typedef unsigned long ncrm_Timestamp_t;
/** A journal message level type */
typedef int ncrm_JournalEntryLevel_t;

/**\brief Represents single journal entry (a log message) */
struct ncrm_JournalEntry {
    /** Timestamp of the message */
    ncrm_Timestamp_t timest;
    /** Level (severity) of the message (debug, warning, error, etc) */
    ncrm_JournalEntryLevel_t level;
    /** Message category (functional block, affiliation) */
    char * category;
    /** Text of the message */
    char * message;
};

/** Returns non-zero code if given entry is all-null */
int
ncrm_je_is_terminative_entry(const struct ncrm_JournalEntry *);

/** Marks entry as terminative */
void
ncrm_je_mark_as_terminative(struct ncrm_JournalEntry *);

/** Returns number of entries in an array assuming it is ended with terminative
 * entry */
unsigned long
ncrm_je_len(const struct ncrm_JournalEntry *);

/**\brief Single-linked list representing collection of journal entries */
struct ncrm_JournalEntries {
    /** Journal entries block maintained by this entry. Terminated with entry
     * of all-null fields (`ncrm_is_null_entry()`) */
    struct ncrm_JournalEntry * entries;
    /** Pointer to next element in a list */
    struct ncrm_JournalEntries * next;
};

/**\brief Adds block of journal entries to collection
 *
 * Prepends the list with block and returns pointer to new head. Sorts the
 * messages within added block. Optionally sorts blocks in a list if detects
 * intersection between blocks -- it is not guaranteed that returned ptr will
 * necessarily contain the `newBlock` ptr. May re-allocate blocks, freeing the
 * pointers, so it is assumed that all the blocks were allocated
 * with `malloc()`.
 *
 * Two blocks with intersecting ranges will be merged into new one.
 * */
struct ncrm_JournalEntries *
ncrm_je_append( struct ncrm_JournalEntries * dest
              , struct ncrm_JournalEntry * newBlock );

/**\brief Helper function that invokes a callback on every journal entry within
 *        all the blocks
 *
 * Note, that sequence of iterated entries will be in somewhat mixed order:
 * blocks are usually at descending order (recent items with higher timestamps
 * go first), entries are ascending.
 *
 * \param src[in] a journal entries set to iterate over
 * \param callback[in] a callback function to call; shall return non-zero to stop
 * \param userData[in] a supplementary data to be provided to a callback
 * */
unsigned long
ncrm_je_iterate( struct ncrm_JournalEntries * src
               , int (*callback)(struct ncrm_JournalEntry *, void *)
               , void * userData
               );


struct ncrm_QueryParams {
    /** Pattern to filter for certain category(-ies) */
    char * categoryPatern;
    /** Pattern to filter for certain message */
    char * msgPattern;
    /** Range of logging levels to filter */
    ncrm_JournalEntryLevel_t levelRange[2];
    /** Time range */
    ncrm_Timestamp_t timeRange[2];
};

/**\brief Applies filters to journal entries blocks selecting entries that
 *        match criteria.
 *
 * Note, that `dest` will be set to `malloc()`d ptr if at least one entry is
 * found, so one has to `free()` it to avoid memleaks. If none entries found,
 * however, `dest` will be set to null pointer.
 *
 * Returned is the number of entries found.
 *
 * \param src[in] source subset of journal entries
 * \param qp[in] struct describing query parameters
 * \param dest[out] destination ptr
 * */
unsigned long
ncrm_je_query( const struct ncrm_JournalEntries * src
             , const struct ncrm_QueryParams * qp
             , struct ncrm_JournalEntry *** dest
             );

/**\brief Timestamp formatting settings */
struct ncrm_JournalTimestampFormat {
    /** Shall format timestamp string for given entry `entry`, write the string
     * to `dest` and return count of meaningful bytes (zero termination is not
     * necessary).*/
    uint16_t (*callback)( struct ncrm_JournalTimestampFormat *
                        , char * dest
                        , ncrm_Timestamp_t //const struct ncrm_JournalEntry * entry
                        );
    // ...
};


struct ncrm_Extension;

struct ncrm_JournalExtensionConfig {
    /** Pointer to model config */
    struct ncrm_Model * modelPtr;
    /** Address of the socket to connect to (0MQ SUB)
     * Example: "tcp://127.0.0.1:5555" */
    char * address;
    /** Check updates unce per msec (zero for blocking recv) */
    unsigned int recvIntervalMSec;
    /** Default (starting) query parameters for new view */
    struct ncrm_QueryParams defaultQueryParameters;
    /** Default (starting) timestamp formatter settings */
    struct ncrm_JournalTimestampFormat defaultTimestampFormatter;
    /** Dimensions (set automatically at extension initialization, updated
     * by resize event)*/
    uint16_t dims[2][2];
};

extern struct ncrm_Extension gJournalExtension;

#endif /* H_NCRM_MONITOR_JOURNAL_ENTRIES_H */

