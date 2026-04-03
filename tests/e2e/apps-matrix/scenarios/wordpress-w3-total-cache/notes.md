# wordpress-w3-total-cache

Focus:

- long-lived static TTL
- `Cache-Control`
- `Expires`
- `Vary`
- optional `ETag` normalization/removal

Implementation note:

Use deterministic synthetic assets instead of plugin-generated cache files that
may vary run-to-run.
