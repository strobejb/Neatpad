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

G_BEGIN_DECLS

#define NEATPAD_TYPE_TEXT_VIEW (neatpad_text_view_get_type())

G_DECLARE_FINAL_TYPE(NeatpadTextView, neatpad_text_view, NEATPAD, TEXT_VIEW, GtkWidget)

GtkWidget *neatpad_text_view_new(void);

// Open a file, in the encoding its byte-order mark says (or UTF-8, or ANSI)
gboolean neatpad_text_view_open_file(NeatpadTextView *view, const char *filename);

// Start again with an empty, unnamed document
void neatpad_text_view_new_file(NeatpadTextView *view);

// Save to a file, in the encoding it was opened in, with its byte-order mark if
// it had one
gboolean neatpad_text_view_save_file(NeatpadTextView *view, const char *filename);

// Whether the text differs from the file it was opened from or saved to
gboolean neatpad_text_view_get_modified(NeatpadTextView *view);

// The document's encoding, to show: "UTF-8", "UTF-16 LE", ...
const char *neatpad_text_view_get_encoding(NeatpadTextView *view);

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
