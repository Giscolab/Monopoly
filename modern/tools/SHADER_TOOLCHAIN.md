# Offline shader compilation

Python 3.10+, CMake and the host C++ toolchain are required. The lock file fixes
the official DXC release archive hashes and the SPIRV-Cross source revision/hash.
Bootstrap never installs globally; its default cache is `modern/build-shader-tools`
(already ignored by the repository).
Use `--cmake /absolute/path/cmake` if CMake is not available on PATH.

```sh
python modern/tools/compile_world3d_shaders.py --bootstrap-only
python modern/tools/compile_world3d_shaders.py --output modern/build/shaders
python modern/tools/compile_world3d_shaders.py --name ModernPBR --output modern/build/shaders
```

Bootstrap supports Windows x64/arm64 and Linux x64, matching Microsoft's binary
release availability. On macOS or other hosts, build DXC and SPIRV-Cross from the
locked versions and pass `--dxc /absolute/path/dxc --spirv-cross /absolute/path/spirv-cross`.
External compiler provenance is the caller's responsibility; output manifests
record that override and the executable hashes. Linux DXC runtime dependencies
must be supplied by the host, as documented in its release.

Each invocation produces DXIL, SPIR-V and MSL for both stages. MSL uses `main0`;
DXIL and SPIR-V use `main`. Reflection verifies the SDL uniform register spaces
and the combined sampler contract, and MSL binding checks verify buffer/texture
indices. World3D needs `[[vk::combinedImageSampler]]` on its texture and sampler
declarations for Vulkan. ModernPBR requires six combined image samplers at set 2:
2D material maps at bindings 0 through 4 and the linear HDR specular environment
cube at binding 5. Use `--samplers 5` for the earlier material-map checkpoint or
`--samplers 0` for the earlier factor-only checkpoint, with those checkpoint's
HLSL sources. Vertex stages require zero sampled textures.
Manifests record exact source-byte hashes plus CRLF-normalized LF hashes, allowing
source-content comparisons across Windows and Unix checkouts without losing the
provenance of the actual compiler inputs. These checks do
not replace Vulkan/Metal/D3D12 runtime validation.

The 256-byte ModernPBR fragment uniform layout remains unchanged. Its
`sceneAmbient.w` explicitly enables the specular environment (zero by default).
The enabled path samples `reflect(-view, normal)` at
`roughness * (cubeMipCount - 1)`; each cube mip must be GGX-prefiltered at
perceptual roughness `mip / (cubeMipCount - 1)`, with GGX alpha = roughness squared.
Cube values are linear HDR radiance, not sRGB image values. Karis's analytic
environment BRDF approximation replaces the constant specular ambient term;
the current diffuse ambient approximation remains. This is specular environment
lighting with an analytic DFG fit, not diffuse irradiance integration or a
precomputed BRDF LUT. The disabled path retains the previous ambient expression.

For CMake integration, make an `add_custom_command` produce all six shader files
and the matching `<name>.manifest.json` in the binary shader directory. Invoke
this script with `--name`, `--output`, and `--cache` pointing to the prebootstrapped
cache. Depend on both HLSL sources, this script and its JSON lock file. A custom
target can depend on those outputs and feed the existing runtime staging target.
Keep bootstrap a separate explicit dependency preparation step; normal builds
then run offline. Do not stage checked-in stale binaries over freshly compiled
files. To refresh checked-in assets deliberately, pass the generated source
directory explicitly as `--output` and review every artifact.

Primary references:

- [Microsoft DXC release](https://github.com/microsoft/DirectXShaderCompiler/releases/tag/v1.9.2609)
- [DXC Vulkan binding and combined sampler documentation](https://github.com/microsoft/DirectXShaderCompiler/blob/v1.9.2609/docs/SPIR-V.rst)
- [SPIRV-Cross CLI and build documentation](https://github.com/KhronosGroup/SPIRV-Cross/tree/aa217aeb6c9f0ace7a0ab233b28807edf45eb165)
- [Karis: Real Shading in Unreal Engine 4 (2013), GGX split-sum prefilter](https://cdn2.unrealengine.com/Resources/files/2013SiggraphPresentationsNotes-26915738.pdf)
- [Karis: Physically Based Shading on Mobile (2014), analytic environment BRDF](https://www.unrealengine.com/blog/physically-based-shading-on-mobile?lang=en-US)
- [Microsoft HLSL GetDimensions, including cube mip-level count](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-to-getdimensions)
