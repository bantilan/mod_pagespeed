- **Shared-memory runtimes are numbered atomically.** Concurrent factory
  constructions (e.g. nginx config reload) can no longer receive the same
  instance number and share segment namespaces.
