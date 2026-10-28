#!/bin/bash
# Host tests (no hardware needed): pedalboard document, effects, ADEL engines, math. Run from anywhere; the repository must sit at circle/addon/wlan/sample/pisound-wifi-test
# (the Circle headers are found with ../../../../include). Compiles every test with g++ and prints one result line per test.
cd "$(dirname "$0")/.." || exit 1
INC="-Itests/host_shim -I../../../../include -Isrc -Isrc/adel -I."
SRC="src/board.cpp src/graph.cpp src/fx.cpp src/fx_comp.cpp src/fx_delay.cpp src/fx_verb.cpp src/fx_phase.cpp src/fx_sweep.cpp src/fx_spring.cpp src/fx_chorus.cpp src/fx_cab.cpp src/fx_equal.cpp src/fx_delaym.cpp src/fx_verbm.cpp src/fx_shifter.cpp src/fx_extra.cpp src/fx_extra2.cpp src/fx_tube.cpp src/fx_rat.cpp src/fx_la2a.cpp src/fx_dimd.cpp src/fx_muff.cpp src/fx_fuzzface.cpp src/fx_rangemaster.cpp src/fx_ds1.cpp src/fx_guvnor.cpp src/fx_klon.cpp src/fx_bender.cpp src/fx_wah.cpp src/fx_tonestack.cpp src/fx_dynacomp.cpp src/fx_phase90.cpp src/fx_ce2.cpp src/fx_factory.cpp src/fx_ce1.cpp src/fx_ad9.cpp src/fx_mflanger.cpp src/midi.cpp src/fx_univibe.cpp src/fx_ocd.cpp src/fx_octaver.cpp src/fx_flanger117.cpp"
echo "== test_board"; g++ -std=c++17 -O1 $INC tests/test_board.cpp $SRC -o /tmp/test_board 2>&1 | grep -E 'error' | head -5; /tmp/test_board | tail -2
echo "== test_fx (effects, presets, moves, ADEL)"; g++ -std=c++17 -O1 $INC tests/test_fx.cpp $SRC -o /tmp/test_fx 2>&1 | grep -E 'error' | head -5; /tmp/test_fx | grep -E 'ADEL|RESULT|FAILED|error'
echo "== test_fxmath"; g++ -std=c++17 -O2 -iquote src/fxshim tests/test_fxmath.cpp src/fxmath.cpp -o /tmp/test_fxmath 2>&1 | grep error; /tmp/test_fxmath | tail -2
echo "== test_net (network mode)"; g++ -std=c++17 -O1 $INC tests/test_net.cpp $SRC -o /tmp/test_net 2>&1 | grep -E 'error' | head -5; /tmp/test_net | tail -12
echo "== test_extra (20 ToTest effects)"; g++ -std=c++17 -O1 $INC tests/test_extra.cpp $SRC -o /tmp/test_extra 2>&1 | grep -E "error" | head -8; /tmp/test_extra | tail -40
echo "== test_midi_board (MIDI in the document)"; g++ -std=c++17 -O1 $INC tests/test_midi_board.cpp $SRC -o /tmp/test_midi_board 2>&1 | grep -E "error" | head -8; /tmp/test_midi_board | tail -20
echo "== test_dhcp (fallback network DHCP server)"; g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined $INC tests/test_dhcp.cpp src/dhcpcore.cpp -o /tmp/test_dhcp 2>&1 | grep -E "error" | head -5; ASAN_OPTIONS=detect_leaks=0 /tmp/test_dhcp | tail -3
echo "== test_serialrpc (line protocol with the ESP32 display)"; g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Itests/host_shim -I../../../../include -Isrc tests/test_serialrpc.cpp src/serialrpc.cpp -o /tmp/test_serialrpc 2>&1 | grep -E "error" | head -5; ASAN_OPTIONS=detect_leaks=0 /tmp/test_serialrpc | tail -3
echo "== test_newpreset (the display builds a simple preset: the command sequence on the board model)"; g++ -std=c++17 -O1 $INC tests/test_newpreset.cpp $SRC -o /tmp/test_newpreset 2>&1 | grep -E "error" | head -5; /tmp/test_newpreset | tail -2
echo "== test_din (Pisound DIN socket)"; g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Itests/host_shim -I../../../../include -Isrc tests/test_din.cpp src/pisounddin.cpp src/midi.cpp -o /tmp/test_din 2>&1 | grep -E "error" | head -5; ASAN_OPTIONS=detect_leaks=0 /tmp/test_din | tail -3
echo "== test_namtone (NAM Bass/Mid/Treble)"; g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined $INC tests/test_namtone.cpp $SRC -o /tmp/test_namtone 2>&1 | grep -E "error" | head -5; ASAN_OPTIONS=detect_leaks=0 /tmp/test_namtone | tail -3
echo "== test_ftools (10 functions with a switch)"; g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined $INC tests/test_ftools.cpp src/ftools.cpp -o /tmp/test_ftools 2>&1 | grep -E "error" | head -5; ASAN_OPTIONS=detect_leaks=0 /tmp/test_ftools | tail -3
echo "== test_fancurve (fan curve)"; g++ -std=c++17 -O1 -Itests/host_shim -I../../../../include -Isrc tests/test_fancurve.cpp -o /tmp/test_fancurve 2>&1 | grep -E "error" | head -5; /tmp/test_fancurve | tail -2
echo "== test_namcal (NAM input calibration)"; g++ -std=c++17 -O1 -Isrc tests/test_namcal.cpp -o /tmp/test_namcal 2>&1 | grep -E "error" | head -5; /tmp/test_namcal | tail -2
echo "== test_ghost (uninterrupted preset change: tails, fades, slots)"; g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined $INC tests/test_ghost.cpp $SRC -o /tmp/test_ghost 2>&1 | grep -E "error" | head -5; ASAN_OPTIONS=detect_leaks=0 /tmp/test_ghost | tail -3
echo "== test_fxmath_race (exp/pow cache with several colliding threads)"; g++ -std=c++17 -O2 -pthread -iquote src/fxshim tests/test_fxmath_race.cpp src/fxmath.cpp -o /tmp/test_fxmath_race 2>&1 | grep -E "error" | head -5; /tmp/test_fxmath_race | tail -3
echo "== test_tube (Tube Screamer: response against the exact circuit, controls, models, clipping)"; g++ -std=c++17 -O1 -Itests/host_shim -I../../../../include -Isrc -Isrc/adel tests/test_tube.cpp $SRC -o /tmp/test_tube 2>&1 | grep -E "error" | head -5; /tmp/test_tube | tail -2
echo "== test_rat (RAT: anchors from the real captures, controls, variants, stability)"; g++ -std=c++17 -O1 $INC tests/test_rat.cpp $SRC -o /tmp/test_rat 2>&1 | grep -E "error" | head -5; RAT_OS=1 /tmp/test_rat | grep -E "FAILED|RESULT"
echo "== test_la2a (Optical LA Real: threshold, order of the controls, attack/release, link, stability)"; g++ -std=c++17 -O1 $INC tests/test_la2a.cpp $SRC -o /tmp/test_la2a 2>&1 | grep -E "error" | head -5; /tmp/test_la2a | grep -E "FAILED|RESULT"

echo "== test_dimd (Dimension D Real: delays per mode, antiphase, crossing, controls, stability)"; g++ -std=c++17 -O1 $INC tests/test_dimd.cpp $SRC -o /tmp/test_dimd 2>&1 | grep -E "error" | head -5; /tmp/test_dimd | grep -E "FAILED|all ok|TESTS"

echo "== test_tonestack"; g++ -std=c++17 -O1 $INC tests/test_tonestack.cpp $SRC -o /tmp/test_tonestack 2>&1 | grep -E "error" | head -5; /tmp/test_tonestack | grep -E "FAILED|RESULT|all ok|TESTS"

echo "== test_dynacomp"; g++ -std=c++17 -O1 $INC tests/test_dynacomp.cpp $SRC -o /tmp/test_dynacomp 2>&1 | grep -E "error" | head -5; /tmp/test_dynacomp | grep -E "FAILED|RESULT|all ok|TESTS"

echo "== test_phase90"; g++ -std=c++17 -O1 $INC tests/test_phase90.cpp $SRC -o /tmp/test_phase90 2>&1 | grep -E "error" | head -5; /tmp/test_phase90 | grep -E "FAILED|RESULT|all ok|TESTS"

echo "== test_ce2"; g++ -std=c++17 -O1 $INC tests/test_ce2.cpp $SRC -o /tmp/test_ce2 2>&1 | grep -E "error" | head -5; /tmp/test_ce2 | grep -E "FAILED|RESULT|all ok|TESTS"

echo "== test_ce1"; g++ -std=c++17 -O1 $INC tests/test_ce1.cpp $SRC -o /tmp/test_ce1 2>&1 | grep -E "error" | head -5; /tmp/test_ce1 | grep -E "FAILED|RESULT|all ok|TESTS"

echo "== test_factory"; g++ -std=c++17 -O1 $INC tests/test_factory.cpp $SRC -o /tmp/test_factory 2>&1 | grep -E "error" | head -5; /tmp/test_factory | grep -E "FAILED|RESULT|all ok|TESTS"

echo "== test_muff"; g++ -std=c++17 -O1 $INC tests/test_muff.cpp $SRC -o /tmp/test_muff 2>&1 | grep -E "error" | head -5; /tmp/test_muff | grep -E "FAILED|RESULT|all ok|TESTS"

echo "== test_ad9"; g++ -std=c++17 -O1 $INC tests/test_ad9.cpp $SRC -o /tmp/test_ad9 2>&1 | grep -E "error" | head -5; /tmp/test_ad9 | grep -E "FAILED|RESULT|all ok|TESTS"

echo "== test_mflanger"; g++ -std=c++17 -O1 $INC tests/test_mflanger.cpp $SRC -o /tmp/test_mflanger 2>&1 | grep -E "error" | head -5; /tmp/test_mflanger | grep -E "FAILED|RESULT|all ok|TESTS"
echo "== test_univibe"; g++ -std=c++17 -O2 $INC tests/test_univibe.cpp $SRC -o /tmp/test_univibe 2>&1 | grep -E "error" | head -5; /tmp/test_univibe | grep -E "FAILED|RESULT|all ok|TESTS"
echo "== test_ocd"; g++ -std=c++17 -O2 $INC tests/test_ocd.cpp $SRC -o /tmp/test_ocd 2>&1 | grep -E "error" | head -5; /tmp/test_ocd | grep -E "FAILED|RESULT|all ok|TESTS"
echo "== test_octaver"; g++ -std=c++17 -O2 $INC tests/test_octaver.cpp $SRC -o /tmp/test_octaver 2>&1 | grep -E "error" | head -5; /tmp/test_octaver | grep -E "FAILED|RESULT"
echo "== test_flanger117"; g++ -std=c++17 -O2 $INC tests/test_flanger117.cpp $SRC -o /tmp/test_flanger117 2>&1 | grep -E "error" | head -5; /tmp/test_flanger117 | grep -E "FAILED|RESULT"
echo "== test_midi (MIDI core)"; g++ -std=c++17 -O1 -g -Wall -fsanitize=address,undefined -fno-sanitize-recover=undefined -Itests/host_shim -I../../../../include -Isrc tests/test_midi.cpp src/midi.cpp -o /tmp/test_midi 2>&1 | grep -E 'error|warning' | head -10; ASAN_OPTIONS=detect_leaks=0 /tmp/test_midi 2>&1 | tail -3
echo "== test_phase_rot (ADELphase: phase rotation)"; g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Isrc/adel tests/test_phase_rot.cpp -o /tmp/test_phase_rot 2>&1 | grep -E 'error' | head -8; ASAN_OPTIONS=detect_leaks=0 /tmp/test_phase_rot 2>&1 | tail -3