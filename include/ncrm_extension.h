#ifndef H_NCRM_EXTENSION_H
#define H_NCRM_EXTENSION_H

/**\brief Represents extension of monitoring app.
 *
 * Extension are shown as switchable tabs composed in multiple windows+panels.
 * Besides of this, extension some lifetime logic (possibly asynchronious):
 *  1. init() -- allocates resources based on app's configuration
 *  2. redraw() -- shall update content of the windows/panels
 *  3. shutdown() -- frees resources at the end of lifetime
 * */
struct ncrm_Extension {
    char * name;

    int needsUpdate:1;

    void (*init)(struct ncrm_Extension *);
    void (*update)(struct ncrm_Extension *);
    void (*shutdown)(struct ncrm_Extension *);
};

#endif  /* H_NCRM_EXTENSION_H */
