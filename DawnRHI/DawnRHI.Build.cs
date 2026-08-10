// DawnRHI — an independent, open-source WebGPU RHI backend for UE5.8, built on
// Dawn (native) via Dawn's public webgpu.h / dawn/native API only.
//
// This module is NOT part of, and does not link against, SimplyStream's
// closed WebGPURHI or WebGPUShaderFormat modules. It only reuses the
// vendored, open-source Dawn/Tint static libraries that happen to be checked
// in under the SimplyStream fork's ThirdParty folder (same libdawn.a/
// libtint.a anyone using Dawn would ship), and Epic's own open RHI headers.
using System.IO;
using UnrealBuildTool;

[SupportedPlatformGroups("Linux")]
public class DawnRHI : ModuleRules
{
	public DawnRHI(ReadOnlyTargetRules Target) : base(Target)
	{
		IWYUSupport = IWYUSupport.None;
		bLegalToDistributeObjectCode = true;

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"RHI",
				"RHICore",
				"TraceLog",
			}
		);

		// Vendored Dawn/Tint (open source, Apache-2.0/BSD) — reused in place
		// from the SimplyStream fork's ThirdParty checkout per project
		// instructions. Only the public Dawn/Tint headers + static libs are
		// referenced; nothing under Source/Runtime/WebGPURHI or
		// Source/Developer/WebGPUShaderFormat (SimplyStream's closed code)
		// is touched.
		string DawnDir = Path.Combine(EngineDirectory, "Platforms", "SimplyStream", "Source", "ThirdParty", "Dawn");
		string DawnIncludeDir = Path.Combine(DawnDir, "include");

		if (!Directory.Exists(DawnIncludeDir))
		{
			throw new BuildException("DawnRHI: expected vendored Dawn headers at " + DawnIncludeDir);
		}

		PublicIncludePaths.Add(DawnIncludeDir);

		if (Target.IsInPlatformGroup(UnrealPlatformGroup.Linux))
		{
			string LibDir = Path.Combine(DawnDir, "lib", "linux");
			PublicAdditionalLibraries.Add(Path.Combine(LibDir, "libdawn.a"));
			PublicAdditionalLibraries.Add(Path.Combine(LibDir, "libtint.a"));
			PublicAdditionalLibraries.Add(Path.Combine(LibDir, "libSPIRV-Tools.a"));

			PublicSystemLibraries.Add("dl");
			PublicSystemLibraries.Add("pthread");

			// libdawn.a's Vulkan backend has direct undefined references to
			// the Vulkan loader (libvulkan.so) at link time. This box's
			// system libvulkan.so lives under /nix/store (NixOS), not on
			// the default linker search path, so a plain "-lvulkan" fails.
			// Stage 1 pragmatism: resolve the nix store path at build time
			// instead of hardcoding a specific store hash. Known fragility
			// to revisit later (e.g. dlopen libvulkan.so.1 at runtime
			// instead of link-time linking).
			string VulkanLibDir = null;
			if (Directory.Exists("/nix/store"))
			{
				foreach (string Dir in Directory.GetDirectories("/nix/store", "*-vulkan-loader-*"))
				{
					string CandidateSo = Path.Combine(Dir, "lib", "libvulkan.so");
					if (!File.Exists(CandidateSo))
					{
						continue;
					}
					// /nix/store can contain builds for multiple architectures
					// (e.g. an ELF32 x86 variant alongside the native ELF64
					// x86-64 one) — a plain glob pick can grab the wrong one
					// and fail at link time with "incompatible with
					// elf64-x86-64". Check the ELF header (byte 4 = EI_CLASS,
					// 2 = ELFCLASS64; bytes 18-19 = e_machine, 0x3E = EM_X86_64)
					// rather than trust the directory name.
					byte[] Header = new byte[20];
					using (FileStream Fs = File.OpenRead(CandidateSo))
					{
						Fs.Read(Header, 0, Header.Length);
					}
					bool bIsElf64 = Header[4] == 2;
					bool bIsX86_64 = Header[18] == 0x3E && Header[19] == 0x00;
					if (bIsElf64 && bIsX86_64)
					{
						VulkanLibDir = Path.Combine(Dir, "lib");
						break;
					}
				}
			}
			if (VulkanLibDir != null)
			{
				PublicSystemLibraryPaths.Add(VulkanLibDir);
				PublicRuntimeLibraryPaths.Add(VulkanLibDir);
				PublicSystemLibraries.Add("vulkan");
			}
			else
			{
				throw new BuildException("DawnRHI: could not locate an ELF64/x86-64 Vulkan loader under /nix/store — adjust DawnRHI.Build.cs for non-NixOS hosts.");
			}
		}
		else
		{
			PrecompileForTargets = PrecompileTargetsType.None;
		}
	}
}
