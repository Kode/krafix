#pragma once

#include "Translator.h"

namespace krafix {
	class GlslTranslator2 : public Translator {
	public:
		GlslTranslator2(std::vector<unsigned>& spirv, ShaderStage stage, bool relax) : Translator(spirv, stage), relax(relax) {}
		void outputCode(const Target& target, const char* sourcefilename, const char* filename, std::map<std::string, int>& attributes);
	private:
		bool relax;
	};
}
