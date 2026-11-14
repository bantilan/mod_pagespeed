- **Unresized image outputs keep naming their input.** When resizing is
  skipped, the output name is re-encoded from the URL decoded out of the
  current name rather than from the input leaf, so fetches from another
  directory still decode to the resource they were made from.
