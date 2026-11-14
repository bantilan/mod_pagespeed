- **Single-process reloads degrade safely.** A reload with `master_process
  off` is refused at module init (optimization stays off until restart),
  and uninitialized cycles decline requests instead of dereferencing unset
  factories, timers, and caches.
