/* GDA library
 * Copyright (C) 2009 The GNOME Foundation.
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

#ifndef __GDA_TREE_H__
#define __GDA_TREE_H__

#include <glib-object.h>
#include <stdio.h>
#include "gda-decl.h"

G_BEGIN_DECLS

#define GDA_TYPE_TREE            (gda_tree_get_type())
#define GDA_TREE(obj)            (G_TYPE_CHECK_INSTANCE_CAST (obj, GDA_TYPE_TREE, GdaTree))
#define GDA_TREE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST (klass, GDA_TYPE_TREE, GdaTreeClass))
#define GDA_IS_TREE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE(obj, GDA_TYPE_TREE))
#define GDA_IS_TREE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GDA_TYPE_TREE))

/* error reporting */
extern GQuark gda_tree_error_quark (void);
#define GDA_TREE_ERROR gda_tree_error_quark ()

typedef enum {
	GDA_TREE_UNKNOWN_ERROR
} GdaTreeError;

struct _GdaTree {
	GObject           object;
	GdaTreePrivate   *priv;
};

struct _GdaTreeClass {
	GObjectClass      object_class;

	/* Padding for future expansion */
	void (*_gda_reserved1) (void);
	void (*_gda_reserved2) (void);
	void (*_gda_reserved3) (void);
	void (*_gda_reserved4) (void);
};

GType              gda_tree_get_type      (void) G_GNUC_CONST;
GdaTree*           gda_tree_new           (void);
void               gda_tree_add_manager   (GdaTree *tree, GdaTreeManager *manager);

void               gda_tree_clean         (GdaTree *tree);
gboolean           gda_tree_update_all    (GdaTree *tree, GError **error);
gboolean           gda_tree_update_part   (GdaTree *tree, GdaTreeNode *node, GError **error);

GSList            *gda_tree_get_nodes_in_path (GdaTree *tree, const gchar *tree_path, gboolean use_names);
GdaTreeNode       *gda_tree_get_node      (GdaTree *tree, const gchar *tree_path, gboolean use_names);

void               gda_tree_set_attribute (GdaTree *tree, const gchar *attribute, const GValue *value,
					   GDestroyNotify destroy);

void               gda_tree_dump          (GdaTree *tree, GdaTreeNode *node, FILE *stream);

G_END_DECLS

#endif