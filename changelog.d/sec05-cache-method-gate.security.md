- **Only GET and HEAD are answered through the proxy/cache branch.** Other
  methods fall through to nginx instead of being proxied or cached; request
  methods are now plumbed through (`RequestHeaders::MethodFromString`) on the
  nginx and Apache paths, and HEAD responses are excluded from in-place
  recording and rewriting.
