/* ui-formgrid.c
 *
 * Copyright (C) 2010 - 2011 Vivien Malerba
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include <string.h>
#include <glib/gi18n-lib.h>
#include <libgda/libgda.h>
#include "ui-formgrid.h"
#include <libgda-ui/gdaui-data-proxy.h>
#include <libgda-ui/gdaui-raw-form.h>
#include <libgda-ui/gdaui-data-selector.h>
#include "../support.h"
#include "../browser-window.h"

static void ui_formgrid_class_init (UiFormGridClass * class);
static void ui_formgrid_init (UiFormGrid *wid);
static void ui_formgrid_dispose (GObject *object);

static void ui_formgrid_set_property (GObject *object,
				      guint param_id,
				      const GValue *value,
				      GParamSpec *pspec);
static void ui_formgrid_get_property (GObject *object,
				      guint param_id,
				      GValue *value,
				      GParamSpec *pspec);
static BrowserConnection *get_browser_connection (UiFormGrid *formgrid);

struct _UiFormGridPriv
{
	GtkWidget   *nb;
	GtkWidget   *sw;
	GtkWidget   *raw_form;
	GtkWidget   *raw_grid;
	GtkWidget   *info;
	GdauiDataProxyInfoFlag flags;
	
	BrowserConnection *bcnc;
};

/* get a pointer to the parents to be able to call their destructor */
static GObjectClass *parent_class = NULL;

/* properties */
enum {
	PROP_0,
	PROP_RAW_GRID,
	PROP_RAW_FORM,
	PROP_INFO,
};

GType
ui_formgrid_get_type (void)
{
	static GType type = 0;

	if (!type) {
		static const GTypeInfo info = {
			sizeof (UiFormGridClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) ui_formgrid_class_init,
			NULL,
			NULL,
			sizeof (UiFormGrid),
			0,
			(GInstanceInitFunc) ui_formgrid_init,
			0
		};		

		type = g_type_register_static (GTK_TYPE_VBOX, "UiFormGrid", &info, 0);
	}

	return type;
}

static void
ui_formgrid_class_init (UiFormGridClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);
	
	parent_class = g_type_class_peek_parent (class);
	object_class->dispose = ui_formgrid_dispose;

	/* Properties */
        object_class->set_property = ui_formgrid_set_property;
        object_class->get_property = ui_formgrid_get_property;
	g_object_class_install_property (object_class, PROP_RAW_GRID,
                                         g_param_spec_object ("raw_grid", NULL, NULL, 
							      GDAUI_TYPE_RAW_GRID,
							      G_PARAM_READABLE));
	g_object_class_install_property (object_class, PROP_RAW_FORM,
                                         g_param_spec_object ("raw_form", NULL, NULL, 
							      GDAUI_TYPE_RAW_GRID,
							      G_PARAM_READABLE));
	g_object_class_install_property (object_class, PROP_INFO,
                                         g_param_spec_object ("widget_info", NULL, NULL, 
							      GDAUI_TYPE_DATA_PROXY_INFO,
							      G_PARAM_READABLE));
}

static void
ui_formgrid_dispose (GObject *object)
{
        UiFormGrid *formgrid;

        formgrid = UI_FORMGRID (object);
        if (formgrid->priv) {
                if (formgrid->priv->bcnc)
                        g_object_unref (formgrid->priv->bcnc);

                g_free (formgrid->priv);
                formgrid->priv = NULL;
        }

        /* parent class */
        parent_class->dispose (object);
}

static void form_grid_toggled_cb (GtkToggleButton *button, UiFormGrid *formgrid);
static void raw_grid_populate_popup_cb (GdauiRawGrid *gdauirawgrid, GtkMenu *menu, UiFormGrid *formgrid);

static void
ui_formgrid_init (UiFormGrid *formgrid)
{
	GtkWidget *sw;
	GtkWidget *hbox, *button;
	
	formgrid->priv = g_new0 (UiFormGridPriv, 1);
	formgrid->priv->raw_grid = NULL;
	formgrid->priv->info = NULL;
	formgrid->priv->flags = GDAUI_DATA_PROXY_INFO_CURRENT_ROW;
	formgrid->priv->bcnc = NULL;

	/* notebook */
	formgrid->priv->nb = gtk_notebook_new ();
	gtk_notebook_set_show_tabs (GTK_NOTEBOOK (formgrid->priv->nb), FALSE);
	gtk_notebook_set_show_border (GTK_NOTEBOOK (formgrid->priv->nb), FALSE);
	gtk_box_pack_start (GTK_BOX (formgrid), formgrid->priv->nb, TRUE, TRUE, 0);
	gtk_widget_show (formgrid->priv->nb);

	/* grid on 1st page of notebook */
	sw = gtk_scrolled_window_new (NULL, NULL);
        gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sw), GTK_SHADOW_IN);
	gtk_notebook_append_page (GTK_NOTEBOOK (formgrid->priv->nb), sw, NULL);
	gtk_widget_show (sw);

	formgrid->priv->raw_grid = gdaui_raw_grid_new (NULL);
	gdaui_data_proxy_column_show_actions (GDAUI_DATA_PROXY (formgrid->priv->raw_grid), -1, FALSE);
	gtk_container_add (GTK_CONTAINER (sw), formgrid->priv->raw_grid);
	gtk_widget_show (formgrid->priv->raw_grid);
	g_signal_connect (formgrid->priv->raw_grid, "populate-popup",
			  G_CALLBACK (raw_grid_populate_popup_cb), formgrid);

	/* form on the 2nd page of the notebook */
	formgrid->priv->sw = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (formgrid->priv->sw), GTK_SHADOW_NONE);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (formgrid->priv->sw), GTK_POLICY_AUTOMATIC,
					GTK_POLICY_AUTOMATIC);
	formgrid->priv->raw_form = gdaui_raw_form_new (NULL);
	gtk_scrolled_window_add_with_viewport (GTK_SCROLLED_WINDOW (formgrid->priv->sw), formgrid->priv->raw_form);
	gdaui_data_proxy_column_show_actions (GDAUI_DATA_PROXY (formgrid->priv->raw_form), -1, FALSE);
	gtk_notebook_append_page (GTK_NOTEBOOK (formgrid->priv->nb), formgrid->priv->sw, NULL);
        gtk_widget_show (formgrid->priv->sw);
        gtk_widget_show (formgrid->priv->raw_form);

	/* info widget and toggle button at last */
	hbox = gtk_hbox_new (FALSE, 0);
	gtk_box_pack_start (GTK_BOX (formgrid), hbox, FALSE, TRUE, 0);
	gtk_widget_show (hbox);
		
	button = gtk_toggle_button_new ();
	GdkPixbuf *pixbuf = browser_get_pixbuf_icon (BROWSER_ICON_GRID);
	gtk_button_set_image (GTK_BUTTON (button), gtk_image_new_from_pixbuf (pixbuf));

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), TRUE);
	gtk_box_pack_start (GTK_BOX (hbox), button, FALSE, TRUE, 0);
	gtk_widget_show (button);
	g_signal_connect (G_OBJECT (button), "toggled",
			  G_CALLBACK (form_grid_toggled_cb), formgrid);
	gtk_widget_set_tooltip_text (button, _("Toggle between grid and form presentations"));

	formgrid->priv->info = gdaui_data_proxy_info_new (GDAUI_DATA_PROXY (formgrid->priv->raw_grid), 
							  formgrid->priv->flags |
							  GDAUI_DATA_PROXY_INFO_CURRENT_ROW |
							  GDAUI_DATA_PROXY_INFO_CHUNCK_CHANGE_BUTTONS);
	gtk_box_pack_start (GTK_BOX (hbox), formgrid->priv->info, TRUE, TRUE, 0);
	gtk_widget_show (formgrid->priv->info);
}

static void
form_grid_toggled_cb (GtkToggleButton *button, UiFormGrid *formgrid)
{
	GdaDataModelIter *iter;
	gint row;

	if (!gtk_toggle_button_get_active (button)) {
		/* switch to form  view */
		gtk_notebook_set_current_page (GTK_NOTEBOOK (formgrid->priv->nb), 1);
		g_object_set (G_OBJECT (formgrid->priv->info),
			      "data-proxy", formgrid->priv->raw_form,
			      "flags", formgrid->priv->flags | GDAUI_DATA_PROXY_INFO_CURRENT_ROW |
			      GDAUI_DATA_PROXY_INFO_ROW_MOVE_BUTTONS
			      /*GDAUI_DATA_PROXY_INFO_CURRENT_ROW |
				GDAUI_DATA_PROXY_INFO_ROW_MODIFY_BUTTONS |
				GDAUI_DATA_PROXY_INFO_ROW_MOVE_BUTTONS*/, NULL);

		GdkPixbuf *pixbuf = browser_get_pixbuf_icon (BROWSER_ICON_FORM);
		gtk_button_set_image (GTK_BUTTON (button), gtk_image_new_from_pixbuf (pixbuf));

		iter = gdaui_data_selector_get_data_set (GDAUI_DATA_SELECTOR (formgrid->priv->raw_grid));
		row = gda_data_model_iter_get_row (iter);
		iter = gdaui_data_selector_get_data_set (GDAUI_DATA_SELECTOR (formgrid->priv->raw_form));
	}
	else {
		/* switch to grid view */
		gtk_notebook_set_current_page (GTK_NOTEBOOK (formgrid->priv->nb), 0);
		g_object_set (G_OBJECT (formgrid->priv->info),
			      "data-proxy", formgrid->priv->raw_grid,
			      "flags", formgrid->priv->flags | GDAUI_DATA_PROXY_INFO_CURRENT_ROW |
			      GDAUI_DATA_PROXY_INFO_CHUNCK_CHANGE_BUTTONS
			      /*GDAUI_DATA_PROXY_INFO_CURRENT_ROW |
				GDAUI_DATA_PROXY_INFO_ROW_MODIFY_BUTTONS |
				GDAUI_DATA_PROXY_INFO_CHUNCK_CHANGE_BUTTONS*/, NULL);

		GdkPixbuf *pixbuf = browser_get_pixbuf_icon (BROWSER_ICON_GRID);
		gtk_button_set_image (GTK_BUTTON (button), gtk_image_new_from_pixbuf (pixbuf));

		iter = gdaui_data_selector_get_data_set (GDAUI_DATA_SELECTOR (formgrid->priv->raw_form));
		row = gda_data_model_iter_get_row (iter);
		iter = gdaui_data_selector_get_data_set (GDAUI_DATA_SELECTOR (formgrid->priv->raw_grid));
	}

	gda_data_model_iter_move_to_row (iter, row >= 0 ? row : 0);
}

static BrowserConnection *
get_browser_connection (UiFormGrid *formgrid)
{
	if (formgrid->priv->bcnc)
		return formgrid->priv->bcnc;
	else {
		GtkWidget *toplevel;
		
		toplevel = gtk_widget_get_toplevel (GTK_WIDGET (formgrid));
		if (BROWSER_IS_WINDOW (toplevel))
			return browser_window_get_connection (BROWSER_WINDOW (toplevel));
	}
	return NULL;
}


static void execute_action_mitem_cb (GtkMenuItem *menuitem, UiFormGrid *formgrid);

static void
raw_grid_populate_popup_cb (GdauiRawGrid *gdauirawgrid, GtkMenu *menu, UiFormGrid *formgrid)
{
	/* add actions to execute to menu */
	GdaDataModelIter *iter;
	GSList *actions_list, *list;
	BrowserConnection *bcnc = NULL;

	bcnc = get_browser_connection (formgrid);
	if (!bcnc)
		return;
	
	iter = gdaui_data_selector_get_data_set (GDAUI_DATA_SELECTOR (formgrid->priv->raw_grid));
	actions_list = browser_favorites_get_actions (browser_connection_get_favorites (bcnc),
						      bcnc, GDA_SET (iter));
	if (! actions_list)
		return;

	GtkWidget *mitem, *submenu;
	mitem = gtk_menu_item_new_with_label (_("Execute action"));
	gtk_widget_show (mitem);
	gtk_menu_shell_prepend (GTK_MENU_SHELL (menu), mitem);

	submenu = gtk_menu_new ();
	gtk_menu_item_set_submenu (GTK_MENU_ITEM (mitem), submenu);
	for (list = actions_list; list; list = list->next) {
		BrowserFavoriteAction *act = (BrowserFavoriteAction*) list->data;
		mitem = gtk_menu_item_new_with_label (act->name);
		gtk_widget_show (mitem);
		gtk_menu_shell_append (GTK_MENU_SHELL (submenu), mitem);
		g_object_set_data_full (G_OBJECT (mitem), "action", act,
					(GDestroyNotify) browser_favorites_free_action);
		g_signal_connect (mitem, "activate",
				  G_CALLBACK (execute_action_mitem_cb), formgrid);
	}

	g_slist_free (actions_list);
}

static void
statement_executed_cb (G_GNUC_UNUSED BrowserConnection *bcnc,
		       G_GNUC_UNUSED guint exec_id,
		       GObject *out_result,
		       G_GNUC_UNUSED GdaSet *out_last_inserted_row, GError *error,
		       UiFormGrid *formgrid)
{
	GtkWidget *toplevel;
	toplevel = gtk_widget_get_toplevel (GTK_WIDGET (formgrid));
        if (error)
                browser_show_error (GTK_WINDOW (toplevel),
                                    _("Error executing query:\n%s"),
                                    error->message ?
                                    error->message : _("No detail"));
	else if (out_result && GDA_IS_DATA_MODEL (out_result)) {
		GtkWidget *dialog, *toplevel, *label, *fg;
		GtkWidget *dcontents;
		
		gchar *tmp;
		toplevel = gtk_widget_get_toplevel (GTK_WIDGET (formgrid));
		dialog = gtk_dialog_new_with_buttons (_("Action executed"),
						      GTK_WINDOW (toplevel),
						      GTK_DIALOG_NO_SEPARATOR,
						      GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE, NULL);
#if GTK_CHECK_VERSION(2,18,0)
		dcontents = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
#else
		dcontents = GTK_DIALOG (dialog)->vbox;
#endif
		gtk_box_set_spacing (GTK_BOX (dcontents), 5);
		gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog), GTK_RESPONSE_CLOSE, TRUE);
		
		tmp = g_strdup_printf ("<b>%s</b>", _("Action executed"));
		label = gtk_label_new ("");
		gtk_label_set_markup (GTK_LABEL (label), tmp);
		g_free (tmp);
		gtk_misc_set_alignment (GTK_MISC (label), 0., 0.);
		gtk_box_pack_start (GTK_BOX (dcontents), label, FALSE, FALSE, 0);
		
		fg = ui_formgrid_new (GDA_DATA_MODEL (out_result),
				      GDAUI_DATA_PROXY_INFO_CURRENT_ROW);
		ui_formgrid_set_connection (UI_FORMGRID (fg), get_browser_connection (formgrid));

		if (GDA_IS_DATA_SELECT (out_result)) {
			GdaStatement *stmt;
			g_object_get (G_OBJECT (out_result), "select-stmt", &stmt, NULL);
			if (stmt) {
				ui_formgrid_handle_user_prefs (UI_FORMGRID (fg), NULL, stmt);
				g_object_unref (stmt);
			}
		}
		gtk_box_pack_start (GTK_BOX (dcontents), fg, TRUE, TRUE, 0);
		
		gtk_window_set_default_size (GTK_WINDOW (dialog), 400, 600);
		gtk_widget_show_all (dialog);
		
		g_signal_connect (dialog, "response",
				  G_CALLBACK (gtk_widget_destroy), NULL);
		g_signal_connect (dialog, "close",
				  G_CALLBACK (gtk_widget_destroy), NULL);
	}
        else if (BROWSER_IS_WINDOW (toplevel)) {
		browser_window_show_notice_printf (BROWSER_WINDOW (toplevel),
						   GTK_MESSAGE_INFO,
						   "ActionExecution",
						   "%s", _("Action successfully executed"));
	}
	else
		browser_show_message (GTK_WINDOW (toplevel),
				      "%s", _("Action successfully executed"));
}

static void
execute_action_mitem_cb (GtkMenuItem *menuitem, UiFormGrid *formgrid)
{
	BrowserFavoriteAction *act;
	GtkWidget *dlg;
	gchar *tmp;
	gint response;
	GtkWidget *toplevel;

	act = (BrowserFavoriteAction*) g_object_get_data (G_OBJECT (menuitem), "action");
	toplevel = gtk_widget_get_toplevel ((GtkWidget*) formgrid);
	tmp = g_strdup_printf (_("Set or confirm the parameters to execute\n"
				 "action '%s'"), act->name);
	dlg = gdaui_basic_form_new_in_dialog (act->params,
					      (GtkWindow*) toplevel,
					      _("Execution of action"), tmp);
	g_free (tmp);
	response = gtk_dialog_run (GTK_DIALOG (dlg));
	gtk_widget_destroy (dlg);
	if (response == GTK_RESPONSE_ACCEPT) {
                GError *lerror = NULL;
		BrowserConnection *bcnc;
		
		bcnc = get_browser_connection (formgrid);
		g_assert (bcnc);
		
                if (! browser_connection_execute_statement_cb (bcnc,
                                                               act->stmt, act->params,
                                                               GDA_STATEMENT_MODEL_RANDOM_ACCESS,
                                                               FALSE,
                                                               (BrowserConnectionExecuteCallback) statement_executed_cb,
                                                               formgrid, &lerror)) {
                        browser_show_error (GTK_WINDOW (toplevel),
                                            _("Error executing query: %s"),
                                            lerror && lerror->message ? lerror->message : _("No detail"));
                        g_clear_error (&lerror);
                }
	}
}

/**
 * ui_formgrid_new
 * @model: a #GdaDataModel
 *
 * Creates a new #UiFormGrid widget suitable to display the data in @model
 *
 *  Returns: the new widget
 */
GtkWidget *
ui_formgrid_new (GdaDataModel *model, GdauiDataProxyInfoFlag flags)
{
	UiFormGrid *formgrid;
	GdaDataProxy *proxy;

	g_return_val_if_fail (!model || GDA_IS_DATA_MODEL (model), NULL);

	formgrid = (UiFormGrid *) g_object_new (UI_TYPE_FORMGRID, NULL);
	formgrid->priv->flags = flags;

	/* a raw form and a raw grid for the same proxy */
	g_object_set (formgrid->priv->raw_grid, "model", model, NULL);
	proxy = gdaui_data_proxy_get_proxy (GDAUI_DATA_PROXY (formgrid->priv->raw_grid));
	g_object_set (formgrid->priv->raw_form, "model", proxy, NULL);
	gdaui_data_proxy_set_write_mode (GDAUI_DATA_PROXY (formgrid->priv->raw_form),
					 GDAUI_DATA_PROXY_WRITE_ON_ROW_CHANGE);
	g_object_set (G_OBJECT (formgrid->priv->info),
		      "flags", formgrid->priv->flags | GDAUI_DATA_PROXY_INFO_CURRENT_ROW |
		      GDAUI_DATA_PROXY_INFO_CHUNCK_CHANGE_BUTTONS, NULL);

	/* no more than 300 rows at a time */
	gda_data_proxy_set_sample_size (proxy, 300);

	return (GtkWidget *) formgrid;
}

/**
 * ui_formgrid_handle_user_prefs
 * @formgrid: a #UiFormGrid widget
 * @bcnc: a #BrowserConnection, or %NULL to let @formgrid determine it itself
 * @stmt: the #GdaStatement which has been executed to produce the #GdaDataModel displayed in @formgrid
 *
 * Takes into account the UI preferences of the user
 */
void
ui_formgrid_handle_user_prefs (UiFormGrid *formgrid, BrowserConnection *bcnc, GdaStatement *stmt)
{
	g_return_if_fail (UI_IS_FORMGRID (formgrid));
	if (bcnc)
		g_return_if_fail (BROWSER_IS_CONNECTION (bcnc));
	else {
		bcnc = get_browser_connection (formgrid);
		if (!bcnc)
			return;
	}
	if (stmt)
		g_return_if_fail (GDA_IS_STATEMENT (stmt));
	else
		return;

	GdaSqlStatement *sqlst;
	g_object_get ((GObject*) stmt, "structure", &sqlst, NULL);
	if (!sqlst)
		return;
	
	if ((sqlst->stmt_type != GDA_SQL_STATEMENT_SELECT) ||
	    !browser_connection_normalize_sql_statement (bcnc, sqlst, NULL))
		goto out;
	
	GdaSet *set;
	set = (GdaSet*) ui_formgrid_get_form_data_set (UI_FORMGRID (formgrid));
	
	GdaSqlStatementSelect *sel;
	GSList *list;
	gint pos;
	sel = (GdaSqlStatementSelect*) sqlst->contents;
	for (pos = 0, list = sel->expr_list; list; pos ++, list = list->next) {
		GdaSqlSelectField *field = (GdaSqlSelectField*) list->data;
		if (! field->validity_meta_object ||
		    (field->validity_meta_object->obj_type != GDA_META_DB_TABLE) ||
		    !field->validity_meta_table_column)
			continue;
		
		gchar *plugin;
		plugin = browser_connection_get_table_column_attribute (bcnc,
									GDA_META_TABLE (field->validity_meta_object),
									field->validity_meta_table_column,
									BROWSER_CONNECTION_COLUMN_PLUGIN, NULL);
		if (!plugin)
			continue;
		
		GdaHolder *holder;
		holder = gda_set_get_nth_holder (set, pos);
		if (holder) {
			GValue *value;
			value = gda_value_new_from_string (plugin, G_TYPE_STRING);
			gda_holder_set_attribute_static (holder, GDAUI_ATTRIBUTE_PLUGIN, value);
			gda_value_free (value);
		}
		g_free (plugin);
	}
	
 out:
	gda_sql_statement_free (sqlst);
}



static void
ui_formgrid_set_property (GObject *object,
			  guint param_id,
			  G_GNUC_UNUSED const GValue *value,
			  GParamSpec *pspec)
{
	UiFormGrid *formgrid;
	
	formgrid = UI_FORMGRID (object);
	
	switch (param_id) {
		
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	}
}

static void
ui_formgrid_get_property (GObject *object,
			  guint param_id,
			  GValue *value,
			  GParamSpec *pspec)
{
	UiFormGrid *formgrid;

	formgrid = UI_FORMGRID (object);
	
	switch (param_id) {
	case PROP_RAW_GRID:
		g_value_set_object (value, formgrid->priv->raw_grid);
		break;
	case PROP_RAW_FORM:
		g_value_set_object (value, formgrid->priv->raw_form);
		break;
	case PROP_INFO:
		g_value_set_object (value, formgrid->priv->info);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	}	
}

/**
 * ui_formgrid_get_selection
 * @formgrid: a #UiFormGrid widget
 * 
 * Returns the list of the currently selected rows in a #UiFormGrid widget. 
 * The returned value is a list of integers, which represent each of the selected rows.
 *
 * If new rows have been inserted, then those new rows will have a row number equal to -1.
 * This function is a wrapper around the gdaui_raw_grid_get_selection() function.
 *
 * Returns: a new array, should be freed (by calling g_array_free() and passing %TRUE as last argument) when no longer needed.
 */
GArray *
ui_formgrid_get_selection (UiFormGrid *formgrid)
{
	g_return_val_if_fail (UI_IS_FORMGRID (formgrid), NULL);
	g_return_val_if_fail (formgrid->priv, NULL);

	return gdaui_data_selector_get_selected_rows (GDAUI_DATA_SELECTOR (formgrid->priv->raw_grid));
}

/**
 * ui_formgrid_get_form_data_set
 */
GdaDataModelIter *
ui_formgrid_get_form_data_set (UiFormGrid *formgrid)
{
	g_return_val_if_fail (UI_IS_FORMGRID (formgrid), NULL);
	g_return_val_if_fail (formgrid->priv, NULL);

	return gdaui_data_selector_get_data_set (GDAUI_DATA_SELECTOR (formgrid->priv->raw_form));
}

/**
 * ui_formgrid_get_grid_data_set
 */
GdaDataModelIter *
ui_formgrid_get_grid_data_set (UiFormGrid *formgrid)
{
	g_return_val_if_fail (UI_IS_FORMGRID (formgrid), NULL);
	g_return_val_if_fail (formgrid->priv, NULL);

	return gdaui_data_selector_get_data_set (GDAUI_DATA_SELECTOR (formgrid->priv->raw_grid));
}


/**
 * ui_formgrid_set_sample_size
 * @formgrid: a #UiFormGrid widget
 * @sample_size: the sample size
 *
 * Set the size of the sample displayed in @formgrid, see gdaui_raw_grid_set_sample_size()
 */
void
ui_formgrid_set_sample_size (UiFormGrid *formgrid, gint sample_size)
{
	g_return_if_fail (UI_IS_FORMGRID (formgrid));
	g_return_if_fail (formgrid->priv);

	gdaui_raw_grid_set_sample_size (GDAUI_RAW_GRID (formgrid->priv->raw_grid), sample_size);
}

/**
 * ui_formgrid_get_grid_widget
 * @formgrid: a #UiFormGrid widget
 *
 * Returns: the #GdauiRawGrid embedded in @formgrid
 */
GdauiRawGrid *
ui_formgrid_get_grid_widget (UiFormGrid *formgrid)
{
	g_return_val_if_fail (UI_IS_FORMGRID (formgrid), NULL);
	g_return_val_if_fail (formgrid->priv, NULL);

	return GDAUI_RAW_GRID (formgrid->priv->raw_grid);
}

/**
 * ui_formgrid_set_connection
 * @formgrid: a #UiFormGrid widget
 * @bcnc: a #BrowserConnection, or %NULL
 *
 * Tells @formgrid to use @bcnc as connection when actions have to be executed
 */
void
ui_formgrid_set_connection (UiFormGrid *formgrid, BrowserConnection *bcnc)
{
	g_return_if_fail (UI_IS_FORMGRID (formgrid));
	g_return_if_fail (!bcnc || BROWSER_IS_CONNECTION (bcnc));

	if (formgrid->priv->bcnc) {
		g_object_unref (formgrid->priv->bcnc);
		formgrid->priv->bcnc = NULL;
	}
	if (bcnc)
		formgrid->priv->bcnc = g_object_ref (bcnc);
}