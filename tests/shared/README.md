# tests/shared/

Cross-scope test directory.

**Purpose**: Place tests that span multiple scopes (test-fixture + umd-evolution + shared).
Currently empty. Populated when cross-scope integration tests are needed.

**When to use**:
- Tests that verify both test-fixture and umd-evolution behavior
- Shared infrastructure tests (IGpuDriver contract, sync primitives)
- Integration tests requiring shared + test-fixture + umd-evolution targets

**Build**: Currently not built automatically. To enable, add to cmake/Shared.cmake or
appropriate scope .cmake file.
