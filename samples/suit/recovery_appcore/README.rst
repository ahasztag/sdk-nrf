.. _suit_recovery:

SUIT: Recovery application - application core only
##########################

.. contents::
   :local:
   :depth: 2

Building
=======

Build the main application (e. g. smp_transfer sample) with:

``west build -b nrf54h20dk/nrf54h20/cpuapp --  -DSB_CONFIG_SUIT_BUILD_RECOVERY=y -DSB_CONFIG_SUIT_BUILD_RECOVERY_FIRMWARE_PATH="\"<PATH_TO_NCS>/nrf/samples/suit/recovery_appcore\""``