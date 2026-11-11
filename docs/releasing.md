# Releasing

Releases are cut from Git tags. Pushing a tag that matches `v<major>.<minor>.<patch>`
(optionally with a prerelease suffix) starts `.github/workflows/publish.yml`,
which validates the tag, rebuilds and tests the tagged commit, publishes to npm
with trusted publishing and provenance, and creates the GitHub Release.

The tag must always equal the `version` in `package.json` at the tagged commit;
the tag never overrides the manifest.

## One-time setup (maintainer)

1. Claim the package name if it does not exist yet (a placeholder version is
   enough; it may live under a throwaway dist-tag such as `bootstrap`).
2. On npmjs.com, open the package → **Settings** → **Trusted Publisher** and add
   a **GitHub Actions** publisher:

   | Field | Value |
   |---|---|
   | Organization or user | `mode777` |
   | Repository | `highmap-wasm` |
   | Workflow filename | `publish.yml` |
   | Environment name | `npm-publish` |
   | Allowed actions | `npm publish` |

   The CLI equivalent (npm >= 11.15, 2FA enabled):

   ```sh
   npm trust github highmap-wasm \
     --file publish.yml \
     --repo mode777/highmap-wasm \
     --env npm-publish \
     --allow-publish \
     --yes
   ```

3. Create the `npm-publish` GitHub environment if it does not exist. The
   workflow uses it for auditability; add protection rules there if releases
   should require approval.
4. A new trusted-publisher configuration expires after **2 days** unless a
   publish succeeds, so configure it when you are ready to tag.

After the first successful publish you may harden the package under
**Settings → Publishing access** → "Require two-factor authentication and
disallow tokens"; trusted publishing keeps working because it uses OIDC, not
tokens. The workflow itself never reads an npm token.

## Cutting a release

```sh
# 1. Make sure main is green and your tree is clean.
git checkout main
git pull --ff-only origin main

# 2. Bump the version (updates package.json and package-lock.json).
npm version 0.1.2 --no-git-tag-version

git add package.json package-lock.json
git commit -m "release: 0.1.2"
git push origin main

# 3. Tag and push the tag.
git tag -a v0.1.2 -m "highmap-wasm 0.1.2"
git push origin v0.1.2

# 4. Watch the run.
gh run list --workflow publish.yml --limit 1
gh run watch <run-id> --exit-status
```

A prerelease works the same way (`0.1.2-rc.1` + `v0.1.2-rc.1`); it publishes
under the `next` dist-tag and is marked as a GitHub prerelease, so it never
becomes the default install.

The workflow refuses to publish when:

- the tag is not `vX.Y.Z[-prerelease]` or does not match `package.json`;
- the tagged commit is not reachable from `main`;
- the version already exists on npm;
- the build or `npm test` fails;
- the tarball contains files outside the allowlist (`dist/`, `README.md`,
  `LICENSE`, `THIRD_PARTY_LICENSES.md`, and `package.json`).

## Verifying a release

```sh
npm view highmap-wasm dist-tags --json
npm view highmap-wasm@0.1.2 dist.attestations --json
gh release view v0.1.2
```

Then install it in a clean project and verify provenance:

```sh
tmp="$(mktemp -d)" && cd "$tmp"
npm init -y
npm install highmap-wasm@0.1.2
npm audit signatures
```

## Rollback

- Never reuse a published version number.
- If a bad version shipped, `npm deprecate highmap-wasm@<version> "<reason>"`
  and publish a fixed patch. `npm unpublish` is subject to npm's registry
  policy (generally limited to 72 hours for version-specific removals).
- The tag can be deleted (`git push origin :refs/tags/vX.Y.Z` and
  `git tag -d vX.Y.Z`), but the npm version remains; bump instead of
  republishing.
- Disable the `publish` workflow to stop all releases.

## License

The tarball ships `LICENSE` and `THIRD_PARTY_LICENSES.md`. The public tagged
commit and the GitHub Release source archives provide the LGPL-2.1
source-availability evidence for the wasm binary.
