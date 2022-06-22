let project = new Project('krafix');

project.useAsLibrary = () => {
	project.addDefine('KRAFIX_LIBRARY');
	project.cmd = false;
	project.debugDir = null;
	project.kore = true;
};

project.addDefine('SPIRV_CROSS_KRAFIX');
project.addDefine('ENABLE_HLSL');

// glslang defines to enable vendor-specific extensions
project.addDefine('NV_EXTENSIONS');
project.addDefine('AMD_EXTENSIONS');

project.setCmd();
project.setDebugDir('tests');
project.kore = false;

project.cpp11 = true;

project.addExclude('.git/**');
project.addExclude('glslang/.git/**');
project.addExclude('build/**');

project.addFile('Sources/**');

project.addFile('sourcemap.cpp/src/**.hpp');
project.addFile('sourcemap.cpp/deps/json/json.cpp');
project.addFile('sourcemap.cpp/deps/cencode/cencode.c');
project.addFile('sourcemap.cpp/deps/cencode/cdecode.c');
project.addFile('sourcemap.cpp/src/map_line.cpp');
project.addFile('sourcemap.cpp/src/map_col.cpp');
project.addFile('sourcemap.cpp/src/mappings.cpp');
project.addFile('sourcemap.cpp/src/pos_idx.cpp');
project.addFile('sourcemap.cpp/src/pos_txt.cpp');
project.addFile('sourcemap.cpp/src/format/v3.cpp');
project.addFile('sourcemap.cpp/src/document.cpp');

project.addFile('glslang/StandAlone/ResourceLimits.cpp');
project.addFile('glslang/glslang/GenericCodeGen/**');
project.addFile('glslang/glslang/MachineIndependent/**');
project.addFile('glslang/glslang/Include/**');
project.addFile('glslang/hlsl/**');
project.addFile('glslang/OGLCompilersDLL/**');
project.addFile('glslang/SPIRV/**');

project.addFiles('SPIRV-Cross/*.cpp', 'SPIRV-Cross/*.hpp', 'SPIRV-Cross/*.h');
project.addExclude('SPIRV-Cross/main.cpp');

project.addIncludeDir('glslang');
project.addIncludeDir('glslang/glslang');
project.addIncludeDir('glslang/glslang/MachineIndependent');
project.addIncludeDir('glslang/glslang/Include');
project.addIncludeDir('glslang/OGLCompilersDLL');

if (platform === Platform.Windows) {
	project.addFile('glslang/glslang/OSDependent/Windows/**');
	project.addIncludeDir('glslang/glslang/OSDependent/Windows');
	project.addLib('d3dcompiler');
}
else {
	project.addFile('glslang/glslang/OSDependent/Unix/**');
	project.addIncludeDir('glslang/glslang/OSDependent/Unix');
}

resolve(project);
