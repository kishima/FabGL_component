
#COMPONENT_OWNBUILDTARGET := true
#COMPONENT_OWNCLEANTARGET := true

SDKPATH = $(COMPONENT_PATH)/../arduino/tools/sdk/include
COREPATH = $(COMPONENT_PATH)/../arduino/cores

COMPONENT_EXTRA_INCLUDES := $(SDKPATH)/esp32 $(SDKPATH)/driver $(SDKPATH)/soc $(SDKPATH)/freertos $(PROJECT_PATH)/build/include $(SDKPATH)/log $(SDKPATH)/ulp $(SDKPATH)/esp_adc_cal $(SDKPATH)/heap $(COREPATH)/esp32 $(COMPONENT_PATH)/../arduino/variants/esp32

COMPONENT_SRCDIRS := $(COMPONENT_PATH)/src

CPPFLAGS += -std=gnu++11 -fno-rtti

#build: 
	#mkdir -p $(COMPONENT_PATH)/build
	#cd $(COMPONENT_PATH) && $(MAKE)
	#cp $(COMPONENT_PATH)/build/libfabgl.a $(COMPONENT_LIBRARY)

#clean: 
#	#cd $(COMPONENT_PATH) && $(MAKE) clean

