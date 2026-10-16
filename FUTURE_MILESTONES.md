| # | Phase | Goal |
|---|---|---|
| **23** | **Architecture Review** | Review everything learned from Breakout, Tetris and Platformer. Identify actual machine requirements, bottlenecks, unnecessary features, and missing capabilities. |
| **24** | **ARCADE-1 v0.1 Candidate** | Write the first complete machine specification: CPU, memory, MMIO, timing, video, audio, input, ROM/program model and compatibility rules. |
| **25** | **SDK v0.1** | Build the real developer-facing ARCADE-1 SDK. Make game development pleasant without exposing VM/Libretro/FPGA internals. |
| **26** | **SDK → Machine Validation** | Build representative game functionality using only the SDK. Identify awkward APIs, excessive CPU work, inefficient memory/data paths, missing primitives, etc. |
| **27** | **Architecture Revision** | Change the machine architecture **only where SDK/game evidence justifies it**. Re-test against the existing games. |
| **28** | **ARCADE-1 v0.1 Freeze** | Freeze the machine contract and SDK baseline. From here, compatibility becomes a serious requirement. |
| **29** | **Asset Pipeline** | Define source assets → conversion → native ARCADE-1 resources → `.arc1`. Build asset tools and packaging. |
| **30** | **Graphics Expansion** | Add the graphics capabilities actually justified by the games: sprites, animation, palettes, blending, particles, layers, etc. |
| **31** | **2.5D / Fake 3D** | Explore perspective effects, sprite scaling, affine/scanline effects, parallax, isometric rendering and similar techniques without becoming a general 3D GPU. |
| **32** | **Developer Toolchain** | `arcade1 build`, `run`, `test`, `debug`, `capture`, `inspect`, profiling/replay, documentation, project templates. |
| **33** | **FPGA Implementation** | Implement the frozen ARCADE-1 machine in RTL. C VM remains the reference implementation. |
| **34** | **Conformance / Lockstep** | Same ROMs and test programs run against C VM and FPGA; establish machine compatibility tests. |
| **35** | **More Games** | Build increasingly ambitious games to validate the final machine: shmup, RPG, racing, tactics, pseudo-3D, etc. |
| **36** | **ARCADE-1 Ecosystem Review** | Evaluate the complete platform: machine, SDK, compiler, assets, FPGA, VM, tooling and games. Decide what ARCADE-1 actually is as a platform. |