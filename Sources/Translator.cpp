#include "Translator.h"

using namespace krafix;

Instruction::Instruction(std::vector<unsigned>& spirv, unsigned& index) {
	using namespace spv;

	int wordCount = spirv[index] >> 16;
	opcode = (Op)(spirv[index] & 0xffff);

	operands = wordCount > 1 ? &spirv[index + 1] : NULL;
	length = wordCount - 1;

	switch (opcode) {
	case OpString:
		string = (char*)&spirv[index + 2];
		break;
	case OpName:
		string = (char*)&spirv[index + 2];
		break;
	case OpMemberName:
		string = (char*)&spirv[index + 3];
		break;
	case OpEntryPoint:
		string = (char*)&spirv[index + 3];
		break;
	case OpSourceExtension:
		string = (char*)&spirv[index + 1];
		break;
	default:
		string = NULL;
		break;
	}

	index += wordCount;
}

Translator::Translator(std::vector<unsigned>& spirv, EShLanguage stage) : stage(stage), spirv(spirv) {
	if (spirv.size() < 5) { return; }

	unsigned index = 0;
	magicNumber = spirv[index++];
	version = spirv[index++];
	generator = spirv[index++];
	bound = spirv[index++];
	schema = index++;

	while (index < spirv.size()) {
		instructions.push_back(Instruction(spirv, index));
	}

	//printf("Read %i instructions.\n", instructions.size());
}
