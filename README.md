# SM-ModTempDataSupport

A DLL mod for **Scrap Mechanic** that enables mods to read and write JSON files in the game's `$TEMP_DATA` directory, a sandboxed scratch space that is automatically wiped when the game exits.

> [!NOTE]
> You can detect whether `SM-ModTempDataSupport` is installed by checking `sm.tempDataMod_installed` (May be changed to `sm.modTempDataSupport_installed` in a later version) is set to true.

## Features

- Exposes `$TEMP_DATA` write access to other mods via JSON (without dll mod its read-only)
- Zero persistent side effects, all written files are cleaned up on game shutdown

## Supported Versions

| Game Version | Build |
| ------------ | ----- |
| 0.7.4        | 778   |


## Usage

Once injected, other mods can use the `sm.json` api to write and read JSON files within `$TEMP_DATA`. These files persist only for the current session and are deleted automatically when the game closes.

> [!NOTE]
> `$TEMP_DATA` is a game-managed directory. Do not rely on files written there surviving a restart.

## Building from Source

Prerequisites:
- Visual Studio 2026
- Windows SDK

Steps:
1. Clone the repository.
2. Open the `.slnx` file in Visual Studio.
3. Build it.
4. The compiled `SM-ModTempDataSupport.dll` will appear in the output directory.

## Contributing

Pull requests are welcome. For major changes, please open an issue first to discuss what you'd like to change.

## License

See [LICENSE](LICENSE) for details.