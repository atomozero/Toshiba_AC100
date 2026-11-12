# Using RenkuOS on the Toshiba AC100

```
            _____________________________________________
           |  ._______________________________________.  |
           |  |                                       |  |
           |  |     ____                 _            |  |
           |  |    |  _ \ ___ _ __  | | ___   _       |  |
           |  |    | |_) / _ \ '_ \ | |/ / | | |      |  |
           |  |    |  _ <  __/ | | ||   <| |_| |      |  |
           |  |    |_| \_\___|_| |_||_|\_\\__,_| OS   |  |
           |  |                                       |  |
           |  |        on the Toshiba AC100           |  |
           |  |    Tegra 2 - 2 x Cortex-A9 - 512 MB   |  |
           |  |_______________________________________|  |
           |                  T O S H I B A              |
           |_____________________________________________|
          /  __________________________________________  \
         /  |Esc|F1|F2|F3|F4|F5|F6|F7|F8|F9|F10|F11|Del|  \
        /   |`  |1 |2 |3 |4 |5 |6 |7 |8 |9 |0 |-  |<-- |   \
       /    |Tab |Q |W |E |R |T |Y |U |I |O |P |[ |]   |    \
      /     |Caps |A |S |D |F |G |H |J |K |L |; |Enter |     \
     /      |Shift |Z |X |C |V |B |N |M |, |. |Shift   |      \
    /       |Fn|Ctrl|Alt|      space      |Alt|<|v|>   |       \
   /                                                            \
  /                     [                  ]                     \
 /__________________________[__________]__________________________\
```

This guide is for after the installation: what the AC100 can do with RenkuOS and how
to use it day by day. To write the card and start the system, see
[INSTALL.md](INSTALL.md).

## The keyboard and the touchpad

- **Brightness**: Fn+F4 darker, Fn+F5 brighter, or the slider in Preferences > Screen.
- **Touchpad on and off**: Fn+F9. The embedded controller remembers it, also after
  switching off: if the touchpad does not move the pointer, press Fn+F9 first.
- Closing the lid switches the backlight off; opening it switches it on again.

## Battery and power

RenkuOS on the AC100 takes care of the battery by itself:

- **The CPU clock follows the load.** Idle, the CPUs run at 456 MHz and 0.825 V; as soon
  as an application needs it (a video, a page loading) they go to 1 GHz in a twentieth of
  a second. Pulse and ActivityMonitor show the current clock.
- **Power saving mode**: in ProcessController (the CPU meter in the Deskbar) choose
  "Power saving". Idle, the CPUs then go down to 216 MHz and 0.75 V, and step up one
  rate at a time; good for reading and writing on battery. "Low latency" is the default.
- **The screen saver switches the panel off for real**, not only the backlight: set
  "Turn off screen" in Preferences > Screen saver to make the battery last longer while
  you are away (about 1.5 W less, a third of what the AC100 uses idle).
- The battery level is in the Deskbar (PowerStatus).

For tests, the governor can be stopped: `governor false` in
`~/config/settings/kernel/drivers/tegra20_cpufreq` keeps the CPUs at 1 GHz, and
`disabled true` turns the whole module off. Remove the file to go back.

## Network

- **Wi-Fi**: click the network icon in the Deskbar, or Preferences > Network, and choose
  your network. The red Wi-Fi LED on the front lights up while connected.
- **SSH**: the server is installed but does not start by itself.

## Sound

- **Output**: Preferences > Media. The speakers go quiet by themselves when headphones
  are plugged in. With an HDMI screen, the sound goes there too.
- **Bluetooth speakers and headphones**: pair them in Preferences > Bluetooth, then
  choose them as the audio output in Preferences > Media. If they go away, the sound
  comes back to the AC100's speakers, and goes to them again when they return.
- **Microphone**: the built-in one is next to the camera, above the screen. Its level is
  the **Boost** of "Toshiba AC100 audio" in Preferences > Media (24 dB by default; 18
  is quieter, 30 louder). The record gain there only changes the headset microphone.

## Bluetooth

Preferences > Bluetooth finds and pairs keyboards, mice, gamepads and speakers. A
gamepad (tested with an Xbox Wireless Controller) appears in Preferences > Input with its
battery level, and works in the games of SalaGiochi.

## Applications

WebPositive (web), LocalSend (files to and from phones and PCs), Genio (programming),
BePDF, WonderBrush (drawing), Atomo123, R Television, Cricket, a SIP softphone, and
SalaGiochi: it comes without games; drop the game files you own on "Add games", on the
desktop. In the Terminal: git, curl, ssh, nano, htop and the usual tools.

## Things that do not work yet

- The **webcam**: on our AC100 it does not answer on USB, most likely a hardware fault
  of the camera module or of its cable; tell us if yours does.
- 3D acceleration and suspend.
- **Restarting** does not come back by itself when U-Boot was loaded over USB: switch the
  AC100 off and start it again as in INSTALL.md.

## When something goes wrong

- The system log is in `/var/log/syslog` (Terminal: `tail -50 /var/log/syslog`).
- The log of the last start is also written to `AC100LOG.TXT` on the small FAT partition
  of the card, readable from any PC: useful when the AC100 did not get to the desktop.
- If the AC100 stops responding, hold the power button until it switches off, then
  start it again.

Problems, questions and stories of your AC100:
[issues](https://github.com/atomozero/Toshiba_AC100/issues).

What we measured and learnt while bringing RenkuOS to the AC100 (power use, the panel,
the CPU clock, the microphone, the webcam) is in
[docs/AC100_ENERGIA_E_SCOPERTE.md](docs/AC100_ENERGIA_E_SCOPERTE.md) (Italian).
