# Accord

Accord brings Discord to your PlayStation 5. Your servers, your channels, your messages, voice
chat and live streams — on the TV, driven with the DualSense in your hand, with no PC sitting in
the middle.

Accord is an **unofficial** client. It is not made, endorsed or supported by Discord Inc., and it
is not a system-level integration built into the console. You sign in with your own Discord
account; doing that from an unofficial app can break Discord's terms of service and put the
account at risk. Whether to use it is your call.

## What Accord does

**Sign in from the couch.** Accord puts a code on your TV. You scan it with the Discord app on
your phone and approve the login there — your password is never typed on the console and never
stored on it.

**Your servers, one at a time.** The rail on the left lists the servers you have joined; open one
and its channels appear with an unread marker on the ones you have not read yet.

**Read and write.** A channel opens as a normal chat view, and a message is written with the
on-screen keyboard, so you do not need a USB keyboard plugged into the console.

**Voice channels.** Join the channel you want, see at a glance who is in it and who is talking,
mute your microphone, and leave when you are done.

**Live streams.** When somebody goes live, Accord shows you their stream full screen and lets you
follow the viewers as they come and go.

**Played with the pad, not a mouse.** Moving around, going back, and writing all live on the
DualSense — including the on-screen keyboard, which opens wherever text can be typed.

**Made to be stepped away from.** Accord runs as a homebrew title, so you can leave it and come
back. The connection is kept by a small background service on the console, which is why the app
can shut down and reopen without logging you in again.

## What you need

- A PlayStation 5 on firmware 13.60 with homebrew enabled. Earlier firmware is not supported.
- A Discord account. Accord logs in as you, a normal user — not as a bot.
- Nobody watching over your shoulder while you type a message, ideally.

Accord is delivered as a `.ffpkg` package — the format homebrew titles on the console use.

## Where Accord is today

Accord is early work, and this README describes the app it is becoming rather than a finished
product. The interface exists and draws: all six screens — login, servers, channels, chat, voice
and stream — render, and the interface kit behind them already carries the controller handling
and the on-screen keyboard. The half that talks to Discord is still being built, and the console
title does not yet run this interface, so a login today does not reach the service. Development
happens in the open, and the commit history is the honest record of what runs.

## Licence

GPL-3.0-or-later — see `LICENSE`. The interface kit Accord draws with is
[ps5-homebrew-ui](https://github.com/blackbearreloaded/ps5-homebrew-ui), also GPL-3.0-or-later,
which is why the whole app carries that licence. Licences of every bundled component are listed in
`THIRD_PARTY.md`.

## Building it yourself

Developer documentation — how the app is put together, how to build it, and what each `make`
target does — lives in `docs/BUILD.md`.
