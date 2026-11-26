# Protocol notes

These are the packets `aula-s75` sends. They are a small subset of the AULA S75 Pro
vendor protocol, which was reverse-engineered by others (see [Credits](#credits)).
Everything here was checked on a real S75 Pro, on macOS, in November 2026.

## Devices

| Link | VID:PID | HID collection | Reports |
|---|---|---|---|
| USB cable | `0c45:800a` (SONiX, product string `AULA-S75Pro`) | usage page `0xFF13`, usage `0x01` | 64-byte feature, no report ID |
| 2.4 GHz dongle | `05ac:024f` (product string `2.4G Dongle`) | usage page `0xFF60`, usage `0x61` | 32-byte output and input, no report ID |

The cable also exposes usage page `0xFF68` (a 4096-byte output report, used for screen
image upload), a boot keyboard, and a composite consumer/mouse/keyboard collection.
The dongle also exposes usage page `0xFF59` with 64-byte reports, which `aula-s75` does not use.

`05ac:024f` is Apple's vendor ID with a generic keyboard product ID. Other keyboards
(Keychron, for example) use the same pair, so matching on the `0xFF60` collection matters.

## USB cable

Every packet is a 64-byte feature report (`SET_REPORT`). After each control command the
host waits about 35 ms and reads one feature report (`GET_REPORT`) as an acknowledgement.

Control command:

```
04 <op> 00 00 00 00 00 00 <count lo> <count hi> 00 … 00
```

`count` is the number of 64-byte data packets that follow. The keyboard acknowledges by
echoing the command with byte 3 set to `01`.

### Set the clock

```
-> 04 18 00 …                     begin session            <- 04 18 00 01 …
-> 04 28 00 00 00 00 00 00 01 …   clock, 1 packet follows  <- 04 28 00 01 …
-> 00 01 5a YY MM DD hh mm ss 00 WD 00 … 00 aa 55          <- echo of the same packet
-> 04 02 00 …                     end session, save        <- 04 02 00 01 …
```

| Byte | Value |
|---|---|
| 0 | `00` |
| 1 | `01`, clock slot |
| 2 | `5a` |
| 3 | year − 2000 |
| 4–8 | month, day, hour, minute, second (local time, 24 h) |
| 9 | `00` |
| 10 | weekday, `0` = Sunday |
| 62–63 | `aa 55` |

## 2.4 GHz dongle

Every packet is a 32-byte output report:

```
cmd len seq data[28] checksum
```

`checksum` is the sum of the first 31 bytes, mod 256. The keyboard answers with an input
report whose first three bytes echo `cmd len seq`. While it is asleep it does not answer,
so a key press is needed first.

### Read the battery

```
-> 20 01 00 00 … 00 21
<- 20 01 00 <percent> …
```

The keyboard reports the level only over 2.4 GHz, never over the cable. A reading of `0`
means either an empty battery or no reading.

### Set the clock

Command `0c`, length `10`. The data are the first 14 bytes of the USB clock packet,
then `aa 55` at data bytes 14–15.

```
-> 0c 10 00 00 01 5a YY MM DD hh mm ss 00 WD 00 00 00 aa 55 00 … 00 <checksum>
```

## Credits

- [paulcoding810/open-aula](https://github.com/paulcoding810/open-aula): the S75 Pro
  protocol, taken from the vendor's Windows driver (`PROTOCOL.md`, `REVERSING.md`).
- [w33ble/aula-f75-max-control](https://github.com/w33ble/aula-f75-max-control): the same
  `0c45:800a` clock sequence on the AULA F75 Max, including the ack check.
- [livvaa/AulaToolkit](https://github.com/livvaa/AulaToolkit): battery handling over the
  dongle, and the observation that the cable reports no level.
