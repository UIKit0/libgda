/*
 * Copyright (C) 2009 The GNOME Foundation
 *
 * AUTHORS:
 *      Vivien Malerba <malerba@gnome-db.org>
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

#include <glib/gi18n-lib.h>
#include <string.h>
#include <gtk/gtk.h>
#include "query-console.h"
#include "../dnd.h"
#include "../support.h"
#include "../cc-gray-bar.h"
#include "query-exec-perspective.h"
#include "../browser-page.h"
#include "../browser-stock-icons.h"
#include "query-editor.h"
#include "../common/popup-container.h"
#include <libgda/sql-parser/gda-sql-parser.h>
#include <libgda-ui/libgda-ui.h>

#define VARIABLES_HELP _("This area allows to give values to\n" \
			 "variables defined in the SQL code\n"		\
			 "using the following syntax:\n"		\
			 "<b><tt>##&lt;variable name&gt;::&lt;type&gt;[::null]</tt></b>\n" \
			 "For example:\n"				\
			 "<span foreground=\"#4e9a06\"><b><tt>##id::int</tt></b></span>\n      defines <b>id</b> as a non NULL integer\n" \
			 "<span foreground=\"#4e9a06\"><b><tt>##age::string::null</tt></b></span>\n      defines <b>age</b> as a a string\n\n" \
			 "Valid types are: <tt>string</tt>, <tt>boolean</tt>, <tt>int</tt>,\n" \
			 "<tt>date</tt>, <tt>time</tt>, <tt>timestamp</tt>, <tt>guint</tt>")

struct _QueryConsolePrivate {
	BrowserConnection *bcnc;
	GdaSqlParser *parser;

	CcGrayBar *header;
	GtkWidget *vpaned; /* top=>query editor, bottom=>results */

	QueryEditor *editor;
	guint params_compute_id; /* timout ID to compute params */
	GdaSet *past_params; /* keeps values given to old params */
	GdaSet *params; /* execution params */
	GtkWidget *params_popup; /* popup shown when invalid params are required */

	GtkToggleButton *params_toggle;
	GtkWidget *params_top;
	GtkWidget *params_form_box;
	GtkWidget *params_form;
	
	QueryEditor *history;
};

static void query_console_class_init (QueryConsoleClass *klass);
static void query_console_init       (QueryConsole *tconsole, QueryConsoleClass *klass);
static void query_console_dispose   (GObject *object);
static void query_console_show_all (GtkWidget *widget);

/* BrowserPage interface */
static void                 query_console_page_init (BrowserPageIface *iface);
static GtkActionGroup      *query_console_page_get_actions_group (BrowserPage *page);
static const gchar         *query_console_page_get_actions_ui (BrowserPage *page);
static GtkWidget           *query_console_page_get_tab_label (BrowserPage *page, GtkWidget **out_close_button);

enum {
	LAST_SIGNAL
};

static guint query_console_signals[LAST_SIGNAL] = { };
static GObjectClass *parent_class = NULL;

/*
 * QueryConsole class implementation
 */

static void
query_console_class_init (QueryConsoleClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->dispose = query_console_dispose;
	GTK_WIDGET_CLASS (klass)->show_all = query_console_show_all;
}

static void
query_console_show_all (GtkWidget *widget)
{
	QueryConsole *tconsole = (QueryConsole *) widget;
	GTK_WIDGET_CLASS (parent_class)->show_all (widget);

	if (gtk_toggle_button_get_active (tconsole->priv->params_toggle))
		gtk_widget_show (tconsole->priv->params_top);
	else
		gtk_widget_hide (tconsole->priv->params_top);
}

static void
query_console_page_init (BrowserPageIface *iface)
{
	iface->i_get_actions_group = query_console_page_get_actions_group;
	iface->i_get_actions_ui = query_console_page_get_actions_ui;
	iface->i_get_tab_label = query_console_page_get_tab_label;
}

static void
query_console_init (QueryConsole *tconsole, QueryConsoleClass *klass)
{
	tconsole->priv = g_new0 (QueryConsolePrivate, 1);
	tconsole->priv->parser = NULL;
	tconsole->priv->params_compute_id = 0;
	tconsole->priv->past_params = NULL;
	tconsole->priv->params = NULL;
	tconsole->priv->params_popup = NULL;
}

static void
query_console_dispose (GObject *object)
{
	QueryConsole *tconsole = (QueryConsole *) object;

	/* free memory */
	if (tconsole->priv) {
		if (tconsole->priv->bcnc)
			g_object_unref (tconsole->priv->bcnc);
		if (tconsole->priv->parser)
			g_object_unref (tconsole->priv->parser);
		if (tconsole->priv->past_params)
			g_object_unref (tconsole->priv->past_params);
		if (tconsole->priv->params)
			g_object_unref (tconsole->priv->params);
		if (tconsole->priv->params_compute_id)
			g_source_remove (tconsole->priv->params_compute_id);
		if (tconsole->priv->params_popup)
			gtk_widget_destroy (tconsole->priv->params_popup);

		g_free (tconsole->priv);
		tconsole->priv = NULL;
	}

	parent_class->dispose (object);
}

GType
query_console_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		static const GTypeInfo console = {
			sizeof (QueryConsoleClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) query_console_class_init,
			NULL,
			NULL,
			sizeof (QueryConsole),
			0,
			(GInstanceInitFunc) query_console_init
		};

		static GInterfaceInfo page_console = {
                        (GInterfaceInitFunc) query_console_page_init,
			NULL,
                        NULL
                };

		type = g_type_register_static (GTK_TYPE_VBOX, "QueryConsole", &console, 0);
		g_type_add_interface_static (type, BROWSER_PAGE_TYPE, &page_console);
	}
	return type;
}

static GtkWidget *make_small_button (gboolean is_toggle,
				     const gchar *label, const gchar *stock_id, const gchar *tooltip);

static void editor_changed_cb (QueryEditor *editor, QueryConsole *tconsole);
static void sql_clear_clicked_cb (GtkButton *button, QueryConsole *tconsole);
static void sql_variables_clicked_cb (GtkToggleButton *button, QueryConsole *tconsole);
static void sql_execute_clicked_cb (GtkButton *button, QueryConsole *tconsole);
static void sql_indent_clicked_cb (GtkButton *button, QueryConsole *tconsole);

/**
 * query_console_new
 *
 * Returns: a new #GtkWidget
 */
GtkWidget *
query_console_new (BrowserConnection *bcnc)
{
	QueryConsole *tconsole;

	g_return_val_if_fail (BROWSER_IS_CONNECTION (bcnc), NULL);

	tconsole = QUERY_CONSOLE (g_object_new (QUERY_CONSOLE_TYPE, NULL));

	tconsole->priv->bcnc = g_object_ref (bcnc);
	
	/* header */
        GtkWidget *label;
	gchar *str;
	str = g_strdup_printf ("<b>%s</b>", _("Query editor"));
	label = cc_gray_bar_new (str);
	g_free (str);
        gtk_box_pack_start (GTK_BOX (tconsole), label, FALSE, FALSE, 0);
        gtk_widget_show (label);
	tconsole->priv->header = CC_GRAY_BAR (label);

	/* main contents */
	GtkWidget *vpaned;
	vpaned = gtk_vpaned_new ();
	tconsole->priv->vpaned = NULL;
	gtk_box_pack_start (GTK_BOX (tconsole), vpaned, TRUE, TRUE, 0);	

	/* top paned for the editor */
	GtkWidget *wid, *vbox, *hbox, *bbox, *hpaned, *button;

	hbox = gtk_hbox_new (FALSE, 0);
	gtk_paned_add1 (GTK_PANED (vpaned), hbox);

	hpaned = gtk_hpaned_new ();
	gtk_box_pack_start (GTK_BOX (hbox), hpaned, TRUE, TRUE, 0);

	vbox = gtk_vbox_new (FALSE, 0);
	gtk_paned_pack1 (GTK_PANED (hpaned), vbox, TRUE, FALSE);

	wid = gtk_label_new ("");
	str = g_strdup_printf ("<b>%s</b>", _("SQL code to execute:"));
	gtk_label_set_markup (GTK_LABEL (wid), str);
	g_free (str);
	gtk_misc_set_alignment (GTK_MISC (wid), 0., -1);
	gtk_widget_set_tooltip_text (wid, _("Enter SQL code to execute\nwhich can be specific to the database to\n"
					    "which the connection is opened"));
	gtk_box_pack_start (GTK_BOX (vbox), wid, FALSE, FALSE, 0);

	wid = query_editor_new ();
	tconsole->priv->editor = QUERY_EDITOR (wid);
	gtk_box_pack_start (GTK_BOX (vbox), wid, TRUE, TRUE, 0);
	g_signal_connect (wid, "changed",
			  G_CALLBACK (editor_changed_cb), tconsole);
	gtk_widget_set_size_request (wid, -1, 200);
	
	vbox = gtk_vbox_new (FALSE, 0);
	tconsole->priv->params_top = vbox;
	gtk_paned_pack2 (GTK_PANED (hpaned), vbox, FALSE, TRUE);
	
	wid = gtk_label_new ("");
	str = g_strdup_printf ("<b>%s</b>", _("Variables' values:"));
	gtk_label_set_markup (GTK_LABEL (wid), str);
	g_free (str);
	gtk_misc_set_alignment (GTK_MISC (wid), 0., -1);
	gtk_box_pack_start (GTK_BOX (vbox), wid, FALSE, FALSE, 0);
	
	GtkWidget *sw;
	sw = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sw), GTK_SHADOW_NONE);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
					GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	tconsole->priv->params_form_box = gtk_viewport_new (NULL, NULL);
	gtk_viewport_set_shadow_type (GTK_VIEWPORT (tconsole->priv->params_form_box), GTK_SHADOW_NONE);
	gtk_container_add (GTK_CONTAINER (sw), tconsole->priv->params_form_box);
	gtk_box_pack_start (GTK_BOX (vbox), sw, TRUE, TRUE, 0);
	gtk_widget_set_size_request (tconsole->priv->params_form_box, 250, -1);

	wid = gtk_label_new ("");
	gtk_label_set_markup (GTK_LABEL (wid), VARIABLES_HELP);
	gtk_container_add (GTK_CONTAINER (tconsole->priv->params_form_box), wid);
	tconsole->priv->params_form = wid;
	
	bbox = gtk_vbutton_box_new ();
	gtk_button_box_set_layout (GTK_BUTTON_BOX (bbox), GTK_BUTTONBOX_END);
	gtk_box_pack_start (GTK_BOX (hbox), bbox, FALSE, FALSE, 5);

	button = make_small_button (FALSE, _("Clear"), GTK_STOCK_CLEAR, _("Clear the editor"));
	gtk_box_pack_start (GTK_BOX (bbox), button, FALSE, FALSE, 0);
	g_signal_connect (button, "clicked",
			  G_CALLBACK (sql_clear_clicked_cb), tconsole);

	button = make_small_button (TRUE, _("Variables"), NULL, _("Show variables needed\nto execute SQL"));
	gtk_box_pack_start (GTK_BOX (bbox), button, FALSE, FALSE, 0);
	tconsole->priv->params_toggle = GTK_TOGGLE_BUTTON (button);
	g_signal_connect (button, "toggled",
			  G_CALLBACK (sql_variables_clicked_cb), tconsole);

	button = make_small_button (FALSE, _("Execute"), GTK_STOCK_EXECUTE, _("Execute SQL in editor"));
	gtk_box_pack_start (GTK_BOX (bbox), button, FALSE, FALSE, 0);
	g_signal_connect (button, "clicked",
			  G_CALLBACK (sql_execute_clicked_cb), tconsole);
	
	button = make_small_button (FALSE, _("Indent"), GTK_STOCK_INDENT, _("Indent SQL in editor\n"
									    "and make the code more readable\n"
									    "(removes comments)"));
	gtk_box_pack_start (GTK_BOX (bbox), button, FALSE, FALSE, 0);
	g_signal_connect (button, "clicked",
			  G_CALLBACK (sql_indent_clicked_cb), tconsole);

	/* bottom paned for the results and history */
	hpaned = gtk_hpaned_new ();
	gtk_paned_add2 (GTK_PANED (vpaned), hpaned);

	/* bottom left */
	vbox = gtk_vbox_new (FALSE, 0);
	gtk_paned_pack1 (GTK_PANED (hpaned), vbox, FALSE, TRUE);

	wid = gtk_label_new ("");
	str = g_strdup_printf ("<b>%s</b>", _("Execution history:"));
	gtk_label_set_markup (GTK_LABEL (wid), str);
	g_free (str);
	gtk_misc_set_alignment (GTK_MISC (wid), 0., -1);
	gtk_box_pack_start (GTK_BOX (vbox), wid, FALSE, FALSE, 0);

	wid = query_editor_new ();
	tconsole->priv->history = QUERY_EDITOR (wid);
	query_editor_set_mode (tconsole->priv->history, QUERY_EDITOR_HISTORY);
	gtk_widget_set_size_request (wid, 200, -1);
	gtk_box_pack_start (GTK_BOX (vbox), wid, TRUE, TRUE, 0);

	bbox = gtk_vbutton_box_new ();
	gtk_box_pack_start (GTK_BOX (vbox), bbox, FALSE, FALSE, 0);

	button = make_small_button (FALSE, _("Delete"), GTK_STOCK_DELETE, _("Delete history item"));
	gtk_box_pack_start (GTK_BOX (bbox), button, FALSE, FALSE, 0);

	/* bottom right */
	vbox = gtk_vbox_new (FALSE, 0);
	gtk_paned_pack2 (GTK_PANED (hpaned), vbox, TRUE, FALSE);
	
	wid = gtk_label_new ("");
	str = g_strdup_printf ("<b>%s</b>", _("Execution Results:"));
	gtk_label_set_markup (GTK_LABEL (wid), str);
	g_free (str);
	gtk_misc_set_alignment (GTK_MISC (wid), 0., -1);
	gtk_box_pack_start (GTK_BOX (vbox), wid, FALSE, FALSE, 0);

	wid = gtk_label_new ("Here go the\nresults' form");
	gtk_box_pack_start (GTK_BOX (vbox), wid, TRUE, TRUE, 0);

	/* show everything */
        gtk_widget_show_all (vpaned);
	gtk_widget_hide (tconsole->priv->params_top);

	return (GtkWidget*) tconsole;
}

static GtkWidget *
make_small_button (gboolean is_toggle, const gchar *label, const gchar *stock_id, const gchar *tooltip)
{
	GtkWidget *button, *hbox = NULL;

	if (is_toggle)
		button = gtk_toggle_button_new ();
	else
		button = gtk_button_new ();
	if (label && stock_id) {
		hbox = gtk_hbox_new (FALSE, 0);
		gtk_container_add (GTK_CONTAINER (button), hbox);
		gtk_widget_show (hbox);
	}

	if (stock_id) {
		GtkWidget *image;
		image = gtk_image_new_from_stock (stock_id, GTK_ICON_SIZE_MENU);
		if (hbox)
			gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, FALSE, 0);
		else
			gtk_container_add (GTK_CONTAINER (button), image);
		gtk_widget_show (image);
	}
	if (label) {
		GtkWidget *wid;
		wid = gtk_label_new (label);
		if (hbox)
			gtk_box_pack_start (GTK_BOX (hbox), wid, FALSE, FALSE, 5);
		else
			gtk_container_add (GTK_CONTAINER (button), wid);
		gtk_widget_show (wid);
	}

	if (tooltip)
		gtk_widget_set_tooltip_text (button, tooltip);
	return button;
}

static gboolean
compute_params (QueryConsole *tconsole)
{
	gchar *sql;
	GdaBatch *batch;

	if (tconsole->priv->params) {
		if (tconsole->priv->past_params) {
			gda_set_merge_with_set (tconsole->priv->params, tconsole->priv->past_params);
			g_object_unref (tconsole->priv->past_params);
			tconsole->priv->past_params = tconsole->priv->params;
		}
		else
			tconsole->priv->past_params = tconsole->priv->params;
	}

	tconsole->priv->params = NULL;
	if (tconsole->priv->params_form) {
		gtk_widget_destroy (tconsole->priv->params_form);
		tconsole->priv->params_form = NULL;		
	}

	if (!tconsole->priv->parser)
		tconsole->priv->parser = browser_connection_create_parser (tconsole->priv->bcnc);

	sql = query_editor_get_all_text (tconsole->priv->editor);
	batch = gda_sql_parser_parse_string_as_batch (tconsole->priv->parser, sql, NULL, NULL);
	g_free (sql);
	if (batch) {
		GError *error = NULL;
		gboolean show_variables = FALSE;

		if (gda_batch_get_parameters (batch, &(tconsole->priv->params), &error)) {
			if (tconsole->priv->params) {
				show_variables = TRUE;
				tconsole->priv->params_form = gdaui_basic_form_new (tconsole->priv->params);
				gdaui_basic_form_show_entry_actions (GDAUI_BASIC_FORM (tconsole->priv->params_form),
								     TRUE);
			}
			else {
				tconsole->priv->params_form = gtk_label_new ("");
				gtk_label_set_markup (GTK_LABEL (tconsole->priv->params_form), VARIABLES_HELP);
			}
		}
		else {
			show_variables = TRUE;
			tconsole->priv->params_form = gtk_label_new ("Error!");
		}
		gtk_container_add (GTK_CONTAINER (tconsole->priv->params_form_box), tconsole->priv->params_form);
		gtk_widget_show (tconsole->priv->params_form);
		g_object_unref (batch);

		if (tconsole->priv->past_params && tconsole->priv->params) {
			/* copy the values from tconsole->priv->past_params to tconsole->priv->params */
			GSList *list;
			for (list = tconsole->priv->params->holders; list; list = list->next) {
				GdaHolder *oldh, *newh;
				newh = GDA_HOLDER (list->data);
				oldh = gda_set_get_holder (tconsole->priv->past_params, gda_holder_get_id  (newh));
				if (oldh) {
					GType otype, ntype;
					otype = gda_holder_get_g_type (oldh);
					ntype = gda_holder_get_g_type (newh);
					if (otype == ntype) {
						const GValue *ovalue;
						ovalue = gda_holder_get_value (oldh);
						gda_holder_set_value (newh, ovalue, NULL);
					}
					else if (g_value_type_transformable (otype, ntype)) {
						const GValue *ovalue;
						GValue *nvalue;
						ovalue = gda_holder_get_value (oldh);
						nvalue = gda_value_new (ntype);
						if (g_value_transform (ovalue, nvalue))
							gda_holder_take_value (newh, nvalue, NULL);
						else
							gda_value_free  (nvalue);
					}
				}
			}
		}

		if (show_variables && !gtk_toggle_button_get_active (tconsole->priv->params_toggle))
			gtk_toggle_button_set_active (tconsole->priv->params_toggle, TRUE);
	}
	else {
		tconsole->priv->params_form = gtk_label_new ("");
		gtk_label_set_markup (GTK_LABEL (tconsole->priv->params_form), VARIABLES_HELP);
		gtk_container_add (GTK_CONTAINER (tconsole->priv->params_form_box), tconsole->priv->params_form);
		gtk_widget_show (tconsole->priv->params_form);
	}
	
	/* remove timeout */
	tconsole->priv->params_compute_id = 0;
	return FALSE;
}

static void
editor_changed_cb (QueryEditor *editor, QueryConsole *tconsole)
{
	if (tconsole->priv->params_compute_id)
		g_source_remove (tconsole->priv->params_compute_id);
	tconsole->priv->params_compute_id = g_timeout_add_seconds (1, (GSourceFunc) compute_params, tconsole);
}
		
static void
sql_variables_clicked_cb (GtkToggleButton *button, QueryConsole *tconsole)
{
	if (gtk_toggle_button_get_active (button))
		gtk_widget_show (tconsole->priv->params_top);
	else
		gtk_widget_hide (tconsole->priv->params_top);
}

static void
sql_clear_clicked_cb (GtkButton *button, QueryConsole *tconsole)
{
	query_editor_set_text (tconsole->priv->editor, NULL);
}

static void
sql_indent_clicked_cb (GtkButton *button, QueryConsole *tconsole)
{
	gchar *sql;
	GdaBatch *batch;

	if (!tconsole->priv->parser)
		tconsole->priv->parser = browser_connection_create_parser (tconsole->priv->bcnc);

	sql = query_editor_get_all_text (tconsole->priv->editor);
	batch = gda_sql_parser_parse_string_as_batch (tconsole->priv->parser, sql, NULL, NULL);
	g_free (sql);
	if (batch) {
		GString *string;
		const GSList *stmt_list, *list;
		stmt_list = gda_batch_get_statements (batch);
		string = g_string_new ("");
		for (list = stmt_list; list; list = list->next) {
			GError *error = NULL;
			sql = gda_statement_to_sql_extended (GDA_STATEMENT (list->data), NULL, NULL,
							     GDA_STATEMENT_SQL_PRETTY |
							     GDA_STATEMENT_SQL_PARAMS_SHORT,
							     NULL, &error);
			if (!sql)
				sql = gda_statement_to_sql (GDA_STATEMENT (list->data), NULL, NULL);
			if (list != stmt_list)
				g_string_append (string, "\n\n");
			g_string_append_printf (string, "%s;\n", sql);
			g_free (sql);

		}
		g_object_unref (batch);

		query_editor_set_text (tconsole->priv->editor, string->str);
		g_string_free (string, TRUE);
	}
}

static void
popup_container_position_func (PopupContainer *cont, gint *out_x, gint *out_y)
{
	GtkWidget *console, *top;
	gint x, y;
        GtkRequisition req;

	console = g_object_get_data (G_OBJECT (cont), "console");
	top = gtk_widget_get_toplevel (console);	
        gtk_widget_size_request ((GtkWidget*) cont, &req);
        gdk_window_get_origin (top->window, &x, &y);

	x += (top->allocation.width - req.width) / 2;
	y += (top->allocation.height - req.height) / 2;

        if (x < 0)
                x = 0;

        if (y < 0)
                y = 0;

	*out_x = x;
	*out_y = y;
}

static void
params_form_changed_cb (GdauiBasicForm *form, GdaHolder *param, gboolean is_user_modif, QueryConsole *tconsole)
{
	/* if all params are valid => authorize the execute button */
	GtkWidget *button;

	button = g_object_get_data (G_OBJECT (tconsole->priv->params_popup), "exec");
	gtk_widget_set_sensitive (button,
				  gdaui_basic_form_is_valid (form));
}

static void
sql_execute_clicked_cb (GtkButton *button, QueryConsole *tconsole)
{
	gchar *sql;
	const gchar *remain;
	GdaBatch *batch;
	GError *error = NULL;

	/* compute parameters if necessary */
	if (tconsole->priv->params_compute_id > 0) {
		g_source_remove (tconsole->priv->params_compute_id);
		tconsole->priv->params_compute_id = 0;
		compute_params (tconsole);
	}

	if (tconsole->priv->params) {
		if (! gdaui_basic_form_is_valid (GDAUI_BASIC_FORM (tconsole->priv->params_form))) {
			GtkWidget *form, *cont;
			if (! tconsole->priv->params_popup) {
				tconsole->priv->params_popup = popup_container_new_with_func (popup_container_position_func);
				g_object_set_data (G_OBJECT (tconsole->priv->params_popup), "console", tconsole);

				GtkWidget *vbox, *label, *bbox, *button;
				gchar *str;
				vbox = gtk_vbox_new (FALSE, 0);
				gtk_container_add (GTK_CONTAINER (tconsole->priv->params_popup), vbox);
				gtk_container_set_border_width (GTK_CONTAINER (tconsole->priv->params_popup), 10);

				label = gtk_label_new ("");
				str = g_strdup_printf ("<b>%s</b>:\n<small>%s</small>",
						       _("Invalid variable's contents"),
						       _("assign values to the following variables"));
				gtk_label_set_markup (GTK_LABEL (label), str);
				g_free (str);
				gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

				cont = gtk_vbox_new (FALSE, 0);
				gtk_box_pack_start (GTK_BOX (vbox), cont, FALSE, FALSE, 10);
				g_object_set_data (G_OBJECT (tconsole->priv->params_popup), "cont", cont);

				bbox = gtk_hbutton_box_new ();
				gtk_box_pack_start (GTK_BOX (vbox), bbox, FALSE, FALSE, 10);
				gtk_button_box_set_layout (GTK_BUTTON_BOX (bbox), GTK_BUTTONBOX_END);
				
				button = gtk_button_new_from_stock (GTK_STOCK_EXECUTE);
				gtk_box_pack_start (GTK_BOX (bbox), button, TRUE, TRUE, 0);
				g_signal_connect_swapped (button, "clicked",
							  G_CALLBACK (gtk_widget_hide), tconsole->priv->params_popup);
				g_signal_connect (button, "clicked",
						  G_CALLBACK (sql_execute_clicked_cb), tconsole);
				gtk_widget_set_sensitive (button, FALSE);
				g_object_set_data (G_OBJECT (tconsole->priv->params_popup), "exec", button);

				button = gtk_button_new_from_stock (GTK_STOCK_CANCEL);
				gtk_box_pack_start (GTK_BOX (bbox), button, TRUE, TRUE, 0);
				g_signal_connect_swapped (button, "clicked",
							  G_CALLBACK (gtk_widget_hide), tconsole->priv->params_popup);
			}
			else {
				form = g_object_get_data (G_OBJECT (tconsole->priv->params_popup), "form");
				if (form)
					gtk_widget_destroy (form);
			}

			cont = g_object_get_data (G_OBJECT (tconsole->priv->params_popup), "cont");
			form = gdaui_basic_form_new (tconsole->priv->params);			
			gdaui_basic_form_show_entry_actions (GDAUI_BASIC_FORM (form), TRUE);
			g_signal_connect (form, "param-changed",
					  G_CALLBACK (params_form_changed_cb), tconsole);

			gtk_box_pack_start (GTK_BOX (cont), form, TRUE, TRUE, 0);
			g_object_set_data (G_OBJECT (tconsole->priv->params_popup), "form", form);
			gtk_widget_show_all (tconsole->priv->params_popup);

			return;
		}
	}

	if (!tconsole->priv->parser)
		tconsole->priv->parser = browser_connection_create_parser (tconsole->priv->bcnc);

	sql = query_editor_get_all_text (tconsole->priv->editor);
	batch = gda_sql_parser_parse_string_as_batch (tconsole->priv->parser, sql, &remain, &error);
	if (!batch) {
		browser_show_error (GTK_WINDOW (gtk_widget_get_toplevel ((GtkWidget*) tconsole)),
				    _("Error while parsing code: %s"),
				    error && error->message ? error->message : _("No detail"));
		g_clear_error (&error);
		g_free (sql);
		return;
	}
	g_free (sql);

	QueryEditorHistoryBatch *hbatch;
	GTimeVal tv;
	g_get_current_time (&tv);
	hbatch = query_editor_history_batch_new (tv);
	query_editor_start_history_batch (tconsole->priv->history, hbatch);
	query_editor_history_batch_unref (hbatch);

	const GSList *stmt_list, *list;
	stmt_list = gda_batch_get_statements (batch);
	for (list = stmt_list; list; list = list->next) {
		QueryEditorHistoryItem *history;
		GdaSqlStatement *sqlst;
		g_object_get (G_OBJECT (list->data), "structure", &sqlst, NULL);
		if (!sqlst->sql) {
			sql = gda_statement_to_sql (GDA_STATEMENT (list->data), NULL, NULL);
			history = query_editor_history_item_new (sql, NULL, NULL);
			g_free (sql);
		}
		else
			history = query_editor_history_item_new (sqlst->sql, NULL, NULL);
		gda_sql_statement_free (sqlst);

		query_editor_add_history_item (tconsole->priv->history, history);
		query_editor_history_item_unref (history);
	}
	g_object_unref (batch);
}


/*
 * UI actions
 */
static void
query_execute_cb (GtkAction *action, QueryConsole *tconsole)
{
	sql_execute_clicked_cb (NULL, tconsole);
}

#ifdef HAVE_GTKSOURCEVIEW
static void
editor_undo_cb (GtkAction *action, QueryConsole *tconsole)
{
	TO_IMPLEMENT;
}
#endif

static GtkActionEntry ui_actions[] = {
	{ "ExecuteQuery", GTK_STOCK_EXECUTE, N_("_Execute"), NULL, N_("Execute query"),
	  G_CALLBACK (query_execute_cb)},
#ifdef HAVE_GTKSOURCEVIEW
	{ "EditorUndo", GTK_STOCK_UNDO, N_("_Undo"), NULL, N_("Undo last change"),
	  G_CALLBACK (editor_undo_cb)},
#endif
};
static const gchar *ui_actions_console =
	"<ui>"
	"  <menubar name='MenuBar'>"
	"      <menu name='Edit' action='Edit'>"
        "        <menuitem name='EditorUndo' action= 'EditorUndo'/>"
        "      </menu>"
	"  </menubar>"
	"  <toolbar name='ToolBar'>"
	"    <separator/>"
	"    <toolitem action='ExecuteQuery'/>"
	"  </toolbar>"
	"</ui>";

static GtkActionGroup *
query_console_page_get_actions_group (BrowserPage *page)
{
	GtkActionGroup *agroup;
	agroup = gtk_action_group_new ("QueryExecConsoleActions");
	gtk_action_group_add_actions (agroup, ui_actions, G_N_ELEMENTS (ui_actions), page);
	
	return agroup;
}

static const gchar *
query_console_page_get_actions_ui (BrowserPage *page)
{
	return ui_actions_console;
}

static GtkWidget *
query_console_page_get_tab_label (BrowserPage *page, GtkWidget **out_close_button)
{
	QueryConsole *tconsole;
	const gchar *tab_name;

	tconsole = QUERY_CONSOLE (page);
	tab_name = _("Query editor");
	return browser_make_tab_label_with_stock (tab_name,
						  STOCK_CONSOLE,
						  out_close_button ? TRUE : FALSE, out_close_button);
}