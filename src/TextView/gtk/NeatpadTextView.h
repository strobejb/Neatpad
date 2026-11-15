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

// The font, as Pango describes one: "Monospace 11"
void neatpad_text_view_set_font(NeatpadTextView *view, const char *font);

// TXS_* styles: those in mask become those in styles
void neatpad_text_view_set_style(NeatpadTextView *view, guint mask, guint styles);

// The caret's line (from 0; an estimate past the indexed part of a large file)
// and its column, in bytes of the line's UTF-8 text
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
