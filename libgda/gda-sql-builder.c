/* gda-sql-builder.c
 *
 * Copyright (C) 2008 Vivien Malerba
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
#include <stdarg.h>
#include <libgda/gda-sql-builder.h>
#include <libgda/gda-statement.h>
#include <libgda/gda-data-handler.h>
#include <libgda/gda-easy.h>

/*
 * Main static functions
 */
static void gda_sql_builder_class_init (GdaSqlBuilderClass *klass);
static void gda_sql_builder_init (GdaSqlBuilder *builder);
static void gda_sql_builder_dispose (GObject *object);
static void gda_sql_builder_finalize (GObject *object);

static void gda_sql_builder_set_property (GObject *object,
					 guint param_id,
					 const GValue *value,
					 GParamSpec *pspec);
static void gda_sql_builder_get_property (GObject *object,
					 guint param_id,
					 GValue *value,
					 GParamSpec *pspec);

static GStaticRecMutex init_mutex = G_STATIC_REC_MUTEX_INIT;


typedef struct {
	GdaSqlAnyPart *part;
}  SqlPart;

struct _GdaSqlBuilderPrivate {
	GdaSqlStatement *main_stmt;
	GHashTable *parts_hash; /* key = part ID as an guint, value = SqlPart */
	guint next_assigned_id;
};


/* get a pointer to the parents to be able to call their destructor */
static GObjectClass *parent_class = NULL;

/* signals */
enum {
	DUMMY,
	LAST_SIGNAL
};

static gint gda_sql_builder_signals[LAST_SIGNAL] = { 0 };

/* properties */
enum {
	PROP_0,
};

/* module error */
GQuark gda_sql_builder_error_quark (void) {
	static GQuark quark;
	if (!quark)
		quark = g_quark_from_static_string ("gda_sql_builder_error");
	return quark;
}

GType
gda_sql_builder_get_type (void) {
	static GType type = 0;
	
	if (G_UNLIKELY (type == 0)) {
		static const GTypeInfo info = {
			sizeof (GdaSqlBuilderClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) gda_sql_builder_class_init,
			NULL,
			NULL,
			sizeof (GdaSqlBuilder),
			0,
			(GInstanceInitFunc) gda_sql_builder_init
		};
		
		g_static_rec_mutex_lock (&init_mutex);
		if (type == 0)
			type = g_type_register_static (G_TYPE_OBJECT, "GdaSqlBuilder", &info, 0);
		g_static_rec_mutex_unlock (&init_mutex);
	}
	return type;
}


static void
gda_sql_builder_class_init (GdaSqlBuilderClass *klass) 
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);
	
	/* Properties */
	object_class->set_property = gda_sql_builder_set_property;
	object_class->get_property = gda_sql_builder_get_property;
	object_class->dispose = gda_sql_builder_dispose;
	object_class->finalize = gda_sql_builder_finalize;
}

static void
any_part_free (SqlPart *part)
{
	switch (part->part->type) {
	case GDA_SQL_ANY_EXPR:
		gda_sql_expr_free ((GdaSqlExpr*) part->part);
		break;
	default:
		TO_IMPLEMENT;
	}

	g_free (part);
}

static void
gda_sql_builder_init (GdaSqlBuilder *builder) 
{
	builder->priv = g_new0 (GdaSqlBuilderPrivate, 1);
	builder->priv->main_stmt = NULL;
	builder->priv->parts_hash = g_hash_table_new_full (g_int_hash, g_int_equal,
							   g_free, (GDestroyNotify) any_part_free);
	builder->priv->next_assigned_id = G_MAXUINT;
}


/**
 * gda_sql_builder_new
 * @stmt_type: the type of statement to build
 *
 * Create a new #GdaSqlBuilder object to build #GdaStatement or #GdaSqlStatement
 * objects of type @stmt_type
 *
 * Returns: the newly created object, or %NULL if an error occurred (such as unsupported
 * statement type)
 *
 * Since: 4.2
 */
GdaSqlBuilder *
gda_sql_builder_new (GdaSqlStatementType stmt_type) 
{
	GdaSqlBuilder *builder;

	g_return_val_if_fail ((stmt_type == GDA_SQL_STATEMENT_SELECT) ||
			      (stmt_type == GDA_SQL_STATEMENT_UPDATE) ||
			      (stmt_type == GDA_SQL_STATEMENT_INSERT) ||
			      (stmt_type == GDA_SQL_STATEMENT_DELETE), NULL);
	builder = (GdaSqlBuilder*) g_object_new (GDA_TYPE_SQL_BUILDER, NULL);
	builder->priv->main_stmt = gda_sql_statement_new (stmt_type);
	/* FIXME: use a property here and check type for SELECT, INSERT, UPDATE or DELETE only */
	return builder;
}


static void
gda_sql_builder_dispose (GObject *object) 
{
	GdaSqlBuilder *builder;
	
	g_return_if_fail (GDA_IS_SQL_BUILDER (object));
	
	builder = GDA_SQL_BUILDER (object);
	if (builder->priv) {
		if (builder->priv->main_stmt) {
			gda_sql_statement_free (builder->priv->main_stmt);
			builder->priv->main_stmt = NULL;
		}
		if (builder->priv->parts_hash) {
			g_hash_table_destroy (builder->priv->parts_hash);
			builder->priv->parts_hash = NULL;
		}
	}
	
	/* parent class */
	parent_class->dispose (object);
}

static void
gda_sql_builder_finalize (GObject *object)
{
	GdaSqlBuilder *builder;
	
	g_return_if_fail (object != NULL);
	g_return_if_fail (GDA_IS_SQL_BUILDER (object));
	
	builder = GDA_SQL_BUILDER (object);
	if (builder->priv) {
		g_free (builder->priv);
		builder->priv = NULL;
	}
	
	/* parent class */
	parent_class->finalize (object);
}

static void
gda_sql_builder_set_property (GObject *object,
			     guint param_id,
			     const GValue *value,
			     GParamSpec *pspec) 
{
	GdaSqlBuilder *builder;
	
	builder = GDA_SQL_BUILDER (object);
	if (builder->priv) {
		switch (param_id) {
		}
	}
}

static void
gda_sql_builder_get_property (GObject *object,
			     guint param_id,
			     GValue *value,
			     GParamSpec *pspec) 
{
	GdaSqlBuilder *builder;
	builder = GDA_SQL_BUILDER (object);
	
	if (builder->priv) {
		switch (param_id) {
		}
	}
}

static guint
add_part (GdaSqlBuilder *builder, guint id, GdaSqlAnyPart *part)
{
	SqlPart *p;
	guint *realid = g_new0 (guint, 1);
	if (id == 0)
		id = builder->priv->next_assigned_id --;
	*realid = id;
	p = g_new0 (SqlPart, 1);
	p->part = part;
	g_hash_table_insert (builder->priv->parts_hash, realid, p);
	return id;
}

static SqlPart *
get_part (GdaSqlBuilder *builder, guint id, GdaSqlAnyPartType req_type)
{
	SqlPart *p;
	guint lid = id;
	if (id == 0)
		return NULL;
	p = g_hash_table_lookup (builder->priv->parts_hash, &lid);
	if (!p) {
		g_warning (_("Unknown part ID %u"), id);
		return NULL;
	}
	if (p->part->type != req_type) {
		g_warning (_("Unknown part type"));
		return NULL;
	}
	return p;
}

static GdaSqlAnyPart *
use_part (SqlPart *p, GdaSqlAnyPart *parent)
{
	if (!p)
		return NULL;

	/* copy */
	GdaSqlAnyPart *anyp = NULL;
	switch (p->part->type) {
	case GDA_SQL_ANY_EXPR:
		anyp = (GdaSqlAnyPart*) gda_sql_expr_copy ((GdaSqlExpr*) p->part);
		break;
	default:
		TO_IMPLEMENT;
		return NULL;
	}
	if (anyp)
		anyp->parent = parent;
	return anyp;
}

/**
 * gda_sql_builder_get_statement
 * @builder: a #GdaSqlBuilder object
 * @error: a place to store errors, or %NULL
 *
 * Creates a new #GdaStatement statement from @builder's contents.
 *
 * Returns: a new #GdaStatement object, or %NULL if an error occurred
 *
 * Since: 4.2
 */
GdaStatement *
gda_sql_builder_get_statement (GdaSqlBuilder *builder, GError **error)
{
	g_return_val_if_fail (GDA_IS_SQL_BUILDER (builder), NULL);
	if (!builder->priv->main_stmt) {
		g_set_error (error, GDA_SQL_BUILDER_ERROR, GDA_SQL_BUILDER_MISUSE_ERROR,
			     _("SqlBuilder is empty"));
		return NULL;
	}
	if (! gda_sql_statement_check_structure (builder->priv->main_stmt, error))
		return NULL;

	return (GdaStatement*) g_object_new (GDA_TYPE_STATEMENT, "structure", builder->priv->main_stmt, NULL);
}

/**
 * gda_sql_builder_get_sql_statement
 * @builder: a #GdaSqlBuilder object
 * @copy_it: set to %TRUE to be able to reuse @builder
 *
 * Creates a new #GdaSqlStatement structure from @builder's contents.
 *
 * If @copy_it is %FALSE, then the returned pointer is considered to be stolen from @builder's internal
 * representation and will make it unusable anymore (resulting in a %GDA_SQL_BUILDER_MISUSE_ERROR error or
 * some warnings if one tries to reuse it).
 * If, on the other
 * hand it is set to %TRUE, then the returned #GdaSqlStatement is a copy of the on @builder uses
 * internally, making it reusable.
 *
 * Returns: a #GdaSqlStatement pointer
 *
 * Since: 4.2
 */
GdaSqlStatement *
gda_sql_builder_get_sql_statement (GdaSqlBuilder *builder, gboolean copy_it)
{
	g_return_val_if_fail (GDA_IS_SQL_BUILDER (builder), NULL);
	if (!builder->priv->main_stmt) 
		return NULL;
	if (copy_it)
		return gda_sql_statement_copy (builder->priv->main_stmt);
	else {
		GdaSqlStatement *stmt = builder->priv->main_stmt;
		builder->priv->main_stmt = NULL;
		return stmt;
	}
}

/**
 * gda_sql_builder_set_table
 * @builder: a #GdaSqlBuilder object
 * @table_name: a table name
 *
 * Valid only for: INSERT, UPDATE, DELETE statements
 *
 * Sets the name of the table on which the built statement operates.
 *
 * Since: 4.2
 */
void
gda_sql_builder_set_table (GdaSqlBuilder *builder, const gchar *table_name)
{
	g_return_if_fail (GDA_IS_SQL_BUILDER (builder));
	g_return_if_fail (builder->priv->main_stmt);
	g_return_if_fail (table_name && *table_name);

	GdaSqlTable *table = NULL;

	switch (builder->priv->main_stmt->stmt_type) {
	case GDA_SQL_STATEMENT_DELETE: {
		GdaSqlStatementDelete *del = (GdaSqlStatementDelete*) builder->priv->main_stmt->contents;
		if (!del->table)
			del->table = gda_sql_table_new (GDA_SQL_ANY_PART (del));
		table = del->table;
		break;
	}
	case GDA_SQL_STATEMENT_UPDATE: {
		GdaSqlStatementUpdate *upd = (GdaSqlStatementUpdate*) builder->priv->main_stmt->contents;
		if (!upd->table)
			upd->table = gda_sql_table_new (GDA_SQL_ANY_PART (upd));
		table = upd->table;
		break;
	}
	case GDA_SQL_STATEMENT_INSERT: {
		GdaSqlStatementInsert *ins = (GdaSqlStatementInsert*) builder->priv->main_stmt->contents;
		if (!ins->table)
			ins->table = gda_sql_table_new (GDA_SQL_ANY_PART (ins));
		table = ins->table;
		break;
	}
	default:
		g_warning (_("Wrong statement type"));
		break;
	}

	g_assert (table);
	if (table->table_name)
		g_free (table->table_name);
	table->table_name = g_strdup (table_name);
}

/**
 * gda_sql_builder_set_where
 * @builder: a #GdaSqlBuilder object
 * @cond_id: the ID of the expression to set as WHERE condition, or 0 to unset any previous WHERE condition
 *
 * Valid only for: UPDATE, DELETE, SELECT statements
 *
 * Sets the WHERE condition of the statement
 *
 * Since: 4.2
 */
void
gda_sql_builder_set_where (GdaSqlBuilder *builder, guint cond_id)
{
	g_return_if_fail (GDA_IS_SQL_BUILDER (builder));
	g_return_if_fail (builder->priv->main_stmt);

	SqlPart *p = NULL;
	if (cond_id > 0) {
		p = get_part (builder, cond_id, GDA_SQL_ANY_EXPR);
		if (!p)
			return;
	}

	switch (builder->priv->main_stmt->stmt_type) {
	case GDA_SQL_STATEMENT_UPDATE: {
		GdaSqlStatementUpdate *upd = (GdaSqlStatementUpdate*) builder->priv->main_stmt->contents;
		if (upd->cond)
			gda_sql_expr_free (upd->cond);
		upd->cond = (GdaSqlExpr*) use_part (p, GDA_SQL_ANY_PART (upd));
		break;
	}
	case GDA_SQL_STATEMENT_DELETE:{
		GdaSqlStatementDelete *del = (GdaSqlStatementDelete*) builder->priv->main_stmt->contents;
		if (del->cond)
			gda_sql_expr_free (del->cond);
		del->cond = (GdaSqlExpr*) use_part (p, GDA_SQL_ANY_PART (del));
		break;
	}
	case GDA_SQL_STATEMENT_SELECT:{
		GdaSqlStatementSelect *sel = (GdaSqlStatementSelect*) builder->priv->main_stmt->contents;
		if (sel->where_cond)
			gda_sql_expr_free (sel->where_cond);
		sel->where_cond = (GdaSqlExpr*) use_part (p, GDA_SQL_ANY_PART (sel));
		break;
	}
	default:
		g_warning (_("Wrong statement type"));
		break;
	}
}


/**
 * gda_sql_builder_add_field
 * @builder: a #GdaSqlBuilder object
 * @field_id: the ID of the field's name or definition
 * @value_id: the ID of the value to set the field to
 *
 * Valid only for: INSERT, UPDATE, SELECT statements
 *
 * For INSERT and UPDATE: specifies that the field named @field_name will be set to the value identified by @value_id.
 * For SELECT: add a selected item to the statement, and if @value_id is not %0, then use it as an alias
 *
 * Since: 4.2
 */
void
gda_sql_builder_add_field (GdaSqlBuilder *builder, guint field_id, guint value_id)
{
	g_return_if_fail (GDA_IS_SQL_BUILDER (builder));
	g_return_if_fail (builder->priv->main_stmt);

	SqlPart *value_part, *field_part;
	GdaSqlExpr *field_expr;
	value_part = get_part (builder, value_id, GDA_SQL_ANY_EXPR);
	field_part = get_part (builder, field_id, GDA_SQL_ANY_EXPR);
	if (!field_part)
		return;
	field_expr = (GdaSqlExpr*) (field_part->part);

	/* checks */
	switch (builder->priv->main_stmt->stmt_type) {
	case GDA_SQL_STATEMENT_UPDATE:
	case GDA_SQL_STATEMENT_INSERT:
		if (!value_part)
			return;
		if (!field_expr->value || (G_VALUE_TYPE (field_expr->value) !=  G_TYPE_STRING)) {
			g_warning (_("Wrong field format"));
			return;
		}
		break;
	case GDA_SQL_STATEMENT_SELECT:
		/* no specific check */
		break;
	default:
		g_warning (_("Wrong statement type"));
		return;
	}

	/* work */
	switch (builder->priv->main_stmt->stmt_type) {
	case GDA_SQL_STATEMENT_UPDATE: {
		GdaSqlStatementUpdate *upd = (GdaSqlStatementUpdate*) builder->priv->main_stmt->contents;
		GdaSqlField *field = gda_sql_field_new (GDA_SQL_ANY_PART (upd));
		field->field_name = g_value_dup_string (field_expr->value);
		upd->fields_list = g_slist_append (upd->fields_list, field);
		upd->expr_list = g_slist_append (upd->expr_list, use_part (value_part, GDA_SQL_ANY_PART (upd)));
		break;
	}
	case GDA_SQL_STATEMENT_INSERT:{
		GdaSqlStatementInsert *ins = (GdaSqlStatementInsert*) builder->priv->main_stmt->contents;
		GdaSqlField *field = gda_sql_field_new (GDA_SQL_ANY_PART (ins));
		field->field_name = g_value_dup_string (field_expr->value);

		ins->fields_list = g_slist_append (ins->fields_list, field);
		
		if (! ins->values_list)
			ins->values_list = g_slist_append (NULL,
							   g_slist_append (NULL,
							   use_part (value_part, GDA_SQL_ANY_PART (ins))));
		else
			ins->values_list->data = g_slist_append ((GSList*) ins->values_list->data, 
								 use_part (value_part, GDA_SQL_ANY_PART (ins)));
		break;
	}
	case GDA_SQL_STATEMENT_SELECT: {
		GdaSqlStatementSelect *sel = (GdaSqlStatementSelect*) builder->priv->main_stmt->contents;
		GdaSqlSelectField *field;
		field = gda_sql_select_field_new (GDA_SQL_ANY_PART (sel));
		field->expr = (GdaSqlExpr*) use_part (field_part, GDA_SQL_ANY_PART (field));
		if (value_part) {
			GdaSqlExpr *value_expr = (GdaSqlExpr*) (value_part->part);
			if (G_VALUE_TYPE (value_expr->value) ==  G_TYPE_STRING)
				field->as = g_value_dup_string (value_expr->value);
		}
		sel->expr_list = g_slist_append (sel->expr_list, field);
		break;
	}
	default:
		g_warning (_("Wrong statement type"));
		break;
	}
}

/**
 * gda_sql_builder_expr
 * @builder: a #GdaSqlBuilder object
 * @id: the requested ID, or 0 if to be determined by @builder
 * @dh: a #GdaDataHandler to use, or %NULL
 * @type: the GType of the following argument
 * @...: value to set the expression to, of the type specified by @type
 *
 * Defines an expression in @builder which may be reused to build other parts of a statement.
 *
 * The new expression will contain the value passed as the @... argument. It is possible to
 * customize how the value has to be interpreted by passing a specific #GdaDataHandler object as @dh.
 *
 * For example:
 * <programlisting>
 * gda_sql_builder_expr (b, 0, G_TYPE_INT, 15);
 * gda_sql_builder_expr (b, 5, G_TYPE_STRING, "joe")
 * </programlisting>
 *
 * will be rendered as SQL as:
 * <programlisting>
 * 15
 * 'joe'
 * </programlisting>
 *
 * Returns: the ID of the new expression, or 0 if there was an error
 *
 * Since: 4.2
 */
guint
gda_sql_builder_expr (GdaSqlBuilder *builder, guint id, GdaDataHandler *dh, GType type, ...)
{
	g_return_val_if_fail (GDA_IS_SQL_BUILDER (builder), 0);
	g_return_val_if_fail (builder->priv->main_stmt, 0);
	
	va_list ap;
	gchar *str;
	GValue *v = NULL;

	if (!dh)
		dh = gda_get_default_handler (type);
	else {
		if (! gda_data_handler_accepts_g_type (dh, type)) {
			g_warning (_("Unhandled data type '%s'"), g_type_name (type));
			return 0;
		}
	}
	if (!dh) {
		g_warning (_("Unhandled data type '%s'"), g_type_name (type));
		return 0;
	}

	va_start (ap, type);
	if (type == G_TYPE_STRING) {
		g_value_set_string ((v = gda_value_new (G_TYPE_STRING)), va_arg (ap, gchar*));
		str = gda_data_handler_get_sql_from_value (dh, v);
	}
	else if (type == G_TYPE_INT) {
		g_value_set_int ((v = gda_value_new (G_TYPE_INT)), va_arg (ap, gint));
		str = gda_data_handler_get_sql_from_value (dh, v);
	}
	va_end (ap);

	if (v)
		gda_value_free (v);

	if (str) {
		GdaSqlExpr *expr;
		expr = gda_sql_expr_new (NULL);
		expr->value = gda_value_new (G_TYPE_STRING);
		g_value_take_string (expr->value, str);
		return add_part (builder, id, (GdaSqlAnyPart *) expr);
	}
	else {
		g_warning (_("Could not convert value to type '%s'"), g_type_name (type));
		return 0;
	}
}

/**
 * gda_sql_builder_ident
 * @builder: a #GdaSqlBuilder object
 * @id: the requested ID, or 0 if to be determined by @builder
 * @string: a string
 *
 * Defines an expression in @builder which may be reused to build other parts of a statement.
 *
 * The new expression will contain the @string literal.
 * For example:
 * <programlisting>
 * gda_sql_builder_expr_liretal (b, 0, "name")
 * </programlisting>
 *
 * will be rendered as SQL as:
 * <programlisting>
 * name
 * </programlisting>
 *
 * Returns: the ID of the new expression, or 0 if there was an error
 *
 * Since: 4.2
 */
guint
gda_sql_builder_ident (GdaSqlBuilder *builder, guint id, const gchar *string)
{
	g_return_val_if_fail (GDA_IS_SQL_BUILDER (builder), 0);
	g_return_val_if_fail (builder->priv->main_stmt, 0);
	
	GdaSqlExpr *expr;
	expr = gda_sql_expr_new (NULL);
	if (string) {
		expr->value = gda_value_new (G_TYPE_STRING);
		g_value_set_string (expr->value, string);
		expr->value_is_ident = (gpointer) 0x1;
	}
	
	return add_part (builder, id, (GdaSqlAnyPart *) expr);
}

/**
 * gda_sql_builder_param
 * @builder: a #GdaSqlBuilder object
 * @id: the requested ID, or 0 if to be determined by @builder
 * @param_name: parameter's name
 * @type: parameter's type
 * @nullok: TRUE if the parameter can be set to %NULL
 *
 * Defines a parameter in @builder which may be reused to build other parts of a statement.
 *
 * The new expression will contain the @string literal.
 * For example:
 * <programlisting>
 * gda_sql_builder_param (b, 0, "age", G_TYPE_INT, FALSE)
 * </programlisting>
 *
 * will be rendered as SQL as:
 * <programlisting><![CDATA[
 * ##age::int
 * ]]>
 * </programlisting>
 *
 * Returns: the ID of the new expression, or 0 if there was an error
 *
 * Since: 4.2
 */
guint
gda_sql_builder_param (GdaSqlBuilder *builder, guint id, const gchar *param_name, GType type, gboolean nullok)
{
	g_return_val_if_fail (GDA_IS_SQL_BUILDER (builder), 0);
	g_return_val_if_fail (builder->priv->main_stmt, 0);
	g_return_val_if_fail (param_name && *param_name, 0);
	
	GdaSqlExpr *expr;
	expr = gda_sql_expr_new (NULL);
	expr->param_spec = g_new0 (GdaSqlParamSpec, 1);
	expr->param_spec->name = g_strdup (param_name);
	expr->param_spec->is_param = TRUE;
	expr->param_spec->nullok = nullok;
	expr->param_spec->g_type = type;

	return add_part (builder, id, (GdaSqlAnyPart *) expr);
}

/**
 * gda_sql_builder_cond
 * @builder: a #GdaSqlBuilder object
 * @id: the requested ID, or 0 if to be determined by @builder
 * @op: type of condition
 * @op1: the ID of the 1st argument (not 0)
 * @op2: the ID of the 2nd argument (may be %0 if @op needs only one operand)
 * @op3: the ID of the 3rd argument (may be %0 if @op needs only one or two operand)
 *
 * Builds a new expression which reprenents a condition (or operation).
 *
 * Returns: the ID of the new expression, or 0 if there was an error
 *
 * Since: 4.2
 */
guint
gda_sql_builder_cond (GdaSqlBuilder *builder, guint id, GdaSqlOperatorType op, guint op1, guint op2, guint op3)
{
	g_return_val_if_fail (GDA_IS_SQL_BUILDER (builder), 0);
	g_return_val_if_fail (builder->priv->main_stmt, 0);

	SqlPart *p1, *p2;
	p1 = get_part (builder, op1, GDA_SQL_ANY_EXPR);
	if (!p1)
		return 0;
	p2 = get_part (builder, op2, GDA_SQL_ANY_EXPR);
	
	GdaSqlExpr *expr;
	expr = gda_sql_expr_new (NULL);
	expr->cond = gda_sql_operation_new (GDA_SQL_ANY_PART (expr));
	expr->cond->operator_type = op;
	expr->cond->operands = g_slist_append (NULL, use_part (p1, GDA_SQL_ANY_PART (expr->cond)));
	if (p2) {
		SqlPart *p3;
		expr->cond->operands = g_slist_append (expr->cond->operands,
						       use_part (p2, GDA_SQL_ANY_PART (expr->cond)));
		p3 = get_part (builder, op3, GDA_SQL_ANY_EXPR);
		if (p3)
			expr->cond->operands = g_slist_append (expr->cond->operands,
							       use_part (p3, GDA_SQL_ANY_PART (expr->cond)));	
	}

	return add_part (builder, id, (GdaSqlAnyPart *) expr);
}

/**
 * gda_sql_builder_cond_v
 * @builder: a #GdaSqlBuilder object
 * @id: the requested ID, or 0 if to be determined by @builder
 * @op: type of condition
 * @op_ids: an array of ID for the arguments (not %0)
 * @ops_ids_size: size of @ops_ids
 *
 * Builds a new expression which reprenents a condition (or operation).
 *
 * As a side case, if @ops_ids_size is 1,
 * then @op is ignored, and the returned ID represents @op_ids[0] (this avoids any problem for example
 * when @op is GDA_SQL_OPERATOR_TYPE_AND and there is in fact only one operand).
 *
 * Returns: the ID of the new expression, or 0 if there was an error
 *
 * Since: 4.2
 */
guint
gda_sql_builder_cond_v (GdaSqlBuilder *builder, guint id, GdaSqlOperatorType op,
			guint *op_ids, gint op_ids_size)
{
	gint i;
	SqlPart **parts;

	g_return_val_if_fail (GDA_IS_SQL_BUILDER (builder), 0);
	g_return_val_if_fail (builder->priv->main_stmt, 0);
	g_return_val_if_fail (op_ids, 0);
	g_return_val_if_fail (op_ids_size > 0, 0);

	parts = g_new (SqlPart *, op_ids_size);
	for (i = 0; i < op_ids_size; i++) {
		parts [i] = get_part (builder, op_ids [i], GDA_SQL_ANY_EXPR);
		if (!parts [i]) {
			g_free (parts);
			return 0;
		}
	}

	if (op_ids_size == 1) {
		SqlPart *part = parts [0];
		g_free (parts);
		if (id)
			return add_part (builder, id, use_part (part, NULL));
		else
			return op_ids [0]; /* return the same ID as none was specified */
	}
	
	GdaSqlExpr *expr;
	expr = gda_sql_expr_new (NULL);
	expr->cond = gda_sql_operation_new (GDA_SQL_ANY_PART (expr));
	expr->cond->operator_type = op;
	expr->cond->operands = NULL;
	for (i = 0; i < op_ids_size; i++)
		expr->cond->operands = g_slist_append (expr->cond->operands,
						       use_part (parts [i],
								 GDA_SQL_ANY_PART (expr->cond)));
	g_free (parts);

	return add_part (builder, id, (GdaSqlAnyPart *) expr);
}


typedef struct {
	GdaSqlSelectTarget target; /* inheritance! */
	guint part_id; /* copied from this part ID */
} BuildTarget;

/**
 * gda_sql_builder_select_add_target
 * @builder: a #GdaSqlBuilder object
 * @id: the requested ID, or 0 if to be determined by @builder
 * @table_id: the ID of the expression holding a table reference (not %0)
 * @alias: the alias to give to the target, or %NULL
 *
 * Adds a new target to a SELECT statement
 *
 * Returns: the ID of the new target, or 0 if there was an error
 *
 * Since: 4.2
 */
guint
gda_sql_builder_select_add_target (GdaSqlBuilder *builder, guint id, guint table_id, const gchar *alias)
{
	g_return_val_if_fail (GDA_IS_SQL_BUILDER (builder), 0);
	g_return_val_if_fail (builder->priv->main_stmt, 0);
	
	if (builder->priv->main_stmt->stmt_type != GDA_SQL_STATEMENT_SELECT) {
		g_warning (_("Wrong statement type"));
		return 0;
	}

	SqlPart *p;
	p = get_part (builder, table_id, GDA_SQL_ANY_EXPR);
	if (!p)
		return 0;
	
	BuildTarget *btarget;
	GdaSqlStatementSelect *sel = (GdaSqlStatementSelect*) builder->priv->main_stmt->contents;
	btarget = g_new0 (BuildTarget, 1);
        GDA_SQL_ANY_PART(btarget)->type = GDA_SQL_ANY_SQL_SELECT_TARGET;
        GDA_SQL_ANY_PART(btarget)->parent = GDA_SQL_ANY_PART (sel->from);
	if (id)
		btarget->part_id = id;
	else
		btarget->part_id = builder->priv->next_assigned_id --;
	
	((GdaSqlSelectTarget*) btarget)->expr = (GdaSqlExpr*) use_part (p, GDA_SQL_ANY_PART (btarget));
	if (alias) 
		((GdaSqlSelectTarget*) btarget)->as = g_strdup (alias);

	/* add target to sel->from. NOTE: @btarget is NOT added to the "repository" or GdaSqlAnyPart parts
	* like others */
	if (!sel->from)
		sel->from = gda_sql_select_from_new (GDA_SQL_ANY_PART (sel));
	sel->from->targets = g_slist_append (sel->from->targets, btarget);

	return btarget->part_id;
}

typedef struct {
	GdaSqlSelectJoin join; /* inheritance! */
	guint part_id; /* copied from this part ID */
} BuilderJoin;

/**
 * gda_sql_builder_select_join_targets
 * @builder: a #GdaSqlBuilder object
 * @id: the requested ID, or 0 if to be determined by @builder
 * @left_target_id: the ID of the left target to use (not %0)
 * @right_target_id: the ID of the right target to use (not %0)
 * @join_type: the type of join
 * @join_expr: joining expression's ID, or %0
 *
 * Joins two targets in a SELECT statement
 *
 * Returns: the ID of the new join, or 0 if there was an error
 *
 * Since: 4.2
 */
guint
gda_sql_builder_select_join_targets (GdaSqlBuilder *builder, guint id,
				     guint left_target_id, guint right_target_id,
				     GdaSqlSelectJoinType join_type,
				     guint join_expr)
{
	g_return_val_if_fail (GDA_IS_SQL_BUILDER (builder), 0);
	g_return_val_if_fail (builder->priv->main_stmt, 0);
	
	if (builder->priv->main_stmt->stmt_type != GDA_SQL_STATEMENT_SELECT) {
		g_warning (_("Wrong statement type"));
		return 0;
	}

	/* determine join position */
	GdaSqlStatementSelect *sel = (GdaSqlStatementSelect*) builder->priv->main_stmt->contents;
	GSList *list;
	gint left_pos = -1, right_pos = -1;
	gint pos;
	for (pos = 0, list = sel->from ? sel->from->targets : NULL;
	     list;
	     pos++, list = list->next) {
		BuildTarget *btarget = (BuildTarget*) list->data;
		if (btarget->part_id == left_target_id)
			left_pos = pos;
		else if (btarget->part_id == right_target_id)
			right_pos = pos;
		if ((left_pos != -1) && (right_pos != -1))
			break;
	}
	if ((left_pos == -1) || (right_pos == -1)) {
		g_warning (_("Unknown part ID %u"), (left_pos == -1) ? left_pos : right_pos);
		return 0;
	}
	
	if (left_pos > right_pos) {
		TO_IMPLEMENT;
	}
	
	/* create join */
	BuilderJoin *bjoin;
	GdaSqlSelectJoin *join;

	bjoin = g_new0 (BuilderJoin, 1);
	GDA_SQL_ANY_PART(bjoin)->type = GDA_SQL_ANY_SQL_SELECT_JOIN;
        GDA_SQL_ANY_PART(bjoin)->parent = GDA_SQL_ANY_PART (sel->from);
	if (id)
		bjoin->part_id = id;
	else
		bjoin->part_id = builder->priv->next_assigned_id --;
	join = (GdaSqlSelectJoin*) bjoin;
	join->type = join_type;
	join->position = right_pos;
	
	SqlPart *ep;
	ep = get_part (builder, join_expr, GDA_SQL_ANY_EXPR);
	if (ep)
		join->expr = (GdaSqlExpr*) use_part (ep, GDA_SQL_ANY_PART (join));

	sel->from->joins = g_slist_append (sel->from->joins, bjoin);

	return bjoin->part_id;
}

/**
 * gda_sql_builder_join_add_field
 * @builder: a #GdaSqlBuilder object
 * @join_id: the ID of the join to modify (not %0)
 * @field_name: the name of the field to use in the join condition (not %NULL)
 *
 * Alter a joins in a SELECT statement to make its condition on the field which name
 * is @field_name
 *
 * Returns: the ID of the new join, or 0 if there was an error
 *
 * Since: 4.2
 */
void
gda_sql_builder_join_add_field (GdaSqlBuilder *builder, guint join_id, const gchar *field_name)
{
	g_return_if_fail (GDA_IS_SQL_BUILDER (builder));
	g_return_if_fail (builder->priv->main_stmt);
	g_return_if_fail (field_name);
	
	if (builder->priv->main_stmt->stmt_type != GDA_SQL_STATEMENT_SELECT) {
		g_warning (_("Wrong statement type"));
		return;
	}

	/* determine join */
	GdaSqlStatementSelect *sel = (GdaSqlStatementSelect*) builder->priv->main_stmt->contents;
	GdaSqlSelectJoin *join = NULL;
	GSList *list;
	for (list = sel->from ? sel->from->joins : NULL;
	     list;
	     list = list->next) {
		BuilderJoin *bjoin = (BuilderJoin*) list->data;
		if (bjoin->part_id == join_id) {
			join = (GdaSqlSelectJoin*) bjoin;
			break;
		}
	}
	if (!join) {
		g_warning (_("Unknown part ID %u"), join_id);
		return;
	}

	GdaSqlField *field;
	field = gda_sql_field_new (GDA_SQL_ANY_PART (join));
	field->field_name = g_strdup (field_name);
	join->use = g_slist_append (join->use, field);
}

/**
 * gda_sql_builder_select_order_by
 * @builder: a #GdaSqlBuiler
 * @expr_id: the ID of the expression to use during sorting (not %0)
 * @asc: %TRUE for an ascending sorting
 * @collation_name: name of the collation to use when sorting, or %NULL
 *
 * Adds a new ORDER BY expression to a SELECT statement.
 * 
 * Since: 4.2
 */
void
gda_sql_builder_select_order_by (GdaSqlBuilder *builder, guint expr_id,
				 gboolean asc, const gchar *collation_name)
{
	SqlPart *part;
	GdaSqlStatementSelect *sel;
	GdaSqlSelectOrder *sorder;

	g_return_if_fail (GDA_IS_SQL_BUILDER (builder));
	g_return_if_fail (expr_id > 0);

	if (builder->priv->main_stmt->stmt_type != GDA_SQL_STATEMENT_SELECT) {
		g_warning (_("Wrong statement type"));
		return;
	}

	part = get_part (builder, expr_id, GDA_SQL_ANY_EXPR);
	if (!part)
		return;
	sel = (GdaSqlStatementSelect*) builder->priv->main_stmt->contents;
	
	sorder = gda_sql_select_order_new (GDA_SQL_ANY_PART (sel));
	sorder->expr = (GdaSqlExpr*) use_part (part, GDA_SQL_ANY_PART (sorder));
	sorder->asc = asc;
	if (collation_name)
		sorder->collation_name = g_strdup (collation_name);
	sel->order_by = g_slist_append (sel->order_by, sorder);
}