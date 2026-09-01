# Phase 3 Clean-Cache-Scanner Smoke Test Guide

## Prerequisites
- Windows 10 version 17763+ or Windows 11
- Visual Studio 2022 with C++ desktop development
- Built Orbit.sln in Debug|x64 configuration
- At least a few files in %TEMP% for testing

## Test Scenarios

### 1. Basic Scan Flow
**Objective:** Verify scan completes and shows results

1. Launch Orbit
2. Navigate to "Earth — Clean" page
3. Click **Scan caches**
4. **Expected:**
   - Scan progress bar appears
   - Categories populate sorted by size (largest first)
   - No UI freeze during scan
   - Status shows "Scan complete — review selections before cleaning"
   - Action bar appears at bottom with selection summary

**Success Criteria:**
- ✅ Scan of %TEMP% completes quickly (~<2s for ~5k files)
- ✅ UI remains responsive (NavigationView clickable during scan)

### 2. Hardlink Deduplication
**Objective:** Verify hardlinks count only once

**Setup:** Create hardlinks in %TEMP% for testing:
```cmd
cd %TEMP%
echo test > hardlink_source.txt
mklink /H hardlink_copy1.txt hardlink_source.txt
mklink /H hardlink_copy2.txt hardlink_source.txt
```

1. Run scan
2. Expand "User Temp" category
3. Find the hardlink files
4. **Expected:**
   - First occurrence shows size
   - Duplicate hardlinks show 0 bytes with tooltip "Duplicate hardlink; excluded from reclaimable total"
   - Total reclaimable size counts file only once

**Success Criteria:**
- ✅ Hardlink file with 2+ links counted once in TotalReclaimable

### 3. System Path Protection
**Objective:** Verify C:\Windows\System32 never appears

1. Run scan on all categories
2. Expand "System Temp" category (if accessible)
3. Check all file paths in results
4. **Expected:**
   - No paths starting with `C:\Windows\System32`
   - If System Temp requires elevation, category shows "Skipped: Requires elevation"

**Success Criteria:**
- ✅ C:\Windows\System32 never appears in file list

### 4. Delete to Recycle Bin (Default)
**Objective:** Verify FOF_ALLOWUNDO moves to Recycle Bin

1. Settings → Delete mode → ensure "Move to Recycle Bin (recommended)" selected
2. Scan caches
3. Expand "User Temp" category
4. Select a few test files (create safe dummy files first)
5. Click **Move to Recycle Bin**
6. Confirm dialog
7. **Expected:**
   - Files removed from list
   - InfoBar: "Clean complete — N items cleaned. Use File Explorer to restore recycled files. Operation logged to history.jsonl."
   - "View log" button visible
   - Files in Recycle Bin (check via File Explorer)

**Success Criteria:**
- ✅ Delete moves to Recycle Bin (FOF_ALLOWUNDO), not silent permanent

### 5. Permanent Delete (Two Confirmations)
**Objective:** Verify permanent delete requires two explicit confirmations with red warning

1. Settings → Delete mode → **Permanent delete**
2. Warning InfoBar appears: "Permanent deletion enabled..."
3. Navigate back to Clean page
4. Scan and select files
5. Click **Permanently delete**
6. **Expected:**
   - First dialog: "Review permanent deletion — N items. This cannot be undone."
   - Click Continue
   - Second dialog: "Permanently delete selected items? Orbit will bypass the Recycle Bin. This action cannot be undone."
   - Default button is Cancel (not Continue)
7. Confirm both → files permanently deleted

**Success Criteria:**
- ✅ Permanent delete confirmed twice with warnings

### 6. Whitelist Persistence
**Objective:** Verify whitelisted paths excluded after restart

1. Scan caches
2. Expand a category
3. Right-click a file → "Protect this path from future scans"
4. **Expected:** InfoBar "Path protected — excluded now and on future scans"
5. File removed from list immediately
6. **Or** via top button: Click "Add protection" → enter pattern (e.g., `C:\Temp\important\**`)
7. Re-scan → path excluded
8. Restart Orbit → scan again
9. **Expected:** Whitelisted path still excluded

**Success Criteria:**
- ✅ Whitelisted path excluded on next scan
- ✅ Whitelist persists after restart (stored in LocalSettings + JSON)

### 7. Cancel Scan
**Objective:** Verify cancel aborts within ~1s with partial results

1. Click **Scan caches**
2. Immediately click **Cancel scan** (within 1-2 seconds)
3. **Expected:**
   - Scan stops within ~1 second
   - Partial results displayed
   - Status: "Cancelled — partial results shown"
   - InfoBar: "Scan cancelled — Partial results are shown and can be reviewed"
   - Badge or label shows "Cancelled"

**Success Criteria:**
- ✅ Cancel aborts within 1s with partial results + Cancelled badge

### 8. Category Tiers & Risky Items
**Objective:** Verify .exe/.dll in temp marked Risky, never auto-selected

1. Place a .exe or .dll in %TEMP% (copy any safe system binary)
2. Scan caches
3. Expand "User Temp"
4. Enable "Show risky items" toggle
5. **Expected:**
   - .exe/.dll files show tier badge "Risky" (different color)
   - Risky files NOT checked by default
   - Category select-all skips risky files
   - Disabling "Show risky" hides risky files

**Success Criteria:**
- ✅ *.exe/*.dll in temp are Risky, never auto-selected

### 9. OneDrive Placeholders
**Objective:** Verify FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS files shown but not selected

1. If OneDrive sync active, locate a cloud-only file (placeholder with download icon)
2. Create temp folder with OneDrive placeholder files
3. Scan
4. **Expected:**
   - Placeholder files appear in "Skipped" expander
   - Reason: "OneDrive cloud placeholder — not selected"
   - Never auto-selected for deletion

**Success Criteria:**
- ✅ OneDrive placeholders shown but never auto-selected

### 10. Skipped / In-Use Files
**Objective:** Verify access-denied / in-use files listed with reason

1. Create a locked file in %TEMP%:
   ```powershell
   $file = [System.IO.File]::Open("$env:TEMP\locked.txt", "Create", "ReadWrite", "None")
   # Keep handle open
   ```
2. Scan
3. Expand category with skipped files
4. **Expected:**
   - "N paths skipped" section
   - Each skipped file with reason (e.g., "Access denied or enumeration failed", "Requires elevation")

**Success Criteria:**
- ✅ Access-denied / in-use → Skipped expander with reason

### 11. Empty Recycle Bin
**Objective:** Verify explicit Recycle Bin emptying

1. Put some files in Recycle Bin (via File Explorer or prior clean)
2. Scan caches
3. **Expected:**
   - Recycle Bin category shows size + item count
   - Action bar has **Empty Recycle Bin** button (enabled)
4. Click **Empty Recycle Bin**
5. Confirmation dialog: "The Recycle Bin contains N items (X). Emptying permanently deletes them."
6. Confirm
7. **Expected:**
   - Recycle Bin emptied
   - InfoBar: "Recycle Bin emptied — All recycled items were permanently deleted."
   - "View log" button visible

**Success Criteria:**
- ✅ Empty Recycle Bin explicit with confirmation

### 12. Categories Present
**Objective:** Verify all 11 required categories

Expected categories (some may be "Not present on this system"):
1. User Temp (%TEMP%, %LOCALAPPDATA%\Temp)
2. System Temp (C:\Windows\Temp)
3. Windows Update Cache (SoftwareDistribution\Download)
4. Delivery Optimization
5. Thumbnail Cache (thumbcache_*.db)
6. DirectX Shader Cache
7. Browser Temp (Edge, Chrome cache)
8. Dev Caches (npm, yarn, pnpm, pip, NuGet HTTP cache)
9. Windows Error Reports
10. Prefetch (read-only hint, non-recyclable)
11. Recycle Bin (size-only summary)

**Success Criteria:**
- ✅ All 11 categories present in registry

### 13. View Operation Log
**Objective:** Verify OperationLog.h appends and opens

1. Complete a clean operation
2. InfoBar shows "View log" button
3. Click **View log**
4. **Expected:**
   - File Explorer opens to `%LOCALAPPDATA%\Orbit\`
   - `history.jsonl` present with JSON entries

**Success Criteria:**
- ✅ After delete, OperationLog appended and "View log" opens folder

### 14. NuGet HTTP Cache (Not Global Packages)
**Objective:** Verify Dev Caches only targets NuGet HTTP cache

1. Check Dev Caches probed paths (or read `CleanCategoryProber.h:136`)
2. **Expected paths:**
   - `%LOCALAPPDATA%\NuGet\v3-cache` ✅
   - `%USERPROFILE%\.nuget\v3-cache` ✅
   - **NOT** `%USERPROFILE%\.nuget\packages` (actual package DLLs)

**Success Criteria:**
- ✅ NuGet v3-cache (HTTP metadata) probed, NOT global-packages

### 15. Performance & Memory
**Objective:** Verify no UI freeze and bounded memory

1. Scan with large temp folders (50k+ files if available)
2. **Expected:**
   - Scan completes without UI hang
   - Category file lists cap at ~50k with "Show more (N hidden)"
   - Memory discipline (no leaks after repeated scans)

**Success Criteria:**
- ✅ Scan 100k files without UI freeze (chunked/coalesced updates)

## Build Verification

**On Windows with VS 2022:**
```cmd
nuget restore Orbit.sln
msbuild Orbit.sln /p:Configuration=Debug /p:Platform=x64 /v:minimal
```

**Expected:**
- ✅ Build succeeds with no new /W4 warnings
- ✅ No errors related to Phase 3 additions

## Known Limitations (Not Blocking)
- Some System Temp / WER paths require admin elevation (expected, shows skip reason)
- Very large directory trees (500k+ files) may take minutes (acceptable for comprehensive scan)
- Prefetch is read-only size hint (never deleted, by design)
- Recycle Bin empty is system-wide (cannot filter by category)

## Regression Checks
- ✅ NavigationView planet shell still works
- ✅ Settings page delete mode toggle persists
- ✅ Other pages (Analyze, Software, Optimize, Status) unaffected
