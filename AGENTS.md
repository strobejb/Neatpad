# Source formatting

Use `.clang-format` for C and C++ and `.editorconfig` for text-file settings.
The formatter follows the qexed project at `C:\src\qexed`: Allman braces,
four-space indentation, right-aligned pointer/reference symbols, aligned
declarations and assignments, and a 120-column limit.

- Expand functions, loops, conditions, lambdas and blocks onto separate lines.
  Do not compress multiple statements onto one line.
- Keep blank lines between declarations and executable code, and between
  distinct steps. Keep related assignment groups aligned.
- Preserve include order: Neatpad's Win32 headers have ordering dependencies.
- Use LF line endings, except for Windows-only files (`.rc`, `.sln`, `.vcxproj`,
  `.filters`, `.bat`, `.cmd`), which are CRLF, as `.gitattributes` says.
- Format new files and edited regions; do not reformat unrelated existing code.
- Keep formatting changes separate from behavior changes. Preserve the user's
  in-progress edits.
- `src/Neatpad` uses C. Keep C++ implementation details behind TextView's public
  C-compatible interface.
