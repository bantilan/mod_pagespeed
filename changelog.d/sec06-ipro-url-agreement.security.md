- **In-place optimization only touches URLs the server and the module read
  alike.** Requests whose normalized server reading differs from the
  module's parsed URL (separators, dot segments, case, hosts) are left to
  the server: no lookup, no recording under another entry.
