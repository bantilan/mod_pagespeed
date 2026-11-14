- **Repeated info lines are deduplicated in the message buffer.** An identical
  info line (same file, line, and text) is kept once per minute; repeats are
  counted and reported with the next kept line ("(repeated N times)").
  Warnings and errors are never deduplicated.
