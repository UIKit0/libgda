/*
 * Copyright (C) 2011 Vivien Malerba <malerba@gnome-db.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#undef GDA_DISABLE_DEPRECATED
#include "gda-data-pivot.h"

#include <string.h>
#include <glib/gi18n-lib.h>
#include <libgda/gda-enums.h>
#include <libgda/gda-holder.h>
#include <libgda/gda-data-model.h>
#include <libgda/gda-data-model-array.h>
#include <libgda/gda-data-model-iter.h>
#include <libgda/gda-row.h>
#include <libgda/gda-util.h>
#include <libgda/gda-blob-op.h>
#include <libgda/gda-sql-builder.h>
#include <virtual/libgda-virtual.h>
#include <sql-parser/gda-sql-parser.h>
#include <libgda/sql-parser/gda-sql-statement.h>

typedef struct {
	GValue *value;
	gint    column_fields_index;
	gint    data_pos; /* index in pivot->priv->data_fields, or -1 if no index */
} ColumnData;
static guint column_data_hash (gconstpointer key);
static gboolean column_data_equal (gconstpointer a, gconstpointer b);
static void column_data_free (ColumnData *cdata);


typedef struct {
	gint    row;
	gint    col;
	GArray *values; /* array of #GValue, none will be GDA_TYPE_NULL, and there is at least always 1 GValue */
	GdaDataPivotAggregate aggregate;
	GValue *computed_value;
} CellData;
static guint cell_data_hash (gconstpointer key);
static gboolean cell_data_equal (gconstpointer a, gconstpointer b);
static void cell_data_free (CellData *cdata);
static void cell_data_compute_aggregate (CellData *cdata);

static GValue *aggregate_get_empty_value (GdaDataPivotAggregate aggregate);


struct _GdaDataPivotPrivate {
	GdaDataModel  *model; /* data to analyse */

	GdaConnection *vcnc; /* to use data in @model for row and column fields */

	GArray        *row_fields; /* array of (gchar *) field specifications */
	GArray        *column_fields; /* array of (gchar *) field specifications */
	GArray        *data_fields; /* array of (gchar *) field specifications */
	GArray        *data_aggregates; /* array of GdaDataPivotAggregate, corresponding to @data_fields */

	/* computed data */
	GArray        *columns; /* Array of GdaColumn objects, for ALL columns! */
	GdaDataModel  *results;
};

/* properties */
enum
{
        PROP_0,
	PROP_MODEL,
};

#define TABLE_NAME "data"

static void gda_data_pivot_class_init (GdaDataPivotClass *klass);
static void gda_data_pivot_init       (GdaDataPivot *model,
					      GdaDataPivotClass *klass);
static void gda_data_pivot_dispose    (GObject *object);
static void gda_data_pivot_finalize   (GObject *object);

static void gda_data_pivot_set_property (GObject *object,
					 guint param_id,
					 const GValue *value,
					 GParamSpec *pspec);
static void gda_data_pivot_get_property (GObject *object,
					 guint param_id,
					 GValue *value,
					 GParamSpec *pspec);

static guint _gda_value_hash (gconstpointer key);
static guint _gda_row_hash (gconstpointer key);
static gboolean _gda_row_equal (gconstpointer a, gconstpointer b);
static gboolean create_vcnc (GdaDataPivot *pivot, GError **error);
static gboolean bind_source_model (GdaDataPivot *pivot, GError **error);
static void clean_previous_population (GdaDataPivot *pivot);

/* GdaDataModel interface */
static void                 gda_data_pivot_data_model_init (GdaDataModelIface *iface);
static gint                 gda_data_pivot_get_n_rows      (GdaDataModel *model);
static gint                 gda_data_pivot_get_n_columns   (GdaDataModel *model);
static GdaColumn           *gda_data_pivot_describe_column (GdaDataModel *model, gint col);
static GdaDataModelAccessFlags gda_data_pivot_get_access_flags(GdaDataModel *model);
static const GValue        *gda_data_pivot_get_value_at    (GdaDataModel *model, gint col, gint row, GError **error);

static GObjectClass *parent_class = NULL;
static GStaticMutex provider_mutex = G_STATIC_MUTEX_INIT;
static GdaVirtualProvider *virtual_provider = NULL;

/**
 * gda_data_pivot_get_type
 *
 * Returns: the #GType of GdaDataPivot.
 */
GType
gda_data_pivot_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		static GStaticMutex registering = G_STATIC_MUTEX_INIT;
		static const GTypeInfo info = {
			sizeof (GdaDataPivotClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) gda_data_pivot_class_init,
			NULL,
			NULL,
			sizeof (GdaDataPivot),
			0,
			(GInstanceInitFunc) gda_data_pivot_init,
			0
		};

		static const GInterfaceInfo data_model_info = {
			(GInterfaceInitFunc) gda_data_pivot_data_model_init,
			NULL,
			NULL
		};

		g_static_mutex_lock (&registering);
		if (type == 0) {
			type = g_type_register_static (G_TYPE_OBJECT, "GdaDataPivot", &info, 0);
			g_type_add_interface_static (type, GDA_TYPE_DATA_MODEL, &data_model_info);
		}
		g_static_mutex_unlock (&registering);
	}
	return type;
}

static void 
gda_data_pivot_class_init (GdaDataPivotClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	/* properties */
	object_class->set_property = gda_data_pivot_set_property;
        object_class->get_property = gda_data_pivot_get_property;
	g_object_class_install_property (object_class, PROP_MODEL,
                                         g_param_spec_object ("model", NULL, "Data model from which data is analysed",
                                                              GDA_TYPE_DATA_MODEL,
							      G_PARAM_READABLE | G_PARAM_WRITABLE));

	/* virtual functions */
	object_class->dispose = gda_data_pivot_dispose;
	object_class->finalize = gda_data_pivot_finalize;
}

static void
gda_data_pivot_data_model_init (GdaDataModelIface *iface)
{
	iface->i_get_n_rows = gda_data_pivot_get_n_rows;
	iface->i_get_n_columns = gda_data_pivot_get_n_columns;
	iface->i_describe_column = gda_data_pivot_describe_column;
        iface->i_get_access_flags = gda_data_pivot_get_access_flags;
	iface->i_get_value_at = gda_data_pivot_get_value_at;
	iface->i_get_attributes_at = NULL;

	iface->i_create_iter = NULL;
	iface->i_iter_at_row = NULL;
	iface->i_iter_next = NULL;
	iface->i_iter_prev = NULL;

	iface->i_set_value_at = NULL;
	iface->i_iter_set_value = NULL;
	iface->i_set_values = NULL;
        iface->i_append_values = NULL;
	iface->i_append_row = NULL;
	iface->i_remove_row = NULL;
	iface->i_find_row = NULL;
	
	iface->i_set_notify = NULL;
	iface->i_get_notify = NULL;
	iface->i_send_hint = NULL;
}

static void
gda_data_pivot_init (GdaDataPivot *model, G_GNUC_UNUSED GdaDataPivotClass *klass)
{
	g_return_if_fail (GDA_IS_DATA_PIVOT (model));
	model->priv = g_new0 (GdaDataPivotPrivate, 1);
	model->priv->model = NULL;
}

static void
clean_previous_population (GdaDataPivot *pivot)
{
	if (pivot->priv->results) {
		g_object_unref ((GObject*) pivot->priv->results);
		pivot->priv->results = NULL;
	}

	if (pivot->priv->columns) {
		guint i;
		for (i = 0; i < pivot->priv->columns->len; i++) {
			GObject *obj;
			obj = g_array_index (pivot->priv->columns, GObject*, i);
			g_object_unref (obj);
		}
		g_array_free (pivot->priv->columns, TRUE);
		pivot->priv->columns = NULL;
	}
}

static void
gda_data_pivot_dispose (GObject *object)
{
	GdaDataPivot *model = (GdaDataPivot *) object;

	g_return_if_fail (GDA_IS_DATA_PIVOT (model));

	/* free memory */
	if (model->priv) {
		clean_previous_population (model);

		if (model->priv->row_fields) {
			guint i;
			for (i = 0; i < model->priv->row_fields->len; i++) {
				gchar *tmp;
				tmp = g_array_index (model->priv->row_fields, gchar*, i);
				g_free (tmp);
			}
			g_array_free (model->priv->row_fields, TRUE);
			model->priv->row_fields = NULL;
		}

		if (model->priv->column_fields) {
			guint i;
			for (i = 0; i < model->priv->column_fields->len; i++) {
				gchar *tmp;
				tmp = g_array_index (model->priv->column_fields, gchar*, i);
				g_free (tmp);
			}
			g_array_free (model->priv->column_fields, TRUE);
			model->priv->column_fields = NULL;
		}

		if (model->priv->data_fields) {
			guint i;
			for (i = 0; i < model->priv->data_fields->len; i++) {
				gchar *tmp;
				tmp = g_array_index (model->priv->data_fields, gchar*, i);
				g_free (tmp);
			}
			g_array_free (model->priv->data_fields, TRUE);
			model->priv->data_fields = NULL;
		}

		if (model->priv->data_aggregates) {
			g_array_free (model->priv->data_aggregates, TRUE);
			model->priv->data_aggregates = NULL;
		}

		if (model->priv->vcnc) {
			g_object_unref (model->priv->vcnc);
			model->priv->vcnc = NULL;
		}

		if (model->priv->model) {
			g_object_unref (model->priv->model);
			model->priv->model = NULL;
		}
	}

	/* chain to parent class */
	parent_class->dispose (object);
}

static void
gda_data_pivot_finalize (GObject *object)
{
	GdaDataPivot *model = (GdaDataPivot *) object;

	g_return_if_fail (GDA_IS_DATA_PIVOT (model));

	/* free memory */
	if (model->priv) {
		g_free (model->priv);
		model->priv = NULL;
	}

	/* chain to parent class */
	parent_class->finalize (object);
}

/* module error */
GQuark gda_data_pivot_error_quark (void)
{
        static GQuark quark;
        if (!quark)
                quark = g_quark_from_static_string ("gda_data_pivot_error");
        return quark;
}

static void
gda_data_pivot_set_property (GObject *object,
			     guint param_id,
			     const GValue *value,
			     GParamSpec *pspec)
{
	GdaDataPivot *model;

	model = GDA_DATA_PIVOT (object);
	if (model->priv) {
		switch (param_id) {
		case PROP_MODEL: {
			GdaDataModel *mod = g_value_get_object (value);

			clean_previous_population (model);

			if (mod) {
				g_return_if_fail (GDA_IS_DATA_MODEL (mod));
  
                                if (model->priv->model) {
					if (model->priv->vcnc)
						gda_vconnection_data_model_remove (GDA_VCONNECTION_DATA_MODEL (model->priv->vcnc),
										   TABLE_NAME, NULL);
					g_object_unref (model->priv->model);
				}

				model->priv->model = mod;
				g_object_ref (mod);
			}
			break;
		}
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
			break;
		}
	}
}

static void
gda_data_pivot_get_property (GObject *object,
			     guint param_id,
			     GValue *value,
			     GParamSpec *pspec)
{
	GdaDataPivot *model;

	model = GDA_DATA_PIVOT (object);
	if (model->priv) {
		switch (param_id) {
		case PROP_MODEL:
			g_value_set_object (value, G_OBJECT (model->priv->model));
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
			break;
		}
	}
}

/**
 * gda_data_pivot_new:
 * @model: (allow-none): a #GdaDataModel to analyse data from, or %NULL
 *
 * Creates a new #GdaDataModel which will contain analysed data from @model.
 *
 * Returns: a pointer to the newly created #GdaDataModel.
 */
GdaDataModel *
gda_data_pivot_new (GdaDataModel *model)
{
	GdaDataPivot *retmodel;

	g_return_val_if_fail (!model || GDA_IS_DATA_MODEL (model), NULL);
	
	retmodel = g_object_new (GDA_TYPE_DATA_PIVOT,
				 "model", model, NULL);

	return GDA_DATA_MODEL (retmodel);
}

/*
 * GdaDataModel interface implementation
 */
static gint
gda_data_pivot_get_n_rows (GdaDataModel *model)
{
	GdaDataPivot *pivot;
	g_return_val_if_fail (GDA_IS_DATA_PIVOT (model), -1);
	pivot = GDA_DATA_PIVOT (model);
	g_return_val_if_fail (pivot->priv, -1);

	if (pivot->priv->results)
		return gda_data_model_get_n_rows (pivot->priv->results);
	else
		return -1;
}

static gint
gda_data_pivot_get_n_columns (GdaDataModel *model)
{
	GdaDataPivot *pivot;
	g_return_val_if_fail (GDA_IS_DATA_PIVOT (model), 0);
	pivot = GDA_DATA_PIVOT (model);
	g_return_val_if_fail (pivot->priv, 0);
	
	if (pivot->priv->columns)
		return (gint) pivot->priv->columns->len;
	else
		return 0;
}

static GdaColumn *
gda_data_pivot_describe_column (GdaDataModel *model, gint col)
{
	GdaDataPivot *pivot;
	GdaColumn *column = NULL;
	g_return_val_if_fail (GDA_IS_DATA_PIVOT (model), NULL);
	pivot = GDA_DATA_PIVOT (model);
	g_return_val_if_fail (pivot->priv, NULL);

	if (pivot->priv->columns && (col < (gint) pivot->priv->columns->len))
		column = g_array_index (pivot->priv->columns, GdaColumn*, col);
	else {
		if (pivot->priv->columns->len > 0)
			g_warning ("Column %d out of range (0-%d)", col,
				   (gint) pivot->priv->columns->len);
		else
			g_warning ("No column defined");
	}

	return column;
}

static GdaDataModelAccessFlags
gda_data_pivot_get_access_flags (GdaDataModel *model)
{
	GdaDataPivot *pivot;

	g_return_val_if_fail (GDA_IS_DATA_PIVOT (model), 0);
	pivot = GDA_DATA_PIVOT (model);
	g_return_val_if_fail (pivot->priv, 0);
	
	return GDA_DATA_MODEL_ACCESS_RANDOM;
}

static const GValue *
gda_data_pivot_get_value_at (GdaDataModel *model, gint col, gint row, GError **error)
{
	GdaDataPivot *pivot;

	g_return_val_if_fail (GDA_IS_DATA_PIVOT (model), NULL);
	pivot = GDA_DATA_PIVOT (model);
	g_return_val_if_fail (pivot->priv, NULL);
	g_return_val_if_fail (pivot->priv->model, NULL);
	g_return_val_if_fail (row >= 0, NULL);
	g_return_val_if_fail (col >= 0, NULL);

	if (! pivot->priv->results) {
		g_set_error (error, GDA_DATA_PIVOT_ERROR, GDA_DATA_PIVOT_USAGE_ERROR,
			     "%s", _("Pivot model not populated"));
		return NULL;
	}
	return gda_data_model_get_value_at (pivot->priv->results, col, row, error);
}

static GValue *
aggregate_get_empty_value (GdaDataPivotAggregate aggregate)
{
	GValue *value;
	switch (aggregate) {
	case GDA_DATA_PIVOT_MIN:
	case GDA_DATA_PIVOT_MAX:
	case GDA_DATA_PIVOT_AVG:
		return gda_value_new_null ();
	case GDA_DATA_PIVOT_SUM:
	case GDA_DATA_PIVOT_COUNT:
		value = gda_value_new (G_TYPE_INT);
		g_value_set_int (value, 0);
		return value;
		break;
	default:
		return gda_value_new_null ();
	}
}

static gint64
compute_for_gint (GdaDataPivotAggregate type, GArray *values)
{
	guint i;
	gint64 res = 0;
	for (i = 0; i < values->len; i++) {
		GValue *v;
		v = g_array_index (values, GValue*, i);
		if (type == GDA_DATA_PIVOT_SUM)
			res += g_value_get_int (v);
		else {
			if (i == 0)
				res = g_value_get_int (v);
			else if (type == GDA_DATA_PIVOT_MIN)
				res = MIN (res, g_value_get_int (v));
			else
				res = MAX (res, g_value_get_int (v));
		}
		gda_value_free (v);
	}
	return res;
}

static gfloat
compute_for_gfloat (GdaDataPivotAggregate type, GArray *values)
{
	guint i;
	gfloat res = 0;
	for (i = 0; i < values->len; i++) {
		GValue *v;
		v = g_array_index (values, GValue*, i);
		if (type == GDA_DATA_PIVOT_SUM)
			res += g_value_get_float (v);
		else {
			if (i == 0)
				res = g_value_get_float (v);
			else if (type == GDA_DATA_PIVOT_MIN)
				res = MIN (res, g_value_get_float (v));
			else
				res = MAX (res, g_value_get_float (v));
		}
		gda_value_free (v);
	}
	return res;
}

static gdouble
compute_for_gdouble (GdaDataPivotAggregate type, GArray *values)
{
	guint i;
	gdouble res = 0;
	for (i = 0; i < values->len; i++) {
		GValue *v;
		v = g_array_index (values, GValue*, i);
		if (type == GDA_DATA_PIVOT_SUM)
			res += g_value_get_double (v);
		else {
			if (i == 0)
				res = g_value_get_double (v);
			else if (type == GDA_DATA_PIVOT_MIN)
				res = MIN (res, g_value_get_double (v));
			else
				res = MAX (res, g_value_get_double (v));
		}
		gda_value_free (v);
	}
	return res;
}

/*
 * Sets cdata->computed_value to a #GValue
 * if an error occurs then cdata->computed_value is set to %NULL
 *
 * Also frees cdata->values.
 */
static void
cell_data_compute_aggregate (CellData *cdata)
{
	guint i;
	switch (cdata->aggregate) {
	case GDA_DATA_PIVOT_AVG:
	case GDA_DATA_PIVOT_SUM: {
		GValue *v;
		g_assert (cdata->values && (cdata->values->len > 0));
		v = g_array_index (cdata->values, GValue*, 0);
		if ((G_VALUE_TYPE (v) == G_TYPE_INT) || (G_VALUE_TYPE (v) == G_TYPE_INT64) ||
		    (G_VALUE_TYPE (v) == G_TYPE_CHAR) || (G_VALUE_TYPE (v) == GDA_TYPE_SHORT)) {
			gint64 sum;
			sum = compute_for_gint (GDA_DATA_PIVOT_SUM, cdata->values);
			if (cdata->aggregate == GDA_DATA_PIVOT_AVG) {
				cdata->computed_value = gda_value_new (G_TYPE_DOUBLE);
				g_value_set_double (cdata->computed_value,
						    sum / cdata->values->len);
			}
			else {
				cdata->computed_value = gda_value_new (G_TYPE_INT64);
				g_value_set_int64 (cdata->computed_value, sum);
			}
		}
		else if (G_VALUE_TYPE (v) == G_TYPE_FLOAT) {
			gfloat sum;
			sum = compute_for_gfloat (GDA_DATA_PIVOT_SUM, cdata->values);
			cdata->computed_value = gda_value_new (G_TYPE_FLOAT);
			if (cdata->aggregate == GDA_DATA_PIVOT_AVG)
				g_value_set_float (cdata->computed_value,
						   sum / cdata->values->len);
			else
				g_value_set_float (cdata->computed_value, sum);
		}
		else if (G_VALUE_TYPE (v) == G_TYPE_DOUBLE) {
			gdouble sum;
			sum = compute_for_gdouble (GDA_DATA_PIVOT_SUM, cdata->values);
			cdata->computed_value = gda_value_new (G_TYPE_DOUBLE);
			if (cdata->aggregate == GDA_DATA_PIVOT_AVG)
				g_value_set_double (cdata->computed_value,
						   sum / cdata->values->len);
			else
				g_value_set_double (cdata->computed_value, sum);
		}
		else {
			/* error: can't compute aggregate */
			cdata->computed_value = NULL;
		}
		break;
	}
	case GDA_DATA_PIVOT_COUNT: {
		guint64 count = 0;
		for (i = 0; i < cdata->values->len; i++) {
			GValue *value;
			value = g_array_index (cdata->values, GValue*, i);
			count += 1;
			gda_value_free (value);
		}
		cdata->computed_value = gda_value_new (G_TYPE_UINT64);
		g_value_set_uint64 (cdata->computed_value, count);
		break;
	}
	case GDA_DATA_PIVOT_MAX:
	case GDA_DATA_PIVOT_MIN: {
		GValue *v;
		g_assert (cdata->values && (cdata->values->len > 0));
		v = g_array_index (cdata->values, GValue*, 0);
		if (G_VALUE_TYPE (v) == G_TYPE_INT) {
			gint64 res = 0;
			res = compute_for_gint (cdata->aggregate, cdata->values);
			cdata->computed_value = gda_value_new (G_TYPE_INT);
			g_value_set_int (cdata->computed_value, (gint) res);
		}
		else if (G_VALUE_TYPE (v) == G_TYPE_FLOAT) {
			gfloat res;
			res = compute_for_gfloat (cdata->aggregate, cdata->values);
			cdata->computed_value = gda_value_new (G_TYPE_FLOAT);
			g_value_set_float (cdata->computed_value, res);
		}
		else if (G_VALUE_TYPE (v) == G_TYPE_DOUBLE) {
			gdouble res;
			res = compute_for_gdouble (cdata->aggregate, cdata->values);
			cdata->computed_value = gda_value_new (G_TYPE_DOUBLE);
			g_value_set_double (cdata->computed_value, res);
		}
		else {
			/* error: can't compute aggregate */
			cdata->computed_value = NULL;
		}
		break;
	}
	default:
		TO_IMPLEMENT;
	}

	/* free tmp data (don't free each value, it has already been done) */
	g_array_free (cdata->values, TRUE);
	cdata->values = NULL;
}

static GdaSqlStatement *
parse_field_spec (GdaDataPivot *pivot, const gchar *field, const gchar *alias, GError **error)
{
	GdaSqlParser *parser;
	gchar *sql;
	GdaStatement *stmt;
	const gchar *remain;
	GError *lerror = NULL;

	g_return_val_if_fail (field, FALSE);

	if (! bind_source_model (pivot, error))
		return NULL;

	parser = gda_connection_create_parser (pivot->priv->vcnc);
	g_assert (parser);
	if (alias)
		sql = g_strdup_printf ("SELECT %s AS %s FROM " TABLE_NAME, field, alias);
	else
		sql = g_strdup_printf ("SELECT %s FROM " TABLE_NAME, field);
	stmt = gda_sql_parser_parse_string (parser, sql, &remain, &lerror);
	g_free (sql);
	g_object_unref (parser);
	if (!stmt || remain) {
		g_set_error (error, GDA_DATA_PIVOT_ERROR, GDA_DATA_PIVOT_FIELD_FORMAT_ERROR,
			     _("Wrong field format error: %s"),
			     lerror && lerror->message ? lerror->message : _("No detail"));
		g_clear_error (&lerror);
		return NULL;
	}
	
	/* test syntax: a valid SQL expression is required */
	GdaSqlStatement *sqlst = NULL;
	g_object_get ((GObject*) stmt, "structure", &sqlst, NULL);

	if (sqlst->stmt_type != GDA_SQL_STATEMENT_SELECT) {
		g_object_unref (stmt);
		gda_sql_statement_free (sqlst);
		g_set_error (error, GDA_DATA_PIVOT_ERROR, GDA_DATA_PIVOT_FIELD_FORMAT_ERROR,
			     "%s", _("Wrong field format"));
		return NULL;
	}
	
	/*
	  gchar *tmp = gda_sql_statement_serialize (sqlst);
	  g_print ("SQLST [%p, %s]\n", sqlst, tmp);
	  g_free (tmp);
	*/

	/* further tests */
	GdaDataModel *model;
	model = gda_connection_statement_execute_select (pivot->priv->vcnc, stmt, NULL, &lerror);
	g_object_unref (stmt);
	if (!model) {
		g_set_error (error, GDA_DATA_PIVOT_ERROR, GDA_DATA_PIVOT_FIELD_FORMAT_ERROR,
			     _("Wrong field format error: %s"),
			     lerror && lerror->message ? lerror->message : _("No detail"));
		g_clear_error (&lerror);
		return NULL;
	}
	/*
	  g_print ("================= Pivot source:\n");
	  gda_data_model_dump (model, NULL);
	*/

	g_object_unref (model);
	return sqlst;
}

/**
 * gda_data_pivot_add_field:
 * @pivot: a #GdaDataPivot object
 * @field_type: the type of field to add
 * @field: the field description, see below
 * @alias: (allow-none): the field alias, or %NULL
 * @error: (allow-none): ta place to store errors, or %NULL
 *
 * Specifies that @field has to be included in the analysis.
 * @field is a field specification with the following accepted syntaxes:
 * <itemizedlist>
 *   <listitem><para>a column name in the source data model (see <link linkend="gda-data-model-get-column-index">gda_data_model_get_column_index()</link>); or</para></listitem>
 *   <listitem><para>an SQL expression involving a column name in the source data model, for example:
 *   <programlisting>
 * price
 * firstname || ' ' || lastname 
 * nb BETWEEN 5 AND 10</programlisting>
 * </para></listitem>
 * </itemizedlist>
 *
 * It is also possible to specify several fields to be added, while separating them by a comma (in effect
 * still forming a valid SQL syntax).
 *
 * Returns: %TRUE if no error occurred
 *
 * Since: 4.2.10
 */
gboolean
gda_data_pivot_add_field (GdaDataPivot *pivot, GdaDataPivotFieldType field_type,
			  const gchar *field, const gchar *alias, GError **error)
{
	gchar *tmp;
	GError *lerror = NULL;

	g_return_val_if_fail (GDA_IS_DATA_PIVOT (pivot), FALSE);
	g_return_val_if_fail (field, FALSE);

	GdaSqlStatement *sqlst;
	sqlst = parse_field_spec (pivot, field, alias, error);
	if (! sqlst)
		return FALSE;
	
	GArray *array;
	if (field_type == GDA_DATA_PIVOT_FIELD_ROW) {
		if (! pivot->priv->row_fields)
			pivot->priv->row_fields = g_array_new (FALSE, FALSE, sizeof (gchar*));
		array = pivot->priv->row_fields;
	}
	else {
		if (! pivot->priv->column_fields)
			pivot->priv->column_fields = g_array_new (FALSE, FALSE, sizeof (gchar*));
		array = pivot->priv->column_fields;
	}

	GdaSqlStatementSelect *sel;
	GSList *sf_list;
	sel = (GdaSqlStatementSelect*) sqlst->contents;
	for (sf_list = sel->expr_list; sf_list; sf_list = sf_list->next) {
		GdaSqlBuilder *b;
		GdaSqlBuilderId bid;
		GdaSqlSelectField *sf;
		sf = (GdaSqlSelectField*) sf_list->data;
		b = gda_sql_builder_new (GDA_SQL_STATEMENT_SELECT);
		gda_sql_builder_select_add_target_id (b,
						      gda_sql_builder_add_id (b, "T"),
						      NULL);
		bid = gda_sql_builder_import_expression (b, sf->expr);

		if (bid == 0) {
			g_set_error (error, GDA_DATA_PIVOT_ERROR,
				     GDA_DATA_PIVOT_FIELD_FORMAT_ERROR,
				     _("Wrong field format"));
			gda_sql_statement_free (sqlst);
			return FALSE;
		}
		gda_sql_builder_add_field_value_id (b, bid, 0);
		gchar *sql;
		GdaStatement *stmt;
		stmt = gda_sql_builder_get_statement (b, &lerror);
		g_object_unref (b);
		if (!stmt) {
			gda_sql_statement_free (sqlst);
			g_set_error (error, GDA_DATA_PIVOT_ERROR,
				     GDA_DATA_PIVOT_FIELD_FORMAT_ERROR,
				     _("Wrong field format error: %s"),
				     lerror && lerror->message ? lerror->message : _("No detail"));
			g_clear_error (&lerror);
			return FALSE;
		}
		sql = gda_statement_to_sql (stmt, NULL, NULL);
		g_object_unref (stmt);
		if (!sql) {
			g_set_error (error, GDA_DATA_PIVOT_ERROR,
				     GDA_DATA_PIVOT_FIELD_FORMAT_ERROR,
				     _("Wrong field format"));
			gda_sql_statement_free (sqlst);
			return FALSE;
		}
		/*g_print ("PART [%s][%s]\n", sql, sf->as);*/
		tmp = sql + 7; /* remove the "SELECT " start */
		tmp [strlen (tmp) - 7] = 0; /* remove the " FROM T" end */
		if (sf->as && *(sf->as)) {
			gchar *tmp2;
			tmp2 = g_strdup_printf ("%s AS %s", tmp, sf->as);
			g_array_append_val (array, tmp2);
		}
		else {
			tmp = g_strdup (tmp);
			g_array_append_val (array, tmp);
		}
		g_free (sql);
	}
	
	gda_sql_statement_free (sqlst);

	clean_previous_population (pivot);

	return TRUE;
}

/**
 * gda_data_pivot_add_data:
 * @pivot: a #GdaDataPivot object
 * @aggregate_type: the type of aggregate operation to perform
 * @field: the field description, see below
 * @alias: (allow-none): the field alias, or %NULL
 * @error: (allow-none): ta place to store errors, or %NULL
 *
 * Specifies that @field has to be included in the analysis.
 * @field is a field specification with the following accepted syntaxes:
 * <itemizedlist>
 *   <listitem><para>a column name in the source data model (see <link linkend="gda-data-model-get-column-index">gda_data_model_get_column_index()</link>); or</para></listitem>
 *   <listitem><para>an SQL expression involving a column name in the source data model, for examples:
 *   <programlisting>
 * price
 * firstname || ' ' || lastname 
 * nb BETWEEN 5 AND 10</programlisting>
 * </para></listitem>
 * </itemizedlist>
 *
 * It is also possible to specify several fields to be added, while separating them by a comma (in effect
 * still forming a valid SQL syntax).
 *
 * Returns: %TRUE if no error occurred
 *
 * Since: 4.2.10
 */
gboolean
gda_data_pivot_add_data (GdaDataPivot *pivot, GdaDataPivotAggregate aggregate_type,
			 const gchar *field, const gchar *alias, GError **error)
{
	gchar *tmp;
	GError *lerror = NULL;

	g_return_val_if_fail (GDA_IS_DATA_PIVOT (pivot), FALSE);
	g_return_val_if_fail (field, FALSE);

	GdaSqlStatement *sqlst;
	sqlst = parse_field_spec (pivot, field, alias, error);
	if (! sqlst)
		return FALSE;
	
	if (! pivot->priv->data_fields)
		pivot->priv->data_fields = g_array_new (FALSE, FALSE, sizeof (gchar*));
	if (! pivot->priv->data_aggregates)
		pivot->priv->data_aggregates = g_array_new (FALSE, FALSE, sizeof (GdaDataPivotAggregate));

	GdaSqlStatementSelect *sel;
	GSList *sf_list;
	sel = (GdaSqlStatementSelect*) sqlst->contents;
	for (sf_list = sel->expr_list; sf_list; sf_list = sf_list->next) {
		GdaSqlBuilder *b;
		GdaSqlBuilderId bid;
		GdaSqlSelectField *sf;
		sf = (GdaSqlSelectField*) sf_list->data;
		b = gda_sql_builder_new (GDA_SQL_STATEMENT_SELECT);
		gda_sql_builder_select_add_target_id (b,
						      gda_sql_builder_add_id (b, "T"),
						      NULL);
		bid = gda_sql_builder_import_expression (b, sf->expr);

		if (bid == 0) {
			g_set_error (error, GDA_DATA_PIVOT_ERROR,
				     GDA_DATA_PIVOT_FIELD_FORMAT_ERROR,
				     _("Wrong field format"));
			gda_sql_statement_free (sqlst);
			return FALSE;
		}
		gda_sql_builder_add_field_value_id (b, bid, 0);
		gchar *sql;
		GdaStatement *stmt;
		stmt = gda_sql_builder_get_statement (b, &lerror);
		g_object_unref (b);
		if (!stmt) {
			gda_sql_statement_free (sqlst);
			g_set_error (error, GDA_DATA_PIVOT_ERROR,
				     GDA_DATA_PIVOT_FIELD_FORMAT_ERROR,
				     _("Wrong field format error: %s"),
				     lerror && lerror->message ? lerror->message : _("No detail"));
			g_clear_error (&lerror);
			return FALSE;
		}
		sql = gda_statement_to_sql (stmt, NULL, NULL);
		g_object_unref (stmt);
		if (!sql) {
			g_set_error (error, GDA_DATA_PIVOT_ERROR,
				     GDA_DATA_PIVOT_FIELD_FORMAT_ERROR,
				     _("Wrong field format"));
			gda_sql_statement_free (sqlst);
			return FALSE;
		}
		/*g_print ("PART [%s][%s]\n", sql, sf->as);*/
		tmp = sql + 7; /* remove the "SELECT " start */
		tmp [strlen (tmp) - 7] = 0; /* remove the " FROM T" end */
		if (sf->as && *(sf->as)) {
			gchar *tmp2;
			tmp2 = g_strdup_printf ("%s AS %s", tmp, sf->as);
			g_array_append_val (pivot->priv->data_fields, tmp2);
		}
		else {
			tmp = g_strdup (tmp);
			g_array_append_val (pivot->priv->data_fields, tmp);
		}
		g_array_append_val (pivot->priv->data_aggregates, aggregate_type);
		g_free (sql);
	}
	
	gda_sql_statement_free (sqlst);

	clean_previous_population (pivot);

	return TRUE;
}

/**
 * gda_data_pivot_populate:
 * @pivot: a #GdaDataPivot object
 * @error: (allow-none): ta place to store errors, or %NULL
 *
 * Acutally populates @pivot by analysing the data from the provided data model.
 *
 * Returns: %TRUE if no error occurred.
 *
 * Since: 4.2.10
 */
gboolean
gda_data_pivot_populate (GdaDataPivot *pivot, GError **error)
{
	gboolean retval = FALSE;
	g_return_val_if_fail (GDA_IS_DATA_PIVOT (pivot), FALSE);

	if (!pivot->priv->column_fields || (pivot->priv->column_fields->len == 0)) {
		g_set_error (error, GDA_DATA_PIVOT_ERROR, GDA_DATA_PIVOT_USAGE_ERROR,
			     "%s", _("No column field defined"));
		return FALSE;
	}

	if (!pivot->priv->row_fields || (pivot->priv->row_fields->len == 0)) {
		g_set_error (error, GDA_DATA_PIVOT_ERROR, GDA_DATA_PIVOT_USAGE_ERROR,
			     "%s", _("No row field defined"));
		return FALSE;
	}

	clean_previous_population (pivot);

	/*
	 * create data model extracted using the virtual connection.
	 * The resulting data model's columns are:
	 *   - for pivot->priv->row_fields: 0 to pivot->priv->row_fields->len - 1
	 *   - for pivot->priv->column_fields:
	 *     pivot->priv->row_fields->len to pivot->priv->row_fields->len + pivot->priv->column_fields->len - 1
	 *   - for pivot->priv->data_fields: pivot->priv->row_fields->len + pivot->priv->column_fields->len to pivot->priv->row_fields->len + pivot->priv->column_fields->len + pivot->priv->data_fields->len - 1
	 */
	GString *string;
	guint i;
	string = g_string_new ("SELECT ");
	for (i = 0; i < pivot->priv->row_fields->len; i++) {
		gchar *part;
		part = g_array_index (pivot->priv->row_fields, gchar *, i);
		if (i != 0)
			g_string_append (string, ", ");
		g_string_append (string, part);
	}
	for (i = 0; i < pivot->priv->column_fields->len; i++) {
		gchar *part;
		part = g_array_index (pivot->priv->column_fields, gchar *, i);
		g_string_append (string, ", ");
		g_string_append (string, part);
	}
	if (pivot->priv->data_fields) {
		for (i = 0; i < pivot->priv->data_fields->len; i++) {
			gchar *part;
			part = g_array_index (pivot->priv->data_fields, gchar *, i);
			g_string_append (string, ", ");
			g_string_append (string, part);
		}
	}
	g_string_append (string, " FROM " TABLE_NAME);
	
	GdaStatement *stmt;
	stmt = gda_connection_parse_sql_string (pivot->priv->vcnc, string->str, NULL, NULL);
	g_string_free (string, TRUE);
	if (!stmt) {
		g_set_error (error, GDA_DATA_PIVOT_ERROR, GDA_DATA_PIVOT_INTERNAL_ERROR,
			     "%s", _("Could not get information from source data model"));
		return FALSE;
	}

	GdaDataModel *model;
	model = gda_connection_statement_execute_select_full (pivot->priv->vcnc, stmt, NULL,
							      GDA_STATEMENT_MODEL_CURSOR_FORWARD,
							      //GDA_STATEMENT_MODEL_RANDOM_ACCESS,
							      NULL, NULL);
	g_object_unref (stmt);
	if (!model) {
		g_set_error (error, GDA_DATA_PIVOT_ERROR, GDA_DATA_PIVOT_INTERNAL_ERROR,
			     "%s", _("Could not get information from source data model"));
		return FALSE;
	}

	/*
	g_print ("========== Iterable data model\n");
	gda_data_model_dump (model, NULL);
	*/

	/* iterate through the data model */
	GdaDataModelIter *iter;
	gint col;
	iter = gda_data_model_create_iter (model);
	if (!iter) {
		g_object_unref (model);
		g_set_error (error, GDA_DATA_PIVOT_ERROR, GDA_DATA_PIVOT_INTERNAL_ERROR,
			     "%s", _("Could not get information from source data model"));
		return FALSE;
	}

	pivot->priv->columns = g_array_new (FALSE, FALSE, sizeof (GdaColumn*));
	for (col = 0; col < (gint) pivot->priv->row_fields->len; col++) {
		GdaColumn *column;
		GdaHolder *holder;
		column = gda_column_new ();
		holder = GDA_HOLDER (g_slist_nth_data (GDA_SET (iter)->holders, col));
		
		gda_column_set_name (column, gda_holder_get_id (holder));
		gda_column_set_description (column, gda_holder_get_id (holder));
		gda_column_set_g_type (column, gda_holder_get_g_type (holder));
		gda_column_set_position (column, col);
		g_array_append_val (pivot->priv->columns, column);
	}

	/*
	 * actual computation
	 */
	GHashTable    *column_values_index; /* key = a #GValue (ref held in @column_values),
					     * value = pointer to a guint containing the position
					     * in @columns of the column */

	GArray        *first_rows; /* Array of GdaRow objects, the size of each GdaRow is
				    * the number of row fields defined */
	GHashTable    *first_rows_index; /* key = a #GdaRow (ref held in @first_rows),
					  * value = pointer to a guint containing the position
					  * in @first_rows of the row. */
	GHashTable    *data_hash; /* key = a CellData pointer, value = The CellData pointer
				   * as ref'ed in @data */

	first_rows = g_array_new (FALSE, FALSE, sizeof (GdaRow*));
	first_rows_index = g_hash_table_new_full (_gda_row_hash, _gda_row_equal, NULL, g_free);
	column_values_index = g_hash_table_new_full (column_data_hash, column_data_equal,
						     (GDestroyNotify) column_data_free, g_free);
	data_hash = g_hash_table_new_full (cell_data_hash, cell_data_equal,
					   (GDestroyNotify) cell_data_free, NULL);

	for (;gda_data_model_iter_move_next (iter);) {
		/*
		 * Row handling
		 */
		GdaRow *nrow;
#ifdef GDA_DEBUG_ROWS_HASH
		g_print ("read from iter: ");
#endif
		nrow = gda_row_new ((gint) pivot->priv->row_fields->len);
		for (col = 0; col < (gint) pivot->priv->row_fields->len; col++) {
			const GValue *ivalue;
			GValue *rvalue;
			GError *lerror = NULL;
			ivalue = gda_data_model_iter_get_value_at_e (iter, col, &lerror);
			if (!ivalue || lerror) {
				clean_previous_population (pivot);
				g_object_unref (nrow);
				g_propagate_error (error, lerror);
				goto out;
			}
			rvalue = gda_row_get_value (nrow, col);
			gda_value_reset_with_type (rvalue, G_VALUE_TYPE (ivalue));
			g_value_copy (ivalue, rvalue);
#ifdef GDA_DEBUG_ROWS_HASH
			gchar *tmp;
			tmp = gda_value_stringify (rvalue);
			g_print ("[%s]", tmp);
			g_free (tmp);
#endif
		}

		gint *rowindex; /* *rowindex will contain the actual row of the resulting data mode */
		rowindex = g_hash_table_lookup (first_rows_index, nrow);
		if (rowindex)
			g_object_unref (nrow);
		else {
			g_array_append_val (first_rows, nrow);
			rowindex = g_new (gint, 1);
			*rowindex = first_rows->len - 1;
			g_hash_table_insert (first_rows_index, nrow,
					     rowindex);
		}
#ifdef GDA_DEBUG_ROWS_HASH
		g_print (" => @row %d\n", *rowindex);
#endif

		/*
		 * Column handling
		 */
		for (col = 0; col < (gint) pivot->priv->column_fields->len;
		     col++) {
			const GValue *ivalue;
			GError *lerror = NULL;
			ivalue = gda_data_model_iter_get_value_at_e (iter,
								     col + pivot->priv->row_fields->len,
								     &lerror);
			if (!ivalue || lerror) {
				clean_previous_population (pivot);
				g_propagate_error (error, lerror);
				goto out;
			}

			gint di, dimax;
			if (pivot->priv->data_fields && pivot->priv->data_fields->len > 0) {
				di = 0;
				dimax = pivot->priv->data_fields->len;
			}
			else {
				di = -1;
				dimax = 0;
			}
			for (; di < dimax; di++) {
				ColumnData coldata;
				gint *colindex;
				GdaDataPivotAggregate aggregate;
				coldata.value = (GValue*) ivalue;
				coldata.column_fields_index = col;
				coldata.data_pos = di;
				colindex = g_hash_table_lookup (column_values_index, &coldata);
				if (di >= 0)
					aggregate = g_array_index (pivot->priv->data_aggregates,
								   GdaDataPivotAggregate, di);
				else
					aggregate = GDA_DATA_PIVOT_SUM;

				if (!colindex) {
					/* create new column */
					GdaColumn *column;
					GString *name;
					gchar *tmp;

					name = g_string_new ("");
					if (pivot->priv->column_fields->len > 1) {
						GdaColumn *column;
						column = gda_data_model_describe_column (model,
											 pivot->priv->row_fields->len + col);
						g_string_append_printf (name, "[%s]",
									gda_column_get_name (column));
					}
					tmp = gda_value_stringify (ivalue);
					g_string_append (name, tmp);
					g_free (tmp);
					if ((di >= 0) && (dimax > 0)) {
						GdaColumn *column;
						column = gda_data_model_describe_column (model,
											 pivot->priv->row_fields->len + pivot->priv->column_fields->len + di);
						g_string_append_printf (name, "[%s]",
									gda_column_get_name (column));
					}

					column = gda_column_new ();
					g_object_set_data ((GObject*) column, "agg",
							   GINT_TO_POINTER (aggregate));
					gda_column_set_name (column, name->str);
					gda_column_set_description (column, name->str);
					g_array_append_val (pivot->priv->columns, column);
					gda_column_set_position (column, pivot->priv->columns->len - 1);
					/* don't set the column's type now */
					/*g_print ("New column [%s] @real column %d, type %s\n", name->str, pivot->priv->columns->len - 1, gda_g_type_to_string (gda_column_get_g_type (column)));*/
					g_string_free (name, TRUE);
					
					ColumnData *ncoldata;
					ncoldata = g_new (ColumnData, 1);
					ncoldata->value = gda_value_copy ((GValue*) ivalue);
					ncoldata->column_fields_index = col;
					ncoldata->data_pos = di;
					colindex = g_new (gint, 1);
					*colindex = pivot->priv->columns->len - 1;
					g_hash_table_insert (column_values_index, ncoldata,
							     colindex);
				}
				
				/* compute value to take into account */
				GValue *value = NULL;
				if (di >= 0) {
					const GValue *cvalue;
					GError *lerror = NULL;
					cvalue = gda_data_model_iter_get_value_at_e (iter,
										     pivot->priv->row_fields->len + pivot->priv->column_fields->len + di,
										     &lerror);
					if (!cvalue || lerror) {
						g_object_unref (nrow);
						g_propagate_error (error, lerror);
						goto out;
					}
					if (G_VALUE_TYPE (cvalue) != GDA_TYPE_NULL)
						value = gda_value_copy (cvalue);
				}
				else {
					value = gda_value_new (G_TYPE_INT);
					g_value_set_int (value, 1);
				}

				if (value) {
					/* accumulate data */
					CellData ccdata, *pcdata;
					ccdata.row = *rowindex;
					ccdata.col = *colindex;
					ccdata.values = NULL;
					ccdata.computed_value = NULL;
					pcdata = g_hash_table_lookup (data_hash, &ccdata);
					if (!pcdata) {
						pcdata = g_new (CellData, 1);
						pcdata->row = *rowindex;
						pcdata->col = *colindex;
						pcdata->values = NULL;
						pcdata->computed_value = NULL;
						pcdata->aggregate = aggregate;
						g_hash_table_insert (data_hash, pcdata, pcdata);
					}
					if (!pcdata->values)
						pcdata->values = g_array_new (FALSE, FALSE, sizeof (GValue*));
					g_array_append_val (pcdata->values, value);
					/*g_print ("row %d col %d => [%s]\n", pcdata->row, pcdata->col,
					  gda_value_stringify (value));*/
				}
			}
		}
	}
	if (gda_data_model_iter_get_row (iter) != -1) {
		/* an error occurred! */
		g_print ("////////\n");
		goto out;
	}

	/* compute real data model's values from all the collected data */
	GdaDataModel *results;
	gint ncols, nrows;
	ncols = pivot->priv->columns->len;
	nrows = first_rows->len;
	results = gda_data_model_array_new (ncols);
	for (i = 0; i < pivot->priv->row_fields->len; i++) {
		GdaColumn *acolumn, *ecolumn;
		ecolumn = g_array_index (pivot->priv->columns, GdaColumn*, i);
		acolumn = gda_data_model_describe_column (results, i);
		gda_column_set_g_type (acolumn, gda_column_get_g_type (ecolumn));
		
		gint j;
		for (j = 0; j < nrows; j++) {
			GdaRow *arow, *erow;
			erow = g_array_index (first_rows, GdaRow*, j);
			arow = gda_data_model_array_get_row ((GdaDataModelArray*) results, j, NULL);
			if (!arow) {
				g_assert (gda_data_model_append_row (results, NULL) == j);
				arow = gda_data_model_array_get_row ((GdaDataModelArray*) results, j,
								     NULL);
				g_assert (arow);
			}
			GValue *av, *ev;
			av = gda_row_get_value (arow, i);
			ev = gda_row_get_value (erow, i);
			gda_value_reset_with_type (av, G_VALUE_TYPE (ev));
			g_value_copy (ev, av);
		}
	}
	for (; i < (guint) ncols; i++) {
		GdaColumn *acolumn, *ecolumn;
		ecolumn = g_array_index (pivot->priv->columns, GdaColumn*, i);
		acolumn = gda_data_model_describe_column (results, i);

		gint j;
		for (j = 0; j < nrows; j++) {
			GdaRow *arow, *erow;
			GValue *av;
			erow = g_array_index (first_rows, GdaRow*, j);
			arow = gda_data_model_array_get_row ((GdaDataModelArray*) results, j, NULL);
			av = gda_row_get_value (arow, i);

			CellData ccdata, *pcdata;
			ccdata.row = j;
			ccdata.col = i;
			ccdata.values = NULL;
			ccdata.computed_value = NULL;
			pcdata = g_hash_table_lookup (data_hash, &ccdata);
			if (pcdata) {
				cell_data_compute_aggregate (pcdata);
				if (pcdata->computed_value) {
					gda_value_reset_with_type (av,
							     G_VALUE_TYPE (pcdata->computed_value));
					g_value_copy (pcdata->computed_value, av);
				}
				else
					gda_row_invalidate_value (arow, av);
			}
			else {
				GValue *empty;
				GdaDataPivotAggregate agg;
				agg = GPOINTER_TO_INT (g_object_get_data ((GObject*) ecolumn, "agg"));
				empty = aggregate_get_empty_value (agg);
				gda_value_reset_with_type (av, G_VALUE_TYPE (empty));
				g_value_copy (empty, av);
			}
		}
	}
	pivot->priv->results = results;

	retval = TRUE;

 out:
	if (!retval)
		clean_previous_population (pivot);

	if (data_hash)
		g_hash_table_destroy (data_hash);

	if (first_rows) {
		guint i;
		for (i = 0; i < first_rows->len; i++) {
			GObject *obj;
			obj = g_array_index (first_rows, GObject*, i);
			g_object_unref (obj);
		}
		g_array_free (first_rows, TRUE);
	}

	if (first_rows_index)
		g_hash_table_destroy (first_rows_index);

	g_hash_table_destroy (column_values_index);

	g_object_unref (iter);
	g_object_unref (model);

	return retval;
}

static guint
_gda_value_hash (gconstpointer key)
{
	GValue *v;
	GType vt;
	guint res = 0;
	v = (GValue *) key;
	vt = G_VALUE_TYPE (v);
	if ((vt == G_TYPE_BOOLEAN) || (vt == G_TYPE_INT) || (vt == G_TYPE_UINT) ||
	    (vt == G_TYPE_FLOAT) || (vt == G_TYPE_DOUBLE) ||
	    (vt == G_TYPE_INT64) || (vt == G_TYPE_INT64) ||
	    (vt == GDA_TYPE_SHORT) || (vt == GDA_TYPE_USHORT) ||
	    (vt == G_TYPE_CHAR) || (vt == G_TYPE_UCHAR) ||
	    (vt == GDA_TYPE_NULL) || (vt == G_TYPE_GTYPE) ||
	    (vt == G_TYPE_LONG) || (vt == G_TYPE_ULONG)) {
		const signed char *p, *data;
		data = (signed char*) v;
		res = 5381;
		for (p = data; p < data + sizeof (GValue); p++)
			res = (res << 5) + res + *p;
	}
	else if (vt == G_TYPE_STRING) {
		const gchar *tmp;
		tmp = g_value_get_string (v);
		if (tmp)
			res += g_str_hash (tmp);
	}
	else if ((vt == GDA_TYPE_BINARY) || (vt == GDA_TYPE_BLOB)) {
		const GdaBinary *bin;
		if (vt == GDA_TYPE_BLOB) {
			GdaBlob *blob;
			blob = (GdaBlob*) gda_value_get_blob ((GValue *) v);
			bin = (GdaBinary *) blob;
			if (blob->op &&
			    (bin->binary_length != gda_blob_op_get_length (blob->op)))
				gda_blob_op_read_all (blob->op, blob);
		}
		else
			bin = gda_value_get_binary ((GValue *) v);
		if (bin) {
			glong l;
			for (l = 0; l < bin->binary_length; l++)
				res += (guint) bin->data [l];
		}
	}
	else {
		gchar *tmp;
		tmp = gda_value_stringify (v);
		res += g_str_hash (tmp);
		g_free (tmp);
	}
	return res;
}

static guint
_gda_row_hash (gconstpointer key)
{
	gint i, len;
	GdaRow *r;
	guint res = 0;
	r = (GdaRow*) key;
	len = gda_row_get_length (r);
	for (i = 0; i < len ; i++) {
		GValue *v;
		v = gda_row_get_value (r, i);
		res = (res << 5) + res + _gda_value_hash (v);
	}
	return res;
}

static gboolean
_gda_row_equal (gconstpointer a, gconstpointer b)
{
	gint i, len;
	GdaRow *ra, *rb;
	ra = (GdaRow*) a;
	rb = (GdaRow*) b;
	len = gda_row_get_length (ra);
	g_assert (len == gda_row_get_length (rb));
	for (i = 0; i < len ; i++) {
		GValue *va, *vb;
		va = gda_row_get_value (ra, i);
		vb = gda_row_get_value (rb, i);
		if (gda_value_differ (va, vb))
			return FALSE;
	}
	return TRUE;
}

/*
 * Create a new virtual connection for @pivot
 */
static gboolean
create_vcnc (GdaDataPivot *pivot, GError **error)
{
	GdaConnection *vcnc;
	GError *lerror = NULL;
	if (pivot->priv->vcnc)
		return TRUE;

	g_static_mutex_lock (&provider_mutex);
	if (!virtual_provider)
		virtual_provider = gda_vprovider_data_model_new ();
	g_static_mutex_unlock (&provider_mutex);

	vcnc = gda_virtual_connection_open (virtual_provider, &lerror);
	if (! vcnc) {
		g_print ("Virtual ERROR: %s\n", lerror && lerror->message ? lerror->message : "No detail");
		if (lerror)
			g_error_free (lerror);
		g_set_error (error, GDA_DATA_PIVOT_ERROR, GDA_DATA_PIVOT_INTERNAL_ERROR,
			     "%s", _("Could not create virtual connection"));
		return FALSE;
	}

	pivot->priv->vcnc = vcnc;
	return TRUE;
}

/*
 * Bind @pivot->priv->model as a table named TABLE_NAME in @pivot's virtual connection
 */
static gboolean
bind_source_model (GdaDataPivot *pivot, GError **error)
{	
	if (! pivot->priv->model) {
		g_set_error (error, GDA_DATA_PIVOT_ERROR, GDA_DATA_PIVOT_SOURCE_MODEL_ERROR,
			     "%s", _("No source defined"));
		return FALSE;
	}
	if (! create_vcnc (pivot, error))
		return FALSE;

	if (gda_vconnection_data_model_get_model (GDA_VCONNECTION_DATA_MODEL (pivot->priv->vcnc),
						  TABLE_NAME) == pivot->priv->model) {
		/* already bound */
		return TRUE;
	}

	GError *lerror = NULL;
	if (!gda_vconnection_data_model_add_model (GDA_VCONNECTION_DATA_MODEL (pivot->priv->vcnc),
						   pivot->priv->model,
						   TABLE_NAME, &lerror)) {
		g_set_error (error, GDA_DATA_PIVOT_ERROR, GDA_DATA_PIVOT_SOURCE_MODEL_ERROR,
			     "%s",
			     _("Invalid source data model (may have incompatible column names)"));
		g_clear_error (&lerror);
		return FALSE;
	}

	return TRUE;
}

static
guint column_data_hash (gconstpointer key)
{
	ColumnData *cd;
	cd = (ColumnData*) key;
	return _gda_value_hash (cd->value) + cd->column_fields_index + cd->data_pos;
}

static gboolean
column_data_equal (gconstpointer a, gconstpointer b)
{
	ColumnData *cda, *cdb;
	cda = (ColumnData*) a;
	cdb = (ColumnData*) b;
	if ((cda->column_fields_index != cdb->column_fields_index) ||
	    (cda->data_pos != cdb->data_pos))
		return FALSE;
	return gda_value_differ (cda->value, cdb->value) ? FALSE : TRUE;
}

static void
column_data_free (ColumnData *cdata)
{
	gda_value_free (cdata->value);
	g_free (cdata);
}

static guint
cell_data_hash (gconstpointer key)
{
	CellData *cd;
	cd = (CellData*) key;
	return g_int_hash (&(cd->row)) + g_int_hash (&(cd->col));
}

static gboolean
cell_data_equal (gconstpointer a, gconstpointer b)
{
	CellData *cda, *cdb;
	cda = (CellData*) a;
	cdb = (CellData*) b;
	if ((cda->row == cdb->row) && (cda->col == cdb->col))
		return TRUE;
	else
		return FALSE;
}

static void
cell_data_free (CellData *cdata)
{
	if (cdata->values) {
		guint i;
		for (i = 0; i < cdata->values->len; i++) {
			GValue *value;
			value = g_array_index (cdata->values, GValue*, i);
			gda_value_free (value);
		}
		g_array_free (cdata->values, TRUE);
	}
	gda_value_free (cdata->computed_value);
	g_free (cdata);
}