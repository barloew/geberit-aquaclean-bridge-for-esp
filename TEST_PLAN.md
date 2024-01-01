# Field test plan — Series 250 bridge (Alba)

**You do not need to be technical to help.** This guide walks you through everything: flashing the firmware,
reading the log in your web browser (nothing to install), and what to try. Follow it top to bottom and copy
what you see back to us. Please report in English.

## What this firmware can do today — and what it can't (yet)

This is **experimental** and has **never been run against a real Alba** — you may be the first. So be realistic
about what "working" means right now:

- **What we're trying to prove:** that the bridge can sit invisibly between your Alba and the devices that talk
  to it (the remote and the Geberit app), and **log every action by name** so we learn the device's language.
- **What it does _not_ do yet:** it does **not** give you app-style control from your phone, your home
  automation, or HomeKit. That is the roadmap (see [`VISION.md`](VISION.md)), and it only becomes possible once
  these tests succeed.

So a good test result is: it pairs, the remote and app still work through it, and the log shows what they do by
name. **And if it fails, a complete log of the failure is exactly as valuable** — that is how we fix it.

## Before you start

Do the whole install **and setup** first, exactly as in the [README](README.md#install): flash the board,
register your toilet, let the board reboot, confirm the clone, and **let the bridge pair with the toilet**. That
last part is the **first of two pairings** and belongs to setup: the bridge pairs itself to the toilet (the `+`
key is on the remote, not the toilet — you make the toilet pairable by power-cycling it). All of that —
including how to tell which device is your toilet — lives in the README and isn't repeated here. By the time
you reach this plan, the board's live log should already show it **connected to the toilet**.

The **second pairing** — your **remote to the bridge** — is step 2 below, because the bridge now pretends to be
the toilet and your remote has to be bonded to it before the remote can be tested.

Keep the board's web page (**http://192.168.4.1/**) open throughout; the steps below use its **Test** and
**Live log** sections. The top of the page shows a **Bridge health** line — handy to glance at as you go.

A few things that save confusion (all normal, none are bugs):

- **Placement matters.** The Bluetooth link to the toilet must be strong — keep the board **close, with line of
  sight** (see the README's *Placement* note). A weak link connects and then times out, which looks like a fault
  but is just range; move the board closer.
- **During the bridge↔toilet pairing (setup), keep the remote off / away *and* the Geberit app closed**, so
  neither claims the toilet first — the toilet accepts only one connection at a time, and whoever connects first
  takes the slot.
- **A toilet power-cycle is a normal part of bring-up and recovery.** Switching the toilet off (wait 30 s) and on
  again is expected when things look stuck; it is not a test failure. If the **Bridge health** line ever says
  **needs power-cycle**, that is the bridge telling you to do exactly that.

## Step 1 — Confirm the bridge is connected to the toilet

Open the page (join `AquaClean-Setup`, go to **http://192.168.4.1/**) and look at the live log. Confirm it shows
the bridge **connected to the toilet** from setup. Note how that went: did it connect, how long did it take,
any retries or drops? This is the one thing everything else depends on. If it isn't connected, make the toilet
pairable again — switch the toilet **off, wait 30 seconds, then switch it back on** (per the manual), with the
**remote off / away and the Geberit app closed** so neither grabs the toilet first — and watch the bridge
connect in the log. (There is no button to hold on the toilet; the bridge does the pairing itself.)

## Step 2 — Pair your remote and the app to the bridge

The bridge advertises itself as the toilet, so to both your remote and the Geberit app this looks like pairing
to a toilet. Do **both** — you need each of them bonded to the bridge before its functions can be tested in
step 3. Watch the live log throughout and note how each one went; if either won't bond, say so — that itself is
a key result.

**a) The remote.** Follow the normal remote-pairing procedure from your Geberit manual: with the remote switched
off, **hold `+` for about 20 seconds** until it blinks blue, then shows green for ~3 seconds when it's bonded.
The `+` is pressed on the **remote** by you; the bridge accepts the bond.

**b) The Geberit app.** The app walks you through its own pairing flow — follow what it shows on screen. Two
things to get right:

- **If the app was already paired with your real toilet, remove that first.** In the app, tap your toilet,
  **swipe left**, and tap the **trash-can** icon to delete it. Otherwise the app keeps reconnecting to the real
  toilet instead of the bridge.
- Then tap the **`+`** icon to add a device and follow the app's proposed flow to register the toilet — which is
  now the bridge. **If the flow asks you to switch the toilet off first, skip that step** — the bridge is
  already on and advertising, so there's nothing to power down.

## Step 3 — Run through the Alba's functions (the page guides you)

For every item: click **Press now** on the page's **"2. Test the toilet"** section (**Undo** if you
misclick), then immediately do it on the remote or in the app. The bridge drops a labelled marker in the log so
each command lands right under its name — no typing, no guesswork. If an item logs **nothing**, that's a useful
result too; note it.

Do it in this order — it follows how the remote actually works.

**3a. The remote — sit on the seat first** (there's a weight sensor; the shower won't start otherwise, and that
is the device, not a bug):

1. **Shower on** — short-press **`<Shower>`**. The shower starts, always *with* the oscillating arm movement.
2. **Jet strength** — while it's running, press **`<+>`** then **`<–>`**. Strength changes in 5 steps.
3. **Oscillation toggle** — while it's running, press **`<+>` and `<–>` together** to switch the oscillating
   movement off, and again to switch it on.
4. **Shower off** — press **`<Shower>`** again.

**3b. The remote — maintenance (optional, you don't have to complete a cycle):**

5. **Clean the shower head** — press **`<Shower>` + `<+>` together**; the shower arm slides fully out. A press of
   **`<Shower>`** retracts it.
6. **Descaling** — press **`<Shower>` + `<–>` together and hold ~5 s** to start it (you can cancel in the app).
   Just capturing the start is enough; say so if you let it run.

**3c. The Geberit app — the same session, app-based.** With the app paired to the bridge (step 2), do the same
functions from the app so we see them from that side too: **shower start/stop**, **jet strength (1–5)**,
**oscillation on/off**, **shower-arm position (1–5)** (this one is app-only — it isn't on the remote), **water
temperature (1–5)**, **water heating on/off**, and **descaling** (*Maintenance & care → Descaling*). Trigger
each control with its **Press now** marker first.

**3d. The Geberit app — your profile.** The Alba's **remote** has no profile keys and the toilet keeps no
profiles of its own, but the **app has profiles** (every model does): you set one up in the app and, when you
apply it, the app sends all its settings to the toilet **in one go**. Test that:

1. In the app, open your **profile / personalisation** page, set it how you like, and **save / apply** it —
   put a single **Press now** marker just before you apply. We want to see the bridge log that whole batch of
   settings arriving together.
2. Screenshot the **profile / personalisation** page and the **device-info / error** pages.

Note how the app shows profiles (one personal profile, or several) — that tells us what your Alba's app
actually exposes.

## Step 4 — App *and* remote at the same time

This is important and easy to miss: with the **app still connected**, also use the **remote**. The bridge is
meant to serve both at once. Note whether both keep working together, or whether one drops the other, and copy
anything unusual from the log.

## Step 5 — Fault-log capture *(optional — only with the Event-Storage test build)*

**Skip this step unless you were given the "Event-Storage (ES) build"** — the special firmware for this test.
You can tell: with that build, the live log prints an **`ES:`** line when the bridge connects to the toilet (and
the page shows a small **Fault log** panel). If you don't see that, you have the standard build — go straight to
Step 6.

What we're proving here: the Alba keeps its own **fault log** inside the toilet. This step checks two things —
that the bridge can **read the log that's already there** when it connects, and that it receives a **brand-new
fault the instant it happens** (the "live" part), with a time attached. A real fault captured this way is the
single most useful thing we can get for this feature.

**5a — Baseline (what's already logged).** With the bridge connected, look at the **Fault log** panel (or the
`ES:` lines in the live log). Note what's there now: it may be empty, or list earlier faults with dates. Copy
whatever it shows.

**5b — Confirm it's listening live.** The ES build subscribes to live fault updates by itself on connect — you
should see a line such as `ES: live (subscribed)` shortly after it connects. If you **don't** see it, just note
that and skip to Step 6; the live part isn't running and the rest of 5 won't produce anything.

**5c — Trigger a fault (safe and reversible).** The dependable one is a short water-starve of the shower pump —
the exact fault we already confirmed on the sister model (the pump runs a moment with no water and reports
"current interrupted"):

1. Close the **water shut-off valve** on the toilet's supply line (the small angle valve behind/beside it).
2. Start a **shower** cycle from the remote and let it run **only a few seconds** — just long enough for the
   toilet to notice there's no water. **Don't keep it running.**
3. Stop the shower.
4. **Re-open the water valve straight away.**

The toilet should now show a fault (an error light/indication on the device or remote). This is harmless done
**once, briefly**: it's transient and clears on the next normal cycle or a power-cycle — the sister model
recovered fine. Keeping a dry pump running for a long time is the only thing to avoid, so keep it to a few
seconds. *(If your Alba has another fault you already know how to trigger safely, that works just as well — any
real fault is fine.)*

**5d — Catch it live.** Watch the log the moment the fault appears. Within a second or two you should see a
**new fault-log entry arrive on its own** — a fresh line in the Fault log panel / an `ES: new entry …` line.
Copy it **exactly**, including any number/code and time shown. This arriving by itself (without you pressing
anything) is the whole point of this step.

**5e — Does the time look right?** Note whether the new entry shows a sensible **date/time** (or a small offset
from "now"). If the log shows a time-sync line around the moment it connected, include that too.

**5f — Put it back.** Make sure the water valve is open again and run **one normal shower** to confirm the
toilet works. If the fault indication is still showing, power-cycle the toilet (off 30 s, on) to clear it.

Then go to Step 6 and save the log — the `ES:` lines must be in the saved file.

## Step 6 — Save the log (do this before anything restarts)

**This is the important one — grab the log before you power-cycle or reboot the board, or it's lost.**

On the page, under **Live log**, click **Download log (.txt)**. That saves everything captured since you opened
the page (markers from step 3 plus the toilet's responses) to a file you can attach to your report.

If the download button doesn't work on your device: tap/click inside the black log box, select all
(Ctrl/Cmd+A), copy (Ctrl/Cmd+C) and paste it into your report. On a phone: long-press in the box → Select all →
Copy.

Do this **now**, before step 7 — step 7 power-cycles the board, which clears the log.

## Step 7 — Stability (optional)

If you have time: leave everything running for a while (an hour or more) — and longer is better, because a link
that is healthy for minutes can still wedge after a day or two, which is exactly what we most want to catch. Note
any disconnects or freezes and whether it recovers on its own.

The bridge now **watches itself and tries to recover**: it reconnects on its own, and the **Bridge health** line
tracks its state (`ok` → `recovering` → `needs power-cycle`). If it reaches **needs power-cycle**, the bridge has
decided the toilet itself is stuck and only a power-cycle will clear it — switch the toilet off (30 s) and on. So
for this step, note **what the health line did over time** and whether it recovered without you — that is the
real result. Then power-cycle the board (and, if needed, the toilet) and confirm it pairs again. (Only after
you've saved the log in step 6.)

WiFi and BLE run together fine on this hardware — the main project's bridge already runs BLE alongside WiFi,
stably — so you can leave the log page on during this test. Only if you see something odd and want to rule WiFi
out entirely: press **Stop WiFi** on the page and repeat.

## What to send us

Post in the **Field testing** category in
[Discussions](https://github.com/barloew/geberit-aquaclean-bridge-for-esp/discussions) (choose *Series 250 /
Alba*) with:

- Your Alba model + article number and which board you used.
- How it went (a short "paired after ~5 s" / "jet-strength + logged nothing" / "app would not connect" is
  perfect), and anything from the test checklist that produced no log line.
- **The downloaded log file** (`aquaclean-log.txt` from step 6) covering the whole session.
- For the app: **a screenshot of each page** you opened, so we can match the log lines to the pages —
  especially any page whose controls didn't seem to log.
- **If you did the fault-log capture (step 5):** the baseline fault log, the new entry that arrived live
  (exact code/number and time), and whether the time looked right — plus which fault you triggered and how.

Describe what you **saw** (behaviour and log lines). You don't need to understand or include protocol internals.
If you think you've found a security problem or captured something sensitive, use the private channel in
[`SECURITY.md`](https://github.com/barloew/geberit-aquaclean-bridge-for-esp/blob/main/.github/SECURITY.md) —
never a public post.
