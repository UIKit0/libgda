/* gdaui-basic-form.h
 *
 * Copyright (C) 2002 - 2009 Vivien Malerba <malerba@gnome-db.org>
 *
 * This Library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#ifndef __GDAUI_BASIC_FORM__
#define __GDAUI_BASIC_FORM__

#include <gtk/gtk.h>
#include <libgda/libgda.h>
#ifdef HAVE_LIBGLADE
#include <glade/glade.h>
#endif

G_BEGIN_DECLS

#define GDAUI_TYPE_BASIC_FORM          (gdaui_basic_form_get_type())
#define GDAUI_BASIC_FORM(obj)          G_TYPE_CHECK_INSTANCE_CAST (obj, gdaui_basic_form_get_type(), GdauiBasicForm)
#define GDAUI_BASIC_FORM_CLASS(klass)  G_TYPE_CHECK_CLASS_CAST (klass, gdaui_basic_form_get_type (), GdauiBasicFormClass)
#define GDAUI_IS_BASIC_FORM(obj)       G_TYPE_CHECK_INSTANCE_TYPE (obj, gdaui_basic_form_get_type ())

typedef struct {
#ifdef HAVE_LIBGLADE
	GladeXML *xml_object; /* one of xml_object or */
#endif
	gchar    *xml_file;   /* xml_file is required */

	gchar    *root_element; /* required */
	gchar    *form_prefix;  /* required */
} GdauiFormLayoutSpec;

typedef struct _GdauiBasicForm      GdauiBasicForm;
typedef struct _GdauiBasicFormClass GdauiBasicFormClass;
typedef struct _GdauiBasicFormPriv  GdauiBasicFormPriv;

/* struct for the object's data */
struct _GdauiBasicForm
{
	GtkVBox               object;

	GdauiBasicFormPriv *priv;
};

/* struct for the object's class */
struct _GdauiBasicFormClass
{
	GtkVBoxClass          parent_class;

	/* signals */
        void       (*param_changed)         (GdauiBasicForm *form, GdaHolder *param, gboolean is_user_modif);
	void       (*activated)             (GdauiBasicForm *form);
};

/* 
 * Generic widget's methods 
*/
GType             gdaui_basic_form_get_type                 (void) G_GNUC_CONST;
GtkWidget        *gdaui_basic_form_new                      (GdaSet *data_set);
GtkWidget        *gdaui_basic_form_new_custom               (GdaSet *data_set, const gchar *glade_file, 
								const gchar *root_element, const gchar *form_prefix);
GtkWidget        *gdaui_basic_form_new_in_dialog            (GdaSet *data_set, GtkWindow *parent,
								const gchar *title, const gchar *header);
GdaSet           *gdaui_basic_form_get_data_set             (GdauiBasicForm *form);
gboolean          gdaui_basic_form_is_valid                 (GdauiBasicForm *form);
gboolean          gdaui_basic_form_has_been_changed         (GdauiBasicForm *form);
void              gdaui_basic_form_reset                    (GdauiBasicForm *form);
void              gdaui_basic_form_set_current_as_orig      (GdauiBasicForm *form);

void              gdaui_basic_form_show_entry_actions       (GdauiBasicForm *form, gboolean show_actions);
void              gdaui_basic_form_entry_show               (GdauiBasicForm *form, 
								GdaHolder *param, gboolean show);
void              gdaui_basic_form_entry_grab_focus         (GdauiBasicForm *form, GdaHolder *param);
void              gdaui_basic_form_entry_set_editable       (GdauiBasicForm *form, GdaHolder *param, 
								gboolean editable);
void              gdaui_basic_form_set_entries_auto_default (GdauiBasicForm *form, gboolean auto_default);
void              gdaui_basic_form_set_entries_default      (GdauiBasicForm *form);

GtkWidget        *gdaui_basic_form_get_entry_widget         (GdauiBasicForm *form, GdaHolder *param);
GtkWidget        *gdaui_basic_form_get_label_widget         (GdauiBasicForm *form, GdaHolder *param);



G_END_DECLS

#endif