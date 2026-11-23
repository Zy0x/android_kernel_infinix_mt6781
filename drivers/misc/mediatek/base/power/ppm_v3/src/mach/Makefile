ifneq (,$(findstring 3gear,$(PROJECT_DTB_NAMES)))
obj-y += mt6893/
else
ifeq ($(CONFIG_MACH_MT6893),y)
obj-y += mt6893/
else ifeq ($(CONFIG_MACH_MT6781),y)
obj-y += mt6781/
else
obj-y += $(subst ",,$(CONFIG_MTK_PLATFORM))/
endif
endif
