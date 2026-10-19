# Integration, builds and upstream maintenance

Task guide for KOS Experimental. Read root `AGENTS.md` first; this is a
routing aid, not permission for broad changes. Owning lane: **Integration**.

## Scope and entry points

Build policy, exports/build aggregation, dependency pins, branch integration and publication.

- `environ_base.sh`
- `utils/kos-chain`
- `utils/Makefile.host-test`
- `utils/run-host-tests.sh`
- `.github/workflows/autobuild.yml`
- `BRANCHES.md`

Relevant existing documentation (dated claims must be rechecked):

- `FORK.md`
- `BRANCHES.md`
- `doc/toolchain-clock-refresh.md`

## Preserve these boundaries

The `addon/graphics-extraction` branch also carries `addons/libdcgfx/sources.mk`
and `utils/addon-extraction/audit.py`. These describe extraction ownership;
their presence does not mean a standalone graphics library is complete.

- Confirm actual target flags: GNU17 remains the baseline; a C23 header check is not a whole-project migration.
- For an authorized update, record old/new upstream and submodule SHAs, classify imported changes, reconcile all consumers and rebuild affected archives.
- Do not bulk-propagate fork documentation into narrow PR branches; preserve bundle identity and unrelated local edits.
- Treat Makefile/export changes as cross-area work. Shared scripts can remove build outputs; run aggregate validation only in an assigned isolated checkout.

## Coordinate before changing

- Every component affected by exports, configuration or dependency changes

## Validation entry points

No single component-wide host test was established; select affected suites from the routing index.

Target/example directories (source the assigned checkout's environment and inspect its Makefile before building):

- `examples/dreamcast/cpp/chrono_probe`

These are entry points, not a request to run all tests or a claim of success.
Build only in an assigned isolated checkout. Record the exact compiler/flags,
revision and result. Separate host, target-link, emulator and hardware evidence.

## Known gate and task handoff

No merge, publication, rebase or dependency update follows merely from an audit.

Before editing, state the bounded bug/feature, file scope, owning checkout,
related decision IDs in `notes.md`, and interface partners. On completion or
interruption, update the assigned private handoff with evidence, remaining dirty
work and one next action. Propose central decision/changelog updates to
integration; do not silently rewrite another subsystem's contract.
