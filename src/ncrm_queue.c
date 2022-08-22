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

#include "ncrm_queue.h"

#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

static struct EventListEntry {
    int isInUse:1;
    struct ncrm_Event eventObject;
    struct EventListEntry * next;
} gEventListEntriesPool[NCRM_MAX_EVENTS_IN_QUEUE];
static pthread_mutex_t gEventPoolMutex;

static struct ncrm_Queue {
    pthread_mutex_t mtxQueueEmpty;
    pthread_cond_t cvQueueEmpty;

    struct EventListEntry * head, *tail;
} gQueue;

static struct EventListEntry *
_alloc_evlist_entry() {
    struct EventListEntry * evEntry;
    pthread_mutex_lock(&gEventPoolMutex); {
        uint32_t nEvent = 0;
        for( evEntry = gEventListEntriesPool
           ; (evEntry->isInUse & 0x1) && nEvent <= NCRM_MAX_EVENTS_IN_QUEUE
           ; ++evEntry, ++nEvent ) {}
        if( nEvent == NCRM_MAX_EVENTS_IN_QUEUE ) {
            /* failed to allocate new event (entire pool busy) */
            pthread_mutex_unlock(&gEventPoolMutex);
            return NULL;
        }
        evEntry->isInUse = 0x1;
        evEntry->next = NULL;
        /* no need to zero event object as it will be anyway overwritten by
         * memcpy() */
    } pthread_mutex_unlock(&gEventPoolMutex);
    return evEntry;
}

void
_free_evlist_entry( struct EventListEntry * evEntry) {
    pthread_mutex_lock(&gEventPoolMutex);
    evEntry->isInUse = 0x0;
    pthread_mutex_unlock(&gEventPoolMutex);
}

void
ncrm_queue_init() {
    bzero( gEventListEntriesPool, sizeof(gEventListEntriesPool) );
    gQueue.head = gQueue.tail = NULL;

    pthread_mutex_init(&gEventPoolMutex, NULL);

    pthread_mutex_init(&gQueue.mtxQueueEmpty, NULL);
    pthread_cond_init(&gQueue.cvQueueEmpty, NULL);
}

void
ncrm_queue_free() {
    pthread_mutex_destroy(&gEventPoolMutex);
    pthread_mutex_destroy(&gQueue.mtxQueueEmpty);
    pthread_cond_destroy(&gQueue.cvQueueEmpty);
}

int
ncrm_enqueue( struct ncrm_Event * eventPtr ) {
    /* Allocate new list entry and copy source data to it */
    struct EventListEntry * evListEntry = _alloc_evlist_entry();
    if( !evListEntry ) return -1;
    memcpy(&evListEntry->eventObject, eventPtr, sizeof(struct ncrm_Event));
    /* ^^^ note that here we completely overwrite evListEntry->eventObject */
    /* Add event list entry into queue */
    pthread_mutex_lock(&(gQueue.mtxQueueEmpty));
    {
        if( !gQueue.tail ) {
            assert(!gQueue.head);
            gQueue.head = gQueue.tail = evListEntry;
        } else {
            gQueue.tail->next = evListEntry;
            gQueue.tail = gQueue.tail->next;
        }
    }
    pthread_cond_broadcast(&gQueue.cvQueueEmpty);
    pthread_mutex_unlock(&(gQueue.mtxQueueEmpty));
    /* ^^^ these two lines may be interchanged probably to gain a bit more
     *     performance, see discussion in comments of
     *     https://stackoverflow.com/a/5538447/1734499 */
    return 0;
}

/*
 * Event consumer
 */
#if 1

int
ncrm_do_with_events( void(*callback)(struct ncrm_Event *, void*)
                   , void * userdata) {
    pthread_mutex_lock(&gQueue.mtxQueueEmpty);
    while(!gQueue.head)
        pthread_cond_wait(&gQueue.cvQueueEmpty, &gQueue.mtxQueueEmpty);
    /* Consume events from the queue */
    while(gQueue.head) {
        /* handle the event with callback */
        callback(&gQueue.head->eventObject, userdata);
        /* remove event from queue, memorizing the ptr */
        struct EventListEntry * toFree = gQueue.head;
        gQueue.head = gQueue.head->next;
        /* mark it as free in pool (free evlist entry) */
        _free_evlist_entry(toFree);
    }
    assert(!gQueue.head);
    gQueue.tail = NULL;
    pthread_mutex_unlock(&gQueue.mtxQueueEmpty);
    return 0;
}

#else
struct CalleeData {
    void(*callback)(struct ncrm_Event *, void*);
    void * userdata;
};

static void *
_event_callee(void * calleeData_) {
    struct CalleeData * calleeData
        = (struct CalleeData *) calleeData_;
    pthread_mutex_lock(&gQueue.mtxQueueEmpty);
    while(!gQueue.head)
        pthread_cond_wait(&gQueue.cvQueueEmpty, &gQueue.mtxQueueEmpty);
    /* Consume events from the queue */
    while(gQueue.head) {
        /* handle the event with callback */
        calleeData->callback( &gQueue.head->eventObject
                            , calleeData->userdata
                            );
        /* remove event from queue, memorizing the ptr */
        struct EventListEntry * toFree = gQueue.head;
        gQueue.head = gQueue.head->next;
        /* mark it as free in pool (free evlist entry) */
        _free_evlist_entry(toFree);
    }
    assert(!gQueue.head);
    gQueue.tail = NULL;
    pthread_mutex_unlock(&gQueue.mtxQueueEmpty);
    pthread_exit(NULL);
}

int
ncrm_do_with_events( void(*callback)(struct ncrm_Event *, void*)
                   , void * userdata) {
    pthread_t callee;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    struct CalleeData calleeData = { callback, userdata };
    pthread_create(&callee, &attr, _event_callee, &calleeData );
    pthread_join(callee, NULL);
    pthread_attr_destroy(&attr);
    return 0;
}
#endif
