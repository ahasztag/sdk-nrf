
#
# Copyright (c) 2023 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

if(SB_CONFIG_PARTITION_MANAGER)
	# Make mcuboot a build only target as the main application will flash this from the
	# merged hex file
	set_target_properties(mcuboot PROPERTIES BUILD_ONLY true)

	if(SB_CONFIG_PM_EXTERNAL_FLASH_MCUBOOT_SECONDARY)
		set_config_bool(mcuboot CONFIG_PM_EXTERNAL_FLASH_MCUBOOT_SECONDARY y)
	else()
		set_config_bool(mcuboot CONFIG_PM_EXTERNAL_FLASH_MCUBOOT_SECONDARY n)
	endif()

	if(DEFINED board_qualifiers_secure)
		# Apply configuration for MCUboot using all available RAM or not
		if(SB_CONFIG_MCUBOOT_USE_ALL_AVAILABLE_RAM)
			set_config_bool(mcuboot CONFIG_MCUBOOT_USE_ALL_AVAILABLE_RAM y)
		else()
			set_config_bool(mcuboot CONFIG_MCUBOOT_USE_ALL_AVAILABLE_RAM n)
		endif()
	endif()
endif()

math(EXPR mcuboot_total_images "${SB_CONFIG_MCUBOOT_UPDATEABLE_IMAGES} + ${SB_CONFIG_MCUBOOT_ADDITIONAL_UPDATEABLE_IMAGES}")
set_config_int(mcuboot CONFIG_UPDATEABLE_IMAGE_NUMBER ${mcuboot_total_images})

if(SB_CONFIG_BOOT_SIGNATURE_TYPE_PURE)
	set_config_bool(mcuboot CONFIG_BOOT_SIGNATURE_TYPE_PURE y)
endif()

if(SB_CONFIG_BOOT_IMG_HASH_ALG_SHA512 AND NOT (SB_CONFIG_MCUBOOT_SIGNATURE_USING_KMU AND SB_CONFIG_BOOT_SIGNATURE_TYPE_PURE))
	set_config_bool(mcuboot CONFIG_BOOT_IMG_HASH_ALG_SHA512 y)
endif()

# The NRF54LX goes with PSA crypto by default
if(SB_CONFIG_SOC_SERIES_NRF54LX)
	if(SB_CONFIG_BOOT_SIGNATURE_TYPE_NONE)
		set_config_bool(mcuboot CONFIG_NRF_SECURITY y)
	elseif(SB_CONFIG_BOOT_SIGNATURE_TYPE_ED25519)
		set_config_bool(mcuboot CONFIG_NRF_SECURITY y)

		# We are sure that ED25519 signature on MCUboot does not need these
		set_config_bool(mcuboot CONFIG_PSA_USE_CRACEN_AEAD_DRIVER n)
		set_config_bool(mcuboot CONFIG_PSA_USE_CRACEN_PAKE_DRIVER n)
		if(SB_CONFIG_BOOT_ENCRYPTION)
			set_config_bool(mcuboot CONFIG_PSA_USE_CRACEN_CIPHER_DRIVER y)
			set_config_bool(mcuboot CONFIG_PSA_USE_CRACEN_MAC_DRIVER y)
			set_config_bool(mcuboot CONFIG_PSA_USE_CRACEN_KEY_AGREEMENT_DRIVER y)
			set_config_bool(mcuboot CONFIG_PSA_USE_CRACEN_KEY_DERIVATION_DRIVER y)
		else()
			set_config_bool(mcuboot CONFIG_PSA_USE_CRACEN_CIPHER_DRIVER n)
			set_config_bool(mcuboot CONFIG_PSA_USE_CRACEN_MAC_DRIVER n)
			set_config_bool(mcuboot CONFIG_PSA_USE_CRACEN_KEY_AGREEMENT_DRIVER n)
			set_config_bool(mcuboot CONFIG_PSA_USE_CRACEN_KEY_DERIVATION_DRIVER n)
		endif()

		if(SB_CONFIG_MCUBOOT_SIGNATURE_USING_KMU)
			set_config_bool(mcuboot CONFIG_BOOT_SIGNATURE_USING_KMU y)

			if(SB_CONFIG_MCUBOOT_SIGNATURE_KMU_UROT_MAPPING)
				set_config_bool(mcuboot CONFIG_NCS_BOOT_SIGNATURE_KMU_UROT_MAPPING y)
			else()
				set_config_bool(mcuboot CONFIG_NCS_BOOT_SIGNATURE_KMU_UROT_MAPPING n)
			endif()
		else()
			set_config_bool(mcuboot CONFIG_BOOT_SIGNATURE_USING_KMU n)
		endif()

		# MCUboot uses hash function to identify key internally when KMU is disabled.
		if(SB_CONFIG_MCUBOOT_SIGNATURE_USING_KMU AND SB_CONFIG_BOOT_SIGNATURE_TYPE_PURE)
			set_config_bool(mcuboot CONFIG_PSA_USE_CRACEN_HASH_DRIVER n)
		else()
			set_config_bool(mcuboot CONFIG_PSA_USE_CRACEN_HASH_DRIVER y)
		endif()
	endif()
endif()

if(SB_CONFIG_SOC_SERIES_NRF54HX)
	if(SB_CONFIG_MCUBOOT_SIGNATURE_USING_ITS)
		set_config_bool(mcuboot CONFIG_NCS_BOOT_SIGNATURE_USING_ITS y)
	else()
		set_config_bool(mcuboot CONFIG_NCS_BOOT_SIGNATURE_USING_ITS n)
	endif()
endif()

# A v1 board doesn't define board qualifiers, thus below test will just test the pure board
# name for a v1 board. A v2 board will match against the board qualifier.
if("${BOARD}${BOARD_QUALIFIERS}" MATCHES "(_|/)ns$")
	# Configure MCUboot before application so that TF-M can read MCUboot configuration
	sysbuild_add_dependencies(CONFIGURE ${DEFAULT_IMAGE} mcuboot)

	# Configure MCUBoot to be able to boot TFM
	add_overlay_config(
		mcuboot
		${ZEPHYR_NRF_MODULE_DIR}/modules/mcuboot/tfm.conf
		)

	# Add fault injection protection to MCUBOOT
	set_config_bool(mcuboot CONFIG_BOOT_FIH_PROFILE_DEFAULT_LOW y)
endif()

if(SB_CONFIG_PARTITION_MANAGER OR SB_CONFIG_MCUBOOT_MODE_DIRECT_XIP OR SB_CONFIG_MCUBOOT_MODE_DIRECT_XIP_WITH_REVERT OR SB_CONFIG_MCUBOOT_COMPRESSED_IMAGE_SUPPORT)
	if(SB_CONFIG_QSPI_XIP_SPLIT_IMAGE)
		# Flash drivers are mandatory with QSPI XIP split image support
		set_config_bool(mcuboot CONFIG_FLASH y)
		set_config_bool(mcuboot CONFIG_MULTITHREADING y)
		set_config_bool(mcuboot CONFIG_MCUBOOT_VERIFY_IMG_ADDRESS n)

		if(NOT DEFINED SB_CONFIG_PM_OVERRIDE_EXTERNAL_DRIVER_CHECK)
			# If external driver check Kconfig is enabled then users will need to select their own
			# Kconfigs (to allow for forked/derivative QSPI NOR drivers), otherwise force enable the
			# driver for the default image and MCUboot
			set_config_bool(mcuboot CONFIG_NORDIC_QSPI_NOR y)
			set_config_bool(mcuboot CONFIG_NORDIC_QSPI_NOR_XIP y)
		endif()
	endif()

	if(SB_CONFIG_PM_OVERRIDE_EXTERNAL_DRIVER_CHECK)
		add_overlay_config(mcuboot ${ZEPHYR_NRF_MODULE_DIR}/subsys/bootloader/bl_override/override_external_mcuboot.conf)
	endif()

	if(SB_CONFIG_PM_EXTERNAL_FLASH_MCUBOOT_SECONDARY)
		add_overlay_config(mcuboot ${ZEPHYR_NRF_MODULE_DIR}/subsys/partition_manager/ext_flash_mcuboot_secondary.conf)
	endif()

	if(SB_CONFIG_SECURE_BOOT_APPCORE)
		# Get the s0/s1 MCUboot update package version and split it up into the fields so it can
		# be supplied to the MCUboot image
		string(REPLACE "." ";" s0_s1_package_version ${SB_CONFIG_SECURE_BOOT_MCUBOOT_VERSION})
		string(REPLACE "+" ";" s0_s1_package_version "${s0_s1_package_version}")

		list(GET s0_s1_package_version 0 s0_s1_package_version_major)
		list(GET s0_s1_package_version 1 s0_s1_package_version_minor)
		list(GET s0_s1_package_version 2 s0_s1_package_version_revision)
		list(GET s0_s1_package_version 3 s0_s1_package_version_build_number)

		set_config_int(mcuboot CONFIG_MCUBOOT_MCUBOOT_S0_S1_VERSION_MAJOR ${s0_s1_package_version_major})
		set_config_int(mcuboot CONFIG_MCUBOOT_MCUBOOT_S0_S1_VERSION_MINOR ${s0_s1_package_version_minor})
		set_config_int(mcuboot CONFIG_MCUBOOT_MCUBOOT_S0_S1_VERSION_REVISION ${s0_s1_package_version_revision})
		set_config_int(mcuboot CONFIG_MCUBOOT_MCUBOOT_S0_S1_VERSION_BUILD_NUMBER ${s0_s1_package_version_build_number})

		set(s0_s1_package_version)
		set(s0_s1_package_version_major)
		set(s0_s1_package_version_minor)
		set(s0_s1_package_version_revision)
		set(s0_s1_package_version_build_number)
	else()
		set_config_int(mcuboot CONFIG_MCUBOOT_MCUBOOT_S0_S1_VERSION_MAJOR -1)
		set_config_int(mcuboot CONFIG_MCUBOOT_MCUBOOT_S0_S1_VERSION_MINOR -1)
		set_config_int(mcuboot CONFIG_MCUBOOT_MCUBOOT_S0_S1_VERSION_REVISION -1)
		set_config_int(mcuboot CONFIG_MCUBOOT_MCUBOOT_S0_S1_VERSION_BUILD_NUMBER -1)
	endif()
endif()

if(SB_CONFIG_MCUBOOT_HARDWARE_DOWNGRADE_PREVENTION)
	set_config_bool(mcuboot CONFIG_MCUBOOT_HW_DOWNGRADE_PREVENTION y)
	set_config_bool(mcuboot CONFIG_SECURE_BOOT_STORAGE y)
	set_config_bool(mcuboot CONFIG_SECURE_BOOT_CRYPTO y)
else()
	set_config_bool(mcuboot CONFIG_MCUBOOT_HW_DOWNGRADE_PREVENTION n)
endif()

if(SB_CONFIG_MCUBOOT_NRF53_MULTI_IMAGE_UPDATE)
	set_config_bool(mcuboot CONFIG_NRF53_MULTI_IMAGE_UPDATE y)
	set_config_bool(mcuboot CONFIG_BOOT_IMAGE_ACCESS_HOOKS y)
	set_config_bool(mcuboot CONFIG_FLASH_SIMULATOR y)
	set_config_bool(mcuboot CONFIG_FLASH_SIMULATOR_DOUBLE_WRITES y)
	set_config_bool(mcuboot CONFIG_FLASH_SIMULATOR_STATS n)

	if(NOT SB_CONFIG_MCUBOOT_MODE_OVERWRITE_ONLY)
		set_config_bool(mcuboot CONFIG_USE_NRF53_MULTI_IMAGE_WITHOUT_UPGRADE_ONLY y)
	endif()
endif()


if(NOT DEFINED mcuboot_BOARD AND DEFINED board_target_secure)
	# MCUboot must run in secure mode on the nRF9160/nRF5340
	set_target_properties(mcuboot PROPERTIES BOARD ${board_target_secure})
endif()

# Apply image compression support options
if(SB_CONFIG_MCUBOOT_COMPRESSED_IMAGE_SUPPORT)
	set_config_bool(mcuboot CONFIG_NRF_COMPRESS y)
	set_config_bool(mcuboot CONFIG_NRF_COMPRESS_DECOMPRESSION y)
	set_config_bool(mcuboot CONFIG_NRF_COMPRESS_LZMA y)
	set_config_bool(mcuboot CONFIG_NRF_COMPRESS_LZMA_VERSION_LZMA2 y)
	set_config_bool(mcuboot CONFIG_NRF_COMPRESS_ARM_THUMB y)
	set_config_bool(mcuboot CONFIG_NRF_COMPRESS_CLEANUP y)
	set_config_bool(mcuboot CONFIG_BOOT_DECOMPRESSION y)
else()
	set_config_bool(mcuboot CONFIG_BOOT_DECOMPRESSION n)
endif()

if(SB_CONFIG_SECURE_BOOT_APPCORE)
	if(NOT DEFINED b0_BOARD AND DEFINED board_target_secure)
		# mcuboot must run in secure mode on the nRF9160/nRF5340
		set_target_properties(s1_image PROPERTIES BOARD ${board_target_secure})
	endif()

	add_overlay_config(mcuboot ${ZEPHYR_NRF_MODULE_DIR}/subsys/bootloader/image/log_minimal.conf)
	set_config_bool(mcuboot CONFIG_SECURE_BOOT y)
	set_config_bool(mcuboot CONFIG_FW_INFO y)

	if(SB_CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256)
		if(SB_CONFIG_BOOT_SHARED_CRYPTO_ECDSA_P256)
			add_overlay_config(
				mcuboot
				${ZEPHYR_MCUBOOT_MODULE_DIR}/boot/zephyr/external_crypto.conf
				)
		endif()
	endif()

	if((NOT SB_CONFIG_SECURE_BOOT_SIGNATURE_TYPE_ECDSA AND SB_CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256) OR (SB_CONFIG_SECURE_BOOT_SIGNATURE_TYPE_ECDSA AND NOT SB_CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256))
		message(WARNING "MCUboot and secure boot (application core) are enabled but MCUboot signing key type is not set to ECDSA-P256, this is a non-optimal configuration if sharing of crypto functions is needed from b0 to MCUboot")
	endif()
endif()

if(SB_CONFIG_SECURE_BOOT_NETCORE)
	if(SB_CONFIG_NETCORE_APP_UPDATE)
		set_config_bool(mcuboot CONFIG_PCD_APP y)

		add_overlay_dts(
			mcuboot
			${ZEPHYR_NRF_MODULE_DIR}/modules/mcuboot/flash_sim.overlay
			)
		if(SB_CONFIG_SECURE_BOOT_BUILD_S1_VARIANT_IMAGE)
			add_overlay_dts(
				s1_image
				${ZEPHYR_NRF_MODULE_DIR}/modules/mcuboot/flash_sim.overlay
				)
		endif()
	else()
		set_config_bool(mcuboot CONFIG_PCD_APP n)
	endif()
endif()

if(SB_CONFIG_SUPPORT_NETCORE AND NOT SB_CONFIG_SECURE_BOOT_NETCORE)
	# Disable PCD if there is no secure boot enabled for the network core
	set_config_bool(mcuboot CONFIG_PCD_APP n)
endif()

if(SB_CONFIG_CRACEN_MICROCODE_LOAD_ONCE)
	if(SB_CONFIG_CRACEN_MICROCODE_LOAD_B0)
		set_config_bool(mcuboot CONFIG_CRACEN_LOAD_MICROCODE n)
	elseif(SB_CONFIG_CRACEN_MICROCODE_LOAD_MCUBOOT)
		set_config_bool(mcuboot CONFIG_CRACEN_LOAD_MICROCODE y)
	endif()
endif()
