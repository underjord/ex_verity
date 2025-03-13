VERSION=0.9.3

CFLAGS ?= -Wall -Wextra

# _GNU_SOURCE is for asprintf
EXTRA_CFLAGS = -D_GNU_SOURCE -DPROGRAM_VERSION=$(VERSION)

ifneq ($(DEBUG),)
EXTRA_CFLAGS += -DDEBUG
endif

ifneq ($(TARGET_DEVICE_NAME),)
EXTRA_CFLAGS += -D$(shell echo '$(TARGET_DEVICE_NAME)' | tr '[:lower:]' '[:upper:]')
endif

CFLAGS += -DPROGRAM_VERSION=$(VERSION)

OBJS = nerves_key.o atecc508a.o atecc_serial.o

ifeq ($(shell uname),Darwin)
EXTRA_CFLAGS += -Icompat
OBJS += compat/compat.o
endif

nerves-key: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -c -o $@ $^

clean:
	$(RM) nerves-key $(OBJS)

format:
	astyle --style=kr --indent=spaces=4 --align-pointer=name --align-reference=name --convert-tabs --attach-namespaces --max-code-length=100 --max-instatement-indent=120 --pad-header --pad-oper $^

.PHONY: all clean format