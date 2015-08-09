#pragma once

#include <SPIRV/spirv.hpp>
#include "../glslang/glslang/Public/ShaderLang.h"
#include <map>
#include <string>
#include <vector>

namespace krafix {
	enum TargetLanguage {
		GLSL,
		HLSL,
		Metal,
		AGAL
	};

	enum TargetSystem {
		Windows,
		OSX,
		Linux,
		iOS,
		Android,
		HTML5,
		Flash,
		Unknown
	};

	struct Target {
		TargetLanguage lang;
		int version;
		bool es;
		bool kore;
		TargetSystem system;
	};

	class Instruction {
	public:
		Instruction(std::vector<unsigned>& spirv, unsigned& index);

		spv::Op opcode;
		unsigned* operands;
		unsigned length;
		const char* string;
	};

	class Translator {
	public:
		Translator(std::vector<unsigned>& spirv, EShLanguage stage);
		virtual ~Translator() {}
		virtual void outputCode(const Target& target, const char* filename, std::map<std::string, int>& attributes) = 0;
	protected:
		std::vector<Instruction> instructions;
		EShLanguage stage;
	};
}
