# vyzorix-builder

Turns a project and a `vyzorix.build.toml` recipe into the digest-pinned
artifacts a `vyzorix.toml` deploy manifest references: tarballs at a public HTTPS
URL, pinned by `sha256`, tagged by `arch` and `kind`.

It is the producer half of the deploy pipeline. The device applies what the
server signs; nothing here touches the device.

## Install

```sh
curl -fsSL https://raw.githubusercontent.com/VinnsEdesigner/vyzorix-builder/main/install.sh | sh
```

The script verifies the download against the release's `SHA256SUMS` and reports
whether Docker is present, because nothing builds without it. Set `PREFIX` to
change the install directory and `VYZORIX_BUILDER_VERSION` to pin a release.

## Build and check

```sh
make            # configure and build (Release)
make test       # build and run the unit suite
make check      # configure, build, test, clang-tidy, cppcheck
```

`scripts/check.sh` is the single entry point for every check. It is the same
command run in CI, so a green local run means a green CI run. Requires a C++17
compiler, CMake >= 3.16, and (for `make check`) clang-tidy and cppcheck.

There are no third-party C dependencies. JSON, TOML and SHA-256 are small,
tested implementations; Docker, `tar`, `readelf` and `curl` are driven as
subprocesses.

## Use

One command per runtime. Point it at a project and get an artifact plus the
manifest block to paste:

```sh
vyzorix-builder golang --project . --app myapp
vyzorix-builder python --project . --app myapp
vyzorix-builder rust   --project . --app myapp
```

Run `vyzorix-builder` with no arguments for the full list, and see
[`docs/RUNTIMES.md`](docs/RUNTIMES.md) for every runtime, its flags, and a
copy-paste command each.

`vyzorix build` drives the same build from a `vyzorix.build.toml` recipe, over a
JSON stdin/stdout protocol. Three machine commands:

```sh
# Resolve and print what a build would do: the image, context, floor and output
# filenames per artifact. Runs no Docker, so it works on any machine.
vyzorix-builder plan < request.json

# Build every artifact and publish it (what `vyzorix build` invokes).
vyzorix-builder build < request.json

# Publish already-built files, without rebuilding.
vyzorix-builder publish < request.json
```

A `build`/`plan` request carries the recipe, `projectDir`, `abis` and a `publish`
block. A `publish` request carries an `artifacts` list (each with `name`, `arch`,
`kind`, `file`) instead of a recipe.

Exit codes: `0` success, `1` a refusal (a schema mismatch, or a build/publish
failure), `2` usage or a local failure.

## Layout

Dependencies point downward only: `okfw` <- `domain` <- `modules/*` <- `cmd`.

| Path | What it is |
|:--|:--|
| `okfw/` | cross-cutting utilities: Result, log, subprocess, SHA-256, JSON, TOML |
| `domain/` | the build vocabulary: recipe, target, artifact, the naming policy |
| `modules/plan/` | the adapter port and its catalog |
| `modules/adapter/` | one file per runtime (14), plus the Containerfile escape hatch |
| `modules/containerbuild/` | the docker-subprocess build runner |
| `modules/package/` | deterministic tar, digest, glibc-floor assertion, closure |
| `modules/publish/` | the Store port: composite (device + record), GHCR, object store, local |
| `modules/schema/` | the recipe document and the CLI <-> builder JSON contract |
| `cmd/` | the composition root: the only place implementations meet interfaces |
| `builders/` | the pinned builder images, one per adapter |
| `tests/` | the unit suite |

Adding a runtime is adding one file under `modules/adapter/` and one
`builders/<lang>.Dockerfile` whose name matches the adapter's `lang`. Nothing
below the adapter layer learns a runtime exists.

## Runtimes

Fourteen adapters ship: `golang`, `rust`, `cpp`, `node`, `node-webroot`, `deno`,
`bun`, `zig`, `dotnet`, `ruby`, `php`, `python`, `jvm`, and `containerfile` (the
escape hatch for anything else).

- **Self-contained binaries** (one file, nothing on the device): `golang`,
  `rust`, `zig`, `deno`, `bun`, `dotnet` (self-contained publish), `jvm`
  (`native-image` mode).
- **Runtime on the device** (the artifact carries the app and its dependencies):
  `python`, `ruby`, `php`, `node`.
- **Static files** (no runtime at all): `node-webroot`.
- **Any toolchain** via `containerfile`.

A project that must bundle its own runtime is the `containerfile` escape hatch.

## Publishing

Three backends, chosen by the request's `publish.backend`:

- **`composite`** (recommended) — publishes the bytes to a **device store** (an
  object store: a plain, credentialless URL) and records the build in a **record
  store** (GHCR: an OCI image with a tag). `publish.deviceBackend` /
  `publish.recordBackend` name each. The manifest's `url` is the device store's.
- **`ghcr`** — pushes the artifact as an OCI blob plus a tagged image. Set
  `GHCR_TOKEN` (or `GITHUB_TOKEN`) with `packages:write`.
- **`object`** — an S3/R2 public prefix: `VYZORIX_BUILD_BASE_URL` and
  `VYZORIX_BUILD_UPLOAD_CMD`.
- **`local`** — dev and dry runs only (a `file://` URL the device cannot fetch).

**Why composite is the default recommendation.** A `ghcr.io` blob URL is *not* a
device-fetchable URL: it answers an unauthenticated GET with a **401 challenge even
for a public package**, and its redirected CDN URL is **signed and expires in
minutes**. The device fetches with a plain GET and no credential, so its URL must be
an object store. GHCR stays as the human-browsable, build-provenance record. A
record-store failure is a warning, not a refusal — the device path already works.

Before it reports success, the builder fetches what it published and checks the
sha256 (performing the OCI auth handshake when a store challenges with a 401), so a
URL that serves the wrong bytes is refused here rather than discovered on the
device.

## License

GPL-3.0. See `LICENSE`.
