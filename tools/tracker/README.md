# Pitch tracker test bench

`synth.py` writes synthetic plucked-guitar test files (scale runs picked and legato, a bend, palm mutes, power chords, high notes) with a known pitch timeline.
`yin_eval.cpp` runs the harmonizer's pitch detector (`adel/adelshifter_dsp.hpp`) over a 24-bit stereo WAV and logs what it reports every 256 samples.
`score.py` compares those logs with the truth: frames within 50 cents, octave errors, onset latency, number of reported note changes and of notes shorter than 50 ms.

    python3 synth.py synth/
    g++ -std=c++17 -O2 -I../../adel yin_eval.cpp -o yin_eval
    for f in synth/*.wav; do ./yin_eval $f 0 1 70 -55 > logs/new_$(basename $f .wav).txt; done
    python3 score.py synth logs new

The synthetic signals are cleaner than a real guitar: use them to compare two versions of the tracker, not to promise an accuracy.