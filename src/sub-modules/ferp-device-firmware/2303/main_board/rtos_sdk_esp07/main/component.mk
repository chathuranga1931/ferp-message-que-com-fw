#
# "main" pseudo-component makefile.
#
# (Uses default behaviour of compiling all source files in directory, adding 'include' to include path.)

CFLAGS += -D PROJECT_VER=\""$(CONFIG_APP_PROJECT_VER)"\" -D PROJECT_NAME=\""$(PROJECT_NAME)"\" -D PROJECT_TIME=__TIME__ -D PROJECT_DATE=__DATE__ 
