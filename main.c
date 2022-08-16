#include "ncrm_model.h"

#include <panel.h>
#include <assert.h>

#include <string.h>
#include <stdlib.h>

#define NCRM_MAX_STATUSBAR_TXT_LEN 128

/* A draft for curses-based pipeline monitoring application
 *
 *
____________________________/ handlers /_calibrations_
 Handler         Time       | Handler type: Histogram1D
 Histogram1D     1.1%       | Name: ""
 Histogram1D     2.3%       | Pointer: 0x36eaf12
 Histogram2D     3.1%       | 
-- na64swpipe (running) -- [ filters: none ] ---------
 00:00:12 I Initialized
 00:01:03 I Loaded module foo
 00:01:15 ? Some vomit from third party library
            possibly multiline
-- [######...] 47.3%, 02:34 eps, ext ~05:31 ---------
 Press h for keycodes reference
 *
 * Note: currently, only progress/journal messages supported.
 * */

/* Array of special attributes */
attr_t gSpecialAttrs[] = { A_NORMAL /* 0 - Normal mode */
    , A_DIM  /* 1 - disconnected / darmant / idle / not important */
    , A_BOLD | COLOR_PAIR(1) /* 2 - warning */
    , A_BOLD | COLOR_PAIR(2) /* 3 - error */
    , A_BLINK  /* 4 - requires (immediate) attention / fatal error */
};


/**\brief Represents extension to monitoring app.
 * Extension are shown as switchable tabs composed in multiple windows+panels
 * */
struct ncrm_Extension {
    char * name;
    // void * data; ... TODO ?
};

static struct App {
    /** Model show */
    struct ncrm_Model * model;
    
    uint16_t lines, columns;

    /** A window/panel showing tabs. It is always of full width and
     * of 1 height. It is always visible and can not be cycled. */
    WINDOW * w_tabsHeader;
    PANEL * p_tabsHeader;
    /** A window/panel showing current app name, app status and progress
     * info. It is always of full width and of 1 height. Always visible and
     * can not be cycled */
    WINDOW * w_statusFooter;
    PANEL * p_statusFooter;

    /** Null-terminated set of extensions */
    struct ncrm_Extension ** extensions;
    /** Number of active extension */
    int8_t nActiveExtension;
} gApp;

void
init_wins() {
    /* Create the "tabs header" window */
    gApp.w_tabsHeader = newwin( 1   /* # of lines in win */
                              , gApp.columns  /* # of columns in win */
                              , 0  /* Y of LT corner */
                              , 0  /* X of LT corner */
                              );
    /* Create the "status footer" window */
    gApp.w_statusFooter = newwin( 1   /* # of lines in win */
                                , gApp.columns  /* # of columns in win */
                                , gApp.lines - 1  /* Y of LT corner */
                                , 0  /* X of LT corner */
                                );
    /* Create panels for header and footer */
    gApp.p_tabsHeader   = new_panel(gApp.w_tabsHeader);
    gApp.p_statusFooter = new_panel(gApp.w_statusFooter);
}

void
update_header() {
    /* Shall produce line like
     * one \ two \ three \____
     * with all "tab headers" underlined except for one that is currently
     * "active"
     * */
    wattron(gApp.w_tabsHeader, A_BOLD );

    wattron(gApp.w_tabsHeader, A_UNDERLINE | A_DIM);
    whline(gApp.w_tabsHeader, ACS_S7, gApp.columns);
    wattroff(gApp.w_tabsHeader, A_UNDERLINE | A_DIM);

    if( !gApp.extensions ) {
        wclear(gApp.w_tabsHeader);
        wprintw(gApp.w_tabsHeader, "(no extensions loaded)");
        return;
    }
    assert(*gApp.extensions);
    /* Iterate over list of extensions collecting names */
    int8_t nTab = 0;
    for( struct ncrm_Extension ** extPtr = gApp.extensions
       ; *extPtr
       ; ++extPtr, ++nTab
       ) {
        if( nTab != gApp.nActiveExtension ) {
            wattron(gApp.w_tabsHeader, A_UNDERLINE | A_DIM);
        } else {
            wattroff(gApp.w_tabsHeader, A_UNDERLINE | A_DIM);
        }
        waddch(gApp.w_tabsHeader, '/');
        waddch(gApp.w_tabsHeader, ' ');
        if( (*extPtr)->name ) {
            wprintw(gApp.w_tabsHeader, (*extPtr)->name);
        } else {
            wprintw(gApp.w_tabsHeader, "???");
        }
        waddch(gApp.w_tabsHeader, ' ');
        waddch(gApp.w_tabsHeader, '\\');
        wattroff(gApp.w_tabsHeader, A_UNDERLINE | A_DIM);
    }
    assert(nTab);
    wattron(gApp.w_tabsHeader, A_UNDERLINE | A_DIM);
}

/*
 * Footer (progress and status bar)
 *
 * Depending on the available width and data, displays following info,
 * starting from the left corner
 * If progress is available as estimation with maximum:
 *  1 ASCII-drawn progress bar, based on available width, but no more than
 *    100 charscters long ("[####....]")
 *  2 Percentage of the progress with promille precision ("43.1%")
 *  3 Number of processed entries ( "345" )
 *  4 Elapsed time ("02:03 eps.")
 *  5 Remaining time estimation ("exp. ~ 15:23")
 *  6 Entries per second (processing speed, "34.43/s")
 * If max is not available:
 *  1 spinner ( " / " )
 *  2 Number of processed entries ("345")
 *  3 Elapsed time ("02:03 eps.")
 *  4 Entries-per-second (processing speed, "34.43/s")
 * If no processed entries available, nothing printed (even if max is set).
 */

static int _progress__progress_bar(char * bf) { 
    /* produces progress bar string, like " [#####....] " */
    assert(gApp.model);
    assert(gApp.model->maxProgress);
    /* If number of available columns shorter than 80, 14-char width is to be
     * used, otherwise 40-chars version is used. */
    const uint16_t lenver = gApp.columns > 80 ? 40 : 14;
    /* calc progress estimation */
    const float p = gApp.model->currentProgress / ((float) gApp.model->maxProgress);
    #if 1
    int nFilled = (int)(p*lenver)
      , nLeft = lenver - nFilled;
    assert( nFilled + nLeft + 5 < NCRM_MAX_STATUSBAR_TXT_LEN );
    bf[0] = ' '; bf[1] = '[';
    memset(bf + 2, '#', nFilled);
    memset(bf + 2 + nFilled, '.', nLeft);
    bf[nFilled + nLeft + 2] = ']';
    bf[nFilled + nLeft + 3] = ' ';
    bf[nFilled + nLeft + 4] = '\0';
    return nFilled + nLeft + 4;
    #else
    // TODO: unicode version?
    #endif
}

static int _progress__percentage(char * bf) {
    /* produces progress percentage string, like " 38.1% " */
    assert(gApp.model);
    assert(gApp.model->maxProgress);
    /* calc progress estimation */
    const float p = gApp.model->currentProgress / ((float) gApp.model->maxProgress);
    return snprintf( bf, NCRM_MAX_STATUSBAR_TXT_LEN
                   , " %4.1f%%%% ", p*100);
}

static int _progress__nprocessed(char * bf) {
    /* produces progress percentage string, literally, like " 341 " or " 341/5000" */
    assert(gApp.model);
    if( gApp.model->maxProgress ) {
        return snprintf(bf, NCRM_MAX_STATUSBAR_TXT_LEN, " %lu/%lu "
                       , gApp.model->currentProgress
                       , gApp.model->maxProgress
                       );
    } else {
        return snprintf(bf, NCRM_MAX_STATUSBAR_TXT_LEN, " %lu ", gApp.model->currentProgress);
    }
}

static int _status_format_time(unsigned long timeMSec, char * bf) {
    unsigned long epsTimeSec = timeMSec / 1e3;  /* msec to sec */
    if(!epsTimeSec)
        return snprintf( bf, NCRM_MAX_STATUSBAR_TXT_LEN
                       , "%lums", timeMSec );
    int days = epsTimeSec/(24*60*60)
      , hours = (epsTimeSec - days*(24*60*60))/(60*60)
      , mins = (epsTimeSec - days*(24*60*60) - hours*(60*60) )/60
      , sec = epsTimeSec%60
      ;
    int32_t nUsed = 0;
    if( days )
        nUsed += snprintf( bf, NCRM_MAX_STATUSBAR_TXT_LEN, "%dd,", days );
    if( hours || days )
        nUsed += snprintf( bf + nUsed, (NCRM_MAX_STATUSBAR_TXT_LEN - nUsed)
                         , "%02d:", hours );
    if( mins || hours || days )
        nUsed += snprintf( bf + nUsed, (NCRM_MAX_STATUSBAR_TXT_LEN - nUsed)
                         , "%02d:", mins );
    if( sec || mins || hours || days )
        nUsed += snprintf( bf + nUsed, (NCRM_MAX_STATUSBAR_TXT_LEN - nUsed)
                         , "%02ds", sec );
    return nUsed;
}

static int _progress__elapsed(char * bf) {
    /* produces elapsed time string, like " 3d,01:02:03 eps. " (days, hours,
     * minutes, seconds) */
    assert(gApp.model);
    char sbf[NCRM_MAX_STATUSBAR_TXT_LEN];
    _status_format_time(gApp.model->elapsedTime, sbf);
    return snprintf(bf, NCRM_MAX_STATUSBAR_TXT_LEN, " %s eps. ", sbf);
}

static int _progress__remaining_time(char * bf) {
    /* produces estimated remaining time string, like " (~3d,01:02:03) " (days,
     * hours, minutes, seconds) */    
    assert(gApp.model);
    assert(gApp.model->maxProgress);
    double rate = gApp.model->elapsedTime/gApp.model->currentProgress;
    double remainingEst = (gApp.model->maxProgress - gApp.model->currentProgress)*rate;
    if(remainingEst) snprintf(bf, NCRM_MAX_STATUSBAR_TXT_LEN, " (~err) ");

    char sbf[NCRM_MAX_STATUSBAR_TXT_LEN];
    _status_format_time(remainingEst, sbf);
    return snprintf(bf, NCRM_MAX_STATUSBAR_TXT_LEN, " (~%s) ", sbf);
}

static int _progress__proc_speed(char * bf) {
    /* produces estimated processing speed " 234.1/s " or "1.23e-5/s" */
    assert(gApp.model);
    double rate = gApp.model->elapsedTime/gApp.model->currentProgress;
    rate /= 1e3;
    if(rate > 0.1) {
        return snprintf( bf, NCRM_MAX_STATUSBAR_TXT_LEN
                       , " %.2f/s ", rate);
    } else {
        return snprintf( bf, NCRM_MAX_STATUSBAR_TXT_LEN
                       , " %.2e/s ", rate);
    }
}

#if 0
const char gSpinnerSeq[] = "-\\|/";

"      "
".     "
"..    "
"...   "
" ...  "
"  ... "
"   ..."
"    .."
"     ."
#endif

static int _progress__spinner(char * bf) {
    /* produces "spinner" string reflecting ongoing activity of the
     * application. Depends on both, processed entries and elapsed time. */
    assert(gApp.model);
    /* TODO: need more info about app evaluation to provide adequate update
     * seed */
    return 0;
}

static int _progress__status_msg(char * bf) {
    /* produces status message -- literally as given in model + [], e.g.
     * running -> " [ running ]" */
    assert(gApp.model);
    if(!gApp.model->statusMessage) {
        *bf = '\0';
        return 0;
    } else {
        return snprintf( bf, NCRM_MAX_STATUSBAR_TXT_LEN
                       , " %s ", gApp.model->statusMessage
                       );
    }
}

static const struct ProgressInfoEntry {
    /* Priority and order number: in progress bar mode, in spinner mode.
     * Zero priority means that entry is not used in this mode */
    int orp[2][2];
    /* String formatting function callback */
    int (*print_callback)(char * dest);
    /* ncurses attribute to apply */
    attr_t attrs;
} gProgressInfoEntries[] = {
    /*  p-bar   spinner
     *   p  o     p   o */
    { {{ 3, 1}, {98, -1}}, _progress__progress_bar   , A_BOLD },
    { {{ 1, 2}, {98, -1}}, _progress__percentage     , A_BOLD },
    { {{ 4, 3}, { 2,  2}}, _progress__nprocessed     , A_NORMAL },
    { {{ 6, 4}, { 4,  3}}, _progress__elapsed        , A_DIM },
    { {{ 2, 5}, {98, -1}}, _progress__remaining_time , A_REVERSE | A_BOLD | A_DIM },
    { {{ 5, 6}, { 3,  4}}, _progress__proc_speed     , A_NORMAL },
    { {{98,-1}, { 1,  1}}, _progress__spinner        , A_NORMAL },
    { {{ 0, 0}, { 0,  0}}, _progress__status_msg     , A_REVERSE | A_BOLD },
    { {{99,99}, {99, -1}}, NULL }  /* sentinel */
};

struct ProgressInfoEntryHandle {
    const struct ProgressInfoEntry * pie;
    char buf[NCRM_MAX_STATUSBAR_TXT_LEN];
    int occupied;
};

static int _process__sort_by_pbar_priority(const void * a_, const void * b_) {
    const struct ProgressInfoEntryHandle * a
        = (struct ProgressInfoEntryHandle *) a_;
    const struct ProgressInfoEntryHandle * b
        = (struct ProgressInfoEntryHandle *) b_;
    if( a->pie->orp[0][0] > b->pie->orp[0][0] ) return  1;
    if( a->pie->orp[0][0] < b->pie->orp[0][0] ) return -1;
    return 0;
}

static int _process__sort_by_spinner_priority(const void * a_, const void * b_) {
    const struct ProgressInfoEntryHandle * a
        = (struct ProgressInfoEntryHandle *) a_;
    const struct ProgressInfoEntryHandle * b
        = (struct ProgressInfoEntryHandle *) b_;
    if( a->pie->orp[1][0] > b->pie->orp[1][0] ) return  1;
    if( a->pie->orp[1][0] < b->pie->orp[1][0] ) return -1;
    return 0;
}

static int _process__sort_by_pbar_order(const void * a_, const void * b_) {
    const struct ProgressInfoEntryHandle * a
        = (struct ProgressInfoEntryHandle *) a_;
    const struct ProgressInfoEntryHandle * b
        = (struct ProgressInfoEntryHandle *) b_;
    if( a->pie->orp[0][1] > b->pie->orp[0][1] ) return  1;
    if( a->pie->orp[0][1] < b->pie->orp[0][1] ) return -1;
    return 0;
}

static int _process__sort_by_spinner_order(const void * a_, const void * b_) {
    const struct ProgressInfoEntryHandle * a
        = (struct ProgressInfoEntryHandle *) a_;
    const struct ProgressInfoEntryHandle * b
        = (struct ProgressInfoEntryHandle *) b_;
    if( a->pie->orp[1][1] > b->pie->orp[1][1] ) return  1;
    if( a->pie->orp[1][1] < b->pie->orp[1][1] ) return -1;
    return 0;
}

void
update_footer( int maxlen ) {
    attr_t footerAttrs = A_DIM;
    wattrset(gApp.w_statusFooter, footerAttrs );
    whline(gApp.w_statusFooter, '/', gApp.columns);

    #if 0  // XXX, tests
    {
        char bf[NCRM_MAX_STATUSBAR_TXT_LEN] = "0s";
        assert( _status_format_time( ( 2*60*60 + 11*60 + 3 )*1000
                                   , bf) );
        wprintw(gApp.w_statusFooter, bf);
    }
    #endif

    if((!gApp.model) || !gApp.model->currentProgress) return;
    static const int nWidgets = sizeof(gProgressInfoEntries)/sizeof(*gProgressInfoEntries);
    struct ProgressInfoEntryHandle piehs[sizeof(gProgressInfoEntries)/sizeof(*gProgressInfoEntries)];
    int pIdx = gApp.model->maxProgress ? 0 : 1
      , i = 0
      ;
    /* set handles */
    for( const struct ProgressInfoEntry * pce = gProgressInfoEntries
       ; pce->print_callback
       ; ++pce, ++i ) {
        piehs[i].pie = pce;
        piehs[i].buf[0] = '\0';
        if(pce->orp[pIdx][1] == -1) {  /* widget is not used */
            piehs[i].occupied = 0;
            continue;
        }
    }
    piehs[i].pie = NULL;  /* set sentinel */
    /* sort handles according their priority */
    qsort( piehs, nWidgets - 1, sizeof(struct ProgressInfoEntryHandle)
         , gApp.model->maxProgress ? _process__sort_by_pbar_priority
                                   : _process__sort_by_spinner_priority );
    /* render strings -- all, or until available space is depleted */
    i = 0;
    int overallChars = 0;
    for( i = 0
       ; piehs[i].pie
       ; ++i ) {
        if(piehs[i].pie->orp[pIdx][1] == -1) {  /* widget is not used */
            continue;
        }
        if( overallChars < maxlen ) {
            piehs[i].occupied = piehs[i].pie->print_callback(piehs[i].buf);
            overallChars += piehs[i].occupied;
            if( overallChars >= maxlen ) {
                /* cancel this string */
                piehs[i].buf[0] = '\0';
                piehs[i].occupied = 0;
            }
        } else {
            piehs[i].buf[0] = '\0';
            piehs[i].occupied = 0;
        }
    }
    /* sort handles according their order */
    qsort( piehs, nWidgets - 1, sizeof(struct ProgressInfoEntryHandle)
         , gApp.model->maxProgress ? _process__sort_by_pbar_order
                                   : _process__sort_by_spinner_order );
    /* display rendered text */
    overallChars = 0;
    for( struct ProgressInfoEntryHandle * pieh = piehs
       ; pieh->pie
       ; ++pieh ) {
        if(pieh->occupied != strlen(pieh->buf)) {  // XXX, won't work in unicode case
            printf("len(\"%s\") != %d\n", pieh->buf, pieh->occupied);
            assert( 0 );
        }
        if( pieh->occupied && overallChars + pieh->occupied <= maxlen ) {
            overallChars += pieh->occupied;
            wattrset(gApp.w_statusFooter, pieh->pie->attrs );
            if(  piehs[i].occupied > NCRM_MAX_STATUSBAR_TXT_LEN - 3
              || piehs[i].buf[piehs[i].occupied+1] != 0xAF
              ) {
                wattrset(gApp.w_statusFooter, pieh->pie->attrs );
            } else {
                const int n = piehs[i].buf[piehs[i].occupied+2];
                assert(n < sizeof(gSpecialAttrs)/sizeof(*gSpecialAttrs ));
                wattron(gApp.w_statusFooter, pieh->pie->attrs | gSpecialAttrs[n]);
            }
            
            wprintw(gApp.w_statusFooter, pieh->buf);
        }
    }
    wattrset(gApp.w_statusFooter, footerAttrs );
}

int
main(int argc, char * argv[]) {
    { /* XXX, set mock "extensions" */
        gApp.extensions = malloc(sizeof(struct ncrm_Extension *)*5);
        gApp.extensions[4] = NULL;
        
        gApp.extensions[0] = malloc(sizeof(struct ncrm_Extension));
        gApp.extensions[0]->name = strdup("Logs");

        gApp.extensions[1] = malloc(sizeof(struct ncrm_Extension));
        gApp.extensions[1]->name = strdup("Handlers");

        gApp.extensions[2] = malloc(sizeof(struct ncrm_Extension));
        gApp.extensions[2]->name = strdup("Calibrations");

        gApp.extensions[3] = malloc(sizeof(struct ncrm_Extension));
        gApp.extensions[3]->name = strdup("Resources");

        gApp.nActiveExtension = 2;
    }

    { /* XXX, mock "model" */
        gApp.model = malloc(sizeof(struct ncrm_Model));
        gApp.model->journalEntries = NULL;
        gApp.model->currentProgress = 1563;
        gApp.model->maxProgress = 5000;
        gApp.model->elapsedTime = (5*60 + 23)*1000 + 345;
        gApp.model->statusMessage = strdup("running");
    }

    initscr();

    if( has_colors() != FALSE ) {
        use_default_colors();
        start_color();
        init_pair(1, COLOR_GREEN, -1);  /* used to show "on line" messages */
        init_pair(2, COLOR_BLUE, -1);  /* used for general information */
        init_pair(3, COLOR_WHITE, COLOR_YELLOW );  /* used for warnings */
        init_pair(4, COLOR_RED, COLOR_WHITE );  /* used for errors */
    }

    curs_set(0);
    { /* Get initial terminal size */
        int termX, termY;
        getmaxyx(stdscr, termY, termX);
        gApp.lines = termY;
        gApp.columns = termX;
    }
	cbreak();
	noecho();

    /* Initialize layout */
    init_wins();

    /* Update contetn */
    update_header();
    update_footer( gApp.columns );

    /* Update the stacking order. */
	update_panels();
	/* Show it on the screen */
	doupdate();
	
	getch();
	endwin();
}


