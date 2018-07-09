#pragma once

#include <map>
#include <string>
#include <sstream>
#include <vector>

namespace krafix {
	enum TargetLanguage {
		SpirV,
		GLSL,
		HLSL,
		Metal,
		AGAL,
		VarList,
		JavaScript
	};

	enum ShaderStage {
		StageVertex,
		StageTessControl,
		StageTessEvaluation,
		StageGeometry,
		StageFragment,
		StageCompute
	};

	enum TargetSystem {
		Windows,
		WindowsApp,
		OSX,
		Linux,
		iOS,
		Android,
		HTML5,
		Flash,
		Unity,
		Unknown
	};

	struct Target {
		TargetLanguage lang;
		int version;
		bool es;
		TargetSystem system;

		std::string string() {
			switch (lang) {
			case SpirV:
				return "SPIR-V";
			case GLSL: {
				std::stringstream stream;
				stream << "GLSL ";
				if (es) stream << "ES ";
				stream << version;
				return stream.str();
			}
			case HLSL: {
				std::stringstream stream;
				stream << "HLSL ";
				stream << version;
				return stream.str();
			}
				
			case Metal:
				return "Metal";
			case AGAL:
				return "AGAL";
			case VarList:
				return "VarList";
			case JavaScript:
				return "JavaScript";
			}
			return "Unknown";
		}
	};

	class Instruction {
	public:
		Instruction(std::vector<unsigned>& spirv, unsigned& index);
		Instruction(int opcode, unsigned* operands, unsigned length);

		int opcode;
		unsigned* operands;
		unsigned length;
		const char* string;
	};

	class Translator {
	public:
		Translator(std::vector<unsigned>& spirv, ShaderStage stage);
		virtual ~Translator() {}
		virtual void outputCode(const Target& target, const char* sourcefilename, const char* filename, char* output, std::map<std::string, int>& attributes) = 0;

	protected:
		std::vector<unsigned>& spirv;
		std::vector<Instruction> instructions;
		ShaderStage stage;

		unsigned magicNumber;
		unsigned version;
		unsigned generator;
		unsigned bound;
		unsigned schema;
	};
}
