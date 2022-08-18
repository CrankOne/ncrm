#ifndef H_NCRM_QUEUE_H
#define H_NCRM_QUEUE_H

/**\file
 * \brief Defines event queue objects and API of ncrm. */

/** Max size of the event queue */
#define NCRM_MAX_EVENTS_IN_QUEUE 1024

/**\brief Representation of asynchroneous event (subject of event queue) */
struct ncrm_Event {
    /* Codes:
     *  0x0  -- unidentified (reserved)
     *  0x1  -- increment update count
     *  0x2  -- keypress
     *  0x3  -- to be dispatched to an extension
     *  0x4  -- header update
     *  0x5  -- footer update
     *  ...
     * */
    int type:4;
    // ...
    union Payload {
        char keypress;
        // ...
    } payload;
};

/** Initializes global internal event queue object */
void ncrm_queue_init();
/** Properly releases event queue object */
void ncrm_queue_free();

/** Adds event to queue; event instance is copied once call (ownership not
 * transferred). */
int ncrm_enqueue( struct ncrm_Event * );
/** For non-empty queue, a callback will be called for each event (with user
 * data provided). Event queue is cleared. */
int ncrm_do_with_events( void(*)(struct ncrm_Event *, void*), void *);

#endif  /* H_NCRM_QUEUE_H */
