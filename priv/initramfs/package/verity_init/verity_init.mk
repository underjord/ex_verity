#############################################################
#
# verity_init
#
#############################################################

VERITY_INIT_SITE = $(TOPDIR)/../package/verity_init/src
VERITY_INIT_SITE_METHOD = local
VERITY_INIT_LICENSE = Apache-2.0
VERITY_INIT_LICENSE_FILES = LICENSE

ifdef BR2_PACKAGE_VERITY_INIT_DEBUG
VERITY_INIT_MAKE_OPTS = DEBUG=1
endif


define VERITY_INIT_BUILD_CMDS
	$(MAKE1) \
	    $(TARGET_CONFIGURE_OPTS) \
	    -C $(@D)
endef

define VERITY_INIT_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 755 $(@D)/verity-init $(TARGET_DIR)/init
endef

$(eval $(generic-package))
