- **Expensive admin reads run one at a time.** Graphs/console JSON, config
  dumps, message history, cache lookups, and the global cache listing each
  take a per-leaf slot (short wait, 429 when busy), and statistics-log queries
  are bounded in granularity, time range, and variable count.
