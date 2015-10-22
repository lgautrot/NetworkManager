AM_CPPFLAGS = \
	-I${top_srcdir} \
	-I${top_srcdir}/include \
	-I${top_builddir}/include \
	-I${top_srcdir}/src \
	-I${top_builddir}/src \
	-I${top_srcdir}/libnm-core \
	-I${top_builddir}/libnm-core \
	-I${srcdir}/.. \
	-DG_LOG_DOMAIN=\""NetworkManager"\" \
	-DNETWORKMANAGER_COMPILATION=NM_NETWORKMANAGER_COMPILATION_INSIDE_DAEMON \
	-DNM_VERSION_MAX_ALLOWED=NM_VERSION_NEXT_STABLE \
	$(GLIB_CFLAGS) \

if REQUIRE_ROOT_TESTS
AM_CPPFLAGS += -DREQUIRE_ROOT_TESTS=1
endif

DEVICES_SOURCES = \
	../nm-lldp-listener.c \
TEST_SOURCES = $(DEVICE_SOURCES)
AM_CFLAGS = $(CODE_COVERAGE_CFLAGS)
AM_LDFLAGS = $(GLIB_LIBS) $(CODE_COVERAGE_LDFLAGS)
DEVICES_LDADD = \
	$(top_builddir)/src/libNetworkManager.la

@GNOME_CODE_COVERAGE_RULES@

noinst_PROGRAMS = \
	test-lldp

test_lldp_SOURCES = test-lldp.c $(TEST_SOURCES)
test_lldp_LDADD = $(DEVICES_LDADD)

@VALGRIND_RULES@
TESTS = \
	test-lldp
