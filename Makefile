VERSION=1.0.0
# linux
TOOLS_DIR	=
NAME_LIB	= mp4muxer.a
OPTIMIZE	= -Wall -O2
PROJECT_DIR = $(PWD)
-include $(PROJECT_DIR)/Makefile.sdk.conf


OBJ_DIR=$(PROJECT_DIR)/_build
OBJ = $(OBJ_DIR)/mp4muxer.o


CC = $(CROSSCOMPILE)gcc
CXX = $(CROSSCOMPILE)g++
AR	  =$(CROSSCOMPILE)ar
STRIP =$(CROSSCOMPILE)strip

#enviroment
CFLAGS  +=  -I$(SDK_LIB)/include
CFLAGS	+= -L$(SDK_LIB)/lib

#flag
ifdef HAVE_MP4_DEBUG
FLAG_HAVE += -D HAVE_MP4_DEBUG 
endif
CFLAGS += $(OPTIMIZE)
CFLAGS +=$(FLAG_HAVE) -Wno-deprecated-declarations


lib: create $(NAME_LIB)

create:
	@mkdir -p $(OBJ_DIR)


clean:
	rm -rf $(OBJ_DIR)
	rm -rf $(NAME_LIB)

install:
	sudo cp -r $(NAME_LIB) $(INSTALL_DIR)



$(NAME_LIB): $(OBJ) 
	@echo "--Compiling '${STATIC_LIB} build release'..."
	@$(AR) rcs ${NAME_LIB} ${OBJ}

$(OBJ_DIR)/%.o: %.cpp
	@echo $(GREEN) CPP  $< $(NONE)
	@$(CPP) -o $@ -c $< $(CFLAGS)

$(OBJ_DIR)/%.o: %.c mp4muxer.h
	@echo $(GREEN) CC  $< $(NONE)
	@$(CC) -o $@ -c $< $(CFLAGS)

