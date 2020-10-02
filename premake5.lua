include("tools/build")
require("third_party/premake-export-compile-commands/export-compile-commands")
require("third_party/premake-cmake/cmake")
-- gmake required for androidmk.
require("gmake")
require("third_party/premake-androidmk/androidmk")

location(build_root)
targetdir(build_bin)
objdir(build_obj)

-- Define an ARCH variable
-- Only use this to enable architecture-specific functionality.
if os.istarget("linux") then
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
})

cppdialect("C++17")
exceptionhandling("On")
rtti("On")
symbols("On")

-- TODO(DrChat): Find a way to disable this on other architectures.
if ARCH ~= "ppc64" then
  filter("architecture:x86_64")
    vectorextensions("AVX")
  filter({})
end

characterset("Unicode")
flags({
  "FatalWarnings",        -- Treat warnings as errors.
})

filter("kind:StaticLib")
  defines({
    "_LIB",
  })

filter("configurations:Checked")
  runtime("Debug")
  optimize("Off")
  defines({
    "DEBUG",
  })
filter({"configurations:Checked", "platforms:Windows"})
  buildoptions({
    "/RTCsu",           -- Full Run-Time Checks.
  })
filter({"configurations:Checked", "platforms:Linux"})
  defines({
    "_GLIBCXX_DEBUG",   -- libstdc++ debug mode
  })

filter("configurations:Debug")
  runtime("Release")
  optimize("Off")
  defines({
    "DEBUG",
    "_NO_DEBUG_HEAP=1",
  })
filter({"configurations:Debug", "platforms:Linux"})
  defines({
    "_GLIBCXX_DEBUG",   -- make dbg symbols work on some distros
  })

filter("configurations:Release")
  runtime("Release")
  defines({
    "NDEBUG",
    "_NO_DEBUG_HEAP=1",
  })
  optimize("Speed")
  inlining("Auto")
  flags({
    "LinkTimeOptimization",
  })
  -- Not using floatingpoint("Fast") - NaN checks are used in some places
  -- (though rarely), overall preferable to avoid any functional differences
  -- between debug and release builds, and to have calculations involved in GPU
  -- (especially anything that may affect vertex position invariance) and CPU
  -- (such as constant propagation) emulation as predictable as possible,
  -- including handling of specials since games make assumptions about them.
filter("platforms:Linux")
  system("linux")
  toolset("clang")
  buildoptions({
    -- "-mlzcnt",  -- (don't) Assume lzcnt is supported.
  })
  pkg_config.all("gtk+-x11-3.0")
  links({
    "stdc++fs",
    "dl",
    "lz4",
    "pthread",
    "rt",
  })

filter({"platforms:Linux", "kind:*App"})
  linkgroups("On")

filter({"platforms:Linux", "language:C++", "toolset:gcc"})
  disablewarnings({
    "unused-result"
  })

filter({"platforms:Linux", "toolset:gcc"})
  if ARCH == "ppc64" then
    buildoptions({
      "-m32",
      "-mpowerpc64"
    })
    linkoptions({
      "-m32",
      "-mpowerpc64"
    })
  end

filter({"platforms:Linux", "language:C++", "toolset:clang"})
  disablewarnings({
    "deprecated-register"
  })
filter({"platforms:Linux", "language:C++", "toolset:clang", "files:*.cc or *.cpp"})
  buildoptions({
    "-stdlib=libstdc++",
  })

filter("platforms:Android")
  system("android")
  links({
    "android",
    "dl",
  })

filter("platforms:Windows")
  system("windows")
  toolset("msc")
  buildoptions({
    "/utf-8",   -- 'build correctly on systems with non-Latin codepages'.
    -- Mark warnings as severe
    "/w14839",  -- non-standard use of class 'type' as an argument to a variadic function
    "/w14840",  -- non-portable use of class 'type' as an argument to a variadic function
    -- Disable warnings
    "/wd4100",  -- Unreferenced parameters are ok.
    "/wd4201",  -- Nameless struct/unions are ok.
    "/wd4512",  -- 'assignment operator was implicitly defined as deleted'.
    "/wd4127",  -- 'conditional expression is constant'.
    "/wd4324",  -- 'structure was padded due to alignment specifier'.
    "/wd4189",  -- 'local variable is initialized but not referenced'.
  })
  flags({
    "MultiProcessorCompile",  -- Multiprocessor compilation.
    "NoMinimalRebuild",       -- Required for /MP above.
  })

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
    "comctl32",
    "shcore",
    "shlwapi",
    "dxguid",
    "bcrypt",
  })

-- Create scratch/ path
if not os.isdir("scratch") then
  os.mkdir("scratch")
end

workspace("xenia")
  uuid("931ef4b0-6170-4f7a-aaf2-0fece7632747")
  startproject("xenia-app")
  if os.istarget("android") then
    -- Not setting architecture as that's handled by ndk-build itself.
    platforms({"Android"})
    ndkstl("c++_static")
  else
    architecture("x86_64")
    if os.istarget("linux") then
      platforms({"Linux"})
    elseif os.istarget("windows") then
      platforms({"Windows"})
      -- 10.0.15063.0: ID3D12GraphicsCommandList1::SetSamplePositions.
      -- 10.0.19041.0: D3D12_HEAP_FLAG_CREATE_NOT_ZEROED.
      filter("action:vs2017")
        systemversion("10.0.19041.0")
      filter("action:vs2019")
        systemversion("10.0")
      filter({})
    end
  end
  configurations({"Checked", "Debug", "Release"})

  include("third_party/aes_128.lua")
  include("third_party/capstone.lua")
  include("third_party/dxbc.lua")
  include("third_party/discord-rpc.lua")
  include("third_party/cxxopts.lua")
  include("third_party/cpptoml.lua")
  include("third_party/FFmpeg/premake5.lua")
  include("third_party/fmt.lua")
  include("third_party/glslang-spirv.lua")
  include("third_party/imgui.lua")
  include("third_party/mspack.lua")
  include("third_party/snappy.lua")
  include("third_party/spirv-tools.lua")
  include("third_party/volk.lua")
  include("third_party/xxhash.lua")

  if not os.istarget("android") then
    -- SDL2 requires sdl2-config, and as of November 2020 isn't high-quality on
    -- Android yet, most importantly in game controllers - the keycode and axis
    -- enums are being ruined during conversion to SDL2 enums resulting in only
    -- one controller (Nvidia Shield) being supported, digital triggers are also
    -- not supported; lifecycle management (especially surface loss) is also
    -- complicated.
    include("third_party/SDL2.lua")
  end

  -- Disable treating warnings as fatal errors for all third party projects:
  for _, prj in ipairs(premake.api.scope.current.solution.projects) do
    project(prj.name)
    removeflags({
      "FatalWarnings",
    })
  end

  include("src/xenia")
  include("src/xenia/app/discord")
  include("src/xenia/apu")
  include("src/xenia/apu/nop")
  include("src/xenia/base")
  include("src/xenia/cpu")
  include("src/xenia/cpu/backend/x64")
  include("src/xenia/debug/ui")
  include("src/xenia/gpu")
  include("src/xenia/gpu/null")
  include("src/xenia/gpu/vulkan")
  include("src/xenia/hid")
  include("src/xenia/hid/nop")
  include("src/xenia/kernel")
  include("src/xenia/patcher")
  include("src/xenia/ui")
  include("src/xenia/ui/spirv")
  include("src/xenia/ui/vulkan")
  include("src/xenia/vfs")

  if not os.istarget("android") then
    include("src/xenia/apu/sdl")
    include("src/xenia/helper/sdl")
    include("src/xenia/hid/sdl")

    -- TODO(Triang3l): src/xenia/app has a dependency on xenia-helper-sdl, bring
    -- it back later.
    include("src/xenia/app")
  end

  if os.istarget("windows") then
    include("src/xenia/apu/xaudio2")
    include("src/xenia/gpu/d3d12")
    include("src/xenia/hid/winkey")
    include("src/xenia/hid/xinput")
    include("src/xenia/ui/d3d12")
  end
