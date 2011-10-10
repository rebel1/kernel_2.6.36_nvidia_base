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
		-I. \
		-I./bluetooth \
		-I/home/edu/android-ndk-r5b/platforms/android-9/arch-arm/usr/include
		
			
CFLAGS=		-Wall -g -DABF_DEBUG -nostdlib -mabi=aapcs -Wl,--dynamic-linker=/system/bin/linker -Wl,-rpath=/system/lib -L.
LIBS= 		-Wl,--dynamic-linker=/system/bin/linker -Wl,-rpath=/system/lib -L. -ldbus -lc -lbtfilt -llog -ldl -lbluetooth -lglib -lcutils -lstdc++ -lm -ldbus


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



LDFLAGS=	-L$(WORKAREA)/host/btfilter -nostdlib -L.
FILTERAPP=	abtfilt

all: $(OBJECTS)
	$(CC) -o $(FILTERAPP) $(OBJECTS) crtbegin_dynamic.o $(LDFLAGS) $(LIBS)

.c.o:
	$(CC) -c $(CFLAGS) $(INCLUDES) -DANDROID=1 -DANDROID_ENV=1 $< -o $@

clean:
	rm -f $(FILTERAPP) $(OBJECTS)
