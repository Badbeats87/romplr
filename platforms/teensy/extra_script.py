"""
PlatformIO build script for Teensy Rompler.

Adds the shared synth engine sources to the build and
configures include paths so sample_bank.c finds our
FatFs shim instead of Circle's <fatfs/ff.h>.
"""
Import("env")
import os

project_dir = env.get("PROJECT_DIR")
synth_dir = os.path.join(project_dir, "..", "..", "synth", "baremetal")
synth_inc = os.path.join(project_dir, "..", "..", "synth", "include")
src_dir = os.path.join(project_dir, "src")

# Include paths:
#   src/           -> for fatfs/ff.h shim (angle-bracket include)
#   synth/baremetal -> for synth headers (rompler.h, etc.)
#   synth/include  -> for types.h (relative includes from synth .c files)
env.Append(CPPPATH=[src_dir, synth_dir, synth_inc])

# Compile all .c files from the synth engine
env.BuildSources(
    os.path.join("$BUILD_DIR", "synth_engine"),
    synth_dir,
)
