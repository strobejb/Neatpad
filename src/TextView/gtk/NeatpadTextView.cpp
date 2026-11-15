//
//	MODULE:		NeatpadTextView.cpp
//
//	PURPOSE:	The TextView editor as a GTK widget: input goes to the engine,
//				and the engine paints, through TextViewGtk
//
#include "NeatpadTextView.h"
#include "TextViewGtk.h"
#include "../TextViewInternal.h"

#include <string.h>

struct _NeatpadTextView
{
    GtkWidget parent_instance;

    TextViewGtk  *host;
    GtkWidget    *menu; // the context menu
    GtkIMContext *im;

    // GtkScrollable: the adjustments a GtkScrolledWindow gives, which the
    // engine sets and follows: bytes down (where the top line starts), pixels
    // across
    GtkAdjustment      *vadjust;
    GtkAdjustment      *hadjust;
    GtkScrollablePolicy vpolicy;
    GtkScrollablePolicy hpolicy;

    bool   buttonDown;
    double wheel; // smooth scrolling, until it adds up to a notch
};

G_DEFINE_FINAL_TYPE_WITH_CODE(NeatpadTextView, neatpad_text_view, GTK_TYPE_WIDGET,
                              G_IMPLEMENT_INTERFACE(GTK_TYPE_SCROLLABLE, 0))

enum
{
    PROP_0,
    PROP_HADJUSTMENT,
    PROP_VADJUSTMENT,
    PROP_HSCROLL_POLICY,
    PROP_VSCROLL_POLICY
};

// In the order of the engine's TVNOTIFY_* codes
enum
{
    SIGNAL_CURSOR_CHANGED,
    SIGNAL_SELECTION_CHANGED,
    SIGNAL_EDIT_MODE_CHANGED,
    SIGNAL_CHANGED,
    SIGNAL_INDEX_CHANGED,
    SIGNAL_COUNT
};

static guint signals[SIGNAL_COUNT];

void NeatpadTextViewNotify(GtkWidget *widget, unsigned code)
{
    if (code < SIGNAL_COUNT)
        g_signal_emit(widget, signals[code], 0);
}

TextViewGtk *NeatpadTextViewHost(GtkWidget *widget)
{
    return NEATPAD_TEXT_VIEW(widget)->host;
}

static TextView *view_of(NeatpadTextView *self)
{
    return self->host->View();
}

static void set_modifiers(NeatpadTextView *self, GtkEventController *controller)
{
    self->host->SetModifiers(gtk_event_controller_get_current_event_state(controller));
}

//
//	Keys the engine handles itself, as Win32's virtual keys: the rest are
//	text, which the input method commits
//
static uint32_t engine_key(guint keyval)
{
    switch (keyval)
    {
    case GDK_KEY_Left:
    case GDK_KEY_KP_Left:
        return TVK_LEFT;

    case GDK_KEY_Right:
    case GDK_KEY_KP_Right:
        return TVK_RIGHT;

    case GDK_KEY_Up:
    case GDK_KEY_KP_Up:
        return TVK_UP;

    case GDK_KEY_Down:
    case GDK_KEY_KP_Down:
        return TVK_DOWN;

    case GDK_KEY_Home:
    case GDK_KEY_KP_Home:
        return TVK_HOME;

    case GDK_KEY_End:
    case GDK_KEY_KP_End:
        return TVK_END;

    case GDK_KEY_Page_Up:
    case GDK_KEY_KP_Page_Up:
        return TVK_PRIOR;

    case GDK_KEY_Page_Down:
    case GDK_KEY_KP_Page_Down:
        return TVK_NEXT;

    case GDK_KEY_Insert:
    case GDK_KEY_KP_Insert:
        return TVK_INSERT;

    case GDK_KEY_Delete:
    case GDK_KEY_KP_Delete:
        return TVK_DELETE;

    case GDK_KEY_BackSpace:
        return TVK_BACK;

    default:
        return 0;
    }
}

static gboolean on_key_pressed(GtkEventControllerKey *controller, guint keyval, guint, GdkModifierType state,
                               gpointer data)
{
    NeatpadTextView *self = NEATPAD_TEXT_VIEW(data);
    TextView        *view = view_of(self);
    uint32_t         key  = engine_key(keyval);

    set_modifiers(self, GTK_EVENT_CONTROLLER(controller));

    if (key)
    {
        view->OnKeyDown(key, 0);
        return TRUE;
    }

    if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter)
    {
        view->OnChar('\r', 0);
        return TRUE;
    }

    if (keyval == GDK_KEY_Tab && (state & GDK_CONTROL_MASK) == 0)
    {
        view->OnChar('\t', 0);
        return TRUE;
    }

    if (state & GDK_CONTROL_MASK)
    {
        switch (gdk_keyval_to_lower(keyval))
        {
        case GDK_KEY_a:
            neatpad_text_view_select_all(self);
            return TRUE;

        case GDK_KEY_c:
            neatpad_text_view_copy(self);
            return TRUE;

        case GDK_KEY_x:
            neatpad_text_view_cut(self);
            return TRUE;

        case GDK_KEY_v:
            neatpad_text_view_paste(self);
            return TRUE;

        // the engine undoes on Ctrl+Z and redoes on Ctrl+Y, as does Shift+Ctrl+Z
        case GDK_KEY_z:
            view->OnKeyDown(state & GDK_SHIFT_MASK ? 'Y' : 'Z', 0);
            return TRUE;

        case GDK_KEY_y:
            view->OnKeyDown('Y', 0);
            return TRUE;
        }
    }

    return FALSE;
}

// Text, as the input method gives it
static void on_commit(GtkIMContext *, const char *text, gpointer data)
{
    view_of(NEATPAD_TEXT_VIEW(data))->OnText(text, (uint32_t)strlen(text));
}

//
//	The mouse: GTK keeps sending its motion to the widget while the button is
//	down, also outside it, so dragging a selection past the edge scrolls
//
static void on_pressed(GtkGestureClick *gesture, int presses, double x, double y, gpointer data)
{
    NeatpadTextView *self = NEATPAD_TEXT_VIEW(data);
    int              ex;
    int              ey;

    self->host->EnginePoint(x, y, &ex, &ey);
    gtk_widget_grab_focus(GTK_WIDGET(self));
    set_modifiers(self, GTK_EVENT_CONTROLLER(gesture));
    self->host->SetMousePosition(ex, ey);
    self->buttonDown = true;

    if (presses == 2)
        view_of(self)->OnLButtonDblClick(0, ex, ey);
    else
        view_of(self)->OnLButtonDown(0, ex, ey);
}

static void on_released(GtkGestureClick *gesture, int, double x, double y, gpointer data)
{
    NeatpadTextView *self = NEATPAD_TEXT_VIEW(data);
    int              ex;
    int              ey;

    self->host->EnginePoint(x, y, &ex, &ey);
    set_modifiers(self, GTK_EVENT_CONTROLLER(gesture));
    self->buttonDown = false;
    view_of(self)->OnLButtonUp(0, ex, ey);
}

static void on_motion(GtkEventControllerMotion *controller, double x, double y, gpointer data)
{
    NeatpadTextView *self = NEATPAD_TEXT_VIEW(data);
    int              ex;
    int              ey;

    self->host->EnginePoint(x, y, &ex, &ey);
    set_modifiers(self, GTK_EVENT_CONTROLLER(controller));
    self->host->SetMousePosition(ex, ey);
    view_of(self)->OnMouseMove(0, ex, ey);
}

// The wheel: a notch is 120, as on Win32. Smooth scrolling adds up to notches.
static gboolean on_scroll(GtkEventControllerScroll *controller, double, double dy, gpointer data)
{
    NeatpadTextView *self = NEATPAD_TEXT_VIEW(data);

    set_modifiers(self, GTK_EVENT_CONTROLLER(controller));
    self->wheel += dy;

    while (self->wheel >= 1 || self->wheel <= -1)
    {
        int notch = self->wheel > 0 ? 1 : -1;

        view_of(self)->OnMouseWheel(-notch * 120);
        self->wheel -= notch;
    }

    return TRUE;
}

//
//	The context menu, as Win32's: undo and redo, the clipboard, and select all,
//	each only when it would do something
//
static void on_menu_pressed(GtkGestureClick *, int, double x, double y, gpointer data)
{
    NeatpadTextView   *self      = NEATPAD_TEXT_VIEW(data);
    GtkWidget         *widget    = GTK_WIDGET(self);
    TextView          *view      = view_of(self);
    bool               selection = view->SelectionSize() > 0;
    GdkContentFormats *formats   = gdk_clipboard_get_formats(gtk_widget_get_clipboard(widget));
    GdkRectangle       at        = {(int)x, (int)y, 1, 1};

    gtk_widget_grab_focus(widget);

    gtk_widget_action_set_enabled(widget, "text.undo", view->CanUndo());
    gtk_widget_action_set_enabled(widget, "text.redo", view->CanRedo());
    gtk_widget_action_set_enabled(widget, "text.cut", selection);
    gtk_widget_action_set_enabled(widget, "text.copy", selection);
    gtk_widget_action_set_enabled(widget, "text.paste", gdk_content_formats_contain_gtype(formats, G_TYPE_STRING));
    gtk_widget_action_set_enabled(widget, "text.delete", selection);

    gtk_popover_set_pointing_to(GTK_POPOVER(self->menu), &at);
    gtk_popover_popup(GTK_POPOVER(self->menu));
}

static void on_menu_action(GtkWidget *widget, const char *action, GVariant *)
{
    NeatpadTextView *self = NEATPAD_TEXT_VIEW(widget);

    if (g_str_equal(action, "text.undo"))
        neatpad_text_view_undo(self);
    else if (g_str_equal(action, "text.redo"))
        neatpad_text_view_redo(self);
    else if (g_str_equal(action, "text.cut"))
        neatpad_text_view_cut(self);
    else if (g_str_equal(action, "text.copy"))
        neatpad_text_view_copy(self);
    else if (g_str_equal(action, "text.paste"))
        neatpad_text_view_paste(self);
    else if (g_str_equal(action, "text.delete") && view_of(self)->OnClear())
        NeatpadTextViewNotify(widget, TVNOTIFY_CHANGED);
    else if (g_str_equal(action, "text.select-all"))
        neatpad_text_view_select_all(self);
}

static GtkWidget *context_menu(GtkWidget *widget)
{
    GMenu     *model = g_menu_new();
    GMenu     *undo  = g_menu_new();
    GMenu     *edit  = g_menu_new();
    GMenu     *all   = g_menu_new();
    GtkWidget *menu;

    g_menu_append(undo, "_Undo", "text.undo");
    g_menu_append(undo, "_Redo", "text.redo");
    g_menu_append(edit, "Cu_t", "text.cut");
    g_menu_append(edit, "_Copy", "text.copy");
    g_menu_append(edit, "_Paste", "text.paste");
    g_menu_append(edit, "_Delete", "text.delete");
    g_menu_append(all, "Select _All", "text.select-all");

    g_menu_append_section(model, 0, G_MENU_MODEL(undo));
    g_menu_append_section(model, 0, G_MENU_MODEL(edit));
    g_menu_append_section(model, 0, G_MENU_MODEL(all));

    menu = gtk_popover_menu_new_from_model(G_MENU_MODEL(model));
    gtk_popover_set_has_arrow(GTK_POPOVER(menu), FALSE);
    gtk_widget_set_halign(menu, GTK_ALIGN_START);
    gtk_widget_set_parent(menu, widget);

    g_object_unref(all);
    g_object_unref(edit);
    g_object_unref(undo);
    g_object_unref(model);
    return menu;
}

static void on_focus_enter(GtkEventControllerFocus *, gpointer data)
{
    NeatpadTextView *self = NEATPAD_TEXT_VIEW(data);

    gtk_im_context_focus_in(self->im);
    view_of(self)->OnSetFocus();
}

static void on_focus_leave(GtkEventControllerFocus *, gpointer data)
{
    NeatpadTextView *self = NEATPAD_TEXT_VIEW(data);

    gtk_im_context_focus_out(self->im);
    view_of(self)->OnKillFocus();
}

static void on_vscroll(GtkAdjustment *, gpointer data)
{
    NEATPAD_TEXT_VIEW(data)->host->Scrolled(true);
}

static void on_hscroll(GtkAdjustment *, gpointer data)
{
    NEATPAD_TEXT_VIEW(data)->host->Scrolled(false);
}

//
//	GtkScrollable. Without a scrolled window the view has adjustments of its
//	own, which nothing shows.
//
static void set_adjustment(NeatpadTextView *self, bool vertical, GtkAdjustment *adjustment)
{
    GtkAdjustment **slot = vertical ? &self->vadjust : &self->hadjust;

    if (adjustment && adjustment == *slot)
        return;

    if (*slot)
    {
        g_signal_handlers_disconnect_by_data(*slot, self);
        g_object_unref(*slot);
    }

    *slot = adjustment ? adjustment : gtk_adjustment_new(0, 0, 0, 0, 0, 0);
    g_object_ref_sink(*slot);
    g_signal_connect(*slot, "value-changed", G_CALLBACK(vertical ? on_vscroll : on_hscroll), self);

    self->host->AttachScrollbars(self->vadjust, self->hadjust);

    // the engine sets the new adjustment's range as it's sized
    gtk_widget_queue_allocate(GTK_WIDGET(self));
}

static void neatpad_text_view_set_property(GObject *object, guint id, const GValue *value, GParamSpec *spec)
{
    NeatpadTextView *self = NEATPAD_TEXT_VIEW(object);

    switch (id)
    {
    case PROP_HADJUSTMENT:
        set_adjustment(self, false, GTK_ADJUSTMENT(g_value_get_object(value)));
        break;

    case PROP_VADJUSTMENT:
        set_adjustment(self, true, GTK_ADJUSTMENT(g_value_get_object(value)));
        break;

    case PROP_HSCROLL_POLICY:
        self->hpolicy = (GtkScrollablePolicy)g_value_get_enum(value);
        break;

    case PROP_VSCROLL_POLICY:
        self->vpolicy = (GtkScrollablePolicy)g_value_get_enum(value);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, id, spec);
    }
}

static void neatpad_text_view_get_property(GObject *object, guint id, GValue *value, GParamSpec *spec)
{
    NeatpadTextView *self = NEATPAD_TEXT_VIEW(object);

    switch (id)
    {
    case PROP_HADJUSTMENT:
        g_value_set_object(value, self->hadjust);
        break;

    case PROP_VADJUSTMENT:
        g_value_set_object(value, self->vadjust);
        break;

    case PROP_HSCROLL_POLICY:
        g_value_set_enum(value, self->hpolicy);
        break;

    case PROP_VSCROLL_POLICY:
        g_value_set_enum(value, self->vpolicy);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, id, spec);
    }
}

//
//	The widget: the text area. A GtkScrolledWindow round it has the scrollbars.
//
static void neatpad_text_view_measure(GtkWidget *, GtkOrientation orientation, int, int *minimum, int *natural,
                                      int *minimum_baseline, int *natural_baseline)
{
    *minimum          = 64;
    *natural          = orientation == GTK_ORIENTATION_HORIZONTAL ? 640 : 480;
    *minimum_baseline = -1;
    *natural_baseline = -1;
}

// The engine sets the adjustments' ranges for its size
static void neatpad_text_view_size_allocate(GtkWidget *widget, int width, int height, int)
{
    NeatpadTextView *self = NEATPAD_TEXT_VIEW(widget);

    self->host->Resize(width, height);
    gtk_popover_present(GTK_POPOVER(self->menu));
}

static void neatpad_text_view_snapshot(GtkWidget *widget, GtkSnapshot *snapshot)
{
    NEATPAD_TEXT_VIEW(widget)->host->Snapshot(snapshot);
}

static void neatpad_text_view_dispose(GObject *object)
{
    NeatpadTextView *self = NEATPAD_TEXT_VIEW(object);

    delete self->host;
    self->host = 0;

    g_clear_pointer(&self->menu, gtk_widget_unparent);

    for (GtkAdjustment **adjustment : {&self->vadjust, &self->hadjust})
    {
        if (*adjustment)
            g_signal_handlers_disconnect_by_data(*adjustment, self);

        g_clear_object(adjustment);
    }

    if (self->im)
        gtk_im_context_set_client_widget(self->im, 0);

    g_clear_object(&self->im);

    G_OBJECT_CLASS(neatpad_text_view_parent_class)->dispose(object);
}

static void neatpad_text_view_class_init(NeatpadTextViewClass *klass)
{
    GObjectClass   *object_class = G_OBJECT_CLASS(klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

    object_class->dispose       = neatpad_text_view_dispose;
    object_class->set_property  = neatpad_text_view_set_property;
    object_class->get_property  = neatpad_text_view_get_property;
    widget_class->measure       = neatpad_text_view_measure;
    widget_class->size_allocate = neatpad_text_view_size_allocate;
    widget_class->snapshot      = neatpad_text_view_snapshot;

    const char *names[SIGNAL_COUNT] = {"cursor-changed", "selection-changed", "edit-mode-changed", "changed",
                                       "index-changed"};

    for (int i = 0; i < SIGNAL_COUNT; i++)
        signals[i] = g_signal_new(names[i], G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, 0, 0, 0, G_TYPE_NONE, 0);

    gtk_widget_class_set_css_name(widget_class, "neatpadtextview");

    g_object_class_override_property(object_class, PROP_HADJUSTMENT, "hadjustment");
    g_object_class_override_property(object_class, PROP_VADJUSTMENT, "vadjustment");
    g_object_class_override_property(object_class, PROP_HSCROLL_POLICY, "hscroll-policy");
    g_object_class_override_property(object_class, PROP_VSCROLL_POLICY, "vscroll-policy");

    const char *actions[] = {"text.undo", "text.redo",   "text.cut",       "text.copy",
                             "text.paste", "text.delete", "text.select-all"};

    for (const char *action : actions)
        gtk_widget_class_install_action(widget_class, action, 0, on_menu_action);
}

static void neatpad_text_view_init(NeatpadTextView *self)
{
    GtkWidget          *widget = GTK_WIDGET(self);
    GtkEventController *key    = gtk_event_controller_key_new();
    GtkEventController *motion = gtk_event_controller_motion_new();
    GtkEventController *scroll = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    GtkEventController *focus  = gtk_event_controller_focus_new();
    GtkGesture         *click  = gtk_gesture_click_new();
    GtkGesture         *right  = gtk_gesture_click_new();

    gtk_widget_set_focusable(widget, TRUE);
    gtk_widget_set_cursor_from_name(widget, "text");

    self->buttonDown = false;
    self->wheel      = 0;
    self->host       = new TextViewGtk(widget);

    self->vadjust = 0;
    self->hadjust = 0;
    self->vpolicy = GTK_SCROLL_MINIMUM;
    self->hpolicy = GTK_SCROLL_MINIMUM;
    set_adjustment(self, true, 0);
    set_adjustment(self, false, 0);
    neatpad_text_view_set_scheme(self, FALSE, 0);

    self->im = gtk_im_multicontext_new();
    gtk_im_context_set_client_widget(self->im, widget);
    g_signal_connect(self->im, "commit", G_CALLBACK(on_commit), self);

    gtk_event_controller_key_set_im_context(GTK_EVENT_CONTROLLER_KEY(key), self->im);
    g_signal_connect(key, "key-pressed", G_CALLBACK(on_key_pressed), self);
    gtk_widget_add_controller(widget, key);

    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_PRIMARY);
    g_signal_connect(click, "pressed", G_CALLBACK(on_pressed), self);
    g_signal_connect(click, "released", G_CALLBACK(on_released), self);
    gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(click));

    self->menu = context_menu(widget);
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(right), GDK_BUTTON_SECONDARY);
    g_signal_connect(right, "pressed", G_CALLBACK(on_menu_pressed), self);
    gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(right));

    g_signal_connect(motion, "motion", G_CALLBACK(on_motion), self);
    gtk_widget_add_controller(widget, motion);

    g_signal_connect(scroll, "scroll", G_CALLBACK(on_scroll), self);
    gtk_widget_add_controller(widget, scroll);

    g_signal_connect(focus, "enter", G_CALLBACK(on_focus_enter), self);
    g_signal_connect(focus, "leave", G_CALLBACK(on_focus_leave), self);
    gtk_widget_add_controller(widget, focus);
}

//
//	The API
//
GtkWidget *neatpad_text_view_new(void)
{
    return GTK_WIDGET(g_object_new(NEATPAD_TYPE_TEXT_VIEW, 0));
}

gboolean neatpad_text_view_open_file(NeatpadTextView *view, const char *filename)
{
    g_return_val_if_fail(NEATPAD_IS_TEXT_VIEW(view), FALSE);

    return view_of(view)->OpenFile(filename, -1, false) != 0;
}

gboolean neatpad_text_view_tail_file(NeatpadTextView *view, const char *filename)
{
    g_return_val_if_fail(NEATPAD_IS_TEXT_VIEW(view), FALSE);

    return view_of(view)->TailFile(filename) != 0;
}

gboolean neatpad_text_view_update_tail(NeatpadTextView *view)
{
    g_return_val_if_fail(NEATPAD_IS_TEXT_VIEW(view), FALSE);

    return view_of(view)->UpdateTail();
}

void neatpad_text_view_new_file(NeatpadTextView *view)
{
    g_return_if_fail(NEATPAD_IS_TEXT_VIEW(view));

    view_of(view)->ClearFile();
    NeatpadTextViewNotify(GTK_WIDGET(view), TVNOTIFY_CHANGED);
}

gboolean neatpad_text_view_save_file(NeatpadTextView *view, const char *filename)
{
    TextDocument *doc;

    g_return_val_if_fail(NEATPAD_IS_TEXT_VIEW(view), FALSE);

    doc = view_of(view)->FileDoc();

    if (!view_of(view)->SaveFile(filename, (uint32_t)doc->getformat(), doc->has_bom(), 0))
        return FALSE;

    NeatpadTextViewNotify(GTK_WIDGET(view), TVNOTIFY_CHANGED);
    return TRUE;
}

gboolean neatpad_text_view_save_state(NeatpadTextView *view, const char *directory, const char *filename,
                                      gboolean preserve_sources)
{
    TextViewRecovery state = {};

    g_return_val_if_fail(NEATPAD_IS_TEXT_VIEW(view), FALSE);

    state.directory       = directory;
    state.filename        = filename ? filename : "";
    state.preserveSources = preserve_sources != FALSE;

    return view_of(view)->SaveState(&state);
}

gboolean neatpad_text_view_load_state(NeatpadTextView *view, const char *directory, char **filename,
                                      gboolean *original_unavailable)
{
    TextViewRecovery state = {};
    bool             ok;

    g_return_val_if_fail(NEATPAD_IS_TEXT_VIEW(view), FALSE);

    state.directory = directory;
    ok              = view_of(view)->LoadState(&state);

    if (filename)
        *filename = ok ? g_strdup(state.filename.c_str()) : 0;

    if (original_unavailable)
        *original_unavailable = state.originalUnavailable;

    if (ok)
        NeatpadTextViewNotify(GTK_WIDGET(view), TVNOTIFY_CHANGED);

    return ok;
}

gboolean neatpad_text_view_get_modified(NeatpadTextView *view)
{
    g_return_val_if_fail(NEATPAD_IS_TEXT_VIEW(view), FALSE);

    return view_of(view)->FileDoc()->modified();
}

const char *neatpad_text_view_get_encoding(NeatpadTextView *view)
{
    g_return_val_if_fail(NEATPAD_IS_TEXT_VIEW(view), "");

    switch (view_of(view)->FileDoc()->getformat())
    {
    case NCP_ASCII:
        return "ASCII";

    case NCP_UTF8:
        return "UTF-8";

    case NCP_UTF16:
        return "UTF-16 LE";

    case NCP_UTF16BE:
        return "UTF-16 BE";

    case NCP_UTF32:
        return "UTF-32 LE";

    case NCP_UTF32BE:
        return "UTF-32 BE";
    }

    return "";
}

guint neatpad_text_view_get_line_format(NeatpadTextView *view)
{
    g_return_val_if_fail(NEATPAD_IS_TEXT_VIEW(view), 0);

    return (guint)view_of(view)->LineFormat();
}

gboolean neatpad_text_view_get_doc_stats(NeatpadTextView *view, NeatpadDocStats *stats)
{
    TextViewDocStats engine = {};

    g_return_val_if_fail(NEATPAD_IS_TEXT_VIEW(view), FALSE);

    if (!view_of(view)->GetDocStats(&engine))
        return FALSE;

    stats->bytes             = engine.byte_count;
    stats->chars             = engine.char_count;
    stats->selected_bytes    = engine.sel_byte_count;
    stats->selected_chars    = engine.sel_char_count;
    stats->chars_known       = engine.char_count_known;
    stats->line_endings_seen = engine.linebreaks_seen;
    return TRUE;
}

void neatpad_text_view_get_index_info(NeatpadTextView *view, guint *percent, gboolean *indexing)
{
    TextViewIndexInfo info = {};

    g_return_if_fail(NEATPAD_IS_TEXT_VIEW(view));

    view_of(view)->FillIndexInfo(&info);

    if (percent)
        *percent = info.nPercent;

    if (indexing)
        *indexing = info.fIndexing;
}

void neatpad_text_view_index_file(NeatpadTextView *view, gboolean index)
{
    g_return_if_fail(NEATPAD_IS_TEXT_VIEW(view));

    view_of(view)->IndexFile(index != FALSE);
}

NeatpadFindResult neatpad_text_view_find(NeatpadTextView *view, const char *text, gboolean backwards,
                                         gboolean match_case)
{
    TextViewFind find = {};

    g_return_val_if_fail(NEATPAD_IS_TEXT_VIEW(view), NEATPAD_FIND_NOT_FOUND);

    find.text      = text;
    find.length    = (uint32_t)strlen(text);
    find.backwards = backwards != FALSE;
    find.wrap      = true;
    find.matchcase = match_case != FALSE;

    switch (view_of(view)->FindText(&find))
    {
    case TVFR_FOUND:
        return NEATPAD_FIND_FOUND;

    case TVFR_FOUNDWRAPPED:
        return NEATPAD_FIND_WRAPPED;

    default:
        return NEATPAD_FIND_NOT_FOUND;
    }
}

guint64 neatpad_text_view_replace(NeatpadTextView *view, const char *text, const char *replacement,
                                  gboolean match_case, gboolean all)
{
    TextViewFind find = {};

    g_return_val_if_fail(NEATPAD_IS_TEXT_VIEW(view), 0);

    find.text          = text;
    find.length        = (uint32_t)strlen(text);
    find.matchcase     = match_case != FALSE;
    find.replace       = replacement;
    find.replacelength = (uint32_t)strlen(replacement);

    return view_of(view)->ReplaceText(&find, all != FALSE);
}

gboolean neatpad_text_view_goto_line(NeatpadTextView *view, guint64 line)
{
    g_return_val_if_fail(NEATPAD_IS_TEXT_VIEW(view), FALSE);

    if (view_of(view)->GotoLine(line, true) != TDR_OK)
        return FALSE;

    gtk_widget_grab_focus(GTK_WIDGET(view));
    return TRUE;
}

void neatpad_text_view_set_font(NeatpadTextView *view, const char *font)
{
    g_return_if_fail(NEATPAD_IS_TEXT_VIEW(view));

    view->host->SetFont(font);
}

void neatpad_text_view_set_tab_width(NeatpadTextView *view, guint width)
{
    g_return_if_fail(NEATPAD_IS_TEXT_VIEW(view));

    view_of(view)->SetTabWidth(width);
}

// A colour part way from one to another: 0 is from, 1 is to
static uint32_t mix(uint32_t from, uint32_t to, double amount)
{
    uint32_t mixed = 0;

    for (int shift = 0; shift < 24; shift += 8)
    {
        double a = (from >> shift) & 0xff;
        double b = (to >> shift) & 0xff;

        mixed |= (uint32_t)(a + (b - a) * amount + 0.5) << shift;
    }

    return mixed;
}

//
//	The colours of GtkSourceView's Adwaita and Adwaita Dark schemes, as GNOME
//	Text Editor shows text. A selection is the accent colour a quarter over the
//	background, as libadwaita selects text, and a tenth of the text's colour
//	when the view isn't focused.
//
void neatpad_text_view_set_scheme(NeatpadTextView *view, gboolean dark, const GdkRGBA *accent)
{
    TextView *engine = 0;
    uint32_t  background;
    uint32_t  text;
    uint32_t  highlight;
    uint32_t  current;

    g_return_if_fail(NEATPAD_IS_TEXT_VIEW(view));

    engine     = view_of(view);
    background = dark ? TV_RGB(0x1d, 0x1d, 0x20) : TV_RGB(0xff, 0xff, 0xff);
    text       = dark ? TV_RGB(0xc0, 0xbf, 0xbc) : TV_RGB(0x50, 0x4e, 0x55);
    current    = dark ? TV_RGB(0x24, 0x24, 0x28) : TV_RGB(0xf6, 0xf5, 0xf4);
    highlight  = accent ? TV_RGB((int)(accent->red * 255 + 0.5), (int)(accent->green * 255 + 0.5),
                                 (int)(accent->blue * 255 + 0.5))
                        : TV_RGB(0x35, 0x84, 0xe4);

    const struct
    {
        uint32_t index;
        uint32_t colour;
    } colours[] = {
        {TXC_FOREGROUND, text},
        {TXC_BACKGROUND, background},
        {TXC_HIGHLIGHTTEXT, text},
        {TXC_HIGHLIGHT, mix(background, highlight, 0.25)},
        {TXC_HIGHLIGHTTEXT2, text},
        {TXC_HIGHLIGHT2, mix(background, text, 0.1)},
        {TXC_SELMARGIN1, background},
        {TXC_SELMARGIN2, background},
        {TXC_LINENUMBERTEXT, dark ? TV_RGB(0x5e, 0x5e, 0x5e) : TV_RGB(0xb0, 0xaf, 0xac)},
        {TXC_LINENUMBER, background},
        {TXC_LONGLINETEXT, text},
        {TXC_LONGLINE, current},
        {TXC_CURRENTLINETEXT, text},
        {TXC_CURRENTLINE, current},
        {TXC_SEARCHMATCHTEXT, dark ? TV_RGB(0x24, 0x24, 0x24) : TV_RGB(0x3d, 0x38, 0x46)},
        {TXC_SEARCHMATCH, dark ? mix(background, TV_RGB(0xf6, 0xd3, 0x2d), 0.5) : TV_RGB(0xf8, 0xe4, 0x5c)},
        {TXC_CHANGEDTEXT, dark ? TV_RGB(0xf6, 0x61, 0x51) : TV_RGB(0xc0, 0x1c, 0x28)},
        {TXC_CHANGEDLINE, dark ? TV_RGB(0x62, 0xa0, 0xea) : TV_RGB(0x35, 0x84, 0xe4)},
        {TXC_CHANGEDLINEBG, current},
    };

    for (const auto &colour : colours)
        engine->SetColour(colour.index, colour.colour);

    view->host->SetPalette(background, text, dark ? TV_RGB(0xc0, 0xbf, 0xbc) : TV_RGB(0x77, 0x76, 0x7b));
}

void neatpad_text_view_set_style(NeatpadTextView *view, guint mask, guint styles)
{
    g_return_if_fail(NEATPAD_IS_TEXT_VIEW(view));

    view_of(view)->SetStyle(mask, styles);
    gtk_widget_queue_draw(GTK_WIDGET(view));
}

void neatpad_text_view_get_cursor(NeatpadTextView *view, guint64 *line, guint64 *column, gboolean *line_known)
{
    TextViewCursorInfo info;

    g_return_if_fail(NEATPAD_IS_TEXT_VIEW(view));

    view_of(view)->FillCursorInfo(&info);

    if (line)
        *line = info.nLineNo;

    if (column)
        *column = view_of(view)->CursorCharColumn();

    if (line_known)
        *line_known = info.fLineNoKnown;
}

// Undo and redo, telling whoever listens that the text changed
static gboolean undone(NeatpadTextView *view, bool done)
{
    if (done)
        NeatpadTextViewNotify(GTK_WIDGET(view), TVNOTIFY_CHANGED);

    return done;
}

gboolean neatpad_text_view_undo(NeatpadTextView *view)
{
    g_return_val_if_fail(NEATPAD_IS_TEXT_VIEW(view), FALSE);

    return undone(view, view_of(view)->Undo());
}

gboolean neatpad_text_view_redo(NeatpadTextView *view)
{
    g_return_val_if_fail(NEATPAD_IS_TEXT_VIEW(view), FALSE);

    return undone(view, view_of(view)->Redo());
}

gboolean neatpad_text_view_can_undo(NeatpadTextView *view)
{
    g_return_val_if_fail(NEATPAD_IS_TEXT_VIEW(view), FALSE);

    return view_of(view)->CanUndo();
}

gboolean neatpad_text_view_can_redo(NeatpadTextView *view)
{
    g_return_val_if_fail(NEATPAD_IS_TEXT_VIEW(view), FALSE);

    return view_of(view)->CanRedo();
}

void neatpad_text_view_cut(NeatpadTextView *view)
{
    g_return_if_fail(NEATPAD_IS_TEXT_VIEW(view));

    undone(view, view_of(view)->OnCut());
}

void neatpad_text_view_copy(NeatpadTextView *view)
{
    g_return_if_fail(NEATPAD_IS_TEXT_VIEW(view));

    view_of(view)->OnCopy();
}

// GTK reads the clipboard asynchronously: the text is pasted once it's here
static void on_clipboard_text(GObject *source, GAsyncResult *result, gpointer data)
{
    NeatpadTextView *view = NEATPAD_TEXT_VIEW(data);
    char            *text = gdk_clipboard_read_text_finish(GDK_CLIPBOARD(source), result, 0);

    if (text && view->host)
    {
        view->host->Paste(text);
        NeatpadTextViewNotify(GTK_WIDGET(view), TVNOTIFY_CHANGED);
    }

    g_free(text);
    g_object_unref(view);
}

void neatpad_text_view_paste(NeatpadTextView *view)
{
    g_return_if_fail(NEATPAD_IS_TEXT_VIEW(view));

    gdk_clipboard_read_text_async(gtk_widget_get_clipboard(GTK_WIDGET(view)), 0, on_clipboard_text,
                                  g_object_ref(view));
}

void neatpad_text_view_select_all(NeatpadTextView *view)
{
    g_return_if_fail(NEATPAD_IS_TEXT_VIEW(view));

    view_of(view)->SelectAll();
}
