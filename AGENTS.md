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
ref choice decides the answer. Measured on 2026-08-13, `upstream/dsp56300` gives
25 paths across 17 commits, all authored here; `upstream/main` gives 54 paths
across 47 commits, 30 of which upstream authored on the `dsp56300` branch. A
comparison against `upstream/main` therefore reports other people's code as ours.

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

**One exception, and it is the only one.** A number that a mechanism reads and
checks at build time or at test time may stay. The check is then the source of
truth, not the comment, and it fails loudly when the number drifts. A number
that no mechanism reads is a liability.

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
