#---------------------------------------------------------------------------------
# SNES9x PS5 Edition — PS3 homebrew (PSL1GHT)
#---------------------------------------------------------------------------------
ifeq ($(strip $(PSL1GHT)),)
$(error "Please set PSL1GHT in your environment. export PSL1GHT=<path>")
endif

TITLE		:=	SNES9x PS5 Edition
APPID		:=	SNES95000
CONTENTID	:=	UP0001-$(APPID)_00-0000000000000000
ICON0		:=	$(CURDIR)/pkgfiles/ICON0.PNG
SFOXML		:=	$(CURDIR)/sfo.xml
PKGFILES	:=	$(CURDIR)/build/pkgroot

include $(PSL1GHT)/ppu_rules

TARGET		:=	$(notdir $(CURDIR))
BUILD		:=	build
SOURCES		:=	source
INCLUDES	:=	source $(PORTLIBS)/include/freetype2

# plat_host.c is the PC preview backend; it compiles to nothing on PS3
# the emulator core is built by tools/ci/build-core.sh ps3
CFLAGS		=	-O2 -Wall -mcpu=cell -std=gnu99 -D__PS3__ $(MACHDEP) $(INCLUDE)
CXXFLAGS	=	$(CFLAGS)
LDFLAGS		=	$(MACHDEP) -Wl,-Map,$(notdir $@).map

LIBS		:=	-lsnes9x2010 -ltiny3d -lfreetype -ltheoradec -logg -lz -laudio -lgcm_sys -lrsx -lsysutil -lio -lsysmodule -lsysfs -lrt -llv2 -lm

LIBDIRS		:=	$(PORTLIBS) $(CURDIR)/build-core/ps3

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export BUILDDIR	:=	$(CURDIR)/$(BUILD)
export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
export LD	:=	$(CC)
export OFILES	:=	$(CFILES:.c=.o)
export INCLUDE	:=	$(foreach dir,$(INCLUDES), -I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			$(LIBPSL1GHT_INC) -I$(CURDIR)/$(BUILD) -I$(PORTLIBS)/include/freetype2
export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib) $(LIBPSL1GHT_LIB)

.PHONY: $(BUILD) clean pkg pkgroot

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

# stage the files that go into the package next to EBOOT.BIN
pkgroot:
	@rm -rf $(PKGFILES)
	@mkdir -p $(PKGFILES)/USRDIR/roms $(PKGFILES)/USRDIR/art/boxart $(PKGFILES)/USRDIR/art/snap $(PKGFILES)/USRDIR/art/video
	@cp pkgfiles/PIC1.PNG $(PKGFILES)/
	@cp -r assets/fonts assets/icons $(PKGFILES)/USRDIR/
	@cp assets/README-roms.txt $(PKGFILES)/USRDIR/roms/README.txt
	@cp assets/README-art.txt $(PKGFILES)/USRDIR/art/README.txt
	@cp -r assets/licenses $(PKGFILES)/USRDIR/

pkg: $(BUILD) pkgroot
	@rm -f $(OUTPUT).pkg $(OUTPUT).gnpdrm.pkg
	@$(MAKE) --no-print-directory $(OUTPUT).pkg

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(OUTPUT).elf $(OUTPUT).self $(OUTPUT).fake.self $(OUTPUT)*.pkg

else

DEPENDS	:=	$(OFILES:.o=.d)

$(OUTPUT).self: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

endif
