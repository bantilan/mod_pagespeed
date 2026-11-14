- **Automatic gzip setup resets every configuration cycle.** The rollback
  record no longer carries slots across reloads (use-after-free through
  freed configuration memory), and a rollback in one cycle no longer
  disables the next.
