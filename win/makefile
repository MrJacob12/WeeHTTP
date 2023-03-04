TARGET = api
BINDIR = bin
SRCDIR = src
OBJDIR = obj

INCLUDEDIRS = 
LIBDIRS = 
LIBS = ws2_32

#CC = gcc
CFLAGS = -c -g -std=c11 -Wall -Wextra -Werror -Wno-unused-variable $(addprefix -I./, $(INCLUDEDIRS)) -DUNICODE
LDFLAGS = $(addprefix -L./, $(LIBDIRS)) $(addprefix -l, $(LIBS))

ifeq ($(OS), Windows_NT)
	TARGET := $(addsuffix .exe, $(TARGET))
endif

SRC = $(wildcard $(SRCDIR)/**.c)
OBJS = $(patsubst $(SRCDIR)/%.c, $(OBJDIR)/%.o, $(SRC)) #$(OBJDIR)/resources.o
HEADERS = $(wildcard $(SRCDIR)/**.h)

ifeq ($(OS), Windows_NT)
	DIRGUARD = @cmd /c if not exist $(@D) mkdir $(@D)
	RMDIR = @cmd /c if exist $(1) rd /s /q $(1)
else
	DIRGUARD = @mkdir -p $(@D)
	RMDIR = @rm -rf $(1)
endif

$(BINDIR)/$(TARGET): $(OBJS)
	$(DIRGUARD)
	$(CC) $^ -o $@ $(LDFLAGS) 

$(OBJDIR)/resources.o: $(SRCDIR)/resources.rc $(SRCDIR)/app.manifest $(HEADERS)
	$(DIRGUARD)
	windres $< $@

$(OBJDIR)/%.o: $(SRCDIR)/%.c $(HEADERS)
	$(DIRGUARD)
	$(CC) -o $@ $< $(CFLAGS) 

.PHONY: clean run

clean:
	$(call RMDIR, $(BINDIR))
	$(call RMDIR, $(OBJDIR))

run: $(BINDIR)/$(TARGET)
	$^
