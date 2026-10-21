#
# Makefile - Pisound + Wi-Fi + NAM test on Circle
#

CIRCLEHOME = ../../../..

OBJS	= main.o kernel.o pisoundsound.o ota.o oscillator.o control.o graph.o board.o fx.o fxmath.o fx_comp.o fx_delay.o fx_verb.o fx_phase.o fx_sweep.o fx_spring.o fx_chorus.o fx_cab.o fx_equal.o fx_shifter.o fx_delaym.o fx_verbm.o fx_extra.o fx_extra2.o fx_tube.o fx_rat.o fx_la2a.o fx_dimd.o fx_muff.o fx_fuzzface.o fx_flanger117.o fx_octaver.o fx_ocd.o fx_univibe.o fx_ad9.o fx_mflanger.o fx_ce1.o fx_phase90.o fx_factory.o fx_ce2.o fx_dynacomp.o fx_tonestack.o fx_wah.o fx_bender.o fx_klon.o fx_guvnor.o fx_ds1.o fx_rangemaster.o ftools.o midi.o pisounddin.o dhcpcore.o dhcpserver.o

# multi-core test (make MC_PROBE=1, with Circle built with ARM_ALLOW_MULTI_CORE)
ifdef MC_PROBE
OBJS += mcprobe.o
DEFINE += -DMC_PROBE
endif

# -O3: the NAM engine loops (frame tiles, template parameter T) must unroll completely
OPTIMIZE = -O3

LIBS	= $(CIRCLEHOME)/addon/tftpfileserver/libtftpfileserver.a \
	  $(CIRCLEHOME)/addon/wlan/hostap/wpa_supplicant/libwpa_supplicant.a \
	  $(CIRCLEHOME)/addon/wlan/libwlan.a \
	  $(CIRCLEHOME)/addon/fatfs/libfatfs.a \
	  $(CIRCLEHOME)/addon/SDCard/libsdcard.a \
	  $(CIRCLEHOME)/lib/net/libnet.a \
	  $(CIRCLEHOME)/lib/sched/libsched.a \
	  $(CIRCLEHOME)/lib/sound/libsound.a \
	  $(CIRCLEHOME)/lib/usb/libusb.a \
	  $(CIRCLEHOME)/lib/input/libinput.a \
	  $(CIRCLEHOME)/lib/fs/libfs.a \
	  $(CIRCLEHOME)/lib/libcircle.a

# the linker discards never-used functions (--gc-sections): same generated code, less space (needed to keep the margin below 0x280000)
GC_SECTIONS = 1

include $(CIRCLEHOME)/Rules.mk

# the web server is not in the audio path: optimized for size (the last -O wins)
control.o: CPPFLAGS += -Os

# ADEL engines (standard C++ without libstdc++): compatibility headers in fxshim/, engine sources in adel/
CPPFLAGS += -I fxshim -I adel -fno-threadsafe-statics

-include $(DEPS)

# main-loop files (web page, pedalboard document, kernel, updates): optimized for size, they do not touch the audio callback.
# The kernel limit is 2 MB: with -Os these four files free about 90 KB without touching the audio CPU.
SMALLOBJS = control.o board.o kernel.o ota.o fx_extra2.o
$(SMALLOBJS): OPTIMIZE = -Os

# web page compressed with gzip at build time: control_page_gz.h is generated from control_page.h (the source page stays control_page.h)
control_page_gz.h: control_page.h mkpagegz.py
	python3 mkpagegz.py
control.o: control_page_gz.h
