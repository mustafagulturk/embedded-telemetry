*** Comments ***
Renode end-to-end tests for the STM32F446RE telemetry firmware.

Run (from the repository root):
    renode-test -r renode/tests/results renode/tests/telemetry.robot
    renode-test -r renode/tests/results --variable ELF:/abs/path/firmware.elf renode/tests/telemetry.robot

USART2 carries ONLY binary frames (docs/packet_format.md), and the UART terminal tester of
Renode 1.15.3 is line/text based (no binary mode, no "Wait For Bytes On Uart"). Therefore every
test connects USART2 to a server-socket terminal on ${UART_PORT} and the Python library
telemetry_frames.py reads and decodes the frames (sync A5 5A, ver 1, CRC-16/CCITT-FALSE).

Assumptions about the firmware (all verified against firmware.elf of 2026-10-26):
  * type 0x01 telemetry 32-byte payload at ~100 Hz, type 0x02 stats 18-byte payload at 1 Hz,
    one common seq counter, mode byte at payload offset 30, faults at 31 (bit0 IMU, bit1 BARO).
  * After MPU6050 stops ACKing, the firmware goes to DEGRADED (1) or FAILSAFE (2) within ~2 s
    and sets faults bit0; stats.i2c_errors increases.
  * IWDG is started by the firmware and refreshed; a healthy run produces no watchdog reset.

*** Settings ***
Library           telemetry_frames.py
Suite Setup       Setup
Suite Teardown    Teardown
Test Setup        Reset Emulation And Load Firmware
Test Teardown     Test Teardown And Disconnect
Resource          ${RENODEKEYWORDS}

*** Variables ***
${ELF}            ${CURDIR}/../../build/fw/firmware/firmware.elf
${RESC}           ${CURDIR}/../telemetry.resc
${UART_PORT}      34560

*** Keywords ***
Reset Emulation And Load Firmware
    Test Setup
    Execute Command           $elf=@${ELF}
    Execute Command           $uart_port=${UART_PORT}
    Execute Script            ${RESC}
    # deterministic sensors for tests: no flight, fixed values (individual tests may change them)
    Execute Command           sysbus.i2c1.bmp280 FlightProfile false
    Execute Command           sysbus.i2c1.bmp280 Pressure 101325
    Connect To UART Socket    ${UART_PORT}
    Clear Frames

Test Teardown And Disconnect
    Disconnect From UART Socket
    Test Teardown

Run Virtual Seconds
    [Arguments]    ${seconds}
    # run in 250 ms slices and drain the socket between them so the TCP buffer never fills up
    ${slices}=    Evaluate    int(float(${seconds}) * 4)
    FOR    ${i}    IN RANGE    ${slices}
        Execute Command       emulation RunFor "0.25"
        Drain UART Socket
    END

Monitor Value Should Be
    [Arguments]    ${command}    ${expected}
    ${out}=    Execute Command    ${command}
    ${val}=    Evaluate    int("""${out}""".strip(), 0)
    Should Be Equal As Integers    ${val}    ${expected}

*** Test Cases ***
Firmware Should Send Valid Telemetry Frames
    [Documentation]    (a) liveness: ~100 Hz telemetry + 1 Hz stats, valid CRC, contiguous seq, NORMAL mode
    Run Virtual Seconds    3
    ${n}=    Telemetry Frame Count Should Be At Least    250
    Stats Frame Count Should Be At Least    2
    CRC Errors Should Be Zero
    Sequence Numbers Should Be Contiguous
    All Telemetry Modes Should Be    0
    ${az}=    Last Telemetry Field    az
    Should Be True    900 < ${az} < 1100    msg=az=${az} mg, expected ~1000 (MPU6050 AccelZ=1.0 g)
    ${p}=    Last Telemetry Field    pressure
    Should Be True    101300 < ${p} < 101350    msg=pressure=${p} Pa, expected ~101325
    ${t}=    Last Telemetry Field    temp
    Should Be True    2240 <= ${t} <= 2260    msg=temp=${t}, expected 2250 (BMP280 Temperature=22.5)
    ${drops}=    Last Stats Field    queue_drops
    Should Be Equal As Integers    ${drops}    0    msg=queue_drops=${drops}: no sample/frame may be dropped in a healthy run
    ${desc}=    Describe Last Frames    3
    Log    ${desc}    console=yes

Barometric Altitude Should Follow The Flight Profile
    [Documentation]    BMP280 virtual-time flight profile: 2 s ground, 10 s climb to 120 m
    Execute Command           sysbus.i2c1.bmp280 FlightProfile true
    Run Virtual Seconds    13
    ${alt}=    Last Telemetry Field    alt_filt
    Should Be True    10000 < ${alt} < 13000    msg=alt_filt=${alt} cm, expected ~12000 at t=13 s

MPU6050 Disconnect Should Degrade The Mode
    [Documentation]    (b) fault injection: MPU6050 NACKs its address -> DEGRADED (1) or FAILSAFE (2)
    Run Virtual Seconds    1.5
    All Telemetry Modes Should Be    0
    ${err0}=    Last Stats Field    i2c_errors
    Execute Command           sysbus.i2c1.mpu6050 Disconnected true
    Clear Frames
    Run Virtual Seconds    2
    Telemetry Mode Should Become One Of    1    2
    ${faults}=    Last Telemetry Field    faults
    Should Be True    ${faults} & 0x01    msg=faults=${faults}: IMU bit (bit0) not set
    ${err1}=    Last Stats Field    i2c_errors
    Should Be True    ${err1} > ${err0}    msg=i2c_errors did not increase (${err0} -> ${err1})
    ${desc}=    Describe Last Frames    3
    Log    ${desc}    console=yes
    # Recovery behaviour is firmware policy: only logged, not asserted.
    Execute Command           sysbus.i2c1.mpu6050 Disconnected false
    Run Virtual Seconds    2
    ${mode}=    Last Telemetry Mode
    Log    mode after reconnect: ${mode}    console=yes

BMP280 Disconnect Should Set The Baro Fault
    [Documentation]    (b) fault injection on the barometer
    Run Virtual Seconds    1.5
    Execute Command           sysbus.i2c1.bmp280 Disconnected true
    Clear Frames
    Run Virtual Seconds    2
    Telemetry Mode Should Become One Of    1    2
    ${faults}=    Last Telemetry Field    faults
    Should Be True    ${faults} & 0x02    msg=faults=${faults}: BARO bit (bit1) not set

Watchdog Should Not Reset The Firmware In A Normal Run
    [Documentation]    (c) 5 virtual seconds without an IWDG reset: IWDG started, ResetCount 0,
    ...                t_ms monotonic (a reset would restart it), reset cause = power-on
    Run Virtual Seconds    5
    ${running}=    Execute Command    sysbus.iwdg Running
    Should Contain    ${running}    True    msg=firmware did not start the IWDG
    Monitor Value Should Be    sysbus.iwdg ResetCount    0
    Monitor Value Should Be    sysbus.rcc WatchdogResetCount    0
    Telemetry Time Should Be Monotonic
    Telemetry Frame Count Should Be At Least    450
    ${cause}=    Last Stats Field    reset_cause
    Should Be Equal As Integers    ${cause}    0
