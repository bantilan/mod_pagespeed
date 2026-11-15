- **Thread start no longer touches the Thread object after the OS thread
  exists.** The started flag is set before creation (rolled back on failure)
  and thread naming no longer reads the parent's handle, closing a
  self-delete use-after-free race.
