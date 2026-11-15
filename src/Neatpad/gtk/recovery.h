//
//	MODULE:		recovery.h
//
//	PURPOSE:	Unsaved changes kept safe: each document's state saved to a
//				store of its own as it's edited, kept as a session when the
//				document closes unsaved, and restored when its file opens again
//
#ifndef NEATPAD_RECOVERY_INCLUDED
#define NEATPAD_RECOVERY_INCLUDED

#include "NeatpadTextView.h"

G_BEGIN_DECLS

typedef struct Recovery Recovery;

// What a store asks of its document's window
typedef struct
{
    NeatpadTextView *view;
    char *(*filename)(gpointer data); // the file's path, or NULL: g_free it
    void (*failed)(gpointer data);    // a store couldn't be written: say so, once
    gpointer data;
} RecoveryClient;

Recovery *recovery_new(const RecoveryClient *client);

// The store goes with its document. A kept session stays for next time.
void recovery_free(Recovery *recovery);

// The text changed: its state is saved once the edits pause
void recovery_changed(Recovery *recovery);

// Before saving over a file, keep what the state needs from it; once saved,
// the store is clean
gboolean recovery_before_save(Recovery *recovery);
void     recovery_saved(Recovery *recovery);

// Another document replaces this one: its store goes
void recovery_reset(Recovery *recovery);

// Closing with unsaved changes: keep them as a session. FALSE if they can't
// be kept, and the user must choose.
gboolean recovery_keep_session(Recovery *recovery);

//
//	Stores left by a crash, or kept as a session: each is restored as its file
//	is opened, or (untitled) as it's chosen
//
typedef struct
{
    char    *store;
    char    *filename; // the file's path, or NULL for an untitled document
    gint64   time;     // when it was last written: seconds since 1970
    gboolean session;  // kept as its window closed, rather than left by a crash
} RecoveryStore;

// The stores no window has, newest first: g_ptr_array_unref it
GPtrArray *recovery_list(void);

// The store a file's unsaved changes are in, or NULL: g_free it
char *recovery_store_for(const char *filename);

// A store restored into the document's view, which then has it: the file it's
// for (g_free it), and whether it was kept as a session
gboolean recovery_restore(Recovery *recovery, const char *store, char **filename, gboolean *session);

// A store's changes thrown away: FALSE if a window has it
gboolean recovery_discard(const char *store);

G_END_DECLS

#endif
