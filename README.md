# jfd25

## NVENC Runtime Deployment

The OmniCapture plugin now bundles NVIDIA's NVENC runtime under `Plugins/OmniCapture/ThirdParty/NVENC/Win64/nvEncodeAPI64.dll`. The build script automatically stages the DLL with the plugin so the encoder can be used out of the box. If you need to override the runtime with a different version, you can still point the OmniCapture settings to a custom folder or DLL path via the provided override fields.
