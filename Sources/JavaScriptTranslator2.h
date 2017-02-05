#pragma once

#include "Translator.h"

namespace krafix {
	class JavaScriptTranslator2 : public Translator {
	public:
		JavaScriptTranslator2(std::vector<unsigned>& spirv, ShaderStage stage) : Translator(spirv, stage) {}
		void outputCode(const Target& target, const char* sourcefilename, const char* filename, std::map<std::string, int>& attributes);
	};
}
