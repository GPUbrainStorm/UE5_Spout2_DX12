# UE5_Spout2_DX12

Spout2 plugin for **Unreal Engine 5** using **DirectX 12**.

This plugin enables Spout sending support from Unreal Engine via DX12.  
Tested successfully in both live projects and packaged builds.  
> **Note:** This is a beta release — proceed with caution.

**Test Environment:**  
Unreal Engine 5.2.1 on Windows with DirectX 12.

---

## Installation Instructions

1. **Download**  
   Extract the zip into your project's `Plugins` folder:  
   `YourProjectFolder/Plugins`

2. **Copy DLL**  
   Copy `SpoutDX12.dll` from:  
   `Spout2_DX12/Binaries/ThirdParty/Spout2_DX12Library/Win64`  
   to:  
   `UE_5.2/Engine/Binaries/Win64`

3. **Launch**  
   Open your project in Unreal Engine 5.2.1.

---

## Usage

Currently, the plugin only **supports sending**.  
Receiving will be added in a future update.

### Sending a Spout Source

By default, a **global sender** is available. You can use:

- **Start Spout Broadcasting**  
  Pass a **Name**, **Render Target**, and **FPS** (set `FPS = 0` for a one-time static texture).

- **Stop Spout Broadcasting**  
  Stops updating the texture.

Example:
![Global Sender Example](https://github.com/user-attachments/assets/81508aae-386d-43a1-b79c-cd229edb7fc3)

---

### SpoutSender Component

For multiple senders:

- Add a **SpoutSender** component to any Actor.
- Set the **Name**, **Render Target**, and **FPS** in the **Details Panel**.
- Broadcasting starts automatically on `BeginPlay`.

You can also control broadcasting manually using Blueprint calls:
- `Start Broadcasting`
- `Stop Broadcasting`

> Tip: Use `FPS = 0` for static, one-time texture send.

Examples:
![SpoutSender Setup](https://github.com/user-attachments/assets/d18743bb-dab0-4911-a078-d93a9754379b)
![Details Panel](https://github.com/user-attachments/assets/e8f0c3ef-590b-46ab-8c72-0596b09f7906)
![Blueprint Example](https://github.com/user-attachments/assets/86f2fce1-ffb2-47d6-94f5-4f6561b53ad3)
