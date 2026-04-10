# wordpress-wordfence

Focus:

- deny on known-existing sensitive files
- deny on protected directories
- security headers emitted by hardening rules

Implementation note:

Prefer deterministic fixture files over plugin internals that may change names.
