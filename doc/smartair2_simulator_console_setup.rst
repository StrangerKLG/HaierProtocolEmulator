SmartAir2/v1 simulator bench setup
===================================

AI authorship notice
--------------------

Made by AI, without human authorship.

This simulator extension and this deployment guide were prepared by AI, at the
operator's request, without manual human authorship of the implementation or
documentation text. Human involvement was limited to setting the task,
connecting hardware, testing behavior, and reporting expected results.

Full step-by-step documentation is available in two languages:

- `SmartAir2/v1 AC bench simulator guide (English) <smartair2_simulator_bench_en.rst>`_
- `SmartAir2/v1 AC bench simulator guide (Russian) <smartair2_simulator_bench_ru.rst>`_

Overview
--------

This fork turns the original ``smartair2_simulator`` into a full bench
simulator for Haier SmartAir2/v1 modules such as Lytko. The simulator behaves
like an indoor AC unit connected over UART:

- answers SmartAir2/v1 status requests from the module;
- accepts module commands for power, mode, target temperature, fan, and swing;
- simulates room temperature movement instead of reporting a fixed value;
- exposes a web console for demo control and monitoring;
- writes a JSON status snapshot and JSONL event log for external tools.

The current recommended protocol for the bench is Haier v1 / SmartAir2. The
hOn/v2 simulator can remain in the tree, but it is not the primary demo path.

Bench architecture
------------------

Typical deployment:

.. code-block:: text

  Lytko / Haier module UART
          |
       USB-UART
          |
  Linux host or VM
          |
  smartair2_simulator  <->  runtime files  <->  web console

The simulator process owns the serial device. The web console does not talk to
UART directly; it reads/writes runtime files:

- ``smartair2-control.env``: requested/default simulator state;
- ``smartair2-status.json``: live status snapshot;
- ``smartair2-events.jsonl``: module and simulator events.

Requirements
------------

Hardware:

- Linux machine or VM with a USB-UART adapter connected to the Haier/Lytko
  module UART;
- serial parameters expected by the module, normally ``9600 8N1``;
- access to the serial device, usually ``/dev/ttyUSB0`` or ``/dev/ttyACM0``.

Software:

- CMake;
- C++ compiler;
- ``make``;
- Python 3 for the web console;
- systemd if you want permanent services.

On Debian/Ubuntu:

.. code-block:: sh

  sudo apt update
  sudo apt install -y git cmake build-essential python3

Checkout
--------

Clone your fork and enter the repository:

.. code-block:: sh

  git clone https://github.com/YOUR-ORG/HaierProtocol.git
  cd HaierProtocol

Build
-----

Build the SmartAir2/v1 simulator:

.. code-block:: sh

  cd tools/smartair2_simulator
  mkdir -p build
  cd build
  cmake ..
  make -j"$(nproc)"

The resulting binary should be:

.. code-block:: text

  tools/smartair2_simulator/build/smartair2_simulator

Runtime directory
-----------------

Create a runtime directory. The examples below use ``/opt/haier-sim/runtime``;
replace it if your deployment uses another path.

.. code-block:: sh

  sudo mkdir -p /opt/haier-sim/runtime
  sudo chown -R "$USER":"$USER" /opt/haier-sim

Create the initial control file:

.. code-block:: ini

  power=1
  mode=cool
  fan_mode=auto
  target_temperature=16
  room_temperature=20
  humidity=45
  room_drift_temperature=28
  dynamics=1
  seconds_to_target=60
  hold_seconds=20
  drift_delta=1
  drift_seconds=40
  hysteresis=0.7
  passive_seconds_per_degree=70
  swing_mode=off
  turbo_mode=0
  quiet_mode=0
  display_status=0

Control keys
------------

``power``
  ``1`` for on, ``0`` for off.

``mode``
  ``auto``, ``cool``, ``heat``, ``fan``, or ``dry``.

``fan_mode``
  ``auto``, ``low``, ``medium``, or ``high``.

``target_temperature``
  AC setpoint reported to and accepted from the module.

``room_temperature``
  Current simulated room temperature. The simulator updates it while running.

``room_drift_temperature``
  Passive room target. In cooling demos set it above the AC target, for example
  ``28``. In heating demos set it below the AC target, for example ``16``. This
  makes the room warm up or cool down after the AC is stopped.

``dynamics``
  Enables the room model when set to ``1``.

``seconds_to_target``
  Approximate time for active cooling/heating to move the room toward the AC
  target.

``hold_seconds``
  Time to hold the target before passive drift is allowed.

``hysteresis``
  Temperature gap that starts active cooling/heating again.

``passive_seconds_per_degree``
  Passive drift speed toward ``room_drift_temperature``.

``swing_mode``
  ``off``, ``vertical``, ``horizontal``, or ``both``.

``turbo_mode``, ``quiet_mode``, ``display_status``
  Boolean flags reported in the simulated AC state.

Run manually
------------

For a first smoke test, stop any other process that may use the serial device
and run:

.. code-block:: sh

  SMARTAIR2_SIM_CONTROL_FILE=/opt/haier-sim/runtime/smartair2-control.env \
  SMARTAIR2_SIM_STATUS_FILE=/opt/haier-sim/runtime/smartair2-status.json \
  SMARTAIR2_SIM_EVENT_LOG=/opt/haier-sim/runtime/smartair2-events.jsonl \
  ./tools/smartair2_simulator/build/smartair2_simulator /dev/ttyUSB0

If the module is polling, the simulator should start printing protocol activity
and ``smartair2-status.json`` should appear.

Install the web console
-----------------------

The web console script is dependency-free Python 3. Place it somewhere stable,
for example:

.. code-block:: sh

  sudo mkdir -p /opt/haier-sim/console
  sudo cp console/haier_v1_console.py /opt/haier-sim/console/
  sudo chown -R "$USER":"$USER" /opt/haier-sim/console

Run it manually:

.. code-block:: sh

  HAIER_SIM_CONSOLE_PORT=18081 \
  python3 /opt/haier-sim/console/haier_v1_console.py

Open:

.. code-block:: text

  http://HOST-IP:18081/

Console endpoints:

- ``/``: browser UI;
- ``/api/status``: service, serial, control, and simulator status;
- ``/api/events``: recent JSONL events;
- ``/api/log``: recent systemd log when running as a service;
- ``POST /api/control``: update runtime control values.

Systemd service for the simulator
---------------------------------

Create ``/etc/systemd/system/haier-smartair2-simulator.service``:

.. code-block:: ini

  [Unit]
  Description=Haier SmartAir2/v1 AC simulator
  After=network-online.target
  ConditionPathExists=/dev/ttyUSB0

  [Service]
  Type=simple
  User=haier
  Group=dialout
  WorkingDirectory=/opt/haier-sim
  Environment=AC_SERIAL_PORT=/dev/ttyUSB0
  Environment=SMARTAIR2_SIM_CONTROL_FILE=/opt/haier-sim/runtime/smartair2-control.env
  Environment=SMARTAIR2_SIM_STATUS_FILE=/opt/haier-sim/runtime/smartair2-status.json
  Environment=SMARTAIR2_SIM_EVENT_LOG=/opt/haier-sim/runtime/smartair2-events.jsonl
  ExecStart=/opt/haier-sim/HaierProtocol/tools/smartair2_simulator/build/smartair2_simulator ${AC_SERIAL_PORT}
  Restart=on-failure
  RestartSec=3

  [Install]
  WantedBy=multi-user.target

Adjust ``User``, ``WorkingDirectory``, ``ExecStart``, and serial path to match
your installation. Make sure the service user is in the ``dialout`` group or has
equivalent permission to open the serial device.

Enable it:

.. code-block:: sh

  sudo systemctl daemon-reload
  sudo systemctl enable --now haier-smartair2-simulator.service
  systemctl status haier-smartair2-simulator.service

Systemd service for the web console
-----------------------------------

Create ``/etc/systemd/system/haier-sim-console.service``:

.. code-block:: ini

  [Unit]
  Description=Haier SmartAir2/v1 simulator web console
  After=network-online.target haier-smartair2-simulator.service

  [Service]
  Type=simple
  User=haier
  WorkingDirectory=/opt/haier-sim
  Environment=HAIER_SIM_CONSOLE_PORT=18081
  ExecStart=/usr/bin/python3 /opt/haier-sim/console/haier_v1_console.py
  Restart=on-failure
  RestartSec=3

  [Install]
  WantedBy=multi-user.target

Enable it:

.. code-block:: sh

  sudo systemctl daemon-reload
  sudo systemctl enable --now haier-sim-console.service
  systemctl status haier-sim-console.service

Verification checklist
----------------------

1. Serial device exists:

   .. code-block:: sh

     ls -l /dev/ttyUSB0

2. Simulator service is active:

   .. code-block:: sh

     systemctl is-active haier-smartair2-simulator.service

3. Console service is active:

   .. code-block:: sh

     systemctl is-active haier-sim-console.service

4. Status API returns JSON:

   .. code-block:: sh

     curl http://127.0.0.1:18081/api/status

5. Browser UI shows ``protocol: haier-v1-smartair2`` and active service.

6. Changing target/mode/fan/swing in the module creates entries in
   ``smartair2-events.jsonl``.

7. Changing values in the console affects the state reported back to the module.

Demo scenarios
--------------

Cooling demo:

.. code-block:: ini

  power=1
  mode=cool
  target_temperature=16
  room_temperature=20
  room_drift_temperature=28
  dynamics=1

The room cools toward 16 C. If the module turns cooling off, the room drifts
back toward 28 C and the module can react again.

Heating demo:

.. code-block:: ini

  power=1
  mode=auto
  target_temperature=28
  room_temperature=24
  room_drift_temperature=18
  dynamics=1

In ``auto`` mode the simulator heats when target is above current temperature
and cools when target is below current temperature. The reported mode remains
``auto`` while the internal phase shows ``heating`` or ``cooling``.

Swing/fan demo:

Use the module or the console to switch:

- ``fan_mode``: ``auto`` / ``low`` / ``medium`` / ``high``;
- ``swing_mode``: ``off`` / ``vertical`` / ``horizontal`` / ``both``.

The event log should show module-originated changes and the status JSON should
show the decoded state.

Troubleshooting
---------------

``Permission denied`` on serial device
  Add the service user to ``dialout`` or adjust udev permissions.

``ConditionPathExists=/dev/ttyUSB0`` prevents start
  The adapter path is different or the adapter is not attached. Check
  ``dmesg`` and ``ls /dev/serial/by-id``.

Console opens but service is inactive
  Check ``systemctl status haier-smartair2-simulator.service`` and serial
  ownership. The console can run even when the simulator service is stopped.

Module does not react
  Confirm that the module firmware uses Haier v1 / SmartAir2. hOn/v2 frames
  include CRC and should be tested with the hOn simulator instead.

Browser shows stale UI
  Refresh with cache bypass. The console sends no-store headers, but mobile
  browsers can still keep an old page.

Publishing notes for GitHub
---------------------------

Before publishing the fork:

- keep this AI authorship notice in the README or release notes;
- remove host-specific IP addresses, private usernames, and local paths unless
  they are clearly marked as examples;
- include the web console script and runtime-control C++ files in the commit;
- include systemd units as examples, not as mandatory files for every platform;
- document that the project is a bench/demo simulator, not a safety-certified
  HVAC controller.
