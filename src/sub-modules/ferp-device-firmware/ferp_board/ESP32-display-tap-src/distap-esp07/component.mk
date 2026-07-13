
CFLAGS += -D PROJECT_VER=\""$(CONFIG_APP_PROJECT_VER)"\" -D PROJECT_NAME=\""$(PROJECT_NAME)"\" -D PROJECT_TIME=__TIME__ -D PROJECT_DATE=__DATE__ 

ifndef MAKE_DOCS

exportbinfiles:
	$(Q) mkdir -p esp07_v$(CONFIG_APP_PROJECT_VER)
	$(Q) cp build/bootloader/bootloader.bin esp07_v$(CONFIG_APP_PROJECT_VER)/
	$(Q) cp build/partitions_table.bin esp07_v$(CONFIG_APP_PROJECT_VER)/
	$(Q) cp build/$(PROJECT_NAME).bin esp07_v$(CONFIG_APP_PROJECT_VER)/
	$(Q) echo $(PROJECT_NAME) V$(CONFIG_APP_PROJECT_VER)  > esp07_v$(CONFIG_APP_PROJECT_VER)/$(PROJECT_NAME).txt
	$(Q) cp -a esp07_v$(CONFIG_APP_PROJECT_VER)/* ../esp32/production/data/esp07/
endif