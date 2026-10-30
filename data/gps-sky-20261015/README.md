# The bench GPS under open sky, 2026-10-15 06:51–16:53 UTC

The read-only logger asked the ADC board for its PQ timeline, the broker's clock and the satellite count every 2 minutes. The raw records are in `timeline.log`; the board's name and position are not in them.

The day had several kinds of interruption:
- the 20 planned reboots of #195, between 08:00 and 10:45 UTC;
- the release 20 update;
- the Mac's sleep from 15:12 to 16:03, during which no record was taken;
- the release 21 update at 16:39–16:45.

Before the logger started, the antenna sat in its poor, nearly covered spot (on purpose).

| Period | Uptime covered | GPS losses bridged by holdover | Rate | Expired holdovers | Error when the GPS returned |
| --- | --- | --- | --- | --- | --- |
| Overnight, antenna covered (up to 06:51) | 8.6 h | 44 | 5.1/h | 0 | 93–252 ns in the last ones |
| 08:24–09:44 | 1.32 h | 3 | 2.3/h | 0 | 19, 112, 271 ns |
| 09:46–10:18 | 0.54 h | 1 | 1.9/h | 0 | 175 ns |
| 10:42–16:43 | 5.97 h | 10 | 1.7/h | 0 | 31–1324 ns (9 of the 10 seen) |

- **Holdovers.** Under open sky the board loses the GPS about a third as often. No holdover ran out, and the clock never stepped.
- **Reacquisition.** The errors when the GPS came back have a median of 112 ns and a range of 19–1324 ns. They come from the PRU tick-to-UTC model's prediction at the moment the GPS returns.
- **Satellites.** Between 5 and 12, with a median of 9. The count alone does not explain the losses.
- **The crystal.** The measured sample period went from 31742.712 to 31742.796 ns during the afternoon boot, a spread of 2.7 ppm. It follows the temperature: this is the drift that holdover has to bridge, relevant to E4 and P7.

The logger only kept counters, so this does not measure the holdover error against the length of the outage. That is E4.
