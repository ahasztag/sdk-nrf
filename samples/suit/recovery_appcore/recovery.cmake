include(ExternalProject)

set(sysbuild_root_path "${ZEPHYR_NRF_MODULE_DIR}/../zephyr/share/sysbuild")
set(board_target "${SB_CONFIG_BOARD}/${target_soc}/cpuapp")

# Get class and vendor ID-s to pass to recovery application
sysbuild_get(APP_RECOVERY_VENDOR_NAME IMAGE ${DEFAULT_IMAGE} VAR CONFIG_SUIT_MPI_APP_RECOVERY_VENDOR_NAME KCONFIG)
sysbuild_get(APP_RECOVERY_CLASS_NAME IMAGE ${DEFAULT_IMAGE} VAR CONFIG_SUIT_MPI_APP_RECOVERY_CLASS_NAME KCONFIG)

ExternalProject_Add(
recovery
SOURCE_DIR ${sysbuild_root_path}
PREFIX ${CMAKE_BINARY_DIR}/recovery_appcore
INSTALL_COMMAND ""
CMAKE_CACHE_ARGS
  "-DAPP_DIR:PATH=${SB_CONFIG_SUIT_BUILD_RECOVERY_FIRMWARE_PATH}"
  "-DBOARD:STRING=${board_target}"
  "-DEXTRA_DTC_OVERLAY_FILE:STRING=${APP_DIR}/sysbuild/recovery.overlay"
  "-DCONFIG_SUIT_MPI_APP_RECOVERY_VENDOR_NAME:STRING=\\\"${APP_RECOVERY_VENDOR_NAME}\\\""
  "-DCONFIG_SUIT_MPI_APP_RECOVERY_CLASS_NAME:STRING=\\\"${APP_RECOVERY_CLASS_NAME}\\\""
BUILD_ALWAYS True
)
ExternalProject_Get_property(recovery BINARY_DIR)

set_property(
GLOBAL APPEND PROPERTY SUIT_RECOVERY_ARTIFACTS_TO_MERGE_application
${BINARY_DIR}/recovery_appcore/zephyr/suit_installed_envelopes_application_merged.hex)
set_property(
GLOBAL APPEND PROPERTY SUIT_RECOVERY_ARTIFACTS_TO_MERGE_application ${BINARY_DIR}/recovery_appcore/zephyr/zephyr.hex)

