
COMPONENT_OWNBUILDTARGET := true
COMPONENT_OWNCLEANTARGET := true

COMPONENT_ADD_INCLUDEDIRS := 

CPPFLAGS += -std=gnu++11 -fno-rtti

build: 
	mkdir -p $(COMPONENT_PATH)/build
	cd $(COMPONENT_PATH) && $(MAKE)
	cp $(COMPONENT_PATH)/build/libfabgl.a $(COMPONENT_LIBRARY)

clean: 
	cd $(COMPONENT_PATH) && $(MAKE) clean

