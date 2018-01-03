define Device/rb-nor-flash-16M
  DEVICE_TITLE := MikroTik RouterBoard (16 MB SPI NOR)
  DEVICE_PACKAGES := rbcfg rssileds -nand-utils kmod-ledtrig-gpio
  IMAGE_SIZE := 16000k
  LOADER_TYPE := elf
  KERNEL_INSTALL := 1
  KERNEL := kernel-bin | lzma | loader-kernel
  IMAGE/sysupgrade.bin := append-kernel | kernel2minor -s 1024 -e | pad-to $$$$(BLOCKSIZE) | \
	append-rootfs | pad-rootfs | check-size $$$$(IMAGE_SIZE)
endef
TARGET_DEVICES += rb-nor-flash-16M
