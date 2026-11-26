# Hellblade: Senua's Sacrifice (`010044500CF8E000`) Profile

- **Developer / Porter:** Ninja Theory / QLOC
- **Engine:** Unreal Engine 4 (`UE4Game`), NVN
- **Target Platform:** Lenovo Legion Y700 Gen 4 (`TB322FC`), Snapdragon 8 Elite (`SM8750P`), Adreno 830 GPU
- **Display Target:** 2560x1600 (16:10, 165Hz)
- **Performance Profile:** Rock-solid 60 FPS, < 5W low thermals, upgraded 1080p+ render resolution, enhanced LOD and TAA sharpness

---

## 🎯 Architecture & Optimizations

1. **Memory & Allocator:**
   - `OBS_UE_MALLOC`: Cuts `FMallocBinned3` pool allocations to utilize host `hle_uemalloc.c`.
   - `mem_reserve 0x140000000` / `mem_alias 0x118000000`: 5.0 GB Engage partition layout with 4.375 GB alias region.
   - `game_args -SwitchVirtualAddressSpaceSize=4064`: Sized UE4 virtual address space reservation to 4.064 GB (overcoming the 2.00 GB OOM barrier on level load while staying within Android's 39-bit address space).
   - `ue_texture_pool 512`: Balanced texture streaming cache for Senua's character model and environment meshes.
2. **Visual & Resolution Upgrades:**
   - Switch 1 Asset Gate: 100% Native Pass-Through (zero downscaling or FP16 degradation).
   - Dynamic Resolution Scaling (DRS) floor disabled (`r.DynamicResolution 0`).
   - Internal render target clamped to 1080p docked.
   - Enhanced static mesh LOD distance scale (`r.StaticMeshLODDistanceScale 1.5`) eliminating pop-in.
   - TAA sharpening (`r.Tonemapper.Sharpen 0.6`, `r.TemporalAACurrentFrameWeight 0.20`) eliminating Switch 1 Vaseline blur.
3. **Pacing & Thermals:**
   - 60 FPS cap (`fps_target 60`, `ue_fps_cvar Project.FrameRateLimit`) preserves battery life and limits thermal dissipation to under 5W on the Snapdragon 8 Elite.
4. **Anti-Flicker, New Game Video Fix & Locked 60 FPS Frame Generation:**
   - **New Game Black Screen Fix (`DM_VK_TEX_TRACK=1` default):** Hellblade uses CPU-decoded Bink Video (`QLOCBinkCutsceneHelperMaterial`) and 83 active CPU-written textures in the New Game river canoe intro. Never set `DM_VK_TEX_TRACK=0` (which prevents dirty-page re-uploads and leaves New Game completely black with audio playing).
   - **Hair, Neck Garment & Shirt Flicker Elimination (`DM_UE_CVARS=0x520f2a0=i1,0x496c238=i0,0x496c340=i0,0x5175f08=i0,0x5175ec0=i0,0x51a6c50=i0` + `DM_LSFG_FLOW=75`):** Single-frame pixel-diff heatmap ablation proved that UE4's skeletal mesh velocity buffer is corrupted on Senua (causing both `r.MotionBlurQuality>0` horizontal streaks while standing still and severe 72,588-pixel alternating-frame TAA shimmer/ghosting across her hair, feathered neckpiece, and leather shirt even at `CurrentFrameWeight=0.55`). Switching to FXAA (`r.DefaultFeature.AntiAliasing=1`, `main+0x520f2a0`), disabling motion blur (`r.MotionBlurQuality=0`, `main+0x496c238`), contact shadows (`r.ContactShadows=0`, `main+0x496c340`), half-res checkerboard/shimmering SSS (`r.SSS.Checkerboard=0`, `r.SSS.Quality=0`, `main+0x5175f08`/`0x5175ec0`), and per-object character shadow crawl (`r.Shadow.PerObject=0`, `main+0x51a6c50`), plus raising LSFG optical flow to `DM_LSFG_FLOW=75`, cuts single-frame character flicker **100×** (from `72,588` to `357–743` pixels >15 RGB) while keeping Senua razor-sharp with zero skin checkerboard.
   - **Clean Turnip T30 Defaults:** Keep `DM_VK_UBWC`, `DM_VK_ZSHARE`, `DM_VK_VIN_DYN`, and LRZ at their Turnip T30 defaults (disabling UBWC/LRZ caused rectangular block artifacts on the Main Menu and higher GPU frame times). Keep `DM_VK_PSO_ASYNC=0` and `DM_VK_PSO_PREP=2` for synchronous shader compilation, and pure bilinear presentation (`DM_VK_FSR=0`, `DM_VK_NDSR=0`) to avoid RCAS sharpening of dithered feather tips.
   - **Locked 60 FPS Presentation via LSFG 2x (`30 → 60 FPS`) + Turnip Timeline Pre-Wait Fix (`vka_framegen.c`):** On the Y700's 120 Hz panel (`DM_PRESENT_HZ=120`, `DM_VSYNC_HZ=120`, `DM_VK_FRAME_RATE=0`), capping the internal engine at `30 FPS` (`DM_FPS_TARGET=30`, `DM_FPS_LIMIT=30`) leaves GPU busy at only ~10.2 ms out of 33.3 ms (~31% load, thermal level 0). Fixed a critical Mesa Turnip T30 host-signaled timeline (`V.tl_host == 1`) race in `dm/gfx/vk_android/vka_framegen.c`: previously `vka_fg_frame` submitted `fg_cb` waiting on an unsignaled `V.tl` without calling `vka_tl_wait` or `vka_gate_drain`, causing Turnip to defer `fg_cb` while `present_one(&gi)` (`gi.fence == 0`) jumped ahead and displayed a stale 2-frame-old `F.out[0]` (and tore/flashed black vertical bars on concurrent overwrite). Pre-waiting `vka_tl_wait(V.tl, fence)` and draining pending Turnip gates before submitting `fg_cb` guarantees strict FIFO GPU execution (`game -> fg_cb -> present(gen0) -> present(game)`) with rock-solid **60.0 FPS** monotonic motion.
   - **Mesa Turnip T30 Driver Deployment:** Resolves Qualcomm driver compiler whiteout bugs on landscape materials (`m_BlendMaster` / `m_bgrMountains`).
