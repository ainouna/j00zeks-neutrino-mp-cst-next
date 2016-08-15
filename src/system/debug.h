/*
	NeutrinoNG  -   DBoxII-Project

	Copyright (C) 2001 Steffen Hehn 'McClean'
	Homepage: http://dbox.cyberphoria.org/

	License: GPL

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/


#ifndef __neutrino_debug__
#define __neutrino_debug__
#include <zapit/debug.h>
extern int debug;

enum
{
	DEBUG_NORMAL	, // 0
	J00ZEK_DBG	, // 1
	DEBUG_INFO	, // 2
	DEBUG_DEBUG	, // 3

	DEBUG_MODES	  // 4 count of available modes
};


void setDebugLevel( int level );

#define dprintf(debuglevel, fmt, args...) \
	do { \
		if (debug >= debuglevel) \
			printf( "[neutrino] " fmt, ## args); \
	} while(0)
#define dperror(str) {perror("[neutrino] " str);}

#define j00zekDBG(debuglevel, fmt, args...) \
	do { \
		if (debug >= debuglevel) \
			printf( "[j00zTrino] " fmt, ## args); \
	} while(0)

#endif
