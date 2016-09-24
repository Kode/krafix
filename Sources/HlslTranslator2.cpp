#include "HlslTranslator2.h"
#include "../SPIRV-Cross/spirv_hlsl.hpp"
#include <fstream>
#include <algorithm>

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

	spirv_cross::CompilerGLSL::Options glslOpts = compiler->CompilerGLSL::get_options();
	glslOpts.vertex.fixup_clipspace = false;
	compiler->CompilerGLSL::set_options(glslOpts);
	
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

	if (stage == StageVertex) {
		std::vector<std::string> inputs;
		auto variables = compiler->get_active_interface_variables();
		for (auto var : variables) {
			if (compiler->get_storage_class(var) == spv::StorageClassInput) {
				if (compiler->get_type_from_variable(var).vecsize == 4 && compiler->get_type_from_variable(var).columns == 4) {
					inputs.push_back(compiler->get_name(var) + "_0");
					inputs.push_back(compiler->get_name(var) + "_1");
					inputs.push_back(compiler->get_name(var) + "_2");
					inputs.push_back(compiler->get_name(var) + "_3");
				}
				else {
					inputs.push_back(compiler->get_name(var));
				}
			}
		}
		std::sort(inputs.begin(), inputs.end());
		for (unsigned i = 0; i < inputs.size(); ++i) {
			attributes[inputs[i]] = i;
		}
	}
}
