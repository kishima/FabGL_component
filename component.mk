SDKPATH = $(COMPONENT_PATH)/../arduino/tools/sdk/include
COREPATH = $(COMPONENT_PATH)/../arduino/cores

COMPONENT_EXTRA_INCLUDES := $(SDKPATH)/esp32 $(SDKPATH)/driver $(SDKPATH)/soc $(SDKPATH)/freertos $(PROJECT_PATH)/build/include $(SDKPATH)/log $(SDKPATH)/ulp $(SDKPATH)/esp_adc_cal $(SDKPATH)/heap $(COREPATH)/esp32 $(COMPONENT_PATH)/../arduino/variants/esp32 $(COMPONENT_PATH)/src

COMPONENT_SRCDIRS := src src/comdrivers src/devdrivers src/dispdrivers src/comdrivers

CPPFLAGS += -std=gnu++11 -fno-rtti
