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

#define NCRM_NENTRIES_INC 1024
#define NCRM_JOURNAL_MAX_BUFFER_LENGTH (5*1024*1024)
#define NCRM_JOURNAL_EXTENSION_NAME "log"

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
static void
_collect_entry_if_matches( struct ncrm_JournalEntry * je, void * qcPtr ) {
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

/* Static data structure, common for extension routines */
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
} gLocalData;

/** (internal) structure used to communicate problems from extension */
struct ListenerThreadExitResults {
    int rc;
    char * errorDetails;
};

static void *
_journal_updater(void * cfg_) {
    struct ncrm_JournalExtensionConfig * cfg
        = (struct ncrm_JournalExtensionConfig *) cfg_;

    struct ListenerThreadExitResults * lteR
        = malloc(sizeof(struct ListenerThreadExitResults));
    bzero(lteR, sizeof(struct ListenerThreadExitResults));

    struct ncrm_Event event;
    event.type = 0x3;  /* for extension */
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
        #if 0
        {  // XXX
            char bf[128];
            snprintf(bf, sizeof(bf), "unpacking %d entries", root.via.map.size);
            ncrm_mdl_error( cfg->modelPtr, bf );  // XXX
        }
        #endif
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
                struct ncrm_JournalEntry * newBlock
                    = _convert_msgs_block(&(kv->val.via.array));
                /* `gLocalData.journalEntries` is used by updating callback
                 * from main thread, so it has to be synchronized */
                pthread_mutex_lock(&gLocalData.entriesLock); {
                    ncrm_je_append( gLocalData.journalEntries 
                                  , newBlock
                                  );
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
                continue;
            }
            if(!strcmp("elapsedTime", key)) {
                /* update model's elapsed time: must always be just a number */
                assert( kv->val.type == MSGPACK_OBJECT_POSITIVE_INTEGER );
                cfg->modelPtr->elapsedTime
                        = kv->val.via.u64;
                continue;
            }
        }  /* end of journal entry message processing */
        msgpack_unpacked_destroy(&msg);
    }

    zmq_close(gLocalData.subscriber);
    zmq_ctx_destroy(gLocalData.zmqContext);
    free(gLocalData.recvBuf);

    pthread_exit(lteR);
}

static int
_journal_entries_ext_init( struct ncrm_Extension * ext
                         , struct ncrm_Model * modelPtr ) {
    gLocalData.keepGoingFlag = 0x1;
    assert(ext);
    assert(ext->userData);
    struct ncrm_JournalExtensionConfig * cfg
        = (struct ncrm_JournalExtensionConfig *) ext->userData;
    cfg->modelPtr = modelPtr;
    pthread_mutex_init(&gLocalData.entriesLock, NULL);
    pthread_create( &gLocalData.listenerThread, NULL, _journal_updater, cfg );
    return 0;
}

static int
_journal_entries_ext_update( struct ncrm_Extension * ext
                           , struct ncrm_Event * event ) {
    /* `gLocalData.journalEntries` is used by message-unpacking code
     * from listener thread, so it has to be guarded */
    pthread_mutex_lock(&gLocalData.entriesLock); {
        //ncrm_je_append( gLocalData.journalEntries 
        //              , newBlock
        //              );
        // TODO: update (or create and update) the window, based on data in
        // gLocalData.journalEntries
    } pthread_mutex_unlock(&gLocalData.entriesLock);
    return 0;
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

#endif

