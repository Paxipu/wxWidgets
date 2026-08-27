# Working notes for this fork

Short, and meant to stay short. Things here were learned by getting them
wrong once; each line exists because repeating that mistake cost real time.

This file is fork-only and must be removed before anything goes upstream —
see #112, which tracks the other fork-only changes.

## Comments say what the code is; commit messages say why it changed

A comment explaining what the *old* code did wrong is archaeology. Once the
old code is deleted, nobody reading the file can see what is being contrasted,
and the comment is dead weight. Put that in the commit message, where
`git blame` will lead anyone who needs it.

This came back as the only review comment on the first batch of pull requests
sent upstream, so it is upstream's standard rather than a preference of ours.

Comments about the *current* code's shape are welcome and expected — "do this
before that, because otherwise X" earns its place.

## Check the assignee before starting

Both instances assign the same account, so the assignee field says "somebody is
on this" and not who. Read the governing issue and the claim comments on #98
first. Two of us once fixed the same three wxSlider bugs in parallel because
this step was skipped.

## Every measurement needs a control

Before trusting a number, check that the experiment did what you think. "The
drag left 0 stray pixels" meant nothing until a control revealed that no card
had ever been picked up, so every reading was of a board nobody touched. Ask
what result would prove the harness worked at all, and measure that too.

The same applies to negative results: "does not reproduce" is only reportable
once you can show the reproduction attempt was real.

## One threshold cannot answer two questions

A test asking both "did anything happen?" and "was it exact?" with a single
tolerance will answer the first and silently ignore the second. A card is 3500
pixels; a botched restore is five. See #136.

## Never cherry-pick with -x for upstream

`git cherry-pick -x` appends "(cherry picked from commit ...)" naming a commit
in this fork, which upstream cannot resolve and has no use for. It reached
eight of the nine pull requests in the first batch, and a maintainer asked us
to stop and then had to strip the line by hand while merging.

Lift commits for upstream with a plain `git cherry-pick`, and read the message
before pushing.

## House style worth remembering

* 80 columns.
* No bare `NULL` in `.h` or `.cpp` — CI greps added lines for it.
* `codespell` runs in CI; check new prose before pushing.
* Run the checks CI runs before pushing, not after.
