# wfview TCI Digi

> [!WARNING]
> **Alpha release: not fully developed and still very much in testing.**
> Expect bugs, rough edges and changes between versions. So far it has been used on one
> station: an IC-7610 and an FT-991A, each with wfview v3. When you transmit, start into a
> dummy load or at low power and keep an eye on the radio. Bug reports and test results
> are welcome in the Issues tab. The WSPR tab (new in 0.9.17) has hardly been tested on the
> air yet.

**wfview TCI Digi** is a stand-alone digital-modes program for **wfview**. wfview runs
the radio; this program connects to wfview's **TCI** server (Transceiver Control
Interface) and gets the receive audio, sends transmit audio and keys PTT through it.
One connection, no virtual audio cables, no extra CAT port, and it works the same whether
wfview reaches the radio over LAN or over USB.

![SSTV tab receiving a Martin M1 picture](docs/screenshots/sstv.png)

> **Unofficial companion.** This is not (yet) part of the wfview project. It is offered
> to the wfview team for possible adoption; see [Future](#future).

**Contents:** [At a glance](#at-a-glance) | [Quick start](#quick-start) |
[Screenshots](#screenshots) | [The modes in detail](#the-modes-in-detail) |
[In every tab](#in-every-tab) | [Setting up wfview](#setting-up-wfview-v3) |
[Radio data mode](#radio-data-mode-and-modulation-input) | [Building](#building) |
[Notes on wfview's TCI server](#notes-on-wfviews-tci-server-v3-november-2026) | [License](#license)

## At a glance

| Mode | Receive | Transmit | In short |
|---|:-:|:-:|---|
| **CW** | yes | yes (audio CW) | Two decoders (keyer and hand key), auto pitch and speed, a list of every CW signal in the passband |
| **RTTY** | yes | yes (AFSK) | Any baud and shift, crossed-ellipse tuning scope, auto-tune, presets |
| **PSK** | yes | yes | BPSK31, BPSK63, BPSK125; click a signal on the waterfall to tune it |
| **Feld Hell** | yes | yes | Painted receive with auto column alignment and 30 minutes of scrollback |
| **Olivia / Contestia** | yes | yes | All the usual tone/bandwidth settings; interoperates with fldigi |
| **MT63** | yes | yes | 500 / 1000 / 2000 Hz, short or long interleave (MT63-1KL for net traffic); fldigi's code |
| **SSTV** | yes | yes | Martin, Scottie, Robot, PD modes; auto start, auto slant, auto offset; send a picture with CQ / answer / report / 73 text, a timestamp and two custom text or picture boxes |
| **AMTOR FEC** | yes | yes | Mode B (SITOR-B style), 100 baud |
| **WSPR** | yes | yes (beacon) | WSJT-X's decoder; 2-minute slots, TX %, band hopping, finds free offsets, uploads to WSPRnet / PSK Reporter / GridTracker |
| **NAVTEX** | yes | - | 518 / 490 kHz and SITOR-B on HF |
| **WEFAX** | yes | - | HF weather fax, 60-240 LPM, straightens and saves the chart |
| **WWV Time** | yes | - | Decodes the WWV / WWVH time code; checks the PC clock, the sound-card clock and the radio's dial accuracy |

Around the modes:

- **Logging:** a session log of everything decoded and sent, and a QSO log that writes
  ADIF for QRZ, LoTW or any logger.
- **Never miss a signal:** long scrollback in every receive pane, a few minutes of
  waterfall history, and the last 2 minutes of receive audio always kept in memory.
- **Try it without a radio:** play any WAV recording into the decoders.
- **Screenshots** of the window or a waterfall with one key, stamped with call, time,
  frequency and mode.
- **Transmit safety:** TX time limit, ALC warning, power/ALC/SWR in the top bar,
  watchdogs that drop PTT if anything stalls. *Auto TX level* sets the level from the ALC.
- **RSID:** hears the ID burst fldigi sends ahead of a transmission and switches to that
  mode, tab and frequency (and back again when it ends).
- **Memories and presets:** a list of digital-mode calling frequencies with US license
  classes, kept in an editable `memories.csv`; click one to tune the radio over TCI.
- **Spotting:** PSK Reporter, WSPRnet and live data for GridTracker2 (each off until you
  turn it on).
- **Updates and languages:** tells you when a new release is out; Japanese translation
  (partial), more welcome.

## Quick start

*This app requires wfview v3: **v3.0-beta4 or newer**. The v3 builds are on wfview's
[Releases page](https://github.com/wf-group/wfview/releases), marked **Pre-release** and listed
above v2.25 (GitHub's "Latest" label still points at the v2 line, so look past it).*

1. **wfview v3**: *Settings > External Control > TCI Server Port* = `50001`. For transmit,
   also tick *Settings > User Interface > Enable PTT Controls*.
2. Connect wfview to the radio as usual.
3. Put the radio in a **data mode whose audio input matches how wfview connects** (LAN or
   USB). This is radio-specific; see
   [Radio data mode and modulation input](#radio-data-mode-and-modulation-input).
4. Start **wfview TCI Digi**, enter `127.0.0.1` (or the computer running wfview) and
   `50001`, and press **Connect**. The button turns green ("Disconnect") when connected.
5. Pick a tab and press **Push To Decode**. To transmit, set *My call*, then type and
   press *Send* (or use the F1-F10 macros). Start with a low *TX level* and keep ALC near
   zero.

**Windows:** download the zip from [Releases](https://github.com/KC9oXP/wfview-tci-digi/releases),
unzip it anywhere and run `wfview-tci-digi.exe` (if a DLL is missing, run
`vc_redist.x64.exe` from the same folder first). **Linux (x86-64):** download
`wfview-tci-digi-<version>-x86_64.AppImage` from the same page, then
`chmod +x wfview-tci-digi-*.AppImage` and run it (no install needed; built on Ubuntu 22.04, so
most current distributions work). **macOS:** build from source (see [Building](#building)).

## Screenshots

**Current version (0.9.17).** Rendered by the end-to-end test against a mock of wfview's
TCI server, which plays an RTTY, a BPSK31 and a Feld Hell station. MT63 and WSPR show the
layout only (no signal in the mock; the red note on WSPR is there because the test leaves
*Grid* empty).

| RTTY | PSK (BPSK31) |
|---|---|
| ![RTTY tab](docs/screenshots/rtty.png) | ![PSK tab](docs/screenshots/psk.png) |

| Feld Hell | MT63 |
|---|---|
| ![Feld Hell tab](docs/screenshots/hell.png) | ![MT63 tab](docs/screenshots/mt63.png) |

| WSPR | |
|---|---|
| ![WSPR tab](docs/screenshots/wspr.png) | |

**Earlier version (0.5).** Taken with the demo recordings played in through *File > Play a
WAV file*, so every tab shows a clean signal. The controls have moved on since (input meter,
presets, Memories, Auto TX level), but the decoders work the same way.

| CW (multi-signal list) | Olivia 8/500 |
|---|---|
| ![CW tab](docs/screenshots/cw.png) | ![Olivia tab](docs/screenshots/olivia.png) |

| SSTV | AMTOR FEC |
|---|---|
| ![SSTV tab](docs/screenshots/sstv.png) | ![AMTOR tab](docs/screenshots/amtor.png) |

| NAVTEX | WEFAX |
|---|---|
| ![NAVTEX tab](docs/screenshots/navtex.png) | ![WEFAX tab](docs/screenshots/wefax.png) |

| WWV Time | |
|---|---|
| ![WWV Time tab](docs/screenshots/wwv-time.png) | |

---

## The modes in detail

The tabs that can transmit (CW, RTTY, PSK, Hell, Olivia, MT63, SSTV, AMTOR, WSPR) come first; a
*Read only* divider separates the receive-only tabs (NAVTEX, WEFAX, WWV Time). Drag tabs
to put them in your own order (kept across restarts); transmit tabs stay left of *Read only*
and receive-only tabs right of it. *View > Reset tab order* puts them back. Every
*Decode* button is dark red and reads "Push To Decode" when off, and green "Decode" when
on. Sending (*Send*, a macro, *TX*, an SSTV picture) turns that tab's Decode on, so the
reply is copied; *Tune* and *CW ID* leave it as it is. Only one tab transmits at a time;
*My call* is shared by all tabs.

### Squelch (CW, RTTY, PSK, Olivia / Contestia, MT63)

Every decoding tab with a squelch has the same control: a slider, its value, a **dB** box
and a **squelch meter**.

- **0 is always off**: everything prints, noise included.
- Each mode measures its own level: CW the tone at the decoding pitch over the noise (dB),
  RTTY the mark/space tone contrast (0-0.95), PSK the phase quality (0-0.95), Olivia /
  Contestia the decoder's S/N for each block (0-20), MT63 the decoder's S/N (dB).
- **dB box**: on RTTY, PSK and Olivia the squelch can be set as a signal-to-noise in dB
  instead (RTTY the tone over the noise in the mark/space filters, PSK the S/N in the symbol
  bandwidth worked out from the phase quality, Olivia the same block S/N as 20 log10). Each
  scale keeps its own setting, so ticking and unticking the box does not lose either one.
  CW and MT63 are in dB already (the box is shown ticked).
- Defaults: CW 7 dB, RTTY 0.50 (8 dB), PSK 0.30 (3 dB), Olivia 5.0 (14 dB), MT63 5.5 dB.
  Noise alone reads up to about: CW 5 dB, RTTY 0.44 / 7 dB, PSK 0.35 / 3 dB, Olivia 3 /
  10 dB, MT63 5 dB.
- The **meter** shows the level the decoder measures now and a white marker at the setting
  it must reach. *Options > Squelch meter* picks the look for every tab: a light green **Bar**
  (bright green while the squelch is open), or a **Magic eye** whose dark wedge closes as the
  level rises, with ticks on the rim where its edges must reach. Hover for the numbers.
- Open and close: a short time above the setting opens it (CW half a second, RTTY 2 bits,
  PSK 4 symbols; a noise spike does not), and it rides through short dips (CW 1 s, RTTY
  1.5 characters, PSK 20 symbols), but never for longer than it has been open, so a spike
  that just opens it does not print junk for the whole hang. Olivia decides block by block:
  it opens on two good blocks in a row and holds up to four weak blocks in a fade, printed
  when a good block follows; at the end of a transmission it stops at once (no junk after
  the text). MT63 opens after 3/4 interleave and carries on to the end of the text.

### CW (receive and transmit)

- Two decoders: *Machine* (ggmorse, for keyers and computer-sent CW) and *Hand key*
  (learns each sender's timing).
- Pitch: Auto (strongest steady tone, 200-1200 Hz) or Fixed (200-3000 Hz; Hand key is
  used above 1900 Hz). Auto or fixed speed, squelch (in dB, see *Squelch* above), re-sync.
- The squelch opens only after the tone has been there for half a second and closes a
  second after it stops, so noise before a signal and AGC recovery after one do not print.
- *Multi-signal*: every CW signal from 200 to 2000 Hz with its pitch, speed, SNR and
  text. Each row has its own decoder: it learns that sender's speed and timing, follows its
  own pitch and is gated by its own tone (the Engine, Fixed speed, Pitch and Squelch settings
  are for the main decoder only). Click a row to decode that station above: what the row
  copied so far (its last ~2000 characters) is put in the main pane first. Double-click a call
  sign in the Text column to put it in the QSO log (with its lookup) and the Call box; that
  station goes above too. Right-click a row for both (the row's call signs are listed). Each station's text also
  goes to PSK Reporter auto-detect and the GridTracker link on its own, like the main pane.
- Transmit as **audio CW**: a keyed tone sent as TX audio with the radio in a data mode,
  the way fldigi does it. On the air it is a normal CW signal at the dial frequency plus
  the pitch. Speed 5-60 WPM, pitch 300-1500 Hz (*= RX pitch* puts you on the station you
  are decoding), 5 ms shaped edges (no key clicks), prosigns `<SK>` `<AR>` `<BT>` `<KN>`
  `<BK>`. (wfview's TCI server has no command for the radio's own CW keyer yet.)

### RTTY (receive and transmit)

- Any baud and shift (45.45/50/75/100 bd, 170/200/425/850 Hz presets), 1/1.5/2 stop
  bits, USOS, US TTY or ITA2 figures, Reverse. The *Preset* box shows the preset the
  current settings match (or *Choose...* when they match none).
- Mark/space filter demodulator with W7AY optimal ATC and a tone-contrast squelch.
- *Tones*: the classic high pair (2125/2295 Hz) or centred on 1500 Hz (1415/1585 Hz, as
  fldigi), so RTTY sits in the middle of a narrow data passband.
- Crossed-ellipse tuning scope, mark/space/quality bars, *Auto-tune* (finds the tone pair
  in the last few seconds and sets Mark). While transmitting, the scope shows your own
  tones with a red *TX* in the corner: the clean "+" of a perfectly tuned signal, to
  compare received stations with.
- *Standard*: back to the amateur standard in one click (45.45 baud, 170 Hz, 2125/2295 Hz
  with mark on the side the sideband puts it, Sideband Auto, Reverse off, 1.5 stop bits,
  USOS, US TTY figures, squelch 0.50). PSK (BPSK31, AFC, squelch 0.30), Olivia (8/500,
  squelch 5.0), NAVTEX (1000 Hz, Reverse off) and WEFAX (120 LPM, IOC 576, Auto start)
  have one too. Frequencies, trims, calls, macros and TX level are not changed.
- Sideband *Auto* follows the radio (USB/DIGU or LSB/DIGL), so mark lands on the right
  tone without touching Reverse.
- AFSK transmit: 0.30 s of mark before the first character, 0.15 s after the last,
  optional diddle. Double-click a call in the receive pane to put it in the Call field.

### PSK (receive and transmit)

- BPSK31, BPSK63 and BPSK125 with standard Varicode.
- Matched-filter receiver: copies cleanly down to about -4 dB SNR in the signal bandwidth
  in the tests.
- Waterfall (0-3 kHz): click a signal to tune (the click snaps to the signal's centre),
  mouse wheel for 1 Hz steps. AFC within one baud, phase-quality squelch with a quality
  bar.
- Speed detection: *Detect* sets BPSK31, 63 or 125 from the last 5 seconds of audio at the
  receive frequency; with *Auto* on (the default) a waterfall click does the same. A PSK
  signal's envelope dips at each phase reversal, on the symbol clock, so it carries a line at
  the baud rate (31.25, 62.5 or 125 Hz); the slowest speed with a clear line is the answer.
  It works on idle reversals before any text, and says so in the status line when there is
  no clear answer (noise, or not PSK).
- Transmit on the receive frequency: idle reversals before the text and a shaped fade-out
  after it, so no key clicks.

### Feld Hell (receive and transmit)

- Standard Feld Hell: 7-column characters, 245 pixels a second, shaped keying (more than
  40 dB down outside +/-250 Hz).
- Receive is painted, not decoded, like every Hell program. Each column is drawn twice
  so drifting text stays readable. Zoom 1-4x, contrast, RX trim for slanted text.
- *Auto align* re-cuts the columns so letters are not split top/bottom; manual *Shift*
  as well.
- The last 30 minutes are kept: mouse wheel or PgUp scrolls back (*Live* returns),
  *Save picture* writes a PNG, *Auto-save* keeps one in the logs folder.

### Olivia and Contestia (receive and transmit)

- Olivia 4/125 to 64/2000 and Contestia 4/125 to 64/1000, using Pawel Jalocha's MFSK
  library exactly as fldigi does, so the two interoperate.
- The receiver finds signals a few tone spacings off the set frequency by itself. A squelch
  on each block's own S/N keeps noise (and the library's junk after a signal ends) off the
  screen, and keeps printing through fades (see *Squelch* above).
- Transmit starts with two FEC blocks of idle (1-4 s) so the far end has locked on before
  the first letter.

### MT63 (receive and transmit)

- MT63-500, -1000 and -2000 (5, 10 and 20 characters per second), short (S) or long (L)
  interleave, using Pawel Jalocha's MT63 code as fldigi carries it, so the two interoperate.
  MT63-1000L is the usual choice for net traffic (for example AmRRON / TAPRN nets, which take
  check-ins on Contestia and pass traffic in MT63 with an RSID first).
- 64 carriers with Walsh-function FEC and a long interleave: copy survives fades and a few
  seconds of interference. The price is delay: text comes out one interleave after it was
  sent (6.4 s for 1000L).
- The receiver follows a few tens of Hz of tuning error by itself. An S/N squelch (default
  5.5 dB; noise alone reads up to about 5) keeps noise off the screen, holds back the
  decoder's random characters while it locks on, and carries on to the last character at the
  end of a transmission (the sender's closing fill marks the end).
- Transmit sends one interleave of fill before the text and after it, as fldigi does, so the
  far end gets the first and last letters.

### SSTV (receive and transmit)

- Martin M1/M2, Scottie S1/S2/DX, Robot 36/72, PD50/90/120/160/180/240.
- Pictures start on their VIS header. Joined late? *Start without VIS* starts the picture
  when the sync pulses repeat at one mode's line timing (as MMSSTV does), or pick the
  mode and *Start now*. The mode list follows each picture received, and with *Follow
  received* ticked (beside the send mode) the send mode changes to the mode of a picture
  that came in with its VIS header, so you answer in the mode the other station used.
- Every picture runs for the whole length of its mode: a fade does not end it, and the
  lines after it fill in when the signal comes back. Only the last line, *Stop* or a new
  VIS header end a picture. *Stop* saves what came in, however short (`..._stopped.png`);
  a picture that ran to the end with a sync on fewer than 10 % of its lines was a false
  start on noise and is not saved.
- *Start now* searches the whole line for the sync until three agree, and again if it
  loses them.
- Tuning: click the middle of the signal on the small waterfall (or drag, or the wheel
  for 1 Hz steps); markers show sync, black and white. *Auto* offset refines it from the
  1900 Hz VIS leader and the 1200 Hz sync pulses.
- *Auto slant* fits a line through every line's sync pulse, so a slanted picture is
  straightened as it comes in; a ppm *Trim* covers a known sound-card clock error.
  *Use when sending* applies the Trim to sent pictures too (worth it only for large
  errors: the note beside it says how many pixels it changes in the chosen mode).
- Pictures are saved as PNG (in 4:3) and shown in a strip to click through.
- Transmit: open or paste a picture (or *Use shown* to send back the one received), pick
  the mode, preview, progress, abort, *CW ID after picture*. While sending, a bright
  yellow line moves down the preview at the line on the air (the part still to go is
  dimmed) and the progress bar reads `line 120 / 240  42 s left`. *Tune* sends a steady
  1750 Hz tone to set the level.
- *Picture text*: pick the line drawn along the bottom of the picture - None, CQ
  (`CQ SSTV de {MYCALL} {GRID}`), Answer (`{CALL} de {MYCALL}`), Report
  (`{CALL} de {MYCALL} UR {RST}`), Thanks, 73, Test or Custom. Each text can be edited
  and is kept (*Default* puts it back). `{MYCALL}` `{CALL}` `{RST}` `{GRID}` `{DATE}`
  `{TIME}` are filled in. The text keeps its normal size: a long message wraps onto more
  lines and the dark band grows upward (up to half the picture; only past that does the
  size step down a little, and what still does not fit ends in "...").
- *Timestamp*: date and time (UTC or local) in a top corner, taken when Send is pressed.
- *Custom boxes* (0.9.19): *Custom 1* and *Custom 2*, two more boxes anywhere on the
  picture. Each holds text (the same `{MYCALL}` `{CALL}` ... placeholders, as big as the
  box allows, in the colour you pick) or a picture (a logo, a photo, a picture of text;
  shown whole, its shape kept). *Background* puts a dark see-through box behind it. Drag a
  box on the preview to move it, drag a corner to resize it. The layout is kept as a share
  of the picture, so it stays put in every mode and for the next picture and the next start.
  The boxes go over the picture text; the timestamp stays on top. A missing picture file
  leaves the box out (with a note) instead of stopping the send. No editing while sending.

### AMTOR FEC (receive and transmit)

- Mode B (FEC, as SITOR-B): 100 baud, 170 Hz shift, CCIR 476, every character sent twice,
  phasing signals first and the end-of-emission signal last. Typing as you go is fine.
- Receives with the same decoder as NAVTEX; *Reverse* for LSB or inverted stations. On the
  amateur bands AMTOR belongs in the RTTY/data segments.

### WSPR (receive and beacon transmit)

- Weak Signal Propagation Reporter: 2-minute slots starting on even UTC minutes, USB dial
  (the usual dials are in *Band*, the presets and Memories), signals in the 200 Hz window
  1400-1600 Hz audio. The decoder is WSJT-X's `wsprd` (K1JT, K9AN; Fano decoder by KA9Q), so
  it copies what WSJT-X copies, down to about -28 dB in 2500 Hz. *Depth*: Fast (one pass),
  Normal (a second pass after taking out the strong signals) or Deep.
- **The PC clock must be right to about a second.** The tab warns when the stations heard
  are on average more than a second early or late; the WWV Time tab can check the clock.
- *Enable TX* sends your beacon (*My call*, *Grid*, *Power*) in about *TX %* of the slots
  (20 % = one in five), never two slots in a row up to 50 %; *TX next slot* sends in the next
  one once. Enable TX is always off when the program starts. A 6-character grid, or a call
  with a / prefix or suffix, is sent as WSJT-X does: two messages taking turns
  (`K1ABC FN42 37` and `<K1ABC> FN42AB 37`; `PJ4/K1ABC 37` and `<PJ4/K1ABC> FK52UD 37`).
  The hashed `<call>` of other stations shows once their plain message has been heard
  (kept in `wspr-hashes.txt`).
- The strip over the 200 Hz window is a live waterfall while Decode is on (one line a second,
  about 0.7 Hz per pixel column, so a WSPR station shows as a thin trace about 6 Hz wide).
- **Free offsets:** every decoded slot is remembered for 20 minutes per band: the stations
  decoded and any other energy in the window (signals too weak to decode). The window strip
  shows them (orange = decoded, grey = heard but not decoded, green = suggested free offsets,
  red = your TX offset); the three *Free* buttons are the clearest offsets, and a click on
  the strip puts your beacon there. *Move to a free offset if mine is in use* moves the
  beacon to the best one before a transmission when a station was heard on yours. On a crowded
  band (80 m at night) where no offset is clear, the buttons show the least used offsets (marked
  *), and the beacon only moves if that offset was used less than yours.
- *Band hopping*: *Coordinated (WSJT-X schedule)* goes to the same band as every other
  station hopping (160, 80, 60, 40, 30, 20, 17, 15, 12, 10 m, one per slot, round every 20
  minutes), skipping bands you did not tick; *My bands in turn* steps through your ticked
  bands. *Bands...* picks the bands and on which of them the beacon may transmit. Transmit
  and band hopping only run while the WSPR tab is shown (so another tab is never interrupted).
- Spots: the table (newest first, with the distance from your grid), the session log, and
  `wspr-spots.txt` in the logs folder. *Upload spots to WSPRnet* (off by default) sends each
  spot to wsprnet.org, and a "listening" report when nothing was heard; PSK Reporter and
  GridTracker (WSJT-X's WSPR decode message) get the spots too when they are turned on.
- A slot is only decoded if the radio stayed on one dial, nothing transmitted and Decode was
  on for all of it (the status line says why a slot was skipped).
- *Power* is only what the message reports (the power at the antenna); it does not set or
  measure the radio's output, so set the radio to match. *From radio* (for radios whose wfview
  reports output in watts) sets Power after each transmission from the radio's own reading,
  less the feed-line *loss*, for the next message; the radio's power is never changed. 60 m in the US: the 5.3647 MHz dial
  is in the 5351.5-5366.5 kHz segment (General and up since 2026-02-13, 9.15 W ERP max, so 37
  dBm or less; the tab warns at 40 dBm and up); 5.2872 MHz is not a US amateur frequency.

### NAVTEX / SITOR-B (receive)

- 100 baud, 170 Hz shift, CCIR 476 with the time-diversity FEC repair; bit sync, phasing
  search and FEC after fldigi's NAVTEX decoder.
- 518 kHz / 490 kHz (tune USB 1 kHz below) and SITOR-B on HF.

### WEFAX (receive)

- HF weather fax, 60/90/120/240 LPM, IOC 576/288.
- Starts on the APT start tone, aligns on the phasing lines (and takes the true line
  period from them, so the chart comes out straight), ends on the stop tone, saves a PNG.
  *Start now* for a chart joined late; *Align*, then a click on the chart's left edge,
  moves it into place.

### WWV Time (receive)

Tune WWV or WWVH in AM with a wide filter: WWV (near Fort Collins, Colorado) is on 2.5, 5,
10, 15 and 20 MHz, plus an experimental 25 MHz broadcast that may be off the air; WWVH
(Kauai, Hawaii) is on 2.5, 5, 10 and 15 MHz. The *Go to* buttons at the right of the
lamps tune the radio there in AM; the one you are on is green.

- **Lamps** show what is being heard: second ticks, the 440/500/600 Hz tones, the 100 Hz
  time code, a decoded time, a measured carrier. If *100 Hz code* stays grey while *Ticks*
  is lit, the radio's audio does not pass 100 Hz.
- **Time:** UTC, date, UT1-UTC and the daylight-time and leap-second flags from the 100 Hz
  code. The code has no error check, so a time counts as *verified* when two minutes in a
  row agree (two to three minutes after Decode). WWV or WWVH is told apart by the tick
  tone.
- **PC clock:** how far the computer's clock is from WWV, after taking off the radio path
  (from your grid square) and the audio delay through radio and wfview. *Calibrate
  delay* measures that delay once while the PC clock is known to be right (kept per
  radio). *Set PC clock to WWV* shifts the clock (Windows; asks for administrator rights).
- **Audio clock:** the spacing of the ticks gives the error of the sound path in ppm.
  *Picture trim* shows the SSTV and WEFAX trim and can be changed there; *Copy audio
  clock to trim* puts the measured value in. Only SSTV and WEFAX use it.
- **Tones:** how far the 440/500/600 Hz tones read off.
- **Measure carrier:** tunes 1 kHz below the WWV frequency in USB, then 1 kHz above in
  LSB (about 25 s in all), measures where the carrier lands each time, then puts the radio
  back. A dial error moves the two tones opposite ways, so half their difference is the
  radio's dial error in Hz and ppm; whatever moved both tones the same way (the sound path,
  a sideband offset in the radio) is shown separately and left out.

---

## In every tab

**RSID** (Options > RSID): the receive audio is watched on every tab for the short ID burst
fldigi and other programs can send before a transmission (15 tones, about 1.4 s, 170 Hz
wide). It names the mode and marks its frequency. For PSK (BPSK31/63/125), RTTY (45.45/170,
50/170, 75/850), Olivia, Contestia, MT63, Feld Hell and CW the program shows that tab, sets
the mode and frequency, turns Decode on and prints an amber "RSID: MT63-1000L at 1500 Hz"
line in the receive pane (*Switch to the mode it names*, on by default; off = only a notice
in the status bar). Other modes (MFSK, THOR, DominoEX ...) are named in the status bar.
*Go back to the previous tab when the signal ends* (on by default): once the signal it
switched to has ended (no new text for 10 s, 20 s on MT63, or none for a minute) the tab
you were on comes back, for example the Contestia net after a piece of MT63 traffic. It
stays if you transmit or pick another tab yourself.
*Fix an inverted sideband (switch to DIGU and retune)* (on by default): Olivia, Contestia
and MT63 are sent on upper sideband. On LSB (e.g. DIGL on 80 m) their tones arrive reversed:
the ID is still found, but the mode prints only garbage. When such an ID is heard mirrored
and the radio is on an LSB mode, the program switches the radio to the matching USB data
mode (DIGL -> DIGU, DATA-L -> DATA-U ...) and moves the dial by twice the audio frequency so
the signal stays where it was in the waterfall: 3.586.000 DIGL with the signal at 2003 Hz
becomes 3.581.994 DIGU, still at 2003 Hz. The radio stays there afterwards (*Go back*
only changes the tab). A mirrored ID with the
radio already on USB means the other station sends inverted: only a note. Off: a note
saying to switch by hand. Never while transmitting.

**Every tab keeps decoding** while another is showing (with Decode on). A tab that is not
showing gets an amber dot when its decoder prints something; the dot goes when you look.
Either sideband works: the burst is also looked for mirrored. Nothing is checked while
transmitting. The decoder follows fldigi's (see License).

**Receive pane**: a blank line separates overs. Text that arrives after 8 seconds with
nothing decoded starts after a blank line, and the transmit echo (`[TX] ...`) has a blank
line before and after it. The pane follows the newest text whenever it is scrolled to the
bottom, also with text selected (a double-clicked call sign, Ctrl+A); scroll up to stop it
and *Live* to go back.

**Transmit**
- Macros F1-F10 with `{MYCALL}` / `{CALL}` (two rows of five; F9 and F10 start empty, set
  them with *Edit...*), a type-ahead box (text typed while
  transmitting is added to the queue), *Hold TX*, *TX level*, Esc to abort.
- *CW ID*: "DE <your call>" as keyed audio CW. `{CWID}` at the end of a macro sends it
  after the text without dropping PTT (Options > Tune tone, CW ID and Auto TX level).
- *Auto TX level* (left of the TX level slider): sets the tab's TX level from the radio's ALC. It transmits a short test
  signal shaped like the mode (RTTY and AMTOR: mark and space alternating at the baud
  rate; other modes: one tone at the tab's audio frequency, which has the same peaks) and
  finds the highest level whose ALC reading is at or below the target (0.1 by default,
  Options > Tune tone, CW ID and Auto TX level; the scale is whatever wfview reports for the
  radio). With a target of 0 it first raises the level until ALC shows, then backs off
  until it reads 0. Usually 5-20 s; click again to stop. Needs ALC reports (tx_sensors)
  from the server (a server that only reports changed readings is fine). If it is stopped
  or runs out of time it keeps the highest level it found at or below the target, and a
  box says why it stopped.
- The TX level is in dB below full level (0 to -40 dB, 0.5 dB steps; Page Up/Down 3 dB),
  kept per mode, and per sub-mode for PSK31/63/125 and each Olivia / Contestia setting.
- The mode check accepts every data-mode name servers use (DIGU/DIGL, DATA-U/DATA-L,
  USB-D/LSB-D, PKTUSB ...) and plain USB/LSB.

**Frequency box** (top bar): always 9 digits with leading zeros (`001.810.000`), so every digit
up to 100 MHz can be stepped: on 160 m, turn the 10 MHz digit up to go straight to 10 MHz. Point
at a digit and turn the mouse wheel to step it, or click the top half of the digit to step up
and the bottom half to step down. Clicks only step: they never put a text cursor in the box. To
type a frequency (14.080, 14080k): right-click > *Type a frequency...* (or Tab into the box),
type and press Enter; Esc cancels.

**Transmit safety**
- Options > *TX time limit* (default 5 minutes; off, 1-10): the transmitter is released
  after this long keyed, whatever the mode.
- While transmitting, the top bar shows the output power, ALC and SWR wfview reports.
  Options > *ALC warning...* turns the meter red when ALC stays above a limit you set.
  ALC scales differ between radios, so the limit is kept per radio.
- Watchdogs release PTT if the server never asks for TX audio, the radio does not go to
  transmit, or the radio drops out of transmit (e.g. wfview's PTT timeout).
- If the radio keys up but reports no output power, a hint explains what to check (see
  [Radio data mode](#radio-data-mode-and-modulation-input)).

**Top bar**
- *Connect*: red when disconnected, amber "Cancel" while trying, green "Disconnect" when
  connected. With *Reconnect* ticked it keeps retrying every 5 s.
- Frequency and mode, shown and settable.
- *SNR*: live signal-to-noise of the signal on the tab in use (green 10 dB and up, amber
  3-10 dB; `SNR --` below 3 dB, where there is no signal above the noise; `SNR (TX)` while
  transmitting). The signal's strongest spectrum bin is compared with the typical strongest
  bin of same-width stretches of noise elsewhere in the passband, so an empty frequency reads
  about 0 dB; RTTY needs both mark and space and measures the weaker. Same measurement as the
  QSO log's signal report; not WSJT-X's 2500 Hz-referenced SNR.
- *RX input level* (right of SNR, the same on every tab): the receive audio wfview sends, in
  dBFS (0 = full scale). The bar is the average (RMS) level, the thin mark the recent peak.
  Green -50 to -12 dBFS is about right; amber is too quiet or too hot; red means peaks at full
  scale (clipping). Set it with wfview's RX audio level and the radio's AF/RF gain. It is an
  input level, not a signal report: band noise has a level too. Shows `TX` while transmitting
  and `no audio` when wfview sends none.
- *Log*: the session log on (green) or off, for all modes.
- *Presets*: frequency presets for the tab showing (SSTV 3.845 LSB / 14.230 ..., WEFAX NMF
  / NMG with the 1.9 kHz offset, NAVTEX 518 kHz, WWV 2.5-25 MHz AM ...). A preset sets the
  dial and mode over TCI; add the current frequency or edit the list. While the dial is on a
  preset (within 10 Hz, same sideband) the box shows that preset's name.
- *Memories* (Ctrl+M, also Options > Memories...): a window listing frequencies with their
  tab, sub-mode (BPSK31, Olivia 8/500, RTTY 45.45/170 ...), dial, sideband, band, US
  license classes and a comment. Click one and the app switches to that tab, sets the
  sub-mode and tunes the dial and sideband over TCI (untick *Tune on click* to use
  double-click or Go instead). Filter by tab, by license class (Novice, Technician,
  General, Advanced, Extra, Other) or by text; *New...* adds one by typing it in, *Add
  current...* saves what you are on now. *Other* is a class of your own for things outside the
  US amateur rules (11 m, MARS, another country's bands).
  - The list is `memories.csv` in the data folder, so it can also be edited in Excel or a
    text editor; changes saved there show in the window straight away. Columns: `Name,
    Tab, Sub-mode, Dial MHz, Sideband, Band, Classes, Comment` (Dial MHz also takes Hz or
    kHz, e.g. `14070 kHz`).
  - The first time, the file is made from a built-in list: the digital-mode frequencies of
    the ARRL band plan, common PSK31 / Olivia / SSTV spots (marked as such in the
    comment), the Feld Hell Club's suggested Hell frequencies 160-6 m (dial 1 kHz below,
    so the signal sits at 1000 Hz audio), the US 60 m channels and segment for CW, RTTY,
    PSK31 and Olivia (100 W ERP on the channels, signal centred on the channel at 1500 Hz
    audio; 9.15 W ERP in 5351.5-5366.5 kHz; 2.8 kHz bandwidth), and the NAVTEX, WEFAX and WWV receive frequencies. *Add built-in...* adds any
    built-in entries missing from your list later without touching your own.
  - Built-in rows that a newer version moves (0.9.16: PSK31 and RTTY on 17 m, RTTY on 20 m
    and 12 m, Feld Hell on 160, 80 and 30 m, all clear of FT8/FT4 now) are updated in your
    list when the window opens, but only rows you never edited; your own rows and edited
    ones stay as they are. The status line names the rows it moved.
  - Classes: an empty Classes cell is worked out from FCC Part 97 (ITU Region 2) for the
    tab's emission (CW, RTTY/data or SSTV image) across the 3 kHz the signal can occupy
    from the dial, e.g. SSTV on 7.171 LSB is Advanced and Extra only, data on 28.120 is open
    to every class, Novice and Technician have CW only on 80, 40 and 15 m. 60 m follows the
    FCC rules from 13 February 2026: General and up, CW / data / USB phone only, on the four
    channels centred on 5332, 5348, 5373 and 5405 kHz (USB dial 1.5 kHz below) and anywhere
    in 5351.5-5366.5 kHz. This is a guide: check the current rules before transmitting.

**PSK Reporter** (status bar button, Options > *PSK Reporter...*): sends the callsigns this
station hears to [pskreporter.info](https://pskreporter.info), where they show on the map.
- Off until you turn it on. The button shows *PSKR off*, or *PSKR* and the number of spots sent
  (green; amber when something needs attention, e.g. no grid). Click it to turn reporting on or
  off; the first time it asks for your call and grid (the call is the same My call that every
  transmit tab and the QSO log use, so it can be typed here on a fresh install).
  Right-click for the settings.
- What is reported: every QSO you log, and, with *Auto-detect* ticked, any call seen once
  right after CQ, DE or 73 (`CQ DE W1AW K`, `73 W1AW SK`; one over is enough) in the CW, RTTY,
  PSK, Olivia/Contestia, MT63 and AMTOR tabs, each call at most once per 10 minutes. After 73 the call
  counts only if DE does not follow it (`TU 73 K1ABC DE W1AW` reports W1AW, not K1ABC, who was
  only being thanked). A call sent twice in a row also counts (`VVV TEST TEST N9KRK N9KRK`),
  unless DE follows it (`N9KRK N9KRK DE W9ABC` is W9ABC calling N9KRK). Text you send is never
  read.
- As the PSK Reporter developer page asks: at most one report every 5 minutes (timed from when
  you turn it on, not the clock), each call at most once per 5 minutes and otherwise once an
  hour unless it moves band; mode names are ADIF (CW, RTTY, PSK31, OLIVIA, CONTESTI,
  AMTORFEC). A *test server* option sends to PSK Reporter's packet checker instead of the map.

#### GridTracker (live frequency and heard calls)

*Options > GridTracker (live frequency and heard calls)...* (off until turned on) sends GridTracker2
the dial frequency, band, mode, your call and grid every 15 seconds and whenever they change, and the
calls heard in the CW, RTTY, PSK, Olivia/Contestia, MT63 and AMTOR tabs right after CQ, DE or 73 (or sent twice
in a row, as for PSK Reporter), each call at
most once a minute, so they show on its map. The heard calls appear as `CQ W1AW` (a CQ), `K1ABC W1AW`
(W1AW calling K1ABC; calls to you are highlighted by GridTracker) or `DE W1AW`. It uses the same UDP
message format WSJT-X sends, to 127.0.0.1 port 2237 by default (GridTracker2: Settings > General >
Receive UDP Messages, port 2237). Logged QSOs still reach GridTracker through *Logging and uploads*
(ADIF, port 2333). Text you send is never read.

**Logging and uploads** (Options > *Logging and uploads...*; status bar *QRZ* / *LoTW*): every
logged QSO is always written to the ADIF file. Each one can also go, as soon as it is logged:
- *To another program* as an ADIF record over UDP, e.g. GridTracker2 (Settings > General >
  Receive ADIF UDP Broadcasts For Logging, port 2333), which then uploads with its own LoTW / QRZ
  settings. Nothing secret is stored in this app.
- *Straight to your QRZ.com logbook* with its logbook API key (QRZ subscription needed). *Test key*
  answers Pass or Fail.
- *Straight to LoTW*: TQSL signs and uploads in the background (no TQSL window, no password to type)
  with the certificate password saved once, or none if the certificate has no password. TQSL is found
  where its installer put it. *Station location* lists the Station Locations set up in TQSL; pick
  yours (the name must match TQSL exactly, or TQSL stops with "command syntax error"). A name TQSL
  doesn't have is caught before TQSL runs, and the status says which names TQSL has.
- A QSO that cannot be sent (no internet) waits and goes with the next one; *Retry now* sends it at
  once. A login problem (wrong key or password) stops that service and turns its button red until it
  is fixed, instead of trying again and again. Status bar: green sent, blue sending, amber waiting,
  red stopped.
- Security: the key and password are kept in Windows Credential Manager, never in a file, and shown
  only as stars. When used, a secret is decrypted into locked memory (kept out of the page file and
  encrypted in place while it waits), handed to the HTTPS request or TQSL, and wiped. The QRZ address
  is built in (a settings file cannot point the key elsewhere), redirects are not followed, every
  field is encoded, and nothing secret is written to the logs (TQSL's command line is shown with
  `-p ********`). Outside Windows there is no supported store yet, so secrets last only until the
  app closes. As with any program, malware running as your Windows user could still read them.

**Logs, scrollback, screenshots, recordings**
- *Session log*: everything decoded and sent, a text file per mode per UTC day, with a
  header line for each change of frequency or settings.
- *QSO log* (View > QSO log, Ctrl+Shift+Q): Call / RST / Name / QTH / Grid / Comment with
  frequency, band, mode and times filled in; *Log QSO* (Ctrl+L) appends an ADIF record
  (`wfview-tci-digi.adi` in the logs folder, for QRZ, LoTW via TQSL, or any logger that imports
  ADIF). Double-click a callsign in any receive pane (or right-click > *Log <call>*) to put it
  in the QSO log's Call box.
- *Callsign lookup* (Options > *Callsign lookup (QRZ / HamQTH)...*, off until set up): when a
  call is entered, Name, QTH and Grid are filled in from QRZ.com (your QRZ username and password,
  not the logbook API key; without a QRZ XML subscription QRZ gives name and address but no grid)
  or from HamQTH (free account). Only empty boxes are filled. *Save and test* logs in and looks up
  your own call. The password is kept like the other secrets (Windows Credential Manager); each
  call is looked up once per session.
- *Add signal report to the comment* (on by default): logging adds e.g. `SNR 18 dB, S7` to the
  comment. The SNR is the signal's peak over the noise floor in the receive spectrum, at the
  frequency of the tab in use, smoothed over a few seconds and never measured while you transmit
  (it is not WSJT-X's 2500 Hz-referenced SNR); below 3 dB it is left out. The S-meter is asked from wfview (TCI `rx_smeter`,
  dBm, S9 = -73 dBm); if wfview does not answer, only the SNR is added.
- *Fill RST sent with a measured report* (on by default, shown only on the SSTV and PSK tabs):
  instead of the usual 595 / 599, "RST sent" gets a report measured from the signal. Type in
  the box to override it.
  - SSTV (RSV), from the last picture (up to 15 minutes old): **R** from the share of line
    syncs found (95 % -> 5, 80 % -> 4, 60 % -> 3, 40 % -> 2); **S** from the S-meter when the
    picture ended (the SNR if wfview gives no S-meter); **V** from the picture noise, measured
    on the sync pulses (a steady tone, so their spread is the noise every pixel gets), in
    levels of 255: up to 3 -> 9, 5 -> 8, 8 -> 7, 12 -> 6, 18 -> 5, 26 -> 4, 36 -> 3, 50 -> 2.
    9 and 8 look clean, 7-6 show a fine grain, 5-4 clear snow, 3 and below heavy snow.
  - PSK (RSQ, Readability / Strength / Quality): **R** from the share of characters copied
    (95 % -> 5, 80 % -> 4, 40 % -> 3, 20 % -> 2), worked out from the decoder's phase quality
    (calibrated on this decoder; BPSK31, 63 and 125 give the same curve); **S** from the trace's
    SNR (3 dB -> 1, one per 3 dB, 27 dB -> 9); **Q** from the sideband pairs measured on the
    idle at the start or end of an over (IMD, as fldigi shows it): -30 dB or better -> 9,
    -27 -> 8, -24 -> 7 (one barely visible pair), -21 -> 6, -18 -> 5 (one easily visible pair),
    -15 -> 4, several pairs -> 3 or 2, splatter -> 1. A pair counts as visible when it is over
    the noise and within about 27 dB of the signal. The IMD is added to the comment.
  - CW, RTTY, Hell, Olivia, MT63 and AMTOR have no report that can be measured honestly (the
    T of RST, or the share of a free-text over copied correctly), so the box is not shown there.
- *Fill RST rcvd from the report in the received text* (on by default; CW, RTTY, PSK, Olivia,
  MT63 and AMTOR): "RST rcvd" gets the report the other station sends you - a number after
  UR, YR, RST, RPT or REPORT (`UR RST IS 579` works; CW cut numbers `5NN` = 599), in text
  received on the tab in use that also has your call (and theirs, if it is in the Call box)
  within the 40 words before it. The latest one wins; the tooltip shows the text it came
  from. Type in the box to override it; *Clear* or *Log QSO* starts again. Not on SSTV,
  where the report is drawn in the picture: type it in.
- Receive panes keep a long history: scroll back and the view stays put while new text
  arrives (*Live* returns), Ctrl+F finds text, right-click saves it.
- Waterfalls keep the last few minutes: Shift+wheel or PgUp/PgDn scrolls back, Esc
  returns to live; right-click saves a picture.
- File > *Save window screenshot* (F12) and *Save waterfall picture* (Ctrl+Shift+S).
- File > *Save the last 2 minutes of receive audio* (Ctrl+Shift+A), or *Record receive
  audio* for as long as you like.
- File > *Play a WAV file into the decoders* (real time or 4x): every tab decodes it as
  if it came from the radio.
- Everything goes in one data folder (Options > Data folder; default
  Documents/wfview TCI Digi): logs, sstv, wefax, screenshots, recordings.

**TCI and diagnostics**
- *View > TCI log*: connection messages plus a box to send raw TCI commands.
- The status bar shows the receive and transmit audio streams once a second (rate,
  channels, frames per packet, packets/s, peak level, how TX is paced), and says so when
  wfview sends receive audio that is all zeros (see the notes below).

**Updates**
- At every start the program asks GitHub whether a newer release exists. If one does,
  the status bar shows "Version x.y.z is available": click it to open the release page,
  or *skip* to hear no more about that version. Nothing is installed for you.
- Help > *Check for updates...* asks at any time and shows links to the release page and
  to the download for your computer (Windows zip or Linux AppImage). Options > *Check for
  updates at start-up* switches the start-up check off.
- The only thing sent is the request for the public list of releases.

**Language**
- Options > *Language*: *System language* (default, follows Windows / Linux), *English*,
  or any translation the program has. The choice is saved and used from the next start
  (the window is not re-labelled while it is running, so nothing on the air is disturbed).
- Translations so far: Japanese (JG3HLX), partly done; anything not yet translated stays
  in English. They are built into the program. How to add or finish a language:
  [translations/README.md](translations/README.md).

---

## Setting up wfview (v3)

wfview **v3** is required: v3.0-beta4 or newer, from the *Pre-release* entries on
[wfview's Releases page](https://github.com/wf-group/wfview/releases) (the v3 source is the
[v3-initial branch](https://github.com/wf-group/wfview/tree/v3-initial)). On the IC-7610 over
LAN, v3.0-beta3 put no RF out on the author's station with the TX codec set to LPCM 1ch 16bit;
the next beta fixed it.
wfview 2.24 answers TCI (the radio, frequency and mode show
up) but never sends TCI audio, so the status bar stays at "RX audio: none".

1. **Settings > External Control > TCI Server Port**: set a port to enable the TCI server
   (0 = disabled). The default is **50001**.
2. Set up wfview's receive audio as usual (LAN audio, or the radio's USB audio codec).
3. For transmit: tick **Settings > User Interface > Enable PTT Controls**, and put the
   radio in a data mode (**DIGU** / USB-D or **DIGL** / LSB-D). The program warns if the
   radio is in a mode that does not take computer audio.
4. Connect wfview to the radio (wfview refuses TCI clients while no radio is connected).
5. Start wfview TCI Digi, enter the computer running wfview (127.0.0.1 if it is the same
   one) and port, and press **Connect**. *Options > Connect on start* connects as soon as
   the program opens.

## Radio data mode and modulation input

Every transmit mode here sends **audio**. The radio only puts that audio on the air if it
is in a data (or SSB) mode **and** that data mode takes its audio from the input wfview is
feeding: the LAN input when wfview connects over the network, the USB input when it
connects over USB. Get this wrong and the radio keys up with no output power.

How to set it depends on the radio. On Icom radios each data mode (D1, D2, D3; *Data 1/2/3*
in wfview) has its own modulation input (DATA MOD: MIC, ACC, USB, LAN). Check, in the
radio's menu or in wfview's *Settings > Radio Settings > Current transmit modulation
source*:

1. which input each data mode uses, and
2. that wfview has the radio in the data mode whose input matches the connection.

Example (the author's IC-7610): DATA1 = USB, DATA2 = LAN, DATA3 = ACC, so with wfview on
LAN the radio must be in **Data 2**. wfview can set the inputs every time it connects:
*Set transmit mod input sources on connect*, with Data On = LAN (or USB).

Set the data mode in wfview. Choosing DIGU/DIGL in this program's mode box switches wfview
to **Data 1**, whatever data mode the radio was in (see the notes below).

A radio's data mode can also narrow its receive audio. On the FT-991A in DATA-U the audio
was cut above about 1.5-1.8 kHz, which spoils SSTV; plain USB was fine.

## Building

Qt 6 (Widgets, WebSockets, Network), a C++17 compiler.

```
mkdir build && cd build
qmake ../wfview-tci-digi.pro CONFIG+=release
make          # nmake with MSVC on Windows
```

Tests. The modems are Qt-free engines (`engine/`), each tested encoder -> decoder with
noise, clock error and frequency offsets:

```
# Protocol helpers, RTTY and CW through TCI frames
g++ -std=c++17 -O2 -Isrc -Iengine tests/test_protocol.cpp src/tciprotocol.cpp engine/rttycore.cpp engine/rttytxsource.cpp -o test_protocol
g++ -std=c++17 -O2 -Isrc -Iengine -Iengine/ggmorse/include tests/test_cw.cpp src/tciprotocol.cpp engine/ggmorse/src/ggmorse.cpp engine/ggmorse/src/resampler.cpp -o test_cw
# PSK and Feld Hell
g++ -std=c++17 -O2 -Iengine tests/test_psk.cpp engine/pskcore.cpp -o test_psk
g++ -std=c++17 -O2 -Iengine tests/test_hell.cpp engine/hellcore.cpp -o test_hell
# CW transmit (keyer timing, key clicks, prosigns, loopback through the CW decoder)
g++ -std=c++17 -O2 -Isrc -Iengine -Iengine/ggmorse/include tests/test_cwtx.cpp engine/cwtxcore.cpp engine/modemtxsource.cpp engine/pskcore.cpp engine/hellcore.cpp engine/ggmorse/src/ggmorse.cpp engine/ggmorse/src/resampler.cpp -o test_cwtx
# SSTV, WEFAX, NAVTEX / AMTOR FEC, Olivia / Contestia, WWV
g++ -std=c++17 -O2 -Iengine tests/test_sstv.cpp engine/sstvcore.cpp -o test_sstv
g++ -std=c++17 -O2 -Iengine tests/test_wefax.cpp engine/wefaxcore.cpp -o test_wefax
g++ -std=c++17 -O2 -Iengine tests/test_navtex.cpp engine/navtexcore.cpp -o test_navtex
g++ -std=c++17 -O2 -Iengine tests/test_olivia.cpp engine/oliviacore.cpp -o test_olivia
g++ -std=c++17 -O2 -Iengine tests/test_wwv.cpp engine/wwvcore.cpp -o test_wwv
g++ -std=c++17 -O2 -Iengine tests/test_version.cpp -o test_version
g++ -std=c++17 -O2 -Iengine tests/test_txlevel.cpp -o test_txlevel
g++ -std=c++17 -O2 -Iengine tests/test_freqdigits.cpp -o test_freqdigits
# Memories file format and license-class rules (the arguments also check the built-in list:
# classes, nothing on FT8/FT4, and updating unedited old defaults)
g++ -std=c++17 -O2 -Iengine tests/test_memories.cpp -o test_memories && ./test_memories resources/memories-builtin.csv resources/memories-builtin-old.csv
# PSK Reporter datagrams (checked byte for byte against the sample on pskreporter.info/pskdev)
# and callsign spotting in decoded text
g++ -std=c++17 -O2 -Iengine tests/test_pskreporter.cpp -o test_pskreporter
# Log uploads: QRZ form encoding and replies, TQSL command line and status codes
g++ -std=c++17 -O2 -Iengine tests/test_upload.cpp -o test_upload
# Squelch: open delay / hang / 0 = off, RTTY and PSK on noise and weak signals, the dB scales
g++ -std=c++17 -O2 -Iengine tests/test_squelch.cpp engine/pskcore.cpp engine/rttycore.cpp engine/rttytxsource.cpp -o test_squelch
# MT63: loopback in every mode, sample rates, weak signal, squelch (no junk on noise or other modes)
g++ -std=c++17 -O2 -Iengine tests/test_mt63.cpp engine/mt63core.cpp engine/mt63/dsp.cxx engine/mt63/mt63base.cxx engine/oliviacore.cpp engine/pskcore.cpp -o test_mt63
# RSID: fldigi's code words, bursts at several rates/frequencies/sidebands, no false IDs
g++ -std=c++17 -O2 -Iengine tests/test_rsid.cpp engine/rsidcore.cpp engine/spectrum.cpp engine/pskcore.cpp -o test_rsid
# WSPR: messages, encode -> modulate -> noise -> resample -> decode (SNR, DT, frequency),
# Type 2/3 messages and hashes, free offsets, hopping schedule, TX %, WSPRnet queries
g++ -std=c++17 -O2 -Iengine tests/test_wspr.cpp engine/wsprcore.cpp engine/wsprd/*.cpp -o test_wspr

# End to end against a mock of wfview's TCI server (~3 min); --new runs only the
# newer parts (WAV playback into the newer decoders, logs, Olivia / SSTV / AMTOR
# transmit, Tune and CW ID)
qmake ../tests/test_e2e.pro && make && g++ -std=c++17 -O2 ../tests/fake_tqsl.cpp -o fake_tqsl
QT_QPA_PLATFORM=offscreen ./test_e2e
```

`test_e2e --serve 50001` runs only the mock server, which plays an RTTY station
(2125/2295 Hz), a BPSK31 station (1000 Hz) and a Feld Hell station (500 Hz), decodes the
RTTY, BPSK31 and CW you transmit, and reports TX power from your audio, so the program can
be tried without a radio.

## Notes on wfview's TCI server (v3, November 2026)

Found while writing this client; the client works around each one.

- **RX audio header channel count.** wfview announces `audio_stream_channels:2` and sends
  interleaved stereo (2048 frames = 4096 floats per packet), but writes `channels = 1` in
  every RX audio frame header. A client that trusts the header reads the stream as mono at
  half speed. This client trusts the announced count when the server is wfview.
- **TX audio format is fixed.** wfview's TCI TX input expects 48 kHz stereo float32 and
  ignores the TX frame's header (rate, channels). This client always sends that.
- **TX pacing.** wfview sends a TX chrono frame only in front of each RX audio frame. If
  RX audio stopped while transmitting, so would the chrono frames; this client then paces
  TX audio from its own clock (the status bar says which is in use).
- `modulations_list` reports FM as `FM`, but `modulation` reports it as `nfm`.
- TCI has no command for the radio's CW pitch, so the CW decoder has no "follow radio
  pitch" mode here.
- **No CW keying over TCI.** wfview's TCI server does not implement `cw_macros`, so CW
  transmit here is audio CW.
- **DIGU/DIGL always mean Data 1.** A TCI `modulation:0,digu;` puts the radio in USB with
  data mode D1, even if it was in D2 or D3. On a radio whose D1 input is not the one wfview
  feeds, transmit goes silent. Set the data mode in wfview instead.
- **Silent RX audio.** Seen once (wfview 3.00, IC-7610, November 2026): wfview kept sending
  RX audio packets at the normal rate, but every sample was zero (`peak -120.0 dBFS`,
  empty waterfalls). Restarting wfview cured it. The status bar now says so (not while
  transmitting: wfview sends zeros for receive audio then, and that is normal).
- **`tx_sensors_enable` argument order.** The TCI spec is `tx_sensors_enable:true,<ms>;`;
  wfview 3.00 reads the enable flag from the second argument, so this client sends
  `tx_sensors_enable:0,true;` to wfview. wfview then sends `tx_sensors` with its ALC meter
  in the "mic" field.

## Background

It started as the CW decoder and RTTY window proposed for wfview v3 (wf-group/wfview PRs
#151 and #152). On #152, Elliott (W6EL) suggested building it as a separate program that
talks to wfview over TCI, so wfview v3 can ship without it and wfview's TCI server gets a
real client to test against.

## Easter egg

Options > *STANAG 4481 Easteregg* (on by default). NATO STANAG 4481 FSK shore-to-ship
broadcasts (usually 75 baud, 850 Hz shift; the RTTY tab has a preset for it) are synchronous
and encrypted, so an RTTY decoder prints random characters and the stop bit is right only
about half the time; amateur RTTY always has its stop bits. When the RTTY tab has been locked
on such a signal for 10 seconds and at least 10 characters look like that, it prints one
line in tribute to the 1991 Mega Drive port of Zero Wing, then carries on with the random
characters. Once per signal; it is not written to the session log and never reaches PSK
Reporter or GridTracker.

## Future

- CW with the radio's own keyer once wfview's TCI server supports `cw_macros`.
- More modes (QPSK31/63, MFSK16/32, DominoEX/THOR are candidates), each as a Qt-free
  engine with tests, the same way the others are written. WEFAX transmit. CHU as a second
  time source.
- The goal is for this to become part of wfview if it fits the wfview team's plans: as a
  companion program in the wf-group organization, as a second program built from the
  wfview tree, or folded back into wfview itself after v3. The engines have no Qt
  dependency and the code follows wfview's `.clang-format`, Qt version, qmake build and
  GPLv3 license, so any of these should be straightforward.

## License

GPLv3 (see LICENSE). The CW and RTTY engines come from the author's wfview work. The PSK,
Feld Hell, SSTV, WEFAX and WWV engines and the 5x7 Hell font were written for this program
(SSTV and WEFAX mode timings from the published specifications and QSSTV's tables; WWV
from NIST's published format). `engine/ggmorse` is ggmorse by Georgi Gerganov, MIT
license (see its LICENSE). `engine/jalocha` is Pawel Jalocha's Olivia/Contestia MFSK
library as distributed with fldigi (GPL); the NAVTEX decoder follows fldigi's (GPL) - see
`engine/jalocha/README-WFVIEW.txt`. `engine/mt63` is Pawel Jalocha's MT63 code (Copyright (C)
1999-2004 Pawel Jalocha SP9VRC, 2007-2011 Dave Freese W1HKJ) as distributed with fldigi
(GPLv3 or later) - see `engine/mt63/README-WFVIEW.txt`. `engine/rsidcore` is adapted from fldigi's RSID code
(`src/rsid/rsid.cxx` and `rsid_defs.cxx`, Copyright (C) 2008-2012 Dave Freese W1HKJ, Stelios
Bounanos M0GLD and John Douyere VK2ETA, GPLv3 or later): the ID table, the GF(16) table, the
encoder and the receive method are theirs. `engine/wsprd` is the WSPR decoder `wsprd` from
WSJT-X (Copyright (C) 2001-2018 Joe Taylor K1JT, 2014-2018 Steven Franke K9AN, GPLv3), with
the sequential Fano decoder by Phil Karn KA9Q, in the library form of rtlsdr-wsprd
(Guenael Jouchet VA2GKA, GPLv3); FFTW was replaced by a small built-in FFT and the hash table
moved into memory - see `engine/wsprd/README-WFVIEW.txt`.
