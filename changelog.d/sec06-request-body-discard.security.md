- **Module-generated responses discard an unread request body first.**
  Responses the module answers itself drain the request body (with a
  lingering close when it cannot be discarded), closing a
  desync/smuggling-adjacent shape on reused connections. HTML transforms
  and in-place lookup misses are excluded: another party owns those bodies.
