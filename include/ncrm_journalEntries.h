#ifndef H_NCRM_MONITOR_JOURNAL_ENTRIES_H
#define H_NCRM_MONITOR_JOURNAL_ENTRIES_H

/**\file
 *\brief Contains declarations related to steering of the logging journal shown.

 * NA64SW-Monitor app maintains a small in-memory storage for log messages with
 * some basic querying capabilities (filtering by timestamp, categories,
 * wildcard, priority, etc). This file defines common routines to manage and
 * query this storage.
 *
 * The API is built around a single journal entry (a message,
 * `ncrm_JournalEntry`) that has common properties: message text, its
 * priority (aka level, severity, etc), a timestamp and a category label.
 *
 * This messages are then united into blocks (of arbitrary size), contiguous in
 * memory -- to save some performance while retrieving and sorting them at a
 * runtime. This is rather an internal type, `ncrm_JournalEntries` usually
 * existing as a single-linked list.
 * */

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
 * \param callback[in] a callback function to call
 * \param userData[in] a supplementary data to be provided to a callback
 * */
unsigned long
ncrm_je_iterate( struct ncrm_JournalEntries * src
                  , void (*callback)(struct ncrm_JournalEntry *, void *)
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

#endif /* H_NCRM_MONITOR_JOURNAL_ENTRIES_H */

