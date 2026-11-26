from pathlib import Path
import json
from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.cmake import CMake, CMakeToolchain
from conan.tools.files import copy
from conan.tools.scm import Version

class CxwlDepthSdk(ConanFile):
    name = "cxwl-depth-sdk"
    version = "3.0.0"
    package_type = "shared-library"
    license = "LicenseRef-CXWL-Product"
    settings = "os", "arch", "compiler", "build_type"
    options = {"runtime_profile": ["MINIMAL", "STANDARD", "FULL"]}
    default_options = {"runtime_profile": "STANDARD"}
    def validate(self):
        if str(self.settings.os) != "Windows" or str(self.settings.arch) != "x86_64":
            raise ConanInvalidConfiguration("Windows x64 required")
        if str(self.settings.compiler) != "msvc" or Version(str(self.settings.compiler.version)) < "193":
            raise ConanInvalidConfiguration("MSVC v143 or later required")
        if (str(self.settings.build_type) != "Release" or str(self.settings.compiler.runtime) != "dynamic"
                or str(self.settings.compiler.runtime_type) != "Release"):
            raise ConanInvalidConfiguration("Prebuilt CXWL SDK requires Release with the dynamic Release CRT (/MD)")
    def export_sources(self):
        package = Path(self.recipe_folder).parents[1]
        metadata = json.loads((package/"sdk-manifest.json").read_text(encoding="utf-8"))
        if metadata["sdk_version"] != self.version:
            raise ConanInvalidConfiguration("Recipe and binary SDK versions differ")
        copy(self, "*", src=str(package/"SDK"), dst=str(Path(self.export_sources_folder)/"sdk"))
    def package_id(self):
        self.info.settings.rm_safe("compiler.cppstd")
    def generate(self):
        tc = CMakeToolchain(self, generator="Visual Studio 17 2022")
        tc.variables["CXWL_RUNTIME_PROFILE"] = str(self.options.runtime_profile)
        tc.generate()
    def build(self):
        CMake(self).configure(build_script_folder="sdk")
    def package(self):
        CMake(self).install()
    def package_info(self):
        self.cpp_info.libs = ["CxwlDepthSdk"]
        self.cpp_info.builddirs = ["share/CxwlDepthSdk"]
        self.cpp_info.set_property("cmake_find_mode", "none")
        # CMake consumers use the installed SDK config in builddirs. Other generators use libs and flags.
        self.cpp_info.cflags = ["/utf-8"]
        self.cpp_info.cxxflags = ["/utf-8"]
        self.runenv_info.prepend_path("PATH", str(Path(self.package_folder)/"bin"))
