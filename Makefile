CC = gcc

SOURCE_DIR = src
INCLUDE_DIR = include
OBJECT_DIR = obj

_create_object_dir := $(shell mkdir -p $(OBJECT_DIR))

CFLAGS = -I$(INCLUDE_DIR) -c -fPIC -fstack-protector-all \
	-Wstrict-overflow -Wformat=2 -Wformat-security -Wall -Wextra \
	-g3 -O3 -Werror
LFLAGS = -shared -lpthread

SOURCE_FILES = $(SOURCE_DIR)/thread_comm.c
HEADER_FILES = $(INCLUDE_DIR)/thread_comm.h
OBJ_FILES = $(SOURCE_FILES:$(SOURCE_DIR)/%.c=$(OBJECT_DIR)/%.o)

default: all

all: libthreadcomm.so

libthreadcomm.so: $(OBJ_FILES)
	$(CC) -o libthreadcomm.so $(OBJ_FILES) $(LFLAGS)

$(OBJECT_DIR)/%.o: $(SOURCE_DIR)/%.c $(HEADER_FILES)
	$(CC) $(CFLAGS) $< -o $@
clean:
	rm -rf libthreadcomm.so $(OBJECT_DIR) test/tests test/coverage
