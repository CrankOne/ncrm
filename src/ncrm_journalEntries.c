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

#include "ncrm_journalEntries.h"
#include "ncrm_queue.h"
#include "ncrm_extension.h"
#include "ncrm_model.h"

#include <zmq.h>
#include <msgpack.h>

#include <assert.h>
#include <string.h>
#include <fnmatch.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>

#include <pthread.h>

#include <curses.h>
#include <panel.h>

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
               , int (*callback)(struct ncrm_JournalEntry *, void *)
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
            if( callback(jePtr, userData) ) break;
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
static int
_collect_entry_if_matches( struct ncrm_JournalEntry * je, void * qcPtr ) {
    struct QueryCollector * collector = (struct QueryCollector *) qcPtr;
    /* filter by level */
    if( collector->qp->levelRange[0] > -1 ) {
        if( je->level < collector->qp->levelRange[0] ) {
            return 0;
        }
    }
    if( collector->qp->levelRange[1] > -1 ) {
        if( je->level > collector->qp->levelRange[1] ) {
            return 0;
        }
    }
    /* filter by category pattern */
    if( collector->qp->categoryPatern ) {
        if( fnmatch( collector->qp->categoryPatern
                   , je->category
                   , 0x0 /*FNM_CASEFOLD | FNM_EXTMATCH*/
                   ) ) {
            return 0;
        }
    }
    /* filter by message substring */
    if( collector->qp->msgPattern ) {
        if( fnmatch( collector->qp->msgPattern
                   , je->message
                   , 0x0 /*FNM_CASEFOLD | FNM_EXTMATCH*/
                   ) ) {
            return 0;
        }
    }
    /* filter by time range */
    if( collector->qp->timeRange[0] != ULONG_MAX ) {
        if( je->timest < collector->qp->timeRange[0] ) {
            return 0;
        }
    }
    if( collector->qp->timeRange[1] != ULONG_MAX ) {
        if( je->timest > collector->qp->timeRange[1] ) {
            return 0;
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
    return 0;
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
#else

/* A bulky function converting unpacked msgpack's messages into jounral
 * entries */
static struct ncrm_JournalEntry *
_convert_msgs_block( const msgpack_object_array * msgs ) {
    struct ncrm_JournalEntry * newEntriesBlock
        = malloc(sizeof(struct ncrm_JournalEntry)*(msgs->size+1));
    ncrm_je_mark_as_terminative(newEntriesBlock + msgs->size);
    for( uint64_t nMsg = 0; nMsg < msgs->size; ++nMsg ) {
        struct ncrm_JournalEntry * dest = newEntriesBlock + nMsg;
        msgpack_object * src = msgs->ptr + nMsg;
        assert(src->type == MSGPACK_OBJECT_ARRAY);
        assert(src->via.array.size == 4);
        /* copy data */
        dest->timest = src->via.array.ptr[0].via.u64;
        dest->level  = src->via.array.ptr[1].via.u64;
        dest->category
            = strndup( src->via.array.ptr[2].via.str.ptr
                     , src->via.array.ptr[2].via.str.size );
        dest->message
            = strndup( src->via.array.ptr[3].via.str.ptr
                     , src->via.array.ptr[3].via.str.size );
    }
    return newEntriesBlock;
}

/*
 * Extension definition
 *//////////////////// */

/** Represents view on a journal entris set */
struct JournalEntriesView {
    uint16_t showTimestamp:1;
    uint16_t showCategory:1;
    /** Window width and height. If set to zero means "automatic" (will be
     * (re-)calculated during next udate event).
     * Order: {{top, left}, {nRows, nCols}} */
    uint16_t dims[2][2];
    /** Current query to show in a window */
    struct ncrm_QueryParams query;
    /** View header window -- checkboxes, filters, etc */
    WINDOW * w_jHeader;
    PANEL  * p_jHeader;
    /** Journal entries window */
    WINDOW * w_jBody;  /* Is a pad actually */
    PANEL  * p_jBody;
    /** Current query results */
    struct ncrm_JournalEntry ** queryResults;
    unsigned long nQueryResults;
    /** Timestamp formatting settings */
    struct ncrm_JournalTimestampFormat tstFmtSettings;
    /** Formatting cache for entries to show:
     *  1. <timestamp:char*>, if showTimestamp is on
     *  2. <null:char>, if showTimestamp is on
     *  3. <category:char*>
     *  4. <null:char>
     *  ... XXX?
     * */
    char formattedTimestamps[NCRM_JOURNAL_MAX_LINES_SHOWN][NCRM_JOURNAL_MAX_TIMESTAMP_LEN];
};

static struct JournalEntriesView *
_new_journal_entries_view( struct ncrm_JournalExtensionConfig * cfg ) {
    struct JournalEntriesView * obj = malloc(sizeof(struct JournalEntriesView));
    bzero(obj, sizeof(struct JournalEntriesView));

    obj->showTimestamp = 0x1;
    obj->showCategory = 0x1;
    obj->dims[0][0] = obj->dims[0][1] = obj->dims[1][0] = obj->dims[1][1] = 0;
    obj->queryResults = NULL;
    obj->nQueryResults = 0;

    memcpy( &obj->query
          , &cfg->defaultQueryParameters
          , sizeof(struct ncrm_QueryParams)  );
    memcpy( &obj->tstFmtSettings
          , &cfg->defaultTimestampFormatter
          , sizeof(struct ncrm_JournalTimestampFormat)  );

    return obj;
}

/* Log messages body has special behaviour for refreshing and these functions
 * are for convenience */
void
_jmsgwin_refresh(struct JournalEntriesView * view) {
    pnoutrefresh( view->w_jBody
                , NCRM_JOURNAL_MAX_LINES_SHOWN - view->dims[1][0] + 1
                , 0
                /* ^^^ upper left corner of the rectangle to be shown in the pad */
                , view->dims[0][0] + 1, view->dims[0][1]
                , view->dims[1][0] + 1, view->dims[1][1]
                );
}

void
_jmsgwin_reset_cursor(struct JournalEntriesView * view) {
    wmove( view->w_jBody
         , NCRM_JOURNAL_MAX_LINES_SHOWN - 1
         , 0 );
}

/**\brief Breaks given string into a multile message with lines of `width`
 *        length max returning number of lines.
 *
 * Returned buffer contains lines separated by 0x00 char finally terminated
 * with 0x4. */
static uint16_t
_split_message( const char * msg
              , uint16_t width
              , char * destBuffer
              ) {
    uint16_t nLine = 0;
    uint16_t nCharInLine = 0;
    char * curDest = destBuffer;
    const char * curSrc = msg;
    //for( const char * c = msg; '\0' != *c; ++c, ++nCharInLine )
    const char * c = msg;
    do {
        if( !('\n' == *c || '\0' == *c || nCharInLine == width) ) {
            ++c;
            ++nCharInLine;
            continue;
        }
        /* copy line */
        ++nLine;
        memcpy( curDest, curSrc, nCharInLine );
        curDest[nCharInLine] = '\0';
        /*printf( " copied \"%s\" (%d)\n", curDest, (int) nCharInLine ); // XXX */
        curDest += nCharInLine + 1;  /* +1 for terminator */
        curSrc  += nCharInLine    ;
        nCharInLine = 0;

        if( '\n' == *c ) {  /*jump over newline char*/
            ++c;
            ++curSrc;
        }
    } while('\0' != *curSrc);
    *curDest = 0x4;  /* End of Transmission */
    return nLine;
}

static attr_t
_put_priority_glyph(WINDOW * dest, int val, int omitChar) {
    #define put_formatted_prefix(n, c, name, descr, attrs, ... )    \
    if( n*100 >= val ) {                                            \
        wattrset(dest, attrs);                                      \
        if(!omitChar) waddch(dest, c);                              \
        else waddch(dest, ACS_CKBOARD);                             \
        return attrs;                                               \
    }
    ncrm_for_every_special_attribute(put_formatted_prefix);
    #undef put_formatted_prefix

    wattrset(dest, A_NORMAL);
    if(!omitChar) waddch(dest, '*');
    else waddch(dest, ACS_CKBOARD);
    return A_NORMAL;
}


/* Static data structure, common for extension routines
 *
 * We assume that this extension won't be replicated, so global static object
 * seem to be a right choice.
 * */
static struct {
    /** Listener (SUB) thread join condition
     * Note: it is not guarded by mutex as there is only one thread actually
     * modifying it */
    int keepGoingFlag:1;
    /** 0MQ (SUB) context receiving messages */
    void * zmqContext
       , * subscriber
       ;
    /** recv'ing buffer */
    char * recvBuf;
    /** Collected journal entries */
    struct ncrm_JournalEntries * journalEntries;
    /** Mutex protecting access to `journalEntries` */
    pthread_mutex_t entriesLock;
    /** Listener thread */
    pthread_t listenerThread;

    /** Collection of views */
    struct JournalEntriesView ** views;
} gLocalData;

/** (internal) structure used to communicate problems from extension */
struct ListenerThreadExitResults {
    int rc;
    char * errorDetails;
};

/* A listener loop for journal events; to be ran in thread */
static void *
_journal_updater(void * cfg_) {
    struct ncrm_JournalExtensionConfig * cfg
        = (struct ncrm_JournalExtensionConfig *) cfg_;

    struct ListenerThreadExitResults * lteR
        = malloc(sizeof(struct ListenerThreadExitResults));
    bzero(lteR, sizeof(struct ListenerThreadExitResults));

    struct ncrm_Event event = { ncrm_kEventExtension };
    strncpy( event.payload.forExtension.extensionName
           , NCRM_JOURNAL_EXTENSION_NAME
           , sizeof(event.payload.forExtension.extensionName)
           );

    gLocalData.recvBuf = malloc(NCRM_JOURNAL_MAX_BUFFER_LENGTH);
    gLocalData.zmqContext = zmq_ctx_new();
    gLocalData.subscriber = zmq_socket(gLocalData.zmqContext, ZMQ_SUB);
    int rc = zmq_connect(gLocalData.subscriber, cfg->address);
    if( rc ) {
        lteR->rc = 1;
        lteR->errorDetails = malloc(128);
        snprintf(lteR->errorDetails, 128
                , "zmq_connect(\"%s\") return code: %d, %s"
                , cfg->address, rc, zmq_strerror(errno) );
        pthread_exit(lteR);
    }
    rc = zmq_setsockopt(gLocalData.subscriber, ZMQ_SUBSCRIBE, "", 0);
    if( rc ) {
        lteR->rc = 1;
        lteR->errorDetails = malloc(128);
        snprintf(lteR->errorDetails, 128
                , "zmq_setsockopt(SUBSCRIBE) on \"%s\" return code: %d"
                , cfg->address, rc );
        pthread_exit(lteR);
    }

    while(gLocalData.keepGoingFlag) {
        int recvRet = zmq_recv( gLocalData.subscriber
                              , gLocalData.recvBuf
                              , NCRM_JOURNAL_MAX_BUFFER_LENGTH
                              , 0 == cfg->recvIntervalMSec ? 0 : ZMQ_DONTWAIT );
        int errnoCpy = 0;
        if( recvRet < 0 ) errnoCpy = errno;
        if( 0 != cfg->recvIntervalMSec && EAGAIN == errnoCpy ) {
            usleep(cfg->recvIntervalMSec*1e3);
            continue;
        }
        if( recvRet == -1 ) {
            lteR->rc = 2;
            lteR->errorDetails = malloc(128);
            snprintf(lteR->errorDetails, 128
                    , "zmq_recv(...) on \"%s\" return code: %d, %s"
                    , cfg->address, recvRet, zmq_strerror(errnoCpy) );
            pthread_exit(lteR);
        }
        if( recvRet > NCRM_JOURNAL_MAX_BUFFER_LENGTH ) {
            lteR->rc = 3;
            lteR->errorDetails = malloc(256);
            snprintf(lteR->errorDetails, 256
                    , "zmq_recv(...) on \"%s\" fetched message of %d bytes"
                      " length while ncrm \"" NCRM_JOURNAL_EXTENSION_NAME
                      "\" extesnions limit is %d bytes max."
                    , cfg->address, recvRet, NCRM_JOURNAL_MAX_BUFFER_LENGTH );
            pthread_exit(lteR);
        }

        msgpack_unpacked msg;
        msgpack_unpacked_init(&msg);
        msgpack_unpack_return ret
            = msgpack_unpack_next( &msg  /* ................. result */
                                 , gLocalData.recvBuf  /* ... data */
                                 , recvRet  /* .............. size */
                                 , NULL  /* .............. (size_t *) offset */
                                 );
        if( !ret ) {
            lteR->rc = 4;
            lteR->errorDetails = malloc(256);
            snprintf(lteR->errorDetails, 256
                    , "msgpack_unpack_next(...) on \"%s\" return code: %d"
                    , cfg->address, (int) ret );
            pthread_exit(lteR);
        }

        /* Extract data; we expect the message to be an object */
        msgpack_object root = msg.data;
        if( root.type != MSGPACK_OBJECT_MAP ) {
            lteR->rc = 5;
            lteR->errorDetails = malloc(128);
            snprintf(lteR->errorDetails, 128
                    , "From \"%s\": root message node is of type %#x, not"
                      " an object."
                    , cfg->address, root.type );
            pthread_exit(lteR);
        }
        /* NOTE: schemata checks below relate to the client API that for most
         * of the cases should be provided by ncrm lib itself, so we only
         * check schematic validity with assert() */
        int doUpdateFooter = 0;
        for( int nMapNode = 0; nMapNode < root.via.map.size; ++nMapNode ) {
            const msgpack_object_kv * kv = root.via.map.ptr + nMapNode;
            assert( kv->key.type == MSGPACK_OBJECT_STR );
            char key[128];
            strncpy(key, kv->key.via.str.ptr, kv->key.via.str.size);
            key[ kv->key.via.str.size >= sizeof(key)
               ? sizeof(key)-1
               : kv->key.via.str.size
               ] = '\0';
            /* ^^^ NOTE this string is not null-terminated! */
            //ncrm_mdl_error( cfg->modelPtr, key );  // XXX
            if(!strcmp("j", key)) {
                /* process journal entries */
                assert( kv->val.type == MSGPACK_OBJECT_ARRAY );
                #if 0
                {  // XXX
                    char bf[128];
                    snprintf(bf, sizeof(bf), "unpacking %d entries", (int) kv->val.via.array.size);
                    ncrm_mdl_error( cfg->modelPtr, bf );  // XXX
                }
                #endif
                struct ncrm_JournalEntry * newBlock
                    = _convert_msgs_block(&(kv->val.via.array));
                /* `gLocalData.journalEntries` is used by updating callback
                 * from main thread, so it has to be synchronized */
                pthread_mutex_lock(&gLocalData.entriesLock); {
                    #if 0
                    {  // XXX -------------------------------------------------
                        char bf[128];
                        for( struct ncrm_JournalEntry * jePtr = newBlock
                           ; !ncrm_je_is_terminative_entry(jePtr)
                           ; ++jePtr
                           ) {
                            snprintf(bf, sizeof(bf), " => \"%s\"", jePtr->message);
                            ncrm_mdl_error( cfg->modelPtr, bf );
                        }
                    }  // XXX -------------------------------------------------
                    #endif
                    gLocalData.journalEntries = ncrm_je_append(
                            gLocalData.journalEntries , newBlock );
                } pthread_mutex_unlock(&gLocalData.entriesLock);
                continue;
            }
            if(!strcmp("status", key)) {
                /* update model's status: must always be a (string, u-number) */
                assert( kv->val.type == MSGPACK_OBJECT_ARRAY );
                assert( kv->val.via.array.size == 2 );
                pthread_mutex_lock(&cfg->modelPtr->lock); {
                    const char * s
                        = kv->val.via.array.ptr[0].via.str.ptr;
                    const uint64_t ss
                        = kv->val.via.array.ptr[0].via.str.size;
                    if( !s || *s == '\0' ) {
                        cfg->modelPtr->appMsg[0] = '\0';
                    } else {
                        strncpy( cfg->modelPtr->appMsg
                               , s
                               , sizeof(cfg->modelPtr->appMsg) );
                        cfg->modelPtr->appMsg[ ss >= sizeof(cfg->modelPtr->appMsg)
                                             ? sizeof(cfg->modelPtr->appMsg) - 1
                                             : ss
                                             ] = '\0';
                    }
                    cfg->modelPtr->statusMode
                        = kv->val.via.array.ptr[1].via.u64;
                } pthread_mutex_unlock(&cfg->modelPtr->lock);
                doUpdateFooter = 1;
                continue;
            }
            if(!strcmp("progress", key)) {
                /* update model's progress: must be always (number, number) */
                assert( kv->val.type == MSGPACK_OBJECT_ARRAY );
                assert( kv->val.via.array.size == 2 );
                pthread_mutex_lock(&cfg->modelPtr->lock); {
                    cfg->modelPtr->currentProgress
                        = kv->val.via.array.ptr[0].via.u64;
                    cfg->modelPtr->currentProgress
                        = kv->val.via.array.ptr[1].via.u64;
                } pthread_mutex_unlock(&cfg->modelPtr->lock);
                doUpdateFooter = 1;
                continue;
            }
            if(!strcmp("elapsedTime", key)) {
                /* update model's elapsed time: must always be just a number */
                assert( kv->val.type == MSGPACK_OBJECT_POSITIVE_INTEGER );
                cfg->modelPtr->elapsedTime
                        = kv->val.via.u64;
                doUpdateFooter = 1;
                continue;
            }
        }  /* end of journal entry message processing */
        ncrm_enqueue(&event);
        if( doUpdateFooter ) {
            struct ncrm_Event footerUpdateEvent = {ncrm_kEventFooterUpdate};
            ncrm_enqueue(&footerUpdateEvent);
        }
        msgpack_unpacked_destroy(&msg);
    }

    zmq_close(gLocalData.subscriber);
    zmq_ctx_destroy(gLocalData.zmqContext);
    free(gLocalData.recvBuf);

    pthread_exit(lteR);
}

static int
_journal_entries_ext_init( struct ncrm_Extension * ext
                         , struct ncrm_Model * modelPtr
                         , uint16_t top, uint16_t left
                         , uint16_t nLines, uint16_t nCols ) {
    assert(nLines);
    assert(nCols);

    gLocalData.keepGoingFlag = 0x1;
    assert(ext);
    assert(ext->userData);
    struct ncrm_JournalExtensionConfig * cfg
        = (struct ncrm_JournalExtensionConfig *) ext->userData;
    cfg->modelPtr = modelPtr;

    cfg->dims[0][0] = top;
    cfg->dims[0][1] = left;
    cfg->dims[1][0] = nLines;
    cfg->dims[1][1] = nCols;

    pthread_mutex_init(&gLocalData.entriesLock, NULL);
    pthread_create( &gLocalData.listenerThread, NULL, _journal_updater, cfg );

    /* Init single view (further we possibly will recieve some initial view
     * configuration from outside) */
    gLocalData.views = malloc(2*sizeof(struct JournalEntriesView *));
    gLocalData.views[1] = NULL;  /* sentinel */
    gLocalData.views[0] = _new_journal_entries_view(cfg);

    return 0;
}

static void
_update_view( struct ncrm_Model *, struct JournalEntriesView * view );  /* fwd */

static int
_journal_entries_ext_update( struct ncrm_Extension * ext
                           , struct ncrm_Event * event ) {
    struct ncrm_JournalExtensionConfig * cfg
        = (struct ncrm_JournalExtensionConfig *) ext->userData;
    /* Create windows, pads, panels if need.
     * Iterate over set of views looking for zero dimensions.
     * */
    #if 0
    int overallDims[64][2];  /* todo: dedicated macro */
    int nView = 0;
    for( struct JournalEntriesView * jev = gLocalData.views
       ; jev && *jev
       ; ++jev, ++nView ) {
        overallDims[nDim][0] = jev->dims[0];
        overallDims[nDim][1] = jev->dims[1];
        assert( nView < 64 );  /* todo: dedicated macro */
    }
    assert( nView < 64 );  /* todo: dedicated macro */
    overallDims[nDim] = -1;
    ... TODO: multi-view
    #else
    /* TODO: multiview. We assume single (static) view so far */
    assert( gLocalData.views );
    assert( gLocalData.views[0] );
    assert( gLocalData.views[1] == NULL );
    {
        struct JournalEntriesView * jev = gLocalData.views[0];
        /* If dimensions is not set, use all available space for the
         * extension */
        if( 0 == jev->dims[1][0] || 0 == jev->dims[1][1] ) {
            jev->dims[0][0] = cfg->dims[0][0];   /* top */
            jev->dims[0][1] = cfg->dims[0][1];   /* left */
            jev->dims[1][0] = cfg->dims[1][0];   /* height */
            jev->dims[1][1] = cfg->dims[1][1];   /* width */
        }
        assert(jev->dims[1][0]);
        assert(jev->dims[1][1]);
        /* If windows/panels/pads do not exist, create */
        if( !jev->w_jHeader ) {
            assert(jev->dims[1][0]);
            assert(jev->dims[1][1]);
            jev->w_jHeader = newwin( 1   /* # of lines in win */
                                   , jev->dims[1][1]  /* # of columns in win */
                                   , jev->dims[0][0]  /* Y of LT corner */
                                   , jev->dims[0][1]  /* X of LT corner */
                                   );
            #if 0
            jev->w_jBody = newwin( jev->dims[1][0] - 1  /* # of lines in win */
                                 , jev->dims[1][1]      /* # of columns in win */
                                 , jev->dims[0][0] + 1  /* Y of LT corner */
                                 , jev->dims[0][1]      /* X of LT corner */
                                 );
            #endif
            jev->w_jBody = newpad( NCRM_JOURNAL_MAX_LINES_SHOWN  /* # of lines in pad */
                                 , jev->dims[1][1]      /* # of columns in pad */
                                 );
            box(jev->w_jBody, 0, 0);  // XXX
            #if 0
            box(jev->w_jBody, 0, 0);
            touchwin(jev->w_jBody);
            wrefresh(jev->w_jBody);
            #endif

            #if 0
            box(jev->w_jHeader, 0, 0);
            touchwin(jev->w_jHeader);
            wrefresh(jev->w_jHeader);
            #endif
            
            /* Create panels for header and footer */
            jev->p_jHeader = new_panel(jev->w_jHeader);
            jev->p_jBody   = new_panel(jev->w_jBody);
        }
    }
    #endif

    /* `gLocalData.journalEntries` is used by message-unpacking code
     * from listener thread, so it has to be guarded */
    pthread_mutex_lock(&gLocalData.entriesLock); {
        /* Update views selection according to their queries using new data */
        for( struct JournalEntriesView ** jev = gLocalData.views
           ; jev && *jev
           ; ++jev ) {
            /* re-query items */
            assert( gLocalData.journalEntries );
                (*jev)->nQueryResults =
                    ncrm_je_query( gLocalData.journalEntries
                                 , &(*jev)->query
                                 , &(*jev)->queryResults
                                 );
            _update_view(cfg->modelPtr, *jev);
        }
    } pthread_mutex_unlock(&gLocalData.entriesLock);
    return 0;
}

static void
_update_view( struct ncrm_Model * mdl
            , struct JournalEntriesView * view ) {
    assert(view);
    { /* Update query settings window */
        char printBuf[64];
        wmove(view->w_jHeader, 0, 0);
        wattron( view->w_jHeader, A_DIM | A_REVERSE );
        whline(view->w_jHeader, ' ', view->dims[1][1]);
        wmove(view->w_jHeader, 0, 0);
        //  [x] timestamp [x] category | [*]*::* |
        //wattroff(view->w_jHeader, A_DIM);

        waddch(view->w_jHeader, ' ');

        waddch(view->w_jHeader, '[');
        waddch(view->w_jHeader, view->showTimestamp ? '#' : ' ');
        wprintw(view->w_jHeader, "] time:");

        if( view->query.timeRange[0] != ULONG_MAX ) {
            view->tstFmtSettings.callback( &(view->tstFmtSettings)
                                         , printBuf
                                         , view->query.timeRange[0]*1e3 );
            wprintw(view->w_jHeader, printBuf);
        } else {
            waddch(view->w_jHeader, '*');
        }
        waddch(view->w_jHeader, '-');
        if( view->query.timeRange[1] != ULONG_MAX ) {
            view->tstFmtSettings.callback( &(view->tstFmtSettings)
                                         , printBuf
                                         , view->query.timeRange[1]*1e3 );
            wprintw(view->w_jHeader, printBuf);
        } else {
            waddch(view->w_jHeader, '*');
        }

        wprintw(view->w_jHeader, ", [");
        waddch(view->w_jHeader, view->showCategory ? '#' : ' ');
        wprintw(view->w_jHeader, "] category:");
        if( view->query.categoryPatern ) {
            wattron(view->w_jHeader, A_BOLD);
            wprintw(view->w_jHeader, view->query.categoryPatern);
            wattroff(view->w_jHeader, A_BOLD);
        } else {
            waddch(view->w_jHeader, '*');
        }
        wprintw(view->w_jHeader, ", message:");
        if( view->query.msgPattern ) {
            wattron(view->w_jHeader, A_BOLD);
            wprintw(view->w_jHeader, view->query.msgPattern);
            wattroff(view->w_jHeader, A_BOLD);
        } else {
            waddch(view->w_jHeader, '*');
        }
        wprintw(view->w_jHeader, ", prio:");
        if( view->query.levelRange[0] != -1 ) {
            snprintf( printBuf, sizeof(printBuf), "%d"
                    , view->query.levelRange[0] );
            wprintw(view->w_jHeader, printBuf);
        } else {
            waddch(view->w_jHeader, '*');
        }
        waddch(view->w_jHeader, '-');
        if( view->query.levelRange[1] != -1 ) {
            snprintf( printBuf, sizeof(printBuf), "%d"
                    , view->query.levelRange[1] );
            wprintw(view->w_jHeader, printBuf);
        } else {
            waddch(view->w_jHeader, '*');
        }
        /* debug */
        uint16_t nEntriesOverall = 0;
        for( const struct ncrm_JournalEntries * jes = gLocalData.journalEntries
           ; jes
           ; jes = jes->next
           ) {
            for( const struct ncrm_JournalEntry * je = jes->entries
               ; !ncrm_je_is_terminative_entry(je)
               ; ++je ) {
                ++nEntriesOverall;
            }
        }

        char bf[128];
        snprintf( bf, sizeof(bf)
                , " q%d/%d", (int) view->nQueryResults, (int) nEntriesOverall );
        wprintw(view->w_jHeader, bf);
    }

    //werase( view->w_jBody );  /* TODO: uncomment this */
    _jmsgwin_reset_cursor(view);
    /* Check that we have something to show */
    if( !view->nQueryResults ) {
        wattron(view->w_jBody, A_DIM);
        wprintw(view->w_jBody, "... no messages received.");
        wattroff(view->w_jBody, A_DIM);
        _jmsgwin_refresh(view);
        return;
    }
    /* NOTE: `entries` assumed to go in ascending order, e.g. latest message go
     * last.
     *
     * Iterate over the journal entries finding latest message to iterate
     * backwards while printing and max of variadic width columns, if needed
     * */
    int32_t nEntryLast = 0;
    uint16_t tsMaxLen = 0;
    uint32_t maxMsgLen = 0;
    uint64_t nQuery = 0;
    for( struct ncrm_JournalEntry ** jePtr = view->queryResults
       ; nEntryLast < view->dims[1][0] && nEntryLast < NCRM_JOURNAL_MAX_LINES_SHOWN
         && nQuery < view->nQueryResults
       ; ++jePtr, ++nEntryLast, ++nQuery ) {
        uint32_t msgLen = strlen((*jePtr)->message);
        if(msgLen > maxMsgLen) maxMsgLen = msgLen;
        /* format timestamp */
        if( view->showTimestamp ) {
            const struct ncrm_JournalEntry * je = *jePtr;
            char * formattedBufBgn = view->formattedTimestamps[nEntryLast];
            uint16_t tsLen = view->tstFmtSettings.callback(
                    &view->tstFmtSettings, formattedBufBgn, je->timest );
            /* update timestamp max len if need */
            if(tsMaxLen < tsLen) tsMaxLen = tsLen;
        }
    }
    --nEntryLast;
    /* Calculate widths */
    /* 1 + 1 + [tsw + 1] + [msgW + 1] + 1
     * ^   ^   ^^^^^^^^^   ^^^^    ^    ^
     * |   |       |         |     |    +- scrollbar
     * |   |       |         |     +------ reserved for a newline char
     * |   |       |         +------------ width of the message itself
     * |   |       +---------------------- (opt.) timestamp + 1 for gap
     * |   +------------------------------ gap after priority marking
     * +---------------------------------- priority marking
     * We always have a single-char column with priority marking
     * We foresee also two chars at the end: a newline marking and a scrollbar
     * */
    int32_t msgW = view->dims[1][1]
                 - 1 - 1  /* priority + gap */
                 - (view->showTimestamp ? (tsMaxLen + 1) : 0)
                 - 1      /* scrollbar */
                 ;
    if( nEntryLast < 0 || msgW < 0 ) {
        {
            char errBf[128];
            snprintf( errBf, sizeof(errBf)
                    , "Extension \"" NCRM_JOURNAL_EXTENSION_NAME "\": width error." );
            ncrm_mdl_error( mdl, errBf );
        }
        /* window width is not enough to show the message */
        wprintw(view->w_jBody, "width error");
        _jmsgwin_refresh(view);
        return;
    }
    /* Format messages making breaks. Iterate from last message backwards until
     * either messages or shown lines will be exceeded. */
    int32_t lastMessageBegin = NCRM_JOURNAL_MAX_LINES_SHOWN;
    /* Allocate message formatting buffer to be at least long enough to handle
     * longest formatted string */
    char * buf = malloc(((maxMsgLen)/msgW + 1)*(msgW+2));
    werase(view->w_jBody);
    #if 0
    {  // XXX
        char errBf[128];
        snprintf( errBf, sizeof(errBf)
                , "Extension \"" NCRM_JOURNAL_EXTENSION_NAME "\": printing at most %d messages."
                , (int) nEntryLast+1 );
        ncrm_mdl_error( mdl, errBf );
    }
    #endif
    ncrm_mdl_error( mdl, view->queryResults[0]->message );  // XXX
    uint16_t nLinesShown = 0;
    for( int16_t nEntry = nEntryLast
       ; nEntry >= 0 && nLinesShown < view->dims[1][0]
       ; --nEntry ) {
        assert(nEntry > -1);
        const struct ncrm_JournalEntry * je = view->queryResults[nEntry];
        /* format message to fit message's column width and get number of used
         * lines.
         * Formatted message is of the form:
         *  line1<0x00>line2<0x00>...lineN<0x00><0x04>
         * I.e. single lines are terminated with null character, the last line
         * is terminated with 0x04 (EOT).
         * */
        #if 0
        {  // XXX
            char errBf[128];
            snprintf( errBf, sizeof(errBf)
                    , " print %d query's entry starting at line %d: %s"
                    , (int) nEntry
                    , (int) lastMessageBegin
                    , je->message
                    );
            ncrm_mdl_error( mdl, errBf );
        }
        #endif
        uint16_t nLinesNeed = _split_message( je->message, msgW, buf );
        assert( nLinesNeed );
        lastMessageBegin -= nLinesNeed;
        /* Print
         * We print the messages from bottom to up, while the lines within
         * message are printed from top to bottom. */
        //for( uint16_t nLineInMsg = 0; nLineInMsg < nLinesNeed; ++nLineInMsg ) {
        uint16_t nLineInMsg = 0;
        const char * ts = view->formattedTimestamps[nEntry];
        uint16_t tsLen = strlen(ts);
        for( char * c = buf; *c != 0x4; c += strlen(c) + 1, ++nLineInMsg ) {
            wmove( view->w_jBody
                 , lastMessageBegin + nLineInMsg, 0
                 );
            attr_t pgAttrs = _put_priority_glyph(view->w_jBody, je->level, nLineInMsg);
            if( view->showTimestamp ) {
                wattrset( view->w_jBody, pgAttrs );
                wattroff( view->w_jBody, A_BLINK );
                if( !nLineInMsg ) {
                    waddch( view->w_jBody, ' ' );
                    /* move cursor for right align in column */
                    #if 0
                    wmove( view->w_jBody
                         , lastMessageBegin + nLineInMsg, 3 + tsMaxLen - tsLen - 1
                         );
                    #else
                    uint16_t nSpaces = tsMaxLen - tsLen;
                    for(int i = 0; i < nSpaces; ++i) {
                        waddch( view->w_jBody, ' ' );
                    }
                    #endif
                    wprintw( view->w_jBody, ts );
                    wattrset( view->w_jBody, A_NORMAL );
                    waddch( view->w_jBody, ' ' );
                } else {
                    wmove( view->w_jBody
                         , lastMessageBegin + nLineInMsg, 3 + tsMaxLen
                         );
                }
            } else {
                /* timestamp column disabled */
                wmove( view->w_jBody
                     , lastMessageBegin + nLineInMsg, 3
                     );
            }
            wattrset( view->w_jBody, A_NORMAL );
            /* print message */
            wprintw( view->w_jBody, c );
        }
        nLinesShown += nLinesNeed;
    }
    free(buf);
    _jmsgwin_refresh(view);
    //pnoutrefresh( view->w_jBody
    //            , NCRM_JOURNAL_MAX_LINES_SHOWN - view->dims[1][0] + 1
    //            , 0
    //            /* ^^^ upper left corner of the rectangle to be shown in the pad */
    //            /* these are screen coordinates: */
    //            , view->dims[0][0] + 1, view->dims[0][1]
    //            , view->dims[1][0] + 1, view->dims[1][1]
    //            );
}

static int
_journal_entries_ext_shutdown(struct ncrm_Extension * ext) {
    char errBf[128];
    gLocalData.keepGoingFlag = 0x0;
    void * listenerTheadReturn = NULL;
    pthread_join(gLocalData.listenerThread, &listenerTheadReturn);
    if( listenerTheadReturn ) {
        struct ListenerThreadExitResults * lteR
            = (struct ListenerThreadExitResults *) listenerTheadReturn;
        if( lteR->rc ) {
            snprintf( errBf, sizeof(errBf)
                    , "Listener thread of"
                    " \"" NCRM_JOURNAL_EXTENSION_NAME "\" exit with code %d"
                    ": \"%s\"\n", lteR->rc, lteR->errorDetails );
            ncrm_mdl_error( ((struct ncrm_JournalExtensionConfig *) (ext->userData))->modelPtr
                          , errBf
                          );
            free(lteR->errorDetails);
        }
        int rc = lteR->rc;
        free(lteR);
        return rc;
    } else {
        snprintf( errBf, sizeof(errBf)
                , "Listener thread of"
                  " \"" NCRM_JOURNAL_EXTENSION_NAME "\" exit with NULL"
                  " result.\n" );
        ncrm_mdl_error( ((struct ncrm_JournalExtensionConfig *) (ext->userData))->modelPtr
                      , errBf
                      );
        return -1;
    }
}

struct ncrm_Extension gJournalExtension = {
    NCRM_JOURNAL_EXTENSION_NAME, 'l', NULL,
    _journal_entries_ext_init,
    _journal_entries_ext_update,
    _journal_entries_ext_shutdown
};

#endif  /* standolone build tests */

