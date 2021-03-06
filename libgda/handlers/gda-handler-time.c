/*
 * Copyright (C) 2006 - 2013 Vivien Malerba <malerba@gnome-db.org>
 * Copyright (C) 2007 Murray Cumming <murrayc@murrayc.com>
 * Copyright (C) 2009 Bas Driessen <bas.driessen@xobas.com>
 * Copyright (C) 2010 David King <davidk@openismus.com>
 * Copyright (C) 2010 Jonh Wendell <jwendell@gnome.org>
 * Copyright (C) 2011 Daniel Espinosa <despinosa@src.gnome.org>
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

#include "gda-handler-time.h"
#include <gda-util.h>
#include <string.h>
#include <ctype.h>
#include <glib/gi18n-lib.h>
#include <libgda/gda-debug-macros.h>

static void gda_handler_time_class_init (GdaHandlerTimeClass *class);
static void gda_handler_time_init (GdaHandlerTime *hdl);
static void gda_handler_time_dispose (GObject *object);
static void handler_compute_locale (GdaHandlerTime *hdl);

/* General notes:
 * about months representations:
 * -----------------------------
 * GtkCalendar gets months in [0-11]
 * GDate represents months in [1-12]
 * struct tm represents months in [0-11]
 *
 * about date localization:
 * ------------------------
 * see how this aspect is handled in glib: function g_date_prepare_to_parse()
 * in file gdate.c
 */

/* GdaDataHandler interface */
static void         gda_handler_time_data_handler_init      (GdaDataHandlerIface *iface);
static gchar       *gda_handler_time_get_sql_from_value     (GdaDataHandler *dh, const GValue *value);
static gchar       *gda_handler_time_get_str_from_value     (GdaDataHandler *dh, const GValue *value);
static GValue      *gda_handler_time_get_value_from_sql     (GdaDataHandler *dh, const gchar *sql,
							     GType type);
static GValue      *gda_handler_time_get_value_from_str     (GdaDataHandler *dh, const gchar *sql,
							     GType type);

static GValue      *gda_handler_time_get_sane_init_value    (GdaDataHandler * dh, GType type);

static gboolean     gda_handler_time_accepts_g_type       (GdaDataHandler * dh, GType type);

static const gchar *gda_handler_time_get_descr              (GdaDataHandler *dh);

typedef struct _LocaleSetting {
	GDateDMY        dmy_order[3];
	gboolean        twodigit_years;
	gint            current_offset; /* 1900, 2000, etc... */
	gchar           separator;	
} LocaleSetting;

static gchar *render_date_locale (const GDate *date, LocaleSetting *locale);

struct  _GdaHandlerTimePriv {
	guint           nb_g_types;
	GType          *valid_g_types;

	/* for locale setting */
	LocaleSetting  *sql_locale;
	LocaleSetting  *str_locale;
};

/* get a pointer to the parents to be able to call their destructor */
static GObjectClass *parent_class = NULL;

GType
gda_handler_time_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		static GMutex registering;
		static const GTypeInfo info = {
			sizeof (GdaHandlerTimeClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) gda_handler_time_class_init,
			NULL,
			NULL,
			sizeof (GdaHandlerTime),
			0,
			(GInstanceInitFunc) gda_handler_time_init,
			NULL
		};		

		static const GInterfaceInfo data_entry_info = {
			(GInterfaceInitFunc) gda_handler_time_data_handler_init,
			NULL,
			NULL
		};

		g_mutex_lock (&registering);
		if (type == 0) {
			type = g_type_register_static (G_TYPE_OBJECT, "GdaHandlerTime", &info, 0);
			g_type_add_interface_static (type, GDA_TYPE_DATA_HANDLER, &data_entry_info);
		}
		g_mutex_unlock (&registering);
	}
	return type;
}

static void
gda_handler_time_data_handler_init (GdaDataHandlerIface *iface)
{
	iface->get_sql_from_value = gda_handler_time_get_sql_from_value;
	iface->get_str_from_value = gda_handler_time_get_str_from_value;
	iface->get_value_from_sql = gda_handler_time_get_value_from_sql;
	iface->get_value_from_str = gda_handler_time_get_value_from_str;
	iface->get_sane_init_value = gda_handler_time_get_sane_init_value;
	iface->accepts_g_type = gda_handler_time_accepts_g_type;
	iface->get_descr = gda_handler_time_get_descr;
}


static void
gda_handler_time_class_init (GdaHandlerTimeClass * class)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (class);
	
	parent_class = g_type_class_peek_parent (class);

	object_class->dispose = gda_handler_time_dispose;
}

static void
gda_handler_time_init (GdaHandlerTime *hdl)
{
	/* Private structure */
	hdl->priv = g_new0 (GdaHandlerTimePriv, 1);
	hdl->priv->nb_g_types = 4;
	hdl->priv->valid_g_types = g_new0 (GType, 7);
	hdl->priv->valid_g_types[0] = G_TYPE_DATE;
	hdl->priv->valid_g_types[1] = GDA_TYPE_TIME;
	hdl->priv->valid_g_types[2] = GDA_TYPE_TIMESTAMP;
	hdl->priv->valid_g_types[3] = G_TYPE_DATE_TIME;

	/* taking into accout the locale */
	hdl->priv->sql_locale = g_new0 (LocaleSetting, 1);
	hdl->priv->sql_locale->dmy_order[0] = G_DATE_MONTH;
	hdl->priv->sql_locale->dmy_order[1] = G_DATE_DAY;
	hdl->priv->sql_locale->dmy_order[2] = G_DATE_YEAR;
	hdl->priv->sql_locale->twodigit_years = FALSE;
	hdl->priv->sql_locale->current_offset = 0;
	hdl->priv->sql_locale->separator = '-';

	hdl->priv->str_locale = g_new0 (LocaleSetting, 1);
	hdl->priv->str_locale->dmy_order[0] = G_DATE_MONTH;
	hdl->priv->str_locale->dmy_order[1] = G_DATE_DAY;
	hdl->priv->str_locale->dmy_order[2] = G_DATE_YEAR;
	hdl->priv->str_locale->twodigit_years = FALSE;
	hdl->priv->str_locale->current_offset = 0;
	hdl->priv->str_locale->separator = '-';

	g_object_set_data (G_OBJECT (hdl), "name", "InternalTime");
	g_object_set_data (G_OBJECT (hdl), "descr", _("Time, Date and TimeStamp representation"));
}

static void
gda_handler_time_dispose (GObject *object)
{
	GdaHandlerTime *hdl;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GDA_IS_HANDLER_TIME (object));

	hdl = GDA_HANDLER_TIME (object);

	if (hdl->priv) {
		g_free (hdl->priv->valid_g_types);
		hdl->priv->valid_g_types = NULL;

		g_free (hdl->priv->str_locale);
		g_free (hdl->priv->sql_locale);

		g_free (hdl->priv);
		hdl->priv = NULL;
	}

	/* for the parent class */
	parent_class->dispose (object);
}

/**
 * gda_handler_time_new:
 *
 * Creates a data handler for time values
 *
 * Returns: (transfer full): the new object
 */
GdaDataHandler *
gda_handler_time_new (void)
{
	GObject *obj;

	obj = g_object_new (GDA_TYPE_HANDLER_TIME, NULL);
	handler_compute_locale (GDA_HANDLER_TIME (obj));

	return (GdaDataHandler *) obj;
}

/**
 * gda_handler_time_new_no_locale:
 *
 * Creates a data handler for time values, but using the default C locale
 * instead of the current user locale.
 *
 * Returns: (transfer full): the new object
 */
GdaDataHandler *
gda_handler_time_new_no_locale (void)
{
	GObject *obj;

	obj = g_object_new (GDA_TYPE_HANDLER_TIME, NULL);

	return (GdaDataHandler *) obj;
}

/**
 * gda_handler_time_set_sql_spec:
 * @dh: a #GdaHandlerTime object
 * @first: what comes first in the date representation
 * @sec: what comes second in the date representation
 * @third: what comes third in the date representation
 * @separator: separator character used between year, month and day
 * @twodigits_years: TRUE if year part of date must be rendered on 2 digits
 *
 * Specifies the SQL output style of the @dh data handler. The general format is "FIRSTsSECsTHIRD"
 * where FIRST, SEC and THIRD are specified by @first, @sec and @trird and 's' is the separator,
 * specified by @separator.
 *
 * The default implementation is @first=G_DATE_MONTH, @sec=G_DATE_DAY and @third=G_DATE_YEAR
 * (the year is rendered on 4 digits) and the separator is '-'
 */
void
gda_handler_time_set_sql_spec  (GdaHandlerTime *dh, GDateDMY first, GDateDMY sec,
				GDateDMY third, gchar separator, gboolean twodigits_years)
{
	g_return_if_fail (GDA_IS_HANDLER_TIME (dh));
	g_return_if_fail (dh->priv);
	g_return_if_fail (first != sec);
	g_return_if_fail (sec != third);
	g_return_if_fail (first != third);

	dh->priv->sql_locale->dmy_order[0] = first;
	dh->priv->sql_locale->dmy_order[1] = sec;
	dh->priv->sql_locale->dmy_order[2] = third;
	dh->priv->sql_locale->twodigit_years = twodigits_years;
	dh->priv->sql_locale->separator = separator;
}

/**
 * gda_handler_time_set_str_spec:
 * @dh: a #GdaHandlerTime object
 * @first: what comes first in the date representation
 * @sec: what comes second in the date representation
 * @third: what comes third in the date representation
 * @separator: separator character used between year, month and day
 * @twodigits_years: TRUE if year part of date must be rendered on 2 digits
 *
 * Specifies the human readable output style of the @dh data handler.
 * The general format is "FIRSTsSECsTHIRD"
 * where FIRST, SEC and THIRD are specified by @first, @sec and @trird and 's' is the separator,
 * specified by @separator.
 *
 * The default implementation depends on the current locale, except if @dh was created
 * using gda_handler_time_new_no_locale().
 *
 * Since: 4.2.1
 */
void
gda_handler_time_set_str_spec  (GdaHandlerTime *dh, GDateDMY first, GDateDMY sec,
				GDateDMY third, gchar separator, gboolean twodigits_years)
{
	g_return_if_fail (GDA_IS_HANDLER_TIME (dh));
	g_return_if_fail (dh->priv);
	g_return_if_fail (first != sec);
	g_return_if_fail (sec != third);
	g_return_if_fail (first != third);

	dh->priv->str_locale->dmy_order[0] = first;
	dh->priv->str_locale->dmy_order[1] = sec;
	dh->priv->str_locale->dmy_order[2] = third;
	dh->priv->str_locale->twodigit_years = twodigits_years;
	dh->priv->str_locale->separator = separator;
}

static void
handler_compute_locale (GdaHandlerTime *hdl)
{
	GDate *date;
	gchar buf[128], *ptr, *numstart;
	gint nums[3];
	gboolean error = FALSE;

	date = g_date_new_dmy (4, 7, 1976); /* Same date used by GLib */
	g_date_strftime (buf, 127, "%x", date);
	g_date_free (date);

	/* 1st number */
	ptr = buf;
	numstart = ptr;
	while (*ptr && g_ascii_isdigit (*ptr))
		ptr++;
	if (*ptr) {
		hdl->priv->str_locale->separator = *ptr;
		*ptr = 0;
		nums[0] = atoi (numstart); /* Flawfinder: ignore */
	}
	else
		error = TRUE;

	/* 2nd number */
	if (!error) {
		ptr++;
		numstart = ptr;
		while (*ptr && g_ascii_isdigit (*ptr))
			ptr++;
		if (*ptr) {
			*ptr = 0;
			nums[1] = atoi (numstart); /* Flawfinder: ignore */
		}
		else
			error = TRUE;
	}

	/* 3rd number */
	if (!error) {
		ptr++;
		numstart = ptr;
		while (*ptr && g_ascii_isdigit (*ptr))
			ptr++;
		*ptr = 0;
		nums[2] = atoi (numstart); /* Flawfinder: ignore */
	}
	
	/* computations */
	if (!error) {
#ifdef GDA_DEBUG_NO
		gchar *strings[3];
#endif
		gint i;
		time_t now;
		struct tm *now_tm;

		for (i=0; i < 3; i++) {
			switch (nums[i]) {
			case 7:
				hdl->priv->str_locale->dmy_order[i] = G_DATE_MONTH;
				break;
			case 4:
				hdl->priv->str_locale->dmy_order[i] = G_DATE_DAY;
				break;
			case 76:
				hdl->priv->str_locale->twodigit_years = TRUE;
			case 1976:
				hdl->priv->str_locale->dmy_order[i] = G_DATE_YEAR;
				break;
			default:
				break;
			}
		}
		
		now = time (NULL);
#ifdef HAVE_LOCALTIME_R
		struct tm tmpstm;
		now_tm = localtime_r (&now, &tmpstm);
#elif HAVE_LOCALTIME_S
		struct tm tmpstm;
		g_assert (localtime_s (&tmpstm, &now) == 0);
		now_tm = &tmpstm;
#else
                now_tm = localtime (&now);
#endif
		hdl->priv->str_locale->current_offset = ((now_tm->tm_year + 1900) / 100) * 100;

#ifdef GDA_DEBUG_NO		
		for (i=0; i<3; i++) {
			switch (hdl->priv->str_locale->dmy_order[i]) {
			case G_DATE_MONTH:
				strings[i] = "Month";
				break;
			case G_DATE_YEAR:
				strings[i] = "Year";
				break;
			case G_DATE_DAY:
				strings[i] = "Day";
				break;
			default:
				strings[i] = NULL;
				break;
			}
		}
		g_print ("GdaHandlerTime %p\n", hdl);
		g_print ("\tlocale order = %s %s %s, separator = %c\n", 
			 strings[0], strings[1], strings[2], hdl->priv->str_locale->separator);
		if (hdl->priv->str_locale->twodigit_years)
			g_print ("\tlocale has 2 digits year, using %d as offset\n", hdl->priv->str_locale->current_offset);
		else
			g_print ("\tlocale has 4 digits year\n");
#endif
	}
	else {
		TO_IMPLEMENT;
	}
}

/**
 * gda_handler_time_get_no_locale_str_from_value:
 * @dh: a #GdaHandlerTime object
 * @value: a #GValue value
 *
 * Returns: a new string representing @value without taking the current
 * locale into account (i.e. in the "C" locale)
 */
gchar *
gda_handler_time_get_no_locale_str_from_value (GdaHandlerTime *dh, const GValue *value)
{
	gchar *retval = NULL, *str;
	GType type;

	g_return_val_if_fail (GDA_IS_HANDLER_TIME (dh), NULL);
	type = G_VALUE_TYPE (value);

	if (type == G_TYPE_DATE) {
		const GDate *date;

		date = (GDate *) g_value_get_boxed (value);
		str = render_date_locale (date, dh->priv->sql_locale);
		if (!str)
			retval = g_strdup ("NULL");
		else 
			retval = str;
	}
	else if (type == GDA_TYPE_TIME) {
		const GdaTime *tim;
		GString *string;
		string = g_string_new ("");
		g_string_append_c (string, '\'');
		tim = gda_value_get_time ((GValue *) value);
		g_string_append_printf (string, "%02d:%02d:%02d",
					tim->hour,
					tim->minute,
					tim->second);
		if (tim->timezone != GDA_TIMEZONE_INVALID)
			g_string_append_printf (string, "%+02d",
						(int) tim->timezone / 3600);
		g_string_append_c (string, '\'');
		retval = g_string_free (string, FALSE);
	}
	else if (type == GDA_TYPE_TIMESTAMP) {
		const GdaTimestamp *gdats;
		GDate *vdate;

		gdats = gda_value_get_timestamp ((GValue *) value);
		vdate = g_date_new_dmy (gdats->day, gdats->month, gdats->year);
		str = render_date_locale (vdate, dh->priv->sql_locale);
		g_date_free (vdate);

		if (str) {
			GString *string;
			string = g_string_new ("");
			g_string_append_printf (string, "%02u:%02u:%02u",
						gdats->hour,
						gdats->minute,
						gdats->second);
			if (gdats->fraction != 0)
				g_string_append_printf (string, ".%lu", gdats->fraction);
			
			if (gdats->timezone != GDA_TIMEZONE_INVALID)
				g_string_append_printf (string, "%+02d",
							(int) gdats->timezone / 3600);
			
			retval = g_strdup_printf ("%s %s", str, string->str);
			g_free (str);
			g_string_free (string, TRUE);
		}
		else
			retval = g_strdup ("NULL");	
	}
	else if (type == G_TYPE_DATE_TIME) {
		GDateTime *ts;
		GDate *vdate;

		ts = g_value_get_boxed ((GValue *) value);
		if (ts) {
			gint y, m, d;
			g_date_time_get_ymd (ts, &y, &m, &d);
			vdate = g_date_new_dmy (d, m, y);
			str = render_date_locale (vdate, dh->priv->sql_locale);
			g_date_free (vdate);

			if (str) {
				GString *string;
				string = g_string_new ("");
				g_string_append_printf (string, "%02u:%02u:%02u",
							g_date_time_get_hour (ts),
							g_date_time_get_minute (ts),
							g_date_time_get_second (ts));
				if (g_date_time_get_microsecond (ts) != 0)
					g_string_append_printf (string, ".%d", g_date_time_get_microsecond (ts));

				GTimeSpan span;
				span = g_date_time_get_utc_offset (ts);
				if (span > 0)
					g_string_append_printf (string, "+%02d",
								(int) (span / G_TIME_SPAN_HOUR));
				else
					g_string_append_printf (string, "-%02d",
								(int) (-span / G_TIME_SPAN_HOUR));

				retval = g_strdup_printf ("%s %s", str, string->str);
				g_free (str);
				g_string_free (string, TRUE);
			}
			else
				retval = g_strdup ("NULL");
		}
		else
			retval = g_strdup ("NULL");
	}
	else
		g_assert_not_reached ();

	return retval;
}

/**
 * gda_handler_time_get_format:
 * @dh: a #GdaHandlerTime object
 * @type: the type of data being handled
 *
 * Get a string representing the locale-dependent way to enter a date/time/datetime, using
 * a syntax suitable for the #GdauiFormatEntry widget
 *
 * Returns: a new string
 */
gchar *
gda_handler_time_get_format (GdaHandlerTime *dh, GType type)
{
	gchar *str;
	GString *string;
	gint i;

	g_return_val_if_fail (GDA_IS_HANDLER_TIME (dh), NULL);

	string = g_string_new ("");
	if ((type == G_TYPE_DATE) || (type == GDA_TYPE_TIMESTAMP) || (type == G_TYPE_DATE_TIME)) {
		for (i=0; i<3; i++) {
			if (i > 0)
				g_string_append_c (string, dh->priv->str_locale->separator);
			switch (dh->priv->str_locale->dmy_order[i]) {
			case G_DATE_DAY:
			case G_DATE_MONTH:
				g_string_append (string, "00");
				break;
			case G_DATE_YEAR:
				if (dh->priv->str_locale->twodigit_years)
					g_string_append (string, "00");
				else
					g_string_append (string, "0000");
				break;
			default:
				g_assert_not_reached ();
				break;
			}
		}
	}
	if (type == GDA_TYPE_TIMESTAMP)
		g_string_append_c (string, ' ');

	if ((type == GDA_TYPE_TIME) || (type == GDA_TYPE_TIMESTAMP) || (type == G_TYPE_DATE_TIME))
		g_string_append (string, "00:00:00");

	str = string->str;
	g_string_free (string, FALSE);
	return str;
}

/* Interface implementation */

/* REM: SQL date format is always returned using the MM-DD-YYY format, it's up to the
 * provider to be correctly set up to accept this format.
 */
static gchar *
gda_handler_time_get_sql_from_value (GdaDataHandler *iface, const GValue *value)
{
	g_assert (value);

	gchar *retval = NULL, *str;
	GdaHandlerTime *hdl;
	GType type;

	g_return_val_if_fail (GDA_IS_HANDLER_TIME (iface), NULL);
	hdl = (GdaHandlerTime*) (iface);
	type = G_VALUE_TYPE (value);

	if (type == G_TYPE_DATE) {
		const GDate *date;

		date = (GDate *) g_value_get_boxed (value);
		str = render_date_locale (date, hdl->priv->sql_locale);
		if (!str)
			retval = g_strdup ("NULL");
		else {
			retval = g_strdup_printf ("'%s'", str);
			g_free (str);
		}
	}
	else if (type == GDA_TYPE_TIME) {
		const GdaTime *tim;
		GString *string;
		string = g_string_new ("");
		tim = gda_value_get_time ((GValue *) value);
		g_string_append_c (string, '\'');
		g_string_append_printf (string, "%02d:%02d:%02d",
					tim->hour,
					tim->minute,
					tim->second);
		if (tim->timezone != GDA_TIMEZONE_INVALID)
			g_string_append_printf (string, "%+02d",
						(int) tim->timezone / 3600);
		g_string_append_c (string, '\'');
		retval = g_string_free (string, FALSE);
	}
	else if (type == GDA_TYPE_TIMESTAMP) {
		const GdaTimestamp *gdats;
		GDate *vdate;

		gdats = gda_value_get_timestamp ((GValue *) value);
		vdate = g_date_new_dmy (gdats->day, gdats->month, gdats->year);
		str = render_date_locale (vdate, hdl->priv->sql_locale);
		g_date_free (vdate);

		if (str) {
			GString *string;
			string = g_string_new ("");
			g_string_append_printf (string, "%02u:%02u:%02u",
						gdats->hour,
						gdats->minute,
						gdats->second);
			if (gdats->fraction != 0)
				g_string_append_printf (string, ".%lu", gdats->fraction);
			
			if (gdats->timezone != GDA_TIMEZONE_INVALID)
				g_string_append_printf (string, "%+02d",
							(int) gdats->timezone / 3600);
			
			retval = g_strdup_printf ("'%s %s'", str, string->str);
			g_free (str);
			g_string_free (string, TRUE);
		}
		else
			retval = g_strdup ("NULL");	
	}
	else if (type == G_TYPE_DATE_TIME) {
		GDateTime *ts;
		GDate *vdate;

		ts = g_value_get_boxed ((GValue *) value);
		if (ts) {
			gint y, m, d;
			g_date_time_get_ymd (ts, &y, &m, &d);
			vdate = g_date_new_dmy (d, m, y);
			str = render_date_locale (vdate, hdl->priv->sql_locale);
			g_date_free (vdate);

			if (str) {
				GString *string;
				string = g_string_new ("");
				g_string_append_printf (string, "%02u:%02u:%02u",
							g_date_time_get_hour (ts),
							g_date_time_get_minute (ts),
							g_date_time_get_second (ts));
				if (g_date_time_get_microsecond (ts) != 0)
					g_string_append_printf (string, ".%d", g_date_time_get_microsecond (ts));

				GTimeSpan span;
				span = g_date_time_get_utc_offset (ts);
				if (span > 0)
					g_string_append_printf (string, "+%02d",
								(int) (span / G_TIME_SPAN_HOUR));
				else
					g_string_append_printf (string, "-%02d",
								(int) (-span / G_TIME_SPAN_HOUR));

				retval = g_strdup_printf ("'%s %s'", str, string->str);
				g_free (str);
				g_string_free (string, TRUE);
			}
			else
				retval = g_strdup ("NULL");
		}
		else
			retval = g_strdup ("NULL");
	}
	else
		g_assert_not_reached ();

	return retval;
}

static gchar *strip_quotes (const gchar *str);
static gchar *
gda_handler_time_get_str_from_value (GdaDataHandler *iface, const GValue *value)
{
	g_assert (value);

	GdaHandlerTime *hdl;
	gchar *retval = NULL, *str;
	GType type;

	g_return_val_if_fail (GDA_IS_HANDLER_TIME (iface), NULL);
	hdl = (GdaHandlerTime*) (iface);
	type = G_VALUE_TYPE (value);

	if (type == G_TYPE_DATE) {
		const GDate *date;

		date = (GDate *) g_value_get_boxed (value);
		retval = render_date_locale (date, hdl->priv->str_locale);
		if (!retval)
			retval = g_strdup ("");
	}
	else if (type == GDA_TYPE_TIME) {
		str = gda_handler_time_get_sql_from_value (iface, value);
		retval = strip_quotes (str);
		g_free (str);
	}
	else if (type == GDA_TYPE_TIMESTAMP) {
		const GdaTimestamp *gdats;
		GDate *vdate;

		gdats = gda_value_get_timestamp ((GValue *) value);
		vdate = g_date_new_dmy (gdats->day, gdats->month, gdats->year);
		str = render_date_locale (vdate, hdl->priv->str_locale);
		g_date_free (vdate);

		if (str) {
			GString *string;
			string = g_string_new ("");
			g_string_append_printf (string, "%02u:%02u:%02u",
						gdats->hour,
						gdats->minute,
						gdats->second);
			if (gdats->fraction != 0)
				g_string_append_printf (string, ".%lu", gdats->fraction);
			
			if (gdats->timezone != GDA_TIMEZONE_INVALID)
				g_string_append_printf (string, "%+02d",
							(int) gdats->timezone / 3600);
			
			retval = g_strdup_printf ("%s %s", str, string->str);
			g_free (str);
			g_string_free (string, TRUE);
		}
		else
			retval = g_strdup ("");	
	}
	else if (type == G_TYPE_DATE_TIME) {
		GDateTime *ts;
		GDate *vdate;

		ts = g_value_get_boxed ((GValue *) value);
		if (ts) {
			gint y, m, d;
			g_date_time_get_ymd (ts, &y, &m, &d);
			vdate = g_date_new_dmy (d, m, y);
			str = render_date_locale (vdate, hdl->priv->str_locale);
			g_date_free (vdate);

			if (str) {
				GString *string;
				string = g_string_new ("");
				g_string_append_printf (string, "%02u:%02u:%02u",
							g_date_time_get_hour (ts),
							g_date_time_get_minute (ts),
							g_date_time_get_second (ts));
				if (g_date_time_get_microsecond (ts) != 0)
					g_string_append_printf (string, ".%d", g_date_time_get_microsecond (ts));

				GTimeSpan span;
				span = g_date_time_get_utc_offset (ts);
				if (span > 0)
					g_string_append_printf (string, "+%02d",
								(int) (span / G_TIME_SPAN_HOUR));
				else
					g_string_append_printf (string, "-%02d",
								(int) (-span / G_TIME_SPAN_HOUR));

				retval = g_strdup_printf ("%s %s", str, string->str);
				g_free (str);
				g_string_free (string, TRUE);
			}
			else
				retval = g_strdup ("NULL");
		}
		else
			retval = g_strdup ("NULL");
	}
	else
		g_assert_not_reached ();
       
	return retval;
}

static gchar *
render_date_locale (const GDate *date, LocaleSetting *locale)
{
	GString *string;
	gchar *retval;
	gint i;

	if (!date)
		return NULL;

	string = g_string_new ("");
	for (i=0; i<3; i++) {
		if (i)
			g_string_append_c (string, locale->separator);

		switch (locale->dmy_order[i]) {
		case G_DATE_DAY:
			g_string_append_printf (string, "%02d", g_date_get_day (date));
			break;
		case G_DATE_MONTH:
			g_string_append_printf (string, "%02d", g_date_get_month (date));
			break;
		case G_DATE_YEAR:
			if (locale->twodigit_years) {
				GDateYear year = g_date_get_year (date);
				if ((year >= locale->current_offset) && (year < locale->current_offset + 100))
					g_string_append_printf (string, "%02d", year - locale->current_offset);
				else
					g_string_append_printf (string, "%04d", year);
			}
			else
				g_string_append_printf (string, "%04d", g_date_get_year (date));
			break;
		}
	}

	retval = string->str;
	g_string_free (string, FALSE);
	return retval;
}

static gchar *
strip_quotes (const gchar *str)
{
	gchar *ptr = g_strdup (str);
	gchar *to_free = ptr, *retval;

        if (*ptr == '\'') {
                ptr++;
        }

        if (*(ptr+(strlen (ptr)-1)) == '\'') {
                *(ptr+(strlen (ptr)-1)) = 0;
        }

	retval = g_strdup (ptr);
	g_free (to_free);
        return retval;
}

static GValue *gda_handler_time_get_value_from_locale (GdaDataHandler *iface, const gchar *sql, 
							GType type, LocaleSetting *locale);

static GValue *
gda_handler_time_get_value_from_sql (GdaDataHandler *iface, const gchar *sql, GType type)
{
	g_assert (sql);

	GdaHandlerTime *hdl;
	GValue *value = NULL;

	g_return_val_if_fail (GDA_IS_HANDLER_TIME (iface), NULL);
	hdl = (GdaHandlerTime*) (iface);

	if (*sql) {
		gint i = strlen (sql);
		if ((i>=2) && (*sql=='\'') && (sql[i-1]=='\'')) {
			gchar *str = g_strdup (sql);
			str[i-1] = 0;
			value = gda_handler_time_get_value_from_locale (iface, str+1, type, hdl->priv->sql_locale);
			g_free (str);
		}
	}
	else
		value = gda_value_new_null ();

	return value;
}

static GValue *
gda_handler_time_get_value_from_str (GdaDataHandler *iface, const gchar *str, GType type)
{
	g_assert (str);
	GdaHandlerTime *hdl;

	g_return_val_if_fail (GDA_IS_HANDLER_TIME (iface), NULL);
	hdl = (GdaHandlerTime*) (iface);

	if (*str == '\'')
		return NULL;
	else
		return gda_handler_time_get_value_from_locale (iface, str, type, hdl->priv->str_locale);
}



static gboolean make_timestamp (GdaHandlerTime *hdl, GdaTimestamp *timestamp, 
				const gchar *value, LocaleSetting *locale);
static gboolean make_date (GdaHandlerTime *hdl, GDate *date, const gchar *value,
			   LocaleSetting *locale, const gchar **out_endptr);
static gboolean make_time (GdaHandlerTime *hdl, GdaTime *timegda, const gchar *value);
static GDateTime *make_ts (GdaHandlerTime *hdl, const gchar *value, LocaleSetting *locale);
static GValue *
gda_handler_time_get_value_from_locale (GdaDataHandler *iface, const gchar *sql, 
					GType type, LocaleSetting *locale)
{
	GdaHandlerTime *hdl;
	GValue *value = NULL;

	g_return_val_if_fail (GDA_IS_HANDLER_TIME (iface), NULL);
	hdl = (GdaHandlerTime*) (iface);

	if (type == G_TYPE_DATE) {
		GDate date;
		if (make_date (hdl, &date, sql, locale, NULL)) {
			value = g_value_init (g_new0 (GValue, 1), G_TYPE_DATE);
			g_value_set_boxed (value, (gconstpointer) &date);
		}
	}
	else if (type == GDA_TYPE_TIME) {
		GdaTime timegda;
		if (make_time (hdl, &timegda, sql)) {
			value = g_value_init (g_new0 (GValue, 1), GDA_TYPE_TIME);
			gda_value_set_time (value, &timegda);
		}
	}
	else if (type == GDA_TYPE_TIMESTAMP) {
		GdaTimestamp timestamp;
		if (make_timestamp (hdl, &timestamp, sql, locale)) {
			value = g_value_init (g_new0 (GValue, 1), GDA_TYPE_TIMESTAMP);
			gda_value_set_timestamp (value, &timestamp);
		}
	}
	else if (type == G_TYPE_DATE_TIME) {
		GDateTime *ts;
		ts = make_ts (hdl, sql, locale);
		if (ts) {
			value = g_value_init (g_new0 (GValue, 1), G_TYPE_DATE_TIME);
			g_value_take_boxed (value, ts);
		}
	}
	else
		g_assert_not_reached ();

	return value;
}

static GDateTime *
make_ts (GdaHandlerTime *hdl, const gchar *value, LocaleSetting *locale)
{
	GDateTime *ts;
	const gchar *end_ptr;
	GDate vdate;
	GdaTime vtime;
	memset (&vtime, 0, sizeof (GdaTime));
	vtime.timezone = GDA_TIMEZONE_INVALID;

	gboolean retval;
	retval = make_date (hdl, &vdate, value, locale, &end_ptr);
	if (retval) {
		if (*end_ptr != ' ')
			retval = FALSE;
		else
			retval = make_time (hdl, &vtime, end_ptr + 1);
	}
	if (!retval)
		return NULL;

	GTimeZone *tz;
	if (vtime.timezone != GDA_TIMEZONE_INVALID) {
		gchar *tzdef;
		if (vtime.timezone >= 0)
			tzdef = g_strdup_printf ("+%02d", (gint) (vtime.timezone / 3600));
		else
			tzdef = g_strdup_printf ("-%02d", (gint) (- vtime.timezone / 3600));
		tz = g_time_zone_new (tzdef);
		g_free (tzdef);
	}
	else
		tz = g_time_zone_new_local ();
	ts = g_date_time_new (tz, vdate.year, vdate.month, vdate.day,
			      vtime.hour, vtime.minute, (gdouble) vtime.second + vtime.fraction);
	g_time_zone_unref (tz);

	return ts;
}

/* Makes a GdaTimestamp from a string like "24-12-2003 13:12:01.12+01",
 * taken from libgda/gda-value.h
 * with a modification for the date format
 */
static gboolean
make_timestamp (GdaHandlerTime *hdl, GdaTimestamp *timestamp, const gchar *value, LocaleSetting *locale)
{
	gboolean retval;
	const gchar *end_ptr;
	GDate vdate;
	GdaTime vtime;
	memset (&vtime, 0, sizeof (GdaTime));
	vtime.timezone = GDA_TIMEZONE_INVALID;

	retval = make_date (hdl, &vdate, value, locale, &end_ptr);
	timestamp->day = vdate.day;
	timestamp->month = vdate.month;
	timestamp->year = vdate.year;

	if (retval) {
		if (*end_ptr != ' ')
			retval = FALSE;
		else
			retval = make_time (hdl, &vtime, end_ptr + 1);
	}

	timestamp->hour = vtime.hour;
	timestamp->minute = vtime.minute;
	timestamp->second = vtime.second;
	timestamp->fraction = vtime.fraction;
	timestamp->timezone = vtime.timezone;

	/*g_print ("Value #%s# => %d\n", value, retval);*/

	return retval;
}

static gboolean
get_uint_from_string (const gchar *str, guint16 *out_int)
{
	long int li;
	char *endptr = NULL;
	li = strtol (str, &endptr, 10);
	if (!*endptr && (li >= 0) && (li <= G_MAXUINT16)) {
		*out_int = (guint16) li;
		return TRUE;
	}
	else {
		*out_int = 0;
		return FALSE;
	}
}

/* Makes a GDate from a string like "24-12-2003"
 * If @out_endptr is %NULL, then all the @value has to be consumed and there must not
 * be any character left. If it's not %NULL, then it will point on the first unused character
 * of @value.
 */
static gboolean
make_date (G_GNUC_UNUSED GdaHandlerTime *hdl, GDate *date, const gchar *value,
	   LocaleSetting *locale, const gchar **out_endptr)
{
	gboolean retval = TRUE;
	guint16 nums[3];
	gboolean error = FALSE;
	gchar *ptr, *numstart, *tofree;
	gint i;

	if (out_endptr)
		*out_endptr = NULL;

	if (!value)
		return FALSE;

	g_date_clear (date, 1);
	g_date_set_dmy (date, 1, 1, 1);
	
	/* 1st number */
	ptr = g_strdup (value);
	tofree = ptr;
	numstart = ptr;
	while (*ptr && g_ascii_isdigit (*ptr))
		ptr++;
	if ((ptr != numstart) && *ptr) {
		*ptr = 0;
		if (! get_uint_from_string (numstart, &(nums[0])))
			error = TRUE;
	}
	else
		error = TRUE;

	/* 2nd number */
	if (!error) {
		if (value [ptr-tofree] != locale->separator)
			error = TRUE;
		else {
			ptr++;
			numstart = ptr;
			while (*ptr && g_ascii_isdigit (*ptr))
				ptr++;
			if ((ptr != numstart) && *ptr) {
				*ptr = 0;
				if (! get_uint_from_string (numstart, &(nums[1])))
					error = TRUE;
			}
			else
				error = TRUE;
		}
	}

	/* 3rd number */
	if (!error) {
		if (value [ptr-tofree] != locale->separator)
			error = TRUE;
		else {
			ptr++;
			numstart = ptr;
			while (*ptr && g_ascii_isdigit (*ptr))
				ptr++;
			*ptr = 0;
			if (ptr != numstart) {
				if (! get_uint_from_string (numstart, &(nums[2])))
					error = TRUE;
			}
			else
				error = TRUE;
		}
	}

	/* test if there are some characters left */
	if (!error) {
		if (out_endptr)
			*out_endptr = value + (ptr-tofree);
		else if (value [ptr-tofree])
			error = TRUE;
	}

	/* analyse what's parsed */
	if (!error) {
		for (i=0; i<3; i++) {
			switch (locale->dmy_order[i]) {
			case G_DATE_DAY:
				if ((nums[i] <= G_MAXUINT8) && g_date_valid_day ((GDateDay) nums[i]))
					g_date_set_day (date, nums[i]);
				else
					retval = FALSE;
				break;
			case G_DATE_MONTH:
				if ((nums[i] <= 12) && g_date_valid_month ((GDateMonth) nums[i]))
					g_date_set_month (date, nums[i]);
				else
					retval = FALSE;
				break;
			case G_DATE_YEAR:
				if (g_date_valid_year (nums[i] < 100 ? nums[i] + locale->current_offset : nums[i]))
					g_date_set_year (date, nums[i] < 100 ? nums[i] + locale->current_offset : nums[i]);
				else
					retval = FALSE;
				break;
			}
		}

		/* checks */
		if (retval)
			retval = g_date_valid (date);
	}
	else
		retval = FALSE;

	g_free (tofree);

	return retval;
}

/* Makes a GdaTime from a string like:
 * 12:30:15+01
 * 12:30:15-02
 * 12:30:15.123
 * 123015+01
 * 123015-02
 * 123015.123
 * taken from libgda/gda-value.h
 *
 * Also works if there is only 0 or 1 digit instead of 2
 */
static gboolean
make_time (G_GNUC_UNUSED GdaHandlerTime *hdl, GdaTime *timegda, const gchar *value)
{
	if (gda_parse_iso8601_time (timegda, value))
		return TRUE;
	else
		return gda_parse_formatted_time (timegda, value, 0);
}


static GValue *
gda_handler_time_get_sane_init_value (G_GNUC_UNUSED GdaDataHandler *iface, GType type)
{
	GValue *value = NULL;
	
	time_t now;
	struct tm *stm;

	g_return_val_if_fail (GDA_IS_HANDLER_TIME (iface), NULL);

	now = time (NULL);
#ifdef HAVE_LOCALTIME_R
	struct tm tmpstm;
	stm = localtime_r (&now, &tmpstm);
#elif HAVE_LOCALTIME_S
	struct tm tmpstm;
	g_assert (localtime_s (&tmpstm, &now) == 0);
	stm = &tmpstm;
#else
	stm = localtime (&now);
#endif

	if (type == G_TYPE_DATE) {
		GDate *gdate;

		gdate = g_date_new_dmy (stm->tm_mday, stm->tm_mon + 1, stm->tm_year + 1900);
		value = g_value_init (g_new0 (GValue, 1), G_TYPE_DATE);
		g_value_take_boxed (value, gdate);
	}
	else if (type == GDA_TYPE_TIME) {
		GdaTime gtime;

                gtime.hour = stm->tm_hour;
		gtime.minute = stm->tm_min;
		gtime.second = stm->tm_sec;
		gtime.timezone = GDA_TIMEZONE_INVALID;
		value = g_value_init (g_new0 (GValue, 1), GDA_TYPE_TIME);
		gda_value_set_time (value, &gtime);
	}
	else if (type == GDA_TYPE_TIMESTAMP) {
		GdaTimestamp gts;

		gts.year = stm->tm_year + 1900;
		gts.month = stm->tm_mon + 1;
		gts.day = stm->tm_mday;
                gts.hour = stm->tm_hour;
		gts.minute = stm->tm_min;
		gts.second = stm->tm_sec;
		gts.fraction = 0;
		gts.timezone = GDA_TIMEZONE_INVALID;
		value = g_value_init (g_new0 (GValue, 1), GDA_TYPE_TIMESTAMP);
		gda_value_set_timestamp (value, &gts);
	}
	else if (type == G_TYPE_DATE_TIME) {
		GDateTime *ts;
		GTimeZone *tz;
		tz = g_time_zone_new_local ();
		ts = g_date_time_new_now (tz);
		g_time_zone_unref (tz);
		value = g_value_init (g_new0 (GValue, 1), G_TYPE_DATE_TIME);
		g_value_take_boxed (value, ts);
	}
	else
		g_assert_not_reached ();

	return value;
}

static gboolean
gda_handler_time_accepts_g_type (GdaDataHandler *iface, GType type)
{
	GdaHandlerTime *hdl;
	guint i;

	g_assert (iface);
	hdl = (GdaHandlerTime*) (iface);

	for (i = 0; i < hdl->priv->nb_g_types; i++) {
		if (hdl->priv->valid_g_types [i] == type)
			return TRUE;
	}

	return FALSE;
}

static const gchar *
gda_handler_time_get_descr (GdaDataHandler *iface)
{
	g_return_val_if_fail (GDA_IS_HANDLER_TIME (iface), NULL);
	return g_object_get_data (G_OBJECT (iface), "descr");
}
