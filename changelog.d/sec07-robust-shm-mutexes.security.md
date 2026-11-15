- **Shared-memory mutexes survive a holder that terminates.** Mutexes are
  robust where the platform supports it (Linux, FreeBSD): a lock operation
  takes over an abandoned mutex instead of blocking forever, marks it
  consistent, and notifies a recovery handler. SharedMemCache retires a
  sector taken over this way instead of walking possibly half-updated
  structures, and writers stop waiting after one second for a reader that
  terminated mid-read instead of spinning forever. A server that hangs at
  upgrade time needs a full stop and start to pick this up.
