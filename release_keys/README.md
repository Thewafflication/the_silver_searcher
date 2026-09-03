# Release signing keys

`wpm-release.public` is the Ed25519 public key that official `ag` WPM packages
are signed with. It is committed so anyone can verify a downloaded package, and
it is published as a release asset so WPM clients can trust it once:

```text
wpm trust add wpm-release.public
wpm verify ag-x64-2.2.0.zip
```

## Creating the key pair (one time)

The private half never enters the repository. Generate the pair on a trusted
machine:

```powershell
wpm keygen .\wpm-release.private .\wpm-release.public
```

Then:

1. Commit `wpm-release.public` to this directory.
2. Store the contents of `wpm-release.private` as the `WPM_RELEASE_PRIVATE_KEY`
   secret in the repository's protected `release` environment. That environment
   is the only place the release workflow can reach it, and the workflow deletes
   the materialized key file in an `always()` step.
3. Delete the local private key file, or move it to offline storage.
4. Record the key identifier in the project README so users can check it against
   an independent source before trusting it.

Until both the public key and the secret exist, the `release` job fails early
with a message naming the missing one. Branch and pull-request builds are
unsigned and do not need either.
