# Network & Security Architecture

This document captures the design for multi-machine access, file
encryption, and shared-infrastructure hosting - worked out across an
extended design discussion (2026-09-05) before any of it was built. It's
the detailed "how and why"; ROADMAP.md tracks what's actually being built
and when.

**Status: fully designed, not yet built.** See ROADMAP.md's "Bucket C" for
sequencing - this is deliberately sequenced after the current single-
machine app reaches its "done" state (Bucket A), so staff can start using
the app without waiting for this larger effort.

## The goal, stated plainly

Multiple staff, on multiple machines, need to work with the same day's
data - sequentially handing off who's actively entering, and with
managers able to watch live while reconciling against paper manifests -
without the data being readable by anyone who isn't supposed to see it,
and without any of this compromising the app's primary function
(BUSINESS_RULES.md): making sure today's sale is recorded correctly, with
no lost product, no wrong price, no wrong supplier attribution.

## Storage layout

- **The `.fbd` file remains the sole source of truth for the primary
  function**, unchanged in format. It moves from "next to the exe" to a
  **real network share** (not a sync-based tool like OneDrive/Google
  Drive - see "Why not OneDrive/Google Drive" below for why that
  distinction matters).
- **Fast local autosave keeps working exactly as it already does**
  (v0.9.8-v0.9.14's atomic-write hardening, unchanged) against a local
  working copy on whichever machine currently holds write access. That
  local copy is pushed to the network share periodically and on
  close/handoff - the share always holds the authoritative version, but
  per-keystroke writes stay fast and local, never hitting the network
  directly.
- **SQLite is a separate, derived reporting layer** (see ROADMAP.md's
  hybrid architecture item), built by ingesting each day-file once it's
  finalized (reusing `ParseFbdContent` - no new parsing logic needed).
  Each machine keeps its own **local read-only cached replica** of the
  SQLite database, refreshed periodically, rather than querying the
  network copy directly - SQLite's own documentation is explicit that it
  isn't reliable for concurrent access over network filesystems, and
  lookback queries ("what did we achieve 3 months ago") don't need
  to-the-second freshness the way the live `.fbd` view does.
- **Proposed refinement, not yet confirmed by Jack**: re-run the same
  ingestion (delete + re-insert that date's rows) whenever a *past* day's
  file is saved again, not just at the original finalization moment -
  this handles the "ran the paper/spreadsheet fallback, imported
  corrections later" workflow discussed earlier for free, using the
  exact same mechanism rather than needing to detect "is this a
  correction" specially.
- **Proposed addition, not yet confirmed**: a manual "Tools > Rebuild
  Price History Database" command that wipes and re-ingests every
  day-file found in the history folder - covers first-time setup
  (ingesting Jack's existing historical files), recovery if the SQLite
  file is ever lost/corrupted, and general peace of mind.

## Multi-machine access: single writer, unlimited concurrent readers

- **Exclusive OS-level file lock**, acquired by whichever machine opens
  the file for editing. Genuine OS-level locking (not a hand-rolled
  "lock file" convention) is auto-released by Windows the moment the
  holding process ends - including a crash or power loss - so this can't
  get "stuck" the way a manual lock file could.
- **Automatic fallback to read-only mode** if a second machine tries to
  open an already-locked file - not an error, a different mode. Windows
  natively supports a file being open for exclusive writing by one
  process while other processes open it read-only at the same time; this
  is a standard sharing mode, not something built from scratch.
- **Read-only mode, UI requirements**:
  - Clear, unmissable indicator - the existing status bar (v0.9.8)
    switches to show something like "READ-ONLY — [staff member]'s
    session" instead of the normal version/filename text.
  - Add Entry / Delete / Edit Selected Row / Debtor / Cash all disabled,
    not just visually indicated - belt and suspenders, matching the
    existing pattern from Manage Names' Apply button (v0.9.7).
  - **Live polling refresh**, not push-based change notifications for
    v1 - periodically re-read the file and redraw. Safe specifically
    *because of* the existing atomic-write guarantee (temp file + rename
    means a poll can only ever see a fully-old or fully-new version,
    never a torn/partial write) - a property that wasn't built with this
    use case in mind but turns out to be exactly what makes polling safe
    here. OS-level change notifications would give near-zero latency
    instead of a few seconds, but are less certain to be reliable over a
    network share and are more to build correctly - only worth it if
    polling's delay proves genuinely annoying in practice.
  - **No highlighting of what changed, for v1** - the manager is already
    visually comparing against a paper manifest, so a plain in-place
    refresh is the starting assumption. Revisit only if that proves
    insufficient once it's actually in use.
- **Why single-writer, not read-write concurrent**: confirmed in
  discussion that reconciliation is always sequential in practice - a
  manager watching live tells a clerk to fix something, the clerk (who
  already holds write access) makes the fix, never two people writing at
  once. This means the "one writer, many readers" pattern is sufficient;
  a full concurrent-write/merge system was considered and explicitly not
  needed.
- **Why this makes the SQLite hybrid trustworthy, not just the live
  view**: because there's never more than one active writer, whatever's
  on the share at day-finalization time is guaranteed to be the genuine,
  fully-corrected version of the day - not a stale snapshot from a
  machine that happened to close first. This was a real, initially
  unnoticed gap (two machines independently editing the same day with no
  reconciliation would have silently corrupted the price-history data)
  that the single-writer design closes as a side effect, not a
  coincidence.

## Encryption

- **One shared, business-level AES key** - not a per-machine or
  per-Windows-login key - is what makes a file encrypted on one machine
  readable on another, which any single-machine-scoped scheme (plain
  DPAPI, TPM-bound keys) cannot do. Uses Windows' built-in `bcrypt.dll` -
  no new external dependency for the actual cryptography.
- **DPAPI caches the *key*, not the data.** The shared password is
  entered once, at first setup on a given machine. From then on, a
  DPAPI-wrapped local copy of the derived key is used silently at every
  startup - no password prompt on an already-set-up machine, ever again.
- **AD gates distribution of the shared password, and doubles as the
  access whitelist.** The password lives on a network location
  permissioned to a specific AD security group (standard NTFS/share
  permissions - unchanged in capability across AD versions, including
  old ones). A machine/user must already be an authorized group member
  to retrieve it in the first place. Refined during discussion: the app
  should check the *already-existing* Windows/Kerberos session's group
  membership in the background, rather than prompting for a separate
  login - since a domain-joined user has already authenticated to AD
  just by logging into Windows.
- **Recovery**: one physical, non-digital copy of the shared password
  (a safe, a sealed envelope) as the break-glass fallback if every
  machine's local DPAPI cache is ever lost and nobody remembers it.
- **Optional future hardening, not required for v1 of this design**:
  TPM-sealed storage for the locally-cached key specifically (not for
  the data files themselves), on machines that have a TPM - narrows the
  one real remaining gap (someone with admin access to an already-
  logged-in, stolen machine) without changing the overall design.
  Worth checking actual fleet hardware before treating this as more than
  a nice-to-have.
- **What this does and doesn't protect against, stated plainly**: a file
  leaving custody without the app/key (lost USB, misdirected email,
  stolen backup) is unreadable - directly answers the original concern.
  It does **not** protect against someone with access to an already
  logged-in, already set-up machine (a physical/Windows-login/screen-
  lock problem, not something a file-encryption scheme can fix), or an
  authorized person misusing their own legitimate access (a people
  problem, not a technical one).
- **A OneDrive/Google Drive-synced copy of the encrypted file is exactly
  as unreadable as one on a network share** - the encryption is applied
  before anything else touches the file, so it's opaque bytes to any
  hosting mechanism. Where the file lives only ever affects the
  *locking/concurrency* behavior below, never the confidentiality
  question - these are fully independent layers. (Their own cloud-
  provider encryption protects a different threat - interception in
  transit, or a breach of Microsoft's/Google's own servers - not "someone
  reads the file once it's synced to a local machine," which is the
  actual stated concern here.)

## Why not OneDrive/Google Drive as the primary hosting mechanism

Consumer sync tools give each machine its own local copy in its own sync
folder, kept aligned *after the fact* by a background agent - not one
shared file with OS-enforced locking the way a real network share
provides. Two machines opening the file around the same time doesn't
produce "file in use, try again" - it can produce two genuinely divergent
local copies, which the sync tool resolves (safely, but not preventively)
by creating a "conflicted copy" once it notices. That's the exact failure
mode the single-writer lock design exists to make impossible rather than
just recoverable. (Office's real-time co-authoring on OneDrive is a
Microsoft-built exception specific to their own apps' protocol, not
something any third-party app gets automatically by saving into a synced
folder - replicating that would mean integrating with Microsoft's Graph
API, a fundamentally different and much larger undertaking.)

A synced folder remains a fine choice for a single person's own
cross-device backup/access - just not for the "clerk and manager working
against the same day at once" case this design is built around.

## Hosting options considered

- **A purpose-built NAS** (Synology, QNAP, etc.) - real SMB shares with
  genuine locking, small attack surface, auto-updating firmware, one-time
  hardware cost. Probably the strongest low-effort fit.
- **A self-built Linux box running Samba** - the same underlying
  technology most NAS appliances actually run, DIY instead of bought.
  Zero licensing cost (no Windows Server CALs), mature and
  well-supported, including AD integration (`realm join`/`winbind`/
  `sssd`) for the group-permission design above. The real, honest
  tradeoff: patching and maintenance become the owner's ongoing
  responsibility rather than a vendor's - needs a genuine patching habit
  from day one, or it risks becoming the same kind of neglected liability
  the existing 2008 R2 server is today.
- **A cloud VM as a real file server** - same genuine locking, reachable
  from anywhere with internet access, but a new server someone has to
  own patching for, plus ongoing hosting cost.
- **Azure Files (or AWS FSx)** - a fully-managed SMB endpoint; Windows
  sees it as a normal network drive with real locking, and the cloud
  provider handles patching. Typically needs a VPN into the provider's
  network rather than a direct connection, since raw SMB (port 445)
  should never be exposed directly to the public internet - it's a
  well-documented, actively-exploited attack path (this rule applies
  regardless of which hosting option is chosen).
- **Remote/on-the-road access** (raised as a "blue sky" scenario for a
  manager): if machines are always on the same physical office network,
  none of the VPN discussion applies. If genuinely remote access is
  wanted, a VPN (or a lighter-weight modern option like Tailscale, which
  pairs particularly easily with a self-built Linux box) sits in front of
  whichever hosting option is chosen - never raw SMB exposed directly.

## Explicitly rejected approaches (with reasoning, so they aren't
reconsidered without remembering why)

- **A custom backend server / API gateway, with the client apps never
  touching raw files** - reintroduces exactly the "new network-facing
  service that needs ongoing security maintenance" problem this whole
  conversation started from (the unpatched AD server). Also breaks the
  app's core reliability guarantee (offline capability, crash/power-loss
  safety built across v0.9.8-v0.9.14) by making basic operation depend on
  live backend availability.
- **A cloud KMS / hybrid key management service** - same new-
  infrastructure-to-maintain problem as above, narrower in scope, plus a
  real internet dependency and recurring cost neither previously
  discussed nor decided on.
- **Machine-bound (TPM) keys with a backend sync step** - self-
  contradicting on inspection: to let Machine B read Machine A's data,
  Machine A must decrypt to plaintext and transmit it, so the elaborate
  per-machine encryption only ever protects data that never needs to
  leave its own machine - the case that matters least here. Also
  centralizes plaintext on a new backend, a more attractive target than
  the status quo, not a smaller one.
- **Simple obfuscation** (XOR/custom encoding) - not rejected for zero
  value (it would stop a casual, non-technical viewer, which is a
  realistic scenario for a lost file) but for a real cost outweighing
  that: false confidence. Once multiple people are involved, especially
  anyone without the full context of this discussion, a wrong belief
  that data is protected can lead to less careful handling than
  correctly knowing it isn't - a worse outcome than the modest protection
  obfuscation actually provides.

## Open risks, not yet resolved

- **Network lock reliability during a brief disconnect**: does the
  exclusive lock survive a momentary network drop, or could a second
  machine acquire it while the first is only temporarily unreachable
  rather than actually finished? Needs real testing against whichever
  hosting option is chosen, not assumed.
- **Share unavailable at session start**: what should happen if a
  machine can't reach the share at all when someone tries to begin the
  day - not designed yet.
- **Atomic-write behavior over a network filesystem specifically**: the
  existing `WriteFileAtomicUtf8` (temp file + rename) should preserve its
  atomicity guarantee on a network share in principle, since the temp
  file sits alongside the real one on the same share - but this has real,
  not full, confidence, since SMB behavior can vary by server/
  configuration in ways local NTFS doesn't. Needs actual testing, not
  just this reasoning, before being relied on.
