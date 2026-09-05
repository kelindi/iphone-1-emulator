## Tweet idea

> We got past iPhone OS 1.0 activation without modifying any Apple code. The work image seeds a cached `Activated` state and a locally signed activation record that Apple's own `lockdownd` verifier accepts.

## What you're seeing

No dedicated recording yet. The visible result is in `videos/browser_home_settings_safari_cpi1.mp4` (0:05–0:08): instead of an "Activate iPhone" lock screen, the home screen appears.

## ELI5 technical explanation

- When the real iPhone boots for the first time, `lockdownd` contacts Apple's activation servers over the internet and stores a cryptographically signed record proving the device is activated.
- In the emulator there is no internet or Apple server, so after a while `lockdownd` says "The activation record was not valid" and sets the activation state to `Unactivated`, which makes SpringBoard stop at the lock screen.
- We added a `--activate` option to `rootfs_work.py` that creates a local device certificate and activation record signed by locally generated keys, then writes the matching certificate data files into `/System/Library/Lockdown/` and the activation record to `/var/root/Library/Lockdown/`.
- Apple's unmodified `lockdownd` verifies the RSA/SHA-1 signature, sees the token is not a valid plist (which triggers the `Activated` path), and reports `Activated` to SpringBoard.
- We also seed a cached `data_ark.plist` with `ActivationState=Activated` for the early boot path so `lockdownd` doesn't even try to recompute before SpringBoard starts.

## Why it matters

Activation is gatekeeping. Without it the real SpringBoard cannot proceed past the lock screen. Solving it lets us boot the home screen while keeping every Apple binary untouched.
