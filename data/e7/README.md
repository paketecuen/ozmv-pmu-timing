# E7: the acquisition's noise floor

Program: `analysis/e7_channel_snr.cpp`. Input: the release 17 WMU recording, 40 s with all 7 channels; `data/raw/r17-wmu.ogg` decoded to s32le. Channel 0 carries the mains. Channels 1 and 2 are the unconnected B and C voltage paths. The spectra are Blackman-Harris over 2^18 samples (8.3 s), and DC and every harmonic of the measured fundamental (±1.5 Hz) are excluded.

## Results

**Mains fundamental.** It sits at -8.0 dBFS, at 49.97 Hz, with THD 1.6 %.

**Floor of the unconnected voltage paths.** It is 77.2-77.5 dB below the mains fundamental over the full band, 0 to 15.75 kHz. Inside the PMU band (below 1 kHz), it is equivalent to white noise 74.9-76.0 dB below. That is the acquisition SNR at the operating level.

**Residual on channel 0 after removing the harmonics.** It is 67.2-67.5 dB below the fundamental, or 57.6-58.0 dB in-band. It contains the grid's own interharmonics and fluctuations, which are signal, not measurement noise. It is therefore only an upper bound on the acquisition noise.
