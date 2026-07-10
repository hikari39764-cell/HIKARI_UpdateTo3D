# NVIDIA Streamline SDK

HIKARI uses NVIDIA Streamline as an optional runtime backend for DLAA and
DLSS Super Resolution. The SDK is intentionally not committed because its
signed runtime package is large and must remain an explicit local dependency.

Expected local layout:

```text
ThirdParty/Streamline/sdk/
  include/
  lib/x64/sl.interposer.lib
  bin/x64/sl.interposer.dll
  bin/x64/sl.common.dll
  bin/x64/sl.dlss.dll
  bin/x64/nvngx_dlss.dll
  bin/x64/nvngx_dlss.license.txt
```

The validated package is `streamline-sdk-v2.12.0.zip` from the official
NVIDIA-RTX/Streamline GitHub release. When this layout is absent, HIKARI builds
without Streamline and reports DLSS/DLAA as unavailable instead of silently
enabling a placeholder path.
