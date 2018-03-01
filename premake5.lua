include("tools/build")
require("third_party/premake-export-compile-commands/export-compile-commands")

location(build_root)
targetdir(build_bin)
objdir(build_obj)

-- Define an ARCH variable
-- Only use this to enable architecture-specific functionality.
if os.is("linux") then
  ARCH = os.outputof("uname -p")
else
  ARCH = "unknown"
end

includedirs({
  ".",
  "src",
  "third_party",
})

defines({
  "_UNICODE",
  "UNICODE",

  -- TODO(benvanik): find a better place for this stuff.
  "GLEW_NO_GLU=1",
})

-- TODO(DrChat): Find a way to disable this on other architectures.
filter("architecture:x86_64")
  vectorextensions("AVX")
filter({})

characterset("Unicode")
flags({
  --"ExtraWarnings",        -- Sets the compiler's maximum warning level.
  "FatalWarnings",        -- Treat warnings as errors.
})

filter("kind:StaticLib")
  defines({
    "_LIB",
  })

filter("configurations:Checked")
  runtime("Debug")
  defines({
    "DEBUG",
  })
  runtime("Debug")
filter({"configurations:Checked", "platforms:Windows"})
  buildoptions({
    "/RTCsu",   -- Full Run-Time Checks.
  })

filter("configurations:Debug")
  runtime("Debug")
  defines({
    "DEBUG",
    "_NO_DEBUG_HEAP=1",
  })
  runtime("Release")
filter({"configurations:Debug", "platforms:Windows"})
  linkoptions({
    "/NODEFAULTLIB:MSVCRTD",
  })

filter({"configurations:Debug", "platforms:Linux"})
  buildoptions({
    "-g"
  })

filter("configurations:Release")
  runtime("Release")
  defines({
    "NDEBUG",
    "_NO_DEBUG_HEAP=1",
  })
  optimize("On")
  flags({
    "LinkTimeOptimization",
  })
  runtime("Release")
filter({"configurations:Release", "platforms:Windows"})
  linkoptions({
    "/NODEFAULTLIB:MSVCRTD",
  })

filter("platforms:Linux")
  system("linux")
  toolset("clang")
  buildoptions({
    -- "-mlzcnt",  -- (don't) Assume lzcnt is supported.
    "`pkg-config --cflags gtk+-x11-3.0`",
    "-fno-lto", -- Premake doesn't support LTO on clang
  })
  links({
    "pthread",
    "dl",
    "lz4",
    "rt",
    "X11",
    "xcb",
    "X11-xcb",
    "GL",
    "vulkan",
    "c++",
    "c++abi"
  })
  linkoptions({
    "`pkg-config --libs gtk+-3.0`",
  })

filter({"platforms:Linux", "kind:*App"})
  linkgroups("On")

filter({"platforms:Linux", "language:C++", "toolset:gcc"})
  buildoptions({
    "--std=c++11",
  })
  links({
  })

filter({"platforms:Linux", "language:C++", "toolset:clang"})
  buildoptions({
    "-std=c++14",
    "-stdlib=libstdc++",
  })
  links({
  })
  disablewarnings({
    "deprecated-register"
  })

filter("platforms:Windows")
  system("windows")
  toolset("msc")
  buildoptions({
    "/MP",      -- Multiprocessor compilation.
    "/wd4100",  -- Unreferenced parameters are ok.
    "/wd4201",  -- Nameless struct/unions are ok.
    "/wd4512",  -- 'assignment operator was implicitly defined as deleted'.
    "/wd4127",  -- 'conditional expression is constant'.
    "/wd4324",  -- 'structure was padded due to alignment specifier'.
    "/wd4189",  -- 'local variable is initialized but not referenced'.
    "/utf-8",   -- 'build correctly on systems with non-Latin codepages'.
  })
  flags({
    "NoMinimalRebuild", -- Required for /MP above.
  })

  symbols("On")
  defines({
    "_CRT_NONSTDC_NO_DEPRECATE",
    "_CRT_SECURE_NO_WARNINGS",
    "WIN32",
    "_WIN64=1",
    "_AMD64=1",
  })
  linkoptions({
    "/ignore:4006",  -- Ignores complaints about empty obj files.
    "/ignore:4221",
  })
  links({
    "ntdll",
    "wsock32",
    "ws2_32",
    "xinput",
    "xaudio2",
    "glu32",
    "opengl32",
    "comctl32",
    "shcore",
    "shlwapi",
  })

-- Create scratch/ path and dummy flags file if needed.
if not os.isdir("scratch") then
  os.mkdir("scratch")
  local flags_file = io.open("scratch/flags.txt", "w")
  flags_file:write("# Put flags, one on each line.\n")
  flags_file:write("# Launch executables with --flags_file=scratch/flags.txt\n")
  flags_file:write("\n")
  flags_file:write("--cpu=x64\n")
  flags_file:write("#--enable_haswell_instructions=false\n")
  flags_file:write("\n")
  flags_file:write("--debug\n")
  flags_file:write("#--protect_zero=false\n")
  flags_file:write("\n")
  flags_file:write("#--mute\n")
  flags_file:write("\n")
  flags_file:write("--fast_stdout\n")
  flags_file:write("#--flush_stdout=false\n")
  flags_file:write("\n")
  flags_file:write("#--vsync=false\n")
  flags_file:write("#--gl_debug\n")
  flags_file:write("#--gl_debug_output\n")
  flags_file:write("#--gl_debug_output_synchronous\n")
  flags_file:write("#--trace_gpu_prefix=scratch/gpu/gpu_trace_\n")
  flags_file:write("#--trace_gpu_stream\n")
  flags_file:write("#--disable_framebuffer_readback\n")
  flags_file:write("\n")
  flags_file:close()
end

solution("xenia")
  uuid("931ef4b0-6170-4f7a-aaf2-0fece7632747")
  startproject("xenia-app")
  architecture("x86_64")
  if os.is("linux") then
    platforms({"Linux"})
  elseif os.is("windows") then
    platforms({"Windows"})
  end
  configurations({"Checked", "Debug", "Release"})

  -- Include third party files first so they don't have to deal with gflags.
  include("third_party/capstone.lua")
  include("third_party/gflags.lua")
  include("third_party/glew.lua")
  include("third_party/glslang-spirv.lua")
  include("third_party/imgui.lua")
  include("third_party/libav.lua")
  include("third_party/snappy.lua")
  include("third_party/spirv-tools.lua")
  include("third_party/vulkan/loader")
  include("third_party/xxhash.lua")
  include("third_party/yaml-cpp.lua")

  include("src/xenia")
  include("src/xenia/app")
  include("src/xenia/apu")
  include("src/xenia/apu/nop")
  include("src/xenia/base")
  include("src/xenia/cpu")
  include("src/xenia/cpu/backend/x64")
  include("src/xenia/debug/ui")
  include("src/xenia/gpu")
  include("src/xenia/gpu/null")
  include("src/xenia/gpu/gl4")
  include("src/xenia/gpu/vulkan")
  include("src/xenia/hid")
  include("src/xenia/hid/nop")
  include("src/xenia/kernel")
  include("src/xenia/ui")
  include("src/xenia/ui/gl")
  include("src/xenia/ui/spirv")
  include("src/xenia/ui/vulkan")
  include("src/xenia/vfs")

  if os.is("windows") then
    include("src/xenia/apu/xaudio2")
    include("src/xenia/hid/winkey")
    include("src/xenia/hid/xinput")
  end
