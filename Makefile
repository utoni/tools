CC := gcc
INSTALL := install
CFLAGS := -O2 -g -Wall -ffunction-sections -fdata-sections -ffast-math -fomit-frame-pointer -fexpensive-optimizations -Wl,--gc-sections
LDFLAGS :=
RM := rm -rf

TARGETS := aes asciihexer dummyshell suidcmd ascii85 textify progressbar

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
asciihexer: asciihexer.o
dummyshell: dummyshell.o
suidcmd: suidcmd.o
	@echo 'Building target: $@'
	@echo 'Invoking: GCC C Linker'
	$(CC) $(LDFLAGS)  -o "$@" "$<"
	@echo 'Finished building target: $@'
	@echo ' '

ascii85: ascii85.o
	@echo 'Building target: $@'
	@echo 'Invoking: GCC C Linker'
	$(CC) $(LDFLAGS)  -o "$@" "$<" -lm
	@echo 'Finished building target: $@'
	@echo ' '

textify: textify.o
	@echo 'Building target: $@'
	@echo 'Invoking: GCC C Linker'
	$(CC) $(LDFLAGS)  -o "$@" "$<" -lm
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

xdiff: xdiff.o
	@echo 'Building target: $@'
	@echo 'Invoking: GCC C Linker'
	$(CC) $(LDFLAGS)  -o "$@" "$<" -lX11
	@echo 'Finished building target: $@'
	@echo ' '

progressbar: progressbar.o
	@echo 'Building target: $@'
	@echo 'Invoking: GCC C Linker'
	$(CC) $(LDFLAGS)  -o "$@" "$<"
	@echo 'Finished building target: $@'
	@echo ' '

strip:
	strip -s $(TARGETS)

clean:
	-$(RM) aes.o asciihexer.o dummyshell.o gol.o suidcmd.o ascii85.o textify.o xidle.o xdiff.o
	-$(RM) aes.d asciihexer.d dummyshell.d gol.d suidcmd.d scrambler.d textify.d xidle.d xdiff.d
	-$(RM) aes asciihexer dummyshell gol suidcmd scrambler textify xidle xdiff
	-@echo ' '

install: $(TARGETS)
	$(INSTALL) -d $(PREFIX)/usr/bin
	$(INSTALL) -s $(TARGETS) $(PREFIX)/usr/bin

rebuild: clean all

.PHONY: all clean strip
