AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/libgda \
	-I$(top_builddir) \
	$(COREDEPS_CFLAGS) \
	$(COREDEPS_WFLAGS) \
	$(GTK_CFLAGS) \
	-DROOT_DIR=\""$(top_srcdir)"\" \
	-DTOP_BUILD_DIR=\""$(top_builddir)"\"

TESTS_ENVIRONMENT = GDA_TOP_SRC_DIR="$(abs_top_srcdir)" GDA_TOP_BUILD_DIR="$(abs_top_builddir)"
TESTS = check_sets check_raw_grid_model
check_PROGRAMS = check_sets check_raw_grid_model

check_sets_SOURCES = common.c common.h check_sets.c
check_sets_LDADD = \
	$(top_builddir)/libgda/libgda-6.0.la \
	$(top_builddir)/libgda-ui/libgda-ui-6.0.la \
	$(COREDEPS_LIBS) \
	$(GTK_LIBS)

check_raw_grid_model_SOURCES = common.c common.h check_raw_grid_model.c
check_raw_grid_model_LDADD = \
        $(top_builddir)/libgda/libgda-6.0.la \
        $(top_builddir)/libgda-ui/libgda-ui-6.0.la \
        $(COREDEPS_LIBS) \
        $(GTK_LIBS)
