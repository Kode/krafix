#include "SpirVTranslator.h"
#include <algorithm>
#include <fstream>
#include <map>
#include <string.h>
#include <sstream>

using namespace krafix;

namespace {
	enum SpirVState {
		SpirVStart,
		SpirVDebugInformation,
		SpirVAnnotations,
		SpirVTypes,
		SpirVFunctions
	};

	void writeInstruction(std::ofstream& out, unsigned word) {
		out.put(word & 0xff);
		out.put((word >> 8) & 0xff);
		out.put((word >> 16) & 0xff);
		out.put((word >> 24) & 0xff);
	}

	bool isDebugInformation(Instruction& instruction) {
		return instruction.opcode == spv::OpSource || instruction.opcode == spv::OpSourceExtension
			|| instruction.opcode == spv::OpName || instruction.opcode == spv::OpMemberName;
	}

	bool isAnnotation(Instruction& instruction) {
		return instruction.opcode == spv::OpDecorate || instruction.opcode == spv::OpMemberDecorate;
	}

	struct Var {
		std::string name;
		unsigned id;
		unsigned type;
		unsigned pointertype;
	};

	bool varcompare(const Var& a, const Var& b) {
		return strcmp(a.name.c_str(), b.name.c_str()) < 0;
	}
}

void SpirVTranslator::writeInstructions(const char* filename, std::vector<Instruction>& instructions) {
	std::ofstream out;
	out.open(filename, std::ios::binary | std::ios::out);

	writeInstruction(out, magicNumber);
	writeInstruction(out, version);
	writeInstruction(out, generator);
	writeInstruction(out, bound);
	writeInstruction(out, schema);

	for (unsigned i = 0; i < instructions.size(); ++i) {
		Instruction& inst = instructions[i];
		writeInstruction(out, ((inst.length + 1) << 16) | (unsigned)inst.opcode);
		for (unsigned i2 = 0; i2 < inst.length; ++i2) {
			writeInstruction(out, inst.operands[i2]);
		}
	}

	out.close();
}

void SpirVTranslator::outputCode(const Target& target, const char* filename, std::map<std::string, int>& attributes) {
	using namespace spv;

	std::map<unsigned, std::string> names;
	std::vector<Var> invars;
	std::vector<Var> outvars;
	std::vector<Var> images;
	std::vector<Var> uniforms;
	std::map<unsigned, bool> imageTypes;

	for (unsigned i = 0; i < instructions.size(); ++i) {
		Instruction& inst = instructions[i];
		switch (inst.opcode) {
		case OpName: {
			unsigned id = inst.operands[0];
			if (strcmp(inst.string, "") != 0) {
				names[id] = inst.string;
			}
			break;
		}
		case OpDecorate: {
			unsigned id = inst.operands[0];
			Decoration decoration = (Decoration)inst.operands[1];
			if (decoration == DecorationBuiltIn) names[id] = "";
			break;
		}
		case OpTypeSampledImage: {
			unsigned id = inst.operands[0];
			imageTypes[id] = true;
			break;
		}
		case OpTypeImage: {
			unsigned id = inst.operands[0];
			imageTypes[id] = true;
			break;
		}
		case OpTypeSampler: {
			unsigned id = inst.operands[0];
			imageTypes[id] = true;
			break;
		}
		case OpTypePointer: {
			unsigned id = inst.operands[0];
			unsigned type = inst.operands[2];
			if (imageTypes[type]) imageTypes[id] = true;
			break;
		}
		case OpVariable: {
			unsigned type = inst.operands[0];
			unsigned id = inst.operands[1];
			StorageClass storage = (StorageClass)inst.operands[2];
			Var var;
			var.name = names[id];
			var.id = id;
			var.type = type;
			if (var.name != "") {
				if (storage == StorageClassInput) invars.push_back(var);
				if (storage == StorageClassOutput) outvars.push_back(var);
				if (storage == StorageClassUniformConstant) {
					if (imageTypes[type]) {
						images.push_back(var);
					}
					else {
						uniforms.push_back(var);
					}
				}
			}
			break;
		}
		}
	}

	std::sort(invars.begin(), invars.end(), varcompare);
	std::sort(outvars.begin(), outvars.end(), varcompare);
	std::sort(images.begin(), images.end(), varcompare);

	SpirVState state = SpirVStart;
	std::vector<Instruction> newinstructions;
	unsigned instructionsData[4096];
	unsigned instructionsDataIndex = 0;
	unsigned currentId = bound;
	unsigned structid;
	for (unsigned i = 0; i < instructions.size(); ++i) {
		Instruction& inst = instructions[i];
		
		switch (state) {
		case SpirVStart:
			if (isDebugInformation(inst)) {
				state = SpirVDebugInformation;
			}
			break;
		case SpirVDebugInformation:
			if (isAnnotation(inst)) {
				state = SpirVAnnotations;
			}
			break;
		case SpirVAnnotations:
			if (!isAnnotation(inst)) {
				unsigned location = 0;
				for (auto var : invars) {
					Instruction newinst(OpDecorate, &instructionsData[instructionsDataIndex], 3);
					instructionsData[instructionsDataIndex++] = var.id;
					instructionsData[instructionsDataIndex++] = DecorationLocation;
					instructionsData[instructionsDataIndex++] = location;
					newinstructions.push_back(newinst);
					++location;
				}
				location = 0;
				for (auto var : outvars) {
					Instruction newinst(OpDecorate, &instructionsData[instructionsDataIndex], 3);
					instructionsData[instructionsDataIndex++] = var.id;
					instructionsData[instructionsDataIndex++] = DecorationLocation;
					instructionsData[instructionsDataIndex++] = location;
					newinstructions.push_back(newinst);
					++location;
				}
				unsigned binding = 2;
				for (auto var : images) {
					Instruction newinst(OpDecorate, &instructionsData[instructionsDataIndex], 3);
					instructionsData[instructionsDataIndex++] = var.id;
					instructionsData[instructionsDataIndex++] = DecorationBinding;
					instructionsData[instructionsDataIndex++] = binding;
					newinstructions.push_back(newinst);
					++binding;
				}
				state = SpirVTypes;
			}
			break;
		case SpirVTypes:
			if (inst.opcode == OpFunction) {
				Instruction typestruct(OpTypeStruct, &instructionsData[instructionsDataIndex], 1 + uniforms.size());
				unsigned structtype = instructionsData[instructionsDataIndex++] = currentId++;
				for (unsigned i = 0; i < uniforms.size(); ++i) {
					instructionsData[instructionsDataIndex++] = uniforms[i].type;
				}
				newinstructions.push_back(typestruct);
				Instruction typepointer(OpTypePointer, &instructionsData[instructionsDataIndex], 3);
				unsigned pointertype = instructionsData[instructionsDataIndex++] = currentId++;
				instructionsData[instructionsDataIndex++] = StorageClassUniform;
				instructionsData[instructionsDataIndex++] = structtype;
				newinstructions.push_back(typepointer);
				Instruction variable(OpVariable, &instructionsData[instructionsDataIndex], 3);
				instructionsData[instructionsDataIndex++] = pointertype;
				structid = instructionsData[instructionsDataIndex++] = currentId++;
				instructionsData[instructionsDataIndex++] = StorageClassUniform;
				newinstructions.push_back(variable);

				Instruction typeint(OpTypeInt, &instructionsData[instructionsDataIndex], 3);
				unsigned type = instructionsData[instructionsDataIndex++] = currentId++;
				instructionsData[instructionsDataIndex++] = 32;
				instructionsData[instructionsDataIndex++] = 0;
				newinstructions.push_back(typeint);
				for (unsigned i = 0; i < uniforms.size(); ++i) {
					Instruction constant(OpConstant, &instructionsData[instructionsDataIndex], 3);
					instructionsData[instructionsDataIndex++] = type;
					instructionsData[instructionsDataIndex++] = currentId++;
					instructionsData[instructionsDataIndex++] = i;
					newinstructions.push_back(constant);
					Instruction typepointer(OpTypePointer, &instructionsData[instructionsDataIndex], 3);
					uniforms[i].pointertype = instructionsData[instructionsDataIndex++] = currentId++;
					instructionsData[instructionsDataIndex++] = StorageClassUniform;
					instructionsData[instructionsDataIndex++] = uniforms[i].type;
					newinstructions.push_back(typepointer);
				}
				state = SpirVFunctions;
			}
			break;
		}
		
		if (inst.opcode == OpVariable) {
			unsigned type = inst.operands[0];
			unsigned id = inst.operands[1];
			StorageClass storage = (StorageClass)inst.operands[2];
			if (storage != StorageClassUniformConstant || imageTypes[id]) {
				newinstructions.push_back(inst);
			}
		}
		else if (inst.opcode == OpLoad) {
			unsigned type = inst.operands[0];
			unsigned id = inst.operands[1];
			unsigned pointer = inst.operands[2];
			Var uniform;
			unsigned index;
			bool found = false;
			for (unsigned i = 0; i < uniforms.size(); ++i) {
				if (uniforms[i].id == pointer) {
					uniform = uniforms[i];
					index = i;
					found = true;
					break;
				}
			}
			if (found) {
				Instruction access(OpAccessChain, &instructionsData[instructionsDataIndex], 4);
				instructionsData[instructionsDataIndex++] = uniform.pointertype;
				unsigned pointer = instructionsData[instructionsDataIndex++] = currentId++;
				instructionsData[instructionsDataIndex++] = structid;
				instructionsData[instructionsDataIndex++] = index;
				newinstructions.push_back(access);
				Instruction load(OpLoad, &instructionsData[instructionsDataIndex], 3);
				instructionsData[instructionsDataIndex++] = type;
				instructionsData[instructionsDataIndex++] = id;
				instructionsData[instructionsDataIndex++] = pointer;
				newinstructions.push_back(load);
			}
			else {
				newinstructions.push_back(inst);
			}
		}
		else {
			newinstructions.push_back(inst);
		}
	}
	
	bound = currentId + 1;
	writeInstructions(filename, newinstructions);
}
