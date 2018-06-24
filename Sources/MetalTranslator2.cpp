#include "MetalTranslator2.h"
#include "../SPIRV-Cross/spirv_msl.hpp"
#include <fstream>

using namespace krafix;

namespace {
	std::string extractFilename(std::string path) {
		int i = (int)path.size() - 1;
		for (; i > 0; --i) {
			if (path[i] == '/' || path[i] == '\\') {
				++i;
				break;
			}
		}
		return path.substr(i, std::string::npos);
	}
	
	std::string replace(std::string str, char c1, char c2) {
		std::string ret = str;
		for (unsigned i = 0; i < str.length(); ++i) {
			if (str[i] == c1) ret[i] = c2;
		}
		return ret;
	}
}

void MetalTranslator2::outputCode(const Target& target, const char* sourcefilename, const char* filename, char* output, std::map<std::string, int>& attributes) {
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

	spirv_cross::CompilerMSL* compiler = new spirv_cross::CompilerMSL(spirv);
	
	std::string name = extractFilename(sourcefilename);
	name = name.substr(0, name.find_last_of("."));
	name = replace(name, '-', '_');
	name = replace(name, '.', '_');

	compiler->set_entry_point("main");
	compiler->rename_entry_point("main", name + "_main", stage == StageVertex ? spv::ExecutionModelVertex : spv::ExecutionModelFragment);

	{
		spirv_cross::CompilerGLSL::Options opts = compiler->CompilerGLSL::get_options();
		opts.version = target.version;
		opts.es = target.es;
		opts.force_temporary = false;
		opts.vulkan_semantics = false;
		opts.vertex.fixup_clipspace = true;
		compiler->CompilerGLSL::set_options(opts);
	}

	{
		spirv_cross::CompilerMSL::Options opts = compiler->get_msl_options();
		opts.platform = target.system == iOS ? spirv_cross::CompilerMSL::Options::iOS : spirv_cross::CompilerMSL::Options::macOS;
		compiler->set_msl_options(opts);
	}

	std::vector<spirv_cross::MSLResourceBinding> p_res_bindings;
	spirv_cross::MSLResourceBinding mslBinding;
	mslBinding.stage = stage == StageVertex ? spv::ExecutionModelVertex : spv::ExecutionModelFragment;
	mslBinding.msl_buffer = 1;
	p_res_bindings.push_back(mslBinding);
	
	std::string metal = compiler->compile(nullptr, &p_res_bindings);
	if (output) {
		strcpy(output, metal.c_str());
	}
	else {
		std::ofstream out;
		out.open(filename, std::ios::binary | std::ios::out);
		out << metal;
		out.close();
	}
}
