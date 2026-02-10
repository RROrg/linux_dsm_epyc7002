ifeq ($(SYNO_PLATFORM),MINDSPEED_COMCERTO2K)
obj-m += ds414j-synobios.o
obj-m += ds415j-synobios.o
endif

common-obj += \
		../syno_ttyS/syno_ttyS.o \
		../synobios.o \
		../i2c/i2c-linux.o \
		../rtc/localtime.o \
		../rtc/alarmtime.o \
		../rtc/rtc-ricoh-lib.o \
		../rtc/rtc-seiko-lib.o \
		../rtc/rtc-linux-ricoh.o \
		../rtc/rtc-linux-seiko.o \
		../mapping.o \
		comcerto2k_common.o

fan-pwm-obj += fan-pwm.o
fan-resister-obj += fan-resister.o

ds414j-synobios-objs = ds414j.o $(fan-resister-obj) $(common-obj)
ds415j-synobios-objs = ds415j.o $(fan-resister-obj) $(common-obj)
