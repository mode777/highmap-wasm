# Spec Delta

## Purpose

Defines how consumers initialize the library — locating and instantiating the
WebAssembly artifact identically from Node, browsers, and Web Workers through
one async entry point.

## ADDED Requirements

### Requirement: Async initialization entry point

The package SHALL expose a single asynchronous `init()` function that
resolves to an API object containing the terrain-generation functions and
enum constants; no other initialization step SHALL be required.

#### Scenario: init resolves to a usable API

- **WHEN** a consumer awaits `init()`
- **THEN** the resolved object exposes the documented functions and enum
  constants and is ready for immediate use

### Requirement: Default artifact location works with bundlers

Under Node, browsers, and workers, `init()` SHALL locate the `.wasm` artifact
next to the package's JS entry (the `import.meta.url` pattern) without
requiring any bundler plugin or consumer configuration.

#### Scenario: Bundler emits the wasm as an asset

- **WHEN** a consumer imports the package in a standard ESM bundler setup
- **THEN** the bundler recognizes the wasm reference, emits the artifact as an
  asset, and `init()` loads it at runtime without extra configuration

### Requirement: Caller-supplied artifact overrides

`init()` SHALL accept an options object with a `wasmBinary` option (raw bytes
of the artifact) and a `locateWasm` option (maps the artifact filename to a
URL); a supplied override SHALL take precedence over default location.

#### Scenario: wasmBinary bypasses location entirely

- **WHEN** `init()` is called with `wasmBinary` set to bytes read by the
  caller (for example from disk in an SSR environment)
- **THEN** initialization succeeds without any network or file access by the
  library itself

#### Scenario: locateWasm redirects to a CDN

- **WHEN** `init()` is called with `locateWasm` returning a remote URL
- **THEN** the artifact is fetched from that URL and initialization succeeds

### Requirement: Same import in every environment

The package SHALL be consumable as an ES module in Node >= 18, modern
browsers, and Web Workers with identical import syntax and identical `init()`
usage; it MUST NOT ship a separate entry point per environment.

#### Scenario: Node ESM import works

- **WHEN** the package is imported from an ESM module in Node >= 18
- **THEN** `init()` resolves and terrain generation runs

#### Scenario: Worker import works

- **WHEN** the package is imported inside a Web Worker
- **THEN** `init()` resolves and terrain generation runs on the worker thread

### Requirement: Repeated initialization creates independent instances

Calling `init()` more than once SHALL create independent module instances,
each with its own wasm heap; objects from one instance MUST NOT be passed to
functions of another.

#### Scenario: Second init yields a fresh heap

- **WHEN** `init()` is called twice and both results are kept
- **THEN** both instances work independently and each owns a separate wasm
  memory

### Requirement: Initialization failures are reported clearly

If the artifact cannot be located, fetched, or instantiated, `init()` SHALL
reject with a descriptive error instead of hanging or throwing low-level
glue errors.

#### Scenario: Missing artifact rejects with context

- **WHEN** `init()` cannot retrieve the wasm artifact (for example a broken
  `locateWasm` URL)
- **THEN** the returned promise rejects with an error identifying the artifact
  and the failure reason
