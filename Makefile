# A simple Makefile for libShadowDive

LIBNAME=libshadowdive.a

CC=gcc
RM=rm -f
MKDIR=mkdir -p
RMDIR=rmdir
MV=mv
AR=ar

FILES := \
    src/bk.c \
    src/rgba_image.c \
    src/sprite_image.c \
    src/vga_image.c \
    src/animation.c \
    src/internal/reader.c \
    src/internal/writer.c 
    
LIBDIR=lib
INCDIR=include
OBJDIR=obj

CFLAGS=-I$(INCDIR) -O2 -Wall -s -std=c99

all: 
	$(MKDIR) $(LIBDIR)/
	$(MKDIR) $(OBJDIR)/
	$(CC) $(CFLAGS) -c $(FILES)
	$(MV) *.o $(OBJDIR)/
	$(AR) rcs $(LIBDIR)/$(LIBNAME) $(OBJDIR)/*.o
	@echo "All done!"

clean:
	$(RM) $(OBJDIR)/*
	$(RM) $(LIBDIR)/*
	$(RMDIR) $(OBJDIR)
	$(RMDIR) $(LIBDIR)