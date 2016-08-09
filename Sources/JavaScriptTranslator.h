#pragma once

#include "CStyleTranslator.h"
#include "../sourcemap.cpp/src/document.hpp"

namespace krafix {
	class JavaScriptTranslator : public CStyleTranslator {
	public:
		JavaScriptTranslator(std::vector<unsigned>& spirv, ShaderStage stage) : CStyleTranslator(spirv, stage) {
			sourcemap = SourceMap::make_shared<SourceMap::SrcMapDoc>();
		}
		void outputCode(const Target& target, const char* sourcefilename, const char* filename, std::map<std::string, int>& attributes);
		void outputInstruction(const Target& target, std::map<std::string, int>& attributes, Instruction& inst);
		void outputLibraryInstruction(const Target& target, std::map<std::string, int>& attributes, Instruction& inst, GLSLstd450 entrypoint);
	private:
		SourceMap::SrcMapDocSP sourcemap;
		int outputLine;
		int originalLine;
		char name[256];
	};
}
