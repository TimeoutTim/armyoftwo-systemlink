# Installing and playing

## Apply the release patch (recommended)

Download `armyoftwo-systemlink-<version>-patch.zip` from the repository's Releases page and extract it. Use the patch asset, not the source-code ZIP. The matching SHA256SUMS file lets you verify the downloaded archive.

The patch is standard **xdelta3/VCDIFF**, not a custom format. Choose either method below. Neither requires a compiler, Xbox SDK, Rust, Wine, or emulator.

### Windows: Delta Patcher

Use the free, self-contained [Delta Patcher Windows release](https://github.com/marco-calautti/DeltaPatcher/releases). It does not require Python or a separate xdelta3 installation.

1. Make a copy of your untouched `default.xex` in a separate working folder. Keep the original safe: GUI patchers can replace the selected file.
2. Open Delta Patcher in patch-application mode.
3. Select the copied executable as **Original file** and `army-of-two.xdelta` as **XDelta patch**.
4. Apply the patch with checksum validation enabled. Keep the backup option enabled if offered.
5. Locate the resulting patched executable and name it `default-lan.xex`. Check its SHA-256 against `target.sha256` in the release's `manifest.json` before installing.

PowerShell can compute the hashes without additional software:

```powershell
Get-FileHash "C:\path\to\original\default.xex" -Algorithm SHA256
Get-FileHash ".\default-lan.xex" -Algorithm SHA256
```

### Linux: xdelta3

Install `xdelta3` through your distribution's package manager. From the extracted patch directory:

```sh
sha256sum /path/to/original/default.xex
xdelta3 -d -s /path/to/original/default.xex army-of-two.xdelta default-lan.xex
sha256sum default-lan.xex
```

Compare the original hash with `source.sha256` and the output hash with `target.sha256` in `manifest.json`. Quote paths containing spaces. Use a new output filename; do not overwrite your original. This command has been tested against the release patch and reconstructs the exact deployed executable.

### Optional: automatic SHA-256 verification

The included helper requires Python 3 and `xdelta3` on PATH:

```sh
python3 apply_xex_delta.py /path/to/original/default.xex \
  army-of-two.xdelta manifest.json default-lan.xex
```

Use `python` on Windows and ensure `xdelta3.exe` is on PATH. The helper checks source, patch, and output hashes and refuses to replace an existing output file. It is optional; Delta Patcher and the Linux command above use the same patch directly.

The original executable SHA-256 must be:

```text
246556f56d4062e05d708359f3e7ab0840b4035f426fa380f5bf152436c1d771
```

Install the resulting executable using the steps below. Developers can also produce it using the [source-build instructions](BUILDING.md).

## Xbox 360

Use a console capable of running modified XEX files (the project has been tested on modified/debug-capable hardware). An unmodified retail console will not run this build. This is a patched executable, not a custom title update or DashLaunch plugin.

1. Keep an untouched backup of your game and saves. Create a separate extracted game directory on the console HDD, for example `Hdd1/Games/ArmyOfTwo-SystemLink/`.
2. Copy the full original game content there using your existing FTP or USB workflow. Preserve the original directory structure and content filenames.
3. Copy the patched `default-lan.xex` into that directory, beside the original `default.xex`. Do not install intermediate `.pe` files or `packaging-baseline.xex`.
4. Launch **`default-lan.xex`** explicitly from your dashboard/file manager. If your dashboard only launches `default.xex`, rename the original to `default-original.xex` in this separate copy and name the patched file `default.xex`.
5. Use the same patch revision and original content on both consoles. Disable title updates for this separate game copy; other executable revisions are unsupported.
6. Sign in to a local profile on each console and complete normal storage-device prompts. Connect both consoles to the same LAN/broadcast domain.

In **Campaign**, select **System Link: Host** on one console. Complete the mission/checkpoint selection and wait in its lobby. On the other console choose **System Link: Join**, select the host's profile name, and join. The host selects **Start Game** after both players appear. Complete any storage prompts.

The browser polls while open, so opening Join before the host is supported. Discovery uses UDP 6002; peer/session traffic uses ports 6000/6001. Client isolation or blocked local broadcast traffic can prevent discovery. Xbox Live login and an external matchmaking server are not required.

To leave a pre-game host lobby, press B and confirm. The peer should receive the game's native disconnect message. After dismissing it, another host/join cycle should be possible; report failures with the patch revision and exact sequence.

To roll back, launch the untouched original executable or remove the separate patched game directory. The patch does not require modified game asset packages.

## Xenia

Open the rebuilt `default-lan.xex` from the complete extracted game directory in Xenia Canary. Keep the bundled TOML entry disabled when running an already patched executable: the patch is already baked into it.

The included TOML is a commented, Xenia-format description of the memory edits, but it cannot on its own promote the original XEX payload page to executable. Do not expect simply enabling it on the original game to work. Hardware has been the primary two-player test target; local Xenia menu tests do not establish emulator-to-emulator or mixed emulator/console LAN compatibility.

## When reporting a problem

Include the repository commit, whether you rebuilt the C or used the checked-in patch, the original executable hash, platform, and the sequence of host/join/start/leave actions. State whether the game reached a lobby or gameplay. Do not attach game executables, SDK files, save data, or memory dumps to public issues.
