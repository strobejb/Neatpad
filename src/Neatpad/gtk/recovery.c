//
//	MODULE:		recovery.c
//
//	PURPOSE:	Unsaved changes kept safe, as the Win32 app keeps them
//				(win32/Recovery.c). Each document has a store, a folder in
//				~/.local/state/catch22/neatpad/recovery, holding the view's
//				saved state, created at its first unsaved edit:
//
//					lock		held (flock) while a window has the store
//					clean		the state matches its file: nothing to restore
//					session		kept on purpose as the document closed
//
//				A store without "clean" was left by a crash or kept as a
//				session, and is restored as its file opens again.
//
#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include "recovery.h"

// Save once edits pause this long, or have gone on this long; after a failed
// save, try again this much later
#define PAUSE_US     (1500 * G_TIME_SPAN_MILLISECOND)
#define MOST_US      (10 * G_TIME_SPAN_SECOND)
#define RETRY_MS     5000

struct Recovery
{
    RecoveryClient client;
    char          *store; // its folder, once created
    int            lock;  // the lock file, held, or -1
    guint64        revision;
    guint64        written;
    gint64         changed; // when the text last changed
    gint64         pending; // when it first changed since it was saved
    guint          timer;
    gboolean       warned;
    gboolean       keep; // a session, kept as the document closes
};

static char *state_folder(void)
{
    return g_build_filename(g_get_user_state_dir(), "catch22", "neatpad", NULL);
}

static char *root(void)
{
    char *folder = state_folder();
    char *path   = g_build_filename(folder, "recovery", NULL);

    g_free(folder);
    return path;
}

static char *in_store(const char *store, const char *name)
{
    return g_build_filename(store, name, NULL);
}

// The store's lock, held while it's open: FALSE if another window has it
static int lock_store(const char *store)
{
    char *path = in_store(store, "lock");
    int   fd   = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);

    g_free(path);

    if (fd >= 0 && flock(fd, LOCK_EX | LOCK_NB) != 0)
    {
        close(fd);
        fd = -1;
    }

    return fd;
}

static gboolean has_marker(const char *store, const char *name)
{
    char    *path = in_store(store, name);
    gboolean has  = g_file_test(path, G_FILE_TEST_EXISTS);

    g_free(path);
    return has;
}

static gboolean write_marker(const char *store, const char *name)
{
    char    *path = in_store(store, name);
    int      fd   = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    gboolean ok   = fd >= 0 && fsync(fd) == 0;

    if (fd >= 0)
        close(fd);

    g_free(path);
    return ok;
}

static gboolean remove_marker(const char *store, const char *name)
{
    char    *path = in_store(store, name);
    gboolean ok   = unlink(path) == 0 || errno == ENOENT;

    g_free(path);
    return ok;
}

// A store holds only files the view wrote: nothing else is followed or removed
static void remove_store(const char *store)
{
    GDir       *dir = g_dir_open(store, 0, 0);
    const char *name;

    if (dir)
    {
        while ((name = g_dir_read_name(dir)) != 0)
        {
            char       *path = in_store(store, name);
            struct stat info;

            if (lstat(path, &info) == 0 && !S_ISDIR(info.st_mode))
                unlink(path);

            g_free(path);
        }

        g_dir_close(dir);
    }

    rmdir(store);
}

static gboolean create_store(Recovery *recovery)
{
    char *folder = root();
    char *uuid   = g_uuid_string_random();
    char *store  = g_build_filename(folder, uuid, NULL);
    int   fd     = -1;

    if (g_mkdir_with_parents(folder, 0700) == 0 && mkdir(store, 0700) == 0)
    {
        fd = lock_store(store);

        if (fd < 0)
            rmdir(store);
    }

    g_free(uuid);
    g_free(folder);

    if (fd < 0)
    {
        g_free(store);
        return FALSE;
    }

    recovery->store = store;
    recovery->lock  = fd;
    return TRUE;
}

static void stop_timer(Recovery *recovery)
{
    if (recovery->timer)
    {
        g_source_remove(recovery->timer);
        recovery->timer = 0;
    }
}

// Save the view's state, and mark whether there's anything to restore
static gboolean checkpoint(Recovery *recovery, gboolean preserve_sources)
{
    gboolean modified = neatpad_text_view_get_modified(recovery->client.view);
    char    *filename;
    gboolean ok;

    if (!recovery->store && !create_store(recovery))
        return FALSE;

    if (!modified && !write_marker(recovery->store, "clean"))
        return FALSE;

    filename = recovery->client.filename(recovery->client.data);
    ok       = neatpad_text_view_save_state(recovery->client.view, recovery->store, filename, preserve_sources);
    g_free(filename);

    if (!ok || (modified && !remove_marker(recovery->store, "clean")) || !remove_marker(recovery->store, "session"))
        return FALSE;

    recovery->written = recovery->revision;
    recovery->warned  = FALSE;
    return TRUE;
}

static void schedule(Recovery *recovery, guint milliseconds);

static void failed(Recovery *recovery)
{
    if (!recovery->warned)
    {
        recovery->client.failed(recovery->client.data);
        recovery->warned = TRUE;
    }
}

static gboolean on_timer(gpointer data)
{
    Recovery *recovery = data;

    recovery->timer = 0;

    // saved, or undone back to the file
    if (!neatpad_text_view_get_modified(recovery->client.view))
    {
        if (recovery->written != recovery->revision)
            recovery_saved(recovery);

        return G_SOURCE_REMOVE;
    }

    if (recovery->revision == recovery->written)
        return G_SOURCE_REMOVE;

    if (checkpoint(recovery, FALSE))
    {
        recovery->pending = g_get_monotonic_time();
    }
    else
    {
        failed(recovery);
        schedule(recovery, RETRY_MS);
    }

    return G_SOURCE_REMOVE;
}

static void schedule(Recovery *recovery, guint milliseconds)
{
    stop_timer(recovery);
    recovery->timer = g_timeout_add(milliseconds, on_timer, recovery);
}

Recovery *recovery_new(const RecoveryClient *client)
{
    Recovery *recovery = g_new0(Recovery, 1);

    recovery->client = *client;
    recovery->lock   = -1;
    return recovery;
}

void recovery_changed(Recovery *recovery)
{
    gint64 now = g_get_monotonic_time();
    gint64 due;

    if (recovery->revision == recovery->written)
        recovery->pending = now;

    recovery->revision++;
    recovery->changed = now;

    due = MIN(recovery->changed + PAUSE_US, recovery->pending + MOST_US);
    schedule(recovery, (guint)(MAX(due - now, 0) / G_TIME_SPAN_MILLISECOND));
}

gboolean recovery_before_save(Recovery *recovery)
{
    gboolean ok = checkpoint(recovery, TRUE);

    if (!ok)
        failed(recovery);

    return ok;
}

void recovery_saved(Recovery *recovery)
{
    if (recovery->store && !write_marker(recovery->store, "clean"))
    {
        failed(recovery);
        return;
    }

    recovery->written = recovery->revision;
}

void recovery_reset(Recovery *recovery)
{
    stop_timer(recovery);

    if (recovery->store)
    {
        write_marker(recovery->store, "clean");
        close(recovery->lock);
        remove_store(recovery->store);
        g_clear_pointer(&recovery->store, g_free);
        recovery->lock = -1;
    }

    recovery->revision = 0;
    recovery->written  = 0;
    recovery->warned   = FALSE;
    recovery->keep     = FALSE;
}

gboolean recovery_keep_session(Recovery *recovery)
{
    if (!checkpoint(recovery, FALSE) || !write_marker(recovery->store, "session"))
        return FALSE;

    stop_timer(recovery);
    recovery->keep = TRUE;
    return TRUE;
}

void recovery_free(Recovery *recovery)
{
    stop_timer(recovery);

    if (recovery->keep)
    {
        close(recovery->lock);
        g_free(recovery->store);
    }
    else
    {
        recovery_reset(recovery);
    }

    g_free(recovery);
}

//
//	Restoring
//
static void recovery_store_free(gpointer data)
{
    RecoveryStore *store = data;

    g_free(store->store);
    g_free(store->filename);
    g_free(store);
}

static int newest_first(gconstpointer a, gconstpointer b)
{
    gint64 x = (*(const RecoveryStore *const *)a)->time;
    gint64 y = (*(const RecoveryStore *const *)b)->time;

    return x < y ? 1 : x > y ? -1 : 0;
}

GPtrArray *recovery_list(void)
{
    char       *folder = root();
    GDir       *dir    = g_dir_open(folder, 0, 0);
    GPtrArray  *stores = g_ptr_array_new_with_free_func(recovery_store_free);
    const char *name;

    while (dir && (name = g_dir_read_name(dir)) != 0)
    {
        char       *store = g_build_filename(folder, name, NULL);
        char       *state = in_store(store, "state.bin");
        struct stat info;
        int         fd;

        if (lstat(store, &info) != 0 || !S_ISDIR(info.st_mode) || (fd = lock_store(store)) < 0)
        {
            // not a store, or a window has it
        }
        else if (has_marker(store, "clean"))
        {
            // nothing to restore: tidy it away
            close(fd);
            remove_store(store);
        }
        else
        {
            char *filename = stat(state, &info) == 0 ? neatpad_text_view_state_filename(store) : 0;

            if (filename)
            {
                RecoveryStore *entry = g_new0(RecoveryStore, 1);

                entry->store    = g_strdup(store);
                entry->filename = *filename ? filename : 0;
                entry->time     = (gint64)info.st_mtime;
                entry->session  = has_marker(store, "session");
                g_ptr_array_add(stores, entry);

                if (!*filename)
                    g_free(filename);
            }

            close(fd);
        }

        g_free(state);
        g_free(store);
    }

    if (dir)
        g_dir_close(dir);

    g_ptr_array_sort(stores, newest_first);
    g_free(folder);
    return stores;
}

char *recovery_store_for(const char *filename)
{
    GPtrArray *stores = recovery_list();
    char      *found  = 0;

    for (guint i = 0; i < stores->len && !found; i++)
    {
        RecoveryStore *store = stores->pdata[i];

        if (store->filename && g_str_equal(store->filename, filename))
            found = g_strdup(store->store);
    }

    g_ptr_array_unref(stores);
    return found;
}

gboolean recovery_restore(Recovery *recovery, const char *store, char **filename, gboolean *session)
{
    int fd = lock_store(store);

    *filename = 0;
    *session  = FALSE;

    // another window has it, or it was saved since
    if (fd < 0 || has_marker(store, "clean"))
    {
        if (fd >= 0)
            close(fd);

        return FALSE;
    }

    *session = has_marker(store, "session");

    if (!neatpad_text_view_load_state(recovery->client.view, store, filename, 0))
    {
        close(fd);
        return FALSE;
    }

    // the document's own store goes, and this one is its now, written as far
    // as it goes
    recovery_reset(recovery);
    recovery->store   = g_strdup(store);
    recovery->lock    = fd;
    recovery->written = recovery->revision;
    remove_marker(store, "session");
    return TRUE;
}

gboolean recovery_discard(const char *store)
{
    int fd = lock_store(store);

    if (fd < 0)
        return FALSE;

    write_marker(store, "clean");
    close(fd);
    remove_store(store);
    return TRUE;
}
