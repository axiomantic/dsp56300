# dsp56300 (axiomantic fork) — agent instructions

This is a **fork** of `dsp56300/dsp56300`, the DSP56300 emulation library that
`gearmulator` consumes as the submodule `source/dsp56300`.

Repository: `axiomantic/dsp56300`. Upstream: `dsp56300/dsp56300`.
Licence: GPL-3.0. Contributions must be GPL-3.0 compatible, with attribution.

The fork exists for two reasons: to carry this project's `Peripherals56311` set
and its tests, and so that a framework contribution can open a pull request
against upstream.

## The authorship boundary

The boundary between inherited and authored code is computable rather than
guessed:

**Upstream's default branch is `dsp56300`, not `main`.** Confirm it rather than
assuming it: `git ls-remote --symref upstream HEAD` answers `refs/heads/dsp56300`,
and `git symbolic-ref refs/remotes/upstream/HEAD` answers the same locally. The
ref choice decides the answer: the `upstream/main` base includes commits that
upstream authored on the `dsp56300` branch, and the `upstream/dsp56300` base
includes none. A comparison against `upstream/main` therefore reports other
people's code as ours.

```bash
git fetch upstream
git merge-base HEAD upstream/dsp56300          # the fork point
git diff --name-only $(git merge-base HEAD upstream/dsp56300)...HEAD   # our paths
git log $(git merge-base HEAD upstream/dsp56300)..HEAD                 # our commits
```

At the time of writing, the fork point is
`c051afad31612c2d2c7a81a7ab23e1c5ac9e61af`. **Recompute it rather than trusting
that literal** — the fork point moves whenever upstream is merged in. Use
three-dot semantics so that commits merged in FROM upstream are excluded.

Our work touches `source/dsp56kEmu/`, `source/dsp56kBase/` and the
`.github/workflows/` jobs. Compute the current set with the command above; do
not rely on that list.

This repository runs a T0 suite of its own — the `dsp56k_*` test names this
project registers are required checks — plus the project lint steps. Read the
registered names out of a configured build tree rather than counting them by
hand.

## Build and test

### Narrow — our own tests

**There is no useful configure-time narrowing here, and that is a real answer
rather than a missing one.** The only option the root tree declares is
`DSP56300_DEBUGGER`, which is off by default and only ADDS the wxWidgets
debugger; everything else — `asmjit`, `dsp56kBase`, `dsp56kEmu`, the test
runner, the disassembler — is unconditional. `dsp56kEmu` dominates the build and
every test this project registers links it, so a target-narrowed build saves
little. Build everything and narrow the RUN.

**Use the preset.** It carries the label selection and the build tree, so
neither has to be typed.

```bash
cmake --preset nmg2
cmake --build --preset nmg2
ctest --preset nmg2
```

`cmake --list-presets`, `--list-presets=build` and `--list-presets=test` name
the rest. **The presets live in `CMakeUserPresets.json`, which `.gitignore`
excludes.** Upstream owns `CMakePresets.json`; a fork that overwrote it would
conflict on every merge. So the presets are LOCAL TO THIS MACHINE — a fresh
clone has only the raw form.

The raw form, which is what the preset expands to:

```bash
cmake -S . -B <build> -DCMAKE_BUILD_TYPE=Debug
cmake --build <build> --parallel
ctest --test-dir <build> --no-tests=error -L nmg2 --output-on-failure
```

`-L nmg2` selects by the label `source/dsp56kEmu/test/CMakeLists.txt` attaches
to every test it registers. Prefer it to a `-R '^dsp56k_'` name pattern: the
label is set at the registration site, so a test added there joins the selection
without an edit here.

`--no-tests=error` has no test-preset field. The preset carries it as the
environment variable `CTEST_NO_TESTS_ACTION`, which needs CMake 3.26 or later.
On an older CTest the preset runs without that guard while the raw form keeps it.

**The build preset narrows nothing** and does not pretend to, for the reason
above. It exists so the out-of-tree build path is not typed.

### Full

```bash
cmake --preset full
cmake --build --preset full
ctest --preset full
```

The raw form:

```bash
cmake -S . -B <build> -DCMAKE_BUILD_TYPE=Debug
cmake --build <build> --parallel
ctest --test-dir <build> --no-tests=error --output-on-failure
```

The difference is the RUN, not the build: the full run adds upstream's
`dsp56300_unitTests`, the `dsp56kBase` tests and asmjit's own. Use it whenever a
change touches inherited code — our label covers our tests and says nothing
about what we broke underneath them.

**Neither run reaches the consumer.** `gearmulator` takes this repository as the
submodule `source/dsp56300`, and a change here is invisible there until that
submodule pin moves. A change to `dsp56kEmu`'s public surface needs a build in
that fork as well.

On this host `xcode-select` points at CommandLineTools while full Xcode is
installed. The Unix-Makefiles configure resolves an SDK without help; prefix
`DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer` only if a step fails
to find one.

## Comments

Comments are sparse. Write one only where a reader must otherwise reconstruct a
DECISION. The code says what it does. The comment says why you chose it instead
of the alternative.

Never write these in a comment:

- **A count** — cases, tests, scenarios, symbols, files, or lines. The next
  change makes it wrong, and nothing catches it.
- **A present-tense claim about what the tests cover**, or about what a wrong
  implementation would fail. If coverage matters, assert it in a test. A failing
  test is the only durable statement about coverage.
- **A note about history** ("this used to...", "an earlier version..."). Git
  holds that.
- **An enumeration whose length is the claim.** A stale enumeration is a stale
  count with the number spelled out. Delete the word "four" from "any of those
  four values" and the list above it still says four. It goes wrong by the
  mechanism the word did.
- **A path that does not resolve.** A comment that names a file, a script, a
  test, or a type must name one that exists.

**One exception, and it is the only one.** A number that a mechanism reads and
checks at build time or at test time may stay. The check is then the source of
truth, not the comment, and it fails loudly when the number drifts. A number
that no mechanism reads is a liability.

**The path rule is the one a machine can decide, and that is why it is stated
apart from the others.** Each other rule here needs a reader's judgement about
what a sentence claims. "Every path-shaped token resolves" is a regular
expression and a file test. Write the check. Do not trust a sweep to hold.

**A path that MOVED is corrected. A path that never existed is deleted.** A moved
path has a correct target, so give it one. A named script that exists nowhere has
no target, so the sentence goes — unless the sentence records a known GAP, and
then the gap moves to a tracked item BEFORE the comment goes.

**A date does not rescue a stale claim.** Within a day of churn a date
discriminates nothing.

**An invariant with no mechanism is a comment.** This repository carries a
worked example on the inherited side: an emitter has both its instruction-count
and cycle-count updates commented out under an explicit `TODO`, and a claim
elsewhere rested on the behaviour those lines would have provided. If a property
must hold, make something go red when it stops holding.

### Scope: this rule applies to code we authored, and NOT to upstream

**Do not sweep, rewrite, or delete comments that came from upstream.** They are
not ours to clean, and editing them creates merge conflicts for no gain. Apply
the rule to:

- files that appear only on our side of the fork point, and
- lines we change in an inherited file.

A comment that describes a line you changed may be repaired — a comment
contradicting the line beside it is a defect OF that line. That permission is
narrow: a comment elsewhere in an inherited file still belongs to upstream.

## Gotchas

- `git grep` skips untracked files. Use `grep -r`, `rg`, or `git grep
  --untracked` before claiming something appears nowhere, and name the tool
  beside the claim.
- A build that succeeds is not a check. Verify the artifact a step should have
  produced, not the exit status.
- A check that runs only on the platform the developer already works on is not a
  portable check. State the limitation rather than letting the green stand for
  more than it covers.

## Related

This fork is one repository of a Nord Modular G2 emulator project. The
cross-repository rules and the implementation plan live in the
`nord-modular-emulator` workspace. `gearmulator` consumes this repository as a
submodule; a change here reaches that fork through its submodule pin.
