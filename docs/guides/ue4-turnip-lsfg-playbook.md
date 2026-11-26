# Peanuts / Banana Native — UE4, Turnip T30 & LSFG Fast Troubleshooting Playbook

Distilled from the *Hellblade: Senua's Sacrifice* (`010044500CF8E000`) porting and polish loop on the **Lenovo Legion Y700 Gen 4** (`TB322FC`, Snapdragon 8 Elite `SM8750P`, Adreno 830, Android 16 16KB Page Kernel). Use this playbook to diagnose and fix rendering, flicker, video, and frame-pacing bugs in minutes instead of hours.

---

## 0. Non-Negotiable Rules for Every Loop

1. **Always Visually Inspect Real Gameplay Frames:** Never rely solely on numeric averages or static screenshots when the user reports flicker, blur, or motion bugs. Capture a 2–4 s 60 FPS video via `screenrecord`, extract consecutive frames with `ffmpeg -vsync 0`, and inspect zoomed crops + **1-frame diff heatmaps** (`abs(frame[n+1] - frame[n])`) directly with `read_file`.
2. **Never Uninstall User Packages:** Always update APKs with `adb install -r`. Back up and MD5-verify saves (`/storage/emulated/0/Android/data/<pkg>/files/saves/<TID>/`) before any New Game test or APK install.
3. **Verify Logs in App Files, Not Just `logcat`:** Peanuts Native writes full `[dm]` / `[vk]` / `[nxlaunch]` logs to:
   ```sh
   /sdcard/Android/data/<pkg>/files/logs/*.log
   ```

---

## 1. Symptom → Root Cause → Fast Fix Matrix

| Symptom | Root Cause | Fast Diagnostic (≤ 60 s) | Verified Fix |
|---|---|---|---|
| **Camera-angle screen whiteout / bloom explosion** (e.g. UE4 landscape materials `m_BlendMaster` / `m_bgrMountains`) | Qualcomm proprietary driver (`driver=system`, `0x80320048`) miscompiles specific UE4 material shaders (and/or `DM_VK_GUEST_FPCR=1` flush-to-zero). | Compare `DM_VK_DRIVER=system` vs Mesa Turnip T30 at the exact whiteout camera angle. | Use Mesa Turnip T30 (`recommend any driver=18` in `game.tsv` — must be numeric `VkDriverId` `18`, **never** a string like `turnip_t30`). Keep Turnip defaults (`UBWC`, `ZSHARE`, `VIN_DYN`, `LRZ` **on**; disabling them causes blocky menu artifacts and higher GPU time). |
| **"Start New Game" or cutscenes stay completely black while audio plays** | `DM_VK_TEX_TRACK=0` disables dirty-page tracking (`vka_tex.c`) for CPU-written textures, freezing CPU-decoded Bink Video (`QLOCBinkCutsceneHelperMaterial`) and dynamic UI/intro textures at all-zero black. | Check `env.txt` for `DM_VK_TEX_TRACK=0` and check log line `CPU-written textures alive`. | **Remove `DM_VK_TEX_TRACK=0`** (always keep default `1`). |
| **Violent 30 Hz strobing & flashing black vertical bars during camera pan with LSFG (`DM_LSFG=1`) on Turnip** | Mesa Turnip T30 uses host-signaled timeline semaphores (`V.tl_host=1`). If `vka_fg_frame()` submits `fg_cb` waiting on an unsignaled `V.tl` without pre-waiting, Turnip defers `fg_cb` while `present_one(&gi)` (`gi.fence == 0`) jumps ahead on KGSL, showing a stale 2–3 frame old `F.out[0]` and tearing when overwritten. | Record 3 s `screenrecord` while panning (`RRight~2.0`), track a landmark's X-coordinate frame-by-frame. Backwards jumps (`G5 → G3 → G6 → G4`) prove timeline/present reordering. | **Fixed permanently in `dm/gfx/vk_android/vka_framegen.c`:** `vka_fg_frame()` calls `vka_tl_wait(V.tl, fence, vka_hang_ns())` and drains pending Turnip gates (`vka_gate_drain()`) before submitting `fg_cb`. |
| **Character hair, feathered neckpiece, torso/shirt flickering, smearing, or grey ghosting halo** (while static walls look fine) | UE4's animated skeletal mesh (`GPUSkinCache` / vertex factory) writes a **corrupted velocity buffer**, breaking both UE4 TAA (`r.DefaultFeature.AntiAliasing=2`) and Motion Blur (`r.MotionBlurQuality>0`). Half-res checkerboard SSS (`r.SSS.Checkerboard=2`), `r.SSS.Quality=1`, `r.ContactShadows=1`, `r.Shadow.PerObject=1`, RCAS sharpening (`DM_VK_FSR=1`), and low LSFG flow (`50%`) compound the shimmer. | Capture 1 s `screenrecord` with `DM_LSFG=0`, build a **1-frame diff heatmap** (`abs(f[n+1] - f[n])`) zoomed on the character, and toggle CVars live via `ctl.txt` (`cvari <hex_off> <val>`). | Persist UE4 CVar overrides via `DM_UE_CVARS` (`dm/host/hle_uecvar.c`):<br>• `r.DefaultFeature.AntiAliasing = 1` (FXAA)<br>• `r.MotionBlurQuality = 0`<br>• `r.ContactShadows = 0`<br>• `r.SSS.Checkerboard = 0`, `r.SSS.Quality = 0`<br>• `r.Shadow.PerObject = 0`<br>Set `DM_VK_FSR=0`, `DM_VK_NDSR=0` (bilinear), and `DM_LSFG_FLOW=75`. (Reduced Hellblade character flicker **100×**: `72,588 → 357` px >15 RGB.) |
| **Unstable 40–50 FPS or warm tablet thermals on heavy UE4 games** | Native 60 FPS hits 17–24 ms GPU spikes in heavy lighting/fog scenes, causing 30/60 Hz judder and elevated power draw. | Check `pace up to` in `logs/*.log` for `GPU busy p50/p90` and `dumpsys battery` temperature. | Lock base rate to **30 FPS** (`DM_FPS_TARGET=30`, `DM_FPS_LIMIT=30`) on the **120 Hz** panel (`DM_PRESENT_HZ=120`, `DM_VSYNC_HZ=120`, `DM_VK_FRAME_RATE=0`) + **LSFG 2x** (`DM_LSFG=1`, `DM_LSFG_MULT=2`, `DM_LSFG_TARGET=0`, `DM_LSFG_FLOW=75`). Delivers locked `60.0 FPS` at ~9–10 ms GPU busy and ~32–33 °C battery (`thermal 0`). |

---

## 2. Fast UE4 CVar Discovery & Persistent Overrides (`DM_UE_CVARS`)

### Finding Exact CVar Offsets in 5 Seconds
Decompress the game's `main` NSO and run `dm/tools/ue_cvar.py`:
```python
import sys, struct, lz4.block
sys.path.insert(0, "/home/nick/peanuts-native/dm/tools")
import ue_cvar

with open("/mnt/ally_backups/peanuts-dumps/<TID>/extracted/exefs/main", "rb") as f:
    raw = f.read()
t_off, t_loc, t_sz = struct.unpack_from("<III", raw, 0x10)
r_off, r_loc, r_sz = struct.unpack_from("<III", raw, 0x20)
d_off, d_loc, d_sz = struct.unpack_from("<III", raw, 0x30)
t_csz, r_csz, d_csz = struct.unpack_from("<III", raw, 0x60)
flags = struct.unpack_from("<I", raw, 0x0c)[0]
t = lz4.block.decompress(raw[t_off:t_off+t_csz], uncompressed_size=t_sz) if (flags & 1) else raw[t_off:t_off+t_sz]
r = lz4.block.decompress(raw[r_off:r_off+r_csz], uncompressed_size=r_sz) if (flags & 2) else raw[r_off:r_off+r_sz]
d = lz4.block.decompress(raw[d_off:d_off+d_csz], uncompressed_size=d_sz) if (flags & 4) else raw[d_off:d_off+d_sz]
img = bytearray(d_loc + d_sz)
img[t_loc:t_loc+t_sz] = t; img[r_loc:r_loc+r_sz] = r; img[d_loc:d_loc+d_sz] = d
b = bytes(img)

for name in [
    "r.DefaultFeature.AntiAliasing", "r.MotionBlurQuality", "r.ContactShadows",
    "r.SSS.Checkerboard", "r.SSS.Quality", "r.Shadow.PerObject",
    "r.TemporalAACurrentFrameWeight", "r.ScreenPercentage",
]:
    off, _ = ue_cvar.find(b, t_sz, r_loc, d_loc, name)
    if off:
        print(f"{name:36s} -> main+0x{off:x}")
```

### Live Testing via `ctl.txt` & Persisting via `DM_UE_CVARS`
- **Live test (instant, no restart):** Append `cvari <hex_off> <int>` or `cvarf <hex_off> <float>` to `/sdcard/Android/data/<pkg>/files/ctl.txt`.
- **Persist across boots (`dm/host/hle_uecvar.c`):** Add to `env.txt` (up to 16 comma-separated entries, applied to both game and render threads on every present):
  ```ini
  DM_UE_CVARS=0x520f2a0=i1,0x496c238=i0,0x496c340=i0,0x5175f08=i0,0x5175ec0=i0,0x51a6c50=i0
  ```
- Confirm in `logs/*.log` on cold boot: `[dm] main: Unreal CVar main+0x... (game/render thread) ... (DM_UE_CVARS)`.

---

## 3. Live `ctl.txt` & `screenrecord` Diagnostic Gotchas

- **Append, don't overwrite:** Use `printf "...\n" >> "$CTL"`. Overwriting with `>` only resets `ctl_main`'s read cursor if the file becomes smaller than the previous offset.
- **Key/stick syntax:** Raw key name + optional hold in seconds — e.g. `A`, `RRight~2.0`, `RLeft~2.5` (**no** `input` or `press` prefix).
- **Holds block `ctl_main`:** `ctl_main` sleeps for the full hold duration before reading the next line. Always place `env ...` or `cvari ...` lines **before** `RRight~2.0`.
- **LSFG intermediate dump:** `env DM_LSFG_DUMP=<N>` only triggers when `<N>` changes to a *different* integer value (`if (d != last)` in `vka_framegen.c`). Remember to delete temporary `lsfg-*.png` and `/sdcard/*.mp4` files after analysis.
- **1-frame diff vs multi-frame std:** On an animated idle character (breathing/head turns), multi-frame standard deviation mixes real animation with flicker. Always use **1-frame consecutive diff** (`abs(frame[n+1] - frame[n])`) on a tight ROI crop to isolate true per-frame flicker!
