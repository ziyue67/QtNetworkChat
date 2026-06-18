# Large File Governance

Large-file governance covers object routing, file transfer evidence, package hygiene, and repository size discipline. For QQNT backend work, file-transfer protocol behavior should be represented through IPC docs, C++/Rust tests, and Redis/file routing tests.

## Backend Expectations

- Keep generated archives, local databases, histories, and transfer payloads out of Git.
- Preserve file progress and resume behavior through targeted tests before broad cleanup.
- Keep Redis/file routing evidence separate from frontend rendering changes.

Governance performance closeout remains a summary lane and should not be used to hide missing functional validation.
