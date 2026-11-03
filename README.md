# TCI CW/RTTY

A stand-alone CW decoder and RTTY modem that connects to **wfview** over **TCI**
(Transceiver Control Interface). wfview handles the radio; this program gets the
receive audio, sends transmit audio, and keys PTT, all through wfview's TCI server.
It works the same whether wfview reaches the radio over LAN or over USB, and it
should work with any other TCI server too.

It started as the CW decoder and RTTY window proposed for wfview v3
(wf-group/wfview PRs #151 and #152). Moving them out of wfview keeps wfview's own
code unchanged and gives wfview's TCI server a real client to test against.

## Features

**CW (receive)**
- Two decoders: *Machine* (ggmorse, for keyers and computer-sent CW) and *Hand key*
  (learns each sender's timing).
- Pitch: Auto (strongest steady tone, 200-1200 Hz) or Fixed (200-3000 Hz; Hand key is
  used above 1900 Hz).
- Auto or fixed speed, squelch, re-sync.
- Multi-signal list: every CW signal from 200 to 2000 Hz with its pitch, speed, SNR and
  text. Double-click a row to decode that station.

**RTTY (receive and transmit)**
- Any baud/shift (45.45/50/75/100 bd, 170/200/425/850 Hz presets), 1/1.5/2 stop bits,
  USOS, US TTY or ITA2 figures, Reverse.
- Mark/space filter demodulator with W7AY optimal ATC and a tone-contrast squelch.
- Crossed-ellipse tuning scope, mark/space/quality bars, Auto-tune (finds the tone
  pair in the last few seconds and sets Mark).
- Sideband **Auto**: follows the radio's mode (USB/DIGU or LSB/DIGL), so mark lands on
  the right tone without touching Reverse.
- AFSK transmit through TCI: 8 macros (F1-F8, `{MYCALL}`/`{CALL}`), type-ahead box,
  Hold TX, diddle, TX level, Esc to abort. 0.30 s of mark before the first character
  and 0.15 s after the last.
- Watchdogs release PTT if the server never asks for TX audio, the radio does not go
  to transmit, or the radio drops out of transmit (e.g. wfview's PTT timeout).
- Double-click a call in the receive pane to put it in the Call field.

**TCI**
- Frequency and mode shown and settable from the top bar.
- *View > TCI log*: connection messages plus a box to send raw TCI commands.
- The status bar shows the receive and transmit audio streams once a second
  (rate, channels, frames per packet, packets/s, peak level, how TX is paced). Handy
  for comparing wfview's LAN and USB audio paths.

## Setting up wfview (v3)

1. **Settings > External Control > TCI Server Port**: set a port to enable the TCI
   server (0 = disabled). The default is **50001**.
2. Set up wfview's receive audio as usual (LAN audio, or the radio's USB audio codec).
3. For transmit: tick **Settings > User Interface > Enable PTT Controls**, and put the
   radio in a data mode:
   **DIGU** (USB-D) or **DIGL** (LSB-D). The program warns if the radio is in a mode that
   does not take computer audio.
4. Connect wfview to the radio (wfview refuses TCI clients while no radio is connected).
5. Start TCI CW/RTTY, enter the computer running wfview (127.0.0.1 if it is the same
   one) and port, and press **Connect**. With *Reconnect* ticked it keeps retrying
   every 5 s, so it can be started before wfview.

## Building

Qt 6 (Widgets, WebSockets), a C++17 compiler.

```
mkdir build && cd build
qmake ../tci-cw-rtty.pro CONFIG+=release
make          # nmake with MSVC on Windows
```

Tests:

```
# Qt-free (protocol helpers, RTTY and CW through TCI frames)
g++ -std=c++17 -O2 -Isrc -Iengine tests/test_protocol.cpp src/tciprotocol.cpp engine/rttycore.cpp engine/rttytxsource.cpp -o test_protocol
g++ -std=c++17 -O2 -Isrc -Iengine -Iengine/ggmorse/include tests/test_cw.cpp src/tciprotocol.cpp engine/ggmorse/src/ggmorse.cpp engine/ggmorse/src/resampler.cpp -o test_cw

# End to end against a mock of wfview's TCI server (~40 s)
qmake ../tests/test_e2e.pro && make && QT_QPA_PLATFORM=offscreen ./test_e2e
```

`test_e2e --serve 50001` runs only the mock server, which plays an RTTY station and
decodes what you transmit, so the program can be tried without a radio.

## Notes on wfview's TCI server (v3, November 2026)

Found while writing this client; the client works around each one.

- **RX audio header channel count.** wfview announces `audio_stream_channels:2` and sends
  interleaved stereo (2048 frames = 4096 floats per packet), but writes `channels = 1`
  in every RX audio frame header. A client that trusts the header reads the stream as
  mono at half speed (decoded RTTY is garbage). This client trusts the announced count
  when the server is wfview.
- **TX audio format is fixed.** wfview's TCI TX input expects 48 kHz stereo float32 and
  ignores the TX frame's header (rate, channels). This client always sends that.
- **TX pacing.** wfview sends a TX chrono frame only in front of each RX audio frame.
  If RX audio stopped while transmitting, so would the chrono frames; this client then
  paces TX audio from its own clock (the status bar says which is in use).
- `modulations_list` reports FM as `FM`, but `modulation` reports it as `nfm`.
- TCI has no command for the radio's CW pitch, so the CW decoder has no "follow radio
  pitch" mode here.

## License

GPLv3 (see LICENSE). The CW and RTTY engines come from the author's wfview work.
`engine/ggmorse` is ggmorse by Georgi Gerganov, MIT license (see its LICENSE).
