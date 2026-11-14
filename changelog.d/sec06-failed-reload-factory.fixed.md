- **A failed reload keeps the live cycle's factory.** Per-cycle factory
  ownership replaces the process-global flag: discarding a failed cycle
  never untracks or deletes the factory that is still serving. (Supersedes
  the SEC-05 factory-marker hardening.)
