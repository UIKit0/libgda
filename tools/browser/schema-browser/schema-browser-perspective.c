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
#include "schema-browser-perspective.h"
#include "favorite-selector.h"
#include "objects-index.h"
#include "../browser-window.h"
#include "table-info.h"
#include "../support.h"

/* 
 * Main static functions 
 */
static void schema_browser_perspective_class_init (SchemaBrowserPerspectiveClass *klass);
static void schema_browser_perspective_init (SchemaBrowserPerspective *stmt);
static void schema_browser_perspective_dispose (GObject *object);

/* BrowserPerspective interface */
static void                 schema_browser_perspective_perspective_init (BrowserPerspectiveIface *iface);
static GtkActionGroup      *schema_browser_perspective_get_actions_group (BrowserPerspective *perspective);
static const gchar         *schema_browser_perspective_get_actions_ui (BrowserPerspective *perspective);
/* get a pointer to the parents to be able to call their destructor */
static GObjectClass  *parent_class = NULL;

struct _SchemaBrowserPerspectivePrivate {
	GtkWidget *notebook;
	BrowserWindow *bwin;
};

GType
schema_browser_perspective_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		static GStaticMutex registering = G_STATIC_MUTEX_INIT;
		static const GTypeInfo info = {
			sizeof (SchemaBrowserPerspectiveClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) schema_browser_perspective_class_init,
			NULL,
			NULL,
			sizeof (SchemaBrowserPerspective),
			0,
			(GInstanceInitFunc) schema_browser_perspective_init
		};

		static GInterfaceInfo perspective_info = {
                        (GInterfaceInitFunc) schema_browser_perspective_perspective_init,
			NULL,
                        NULL
                };
		
		g_static_mutex_lock (&registering);
		if (type == 0) {
			type = g_type_register_static (GTK_TYPE_VBOX, "SchemaBrowserPerspective", &info, 0);
			g_type_add_interface_static (type, BROWSER_PERSPECTIVE_TYPE, &perspective_info);
		}
		g_static_mutex_unlock (&registering);
	}
	return type;
}

static void
schema_browser_perspective_class_init (SchemaBrowserPerspectiveClass * klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);
	parent_class = g_type_class_peek_parent (klass);

	object_class->dispose = schema_browser_perspective_dispose;
}

static void
schema_browser_perspective_perspective_init (BrowserPerspectiveIface *iface)
{
	iface->i_get_actions_group = schema_browser_perspective_get_actions_group;
	iface->i_get_actions_ui = schema_browser_perspective_get_actions_ui;
}


static void
schema_browser_perspective_init (SchemaBrowserPerspective *perspective)
{
	perspective->priv = g_new0 (SchemaBrowserPerspectivePrivate, 1);
}

static void selection_changed_cb (GtkWidget *widget, const gchar *selection, SchemaBrowserPerspective *bpers);

/**
 * schema_browser_perspective_new
 *
 * Creates new #BrowserPerspective widget which 
 */
BrowserPerspective *
schema_browser_perspective_new (BrowserWindow *bwin)
{
	BrowserConnection *bcnc;
	BrowserPerspective *bpers;
	SchemaBrowserPerspective *perspective;

	bpers = (BrowserPerspective*) g_object_new (TYPE_SCHEMA_BROWSER_PERSPECTIVE, NULL);
	perspective = (SchemaBrowserPerspective*) bpers;

	perspective->priv->bwin = bwin;

	/* contents */
	GtkWidget *paned, *wid, *nb;
	bcnc = browser_window_get_connection (bwin);
	paned = gtk_hpaned_new ();
	wid = favorite_selector_new (bcnc);
	g_signal_connect (wid, "selection-changed",
			  G_CALLBACK (selection_changed_cb), bpers);
	gtk_paned_add1 (GTK_PANED (paned), wid);

	nb = gtk_notebook_new ();
	perspective->priv->notebook = nb;
	gtk_paned_add2 (GTK_PANED (paned), nb);
	gtk_notebook_set_scrollable (GTK_NOTEBOOK (nb), TRUE);
	gtk_notebook_popup_enable (GTK_NOTEBOOK (nb));

	wid = objects_index_new (bcnc);
	g_signal_connect (wid, "selection-changed",
			  G_CALLBACK (selection_changed_cb), bpers);
	gtk_paned_add2 (GTK_PANED (paned), wid);
	gtk_notebook_append_page (GTK_NOTEBOOK (nb), wid,
				  browser_make_tab_label_with_stock (_("Index"), GTK_STOCK_ABOUT, FALSE,
								     NULL));
	gtk_notebook_set_tab_reorderable (GTK_NOTEBOOK (nb), wid, TRUE);
	gtk_notebook_set_group (GTK_NOTEBOOK (nb), bcnc);

	gtk_notebook_set_menu_label (GTK_NOTEBOOK (nb), wid,
				     browser_make_tab_label_with_stock (_("Index"), GTK_STOCK_ABOUT, FALSE,
									NULL));
	gtk_box_pack_start (GTK_BOX (bpers), paned, TRUE, TRUE, 0);
	gtk_widget_show_all (paned);

	return bpers;
}

static void
close_button_clicked_cb (GtkWidget *wid, GtkWidget *page_widget)
{
	gtk_widget_destroy (page_widget);
}

static void
selection_changed_cb (GtkWidget *widget, const gchar *selection, SchemaBrowserPerspective *bpers)
{
	GdaQuarkList *ql;
	const gchar *type;
	const gchar *schema = NULL, *table = NULL, *short_name = NULL;

	ql = gda_quark_list_new_from_string (selection);
	if (ql) {
		type = gda_quark_list_find (ql, "OBJ_TYPE");
		schema = gda_quark_list_find (ql, "OBJ_SCHEMA");
		table = gda_quark_list_find (ql, "OBJ_NAME");
		short_name = gda_quark_list_find (ql, "OBJ_SHORT_NAME");
	}

	if (!type || !schema || !table) {
		if (ql)
			gda_quark_list_free (ql);
		return;
	}

	if (!strcmp (type, "table")) {
		schema_browser_perspective_display_table_info (bpers, schema, table, short_name);
	}
	else {
		gint ntabs, i;
		ntabs = gtk_notebook_get_n_pages (GTK_NOTEBOOK (bpers->priv->notebook));
		for (i = 0; i < ntabs; i++) {
			GtkWidget *child;
			child = gtk_notebook_get_nth_page (GTK_NOTEBOOK (bpers->priv->notebook), i);
			if (IS_TABLE_INFO (child)) {
				if (!strcmp (schema, table_info_get_table_schema (TABLE_INFO (child))) &&
				    !strcmp (table, table_info_get_table_name (TABLE_INFO (child)))) {
					gtk_notebook_set_current_page (GTK_NOTEBOOK (bpers->priv->notebook), i);
					return;
				}
			}
		}

		g_warning ("Non handled favorite type %s", type);
		TO_IMPLEMENT;
	}

	if (ql)
		gda_quark_list_free (ql);

	g_print ("React to selection %s\n", selection);	
}

static void
schema_browser_perspective_dispose (GObject *object)
{
	SchemaBrowserPerspective *perspective;

	g_return_if_fail (object != NULL);
	g_return_if_fail (IS_SCHEMA_BROWSER_PERSPECTIVE (object));

	perspective = SCHEMA_BROWSER_PERSPECTIVE (object);
	if (perspective->priv) {
		g_free (perspective->priv);
		perspective->priv = NULL;
	}

	/* parent class */
	parent_class->dispose (object);
}

static void
action_create_table_cb (GtkAction *action, BrowserPerspective *bpers)
{
	TO_IMPLEMENT;
}

static void
action_drop_table_cb (GtkAction *action, BrowserPerspective *bpers)
{
	TO_IMPLEMENT;
}

static void
action_rename_table_cb (GtkAction *action, BrowserPerspective *bpers)
{
	TO_IMPLEMENT;
}

static void
action_add_column_cb (GtkAction *action, BrowserPerspective *bpers)
{
	TO_IMPLEMENT;
}

static void
action_drop_column_cb (GtkAction *action, BrowserPerspective *bpers)
{
	TO_IMPLEMENT;
}

static void
action_table_contents_cb (GtkAction *action, BrowserPerspective *bpers)
{
	TO_IMPLEMENT;
}


static GtkActionEntry ui_actions[] = {
        { "Schema", NULL, "_Schema", NULL, "Schema", NULL },
        { "CREATE_TABLE", GTK_STOCK_ADD, "_New table", NULL, "Create a new table",
          G_CALLBACK (action_create_table_cb)},
        { "DROP_TABLE", GTK_STOCK_DELETE, "_Delete", NULL, "Delete the selected table",
          G_CALLBACK (action_drop_table_cb)},
        { "RENAME_TABLE", NULL, "_Rename", NULL, "Rename the selected table",
          G_CALLBACK (action_rename_table_cb)},
        { "ADD_COLUMN", NULL, "_Add column", NULL, "Add a column to the selected table",
          G_CALLBACK (action_add_column_cb)},
        { "DROP_COLUMN", NULL, "_Delete column", NULL, "Delete a column from the selected table",
          G_CALLBACK (action_drop_column_cb)},
        { "TableContents", GTK_STOCK_EDIT, "_Contents", NULL, "Display the contents of the selected table",
          G_CALLBACK (action_table_contents_cb)},
};

static const gchar *ui_actions_info =
        "<ui>"
        "  <menubar name='MenuBar'>"
        "    <placeholder name='MenuExtension'>"
        "      <menu name='Schema' action='Schema'>"
        "        <menuitem name='CREATE_TABLE' action= 'CREATE_TABLE'/>"
        "        <menuitem name='DROP_TABLE' action= 'DROP_TABLE'/>"
        "        <menuitem name='RENAME_TABLE' action= 'RENAME_TABLE'/>"
        "        <separator/>"
        "        <menuitem name='ADD_COLUMN' action= 'ADD_COLUMN'/>"
        "        <menuitem name='DROP_COLUMN' action= 'DROP_COLUMN'/>"
        "        <separator/>"
        "        <menuitem name='TableContents' action= 'TableContents'/>"
        "      </menu>"
        "    </placeholder>"
        "  </menubar>"
        "  <toolbar name='ToolBar'>"
        "    <separator/>"
        "    <toolitem action='CREATE_TABLE'/>"
        "    <toolitem action='DROP_TABLE'/>"
        "    <separator/>"
        "    <toolitem action='TableContents'/>"
        "  </toolbar>"
        "</ui>";

static GtkActionGroup *
schema_browser_perspective_get_actions_group (BrowserPerspective *bpers)
{
	GtkActionGroup *agroup;
	agroup = gtk_action_group_new ("SchemaBrowserActions");
	gtk_action_group_add_actions (agroup, ui_actions, G_N_ELEMENTS (ui_actions), bpers);

	gtk_action_set_sensitive (gtk_action_group_get_action (agroup, "CREATE_TABLE"), TRUE);
	gtk_action_set_sensitive (gtk_action_group_get_action (agroup, "DROP_TABLE"), FALSE);
	gtk_action_set_sensitive (gtk_action_group_get_action (agroup, "RENAME_TABLE"), FALSE);
	gtk_action_set_sensitive (gtk_action_group_get_action (agroup, "ADD_COLUMN"), FALSE);
	gtk_action_set_sensitive (gtk_action_group_get_action (agroup, "DROP_COLUMN"), FALSE);
	gtk_action_set_sensitive (gtk_action_group_get_action (agroup, "TableContents"), FALSE);
	
	return agroup;
}

static const gchar *
schema_browser_perspective_get_actions_ui (BrowserPerspective *bpers)
{
	return ui_actions_info;
}

/**
 * schema_browser_perspective_display_table_info
 *
 * Display (and create if necessary) a new page for the table's properties
 */
void
schema_browser_perspective_display_table_info (SchemaBrowserPerspective *bpers,
					       const gchar *table_schema,
					       const gchar *table_name,
					       const gchar *table_short_name)
{
	g_return_if_fail (IS_SCHEMA_BROWSER_PERSPECTIVE (bpers));

	gint ntabs, i;
	ntabs = gtk_notebook_get_n_pages (GTK_NOTEBOOK (bpers->priv->notebook));
	for (i = 0; i < ntabs; i++) {
		GtkWidget *child;
		child = gtk_notebook_get_nth_page (GTK_NOTEBOOK (bpers->priv->notebook), i);
		if (IS_TABLE_INFO (child)) {
			if (!strcmp (table_schema, table_info_get_table_schema (TABLE_INFO (child))) &&
			    !strcmp (table_name, table_info_get_table_name (TABLE_INFO (child)))) {
				gtk_notebook_set_current_page (GTK_NOTEBOOK (bpers->priv->notebook), i);
				return;
			}
		}
	}
	
	GtkWidget *ti;
	ti = table_info_new (browser_window_get_connection (bpers->priv->bwin), table_schema, table_name);
	if (ti) {
		GtkWidget *close_btn;
		const gchar *tab_name;
		GdkPixbuf *table_pixbuf;
		
		g_object_set (G_OBJECT (ti), "perspective", bpers, NULL);
		table_pixbuf = browser_get_pixbuf_icon (BROWSER_ICON_TABLE);
		tab_name = table_short_name ? table_short_name : table_name;
		i = gtk_notebook_append_page (GTK_NOTEBOOK (bpers->priv->notebook), ti,
					      browser_make_tab_label_with_pixbuf (tab_name,
										  table_pixbuf,
										  TRUE, &close_btn));
		g_signal_connect (close_btn, "clicked",
				  G_CALLBACK (close_button_clicked_cb), ti);
		
		gtk_widget_show (ti);
		gtk_notebook_set_menu_label (GTK_NOTEBOOK (bpers->priv->notebook), ti,
					     browser_make_tab_label_with_pixbuf (tab_name,
										 table_pixbuf,
										 FALSE, NULL));
		gtk_notebook_set_current_page (GTK_NOTEBOOK (bpers->priv->notebook), i);
		gtk_notebook_set_tab_reorderable (GTK_NOTEBOOK (bpers->priv->notebook), ti,
						  TRUE);
		gtk_notebook_set_tab_detachable (GTK_NOTEBOOK (bpers->priv->notebook), ti,
						 TRUE);
	}
}