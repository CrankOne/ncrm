Disclaimer: a draft project.

# Number cruncher monitor application

The `ncrm` is extendable monitoring application designed to seek
and perfrom basic steering of some local or remote process. It leverages log
browsing and provides API for some basic introspection.

It is organized as ncurses-based application (therefore, it offers some kind
of a "GUI") with tabbed layout.

# Design

The main thread is responsible for event loop and GUI updating. It maintains
a global `AppConfig` instance that essentially reflects current application
state: set of active extensions, reference to *data model* and ncurses windows
and panels (there are few that does not depend on currently active *extension*)
and event queue.

## Event Loop

Main thread usually spends most of its time waiting for conditional variable
steering event queue. *Events* are key presses, content updates requestests,
etc. An event usually provokes some updates on the content shown.

## Extensions

Runtime extension define what and how has to be shown (and almost always
provides some async communication to target process). Typical lifecycle of
extension:

1. Once extension is instantiated it may initiate some (network) I/O based on
   endpoint configuration provided by app's config, setting up asynchronious
   IO callbacks to update the *model*.
2. Once ran these IO callbacks recieves data from monitored process and may
   enqueue events to the event loop to update shown content.
3. An event from event loop may trigger a *routine callback* for the extension
   that may initiate additional io.

