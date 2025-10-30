# UE5_Spout2_DX12

Spout2 plugin for **Unreal Engine 5** using **DirectX 12**.

This plugin enables **Spout sending and receiving** directly from and to Unreal Engine via DX12.
Works in both editor and packaged builds.

> **Note:** Beta release.

**Test Environment:**
Unreal Engine 5.2.1 on Windows with DirectX 12.

---

## Installation Instructions

1. **Download**
   Download the correct version from [Releases](https://github.com/GPUbrainStorm/UE5_Spout2_DX12/releases) then extract the zip into your project's `Plugins` folder:
   `YourProjectFolder/Plugins`

2. **Copy DLL**
   Copy `SpoutDX12.dll` from:
   `Spout2_DX12/Binaries/ThirdParty/Spout2_DX12Library/Win64`
   to:
   `UE_5.2/Engine/Binaries/Win64`

3. **Launch**
   Open your project in Unreal Engine 5.2.1.

**For packaged projects:**
Copy `SpoutDX12.dll` from:
`Spout2_DX12/Binaries/ThirdParty/Spout2_DX12Library/Win64`
to your packaged project's binaries folder:
`Packaged_Project\Windows\Packaged_Project\Binaries\Win64`

---

## Usage

### SpoutSender Component

All Spout functionality is now handled by the **SpoutSender Component**.

**To use:**

* Add a **SpoutSender** component to any Actor.
* Assign a **Render Target**, **Sender Name**, and **FPS**.
* Enable **Auto_Start** to begin broadcasting on `BeginPlay`.

**Blueprint Control:**

* `StartBroadcast(RenderTarget, Name, FPS)`
* `StopBroadcast()`

> Tip: Set `FPS = 0` for a one-time static texture send.

Examples:
![SpoutSender Setup](https://github.com/user-attachments/assets/d18743bb-dab0-4911-a078-d93a9754379b)
![Details Panel](https://github.com/user-attachments/assets/e8f0c3ef-590b-46ab-8c72-0596b09f7906)
![Blueprint Example](https://github.com/user-attachments/assets/86f2fce1-ffb2-47d6-94f5-4f6561b53ad3)

---

### SpoutReceiver Component (NEW)

Introducing the new receiver component to read from senders within Unreal Engine over DX12.

## Blueprint Callable
* `StartReceiving()`
* `StopReceiving()`
* `GetAvailableSenders() -> TArray<FString>`
* `IsConnected() -> bool`

## Key Properties
* `bAutoStart` — start on BeginPlay.
* `OutputRenderTarget` — destination RT
* `TargetFPS` — receive cadence; `0` = one frame then stop.
* `SpoutSenderName` — optional explicit sender.

All the properties can be set during the runtime after calling `StopReceiving()` and then calling `StartReceiving()` again.

**Screenshots:**
<img width="1571" height="716" alt="image" src="https://github.com/user-attachments/assets/120a29e2-ccdb-47cb-ad1f-ef94c5f379a7" />
<img width="892" height="565" alt="image" src="https://github.com/user-attachments/assets/83205f90-7f36-42f8-972f-9b1cebe6e875" />

---
## Version 1.0.0 Release Notes

**Summary:**
Introducing the new receiver component to read from senders within Unreal Engine over DX12.

## Blueprint Callable
* `StartReceiving()`
* `StopReceiving()`
* `GetAvailableSenders() -> TArray<FString>`
* `IsConnected() -> bool`

## Key Properties
* `bAutoStart` — start on BeginPlay.
* `OutputRenderTarget` — destination RT
* `TargetFPS` — receive cadence; `0` = one frame then stop.
* `SpoutSenderName` — optional explicit sender.

All the properties can be set during the runtime after calling `StopReceiving()` and then calling `StartReceiving()` again.
