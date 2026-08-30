# Contributing

Please search existing Issues and Discussions before opening a report. For a
runtime failure, attach `%LOCALAPPDATA%\ResolveDlss5\ResolveDlss5.log` and
include the Resolve version, GPU, NVIDIA driver, input format, and reproduction
steps.

Code contributions should:

- target Windows x64 and compile with the documented Visual Studio/CMake preset;
- pass `ResolveDlss5.Feature18RuntimeSmoke` where an authorized runtime is available;
- preserve source-frame fallback on every initialization or evaluation failure;
- avoid committing DLL, SDK, library, build, or release artifacts;
- keep local SDK and runtime paths in environment variables or
  `CMakeUserPresets.json` only; and
- use focused, git-style commit subjects such as `fix: serialize NGX reset`.

By submitting code, you agree to license your contribution under the MIT
License. Do not submit NVIDIA SDK code or any binary whose redistribution
rights you cannot document.
