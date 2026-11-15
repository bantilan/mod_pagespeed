- **A resource's lazy gzip decompression is serialized.** Concurrent first
  reads of one gzip-encoded input (rewrite thread and low-priority rewrite
  thread) can no longer inflate into the same buffer together.
