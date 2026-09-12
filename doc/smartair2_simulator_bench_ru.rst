Стендовый эмулятор кондиционера SmartAir2/v1: полная инструкция
===============================================================

Пометка об авторстве
--------------------

Сделано ИИ, без участия человека.

Расширение эмулятора SmartAir2/v1 и эта инструкция подготовлены ИИ по задаче
оператора. Участие человека ограничивалось постановкой задачи, подключением
железа, проверкой поведения стенда и сообщением ожидаемых результатов.

Что добавлено в fork
--------------------

В этом fork добавлен стендовый эмулятор для модулей Haier SmartAir2/v1,
например Lytko. Linux-компьютер или виртуальная машина может притворяться
внутренним блоком кондиционера Haier по UART.

Добавлено:

- файл управления состоянием эмулятора;
- live JSON со статусом;
- JSONL-журнал событий;
- модель изменения комнатной температуры;
- веб-консоль в браузере;
- примеры systemd-сервисов;
- подробная инструкция по установке, настройке и проверке.

Стенд полезен, когда рядом нет настоящего внутреннего блока Haier, но нужно,
чтобы внешний модуль увидел «живой» кондиционер.

Как это устроено
----------------

.. code-block:: text

  Модуль Lytko / Haier
      TX/RX/GND
        |
    USB-UART адаптер
        |
  Linux-компьютер или VM
        |
  smartair2_simulator
        |
  runtime-директория
        |
  Python web console

Симулятор занимает последовательный порт. Веб-консоль напрямую к UART не
обращается, она читает и меняет файлы в runtime-директории:

- ``smartair2-control.env`` — желаемое состояние эмулятора;
- ``smartair2-status.json`` — текущий live-статус от эмулятора;
- ``smartair2-events.jsonl`` — журнал событий.

Что нужно из железа
-------------------

Понадобится:

- Linux-компьютер, мини-ПК, Raspberry Pi или VM с USB passthrough;
- USB-UART адаптер: CH340, CP2102, FT232, PL2303 или похожий;
- внешний модуль Haier/Lytko;
- три провода: ``GND``, ``TX``, ``RX``;
- отдельное питание для модуля, если он не питается от своей платы.

Подключение:

- ``GND`` адаптера -> ``GND`` модуля;
- ``TXD`` адаптера -> ``RX`` модуля;
- ``RXD`` адаптера -> ``TX`` модуля;
- питание ``5V`` или ``3V3`` с адаптера подключать только если точно известно,
  какое питание нужно модулю;
- если модуль работает с логикой 3.3 В, нельзя напрямую подавать на его ``RX``
  сигнал 5 В. Нужен level shifter или делитель напряжения.

Обычные параметры UART: ``9600 8N1``.

Установка зависимостей
----------------------

Для Debian/Ubuntu:

.. code-block:: sh

  sudo apt update
  sudo apt install -y git cmake build-essential python3 libncurses-dev

``libncurses-dev`` часто нужен для сборки симулятора на Debian-подобных
системах.

Клонирование и сборка
---------------------

.. code-block:: sh

  git clone https://github.com/StrangerKLG/HaierProtocolEmulator.git
  cd HaierProtocolEmulator
  cmake -S tools/smartair2_simulator -B tools/smartair2_simulator/build
  cmake --build tools/smartair2_simulator/build -j"$(nproc)"

Готовый бинарник появится здесь:

.. code-block:: text

  tools/smartair2_simulator/build/smartair2_simulator

Как найти USB-UART адаптер
--------------------------

Подключить адаптер и посмотреть, какое устройство появилось:

.. code-block:: sh

  dmesg | tail -40
  ls -l /dev/ttyUSB* /dev/ttyACM* 2>/dev/null

Чаще всего это ``/dev/ttyUSB0`` или ``/dev/ttyACM0``.

Если симулятор запускается от обычного пользователя, добавить пользователя в
группу ``dialout`` и перелогиниться:

.. code-block:: sh

  sudo usermod -aG dialout "$USER"

Создание runtime-директории
---------------------------

В примерах используется ``/opt/haier-sim``.

.. code-block:: sh

  sudo mkdir -p /opt/haier-sim/runtime
  sudo chown -R "$USER":"$USER" /opt/haier-sim

Создать стартовый файл управления:

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

Ручной запуск симулятора
------------------------

В примере используется ``/dev/ttyUSB0``. Если адаптер определился иначе,
заменить путь.

.. code-block:: sh

  SMARTAIR2_SIM_CONTROL_FILE=/opt/haier-sim/runtime/smartair2-control.env \
  SMARTAIR2_SIM_STATUS_FILE=/opt/haier-sim/runtime/smartair2-status.json \
  SMARTAIR2_SIM_EVENT_LOG=/opt/haier-sim/runtime/smartair2-events.jsonl \
  ./tools/smartair2_simulator/build/smartair2_simulator /dev/ttyUSB0

Если внешний модуль опрашивает шину, в консоли появится обмен пакетами, а в
runtime-директории появится ``smartair2-status.json``.

Ручной запуск веб-консоли
-------------------------

Веб-консоль — это один Python-файл без дополнительных Python-зависимостей.

.. code-block:: sh

  HAIER_SIM_RUNTIME_DIR=/opt/haier-sim/runtime \
  HAIER_SIM_SERIAL=/dev/ttyUSB0 \
  HAIER_SIM_CONSOLE_PORT=18081 \
  python3 tools/smartair2_simulator/console/haier_v1_console.py

Открыть в браузере:

.. code-block:: text

  http://IP-КОМПЬЮТЕРА:18081/

Например:

.. code-block:: text

  http://192.168.13.21:18081/

Что показывает веб-консоль
--------------------------

Веб-консоль показывает:

- состояние сервиса эмулятора;
- наличие serial-устройства;
- включение кондиционера;
- режим работы;
- режим вентилятора;
- режим жалюзи;
- состояние компрессора;
- текущую комнатную температуру;
- целевую температуру;
- цель пассивного дрейфа комнаты;
- текущую фазу модели комнаты;
- PID процесса эмулятора;
- последние события;
- последние строки systemd-лога;
- сырой JSON-статус.

Чем можно управлять
-------------------

``power``
  Включает или выключает эмулируемый кондиционер.

``mode``
  Режим: ``auto``, ``cool``, ``heat``, ``fan`` или ``dry``.

``fan_mode``
  Вентилятор: ``auto``, ``low``, ``medium`` или ``high``.

``swing_mode``
  Жалюзи: ``off``, ``vertical``, ``horizontal`` или ``both``.

``room_temperature``
  Текущая температура комнаты, которую эмулятор показывает модулю.

``target_temperature``
  Уставка кондиционера.

``room_drift_temperature``
  Температура, к которой комната стремится без активного охлаждения/нагрева.
  Для демо охлаждения ставить выше уставки, для демо нагрева — ниже уставки.

``dynamics``
  Включает модель изменения температуры.

``seconds_to_target``
  Примерное время, за которое активное охлаждение или нагрев дойдёт до уставки.

``hold_seconds``
  Сколько секунд держать температуру около уставки перед пассивным дрейфом.

``drift_delta``
  На сколько градусов отвести температуру после удержания, чтобы внешний модуль
  снова увидел разницу и начал реагировать.

``drift_seconds``
  Время активного ухода температуры после удержания.

``hysteresis``
  Зазор температуры, после которого снова запускается активное охлаждение или
  нагрев.

``passive_seconds_per_degree``
  Скорость естественного дрейфа комнаты, секунд на один градус.

``turbo_mode``, ``quiet_mode``, ``display_status``
  Флаги, которые отображаются в состоянии эмулируемого кондиционера.

Простой сценарий демонстрации
-----------------------------

Для демонстрации охлаждения:

1. Включить ``power``.
2. Поставить ``mode=cool``.
3. Поставить ``target_temperature=16``.
4. Поставить ``room_temperature=20``.
5. Поставить ``room_drift_temperature=28``.
6. Включить ``dynamics``.
7. Нажать Apply в веб-консоли.

Ожидаемое поведение:

1. Модуль видит кондиционер в режиме охлаждения.
2. Комнатная температура постепенно идёт от 20 C к 16 C.
3. Эмулятор держит температуру около цели ``hold_seconds`` секунд.
4. Затем температура снова уходит вверх.
5. Внешний модуль видит разницу и может снова отправлять команды.

Systemd-сервис эмулятора
------------------------

Создать отдельного пользователя:

.. code-block:: sh

  sudo useradd --system --home /opt/haier-sim --shell /usr/sbin/nologin --groups dialout haier
  sudo chown -R haier:dialout /opt/haier-sim

Скопировать или склонировать репозиторий сюда:

.. code-block:: text

  /opt/haier-sim/HaierProtocolEmulator

Создать файл ``/etc/systemd/system/haier-smartair2-simulator.service``:

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

Включить:

.. code-block:: sh

  sudo systemctl daemon-reload
  sudo systemctl enable --now haier-smartair2-simulator.service
  systemctl status haier-smartair2-simulator.service

Systemd-сервис веб-консоли
--------------------------

Создать файл ``/etc/systemd/system/haier-sim-console.service``:

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

Включить:

.. code-block:: sh

  sudo systemctl daemon-reload
  sudo systemctl enable --now haier-sim-console.service
  systemctl status haier-sim-console.service
  curl http://127.0.0.1:18081/api/status

Диагностика
-----------

Нет ``/dev/ttyUSB0``
  Посмотреть ``dmesg`` после подключения адаптера. Устройство может называться
  ``/dev/ttyUSB1`` или ``/dev/ttyACM0``.

Permission denied на serial-порт
  Добавить пользователя в группу ``dialout`` и перелогиниться или перезапустить
  сервис.

Веб-консоль открывается, но пишет, что эмулятор inactive
  Запустить или перезапустить ``haier-smartair2-simulator.service`` и посмотреть
  лог: ``journalctl -u haier-smartair2-simulator.service -n 100 --no-pager``.

Модуль не общается
  Проверить перекрёстное подключение TX/RX, общий GND, питание, уровень логики
  UART и правильный serial device.

Браузер показывает старую страницу
  Сделать жёсткое обновление страницы или открыть консоль в приватном окне.

Температура не меняется
  Проверить, что ``dynamics=1`` и текущая температура отличается от уставки или
  температуры дрейфа.

Короткий чек-лист проверки
--------------------------

- USB-UART адаптер виден в ``dmesg``;
- сборка симулятора завершилась без ошибок;
- сервис эмулятора active;
- сервис веб-консоли active;
- ``/api/status`` возвращает JSON;
- ``smartair2-status.json`` обновляется при подключённом модуле;
- из веб-консоли меняются температура, режим, вентилятор и жалюзи;
- внешний модуль реагирует на изменения состояния эмулятора.

