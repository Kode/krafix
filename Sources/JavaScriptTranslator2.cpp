#include "JavaScriptTranslator2.h"
#ifdef SPIRV_JS
#include "../SPIRV-Cross/spirv_js.hpp"
#include <fstream>
#endif

using namespace krafix;

void JavaScriptTranslator2::outputCode(const Target& target, const char* sourcefilename, const char* filename, char* output, std::map<std::string, int>& attributes) {
	std::vector<unsigned> spirv;
	
	spirv.push_back(magicNumber);
	spirv.push_back(version);
	spirv.push_back(generator);
	spirv.push_back(bound);
	spirv.push_back(schema);

	for (unsigned i = 0; i < instructions.size(); ++i) {
		Instruction& inst = instructions[i];
		spirv.push_back(((inst.length + 1) << 16) | (unsigned)inst.opcode);
		for (unsigned i2 = 0; i2 < inst.length; ++i2) {
			spirv.push_back(inst.operands[i2]);
		}
	}

#ifdef SPIRV_JS
	spirv_cross::CompilerJS* compiler = new spirv_cross::CompilerJS(spirv);

	compiler->set_entry_point("main");
	spirv_cross::CompilerJS::Options opts = compiler->get_options();
	
	compiler->set_options(opts);

	std::string js = compiler->compile();
	std::ofstream out;
	out.open(filename, std::ios::binary | std::ios::out);
	out << js;
	out.close();
#endif
}
