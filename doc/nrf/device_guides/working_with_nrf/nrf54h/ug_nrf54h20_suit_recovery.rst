:orphan:

.. _ug_nrf54h20_suit_recovery:

Recovery for devices using SUIT
###############################

.. contents::
   :local:
   :depth: 2

In various scenarios the firmware on the device can get corrupted - either due to influence of unforseen hardware
issues, bugs in the code or attacks. The device should be able to recover from such scenarios.


Overview of the recovery mechanism in SUIT for nRF54H
*****************************************************

If booting of any of the SUIT manifests in the device fails, the Secure Domain enters the recovery mode.
In this mode it is verified, if a special application recovery manifest is configured in the devices Manifest Provisioning Information (MPI).
If it is the device boots the recovery firmware using this manifest.

It is recommended to use the Nordic provided recovery firmware found in  :file:`nrf/samples/suit/recovery`.
See the readme for this sample to see how to configure and use it.
