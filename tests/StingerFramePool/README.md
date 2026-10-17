# Preloaded stinger frame storage checks

This small native test exercises the production uniform-alpha helper with
synthetic frames. It verifies exact YUV/alpha pixels and metadata, padded and
odd-sized planes, fallback for changing alpha, ordinary playback copies and
allocation ownership after transferring the pool to another owner. It does
not open Pulse Weaver, load user media or change a scene collection.

Configure with the Visual Studio CMake executable and an existing OBS build:

```powershell
cmake -S tests/StingerFramePool -B tests/StingerFramePool/build -A x64 -DOBS_BUILD="<existing OBS build>"
cmake --build tests/StingerFramePool/build --config RelWithDebInfo
# Add the matching OBS runtime bin/64bit directory to PATH, then run:
./tests/StingerFramePool/build/RelWithDebInfo/PulseStingerFramePoolTests.exe
```

The optimisation runs only while preloading. It keeps all video frames decoded
and shares constant alpha bytes without introducing playback scanning,
decompression, frame dropping or changes to audio. Savings depend on the
clip's actual alpha data. Use the separate SharedStingerCache native checks
with a real alpha clip to validate the shared-cache lifetime and measure it.
