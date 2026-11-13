# Runtimes

Every runtime is one command. Point it at a project, get a deployable artifact
and the `[[app.artifact]]` block to paste into `vyzorix.toml`.

```sh
vyzorix-builder <runtime> --project . --app myapp
```

The builder runs Docker to compile the project into a tarball, writes it to
`./dist`, and prints its size, its sha256, and the manifest block. It never
touches a device.

---

## Quick reference

| Runtime | Command | What the artifact is |
|:--|:--|:--|
| Go | `vyzorix-builder golang --project . --app myapp` | one static binary |
| Rust | `vyzorix-builder rust --project . --app myapp` | one static (musl) binary |
| C / C++ | `vyzorix-builder cpp --project . --app myapp` | cross-built binary + its shared-library closure |
| Zig | `vyzorix-builder zig --project . --app myapp` | one static binary |
| Node server | `vyzorix-builder node --project . --app myapp` | app + production `node_modules` |
| Web site | `vyzorix-builder node-webroot --project . --app myapp --dist dist` | static files (no runtime) |
| Deno | `vyzorix-builder deno --project . --app myapp` | one self-contained binary |
| Bun | `vyzorix-builder bun --project . --app myapp` | one self-contained binary |
| .NET | `vyzorix-builder dotnet --project . --app myapp` | self-contained publish (no runtime needed) |
| Java / Kotlin | `vyzorix-builder jvm --project . --app myapp` | a native binary (or a fat jar) |
| Python | `vyzorix-builder python --project . --app myapp` | app + resolved wheels |
| Ruby | `vyzorix-builder ruby --project . --app myapp` | app + vendored gems |
| PHP | `vyzorix-builder php --project . --app myapp` | app + `vendor/` |
| Anything else | `vyzorix-builder containerfile --project . --app myapp --file Containerfile` | whatever your Containerfile writes |

Each command takes the same flags (see `vyzorix-builder <runtime> --help`), so
learning one is learning all of them.

---

## The flags

| Flag | Default | Meaning |
|:--|:--|:--|
| `--project DIR` | `.` | the project to build |
| `--app NAME` | the project dir's name | the app name in the manifest |
| `--name NAME` | `app` | the artifact name |
| `--cmd PATH` | `/home/apps/<app>/run.sh` | the entrypoint the device runs |
| `--arch ABI` | `arm64-v8a` | `arm64-v8a`, `armeabi-v7a`, or `x86_64` |
| `--version V` | `v0.0.0` | the artifact version |
| `--out DIR` | `dist` | where local artifacts land |
| `--build CMD` | the runtime's default | override the build command |
| `--file PATH` | — | the Containerfile (`containerfile` only) |
| `--dist DIR` | `dist` | the built output dir (`node-webroot` only) |
| `--kind KIND` | `dir` | `dir` or `webroot` |
| `--publish B` | `local` | `local`, `ghcr`, or `object` |
| `--dry-run` | off | build, but publish nothing |
| `--plan` | off | print the plan, run no Docker |

---

## Try it without Docker first

`--plan` prints exactly what a build would do — the image, the build context,
the glibc floor and the output filename — and runs nothing:

```sh
vyzorix-builder python --project . --app myapp --plan
```

---

## Per runtime

### Go

```sh
vyzorix-builder golang --project . --app myapp
```

Builds with `CGO_ENABLED=0`, so the result is one static binary with nothing to
resolve on the device. The project must have a `main` package (`go build ./...`).

### Rust

```sh
vyzorix-builder rust --project . --app myapp
```

Builds for the musl target, so the result is a static binary. The image installs
a cross linker and points Cargo at it, so no qemu is needed.

### C / C++

```sh
vyzorix-builder cpp --project . --app myapp
```

Cross-compiles with the target's GCC, then bundles the shared-library closure of
the built binaries (minus the glibc the device rootfs provides) and asserts the
glibc floor. The project's build must write into `/out/app` — a `Makefile` or a
`cmake` command, passed with `--build` when the default `make` is not enough:

```sh
vyzorix-builder cpp --project . --app myapp \
  --build 'cmake -B build -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc -DCMAKE_SYSTEM_NAME=Linux && cmake --build build && mkdir -p /out/app && cp build/myapp /out/app/'
```

### Zig

```sh
vyzorix-builder zig --project . --app myapp
```

Runs `zig build` for the target and stages the installed output. Zig ships its
own libc and linker, so the result is static.

### Node (server)

```sh
vyzorix-builder node --project . --app myapp
```

Installs production dependencies (`npm ci` when there is a lockfile, else
`npm install`) and stages the app plus `node_modules`. Node is expected on the
device rootfs; to bundle a runtime instead, use `containerfile`.

### Web site (static)

```sh
vyzorix-builder node-webroot --project . --app myapp --dist dist
```

Runs the project's build (`npm ci && npm run build` by default) and stages the
`--dist` directory. The artifact is architecture-free: the manifest omits
`arch`, and the device serves the files directly.

### Deno

```sh
vyzorix-builder deno --project . --app myapp
```

`deno compile` embeds the runtime and the script into one binary. It compiles
`main.ts` (or `main.js`) by default; name your own entrypoint and permissions
with `--build`:

```sh
vyzorix-builder deno --project . --app myapp \
  --build 'deno compile --allow-net --allow-read server.ts -o app'
```

### Bun

```sh
vyzorix-builder bun --project . --app myapp
```

`bun build --compile` produces one binary with the runtime embedded. Compiles
`main.ts` by default.

### .NET

```sh
vyzorix-builder dotnet --project . --app myapp
```

`dotnet publish --self-contained` bundles the runtime and the app, so the device
needs nothing. Finds the project's `.csproj` or `.fsproj`.

### Java / Kotlin (JVM)

```sh
vyzorix-builder jvm --project . --app myapp
```

By default it builds a GraalVM **native image**: one self-contained binary, no
JRE on the device. It needs a fat jar named `*-all.jar` (Gradle shadowJar, Maven
shade). Point it at your own build with `--build`, and pass the jar glob if your
jar is named differently.

For a plain fat jar instead (needs a JRE on the device), override the build:

```sh
vyzorix-builder jvm --project . --app myapp --build './gradlew shadowJar --no-daemon'
```

### Python

```sh
vyzorix-builder python --project . --app myapp
```

Installs the project's dependencies (`requirements.txt`, or the project metadata)
into a `site-packages` directory, resolved for the target platform, and stages
the app. Python is expected on the device rootfs.

### Ruby

```sh
vyzorix-builder ruby --project . --app myapp
```

Vendors the project's gems (`bundle install`) and stages the app plus the
vendored set. Ruby is expected on the device rootfs.

### PHP

```sh
vyzorix-builder php --project . --app myapp
```

Installs Composer dependencies (`composer install --no-dev`) and stages the app
plus `vendor/`. PHP is expected on the device rootfs.

### Anything else — the Containerfile escape hatch

```sh
vyzorix-builder containerfile --project . --app myapp --file Containerfile
```

Point it at a `Containerfile` (or `Dockerfile`) that writes a tarball to
`/out/<name>.tar.gz`. The engine copies it out unchanged. This is how any
toolchain the first-class runtimes do not cover is built.

---

## Where the artifact goes

With the default `--publish local`, artifacts land in `--out` (default `dist`):

```
dist/myapp-app-linux-android-arm64-v8a-v0.0.0.tar.gz
```

To publish to a registry instead, use `--publish ghcr` with a namespace and repo
(and a `GITHUB_TOKEN` with `packages:write`):

```sh
vyzorix-builder golang --project . --app myapp \
  --publish ghcr --namespace owner --repo myapp
```

The builder pushes the bytes as an OCI blob and verifies the blob answers
**anonymously** (the device holds no credential), then prints the blob URL and
digest to put in the manifest.

---

## From a build to a deploy

The builder prints a ready `[[app.artifact]]` block. A full manifest looks like:

```toml
[device]
id  = "d_01H..."
org = "org_01H..."

[[app]]
name = "myapp"
cmd  = "/home/apps/myapp/run.sh"

  [[app.artifact]]
  name   = "app"
  arch   = "arm64-v8a"
  url    = "https://.../myapp-app-linux-android-arm64-v8a-v0.0.0.tar.gz"
  sha256 = "9f2c..."
```

Then deploy it with the CLI:

```sh
vyzorix deploy --device d_01H... --file vyzorix.toml
```

`vyzorix build` does both steps from a `vyzorix.build.toml` recipe; the
per-runtime commands above are the same build, driven by flags instead.
