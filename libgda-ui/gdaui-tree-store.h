/*
 * Copyright (C) 2009 Vivien Malerba <malerba@gnome-db.org>
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

#ifndef __GDAUI_TREE_STORE__
#define __GDAUI_TREE_STORE__

#include <gtk/gtk.h>
#include <libgda/gda-tree.h>

G_BEGIN_DECLS

#define GDAUI_TYPE_TREE_STORE          (gdaui_tree_store_get_type())
#define GDAUI_TREE_STORE(obj)          G_TYPE_CHECK_INSTANCE_CAST (obj, gdaui_tree_store_get_type(), GdauiTreeStore)
#define GDAUI_TREE_STORE_CLASS(klass)  G_TYPE_CHECK_CLASS_CAST (klass, gdaui_tree_store_get_type (), GdauiTreeStoreClass)
#define GDAUI_IS_TREE_STORE(obj)       G_TYPE_CHECK_INSTANCE_TYPE (obj, gdaui_tree_store_get_type ())

typedef struct _GdauiTreeStore GdauiTreeStore;
typedef struct _GdauiTreeStoreClass GdauiTreeStoreClass;
typedef struct _GdauiTreeStorePriv GdauiTreeStorePriv;


/* struct for the object's tree */
struct _GdauiTreeStore
{
	GObject              object;

	GdauiTreeStorePriv  *priv;
};

/* struct for the object's class */
struct _GdauiTreeStoreClass
{
	GObjectClass         parent_class;

	/* signals */
	gboolean           (*drag_can_drag) (GdauiTreeStore *store, const gchar *path);
	gboolean           (*drag_get)      (GdauiTreeStore *store, const gchar *path, GtkSelectionData *selection_data);
	gboolean           (*drag_can_drop) (GdauiTreeStore *store, const gchar *path, GtkSelectionData *selection_data);
	gboolean           (*drag_drop)     (GdauiTreeStore *store, const gchar *path, GtkSelectionData *selection_data);
	gboolean           (*drag_delete)   (GdauiTreeStore *store, const gchar *path);
};

GType           gdaui_tree_store_get_type             (void) G_GNUC_CONST;

GtkTreeModel   *gdaui_tree_store_new                  (GdaTree *tree, guint n_columns, ...);
GtkTreeModel   *gdaui_tree_store_newv                 (GdaTree *tree, guint n_columns,
						       GType *types, const gchar **attribute_names);

G_END_DECLS

#endif