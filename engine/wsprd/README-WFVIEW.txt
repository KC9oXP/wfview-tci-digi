WSPR decoder used by wfview TCI Digi
====================================

Origin
------
wsprd, the WSPR detector/demodulator/decoder from WSJT-X
  Copyright (C) 2001-2018 Joe Taylor, K1JT
  Copyright (C) 2014-2018 Steven Franke, K9AN
  Sequential Fano decoder (fano.c): Copyright (C) 1994 Phil Karn, KA9Q
  License: GNU GPL v3 (see the file headers)

taken in the library form maintained in rtlsdr-wsprd
  https://github.com/Guenael/rtlsdr-wsprd (commit 1ca9b83, 2026-03-05),
  minor modifications Copyright (C) 2016 Guenael Jouchet, VA2GKA, GPL v3.

Changes for wfview TCI Digi (2026-11, GPL v3)
---------------------------------------------
- .c files renamed .cpp and compiled as C++ (casts on malloc/calloc, no VLAs) so
  MSVC builds them; everything is in namespace wsprd.
- FFTW removed: wsprd.cpp has its own 512-point radix-2 FFT (fft512).
- The power-spectrum array and the hash tables are on the heap, not the stack.
- The hash table is passed in by the caller (decoder_options.hashtab/loctab) and
  kept between slots in memory; nothing is read from or written to the current
  folder (no hashtable.txt, no fftw_wisdom.dat). The program saves it itself.
- metric_tables is static; subtract_signal2's six 45000-sample buffers are
  std::vectors (they overflowed a 1 MB Windows thread stack).

engine/wsprcore.{h,cpp} wraps it (audio front end, message parsing, encoder
checks, transmit modulator, free-offset finder, band hopping, WSPRnet queries).
