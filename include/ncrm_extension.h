#ifndef H_NCRM_EXTENSION_H
#define H_NCRM_EXTENSION_H

struct ncrm_Event;

/**\brief Represents extension of monitoring app.
 *
 * Extension are shown as switchable tabs composed in multiple windows+panels.
 * Besides of this, extension some lifetime logic (possibly asynchronious):
 *  1. init() -- allocates resources based on app's configuration
 *  2. redraw() -- shall update content of the windows/panels
 *  3. shutdown() -- frees resources at the end of lifetime
 * */
struct ncrm_Extension {
    /** An unique name to be shown in header as a tab name */
    char * name;

    /** A key switch to be used in combination with <ctrl> to switch to the
     * tab of this extension. */
    char keyswitch;

    /** Own extension's data */
    void * userData;

    /** Invoked at startup. May create listener thread's, allocate datas, etc. */
    int (*init)(struct ncrm_Extension *, struct ncrm_Model *);
    /** Invoked to update GUI content of a tab */
    int (*update)(struct ncrm_Extension *, struct ncrm_Event *);
    /** Invoked at application shutdown */
    int (*shutdown)(struct ncrm_Extension *);
};

#endif  /* H_NCRM_EXTENSION_H */
