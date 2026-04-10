# laravel

Focus:

- front-controller routing through `public/index.php`
- static asset bypass
- deny access to sensitive files
- probe visibility into request/env state

Expected future probe checks:

- `SCRIPT_NAME`
- `SCRIPT_FILENAME`
- `REQUEST_URI`
- relevant request headers
