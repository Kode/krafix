#include "HlslTranslator2.h"
#include "../SPIRV-Cross/spirv_hlsl.hpp"
#include <fstream>
#include <algorithm>

using namespace krafix;

void HlslTranslator2::outputCode(const Target& target, const char* sourcefilename, const char* filename, char* output, std::map<std::string, int>& attributes) {
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

	compiler->set_entry_point("main", executionModel());

	spirv_cross::CompilerGLSL::Options glslOpts = compiler->CompilerGLSL::get_common_options();
	glslOpts.vertex.fixup_clipspace = true;
	compiler->CompilerGLSL::set_common_options(glslOpts);

	spirv_cross::CompilerHLSL::Options opts = compiler->get_hlsl_options();
	if (target.version > 9) {
		opts.shader_model = 40;
	}
	else {
		opts.shader_model = 30;
	}
	compiler->set_hlsl_options(opts);

	std::string hlsl = compiler->compile();
	if (output) {
		strcpy(output, hlsl.c_str());
	}
	else {
		std::ofstream out;
		out.open(filename, std::ios::binary | std::ios::out);
		out << hlsl;
		out.close();
	}

	if (stage == StageVertex) {
		std::vector<std::string> inputs;
		auto variables = compiler->get_shader_resources().stage_inputs;
		for (auto var : variables) {
			if (compiler->get_type_from_variable(var.id).vecsize == 4 && compiler->get_type_from_variable(var.id).columns == 4) {
				inputs.push_back(compiler->get_name(var.id) + "_0");
				inputs.push_back(compiler->get_name(var.id) + "_1");
				inputs.push_back(compiler->get_name(var.id) + "_2");
				inputs.push_back(compiler->get_name(var.id) + "_3");
			}
			else {
				inputs.push_back(compiler->get_name(var.id));
			}
		}
		std::sort(inputs.begin(), inputs.end());
		unsigned attributeIndex = 0;
		for (unsigned i = 0; i < inputs.size(); ++i) {
			if (inputs[i].substr(0, 3) == "gl_") {
				continue;
			}
			attributes[inputs[i]] = attributeIndex++;
		}
	}
}
