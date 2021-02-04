# audioSampleTeensy

Platform IO project to sample audio from 4 microphones on a Teensy 4.1

Interupt service routine samples 4 channels from an MCP3008 and stuff into a circular buffer. Then when enough data is in the buffer, it is written in raw bytes to an SD card.

A python script is used to reconstruct the data in post processing.

Recently refactored and currently untested.
