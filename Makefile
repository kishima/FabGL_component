#LDFLAGS = 
LIBS    = 
SDKPATH = $(COMPONENT_PATH)/../arduino/tools/sdk/include
COREPATH = $(COMPONENT_PATH)/../arduino/cores

INCLUDE = -I$(SDKPATH)/esp32 -I$(SDKPATH)/driver -I$(SDKPATH)/soc -I$(SDKPATH)/freertos -I$(PROJECT_PATH)/build/include -I$(SDKPATH)/log -I$(SDKPATH)/ulp -I$(SDKPATH)/esp_adc_cal -I$(SDKPATH)/heap -I$(COREPATH)/esp32 -I$(COMPONENT_PATH)/../arduino/variants/esp32
SRC_DIR = $(COMPONENT_PATH)/src
OBJ_DIR = $(COMPONENT_PATH)/build
SOURCES = $(shell ls $(SRC_DIR)/*.cpp) 
OBJS    = $(subst $(SRC_DIR),$(OBJ_DIR), $(SOURCES:.cpp=.o))
TARGET  = libfabgl.a
DEPENDS = $(OBJS:.o=.d)

all: $(TARGET)

$(TARGET): $(OBJS) $(LIBS)
	$(AR) r $(OBJ_DIR)/$@ $(OBJS) $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp 
	$(CC) $(CPPFLAGS) $(INCLUDE) -o $@ -c $<
	chmod 664 $@

clean:
	$(RM) $(OBJS) $(TARGET) $(DEPENDS)

-include $(DEPENDS)

.PHONY: all clean
