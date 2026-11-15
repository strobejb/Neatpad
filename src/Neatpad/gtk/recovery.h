//
//	MODULE:		recovery.h
//
//	PURPOSE:	Unsaved changes kept safe: each document's state saved to a
//				store of its own as it's edited, kept as a session when the
//				document closes unsaved, and restored when Neatpad starts
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
//	Starting: the stores left by a crash or a session, newest first, each
//	restored into a new document's view
//
char   **recovery_stores(void);
gboolean recovery_restore(Recovery *recovery, const char *store, char **filename, gboolean *session);

// The files a session had open, saved ones (unsaved ones are stores). NULL
// sets none.
char **recovery_session_files(void);
void   recovery_set_session_files(const char *const *uris);

G_END_DECLS

#endif
