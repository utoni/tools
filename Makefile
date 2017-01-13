CC := gcc
CFLAGS := -O2 -g -Wall -ffunction-sections -fdata-sections -ffast-math -fomit-frame-pointer -fexpensive-optimizations -Wl,--gc-sections
LDFLAGS :=
RM := rm -rf
LIBS := -lcurses

TARGETS := aes asciihexer dummyshell suidcmd

ifneq ($(strip $(MAKE_NCURSES)),)
TARGETS += gol
endif
ifneq ($(strip $(MAKE_X11)),)
TARGETS += xidle
endif


all: $(TARGETS)

%.o: %.c
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	$(CC) $(CFLAGS) -D_GNU_SOURCE=1 -D_HAVE_CONFIG=1 -std=c99 -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


aes: aes.o
asciihexer: asciihexer.o
dummyshell: dummyshell.o
suidcmd: suidcmd.o
	@echo 'Building target: $@'
	@echo 'Invoking: GCC C Linker'
	$(CC) $(LDFLAGS)  -o "$@" "$<"
	@echo 'Finished building target: $@'
	@echo ' '

gol: gol.o
	@echo 'Building target: $@'
	@echo 'Invoking: GCC C Linker'
	$(CC) $(LDFLAGS)  -o "$@" "$<" -lncurses
	@echo 'Finished building target: $@'
	@echo ' '

xidle: xidle.o
	@echo 'Building target: $@'
	@echo 'Invoking: GCC C Linker'
	$(CC) $(LDFLAGS)  -o "$@" "$<" -lX11 -lXext -lXss
	@echo 'Finished building target: $@'
	@echo ' '

strip:
	strip -s $(TARGETS)

clean:
	-$(RM) aes.o asciihexer.o dummyshell.o gol.o suidcmd.o xidle.o
	-$(RM) aes.d asciihexer.d dummyshell.d gol.d suidcmd.d xidle.d
	-$(RM) aes asciihexer dummyshell gol suidcmd xidle
	-@echo ' '

rebuild: clean all

.PHONY: all clean strip
