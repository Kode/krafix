#include "Translator.h"
#include <SPIRV/spirv.hpp>
#include "../glslang/glslang/Public/ShaderLang.h"

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

Instruction::Instruction(int opcode, unsigned* operands, unsigned length) : opcode(opcode), operands(operands), length(length), string(NULL) {

}

Translator::Translator(std::vector<unsigned>& spirv, ShaderStage stage) : stage(stage), spirv(spirv) {
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

spv::ExecutionModel Translator::executionModel() {
	switch (stage) {
	case StageVertex:
		return spv::ExecutionModelVertex;
	case StageTessControl:
		return spv::ExecutionModelTessellationControl;
	case StageTessEvaluation:
		return spv::ExecutionModelTessellationEvaluation;
	case StageGeometry:
		return spv::ExecutionModelGeometry;
	case StageFragment:
		return spv::ExecutionModelFragment;
	case StageCompute:
		return spv::ExecutionModelGLCompute;
	default:
		throw "Unknown shader stage";
	}
}
