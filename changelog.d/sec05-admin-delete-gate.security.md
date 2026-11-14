- **Admin cache-entry deletes require `EnableCachePurge`.** Deleting an entry
  from the admin console is refused with 403 unless cache purging is enabled,
  matching the existing purge gate.
