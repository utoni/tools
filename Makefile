CC := gcc
INSTALL_DIR := install -d
INSTALL_BIN := install -s
CFLAGS := -Wall -ffunction-sections -fdata-sections -ffast-math -fomit-frame-pointer -fexpensive-optimizations -Wl,--gc-sections
ifneq ($(strip $(DEBUG)),)
CFLAGS += -Og -g
ifneq ($(strip $(DEBUG_ASAN)),)
CFLAGS += -fsanitize=address -fsanitize=pointer-compare -fsanitize=pointer-subtract -fsanitize=leak -fsanitize=undefined
endif
else
CFLAGS += -O2
endif
LDFLAGS :=
RM := rm -rf

TARGETS := aes asciihexer dummyshell suidcmd ascii85 progressbar

ifneq ($(strip $(MAKE_NCURSES)),)
TARGETS += gol
endif
ifneq ($(strip $(MAKE_X11)),)
TARGETS += xidle xdiff
endif


all: $(TARGETS)

%.o: %.c
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	$(CC) $(CFLAGS) -D_GNU_SOURCE=1 -D_HAVE_CONFIG=1 -std=c99 -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


aes: aes.o
	@echo 'Building target: $@'
	@echo 'Invoking: GCC C Linker'
	$(CC) $(CFLAGS) $(LDFLAGS)  -o "$@" "$<"
	@echo 'Finished building target: $@'
	@echo ' '

asciihexer: asciihexer.o
	@echo 'Building target: $@'
	@echo 'Invoking: GCC C Linker'
	$(CC) $(CFLAGS) $(LDFLAGS)  -o "$@" "$<"
	@echo 'Finished building target: $@'
	@echo ' '

dummyshell: dummyshell.o
	@echo 'Building target: $@'
	@echo 'Invoking: GCC C Linker'
	$(CC) $(CFLAGS) $(LDFLAGS)  -o "$@" "$<"
	@echo 'Finished building target: $@'
	@echo ' '

suidcmd: suidcmd.o
	@echo 'Building target: $@'
	@echo 'Invoking: GCC C Linker'
	$(CC) $(CFLAGS) $(LDFLAGS)  -o "$@" "$<"
	@echo 'Finished building target: $@'
	@echo ' '

ascii85: ascii85.o
	@echo 'Building target: $@'
	@echo 'Invoking: GCC C Linker'
	$(CC) $(CFLAGS) $(LDFLAGS)  -o "$@" "$<" -lm
	@echo 'Finished building target: $@'
	@echo ' '

gol: gol.o
	@echo 'Building target: $@'
	@echo 'Invoking: GCC C Linker'
	$(CC) $(CFLAGS) $(LDFLAGS)  -o "$@" "$<" -lncurses
	@echo 'Finished building target: $@'
	@echo ' '

xidle: xidle.o
	@echo 'Building target: $@'
	@echo 'Invoking: GCC C Linker'
	$(CC) $(CFLAGS) $(LDFLAGS)  -o "$@" "$<" -lX11 -lXext -lXss
	@echo 'Finished building target: $@'
	@echo ' '

xdiff: xdiff.o
	@echo 'Building target: $@'
	@echo 'Invoking: GCC C Linker'
	$(CC) $(CFLAGS) $(LDFLAGS)  -o "$@" "$<" -lX11
	@echo 'Finished building target: $@'
	@echo ' '

progressbar: progressbar.o
	@echo 'Building target: $@'
	@echo 'Invoking: GCC C Linker'
	$(CC) $(CFLAGS) $(LDFLAGS)  -o "$@" "$<"
	@echo 'Finished building target: $@'
	@echo ' '

strip:
	strip -s $(TARGETS)

clean:
	-$(RM) aes.o asciihexer.o dummyshell.o gol.o suidcmd.o ascii85.o progressbar.o xidle.o xdiff.o
	-$(RM) aes.d asciihexer.d dummyshell.d gol.d suidcmd.d scrambler.d progressbar.d xidle.d xdiff.d
	-$(RM) aes asciihexer dummyshell gol suidcmd scrambler progressbar xidle xdiff
	-@echo ' '

install: $(TARGETS)
	$(INSTALL_DIR) -d $(PREFIX)/usr/bin
	$(INSTALL_BIN) -s $(TARGETS) $(PREFIX)/usr/bin

help:
	@echo '======================================'
	@echo 'Possible ARGS:'
	@echo '--------------'
	@echo 'make MAKE_X11=y MAKE_NCURSES=y DEBUG=y'
	@echo '======================================'

rebuild: clean all

.PHONY: all clean strip help
