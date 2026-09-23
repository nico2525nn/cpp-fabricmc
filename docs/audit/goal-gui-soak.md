# Goal GUI and soak boundaries

## GUI capability probe

The headless environment was recorded in
`/tmp/grok-goal-e8af7c070433/implementer/gui-capability-final.log` and the
follow-up probe
`/tmp/grok-goal-e8af7c070433/implementer/gui-capability.log`:

```text
DISPLAY=
WAYLAND_DISPLAY=
XDG_SESSION_TYPE=tty
minecraft-launcher=UNAVAILABLE
prismlauncher=UNAVAILABLE
multimc=UNAVAILABLE
java=/usr/bin/java
xvfb-run=/usr/bin/xvfb-run
glxinfo=UNAVAILABLE
```

There is no installed launcher or authenticated vanilla client session, and no
display is configured. Gradle's cached client jars are build inputs only; they
do not provide launcher assets, login, or a retained GUI run. GUI rendering,
first-login screens, keyboard/mouse input, resource-pack presentation,
screenshots, and visual parity are **UNAVAILABLE**. `xvfb-run` alone is not a
Minecraft client, so protocol evidence is not promoted to a rendering claim.

## Soak evidence

The requested command was run with owned-process cleanup:

```text
timeout --foreground --kill-after=5 8000 python3 tests/soak_test.py --duration 7200 --binary ./build/cppfm
```

Artifact: `/tmp/grok-goal-e8af7c070433/implementer/soak-7200-final.log`.
The run did **not** complete: the server exited with `-9` at `t=1200s`, all
five bots observed socket closure, keepalives were `605` versus the expected
`960`, and the RSS post-fill growth check was `12.0%` (within its `15%` limit).
The final `pgrep` check found no remaining `build/cppfm` process. This is a
reproducible long-run failure/boundary, not accepted soak coverage.

A separate bounded comparison run is retained as
`/tmp/grok-goal-e8af7c070433/implementer/soak-300-final.log`. It passed with
`150` keepalives, `0` disconnects, `2,895` actions, and `1.0%` post-fill RSS
growth. It demonstrates short-run stability only and does not replace the
failed two-hour gate.
