# v1.0.2

- Successful first-run file setup now records a completion token in the plugin TOML and is skipped on later launches.
- Existing installations and user settings are preserved; unsuccessful setup can be retried.
- Existing ring installations are recognized without adding duplicate rows; obsolete Cube recipes are removed during the one-time update.
- Installation now handles reordered data columns by name and reports missing populated columns before writing incomplete rows.
