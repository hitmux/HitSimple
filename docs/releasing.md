# Publishing a HitSimple patch release

Release preparation is reviewed in a pull request. Compiler, extension,
lockfile, package examples, and `docs/releases/<version>.md` must agree.
Before publication, verify that the intended commit has passed Release,
Sanitizers, and HS-Smith checks and has no blocking review findings.

## Existing tag path

Push an unused `vX.Y.Z` tag pointing to the reviewed commit. The Release
workflow validates the complete platform matrix, verifies that the tag still
resolves to that commit, uploads a draft, downloads all 13 packages and
`SHA256SUMS`, verifies every checksum, and only then publishes it as Latest.
Lightweight and annotated tags are supported.

## Explicit manual publication

When a standalone tag push is unavailable, run the Release workflow from the
Actions page with these inputs:

- Branch: `master`
- `version`: the exact source version, for example `0.3.9`
- `scope`: `all`
- `publish`: `true` (the default is `false`, which only validates)
- `expected_commit`: the exact reviewed 40-character master commit SHA

The request fails if the branch, scope, version, or SHA does not match. After
the full build/package matrix succeeds, the publication job checks for any
existing release (including drafts) and tag. Only an absent tag is created,
using a create-only Git refs request pinned to this run's SHA. No tag or
existing release is overwritten. Only an actual tag-lookup HTTP 404 counts
as absence; authentication, rate-limit, network, and other API errors stop
publication.

This path uses the publication job's existing `contents: write` permission
and automatically supplied `GITHUB_TOKEN`. It does not require a new secret,
personal access token, or signing change. A tag created with `GITHUB_TOKEN`
does not trigger another push workflow, so this same manually dispatched run
performs draft upload, downloaded-asset verification, and final publication.
See [GitHub's event behavior](https://docs.github.com/en/actions/concepts/security/github_token).

## Failure and retry safety

Failures before tag creation can be retried after their cause is fixed.
If a run creates a tag or draft and then fails, a new manual attempt stops
rather than replacing it. Inspect the existing tag's commit, draft identity,
assets, and the failed job before deciding on a recovery action. Do not
force-push a tag or delete a release just to make a retry succeed.

A successful validation-only run is not a publication. Confirm the public
release, version tag, notes, 13 package assets plus `SHA256SUMS`, and actual
downloaded hashes before reporting a version as released.
