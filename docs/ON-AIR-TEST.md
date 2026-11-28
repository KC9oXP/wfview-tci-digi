# On-air test sheet

Go through the setup checks before every on-air test, then the items for the build being
tested. Note the result (OK / not OK and what you saw) next to each line.

## Setup checks (every time)

- [ ] **wfview TX codec is LPCM 16-bit** (*Settings > Radio Connection > TX codec* =
      `LPCM 1ch 16bit`). µ-law and PCM 8-bit add spurs about 30-40 dB down; see
      *Troubleshooting* in the README.
- [ ] wfview version line at the top of its log is the build you meant to run (v3.0-beta4
      or newer; not an older installed copy).
- [ ] wfview: TCI server port set (50001) and *Enable PTT Controls* ticked.
- [ ] Radio in the data mode whose modulation input matches the connection (IC-7610 over
      LAN: Data 2 = LAN, or wfview sets the inputs on connect).
- [ ] TX level low to start; ALC at or near zero while sending.
- [ ] TX into a dummy load (or low power) for the first key-up of a new build.
- [ ] If a second receiver is at hand (HackRF / SDR), look at the transmit signal with it,
      not only on the radio's scope.

## 0.9.21: PSK multi-signal list

- [ ] PSK tab: tick *Multi-signal*; the list shows under the receive pane.
- [ ] On a busy PSK31 band (e.g. 14.070): one row per station, each with its own text.
- [ ] No rows fill with junk on an empty band or on FT8 / carriers.
- [ ] Click a row: "[now decoding N Hz; copied so far:]" and that row's text appear above,
      once; the receive frequency moves to that station.
- [ ] Double-click a call in a row: it goes to the QSO log and the Call box.
- [ ] With PSK Reporter auto-detect or GridTracker on: stations from the rows are spotted.
- [ ] Change speed to BPSK63: the list starts afresh.
- [ ] Transmit with the list on: the list holds still while sending; rows carry on after.
