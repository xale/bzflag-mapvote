========================================================================
    DYNAMIC LINK LIBRARY : MapVote Project Overview
========================================================================

This is the MapVote plugin. It allows an administrator or operator on a server
to initiate a map voting poll, which will be used to change maps on the server,
and will automatically initiate a poll when the server has a "game over."

HOW TO BUILD:
	- Download current version of BZFlag source
	- Place the MapVote/ directory inside the plugins/ directory
	- Edit the configure script, and add plugins/MapVote/Makefile to ac_config_files
	- Edit the Makefile.in and Makefile.ac in plugins/, and add MapVote to the list of SUBDIRS
	- Run configure script with --enable-shared (add <--disable-client> to speed things up)
	- In the base directory, run make
	- The compiled .so file should be located in plugins/MapVote/.libs/

HOW TO USE THIS PLUGIN:
The plugin should be loaded from the command line or in a configuration file at server startup with the flag:
-loadplugin MapVote.so,/Path/to/maplist/file
The maplist file must contain a list of (absolute) paths to map files, one per line.

The plugin adds the following slash commands:
---------------------------------------------

/startmapvote <mapname> ... <mapname>: initiate a voting session with the specified maps
	(administrator/operator only)
	NOTE: specifying no maps will start a poll with all maps in the maplist

/endmapvote: terminate voting, and switch to the winning map
	(administrator/operator only)

/cancelmapvote: terminate voting, but do not change maps
	(administrator/operator only)

/changemap <mapname>: jump directly to a map without voting
	(administrator/operator only)

/listmaps: list maps available in the server rotation

/listvotes: list the maps in the current map voting poll, along with the number of votes for each
	(only works if voting has been initiated)

/votemap <mapname>: vote for a map
	(only works if voting has been initiated)
