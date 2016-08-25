#include "HlslTranslator2.h"
#include "../SPIRV-Cross/spirv_hlsl.hpp"
#include <fstream>

using namespace krafix;

void HlslTranslator2::outputCode(const Target& target, const char* sourcefilename, const char* filename, std::map<std::string, int>& attributes) {
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

	spirv_cross::CompilerHLSL* compiler = new spirv_cross::CompilerHLSL(spirv);

	compiler->set_entry_point("main");
	spirv_cross::CompilerHLSL::Options opts = compiler->get_options();
	if (target.version > 9) {
		opts.shader_model = 40;
	}
	else {
		opts.shader_model = 30;
	}
	compiler->set_options(opts);

	std::string hlsl = compiler->compile();
	std::ofstream out;
	out.open(filename, std::ios::binary | std::ios::out);
	out << hlsl;
	out.close();
}
