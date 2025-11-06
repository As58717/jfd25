# jfd25

## Runtime requirements

The OmniCapture NVENC backend now loads `nvEncodeAPI64.dll` directly from the
system. Make sure the NVIDIA driver or Video Codec SDK runtime that ships this
DLL is installed on any machine running the plugin; otherwise the encoder will
fail to initialise at runtime.
