# Test keys

These keys exist only for the simulator and its tests. They are public on
purpose and must never go into a real Buddy.

- `link-key.txt`: the simulated Buddy's link key (64 hex digits).
- `update-key.pem`: signs the fake firmware images of the update tests;
  `update-key.pub.pem` is the public half the simulated Buddy holds.
- `wrong-update-key.pem`: a key the simulated Buddy does not trust, for the
  "refused" test.

A Buddy made by `mbsim.Buddy()` starts with both keys in `/keys.dat`, as one
that went through setup. `Buddy(keys=False)` starts without, like a board
fresh from the installer.

Only `link-key.txt` is in git. The three `.pem` files are made by `mbsim.py` the
first time it runs (the repository keeps `*.pem` out on purpose), from fixed
numbers, so they are the same on every machine.
