---
title: Contributing
description: How to contribute to LiteHTTPD
---

## Development Workflow

1. Fork the repository
2. Create a feature branch: `git checkout -b feature/my-change`
3. Make changes and add tests
4. Run the full test suite: `ctest --test-dir build --output-on-failure`
5. Run consistency check: `bash tests/check_consistency.sh`
6. Submit a pull request

## Adding a New Directive

1. Add the directive type to `include/htaccess_directive.h` (enum + AllowOverride category)
2. Add parsing in `src/htaccess_parser.c`
3. Add printing in `src/htaccess_printer.c`
4. Create or update executor in `src/htaccess_exec_*.c`
5. Add execution dispatch in `src/mod_htaccess.c`
6. Add AllowOverride filtering in `src/htaccess_dirwalker.c`
7. Add free logic in `src/htaccess_directive.c`
8. Add unit tests in `tests/unit/`
9. Update `tests/check_consistency.sh` expected count
10. Update README.md directive list

## Code Style

- C11 for module code, C++17 for tests
- 4-space indentation
- `snake_case` for functions and variables
- `UPPER_CASE` for constants and macros
- No dynamic allocation in hot paths (use thread-local buffers)
- All public functions prefixed with `htaccess_`, `exec_`, or `lsi_`

## License

LiteHTTPD is licensed under GPLv3, matching OpenLiteSpeed.
