# Agent guide: UE5_Spout2_DX12

This file applies to the entire plugin repository. Read it before changing code, build rules, bundled libraries, or documentation. Follow explicit user scope and any more specific directory instructions. Keep this guide current when architecture or development procedures change.

## Purpose and supported scope

This is a Windows/Win64 Unreal Engine runtime plugin for GPU texture sharing through Spout and D3D11On12 while Unreal uses the D3D12 RHI. It provides Blueprint-spawnable sender and receiver actor components. It is a plugin repository, not a standalone Unreal project.

The inspected baseline is `Spout2_DX12.uplugin` version name `2.1.1` (numeric `Version: 3`), with one `Runtime` module, `Spout2_DX12`, loaded at `Default`. The descriptor permits content, but no Content directory or sample `.uproject` is currently tracked.

`README.md` reports Windows/DX12 testing on UE 5.2.1, 5.4.4, 5.6.1, 5.7.2, 5.8 Preview, and 5.8. These are historical project claims, not proof that a new change works on those versions. Do not infer DX11, Vulkan, Linux, macOS, headless server, or cross-adapter support from conditional compilation or the presence of other SDK libraries.

## Start each task

1. Read the request and inspect `git status --short` and relevant diffs. Preserve existing work; do not reset, clean, or overwrite unrelated changes.
2. Read this guide, `README.md`, the plugin descriptor, and the files relevant to the requested behavior. Source and build rules take precedence when prose disagrees with implementation.
3. Read `local plans.md` if present. It is a private working notebook, not a committed specification or authorization to implement every backlog item. Create it if absent when planning substantial work.
4. Establish the Unreal version, host project, active RHI, execution mode, and sender/receiver configuration relevant to the task. Do useful inspection before asking for information that cannot be inferred locally.
5. For nontrivial work, record scope, acceptance criteria, steps, validation, and unresolved questions in the local plan. Keep changes limited to the request.

Use `rg` / `rg --files` for navigation. Useful entry points:

```powershell
git status --short
rg --files Source Config
rg -n 'ENGINE_MINOR_VERSION|WITH_EDITOR|PLATFORM_WINDOWS' Source/Spout2_DX12
rg -n 'StartBroadcast|StartReceiving|StopBroadcastInternal|StopReceivingInternal' Source/Spout2_DX12
rg -n 'Fence|Flush|AcquireWrappedResources|ReleaseWrappedResources' Source/Spout2_DX12/Private
```

If Git reports dubious ownership for this known checkout, use a command-scoped exception, for example `git -c safe.directory=D:/UE5_Spout2_DX12 status --short`. Use the actual checkout path. Do not change global Git trust settings or trust every directory to work around it.

## Repository map

| Path | Responsibility |
| --- | --- |
| `Spout2_DX12.uplugin` | Plugin identity, release metadata, and runtime module registration. |
| `Source/Spout2_DX12/Spout2_DX12.Build.cs` | Active Unreal dependencies, SDK include/lib paths, delay loading, and packaged DLL staging. |
| `Source/Spout2_DX12/Public/Spout2_DX12.h` | Module interface and `LogSpoutSender` / `LogSpoutRX` declarations. |
| `Source/Spout2_DX12/Private/Spout2_DX12.cpp` | Module startup/shutdown and explicit `SpoutDX12.dll` loading. |
| `Source/Spout2_DX12/Public/SpoutSenderComponent.h` | Sender Blueprint API, configuration, staging slots, fences, and viewport callback state. |
| `Source/Spout2_DX12/Private/SpoutSenderComponent.cpp` | Sender lifecycle, source resolution, editor ownership, GPU copies, and Spout publication. |
| `Source/Spout2_DX12/Public/SpoutReceiverComponent.h` | Receiver Blueprint API, render targets, connection state, statistics, and fence state. |
| `Source/Spout2_DX12/Private/SpoutReceiverComponent.cpp` | Sender discovery, shared-resource reception, D3D11On12 copies, output publication, and cleanup. |
| `Source/Spout2_DX12/Public/SpoutSenderSource.h` | Serialized `ESpoutSenderSourceType` enum: RenderTarget, GameViewport, EditorViewport. |
| `Source/Spout2_DX12/Public/SpoutWorldPolicy.h` | Shared `ESpoutWorldBootstrapPolicy` enum. Each component implements its own policy checks. |
| `Source/Spout2_DX12/Public/Spout2BlueprintLibrary.h` and matching private `.cpp` | Empty Blueprint function-library scaffold; component methods currently implement the useful API. |
| `Source/ThirdParty/include/` | SDK headers used by the active runtime module. |
| `Source/ThirdParty/lib/Win64/` | SDK libraries; the active module links `Spout.lib` and `SpoutDX12.lib`. |
| `Source/ThirdParty/bin/Win64/` | SDK DLL sources; the active module stages `Spout.dll` and `SpoutDX12.dll`. |
| `Source/ThirdParty/Spout2_DX12Library/` | Alternate SDK layout and external-module Build.cs. It is not a dependency of the active runtime Build.cs. Do not assume changes here affect the plugin. |
| `Binaries/Win64/` | Tracked Spout DLLs plus prebuilt UnrealEditor plugin DLL, PDB, and `.modules` metadata. These are engine/build-specific artifacts. |
| `Config/FilterPlugin.ini` | Plugin packaging filter, currently containing example comments only. |
| `Resources/Icon128.png` | Plugin browser icon. |
| `SpoutSS/` | Screenshots referenced by README; these are documentation, not automated tests. |
| `README.md`, `LICENSE`, `CODE_OF_CONDUCT.md` | User documentation, repository MIT license, and community policy. Preserve vendor license notices separately. |
| `local plans.md` | Git-ignored local feature plans, decisions, reproduction notes, and verification results. |

There is currently no tracked automated test suite, CI workflow, build wrapper, or sample host project. Do not invent existing test commands or treat a shipped DLL as a fresh build result.

## Build dependencies and DLL flow

The runtime module uses Core, Projects, CoreUObject, Engine, RenderCore, RHI, D3D12RHI, Slate, and SlateCore. `UnrealEd` is added only for editor targets. Preserve that boundary so packaged game targets do not acquire editor-only dependencies.

For Win64, the active Build.cs:

- Includes `Source/ThirdParty/include` and links the two import libraries named above.
- Delay-loads `Spout.dll` and `SpoutDX12.dll` by name.
- Copies those DLLs to `$(BinaryOutputDir)` through `RuntimeDependencies` and also stages their source paths as `NonUFS`.

Module startup tries to load `SpoutDX12.dll` from plugin `Binaries/Win64`, project `Binaries/Win64`, and the executable base directory, in that order. DLL-directory pushes and pops are paired. Shutdown frees the held DLL handle. Loading failures currently use `LogTemp`.

For packaging or SDK changes, inspect Build.cs, loader search paths, actual package output, and plugin filtering together. Do not replace the staging mechanism with instructions to copy DLLs into the engine installation. Do not delete the alternate SDK layout, switch static/dynamic linking, or replace vendor binaries as incidental cleanup. An SDK upgrade must identify its provenance/version and keep headers, import libraries, DLLs, architecture, and redistribution notices consistent.

## Runtime architecture

### Sender

`USpoutSenderComponent` owns a `spoutDX12` bridge and up to two `FSpoutStageSlot` entries. Each slot contains a retained RHI texture, wrapped D3D11 resource, dimensions/format, a render-command fence, and a D3D11 GPU fence value.

Normal render-target and editor/PIE viewport flow:

```text
StartBroadcastConfigured -> EnsureBridge(UE native D3D12 device)
TickComponent -> UpdateTexture -> ResolveCurrentSource
  -> ready staging slot -> QueueSendFrame_RenderThread
  -> SendFrame_RenderThread -> RHI copy into staging texture
  -> wrap/acquire -> SendDX11Resource -> release -> signal fence
```

The non-editor game viewport path is different: it binds Slate's `OnBackBufferReadyToPresent`, filters for the intended window, throttles by time, checks slot readiness on the render thread, and calls `SendFrame_RenderThread` there. Component ticking is disabled for that path. Do not assume a working PIE viewport proves packaged viewport capture works.

The bridge opens against `GDynamicRHI->RHIGetNativeDevice()` when starting broadcast. Preserve this device relationship; do not revert to an independent Spout-created D3D12 device without an explicit interop design.

### Receiver

`USpoutReceiverComponent` uses `spoutDX` for sender information and `spoutDX12` for D3D11On12 access on Unreal's native D3D12 device.

```text
StartReceiving -> open bridge -> initialize D3D11 fence interfaces
TickComponent -> ReceiveOnce -> sender metadata/share handle
  -> cached opened D3D11 source -> local GPUCopy11 texture
  -> acquire wrapped UE destination -> copy -> release -> signal fence
```

Without double buffering, the wrapped destination is `OutputRenderTarget`. With double buffering, reception alternates `InternalRT_A` / `InternalRT_B`; `PublishCompletedInternalBuffer` selects the newest completed fence and enqueues a copy to `OutputRenderTarget`. Preserve the user-facing render target object so materials and Blueprint references remain valid.

`MapDxgiToUE` explicitly maps BGRA8/RGBA8 (including sRGB variants), RGBA16F, RGBA32F, and RGB10A2. Its default maps to BGRA8 with a warning; this is not a general pixel-format conversion pipeline. Audit size, format, gamma, sample count, and native-resource identity when touching copies or resize handling.

### Worlds, lifecycle, and API contracts

- Both components start with auto-start disabled, 60 FPS, double buffering disabled, and `StartupPolicy = GameOnly`.
- `IsSupportedWorld` accepts game worlds; policies additionally admit Editor and, for `EditorGameAndSinglePreview`, EditorPreview. Do not equate every preview-world type with EditorPreview.
- Editor startup runs through registration/property-change paths; game startup runs through BeginPlay. Unregistration and EndPlay must release resources safely.
- Desired activity (`bWantsBroadcasting` / `bWantsReceiving`) is separate from actual running state. Internal lifecycle stops can preserve intent; explicit user stops clear intent. Preserve this distinction across editor re-registration.
- Sender editor ownership is checked by scanning live sender components with the same name. An editor-world sender can displace a preview sender; preview duplicates are rejected. `ReleaseEditorOwnership` is currently empty. The receiver has no equivalent ownership arbitration; do not infer identical behavior from the shared enum name.
- `StartBroadcast()` uses configured settings. `StartBroadcastFromRenderTarget` and `StartBroadcastGameViewport` set source/buffering and force `GameOnly`.
- Public `StopBroadcast()` currently clears the configured render target, sender name, and FPS as well as activity intent. A stop/start workflow must restore settings or use the parameterized start functions. Changing this is a behavior change, not cleanup.
- Sender FPS <= 0 means unthrottled; positive send intervals clamp to 1-240 FPS. Receiver FPS <= 0 selects the intended one-shot path; positive receiver FPS clamps to 1-240. Do not unify these meanings.
- Empty receiver sender-name configuration consults Spout's active sender during startup. README's "first available" wording is not proof of a particular enumeration order or automatic failover.
- `TickAfterActor` adds/removes an actor tick prerequisite. Keep it balanced during replacement and shutdown; it does not establish GPU completion or control the Slate callback.

## Development rules

### Threading, synchronization, and ownership

1. Identify the calling thread for every changed path. Keep UObject creation, reflected-property updates, and editor interaction on the appropriate game/editor thread. Retain RHI references and capture stable data when queuing GPU work.
2. Render-command completion and GPU completion are different. Preserve both kinds of synchronization where needed; a queued command or `FlushRenderingCommands()` alone does not prove every D3D11On12 GPU operation is complete.
3. The sender render-thread callback must use `IsStageSlotReady_RenderThread`. Do not call `FRenderCommandFence::IsFenceComplete()` there; that check belongs to `IsStageSlotReady_GameThread`.
4. Keep acquire/release pairs balanced around wrapped-resource use, signal after release, and prove submitted work completes before reusing or destroying resources. Audit failure paths as well as the successful frame path.
5. Remove viewport delegates and stop new submissions before draining work and freeing slot resources, fence interfaces, and bridges. Account for queued lambdas that capture `this` and render-target resource pointers.
6. Track owned versus borrowed COM references. Release owned references exactly once, null released pointers, and invalidate cached wrappers when their underlying resource changes. Prefer scoped COM ownership in new code where compatible with existing lifetimes.
7. Do not remove flushes, fence waits, or RHI transitions merely to improve a benchmark. Document the replacement ordering and verify correctness under load, resize, restart, and teardown.
8. Conversely, avoid introducing steady-state CPU readback, allocations, blocking waits, or extra flushes without justification. Existing sender code already blocks/submits GPU work and flushes its D3D11 context; do not describe the current pipeline as stall-free or fully asynchronous.
9. A critical section used for sender enumeration does not make all receiver state or GPU operations thread-safe. Review the actual protected state before moving work to another thread.

### Unreal and source compatibility

- Follow the surrounding C++ style, Unreal naming conventions, reflection macros, and explicit/shared PCH setup. Avoid unrelated formatting changes.
- Keep each `.generated.h` last among includes in its reflected header. Prefer forward declarations for native interfaces in public headers and keep SDK/Windows includes in implementation files where practical.
- Preserve the existing `PLATFORM_WINDOWS`, `WITH_EDITOR`, Windows type wrappers, third-party include guards, and macro cleanup. Do not assume these currently make non-Windows builds supported.
- Preserve serialized property names, enum ordering/values, Blueprint function names/pins/defaults, categories, and tooltips unless the task explicitly changes the public contract. Use a migration/redirect strategy for intentional breaking changes.
- Review module export macros when exposing new C++ APIs across module boundaries; do not assume Blueprint visibility guarantees native linkage.
- Maintain deliberate engine-version branches. Sender code currently branches at UE 5.4 for texture creation, 5.6 for GPU submission, and 5.8 for Slate viewport-provider callbacks. Compile the affected branch/version before claiming support.
- Prefer narrow implementation changes over another manager/subsystem or module unless the feature needs it. Update both components deliberately when changing shared policy semantics.
- New editor-adjustable settings may require `PostEditChangeProperty` restart logic; Blueprint writes at runtime do not automatically invoke that editor path.

### Diagnostics and performance

Use `LogSpoutSender` and `LogSpoutRX` for component diagnostics. Include actionable context such as world/source, dimensions/format, sender name, and HRESULT when relevant. Keep per-frame traces at Verbose and rate-limit repeated failures. Do not expose sensitive local data in shared reports.

For performance work, record engine version, GPU/driver, resolution/format, source mode, target FPS, buffering, host frame rate, latency, and CPU/GPU timing before and after. Component counters are not substitutes for end-to-end frame delivery measurements.

## Build and validation workflow

Use a compatible installed Unreal Engine and Windows C++ toolchain. Confirm actual paths and the host project's target names; none are fixed by this repository. Close the relevant editor for module/reflection/native-library rebuilds when needed rather than relying on Live Coding to validate those changes.

The following are templates, not commands verified for a particular machine. Replace placeholders before running. Use a fresh disposable package destination outside this checkout and outside the host project; UAT may recreate its output directory. Never point `-Package` at the source plugin.

```powershell
$EngineRoot = 'C:\Path\To\UE_5.x'
$PluginRoot = (Get-Location).Path
$PackageOutput = 'C:\Path\To\DisposableOutput\Spout2_DX12'
& "$EngineRoot\Engine\Build\BatchFiles\RunUAT.bat" BuildPlugin "-Plugin=$PluginRoot\Spout2_DX12.uplugin" "-Package=$PackageOutput" -TargetPlatforms=Win64
```

For an existing C++ host project with this plugin installed:

```powershell
$ProjectFile = 'C:\Path\To\HostProject\HostProject.uproject'
$EditorTarget = 'HostProjectEditor'
& "$EngineRoot\Engine\Build\BatchFiles\Build.bat" $EditorTarget Win64 Development "-Project=$ProjectFile" -WaitMutex
& "$EngineRoot\Engine\Binaries\Win64\UnrealEditor.exe" $ProjectFile -dx12 -log
```

Check process exit codes and logs. Confirm the host actually builds/loads the changed plugin copy, especially if both project and engine plugin installations exist. Package the host separately to validate real game DLL staging and viewport capture; BuildPlugin alone is not an end-to-end game test.

Choose validation based on the change:

| Change | Required evidence where the environment permits |
| --- | --- |
| Documentation / ignore rules | Review paths, symbols, factual claims, Markdown, `git diff --check`, and `git check-ignore`. No Unreal rebuild is needed. |
| Reflected API or component logic | Compile/UHT in the target engine; exercise affected Blueprint nodes and lifecycle behavior in a host. |
| Sender source or synchronization | Exercise affected sources and both buffer modes with an external Spout receiver; verify actual frames, stop/restart, resize, and teardown. |
| Receiver copies or synchronization | Use an external sender; exercise both buffer modes, resolution/format changes, disconnect/restart, output stability, and one-shot mode. |
| World/editor policies | Test Editor, PIE, relevant preview worlds, registration/property changes, manual stop, and duplicate sender names. |
| Build rules, DLLs, loader, or Slate capture | Build plugin and host game; run packaged Win64/DX12 output and inspect DLLs/load logs. |
| Engine compatibility branches | Compile and run the changed branch on each engine version being claimed as validated. |

For broad rendering/lifecycle changes, cover render-target sending, editor viewport, PIE game viewport, standalone/packaged game viewport, and receiving. Include missing/null sources, absent senders, repeated start/stop, level changes, component destruction, viewport resize/minimize, and shutdown. Verify colors, alpha/gamma where applicable, frame continuity, and lack of stale sender registrations or growing resource usage. A sender listing alone does not prove correct frame delivery.

There are no existing automated tests to run. Add focused tests when introducing independently testable policy/state logic or fixing a regression that can be reproduced automatically. Hardware interop still needs runtime evidence. If the engine, host project, GPU access, or peer Spout app is unavailable, state exactly what was inspected and what remains unverified.

## Existing caveats to investigate when relevant

These are source-inspection observations, not runtime-verified defects or authorization for unrelated fixes:

- Receiver one-shot completion calls `StopReceiving` from `PublishCompletedInternalBuffer`, which returns immediately when double buffering is disabled. Test the documented one-shot behavior in both modes before relying on it.
- Receiver render-target initialization checks dimensions in several places; a same-size pixel-format/gamma change can require more than the current size checks. Cached wrapper validity also needs attention when native resources are recreated.
- Receiver fence signaling and double-buffer reuse/publication need their own ordering review. They do not duplicate the sender's explicit flush and slot-readiness guards.
- Receiver `RefreshEditorState` returns for an unsupported world before stopping an existing session. Test transitions to more restrictive startup policies.
- `MissedFrames` is assigned from a counter reset each stats window; `ReconnectCount` increments on a start transition. Do not present them as proven lifetime missed-frame totals or counts of every external sender reconnection.
- The alternate external SDK module's `.tps` file contains sample placeholder text. It does not establish the shipped SDK version or replace vendor notices.

Record reproduction evidence and proposed scope in `local plans.md`. Do not silently fix these while carrying out a documentation or unrelated feature task.

## Local planning, Git hygiene, and handoff

- The exact root filename is `local plans.md`; `.gitignore` should contain `/local plans.md`. Quote this path in shell commands. Never force-add it or copy private notes into release artifacts or shared descriptions without checking their relevance.
- Since the plan is ignored, fresh clones will not contain it. Recreate a concise notebook with objective, scope, steps, decisions, validation, and handoff sections when needed. Do not make normal builds depend on it.
- Update the plan as substantial work progresses. Keep ideas separate from approved active work; record limitations honestly. Never store credentials or secrets there.
- DLLs, LIBs, and `Binaries/Win64` are intentionally tracked in the current repository. Do not add blanket ignores, delete tracked binaries, or commit incidental build output without a task-specific reason. Inspect binary changes after a build.
- Before delivery, run `git diff --check`, review `git diff --stat` and `git status --short`, and verify only intended files changed. For the notebook, run `git check-ignore -v -- 'local plans.md'`; it should not appear in normal status or `git ls-files`.
- Update README for user-visible behavior changes and this guide for architecture/workflow changes. Change release metadata only as part of a requested release/version change.
- Report what changed, why, the exact validation performed, and remaining limitations. Do not claim runtime testing from source inspection, existing binaries, or old screenshots. Do not commit, push, or publish unless requested.
