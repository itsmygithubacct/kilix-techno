# Built-in synthesis bank

Kilix Techno's version-one bank is compiled from original C tables and
`chip-sequencer` instrument parameters. It provides Circuit Kick, Wire Snare,
Arc Clap, Closed Alloy, Open Alloy, Node Perc, Sub Trace, Vector Stab, and
Static Bloom roles. These are oscillator, noise, envelope, and wavetable
definitions; the application contains no sampled audio.

The project track `role` byte selects one of these stable roles. Gate variants
are generated deterministically during scene compilation, while note,
velocity, probability, ratchet, track mix, and master processing remain
project controls.
