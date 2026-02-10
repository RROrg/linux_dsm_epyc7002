include /env.mak

obj-m += ryzen5k-synobios.o

ryzen5k-synobios-objs = \
	../common/common.o \
	../syno_ttyS/syno_ttyS.o \
	../synobios.o \
	rs4024xs+.o \
	../mapping.o \
	../rtc/localtime.o \
	../rtc/alarmtime.o \
	../rtc/rtc-pericom-lib.o \
	../rtc/rtc-linux-pericom.o \
	../i2c/i2c-linux.o \
	ryzen5k_common.o \
	../led/led_trigger.o
