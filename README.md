# jfd25

## NVENC Runtime Deployment

The OmniCapture plugin links against NVIDIA's NVENC runtime but does not ship the DLL. Install an up-to-date NVIDIA driver and copy `nvEncodeAPI64.dll` from the driver package (for example `C:\Windows\System32\nvEncodeAPI64.dll`) into your project's `Binaries/Win64` directory before running. Alternatively, point the OmniCapture settings to the folder that contains the DLL via the provided override fields.
