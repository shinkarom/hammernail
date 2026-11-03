const std = @import("std");

pub fn build(b: *std.Build) void {
    const optimize = b.standardOptimizeOption(.{
        .preferred_optimize_mode = .ReleaseFast,
    });

    // Configure exact CPU features supported by mini-rv64.h:
    // RV64IM + Zba + Zbb + Zbc + Zbs + Zbkb + Zicond + Zicsr (NO 'C' compressed!)
    var enabled_features = std.Target.Cpu.Feature.Set.empty;
    var disabled_features = std.Target.Cpu.Feature.Set.empty;

    const Feature = std.Target.riscv.Feature;
    enabled_features.addFeature(@intFromEnum(Feature.@"64bit"));
    enabled_features.addFeature(@intFromEnum(Feature.i));
    enabled_features.addFeature(@intFromEnum(Feature.m));
    enabled_features.addFeature(@intFromEnum(Feature.zicsr));
    enabled_features.addFeature(@intFromEnum(Feature.zba));
    enabled_features.addFeature(@intFromEnum(Feature.zbb));
    enabled_features.addFeature(@intFromEnum(Feature.zbc));
    enabled_features.addFeature(@intFromEnum(Feature.zbs));
    enabled_features.addFeature(@intFromEnum(Feature.zbkb));
    enabled_features.addFeature(@intFromEnum(Feature.zicond));

    // Strictly disable extensions not supported by mini-rv64.h
    disabled_features.addFeature(@intFromEnum(Feature.c));
    disabled_features.addFeature(@intFromEnum(Feature.a));
    disabled_features.addFeature(@intFromEnum(Feature.f));
    disabled_features.addFeature(@intFromEnum(Feature.d));

    const target = b.resolveTargetQuery(.{
        .cpu_arch = .riscv64,
        .os_tag = .freestanding,
        .abi = .none,
        .cpu_model = .{ .explicit = &std.Target.riscv.cpu.generic_rv64 },
        .cpu_features_add = enabled_features,
        .cpu_features_sub = disabled_features,
    });

    // Create the root module (Zig 0.14+ API)
    const root_mod = b.createModule(.{
        .target = target,
        .optimize = optimize,
        .strip = false,
		.code_model = .medium,
    });

    root_mod.addAssemblyFile(b.path("start.S"));
    root_mod.addCSourceFiles(.{
        .files = &.{"main.c"},
        .flags = &.{ "-Wall", "-Wextra", "-O3", "-ffreestanding", "-nostdlib", "-mcmodel=medany", },
    });

    // Build the ELF executable
    const elf = b.addExecutable(.{
        .name = "cartridge.elf",
        .root_module = root_mod,
    });

    elf.setLinkerScript(b.path("linker.ld"));
    elf.entry = .{ .symbol_name = "_start" };

    // Save the .elf (useful for `zig objdump -d` debugging)
    b.installArtifact(elf);

    // Convert ELF -> Flat Binary (.rom) for DemoCore::loadRomFile()
    const objcopy = b.addObjCopy(elf.getEmittedBin(), .{
        .format = .bin,
    });

    const install_rom = b.addInstallBinFile(objcopy.getOutput(), "cartridge.rom");
    b.getInstallStep().dependOn(&install_rom.step);
}