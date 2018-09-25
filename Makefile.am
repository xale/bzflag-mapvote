pkglib_LTLIBRARIES = MapVote.la

MapVote_la_SOURCES = MapVote.cpp
MapVote_la_CXXFLAGS= -I$(top_srcdir)/include -I$(top_srcdir)/plugins/plugin_utils
MapVote_la_LDFLAGS = -module -avoid-version
MapVote_la_LIBADD = $(top_srcdir)/plugins/plugin_utils/libplugin_utils.la


EXTRA_DIST = \
	README.txt \
	MapVote.def \
	MapVote.sln \
	MapVote.vcproj

MAINTAINERCLEANFILES =	\
	Makefile.in
