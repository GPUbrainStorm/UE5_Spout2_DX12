Spout2 plugin for **Unreal Engine 5** using **DirectX 12**.
You can join the discord server for futher support from the following link:
**https://discord.gg/BxHRHDdkNy**


This plugin enables **Spout sending and receiving** directly in Unreal (Editor & packaged builds).

> **Note:** Beta release.

**Tested:** UE **5.2.1** , **5.4.4** & **5.7.0** on Windows (DX12).

---

## Installation

1. **Download**
   Get the right build from [Releases](https://github.com/GPUbrainStorm/UE5_Spout2_DX12/releases) and extract to:

```
YourProject/Plugins/Spout2_DX12/
```

2. **Done**
   Third-party DLLs and headers are handled automatically. No manual copying to the Engine folder is required.

### Packaged builds

DLLs are staged automatically next to the packaged EXE. Just package as usual.

---

## Usage

### SpoutSender Component

**Add** a `SpoutSender` component to any Actor:

* Set **Render Target**, **Sender Name**, **FPS**.
* Enable **Auto_Start** to begin on `BeginPlay`.

**Blueprints**

* `StartBroadcast(RenderTarget, Name, FPS)`
* `StopBroadcast()`

> Tip: Use `FPS = 0` to push a single frame.

**Examples**
<img width="1916" height="660" alt="image" src="https://github.com/user-attachments/assets/34a65bd8-56ad-4617-b94a-25234c41555e" />
<img width="990" height="471" alt="image" src="https://github.com/user-attachments/assets/42fca5a6-5341-4664-9da9-17ee1c7f5808" />


---

### SpoutReceiver Component

Receive from any Spout2 sender (OBS Spout plugin, other apps, or another UE instance).

**Blueprints**

* `StartReceiving()`
* `StopReceiving()`
* `GetAvailableSenders() -> TArray<FString>`
* `IsConnected() -> bool`

**Properties**

* `bAutoStart` — start on `BeginPlay`
* `OutputRenderTarget` — destination RT
* `TargetFPS` — receive cadence (`0` = one frame)
* `SpoutSenderName` — optional explicit sender

> You can change properties at runtime; call `StopReceiving()` → update settings → `StartReceiving()`.

**Screenshots** <img width="1571" height="716" alt="image" src="https://github.com/user-attachments/assets/120a29e2-ccdb-47cb-ad1f-ef94c5f379a7" /> <img width="892" height="565" alt="image" src="https://github.com/user-attachments/assets/83205f90-7f36-42f8-972f-9b1cebe6e875" />

---

## Notes / Requirements

* **DX12 only.** (Internally uses D3D12 + D3D11on12.)

---

## Compatibility

* **Prebuilt (binary) releases:** UE **5.2.1**, **5.4.4** and **5.7.0**.
* **Source:** Should compile for other UE5 minors.
  If you struggle on a specific version, **open an issue** with the UE version and I’ll try to provide a packaged build ASAP.

---

## Release Notes

### v1.0.2 - 20 Nov 2025

* **Smoother sending:** Replaced per-frame `FlushRenderingCommands()` with a small async GPU task + wait. This avoids flushing the whole render pipeline every frame.
* **Fixed crash risk:** Removed `CurrWrappedResource->Release()` (which was never set) and now only manage `StagingRHI` and `StagingWrapped11`.
* **Fixed resource leaks:** `EndPlay` now deletes `SpoutBridge` and clears the staging texture, avoiding leaks when levels or games close.

### v1.0.1 — 30 Oct 2025

* **Drop-in install:** Third-party **DLLs & headers are handled automatically**. Just copy the plugin into your project’s `Plugins` folder. Works in Editor **and** packaged builds.
* **Delay-load DLL:** `Spout.dll` / `SpoutDX12.dll` are delay-loaded; no more “module could not be loaded” when DLLs aren’t beside the editor or in the Binaries folder.
* **Auto DLL discovery:** At runtime the plugin looks in **Plugin/Binaries**, **Project/Binaries**, then **EXE directory** (packaged) — no manual copying to the Engine folder.

### v1.0.0

* Initial **SpoutReceiver** component.
* Sender improvements and Blueprint calls.

---

**Issues / Requests:** Please open a GitHub issue with logs and your UE version.
