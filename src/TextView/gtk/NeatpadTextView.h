//
//	NeatpadTextView - the TextView editor as a GTK widget. Include this file
//	when you want one: its API is C, as GTK's is.
//
//	Signals, each with no arguments:
//
//		changed				the text was changed
//		cursor-changed		the caret moved: neatpad_text_view_get_cursor says where
//		selection-changed
//		edit-mode-changed	insert, overwrite or read-only (MODE_*)
//		index-changed		more of a large file was indexed
//
#ifndef NEATPAD_TEXT_VIEW_INCLUDED
#define NEATPAD_TEXT_VIEW_INCLUDED

#include <gtk/gtk.h>

#include "../TextViewDefs.h"
#include "../../TextDocument/codepages.h"

G_BEGIN_DECLS

#define NEATPAD_TYPE_TEXT_VIEW (neatpad_text_view_get_type())

G_DECLARE_FINAL_TYPE(NeatpadTextView, neatpad_text_view, NEATPAD, TEXT_VIEW, GtkWidget)

GtkWidget *neatpad_text_view_new(void);

// Open a file, in the encoding its byte-order mark says (or UTF-8, or ANSI)
gboolean neatpad_text_view_open_file(NeatpadTextView *view, const char *filename);

// Follow a file as another program appends to it: open it read-only, at its
// end. update_tail adds what's been appended since, keeping a caret at the
// end there; FALSE if the file has got shorter, and needs opening again.
gboolean neatpad_text_view_tail_file(NeatpadTextView *view, const char *filename);
gboolean neatpad_text_view_update_tail(NeatpadTextView *view);

// Start again with an empty, unnamed document
void neatpad_text_view_new_file(NeatpadTextView *view);

// Save to a file, in the encoding it was opened in, with its byte-order mark if
// it had one
gboolean neatpad_text_view_save_file(NeatpadTextView *view, const char *filename);

// Recovery: the document's unsaved state, kept in a folder of its own, and
// the file it's for (NULL or "" for none). preserve_sources keeps what the
// state refers to in the file, before a save replaces it.
gboolean neatpad_text_view_save_state(NeatpadTextView *view, const char *directory, const char *filename,
                                      gboolean preserve_sources);

// Restore state saved in a folder: the file it's for (g_free it), and whether
// a failure was because that file has changed or gone
gboolean neatpad_text_view_load_state(NeatpadTextView *view, const char *directory, char **filename,
                                      gboolean *original_unavailable);

// Whether the text differs from the file it was opened from or saved to
gboolean neatpad_text_view_get_modified(NeatpadTextView *view);

// The document's encoding, to show: "UTF-8", "UTF-16 LE", ...
const char *neatpad_text_view_get_encoding(NeatpadTextView *view);

// The line endings new lines get, TXL_LF, TXL_CR or TXL_CRLF: the file's own
guint neatpad_text_view_get_line_format(NeatpadTextView *view);

// The document's size and the selection's: bytes, and characters (UTF-16
// units, as Windows counts them) once they're counted, and the TXL_* kinds of
// line ending found so far
typedef struct
{
    guint64  bytes;
    guint64  chars;
    guint64  selected_bytes;
    guint64  selected_chars;
    gboolean chars_known;
    guint    line_endings_seen;
} NeatpadDocStats;

gboolean neatpad_text_view_get_doc_stats(NeatpadTextView *view, NeatpadDocStats *stats);

// How far the file is indexed, as a percentage, and whether indexing is going
// on; and starting or stopping it
void neatpad_text_view_get_index_info(NeatpadTextView *view, guint *percent, gboolean *indexing);
void neatpad_text_view_index_file(NeatpadTextView *view, gboolean index);

// Find text from the caret, wrapping round the end, and select it
typedef enum
{
    NEATPAD_FIND_NOT_FOUND,
    NEATPAD_FIND_FOUND,
    NEATPAD_FIND_WRAPPED
} NeatpadFindResult;

NeatpadFindResult neatpad_text_view_find(NeatpadTextView *view, const char *text, gboolean backwards,
                                         gboolean match_case);

// Replace the selection if it's a match for text, or every match: how many
guint64 neatpad_text_view_replace(NeatpadTextView *view, const char *text, const char *replacement,
                                  gboolean match_case, gboolean all);

// Move the caret to the start of a line, from 0. FALSE if the document hasn't
// that line, or hasn't indexed as far yet.
gboolean neatpad_text_view_goto_line(NeatpadTextView *view, guint64 line);

// The font, as Pango describes one: "Monospace 11"
void neatpad_text_view_set_font(NeatpadTextView *view, const char *font);

// TXS_* styles: those in mask become those in styles
void neatpad_text_view_set_style(NeatpadTextView *view, guint mask, guint styles);

// Characters from one tab stop to the next
void neatpad_text_view_set_tab_width(NeatpadTextView *view, guint width);

// The colours: GNOME's Adwaita or Adwaita Dark editor scheme, with the
// selection in the accent colour (Adwaita blue when accent is NULL)
void neatpad_text_view_set_scheme(NeatpadTextView *view, gboolean dark, const GdkRGBA *accent);

// The caret's line (from 0; an estimate past the indexed part of a large file)
// and its column, in characters from the line's start
void neatpad_text_view_get_cursor(NeatpadTextView *view, guint64 *line, guint64 *column, gboolean *line_known);

gboolean neatpad_text_view_undo(NeatpadTextView *view);
gboolean neatpad_text_view_redo(NeatpadTextView *view);
gboolean neatpad_text_view_can_undo(NeatpadTextView *view);
gboolean neatpad_text_view_can_redo(NeatpadTextView *view);

void neatpad_text_view_cut(NeatpadTextView *view);
void neatpad_text_view_copy(NeatpadTextView *view);
void neatpad_text_view_paste(NeatpadTextView *view);
void neatpad_text_view_select_all(NeatpadTextView *view);

G_END_DECLS

#endif
