#############################################################
#
# rpi_otp_key
#
#############################################################

RPI_OTP_KEY_SITE = $(TOPDIR)/../package/rpi_otp_key/src
RPI_OTP_KEY_SITE_METHOD = local

ifdef BR2_PACKAGE_RPI_OTP_KEY_DEBUG
RPI_OTP_KEY_MAKE_OPTS = DEBUG=1
endif


define RPI_OTP_KEY_BUILD_CMDS
	$(MAKE1) \
	    $(TARGET_CONFIGURE_OPTS) \
	    -C $(@D)
endef

define RPI_OTP_KEY_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 755 $(@D)/rpi-otp-key $(TARGET_DIR)/usr/bin/rpi-otp-key
endef

$(eval $(generic-package))
