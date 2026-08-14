glslc.exe gltf.vert -o gltf.vert.spv
glslc.exe gltf.frag -o gltf.frag.spv
glslc.exe gltf_shadow.vert -o gltf_shadow.vert.spv

@REM glslangValidator -V triangle.vert -o triangle.vert.spv
@REM glslangValidator -V triangle.frag -o triangle.frag.spv
