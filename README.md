<img width="268" height="269" alt="logo" src="https://github.com/user-attachments/assets/8afeb86f-cbe7-400b-8002-252ed8121854" />

# UE5_Spout2_DX12 v2.0.1

Spout2 plugin for Unreal Engine 5 using DirectX 12.

This plugin adds Spout sender and receiver components for Unreal Engine. It works in the editor, in PIE, and in packaged builds based on the startup policy you choose.

Discord Server:
**https://discord.gg/BxHRHDdkNy**

> Beta release.

Tested on Windows with DX12:
- UE 5.2.1
- UE 5.4.4
- UE 5.6.1
- UE 5.7.2

[**You can donate to support the project here.**](https://www.paypal.com/donate/?hosted_button_id=G3JA94AZGB9T2)

---

## What It Includes

- Spout sender component
- Spout receiver component
- Render Target sender mode
- Game Viewport sender mode
- Editor Viewport sender mode
- Editor, game, and preview startup policies
- Optional double buffering for sender and receiver
- Manual Blueprint and C++ runtime control functions
- Property tooltips in the editor for sender and receiver settings

---

## Requirements

- Windows only
- DirectX 12 only

---

## Installation

1. Download the correct build from [Releases](https://github.com/GPUbrainStorm/UE5_Spout2_DX12/releases)
2. Extract it into:

```text
YourProject/Plugins/Spout2_DX12/
```

3. Open the project
4. Enable the plugin if needed

The required DLLs are handled by the plugin. No manual engine copy step is needed.

### Packaged Builds

Package the project normally. The plugin stages the required DLLs next to the packaged executable automatically.

---

## Sender Component

`SpoutSenderComponent` sends frames from Unreal to any Spout receiver.

### Sender Source Modes

- `RenderTarget`
  Sends the render target assigned to `CurrentRenderTarget`.
- `GameViewport`
  Sends the active game viewport.
- `EditorViewport`
  Sends the active editor viewport.

### Sender Properties

- `Auto_Start`
  Starts the sender automatically when the current world matches the selected startup policy.
- `CurrentSenderName`
  The published Spout sender name.
- `SourceType`
  Selects `RenderTarget`, `GameViewport`, or `EditorViewport`.
- `CurrentRenderTarget`
  Used only when `SourceType` is `RenderTarget`.
- `BroadcastFPS`
  Controls how often frames are sent.
- `bUseDoubleBuffer`
  Enables double buffering for smoother frame pacing with extra memory use.
- `StartupPolicy`
  Controls where the sender is allowed to start automatically.
- `TickAfterActor`
  Optional tick dependency if another actor must update first.

### Sender Startup Policies

- `Editor | Game`
  Allows start in editor and game worlds.
- `Game Only`
  Allows start only in game worlds.
- `Editor | Game | Single Preview`
  Allows start in editor, game, and a single preview world.

### Sender Blueprint / C++ Functions

- `StartBroadcast()`
  Starts using the current sender component settings exactly as configured in the editor.
- `StartBroadcastFromRenderTarget(RenderTarget, SenderName, FPS, bEnableDoubleBuffer)`
  Starts from a render target and forces runtime startup behavior to `Game Only`.
- `StartBroadcastGameViewport(SenderName, FPS, bEnableDoubleBuffer)`
  Starts from the game viewport and forces runtime startup behavior to `Game Only`.
- `StopBroadcast()`
  Stops the sender.
- `ChangeRenderTarget(NewRenderTarget)`
  Changes the render target source.
- `SetTickAfterActor(NewTickAfterActor)`
  Sets the optional tick dependency actor.

### Sender Notes

- `StartBroadcast()` uses the same configured settings path that auto-start uses.
- The runtime override functions are intended for manual runtime control.

### Sender Screenshot

---

## Receiver Component

`SpoutReceiverComponent` receives frames from any Spout sender and writes them into a render target.

### Receiver Properties

- `bAutoStart`
  Starts the receiver automatically when the current world matches the selected startup policy.
- `OutputRenderTarget`
  The render target that receives the incoming frame.
- `TargetFPS`
  Controls how often the receiver tries to pull frames. Set to `0` for one frame then stop.
- `SpoutSenderName`
  The specific sender name to connect to. Leave empty to use the first available sender.
- `bUseDoubleBuffer`
  Uses internal double buffering to keep the output render target stable while new frames are copied in.
- `StartupPolicy`
  Controls where the receiver is allowed to start automatically.

### Receiver Blueprint / C++ Functions

- `StartReceiving()`
  Starts the receiver.
- `StopReceiving()`
  Stops the receiver.
- `GetAvailableSenders()`
  Returns available Spout sender names.
- `IsConnected()`
  Returns the current connection state.

### Receiver Stats

- `CopiesPerSecond`
  Average copy operations per second.
- `Flush1PerSecond`
  Average first-stage flush operations per second.
- `FlushPerSecond`
  Average flush operations per second.
- `ReconnectCount`
  Number of reconnects during the current session.
- `MissedFrames`
  Number of missed frames during the current session.

### Receiver Screenshots


---

## Quick Start

### Send From Render Target

1. Add a `SpoutSenderComponent` to an actor
2. Set `SourceType` to `RenderTarget`
3. Assign `CurrentRenderTarget`
4. Set `CurrentSenderName`
5. Enable `Auto_Start` or call `StartBroadcast()`

### Send From Game Viewport

1. Add a `SpoutSenderComponent`
2. Set `SourceType` to `GameViewport`
3. Set `CurrentSenderName`
4. Enable `Auto_Start` or call `StartBroadcast()`

### Send From Editor Viewport

1. Add a `SpoutSenderComponent`
2. Set `SourceType` to `EditorViewport`
3. Set `CurrentSenderName`
4. Choose `Editor | Game` or `Editor | Game | Single Preview`
5. Enable `Auto_Start` or call `StartBroadcast()`

### Receive Into a Render Target

1. Add a `SpoutReceiverComponent`
2. Assign `OutputRenderTarget`
3. Set `SpoutSenderName` if needed
4. Enable `bAutoStart` or call `StartReceiving()`

---

## Notes

- Sender and receiver are DX12 only.
- The plugin handles Windows header issues needed for packaging.
- UE 5.6 deprecated the old `FTexture2DRHIRef` type and UE 5.7 removed it. The plugin now uses the unified texture type for those versions.
- Sender editor ownership and restart behavior were updated to better handle editor, game, and preview transitions.

---

## Version

Current release: `v2.0.1`

---

## Support

If you hit an issue, open a GitHub issue and include:

- Unreal Engine version
- whether the issue happens in editor, PIE, or packaged build
- whether you are using sender or receiver
- logs or screenshots if possible

Or join the Discord server mentioned in the beginning and ask there.