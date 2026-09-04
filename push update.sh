#!/usr/bin/env bash
# Run this from inside your local BalancingAct repo folder, AFTER copying
# the v0.9.16 zip's contents into it (overwriting existing files, adding
# new ones - tests/, NETWORK_ARCHITECTURE.md, etc.)
#
# This is a "catch-up" commit, not a clean per-version history - Claude's
# sandbox doesn't preserve snapshots at each version boundary, only the
# current final state, so this captures everything since the last real
# sync point (v0.9.0) as one well-described commit rather than faking a
# granular history that doesn't actually exist. Claude has no GitHub
# credentials in its own environment (verified directly - no SSH keys, no
# token, no credential helper) - this script runs with YOUR already-
# authenticated local git/GitHub setup instead.
#
# Usage:
#   bash push_update.sh

set -e

echo "Staging all changes..."
git add -A

echo "Committing..."
git commit -m "Catch-up sync: v0.9.1 through v0.9.16 (data integrity, testing, UX, docs)" -m "$(cat <<'EOF'
This is a catch-up commit covering everything since the last real sync
point (v0.9.0 confirmed working) - not a per-version history, since
intermediate snapshots weren't preserved. Grouped by category:

Data-integrity fixes (external audit follow-up):
- LoadFromFile: transactional loading, rejects malformed/truncated/NaN-
  laden files instead of silently corrupting the recovery autosave
- Undo Delete no longer contaminates a different sheet after New/Open/
  Recent Files/rename
- Total Overview tab: fixed showing Kgs under the "Total ($)" column
- Every save path (.fbd, settings.txt, recent.txt, emails.txt, CSV) now
  atomic and checked, not a truncate-and-hope
- Supplier email addresses now migrate correctly on rename/merge, with a
  user prompt when merged suppliers have conflicting saved emails
- Debtor/Cash and the in-progress "Add Entry" draft now persist reliably
  (autosave on focus-loss, not per-keystroke - a real performance fix at
  business volume of 500-1000 entries/day)
- Debtor/Cash expression parser: subtraction now works correctly
  ("123+11-21" computes 113, not 134)
- Print Preview memory use capped (150 DPI preview cap + page limit)

Testing infrastructure (new):
- FishBalanceCore.h: platform-independent core logic extracted from
  main.cpp, zero Win32 dependency
- doctest-based suite in tests/ - 83 test cases / 244 assertions,
  covering parsing, formatting, aggregation, CSV/email logic, and a
  regression test for every fixed bug above

Build quality:
- Zero warnings under MSVC /W4 (was 12), all genuinely fixed not
  suppressed - CMakeLists.txt /EHsc, real safe-CRT usage under MSVC,
  portable fallback under MinGW, uninitialized-variable false positives
  cleaned up
- NOMINMAX fix for a std::min/std::max + windows.h macro collision

UI/UX:
- Combo box first-paint rendering bug fixed
- Manage Names hint text no longer cut off
- Status bar added (version + current filename)
- Data entry workflow: Species clears after Add Entry (Supplier stays),
  new "Duplicate Supplier & Species" button, Manage Names "Apply" button
  reflects whether there's actually anything to apply

Architecture and planning (docs):
- ARCHITECTURE.md: tab-vs-menu-item placement principle
- BUSINESS_RULES.md: corrected business model documentation (consignment
  agency, not buy-resell - Price is market price achieved, not cost)
- NETWORK_ARCHITECTURE.md (new): full multi-machine/encryption/hosting
  design - shared-key encryption with local DPAPI caching, AD-gated
  access, single-writer file locking with read-only fallback, SQLite
  hybrid reporting layer
- SecurityHardeningRegister.md: encryption plan finalized, cross-
  referenced rather than duplicated
- ROADMAP.md: three-bucket sequencing (A: single-machine complete; C:
  multi-machine/security; B: main.cpp decomposition), Price History
  feature fully spec'd
EOF
)"

echo "Pushing to origin..."
git push

echo "Done."