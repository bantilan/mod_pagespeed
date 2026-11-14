- **Configured resource headers replace the origin's.** An `AddResourceHeader`
  name clears the origin's header of the same name first, so responses carry
  exactly one copy; Apache no longer adds a second `X-Content-Type-Options:
  nosniff` when one is already present.
