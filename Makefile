.SUFFIXES:

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

APP_TITLE    := Hyrule Bridge Ultra
APP_AUTHOR   := LINKO
APP_VERSION  := 0.2.2
TARGET       := HyruleBridgeUltra
BUILD        := build
SOURCES      := source
INCLUDES     := source include
NO_ICON      := 1

include ${TOPDIR}/lib/libultrahand/ultrahand.mk

ARCH := -march=armv8-a+simd+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS := -g -Wall -O2 -ffunction-sections -fdata-sections -flto \
          -fuse-linker-plugin -fomit-frame-pointer -fno-strict-aliasing \
          $(ARCH) $(DEFINES)
CFLAGS += $(INCLUDE) -D__SWITCH__ -DAPP_VERSION="\"$(APP_VERSION)\"" -D_FORTIFY_SOURCE=2
CXXFLAGS := $(CFLAGS) -std=gnu++20 -fno-exceptions -fno-rtti
ASFLAGS := $(ARCH)
LDFLAGS := -specs=$(DEVKITPRO)/libnx/switch.specs $(ARCH) -Wl,-Map,$(notdir $*.map) -Wl,--gc-sections
LIBS := -lcurl -lz -lminizip -lmbedtls -lmbedx509 -lmbedcrypto -lnx
LIBDIRS := $(PORTLIBS) $(LIBNX)

ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT := $(CURDIR)/$(TARGET)
export TOPDIR := $(CURDIR)
export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
CFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
SFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
export OFILES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

ifeq ($(strip $(CPPFILES)),)
export LD := $(CC)
else
export LD := $(CXX)
endif

.PHONY: all clean dist $(BUILD)
all: $(BUILD)
$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
	@mkdir -p out/switch/.overlays
	@cp $(TARGET).ovl out/switch/.overlays/$(TARGET).ovl
clean:
	@rm -rf $(BUILD) $(TARGET).ovl $(TARGET).nro $(TARGET).nacp $(TARGET).elf out $(TARGET).zip

dist: all
	@cd out && zip -r ../$(TARGET).zip .
else
DEPENDS := $(OFILES:.o=.d)
.PHONY: all
all: $(OUTPUT).ovl
$(OUTPUT).ovl: $(OUTPUT).elf $(OUTPUT).nacp
	@elf2nro $< $@ $(NROFLAGS)
	@printf 'ULTR' >> $@
	@echo "built $(notdir $@) with Ultrahand signature"
$(OUTPUT).elf: $(OFILES)
-include $(DEPENDS)
endif
