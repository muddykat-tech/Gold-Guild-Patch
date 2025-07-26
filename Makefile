CC = g++
CC_C = gcc
RC = windres
DLL_CFLAGS = -O0 -g -std=c++11 ${WARNS} -Iinclude -DADD_EXPORTS -fpermissive -m32
DLL_LDFLAGS = -m32 -shared -static-libgcc -static-libstdc++ -s -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive -Wl,--subsystem,windows,--out-implib,lib/lib.a
FW_LDFLAGS = -m32 -shared -static-libgcc -static-libstdc++ -s -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive -Wl,--subsystem,windows,--out-implib,lib/patch.a

NTERNALS_OBJ = obj/patch.o

INTERNALS_SRC = src

# Colors
YELLOW_BASH=\033[1;33m
NC_BASH=\033[0m # No Color for Bash

YELLOW_PS=Yellow
NC_PS=Default

# Function to print colored text
define print_colored
	$(if $(findstring bash,$(SHELL)),\
		@echo -e "$(YELLOW_BASH)$(1)$(NC_BASH)",\
		$(if $(findstring cmd.exe,$(ComSpec)),\
			@powershell -NoProfile -Command "Write-Host -ForegroundColor $(YELLOW_PS) '$(1)'",\
			@powershell -NoProfile -Command "Write-Host -ForegroundColor $(YELLOW_PS) '$(1)'"))
endef

all: bin/patch.asi
clean:
	del /Q /F obj\*.o bin\*.asi lib\*.a 2>nul || exit 0

bin lib obj:
	@if not exist "$@" mkdir "$@"

bin/patch.asi: $(NTERNALS_OBJ) | bin lib
	${CC} -o "$@" ${NTERNALS_OBJ} ${FW_LDFLAGS}

obj/patch.o: ${INTERNALS_SRC}/patch.cpp | obj
	$(call print_colored, "================== Starting Build ==================")
	${CC} -mgeneral-regs-only ${DLL_CFLAGS} -c "$<" -o "$@"
