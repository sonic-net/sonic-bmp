# Move BMPReader rBMP above try block for correct scope

## Summary
In `ClientThread()`, `BMPReader rBMP` is used by a `std::thread` that receives `&rBMP`. Previously `rBMP` was declared inside the `try` block, so it could go out of scope before the thread finished. This change moves the declaration above the `try` so `rBMP` stays in scope for the full thread lifetime.

## Type of change
- [x] Bug fix
- [ ] New feature
- [ ] Code refactor
- [ ] Documentation update
- [ ] Other (describe):

## Motivation and Context
The reader thread runs `BMPReader::readerThreadLoop` with a pointer to `rBMP`. If `rBMP` is only in scope inside the `try` block, it is destroyed when the block exits (e.g. on exception or normal path), while the thread may still be running, leading to use-after-free or undefined behavior. Moving `rBMP` above the `try` keeps it in context for the entire `ClientThread` function and the cleanup handler.

## Approach
- Declare `BMPReader rBMP(logger, thr->cfg);` immediately after `pthread_cleanup_push(ClientThread_cancel, &cInfo);` and before `try`.
- Remove the declaration from inside the `try` block (left as a comment for context).

## How has this been tested?
- Code review; build can be verified via `make` in a environment with cmake and dependencies (librdkafka, libyaml-cpp). Upstream Azure Pipelines will build on PR.

## Checklist
- [ ] My code follows the project's style guidelines
- [ ] I have performed a self-review of my code
- [ ] I have commented my code where necessary
- [ ] I have updated the documentation accordingly
- [ ] My changes generate no new warnings
- [ ] I have added tests that prove my fix/feature works

---
**Base repository:** https://github.com/sonic-net/sonic-bmp  
**Base branch:** `master`
