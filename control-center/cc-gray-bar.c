/* GNOME DB library
 *
 * Copyright (C) 1999 - 2009 The GNOME Foundation.
 *
 * AUTHORS:
 *      Rodrigo Moya <rodrigo@gnome-db.org>
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

#include <gtk/gtk.h>
#include "cc-gray-bar.h"

#define PARENT_TYPE GTK_TYPE_BIN



struct _CcGrayBarPrivate {
	GtkWidget *hbox;
	GtkWidget *icon;
	gboolean   show_icon;
	GtkWidget *label;
};

static void cc_gray_bar_class_init   (CcGrayBarClass *klass);
static void cc_gray_bar_init         (CcGrayBar      *bar,
					    CcGrayBarClass *klass);
static void cc_gray_bar_realize      (GtkWidget           *widget);
static void cc_gray_bar_size_request (GtkWidget           *widget,
					    GtkRequisition      *requisition);
static void cc_gray_bar_allocate     (GtkWidget           *widget,
					    GtkAllocation       *allocation);
static void cc_gray_bar_paint        (GtkWidget           *widget,
					    GdkRectangle        *area);
static gint cc_gray_bar_expose       (GtkWidget           *widget,
					    GdkEventExpose      *event);
static void cc_gray_bar_style_set    (GtkWidget           *w,
					    GtkStyle            *previous_style);
static void cc_gray_bar_set_property (GObject *object,
					    guint                paramid,
					    const GValue *value,
					    GParamSpec *pspec);
static void cc_gray_bar_get_property (GObject *object,
					    guint                param_id,
					    GValue *value,
					    GParamSpec *pspec);
static void cc_gray_bar_finalize     (GObject *object);

/* Properties */
enum {
	PROP_0,
	PROP_TEXT,
	PROP_SHOW_ICON
};

static GObjectClass *parent_class = NULL;

/*
 * CcGrayBar class implementation
 */

static void
cc_gray_bar_realize (GtkWidget *widget)
{
	GdkWindowAttr attributes;
	gint attributes_mask;
	gint border_width;
	
	GTK_WIDGET_SET_FLAGS (widget, GTK_REALIZED);
	
	border_width = GTK_CONTAINER (widget)->border_width;
	
	attributes.x = widget->allocation.x + border_width;
	attributes.y = widget->allocation.y + border_width;
	attributes.width = widget->allocation.width - 2*border_width;
	attributes.height = widget->allocation.height - 2*border_width;
	attributes.window_type = GDK_WINDOW_CHILD;
	attributes.wclass = GDK_INPUT_OUTPUT;
	attributes.visual = gtk_widget_get_visual (widget);
	attributes.colormap = gtk_widget_get_colormap (widget);
	attributes.event_mask = gtk_widget_get_events (widget)
		| GDK_BUTTON_MOTION_MASK
		| GDK_BUTTON_PRESS_MASK
		| GDK_BUTTON_RELEASE_MASK
		| GDK_EXPOSURE_MASK
		| GDK_ENTER_NOTIFY_MASK
		| GDK_LEAVE_NOTIFY_MASK;
	
	attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP;
	
	widget->window = gdk_window_new (gtk_widget_get_parent_window (widget), &attributes, attributes_mask);
	gdk_window_set_user_data (widget->window, widget);
	
	widget->style = gtk_style_attach (widget->style, widget->window);
	gtk_style_set_background (widget->style, widget->window, GTK_STATE_NORMAL);
}

static void
cc_gray_bar_size_request (GtkWidget *widget, GtkRequisition *requisition)
{
	GtkBin *bin = GTK_BIN (widget);
	GtkRequisition child_requisition;
	
	requisition->width = GTK_CONTAINER (widget)->border_width * 2;
	requisition->height = GTK_CONTAINER (widget)->border_width * 2;
	
	if (bin->child && GTK_WIDGET_VISIBLE (bin->child)) {
		gtk_widget_size_request (bin->child, &child_requisition);
		
		requisition->width += child_requisition.width;
		requisition->height += child_requisition.height;
	}
}

static void
cc_gray_bar_allocate (GtkWidget *widget, GtkAllocation *allocation)
{
	GtkBin *bin;
	GtkAllocation child_allocation;
	
	widget->allocation = *allocation;
	bin = GTK_BIN (widget);
	
	child_allocation.x = 0;
	child_allocation.y = 0;
	child_allocation.width = MAX (allocation->width - GTK_CONTAINER (widget)->border_width * 2, 0);
	child_allocation.height = MAX (allocation->height - GTK_CONTAINER (widget)->border_width * 2, 0);
	
	if (GTK_WIDGET_REALIZED (widget)) {
		gdk_window_move_resize (widget->window,
					allocation->x + GTK_CONTAINER (widget)->border_width,
					allocation->y + GTK_CONTAINER (widget)->border_width,
					child_allocation.width,
					child_allocation.height);
	}
	
	if (bin->child)
		gtk_widget_size_allocate (bin->child, &child_allocation);
}

static void
cc_gray_bar_style_set (GtkWidget *w, GtkStyle *previous_style)
{
	static int in_style_set = 0;
	GtkStyle   *style;

	if (in_style_set > 0)
                return;

        in_style_set ++;

	style = gtk_rc_get_style (GTK_WIDGET (w));
	gtk_widget_modify_bg (GTK_WIDGET (w), GTK_STATE_NORMAL, &style->bg[GTK_STATE_ACTIVE]);

	in_style_set --;

	GTK_WIDGET_CLASS (parent_class)->style_set (w, previous_style);
}

static void
cc_gray_bar_paint (GtkWidget *widget, GdkRectangle *area)
{
	if (!GTK_WIDGET_APP_PAINTABLE (widget)) {
		gtk_paint_flat_box (widget->style, widget->window,
				    widget->state, GTK_SHADOW_NONE,
				    area, widget, "gnomedbgraybar",
				    1, 1,
				    (widget->allocation.width - 2), 
				    (widget->allocation.height - 2));
	}
}

static gboolean
cc_gray_bar_expose (GtkWidget *widget, GdkEventExpose *event)
{   
	g_return_val_if_fail (widget != NULL, FALSE);
	g_return_val_if_fail (CC_IS_GRAY_BAR (widget), FALSE);
	g_return_val_if_fail (event != NULL, FALSE);
	
	if (event->count > 0)
		return FALSE;
	
	if (GTK_WIDGET_DRAWABLE (widget)) {
		cc_gray_bar_paint (widget, &event->area);
		
		(* GTK_WIDGET_CLASS (parent_class)->expose_event) (widget, event);
	}

	return FALSE;
}

static void
cc_gray_bar_class_init (CcGrayBarClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->set_property  = cc_gray_bar_set_property;
	object_class->get_property  = cc_gray_bar_get_property;
	object_class->finalize      = cc_gray_bar_finalize;
	widget_class->style_set     = cc_gray_bar_style_set;
	widget_class->realize       = cc_gray_bar_realize;
	widget_class->size_request  = cc_gray_bar_size_request;
	widget_class->size_allocate = cc_gray_bar_allocate;
	widget_class->expose_event  = cc_gray_bar_expose;

	/* add class properties */
	g_object_class_install_property (
		object_class, PROP_TEXT,
		g_param_spec_string ("text", NULL, "Text showed inside the widget.", NULL,
				     (G_PARAM_READABLE | G_PARAM_WRITABLE)));
	g_object_class_install_property (
		object_class, PROP_SHOW_ICON,
		g_param_spec_boolean ("show_icon", NULL, NULL, FALSE,
				     (G_PARAM_READABLE | G_PARAM_WRITABLE)));
	
}

static void
cc_gray_bar_init (CcGrayBar *bar, CcGrayBarClass *klass)
{
	GTK_WIDGET_UNSET_FLAGS (bar, GTK_NO_WINDOW);
	
	g_return_if_fail (CC_IS_GRAY_BAR (bar));
	
	bar->priv = g_new0 (CcGrayBarPrivate, 1);
	
	bar->priv->hbox = gtk_hbox_new (FALSE, 6);
	gtk_container_set_border_width (GTK_CONTAINER (bar->priv->hbox), 6);
	
	bar->priv->show_icon = FALSE;
	bar->priv->icon = gtk_image_new ();
	gtk_misc_set_alignment (GTK_MISC (bar->priv->icon), 0.5, 0.0);
	gtk_widget_hide (bar->priv->icon);
	gtk_box_pack_start (GTK_BOX (bar->priv->hbox), bar->priv->icon,
			    FALSE, TRUE, 0);
	
	bar->priv->label = gtk_label_new ("");
	gtk_label_set_selectable (GTK_LABEL (bar->priv->label), FALSE);
	gtk_misc_set_alignment (GTK_MISC (bar->priv->label), 0.00, 0.0);
	gtk_box_pack_end (GTK_BOX (bar->priv->hbox), bar->priv->label,
			  TRUE, TRUE, 0);
	gtk_widget_show (bar->priv->label);
	
	gtk_widget_show (bar->priv->hbox);
	gtk_container_add (GTK_CONTAINER (bar), bar->priv->hbox);
}

static void
cc_gray_bar_set_property (GObject *object,
				guint param_id,
				const GValue *value,
				GParamSpec *pspec)
{
	CcGrayBar *bar = (CcGrayBar *) object;

	g_return_if_fail (CC_IS_GRAY_BAR (bar));

	switch (param_id) {
	case PROP_TEXT :
		cc_gray_bar_set_text (bar, g_value_get_string (value));
		break;
	case PROP_SHOW_ICON:
		cc_gray_bar_set_show_icon (bar, g_value_get_boolean(value));
		break;
	default :
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	}
}

static void
cc_gray_bar_get_property (GObject *object,
				guint param_id,
				GValue *value,
				GParamSpec *pspec)
{
	CcGrayBar *bar = (CcGrayBar *) object;

	g_return_if_fail (CC_IS_GRAY_BAR (bar));

	switch (param_id) {
	case PROP_TEXT :
		g_value_set_string (value, cc_gray_bar_get_text (bar));
		break;
	case PROP_SHOW_ICON:
		g_value_set_boolean(value, cc_gray_bar_get_show_icon (bar));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	}
}

static void
cc_gray_bar_finalize (GObject *object)
{
	CcGrayBar *bar = (CcGrayBar *) object;

	g_return_if_fail (CC_IS_GRAY_BAR (bar));

	if (bar->priv) {
		bar->priv->label = NULL;
		bar->priv->icon = NULL;
		bar->priv->hbox = NULL;

		g_free (bar->priv);
		bar->priv = NULL;
	}

	parent_class->finalize (object);
}

GType
cc_gray_bar_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		static const GTypeInfo info = {
			sizeof (CcGrayBarClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) cc_gray_bar_class_init,
			NULL,
			NULL,
			sizeof (CcGrayBar),
			0,
			(GInstanceInitFunc) cc_gray_bar_init
		};
		type = g_type_register_static (PARENT_TYPE, "CcGrayBar", &info, 0);
	}
	return type;
}

/**
 * cc_gray_bar_new
 * @label:
 *
 *
 *
 * Returns:
 */
GtkWidget *
cc_gray_bar_new (const gchar *label)
{
	CcGrayBar *bar;

	bar = g_object_new (CC_TYPE_GRAY_BAR, "text", label, NULL);

	return GTK_WIDGET (bar);
}

/**
 * cc_gray_bar_get_text
 * @bar: A #CcGrayBar widget.
 *
 * Get the text being displayed in the given gray bar widget. This
 * does not include any embedded underlines indicating mnemonics or
 * Pango markup.
 *
 * Returns: the text in the widget.
 */
const gchar *
cc_gray_bar_get_text (CcGrayBar *bar)
{
	const gchar *text;
	
	g_return_val_if_fail (CC_IS_GRAY_BAR (bar), NULL);

	text = gtk_label_get_text (GTK_LABEL (bar->priv->label));

	return text;
}

/**
 * cc_gray_bar_set_text
 * @bar: A #CcGrayBar widget
 * @text:
 *
 * Set the text displayed in the given gray bar widget. This can include 
 * embedded underlines indicating mnemonics or Pango markup.
 * 
 */
void
cc_gray_bar_set_text (CcGrayBar *bar, const gchar *text)
{
	g_return_if_fail (CC_IS_GRAY_BAR (bar));

	gtk_label_set_markup (GTK_LABEL (bar->priv->label), text);
}

/**
 * cc_gray_set_icon_from_file
 * @bar: A #CcGrayBar widget.
 * @file: filename.
 *
 * Set the icon displayed in the given gray bar widget. This can include 
 * embedded underlines indicating mnemonics or Pango markup.
 * 
 */
void
cc_gray_bar_set_icon_from_file (CcGrayBar *bar, const gchar *file)
{
	g_return_if_fail (CC_IS_GRAY_BAR (bar));
	
	gtk_image_set_from_file (GTK_IMAGE (bar->priv->icon), file);
	cc_gray_bar_set_show_icon (bar, TRUE);
}

/**
 * cc_gray_set_icon_from_stock
 * @bar: a #CcGrayBar widget.
 * @stock_id: a stock icon name.
 * @size: a tock icon size.
 *
 * Set the icon using a stock icon for the given gray bar.
 */
void
cc_gray_bar_set_icon_from_stock (CcGrayBar *bar, const gchar *stock_id, GtkIconSize size)
{
	g_return_if_fail (CC_IS_GRAY_BAR (bar));
	
	gtk_image_set_from_stock (GTK_IMAGE (bar->priv->icon), stock_id, size);
	cc_gray_bar_set_show_icon (bar, TRUE);
}

/**
 * cc_gray_set_icon_from_pixbuf
 * @bar: a #CcGrayBar widget.
 * @pixbuf: a #GdkPixbuf
 *
 * Set the icon using a stock icon for the given gray bar.
 */
void
cc_gray_bar_set_icon_from_pixbuf (CcGrayBar *bar, GdkPixbuf *pixbuf)
{
	g_return_if_fail (CC_IS_GRAY_BAR (bar));
	
	gtk_image_set_from_pixbuf (GTK_IMAGE (bar->priv->icon), pixbuf);
	cc_gray_bar_set_show_icon (bar, TRUE);
}

/**
 * cc_gray_bar_set_show_icon
 * @bar: a #CcGrayBar widget.
 * @show: whether to show the icon or not.
 *
 * Set the icon displaying mode for the given grid.
 */
void
cc_gray_bar_set_show_icon (CcGrayBar *bar, gboolean show)
{
	g_return_if_fail (CC_IS_GRAY_BAR (bar));

	if (show) {
		gtk_widget_show (bar->priv->icon);
		bar->priv->show_icon = TRUE;
	} else {
		gtk_widget_hide (bar->priv->icon);
		bar->priv->show_icon = FALSE;
	}
}

/**
 * cc_gray_bar_get_show_icon
 * @bar: a #CcGrayBar widget.
 *
 * Get whether the icon is being shown for the given gray bar.
 *
 * Returns: TRUE if the icon is shown, FALSE if not.
 */
gboolean
cc_gray_bar_get_show_icon (CcGrayBar *bar)
{
	g_return_val_if_fail (CC_IS_GRAY_BAR (bar), FALSE);

	return bar->priv->show_icon;	
}
