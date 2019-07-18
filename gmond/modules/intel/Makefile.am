AM_CFLAGS  = -I$(top_builddir)/include -I$(top_builddir)/lib -I$(top_builddir)/libmetrics

if STATIC_BUILD
noinst_LTLIBRARIES    = libmodintel.la
libmodintel_la_SOURCES = mod_intel.c 
else
pkglib_LTLIBRARIES    = modintel.la
modintel_la_SOURCES = mod_intel.c 
modintel_la_LDFLAGS = -module -avoid-version
modintel_la_LIBADD = $(top_builddir)/libmetrics/libmetrics.la

EXTRA_DIST = ../conf.d/intel.conf
endif

INCLUDES = @APR_INCLUDES@

