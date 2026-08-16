***English** · [Français](README.fr.md)*

# Silicon Bubbles

**A pocket iMessage client** — sibling of [Silicon Casino](https://github.com/chatelp/geek-casino-cardputeradv).
M5Stack Cardputer ADV · ESP32-S3 · 240 × 135 screen · 56-key keyboard.

![Silicon Bubbles — list, conversation, compose, settings](docs/images/hero.png)

A lightweight client for [BlueBubbles](https://bluebubbles.app), the open
source (Apache-2.0) iMessage bridge that runs on a Mac. The Cardputer polls
the server's REST API over HTTPS, shows your conversations as real chat
bubbles, and sends messages from its physical keyboard. No app, no account,
no cloud in between — your Mac and your device.

![Type, send, get a tapback, get a reply](docs/images/loop.gif)

> Every image here comes straight from the simulator — never a mockup.
> `pio run -e sim && .pio/build/sim/program --screens captures/screens`
> then `python3 scripts/readme_images.py`.

---

## What it does

| | |
|---|---|
| **Conversations** | Merged list (one entry per person, not per thread), preview, unread dot |
| **Bubbles** | Left/right alignment, tail, author grouping, centred time separators |
| **Emoji** | 42 pixel glyphs covering 68 codepoints — never a tofu box |
| **Tapbacks** | Reactions shown as a pill straddling the bubble corner |
| **Sending** | Private API or AppleScript, with an honest *sent-but-unconfirmed* state |
| **Sounds** | Five struck-bar timbres, not beeps — each switchable |
| **Bilingual** | English and French, switchable on the device or in the portal |

Text only, by design: attachments show as `[attachment]`. Polling only —
no Socket.IO, no webhooks.

---

## Setup, in a browser

On first boot the device raises an access point — join `SiliconBubbles`
(password `bluebubbles`) and open `http://192.168.4.1`. Afterwards the
same form lives at `http://cardputer.local` on your network.

Everything is also editable **on the device itself** (Info → `s`):
numeric settings change live; WiFi (with a **network scan** — pick from a
list, type the password) and server address/password go through the same
safe path as the portal — saved to a copy, then the device restarts. The
two paths cannot overwrite each other silently: the portal refuses
(HTTP 409) a form rendered before a setting changed on the device.

**QR codes** do the heavy lifting on a phone: the first-boot screen shows
a QR that joins the setup WiFi directly (`OK` key), and the settings offer
a QR that opens the portal at the device's current address.

**SD backup** (settings → Backup to SD): configuration, pinned
conversations, sync marker and contact names go to `/SiliconBubbles/` on
the microSD card — flashing a firmware never has to cost your setup
again, and a fresh device offers to restore it at first boot (`b`).
Passwords are stored **in clear** on the card: it is your card, treat it
accordingly.

**Contact names**: BlueBubbles does not push the Mac's contacts, so the
list shows raw numbers. Press `n` on a conversation to give it a local
name — stored on the device, backed up to SD, never sent anywhere.

Your server password lives only in the device's NVS. It is never a
firmware default and never enters this repository.

---

## Keys

| Key | List | Conversation | Compose | Settings |
|---|---|---|---|---|
| `;` `.` | move | scroll, bubble by bubble | — | change field |
| `,` `/` | — | — | — | change value |
| `Enter` | open | write | send | — |
| `` ` `` | info | back | cancel | save and exit |
| `r` | resync | — | — | — |
| `n` | name the contact | — | — | — |
| `Enter` *(settings)* | | | | open: WiFi scan, server, QR, SD backup |
| `p` / `s` | *(info screen)* test server / settings | | | |

---

## Build

```bash
pio run -e cardputer-adv -t upload
```

```bash
pio test -e test-native
```

The pure logic — HTTP stream decoding, UTF-8 and emoji segmentation,
scroll stops, tapback parsing, conversation merge keys — lives in headers
under `include/` and is covered by 34 native tests that run on your
machine, with no device attached. On a screen this small, that test bench
is how correctness gets proven.

---

## Requirements

- A working BlueBubbles server (Mac + iMessage), reachable over LAN or
  HTTPS (Cloudflare, ngrok, dynamic DNS)
- An M5Stack **Cardputer ADV** — the original Cardputer should work, same
  library, but is untested

---

## Hardware truths

The ESP32-S3FN8 has **no PSRAM**. What governs every decision here is not
the 8 MB of flash or the ~230 KB of free heap, but the **largest
contiguous block the heap can still hand out in flight: about 31 KB**.
Hence responses parsed straight from the TLS socket without ever
buffering a body, paged requests, and a ratified doctrine — *speed and UX
before exhaustiveness* — that any new feature must answer to.

The traps that cost the most are written down in
[docs/02](docs/02_ARCHITECTURE.md), including the one that took the
longest to find: on TLS, `NetworkClient::readBytes` treats a momentary
empty socket as a fatal error, silently truncating every response bigger
than the decrypted buffer.

---

## Screens

| | | |
|:--:|:--:|:--:|
| ![](docs/images/01-chats.png) | ![](docs/images/02-messages.png) | ![](docs/images/04-compose.png) |
| conversations | bubbles, emoji, tapbacks | compose |
| ![](docs/images/12-wifi-scan.png) | ![](docs/images/14-qr-join.png) | ![](docs/images/16-about.png) |
| WiFi scan, on the device | QR: the phone joins the setup AP | about |

---

## Error codes

Plain problems show a plain message (“No WiFi”). Anything involving the
BlueBubbles server shows a **stable code** you can look up here — the
technical detail always goes to the serial console (115200), never to the
screen.

| Code | Message | Likely cause — what to check |
|---|---|---|
| E11 | server unreachable | Wrong address, server down, or the device can't reach it (VPN, VLAN, firewall). Try the address in a browser on the same network. |
| E13 | server not answering | Connected, but no reply in time. Server overloaded or link very slow — usually transient. |
| E20 | server password refused | The password in the portal doesn't match the BlueBubbles server password (Mac app → API). |
| E21 | error on the server side | The server itself failed (5xx, code shown). Check the BlueBubbles server logs on the Mac. |
| E22 | BlueBubbles API not found | The address answers, but it isn't a BlueBubbles API: wrong URL path, reverse-proxy misroute, or a very old server. |
| E23 | unexpected HTTP reply | Something between the device and the server answered instead of it (captive portal, proxy). Code shown. |
| E30 | absurd response size | The server announced an absurdly large reply. Report it — the device protected itself. |
| E31 | response cut short | The reply died mid-transfer: unstable link between device and server. Usually transient; recurring → check the proxy. |
| E32 | response exceeds memory | Valid reply, too big for the device's RAM. Lower “messages per conversation” in the portal. |
| E40 | device memory is full | Momentary. The device retries on its own; reboot if it persists. |
| E50 | invalid server address | Must start with `http://` or `https://`, no trailing path. |

The source of truth is [`include/bb_errors.h`](include/bb_errors.h) — keep
this table in sync with it.

---

## Documentation

The project documentation is in French.

| | |
|---|---|
| [CLAUDE.md](CLAUDE.md) | project doctrine, scope guardrails |
| [docs/01](docs/01_DECISIONS.md) | decision journal, dated and ratified |
| [docs/02](docs/02_ARCHITECTURE.md) | architecture, measured memory budget, hardware traps |
| [docs/04](docs/04_ANALYSE_CHARGEMENT.md) | why `chat/query` is unusable, and what replaces it |
| [docs/05](docs/05_DESIGN.md) | the “ink and bubble” visual language |
| [docs/06](docs/06_DOCTRINE_MEMOIRE.md) | memory and speed doctrine |

---

## Licence

[MIT](LICENSE) — take it, learn from it, build on it.

BlueBubbles is an independent Apache-2.0 project: this repository reuses
none of its code and only consumes its documented API. iMessage and Apple
are trademarks of Apple Inc.; this project is affiliated with neither
Apple nor BlueBubbles. Third-party libraries (M5Unified, M5GFX,
ArduinoJson) keep their own licences.
