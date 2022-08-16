#include "ncrm_journalEntries.h"

#include <assert.h>
#include <string.h>
#include <fnmatch.h>
#include <stdlib.h>
#include <limits.h>

#define NCRM_NENTRIES_INC 1024

int
ncrm_je_is_terminative_entry(const struct ncrm_JournalEntry * jePtr) {
    return ( 0 == jePtr->timest
          && 0 == jePtr->level
          && NULL == jePtr->category
          && NULL == jePtr->message ) ? 1 : 0;
}

void
ncrm_je_mark_as_terminative(struct ncrm_JournalEntry * je) {
    je->timest = 0;
    je->level = 0;
    je->category = NULL;
    je->message = NULL;
}

unsigned long
ncrm_je_len(const struct ncrm_JournalEntry * jes) {
    const struct ncrm_JournalEntry * je = jes;
    unsigned long n;
    for( n = 0; !ncrm_je_is_terminative_entry(je); ++je, ++n ) {}
    return n;
}

/* compat with 3p `qsort()` */
static int
_compare_journal_entries(const void * a_, const void * b_) {
    const struct ncrm_JournalEntry * a = (const struct ncrm_JournalEntry *) a_
                                    , * b = (const struct ncrm_JournalEntry *) b_
                                    ;
    if( a->timest < b->timest ) return -1;
    if( a->timest > b->timest ) return  1;
    return 0;
}

struct ncrm_JournalEntries *
ncrm_je_append( struct ncrm_JournalEntries * dest
                 , struct ncrm_JournalEntry * newBlock ) {
    unsigned long newBlockSize;
    int merged;
    newBlockSize = ncrm_je_len(newBlock);
    do {
        merged = 0;
        /* Sort messages within the given block by time, ascending */
        qsort( newBlock, newBlockSize
             , sizeof(struct ncrm_JournalEntry)
             , _compare_journal_entries
             );
        if(!dest) break;
        /* Compare last element in a list (latest stored message) and first element
         * of a block (oldest recieved) to identify, whether blocks have to be
         * merged. Keep merging until there is at least one range intersection */

        /* Get the latest element in a list */
        struct ncrm_JournalEntry * lastStored;
        unsigned long lastStoredCount = 0;
        for( lastStored = dest->entries
           ; !ncrm_je_is_terminative_entry(lastStored)
           ; ++lastStored, ++lastStoredCount ) {}
        --lastStored;

        if( _compare_journal_entries(lastStored, newBlock) > 0 ) {
            /* current block contains message older than latest in stored
             * list => need to merge. */
            struct ncrm_JournalEntry * mergedBlock
                = malloc( sizeof(struct ncrm_JournalEntry)
                        * (newBlockSize + lastStoredCount + 1)
                        );
            memcpy( mergedBlock, dest->entries
                  , sizeof(struct ncrm_JournalEntry)*lastStoredCount
                  );
            memcpy( mergedBlock + lastStoredCount, newBlock
                  , sizeof(struct ncrm_JournalEntry)*newBlockSize
                  );
            newBlockSize = newBlockSize + lastStoredCount;
            ncrm_je_mark_as_terminative(mergedBlock + newBlockSize);
            free(newBlock);
            newBlock = mergedBlock;
            merged = 1;
            struct ncrm_JournalEntries * prevDest = dest;
            dest = dest->next;
            free(prevDest);
            continue;
        }
    } while(merged);  /* keep until no merge */

    struct ncrm_JournalEntries * newHead
        = malloc(sizeof(struct ncrm_JournalEntries));
    newHead->entries = newBlock;
    newHead->next = dest;
    return newHead;
}

unsigned long
ncrm_je_iterate( struct ncrm_JournalEntries * src
                  , void (*callback)(struct ncrm_JournalEntry *, void *)
                  , void * userData
                  ) {
    unsigned long count = 0;
    for( struct ncrm_JournalEntries * listElement = src
       ; listElement
       ; listElement = listElement->next ) {
        assert(listElement->entries);
        for( struct ncrm_JournalEntry * jePtr = listElement->entries
           ; !ncrm_je_is_terminative_entry(jePtr)
           ; ++jePtr, ++count
           ) {
            callback(jePtr, userData);
        }
    }
    return count;
}

/** (internal) struct collecting the journal entries */
struct QueryCollector {
    const struct ncrm_QueryParams * qp;
    struct ncrm_JournalEntry ** collectedEntries;
    unsigned long nCollected, nAllocated;
};

/** (internal) callback */
static void _collect_entry_if_matches( struct ncrm_JournalEntry * je, void * qcPtr ) {
    struct QueryCollector * collector = (struct QueryCollector *) qcPtr;
    /* filter by level */
    if( collector->qp->levelRange[0] > -1 ) {
        if( je->level < collector->qp->levelRange[0] ) {
            return;
        }
    }
    if( collector->qp->levelRange[1] > -1 ) {
        if( je->level > collector->qp->levelRange[1] ) {
            return;
        }
    }
    /* filter by category pattern */
    if( collector->qp->categoryPatern ) {
        if( fnmatch( collector->qp->categoryPatern
                   , je->category
                   , 0x0 /*FNM_CASEFOLD | FNM_EXTMATCH*/
                   ) ) {
            return;
        }
    }
    /* filter by message substring */
    if( collector->qp->msgPattern ) {
        if( fnmatch( collector->qp->msgPattern
                   , je->message
                   , 0x0 /*FNM_CASEFOLD | FNM_EXTMATCH*/
                   ) ) {
            return;
        }
    }
    /* filter by time range */
    if( collector->qp->timeRange[0] != ULONG_MAX ) {
        if( je->timest < collector->qp->timeRange[0] ) {
            return;
        }
    }
    if( collector->qp->timeRange[1] != ULONG_MAX ) {
        if( je->timest > collector->qp->timeRange[1] ) {
            return;
        }
    }
    /* ok, the entry passed checks, => collect it, possibly re-allocating */
    if( collector->nCollected == collector->nAllocated ) {
        /* (re)allocate */
        collector->nAllocated += NCRM_NENTRIES_INC;
        struct ncrm_JournalEntry ** newJEs
            = malloc((collector->nAllocated)*sizeof(struct ncrm_JournalEntry *));
        if( collector->collectedEntries ) {
            /* swap and free old */
            memcpy( newJEs, collector->collectedEntries
                  , (collector->nCollected)*sizeof(struct ncrm_JournalEntry *)
                  );
            free(collector->collectedEntries);
        }
        collector->collectedEntries = newJEs;
    }
    assert( collector->nCollected < collector->nAllocated );
    assert( je );
    collector->collectedEntries[(collector->nCollected)++] = je;
}

static int
_compare_journal_entries_ptrs(const void * a_, const void * b_) {
    const struct ncrm_JournalEntry * a = *((const struct ncrm_JournalEntry **) a_)
                                    , * b = *((const struct ncrm_JournalEntry **) b_)
                                    ;
    return _compare_journal_entries(a, b);
}

unsigned long
ncrm_je_query( const struct ncrm_JournalEntries * src
                , const struct ncrm_QueryParams * qp
                , struct ncrm_JournalEntry *** dest
                ) {
    struct QueryCollector qc = {qp, NULL, 0, 0};
    ncrm_je_iterate( (struct ncrm_JournalEntries *) src
                      , _collect_entry_if_matches
                      , &qc
                      );
    *dest = qc.collectedEntries;
    if( qc.nCollected )
        qsort( qc.collectedEntries
             , qc.nCollected
             , sizeof(struct ncrm_JournalEntries *)
             , _compare_journal_entries_ptrs
             );
    return qc.nCollected;
}

#if 0
static void
_print_journal_entry( struct ncrm_JournalEntry * je, void * _) {
    assert(je);
    printf( " %lu : %d // %s // %s\n"
          , je->timest
          , je->level
          , je->category
          , je->message
          );
}

int
main(int argc, char * argv[]) {
    /* Allocate blocks to keep 9 mock messages */
    const int nEntriesPerBlock[] = {1, 2, 3, 1, 2};
    struct ncrm_JournalEntry ** blocks
        = malloc(sizeof(nEntriesPerBlock)*sizeof(struct ncrm_JournalEntry *)/sizeof(int));
    for( int i = 0; i < sizeof(nEntriesPerBlock)/sizeof(int); ++i ) {
        blocks[i] = malloc(sizeof(struct ncrm_JournalEntry)*(nEntriesPerBlock[i]+1));
        /* put sentinel message */
        ncrm_je_mark_as_terminative(blocks[i] + nEntriesPerBlock[i]);
    }

    /* Read mock messages and compose journal logs */
    char * line = NULL;
    size_t len;

    FILE * fp = fopen("j.txt", "r");
    int cBlock = 0, cMsg = 0;
    while(-1 != getline(&line, &len, fp)) {
        struct ncrm_JournalEntry newEntry;
        newEntry.timest = atoi(line);
        getline(&line, &len, fp);
        newEntry.level = atoi(line);
        getline(&line, &len, fp);
        newEntry.category = strndup(line, strlen(line) - 1);
        getline(&line, &len, fp);
        newEntry.message = strndup(line, strlen(line) - 1);
        getline(&line, &len, fp);  /* (empty line) */
        /* add entry to the block */
        if(cMsg == nEntriesPerBlock[cBlock]) {
            cMsg = 0;
            ++cBlock;
        }
        //printf(" -> to %d:%d :: %s\n", cBlock, cMsg, newEntry.message);
        blocks[cBlock][cMsg] = newEntry;
        ++cMsg;
    }

    /* Build mock journal */
    struct ncrm_JournalEntries * j = NULL;
    for( int i = 0; i < sizeof(nEntriesPerBlock)/sizeof(int); ++i ) {
        j = ncrm_je_append(j, blocks[i]);
    }

    /* Dump journal */
    printf("Journal dump:\n");
    ncrm_je_iterate(j, _print_journal_entry, NULL);

    if(argc > 1) {
        struct ncrm_QueryParams q = {
            argv[1], argv[2], atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), atoi(argv[6])
        };
        printf("Query:\n  cat.pat. ..... : \"%s\"\n", q.categoryPatern);
        printf("  msg.pat. ..... : \"%s\"\n", q.msgPattern);
        printf("  lvl range .... : %d-%d\n", q.levelRange[0], q.levelRange[1]);
        printf("  time range ... : %lu-%lu\n", q.timeRange[0], q.timeRange[1]);

        struct ncrm_JournalEntry ** qr = NULL;
        printf("Query results:\n");
        unsigned long qrN = ncrm_je_query(j, &q, &qr);
        if(!qrN) {
            assert(!qr);
            printf(" (no entries)\n");
        } else {
            for(long unsigned int i = 0; i < qrN; ++i) {
                _print_journal_entry(qr[i], NULL);
            }
        }
    }

    return 0;
}
#endif

