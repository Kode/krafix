#pragma once

#include "Translator.h"

#include <cstdint>

namespace krafix {
	class SpirVTranslator : public Translator {
	public:
		SpirVTranslator(std::vector<unsigned>& spirv, ShaderStage stage) : Translator(spirv, stage) {}
		void outputCode(const Target& target, const char* sourcefilename, const char* filename, char* output, std::map<std::string, int>& attributes) override;
		int outputLength;
	private:
		int writeInstructions(const char* filename, char* output, std::vector<Instruction>& instructions);
		int writeInstructions(std::vector<uint32_t>& output, std::vector<Instruction>& instructions);
	};
}
