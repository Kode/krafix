#include "MetalTranslator.h"
#include <fstream>

using namespace krafix;

void MetalTranslator::outputCode(const char* baseName) {
	std::ofstream out("test.metal");
	out << "#include <metal_stdlib>\n";
	out << "using namespace metal;\n";
	out << "\n";
	out << "float4 gl_FragColor;\n";
	out << "\n";

	out << "\n";
	out << "fragment float4 render_pixel() {\n";
	out << "kore();\n";
	out << "return gl_FragColor;\n";
	out << "}\n";
}
