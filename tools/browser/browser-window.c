/*
 * Copyright (C) 2009 Vivien Malerba
 *
 * This Library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This Library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this Library; see the file COPYING.LIB.  If not,
 * write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <string.h>
#include <glib/gi18n-lib.h>
#include "browser-window.h"
#include <libgda/binreloc/gda-binreloc.h>
#include "login-dialog.h"
#include "browser-core.h"
#include "support.h"
#include "browser-perspective.h"
#include "browser-connection.h"
#include "browser-connections-list.h"
#include "browser-spinner.h"

/*
 * structure representing a 'tab' in a window
 *
 * a 'page' is a set of widgets created by a single BrowserPerspectiveFactory object, for a connection
 * Each is owned by a BrowserWindow
 */
typedef struct {
        BrowserWindow      *bwin; /* pointer to window the tab is in, no ref held here */
        BrowserPerspectiveFactory *factory;
        gint                page_number; /* in reference to bwin->perspectives_nb */
        BrowserPerspective  *perspective_widget;
} PerspectiveData;
#define PERSPECTIVE_DATA(x) ((PerspectiveData*)(x))
PerspectiveData *perspective_data_new (BrowserWindow *bwin, BrowserPerspectiveFactory *factory);
void         perspective_data_free (PerspectiveData *pers);

/* 
 * Main static functions 
 */
static void browser_window_class_init (BrowserWindowClass *klass);
static void browser_window_init (BrowserWindow *bwin);
static void browser_window_dispose (GObject *object);

static gboolean window_state_event (GtkWidget *widget, GdkEventWindowState *event);
static void connection_busy_cb (BrowserConnection *bcnc, gboolean is_busy, gchar *reason, BrowserWindow *bwin);


/* get a pointer to the parents to be able to call their destructor */
static GObjectClass  *parent_class = NULL;

struct _BrowserWindowPrivate {
	BrowserConnection *bcnc;
	GtkNotebook       *perspectives_nb; /* notebook used to switch between tabs, for the selector part */
        GSList            *perspectives; /* list of PerspectiveData pointers, owned here */
	guint              ui_manager_merge_id; /* for current perspective */

	GtkWidget         *spinner;
	GtkUIManager      *ui_manager;
	GtkToolbarStyle    toolbar_style;
	GtkActionGroup    *cnc_agroup; /* one GtkAction for each BrowserConnection */
	gulong             cnc_added_sigid;
	gulong             cnc_removed_sigid;

	GtkWidget         *statusbar;
	guint              cnc_statusbar_context;
};

GType
browser_window_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		static GStaticMutex registering = G_STATIC_MUTEX_INIT;
		static const GTypeInfo info = {
			sizeof (BrowserWindowClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) browser_window_class_init,
			NULL,
			NULL,
			sizeof (BrowserWindow),
			0,
			(GInstanceInitFunc) browser_window_init
		};

		
		g_static_mutex_lock (&registering);
		if (type == 0)
			type = g_type_register_static (GTK_TYPE_WINDOW, "BrowserWindow", &info, 0);
		g_static_mutex_unlock (&registering);
	}
	return type;
}

static void
browser_window_class_init (BrowserWindowClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
	widget_class->window_state_event = window_state_event;

	parent_class = g_type_class_peek_parent (klass);

	object_class->dispose = browser_window_dispose;
}

static void
browser_window_init (BrowserWindow *bwin)
{
	bwin->priv = g_new0 (BrowserWindowPrivate, 1);
	bwin->priv->bcnc = NULL;
	bwin->priv->perspectives_nb = NULL;
	bwin->priv->perspectives = NULL;
	bwin->priv->cnc_agroup = NULL;
	bwin->priv->cnc_added_sigid = 0;
	bwin->priv->cnc_removed_sigid = 0;
}

static void
browser_window_dispose (GObject *object)
{
	BrowserWindow *bwin;

	g_return_if_fail (object != NULL);
	g_return_if_fail (BROWSER_IS_WINDOW (object));

	bwin = BROWSER_WINDOW (object);
	if (bwin->priv) {
		g_signal_handlers_disconnect_by_func (bwin->priv->bcnc, G_CALLBACK (connection_busy_cb),
						      bwin);
		if (bwin->priv->cnc_added_sigid > 0)
			g_signal_handler_disconnect (browser_core_get (), bwin->priv->cnc_added_sigid);
		if (bwin->priv->cnc_removed_sigid > 0)
			g_signal_handler_disconnect (browser_core_get (), bwin->priv->cnc_removed_sigid);
		if (bwin->priv->ui_manager)
			g_object_unref (bwin->priv->ui_manager);
		if (bwin->priv->cnc_agroup)
			g_object_unref (bwin->priv->cnc_agroup);

		if (bwin->priv->bcnc)
			g_object_unref (bwin->priv->bcnc);
		if (bwin->priv->perspectives) {
			g_slist_foreach (bwin->priv->perspectives, (GFunc) perspective_data_free, NULL);
			g_slist_free (bwin->priv->perspectives);
		}
		if (bwin->priv->perspectives_nb)
			g_object_unref (bwin->priv->perspectives_nb);
		g_free (bwin->priv);
		bwin->priv = NULL;
	}

	/* parent class */
	parent_class->dispose (object);
}


static gboolean
delete_event (GtkWidget *widget, GdkEvent *event, BrowserWindow *bwin)
{
	browser_core_close_window (bwin);
        return TRUE;
}

static void connection_added_cb (BrowserCore *bcore, BrowserConnection *bcnc, BrowserWindow *bwin);
static void connection_removed_cb (BrowserCore *bcore, BrowserConnection *bcnc, BrowserWindow *bwin);

static void quit_cb (GtkAction *action, BrowserWindow *bwin);
static void about_cb (GtkAction *action, BrowserWindow *bwin);
static void window_close_cb (GtkAction *action, BrowserWindow *bwin);
static void window_fullscreen_cb (GtkToggleAction *action, BrowserWindow *bwin);
static void window_new_cb (GtkAction *action, BrowserWindow *bwin);
static void window_new_with_cnc_cb (GtkAction *action, BrowserWindow *bwin);
static void connection_close_cb (GtkAction *action, BrowserWindow *bwin);
static void connection_open_cb (GtkAction *action, BrowserWindow *bwin);
static void connection_list_cb (GtkAction *action, BrowserWindow *bwin);
static void connection_meta_update_cb (GtkAction *action, BrowserWindow *bwin);
static void perspective_toggle_cb (GtkRadioAction *action, GtkRadioAction *current, BrowserWindow *bwin);

static const GtkToggleActionEntry ui_toggle_actions [] =
{
        { "WindowFullScreen", GTK_STOCK_FULLSCREEN, N_("_Fullscreen"), "F11", N_("Use the whole screen"), G_CALLBACK (window_fullscreen_cb), FALSE}
};

static const GtkActionEntry ui_actions[] = {
        { "Connection", NULL, "_Connection", NULL, "Connection", NULL },
        { "ConnectionOpen", GTK_STOCK_CONNECT, "_Connect", NULL, "Open a connection", G_CALLBACK (connection_open_cb)},
        { "ConnectionList", NULL, "_Connections list", NULL, "Connections list", G_CALLBACK (connection_list_cb)},
        { "ConnectionMetaSync", GTK_STOCK_REFRESH, "_Fetch meta data", NULL, "Fetch meta data", G_CALLBACK (connection_meta_update_cb)},
        { "ConnectionClose", GTK_STOCK_CLOSE, "_Close connection", NULL, "Close this connection", G_CALLBACK (connection_close_cb)},
        { "Quit", GTK_STOCK_QUIT, "_Quit", NULL, "Quit", G_CALLBACK (quit_cb)},
        { "Perspective", NULL, "_Perspective", NULL, "Perspective", NULL },
        { "Window", NULL, "_Window", NULL, "Window", NULL },
        { "WindowNew", GTK_STOCK_NEW, "_New window", NULL, "Open a new window for current connection", G_CALLBACK (window_new_cb)},
        { "WindowNewOthers", NULL, "New window for _connection", NULL, "Open a new window for a connection", NULL},
        { "WindowClose", GTK_STOCK_CLOSE, "_Close", "", "Close this window", G_CALLBACK (window_close_cb)},
        { "Help", NULL, "_Help", NULL, "Help", NULL },
        { "HelpAbout", GTK_STOCK_ABOUT, "_About", NULL, "About", G_CALLBACK (about_cb) }
};

static const gchar *ui_actions_info =
        "<ui>"
        "  <menubar name='MenuBar'>"
        "    <menu name='Connection' action='Connection'>"
        "      <menuitem name='ConnectionOpen' action= 'ConnectionOpen'/>"
        "      <menuitem name='ConnectionList' action= 'ConnectionList'/>"
        "      <menuitem name='ConnectionMetaSync' action= 'ConnectionMetaSync'/>"
        "      <separator/>"
        "      <menuitem name='ConnectionClose' action= 'ConnectionClose'/>"
        "      <menuitem name='Quit' action= 'Quit'/>"
        "      <separator/>"
        "    </menu>"
        "    <menu name='Perspective' action='Perspective'>"
        "      <placeholder name='PersList'/>"
        "    </menu>"
        "    <menu name='Window' action='Window'>"
        "      <menuitem name='WindowFullScreen' action= 'WindowFullScreen'/>"
        "      <separator/>"
        "      <menuitem name='WindowNew' action= 'WindowNew'/>"
        "      <menu name='WindowNewOthers' action='WindowNewOthers'>"
        "          <placeholder name='CncList'/>"
        "      </menu>"
        "      <separator/>"
        "      <menuitem name='WindowClose' action= 'WindowClose'/>"
        "    </menu>"
	"    <placeholder name='MenuExtension'/>"
        "    <menu name='Help' action='Help'>"
        "      <menuitem name='HelpAbout' action= 'HelpAbout'/>"
        "    </menu>"
        "  </menubar>"
        "  <toolbar  name='ToolBar'>"
        "    <toolitem action='WindowClose'/>"
        "    <toolitem action='WindowFullScreen'/>"
        "  </toolbar>"
        "</ui>";

/**
 * browser_window_new
 * @bcnc:
 * @factory: may be %NULL
 *
 * Creates a new #BrowserWindow object, and displays it
 *
 * Returns: the new object
 */
BrowserWindow*
browser_window_new (BrowserConnection *bcnc, BrowserPerspectiveFactory *factory)
{
	BrowserWindow *bwin;
	const gchar *cncname;
	gchar *str;

	g_return_val_if_fail (BROWSER_IS_CONNECTION (bcnc), NULL);

	bwin = BROWSER_WINDOW (g_object_new (BROWSER_TYPE_WINDOW, NULL));
	bwin->priv->bcnc = g_object_ref (bcnc);
	cncname = browser_connection_get_name (bcnc);
	if (!cncname)
		cncname = _("unnamed");
	str = g_strdup_printf (_("Connection: %s"), cncname);
	gtk_window_set_title (GTK_WINDOW (bwin), str);
	g_free (str);
	gtk_window_set_default_size ((GtkWindow*) bwin, 700, 600);
	g_signal_connect (G_OBJECT (bwin), "delete_event",
                          G_CALLBACK (delete_event), bwin);
	/* icon */
	GdkPixbuf *icon;
        gchar *path;
        path = gda_gbr_get_file_path (GDA_DATA_DIR, "pixmaps", "gda-browser.png", NULL);
        icon = gdk_pixbuf_new_from_file (path, NULL);
        g_free (path);
        if (icon) {
                gtk_window_set_icon (GTK_WINDOW (bwin), icon);
                g_object_unref (icon);
        }

	/* main VBox */
	GtkWidget *vbox;
	vbox = gtk_vbox_new (FALSE, 0);
        gtk_container_add (GTK_CONTAINER (bwin), vbox);
        gtk_widget_show (vbox);

	/* menu */
	GtkWidget *menubar;
        GtkWidget *toolbar;
        GtkUIManager *ui;
	GtkActionGroup *group;

        group = gtk_action_group_new ("Actions");
        gtk_action_group_add_actions (group, ui_actions, G_N_ELEMENTS (ui_actions), bwin);
	gtk_action_group_add_toggle_actions (group, ui_toggle_actions, G_N_ELEMENTS (ui_toggle_actions), bwin);

        ui = gtk_ui_manager_new ();
        gtk_ui_manager_insert_action_group (ui, group, 0);
        gtk_ui_manager_add_ui_from_string (ui, ui_actions_info, -1, NULL);
	bwin->priv->ui_manager = ui;

	GtkAccelGroup *accel_group;
        accel_group = gtk_ui_manager_get_accel_group (ui);
	gtk_window_add_accel_group (GTK_WINDOW (bwin), accel_group);

        menubar = gtk_ui_manager_get_widget (ui, "/MenuBar");
        gtk_box_pack_start (GTK_BOX (vbox), menubar, FALSE, FALSE, 0);
        gtk_widget_show (menubar);

        toolbar = gtk_ui_manager_get_widget (ui, "/ToolBar");
        gtk_box_pack_start (GTK_BOX (vbox), toolbar, FALSE, TRUE, 0);
	gtk_toolbar_set_show_arrow (GTK_TOOLBAR (toolbar), TRUE);
        gtk_widget_show (toolbar);
	bwin->priv->toolbar_style = gtk_toolbar_get_style (GTK_TOOLBAR (toolbar));

	GtkToolItem *ti;
	GtkWidget *spinner;

	ti = gtk_separator_tool_item_new ();
	gtk_separator_tool_item_set_draw (GTK_SEPARATOR_TOOL_ITEM (ti), FALSE);
	gtk_tool_item_set_expand (ti, TRUE);
	gtk_toolbar_insert (GTK_TOOLBAR (toolbar), ti, -1);
        gtk_widget_show (GTK_WIDGET (ti));

	spinner = browser_spinner_new ();
	ti = gtk_tool_item_new  ();
	gtk_container_add (GTK_CONTAINER (ti), spinner);
	gtk_toolbar_insert (GTK_TOOLBAR (toolbar), ti, -1);
        gtk_widget_show_all (GTK_WIDGET (ti));
	bwin->priv->spinner = spinner;
	gchar *reason;
	if (browser_connection_is_busy (bwin->priv->bcnc, &reason)) {
		browser_spinner_start (BROWSER_SPINNER (spinner));
		gtk_widget_set_tooltip_text (bwin->priv->spinner, reason);
		g_free (reason);
	}
	g_signal_connect (bwin->priv->bcnc, "busy",
			  G_CALLBACK (connection_busy_cb), bwin);

	guint mid;
	GSList *connections, *list;
	mid = gtk_ui_manager_new_merge_id (bwin->priv->ui_manager);

	bwin->priv->cnc_agroup = gtk_action_group_new ("CncActions");
	connections = browser_core_get_connections ();
	for (list = connections; list; list = list->next)
		connection_added_cb (browser_core_get(), BROWSER_CONNECTION (list->data), bwin);
	g_slist_free (connections);

	gtk_ui_manager_insert_action_group (bwin->priv->ui_manager, bwin->priv->cnc_agroup, 0);
	bwin->priv->cnc_added_sigid = g_signal_connect (browser_core_get (), "connection-added",
							G_CALLBACK (connection_added_cb), bwin);
	bwin->priv->cnc_removed_sigid = g_signal_connect (browser_core_get (), "connection-removed",
							  G_CALLBACK (connection_removed_cb), bwin);


	/* create a PerspectiveData */
	PerspectiveData *pers;
	pers = perspective_data_new (bwin, factory);
	bwin->priv->perspectives = g_slist_prepend (bwin->priv->perspectives, pers);
	GtkActionGroup *actions;
	actions = browser_perspective_get_actions_group (BROWSER_PERSPECTIVE (pers->perspective_widget));
	if (actions) {
		gtk_ui_manager_insert_action_group (bwin->priv->ui_manager, actions, 0);
		g_object_unref (actions);
	}
	const gchar *ui_info;
	ui_info = browser_perspective_get_actions_ui (BROWSER_PERSPECTIVE (pers->perspective_widget));
	if (ui_info)
		bwin->priv->ui_manager_merge_id = gtk_ui_manager_add_ui_from_string (bwin->priv->ui_manager,
										     ui_info, -1, NULL);
	
	/* insert perspective into window */
        bwin->priv->perspectives_nb = (GtkNotebook*) gtk_notebook_new ();
        g_object_ref (bwin->priv->perspectives_nb);
        gtk_notebook_set_show_tabs (bwin->priv->perspectives_nb, FALSE);
	gtk_container_add (GTK_CONTAINER (vbox), (GtkWidget*) bwin->priv->perspectives_nb);

	pers->page_number = gtk_notebook_append_page (bwin->priv->perspectives_nb,
						      GTK_WIDGET (pers->perspective_widget), NULL);
        gtk_widget_show_all ((GtkWidget*) bwin->priv->perspectives_nb);

	/* build the perspectives menu */
	GtkActionGroup *agroup;
	const GSList *plist;
	GSList *radio_group = NULL;

	mid = gtk_ui_manager_new_merge_id (bwin->priv->ui_manager);
	agroup = gtk_action_group_new ("Perspectives");
	gtk_ui_manager_insert_action_group (bwin->priv->ui_manager, agroup, 0);
	g_object_unref (agroup);

	GtkAction *active_action = NULL;
	for (plist = browser_core_get_factories (); plist; plist = plist->next) {
		GtkAction *action;
		const gchar *name;
		
		name = BROWSER_PERSPECTIVE_FACTORY (plist->data)->perspective_name;
		action = GTK_ACTION (gtk_radio_action_new (name, name, NULL, NULL, FALSE));

		if (!active_action && 
		    ((factory && (BROWSER_PERSPECTIVE_FACTORY (plist->data) == factory)) ||
		     (!factory && (BROWSER_PERSPECTIVE_FACTORY (plist->data) == browser_core_get_default_factory ()))))
			active_action = action;
		gtk_action_group_add_action (agroup, action);
		
		gtk_radio_action_set_group (GTK_RADIO_ACTION (action), radio_group);
		radio_group = gtk_radio_action_get_group (GTK_RADIO_ACTION (action));

		g_object_set_data (G_OBJECT (action), "pers", plist->data);
		g_signal_connect (action, "changed",
				  G_CALLBACK (perspective_toggle_cb), bwin);

		g_object_unref (action);

		gtk_ui_manager_add_ui (bwin->priv->ui_manager, mid,  "/MenuBar/Perspective/PersList",
				       name, name,
				       GTK_UI_MANAGER_AUTO, FALSE);
	}
	
	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (active_action), TRUE);

	/* statusbar */
	bwin->priv->statusbar = gtk_statusbar_new ();
	gtk_box_pack_start (GTK_BOX (vbox), bwin->priv->statusbar, FALSE, FALSE, 0);
        gtk_widget_show (bwin->priv->statusbar);
	bwin->priv->cnc_statusbar_context = gtk_statusbar_get_context_id (GTK_STATUSBAR (bwin->priv->statusbar),
									  "cncbusy");

        gtk_widget_show (GTK_WIDGET (bwin));

	return bwin;
}

static void
perspective_toggle_cb (GtkRadioAction *action, GtkRadioAction *current, BrowserWindow *bwin)
{
	BrowserPerspectiveFactory *pf;
	GSList *list;
	PerspectiveData *pers;
	if (action != current)
		return;

	pf = BROWSER_PERSPECTIVE_FACTORY (g_object_get_data (G_OBJECT (action), "pers"));
	g_assert (pf);

	/* check if perspective already exists */
	for (list = bwin->priv->perspectives, pers = NULL; list; list = list->next) {
		if (PERSPECTIVE_DATA (list->data)->factory == pf) {
			pers = PERSPECTIVE_DATA (list->data);
			break;
		}
	}

	if (!pers) {
		pers = perspective_data_new (bwin, pf);
		bwin->priv->perspectives = g_slist_prepend (bwin->priv->perspectives, pers);
		pers->page_number = gtk_notebook_append_page (bwin->priv->perspectives_nb,
							      GTK_WIDGET (pers->perspective_widget), NULL);
		gtk_widget_show_all ((GtkWidget*) bwin->priv->perspectives_nb);

		GtkActionGroup *actions;
		actions = browser_perspective_get_actions_group (BROWSER_PERSPECTIVE (pers->perspective_widget));
		if (actions) {
			gtk_ui_manager_insert_action_group (bwin->priv->ui_manager, actions, 0);
			g_object_unref (actions);
		}
	}

	gtk_notebook_set_current_page (bwin->priv->perspectives_nb, pers->page_number);


	/* menus and toolbar handling */
	if (bwin->priv->ui_manager_merge_id > 0) {
		gtk_ui_manager_remove_ui (bwin->priv->ui_manager, bwin->priv->ui_manager_merge_id);
		bwin->priv->ui_manager_merge_id = 0;
	}

	const gchar *ui_info;
	ui_info = browser_perspective_get_actions_ui (BROWSER_PERSPECTIVE (pers->perspective_widget));
	if (ui_info)
		bwin->priv->ui_manager_merge_id = gtk_ui_manager_add_ui_from_string (bwin->priv->ui_manager,
										     ui_info, -1, NULL);
}

static void
connection_busy_cb (BrowserConnection *bcnc, gboolean is_busy, gchar *reason, BrowserWindow *bwin)
{
	if (is_busy) {
		browser_spinner_start (BROWSER_SPINNER (bwin->priv->spinner));
		gtk_widget_set_tooltip_text (bwin->priv->spinner, reason);
		gtk_statusbar_push (GTK_STATUSBAR (bwin->priv->statusbar),
				    bwin->priv->cnc_statusbar_context,
				    reason);
	}
	else {
		browser_spinner_stop (BROWSER_SPINNER (bwin->priv->spinner));
		gtk_widget_set_tooltip_text (bwin->priv->spinner, NULL);
		gtk_statusbar_pop (GTK_STATUSBAR (bwin->priv->statusbar),
				   bwin->priv->cnc_statusbar_context);
	}
}

/* update @bwin->priv->cnc_agroup and @bwin->priv->ui_manager */
static void
connection_added_cb (BrowserCore *bcore, BrowserConnection *bcnc, BrowserWindow *bwin)
{
	GtkAction *action;
	const gchar *cncname;
	guint mid;

	mid = gtk_ui_manager_new_merge_id (bwin->priv->ui_manager);
	cncname = browser_connection_get_name (bcnc);
	action = gtk_action_new (cncname, cncname, NULL, NULL);
	gtk_action_group_add_action (bwin->priv->cnc_agroup, action);
	guint *amid = g_new (guint, 1);
	*amid = mid;
	g_object_set_data_full (G_OBJECT (action), "mid", amid, g_free);
	
	gtk_ui_manager_add_ui (bwin->priv->ui_manager, mid,  "/MenuBar/Window/WindowNewOthers/CncList",
			       cncname, cncname,
			       GTK_UI_MANAGER_AUTO, FALSE);	
	g_signal_connect (action, "activate",
			  G_CALLBACK (window_new_with_cnc_cb), bwin);
	g_object_set_data (G_OBJECT (action), "bcnc", bcnc);
	g_object_unref (action);
}

/* update @bwin->priv->cnc_agroup and @bwin->priv->ui_manager */
static void
connection_removed_cb (BrowserCore *bcore, BrowserConnection *bcnc, BrowserWindow *bwin)
{
	GtkAction *action;
	gchar *path;
	const gchar *cncname;
	guint *mid;

	cncname = browser_connection_get_name (bcnc);
	path = g_strdup_printf ("/MenuBar/Window/WindowNewOthers/CncList/%s",cncname);
	action = gtk_ui_manager_get_action (bwin->priv->ui_manager, path);
	g_free (path);
	g_assert (action);

	mid = g_object_get_data (G_OBJECT (action), "mid");
	g_assert (mid);
	
	gtk_ui_manager_remove_ui (bwin->priv->ui_manager, *mid);
	gtk_action_group_remove_action (bwin->priv->cnc_agroup, action);
}


static void
connection_close_cb (GtkAction *action, BrowserWindow *bwin)
{
	/* confirmation dialog */
	GtkWidget *dialog;
	char *str;
	BrowserConnection *bcnc;
	bcnc = browser_window_get_connection (bwin);
		
	str = g_strdup_printf (_("Do you want to close the '%s' connection?"),
			       browser_connection_get_name (bcnc));
	dialog = gtk_message_dialog_new_with_markup (GTK_WINDOW (bwin), GTK_DIALOG_MODAL,
						     GTK_MESSAGE_QUESTION,
						     GTK_BUTTONS_YES_NO,
						     "<b>%s</b>", str);
	g_free (str);

	if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_YES) {
		/* actual connection closing */
		browser_core_close_connection (bcnc);

		/* list all the windows using bwin's connection */
		GSList *list, *windows, *bwin_list = NULL;

		windows = browser_core_get_windows ();
		for (list = windows; list; list = list->next) {
			if (browser_window_get_connection (BROWSER_WINDOW (list->data)) == bcnc)
				bwin_list = g_slist_prepend (bwin_list, list->data);
		}
		g_slist_free (windows);

		for (list = bwin_list; list; list = list->next)
			delete_event (NULL, NULL, BROWSER_WINDOW (list->data));
		g_slist_free (bwin_list);
	}
	gtk_widget_destroy (dialog);
}

static void
quit_cb (GtkAction *action, BrowserWindow *bwin)
{
	/* confirmation dialog */
	GtkWidget *dialog;
	GSList *connections;

	connections = browser_core_get_connections ();
	if (connections && connections->next)
		dialog = gtk_message_dialog_new_with_markup (GTK_WINDOW (bwin), GTK_DIALOG_MODAL,
							     GTK_MESSAGE_QUESTION,
							     GTK_BUTTONS_YES_NO,
							     "<b>%s</b>\n<small>%s</small>",
							     _("Do you want to quit the application?"),
							     _("all the connections will be closed."));
	else
		dialog = gtk_message_dialog_new_with_markup (GTK_WINDOW (bwin), GTK_DIALOG_MODAL,
							     GTK_MESSAGE_QUESTION,
							     GTK_BUTTONS_YES_NO,
							     "<b>%s</b>\n<small>%s</small>",
							     _("Do you want to quit the application?"),
							     _("the connection will be closed."));
	

	if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_YES) {
		/* actual connection closing */
		GSList *list;
		for (list = connections; list; list = list->next)
			browser_core_close_connection (BROWSER_CONNECTION (list->data));

		/* list all the windows using bwin's connection */
		GSList *windows;
		windows = browser_core_get_windows ();
		for (list = windows; list; list = list->next)
			delete_event (NULL, NULL, BROWSER_WINDOW (list->data));
		g_slist_free (windows);
	}
	g_slist_free (connections);
	gtk_widget_destroy (dialog);	
}

static void
window_close_cb (GtkAction *action, BrowserWindow *bwin)
{
	delete_event (NULL, NULL, bwin);
}

static void
window_fullscreen_cb (GtkToggleAction *action, BrowserWindow *bwin)
{
	if (gtk_toggle_action_get_active (action))
		gtk_window_fullscreen (GTK_WINDOW (bwin));
	else
		gtk_window_unfullscreen (GTK_WINDOW (bwin));
}

static gboolean
window_state_event (GtkWidget *widget, GdkEventWindowState *event)
{
	BrowserWindow *bwin = BROWSER_WINDOW (widget);
	gboolean (* window_state_event) (GtkWidget *, GdkEventWindowState *);
	window_state_event = GTK_WIDGET_CLASS (parent_class)->window_state_event;

	/* calling parent's method */
	if (window_state_event)
                window_state_event (widget, event);

	if (event->changed_mask & GDK_WINDOW_STATE_FULLSCREEN) {
                gboolean fullscreen;
		GtkWidget *wid;

		wid = gtk_ui_manager_get_widget (bwin->priv->ui_manager, "/ToolBar");

                fullscreen = event->new_window_state & GDK_WINDOW_STATE_FULLSCREEN;
		if (fullscreen) {
			gtk_toolbar_set_style (GTK_TOOLBAR (wid), GTK_TOOLBAR_ICONS);
			browser_spinner_set_size (BROWSER_SPINNER (bwin->priv->spinner), GTK_ICON_SIZE_LARGE_TOOLBAR);
		}
		else {
			gtk_toolbar_set_style (GTK_TOOLBAR (wid), bwin->priv->toolbar_style);
			browser_spinner_set_size (BROWSER_SPINNER (bwin->priv->spinner), GTK_ICON_SIZE_DIALOG);
		}

		wid = gtk_ui_manager_get_widget (bwin->priv->ui_manager, "/MenuBar");
		if (fullscreen)
			gtk_widget_hide (wid);
		else
			gtk_widget_show (wid);
		
		gtk_statusbar_set_has_resize_grip (GTK_STATUSBAR (bwin->priv->statusbar), !fullscreen);
        }
	return FALSE;
}

static void
window_new_cb (GtkAction *action, BrowserWindow *bwin)
{
	BrowserWindow *nbwin;
	BrowserConnection *bcnc;
	bcnc = browser_window_get_connection (bwin);
	nbwin = browser_window_new (bcnc, NULL);
	
	browser_core_take_window (nbwin);
}

static void
window_new_with_cnc_cb (GtkAction *action, BrowserWindow *bwin)
{
	BrowserWindow *nbwin;
	BrowserConnection *bcnc;
	bcnc = g_object_get_data (G_OBJECT (action), "bcnc");
	g_return_if_fail (BROWSER_IS_CONNECTION (bcnc));
	
	nbwin = browser_window_new (bcnc, NULL);
	
	browser_core_take_window (nbwin);
}

static void
connection_open_cb (GtkAction *action, BrowserWindow *bwin)
{
	browser_connection_open (NULL);
}

static void
connection_list_cb (GtkAction *action, BrowserWindow *bwin)
{
	browser_connections_list_show ();
}

static void
connection_meta_update_cb (GtkAction *action, BrowserWindow *bwin)
{
	browser_connection_update_meta_data (bwin->priv->bcnc);
}

static void
about_cb (GtkAction *action, BrowserWindow *bwin)
{
	GdkPixbuf *icon;
        GtkWidget *dialog;
        const gchar *authors[] = {
                "Vivien Malerba <malerba@gnome-db.org> (current maintainer)",
                NULL
        };
        const gchar *documenters[] = {
                NULL
        };
        const gchar *translator_credits = "";

        gchar *path;
        path = gda_gbr_get_file_path (GDA_DATA_DIR, "pixmaps", "gda-browser.png", NULL);
        icon = gdk_pixbuf_new_from_file (path, NULL);
        g_free (path);

        dialog = gtk_about_dialog_new ();
        gtk_about_dialog_set_name (GTK_ABOUT_DIALOG (dialog), _("Database browser"));
        gtk_about_dialog_set_version (GTK_ABOUT_DIALOG (dialog), PACKAGE_VERSION);
        gtk_about_dialog_set_copyright (GTK_ABOUT_DIALOG (dialog), "(C) 2009 GNOME Foundation");
	gtk_about_dialog_set_comments (GTK_ABOUT_DIALOG (dialog), _("Database access services for the GNOME Desktop"));
        gtk_about_dialog_set_license (GTK_ABOUT_DIALOG (dialog), "GNU General Public License");
        gtk_about_dialog_set_website (GTK_ABOUT_DIALOG (dialog), "http://www.gnome-db.org");
        gtk_about_dialog_set_authors (GTK_ABOUT_DIALOG (dialog), authors);
        gtk_about_dialog_set_documenters (GTK_ABOUT_DIALOG (dialog), documenters);
        gtk_about_dialog_set_translator_credits (GTK_ABOUT_DIALOG (dialog), translator_credits);
        gtk_about_dialog_set_logo (GTK_ABOUT_DIALOG (dialog), icon);
        g_signal_connect (G_OBJECT (dialog), "response",
                          G_CALLBACK (gtk_widget_destroy),
                          dialog);
        gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (bwin));
        gtk_widget_show (dialog);
}

/**
 * browser_window_get_connection
 */
BrowserConnection *
browser_window_get_connection (BrowserWindow *bwin)
{
	g_return_val_if_fail (BROWSER_IS_WINDOW (bwin), NULL);
	return bwin->priv->bcnc;
}


/**
 * perspective_data_new
 * @bwin: a #BrowserWindow in which the perspective will be
 * @factory: a #BrowserPerspectiveFactory, or %NULL
 *
 * Creates a new #PerspectiveData structure, it increases @bcnc's reference count.
 *
 * Returns: a new #PerspectiveData
 */
PerspectiveData *
perspective_data_new (BrowserWindow *bwin, BrowserPerspectiveFactory *factory)
{
        PerspectiveData *pers;

        pers = g_new0 (PerspectiveData, 1);
        pers->bwin = NULL;
        pers->factory = factory;
        if (!pers->factory)
                pers->factory = browser_core_get_default_factory ();
        pers->page_number = -1;
        g_assert (pers->factory);
	pers->perspective_widget = g_object_ref (pers->factory->perspective_create (bwin));

        return pers;
}

/**
 * perspective_data_free
 */
void
perspective_data_free (PerspectiveData *pers)
{
        if (pers->perspective_widget)
                g_object_unref (pers->perspective_widget);
        g_free (pers);
}

/**
 * browser_window_push_status
 */
guint
browser_window_push_status (BrowserWindow *bwin, const gchar *context, const gchar *text)
{
	guint cid;

	g_return_val_if_fail (BROWSER_IS_WINDOW (bwin), 0);
	g_return_val_if_fail (context, 0);
	g_return_val_if_fail (text, 0);
	
	cid = gtk_statusbar_get_context_id (GTK_STATUSBAR (bwin->priv->statusbar), context);
	return gtk_statusbar_push (GTK_STATUSBAR (bwin->priv->statusbar), cid, text);
}

/**
 * browser_window_pop_status
 */
void
browser_window_pop_status (BrowserWindow *bwin, const gchar *context)
{
	guint cid;

	g_return_if_fail (BROWSER_IS_WINDOW (bwin));
	g_return_if_fail (context);

	cid = gtk_statusbar_get_context_id (GTK_STATUSBAR (bwin->priv->statusbar), context);
	gtk_statusbar_pop (GTK_STATUSBAR (bwin->priv->statusbar), cid);
}