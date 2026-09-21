.. _gpio_port_latch_demo:

GPIO port latch demo
####################

Overview
********

This sample demonstrates GPIO PORT/LATCH behavior when three buttons on the same GPIO port are monitored with level-low interrupts.

When button 0 is pressed on nRF54LS05 DK, the Zephyr GPIO driver may deliver callbacks for sibling pins on the same port even though only one button was pressed physically.

Requirements
************

* nRF54LS05 DK or nRF54L15 DK
* Debugger (for inspecting sample state variables)

Building and running
********************

Build for nRF54LS05 DK:

.. code-block:: console

   west build -b nrf54ls05dk/nrf54ls05b/cpuapp nrf/samples/peripheral/gpio_port_latch_demo
   west flash

Build for nRF54L15 DK (comparison platform):

.. code-block:: console

   west build -b nrf54l15dk/nrf54l15/cpuapp nrf/samples/peripheral/gpio_port_latch_demo
   west flash

Test procedure
**************

1. Flash the sample and attach a debugger.
2. Press **button 0** once.
3. Inspect the debugger-visible variables listed below.

Debugger variables
==================

``isr_count[]``
  Per-button callback counter (index 0 = button 0, and so on).

``isr_pins_log[]`` / ``isr_level_log[]`` / ``isr_log_idx``
  Callback sequence: pin bitmask passed to the ISR and logical button level at callback time.

Expected result on nRF54LS05 DK
===============================

When pressing button 0 only, spurious sibling callbacks may appear:

.. code-block:: none

   isr_count[0] = 1
   isr_count[1] = 1
   isr_count[2] = 1
   isr_log_idx = 3
   isr_pins_log[0] = 0x00000100   (P1.8)
   isr_pins_log[1] = 0x00000200   (P1.9)
   isr_pins_log[2] = 0x00002000   (P1.13)
   isr_level_log[0] = 0
   isr_level_log[1] = 0
   isr_level_log[2] = 1

On nRF54L15 DK, typically only button 0 registers callbacks when only button 0 is pressed.

Sample behavior
***************

* Configures buttons 0, 1 and 2 (GPIO port 1) as inputs with pull-up.
* Arms ``GPIO_INT_LEVEL_LOW`` on all three pins (same as CAF buttons with inverted polarity).
* Uses one GPIO callback per port with a combined pin mask.
* In the ISR: records the callback, disables the interrupt for that pin immediately.
* Console and logging are disabled to keep interrupt timing release-like.
