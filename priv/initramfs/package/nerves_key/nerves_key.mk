#############################################################
#
# nerves_key
#
#############################################################

NERVES_KEY_SITE = $(TOPDIR)/../package/nerves_key/src
NERVES_KEY_SITE_METHOD = local
NERVES_KEY_LICENSE = Apache-2.0
NERVES_KEY_LICENSE_FILES = LICENSE

ifdef BR2_PACKAGE_NERVES_KEY_DEBUG
NERVES_KEY_MAKE_OPTS = DEBUG=1
endif


define NERVES_KEY_BUILD_CMDS
	$(MAKE1) \
	    $(TARGET_CONFIGURE_OPTS) \
	    -C $(@D)
endef

define NERVES_KEY_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 755 $(@D)/nerves-key $(TARGET_DIR)/usr/bin/nerves-key
endef

$(eval $(generic-package))