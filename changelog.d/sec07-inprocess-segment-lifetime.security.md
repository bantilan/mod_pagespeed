- **In-process segments stay alive while segment objects use them.**
  Recreating or destroying a segment name no longer frees storage that
  outstanding segment and mutex objects still point to.
