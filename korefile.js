var solution = new Solution("krafix");
var project = new Project("krafix");

solution.setCmd();

// bison glslang.y --defines=glslang_tab.cpp.h -o glslang_tab.cpp

project.addExclude('.git/**');
project.addExclude('glslang/.git/**');
project.addExclude('build/**');

project.addFile('Sources/**');
project.addFile('glslang/glslang/GenericCodeGen/**');
project.addFile('glslang/glslang/MachineIndependent/**');
project.addFile('glslang/glslang/Include/**');
project.addFile('glslang/OGLCompilersDLL/**');
project.addFile('glslang/SPIRV/**');

project.addIncludeDir('glslang');
project.addIncludeDir('glslang/glslang');
project.addIncludeDir('glslang/glslang/MachineIndependent');
project.addIncludeDir('glslang/glslang/Include');
project.addIncludeDir('glslang/OGLCompilersDLL');

if (platform === Platform.Windows) {
	project.addFile('glslang/glslang/OSDependent/Windows/**');
	project.addIncludeDir('glslang/glslang/OSDependent/Windows');

	project.addIncludeDir("Libraries/DirectX/Include");
	project.addLibFor("Win32", "Libraries/DirectX/Lib/dxguid");
	project.addLibFor("Win32", "Libraries/DirectX/Lib/d3dx9");
	project.addLibFor("Win32", "d3d11");
	project.addLibFor("Win32", "d3dcompiler");
}
else {
	project.addFile('glslang/glslang/OSDependent/Linux/**');
	project.addIncludeDir('glslang/glslang/OSDependent/Linux');
}

solution.addProject(project);

return solution;
