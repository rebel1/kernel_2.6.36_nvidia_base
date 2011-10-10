#------------------------------------------------------------------------------
# <copyright file="makefile" company="Atheros">
#    Copyright (c) 2008 Atheros Corporation.  All rights reserved.
# 
# The software source and binaries included in this development package are
# licensed, not sold. You, or your company, received the package under one
# or more license agreements. The rights granted to you are specifically
# listed in these license agreement(s). All other rights remain with Atheros
# Communications, Inc., its subsidiaries, or the respective owner including
# those listed on the included copyright notices.  Distribution of any
# portion of this package must be in strict compliance with the license
# agreement(s) terms.
# </copyright>
# 
# <summary>
# 	Wifi driver for AR6002
# </summary>
#
#------------------------------------------------------------------------------
#==============================================================================
# Author(s): ="Atheros"
#==============================================================================

#USE_GLIB=1


CC=		$(ATH_CROSSS_COMPILE_TYPE)gcc
LD=		$(ATH_CROSSS_COMPILE_TYPE)ld
SOURCES=abtfilt_main.c \
		abtfilt_wlan.c \
		abtfilt_core.c \
		abtfilt_utils.c
	
INCLUDES=	-I../../../include \
		-I../../../os/linux/include \
		-I../../../../include \
		-I../../../ \
		-I/usr/include/dbus-1.0/ \
		-I/usr/lib/dbus-1.0/include \
		-I/usr/include/bluetooth \
			
CFLAGS=		-Wall -g -DABF_DEBUG
LIBS= 		-ldbus-1 -lpthread -lbtfilt -lrt -lbluetooth


ifdef USE_GLIB	
SOURCES += abtfilt_bluez_dbus_glib.c

LIBS += -lgobject-2.0 -lglib-2.0 -ldbus-glib-1

INCLUDES += -I/usr/include/glib-2.0     \
			-I/usr/lib/glib-2.0/include \

OBJECTS=	$(SOURCES:.c=.o)			

else	
SOURCES += abtfilt_bluez_dbus.c

OBJECTS=	$(SOURCES:.c=.o)
endif



LDFLAGS=	-L$(WORKAREA)/host/btfilter
FILTERAPP=	abtfilt

all: $(OBJECTS)
	$(CC) -o $(FILTERAPP) $(OBJECTS) $(LDFLAGS) $(LIBS)

.c.o:
	$(CC) -c $(CFLAGS) $(INCLUDES) $< -o $@

clean:
	rm -f $(FILTERAPP) $(OBJECTS)
