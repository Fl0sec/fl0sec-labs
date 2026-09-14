# Kernel anti-cheat CTF

Trainer source and an offline CRC32 patch verifier for
[TBMKE v1](https://crackmes.one/crackme/69db34d6b38f9259eec7eb32).

Read the [write-up](https://www.fl0sec.com/research/kernel-anti-cheat-ctf).

## Build

From an x64 Native Tools Command Prompt for Visual Studio 2022:

```powershell
.\build.ps1
```

The trainer is written to `build/TBMTrainer.exe`. Run it from the directory
containing `TBM.exe`, or pass the game path as its first argument.

## Offline verifier

Obtain `TBM.exe` from the challenge. The expected SHA-256 is
`de2a5b567b6f53ed6625c428e0ebe3fbec0797f5e6b3a3d1bcee5bb3fc403a42`.

```powershell
python .\tools\verify_crc32_preserving_health_patch.py C:\path\to\TBM.exe
```

Use a disposable VM. The original game, watchdog, and driver are not included.
