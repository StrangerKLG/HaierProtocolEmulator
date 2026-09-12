SmartAir2/v1 AC bench simulator: full guide
============================================

AI authorship notice
--------------------

Made by AI, without human authorship.

This SmartAir2/v1 simulator extension and this guide were prepared by AI at the
operator's request. Human involvement was limited to defining the task,
connecting the hardware, testing the bench, and reporting the expected
behaviour.

What this fork adds
-------------------

This fork adds a practical bench simulator for Haier SmartAir2/v1 modules such
as Lytko. It lets a Linux machine or VM behave like a Haier indoor AC unit on a
UART line.

Added features:

- runtime control file for the SmartAir2/v1 simulator;
- live status JSON file;
- JSONL event log;
- room temperature model;
- browser web console;
- example systemd services;
- installation and troubleshooting documentation.

The bench is useful when you do not have a real Haier indoor unit nearby, but
you need the external module to see a believable air conditioner.

Architecture
------------

.. code-block:: text

  Lytko / Haier module
      TX/RX/GND
        |
    USB-UART adapter
        |
  Linux host or VM
        |
  smartair2_simulator
        |
  runtime directory
        |
  Python web console

The simulator owns the serial port. The web console does not talk to UART
directly; it reads and writes files in the runtime directory:

- ``smartair2-control.env``: requested simulator state;
- ``smartair2-status.json``: current live state written by the simulator;
- ``smartair2-events.jsonl``: event log written by the simulator.

Hardware
--------

You need:

- a Linux PC, mini-PC, Raspberry Pi, or VM with USB passthrough;
- a USB-UART adapter such as CH340, CP2102, FT232, or PL2303;
- the external Haier/Lytko module;
- three wires for ``GND``, ``TX``, and ``RX``;
- optional separate power supply if the module is not powered elsewhere.

Wiring:

- adapter ``GND`` -> module ``GND``;
- adapter ``TXD`` -> module ``RX``;
- adapter ``RXD`` -> module ``TX``;
- do not connect ``5V`` or ``3V3`` from the adapter unless you know how the
  module must be powered;
- if the module uses 3.3 V logic, do not drive its ``RX`` pin from a 5 V UART
  without a level shifter or voltage divider.

Typical serial settings are ``9600 8N1``.

Install dependencies
--------------------

On Debian or Ubuntu:

.. code-block:: sh

  sudo apt update
  sudo apt install -y git cmake build-essential python3 libncurses-dev

``libncurses-dev`` is needed by the simulator build on many Debian-based
systems.

Clone and build
---------------

.. code-block:: sh

  git clone https://github.com/StrangerKLG/HaierProtocolEmulator.git
  cd HaierProtocolEmulator
  cmake -S tools/smartair2_simulator -B tools/smartair2_simulator/build
  cmake --build tools/smartair2_simulator/build -j"$(nproc)"

The binary will be created here:

.. code-block:: text

  tools/smartair2_simulator/build/smartair2_simulator

Find the serial adapter
-----------------------

Plug in the USB-UART adapter and check which device appeared:

.. code-block:: sh

  dmesg | tail -40
  ls -l /dev/ttyUSB* /dev/ttyACM* 2>/dev/null

Common device names are ``/dev/ttyUSB0`` and ``/dev/ttyACM0``.

If you run the simulator as a normal user, add that user to the ``dialout``
group and re-login:

.. code-block:: sh

  sudo usermod -aG dialout "$USER"

Create the runtime directory
----------------------------

The examples below use ``/opt/haier-sim``.

.. code-block:: sh

  sudo mkdir -p /opt/haier-sim/runtime
  sudo chown -R "$USER":"$USER" /opt/haier-sim

Create the first control file:

.. code-block:: sh

  cat > /opt/haier-sim/runtime/smartair2-control.env <<'EOF'
  power=1
  mode=cool
  fan_mode=auto
  target_temperature=16
  room_temperature=20
  humidity=45
  ambient_temperature=28
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
  EOF

Run manually
------------

Use the serial device found earlier. This example uses ``/dev/ttyUSB0``:

.. code-block:: sh

  SMARTAIR2_SIM_CONTROL_FILE=/opt/haier-sim/runtime/smartair2-control.env \
  SMARTAIR2_SIM_STATUS_FILE=/opt/haier-sim/runtime/smartair2-status.json \
  SMARTAIR2_SIM_EVENT_LOG=/opt/haier-sim/runtime/smartair2-events.jsonl \
  ./tools/smartair2_simulator/build/smartair2_simulator /dev/ttyUSB0

If the external module is polling the bus, the simulator should print exchange
activity and create ``smartair2-status.json``.

Run the web console manually
----------------------------

The console is a single Python 3 script and has no third-party Python
dependencies.

.. code-block:: sh

  HAIER_SIM_RUNTIME_DIR=/opt/haier-sim/runtime \
  HAIER_SIM_SERIAL=/dev/ttyUSB0 \
  HAIER_SIM_CONSOLE_PORT=18081 \
  python3 tools/smartair2_simulator/console/haier_v1_console.py

Open in a browser:

.. code-block:: text

  http://HOST-IP:18081/

For example:

.. code-block:: text

  http://192.168.13.21:18081/

Console controls
----------------

``power``
  Turns the simulated AC on or off.

``mode``
  One of ``auto``, ``cool``, ``heat``, ``fan``, or ``dry``.

``fan_mode``
  One of ``auto``, ``low``, ``medium``, or ``high``.

``swing_mode``
  One of ``off``, ``vertical``, ``horizontal``, or ``both``.

``room_temperature``
  The current simulated room temperature.

``target_temperature``
  The setpoint that the simulated AC reports and accepts.

``room_drift_temperature``
  The passive room target. For cooling demos set it above the setpoint. For
  heating demos set it below the setpoint.

``dynamics``
  Enables the room model.

``seconds_to_target``
  Approximate time for active heating or cooling to reach the target.

``hold_seconds``
  How long the simulator holds near the target before passive drift starts.

``drift_delta``
  How far beyond the target the passive drift should move before the next
  active cycle becomes visible to the external module.

``drift_seconds``
  Time used for the active post-hold drift step.

``hysteresis``
  Temperature gap before active cooling or heating starts again.

``passive_seconds_per_degree``
  Speed of natural room drift when the AC is idle.

``turbo_mode``, ``quiet_mode``, ``display_status``
  Boolean flags reported in the simulated AC state.

Recommended cooling demo
------------------------

1. Set ``power=1``.
2. Set ``mode=cool``.
3. Set ``target_temperature=16``.
4. Set ``room_temperature=20``.
5. Set ``room_drift_temperature=28``.
6. Set ``dynamics=1``.
7. Apply the settings in the web console.

Expected behaviour:

1. The module sees an AC in cooling mode.
2. The simulated room temperature moves from about 20 C toward 16 C.
3. The simulator holds near the target for ``hold_seconds``.
4. The room then drifts upward again.
5. The external module sees a real temperature gap and can issue commands again.

Systemd service for the simulator
---------------------------------

Create a dedicated user:

.. code-block:: sh

  sudo useradd --system --home /opt/haier-sim --shell /usr/sbin/nologin --groups dialout haier
  sudo chown -R haier:dialout /opt/haier-sim

Copy or clone the repository to:

.. code-block:: text

  /opt/haier-sim/HaierProtocolEmulator

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
  WorkingDirectory=/opt/haier-sim/HaierProtocolEmulator
  Environment=AC_SERIAL_PORT=/dev/ttyUSB0
  Environment=SMARTAIR2_SIM_CONTROL_FILE=/opt/haier-sim/runtime/smartair2-control.env
  Environment=SMARTAIR2_SIM_STATUS_FILE=/opt/haier-sim/runtime/smartair2-status.json
  Environment=SMARTAIR2_SIM_EVENT_LOG=/opt/haier-sim/runtime/smartair2-events.jsonl
  ExecStart=/opt/haier-sim/HaierProtocolEmulator/tools/smartair2_simulator/build/smartair2_simulator ${AC_SERIAL_PORT}
  Restart=on-failure
  RestartSec=3

  [Install]
  WantedBy=multi-user.target

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
  Group=dialout
  WorkingDirectory=/opt/haier-sim/HaierProtocolEmulator
  Environment=HAIER_SIM_CONSOLE_HOST=0.0.0.0
  Environment=HAIER_SIM_CONSOLE_PORT=18081
  Environment=HAIER_SIM_RUNTIME_DIR=/opt/haier-sim/runtime
  Environment=HAIER_SIM_SERIAL=/dev/ttyUSB0
  Environment=HAIER_SIM_SERVICE=haier-smartair2-simulator.service
  ExecStart=/usr/bin/python3 /opt/haier-sim/HaierProtocolEmulator/tools/smartair2_simulator/console/haier_v1_console.py
  Restart=on-failure
  RestartSec=3

  [Install]
  WantedBy=multi-user.target

Enable it:

.. code-block:: sh

  sudo systemctl daemon-reload
  sudo systemctl enable --now haier-sim-console.service
  systemctl status haier-sim-console.service
  curl http://127.0.0.1:18081/api/status

Troubleshooting
---------------

No ``/dev/ttyUSB0``
  Check ``dmesg``. The adapter may be ``/dev/ttyUSB1`` or ``/dev/ttyACM0``.

Permission denied on the serial device
  Add the user to ``dialout`` and re-login or restart the service.

The web console opens but says the simulator is inactive
  Start or restart ``haier-smartair2-simulator.service`` and check:
  ``journalctl -u haier-smartair2-simulator.service -n 100 --no-pager``.

The module does not communicate
  Check crossed TX/RX, shared GND, module power, UART voltage level, and serial
  device selection.

The browser shows an old page
  Hard-refresh the page or open it in a private browser window.

The temperature does not move
  Make sure ``dynamics=1`` and the current room temperature is different from
  the target or drift temperature.

Verification checklist
----------------------

- USB-UART adapter is visible in ``dmesg``;
- simulator build completed successfully;
- simulator service is active;
- web console service is active;
- ``/api/status`` returns JSON;
- ``smartair2-status.json`` is updated while the module is connected;
- web console can change target temperature, mode, fan, and swing;
- external module reacts to simulator state changes.

