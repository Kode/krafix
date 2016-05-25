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

	unsigned copyname(const std::string& name, unsigned* instructionsData, unsigned& instructionsDataIndex) {
		unsigned length = 0;
		bool zeroset = false;
		for (unsigned i2 = 0; i2 < name.size(); i2 += 4) {
			char* data = (char*)&instructionsData[instructionsDataIndex];
			for (unsigned i3 = 0; i3 < 4; ++i3) {
				if (i2 + i3 < name.size()) data[i3] = name[i2 + i3];
				else {
					data[i3] = 0;
					zeroset = true;
				}
			}
			++length;
			++instructionsDataIndex;
		}
		if (!zeroset) {
			instructionsData[instructionsDataIndex++] = 0;
			++length;
		}
		return length;
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

void SpirVTranslator::outputCode(const Target& target, const char* sourcefilename, const char* filename, std::map<std::string, int>& attributes) {
	using namespace spv;

	std::map<unsigned, std::string> names;
	std::vector<Var> invars;
	std::vector<Var> outvars;
	std::vector<Var> images;
	std::vector<Var> uniforms;
	std::map<unsigned, bool> imageTypes;
	std::map<unsigned, unsigned> pointers;
	std::map<unsigned, unsigned> constants;
	unsigned position;

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
			if (decoration == DecorationBuiltIn) {
				names[id] = "";
				if (inst.operands[2] == 0) position = id;
			}
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
			pointers[id] = type;
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
	std::vector<unsigned> structtypeindices;
	unsigned structvarindex;
	unsigned tempposition;
	unsigned floattype;
	unsigned floatpointertype;
	unsigned dotfive;
	unsigned two;
	unsigned three;
	unsigned vec4type;
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
				if (uniforms.size() > 0) {
					Instruction structtypename(OpName, &instructionsData[instructionsDataIndex], 0);
					structtypeindices.push_back(instructionsDataIndex);
					instructionsData[instructionsDataIndex++] = 0;
					structtypename.length = 1 + copyname("_k_global_uniform_buffer_type", instructionsData, instructionsDataIndex);
					newinstructions.push_back(structtypename);

					Instruction structname(OpName, &instructionsData[instructionsDataIndex], 0);
					structvarindex = instructionsDataIndex;
					instructionsData[instructionsDataIndex++] = 0;
					structname.length = 1 + copyname("_k_global_uniform_buffer", instructionsData, instructionsDataIndex);
					newinstructions.push_back(structname);

					for (unsigned i = 0; i < uniforms.size(); ++i) {
						Instruction name(OpMemberName, &instructionsData[instructionsDataIndex], 0);
						structtypeindices.push_back(instructionsDataIndex);
						instructionsData[instructionsDataIndex++] = 0;
						instructionsData[instructionsDataIndex++] = i;
						name.length = 2 + copyname(uniforms[i].name, instructionsData, instructionsDataIndex);
						newinstructions.push_back(name);
					}
				}
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
				unsigned offset = 0;
				for (unsigned i = 0; i < uniforms.size(); ++i) {
					Instruction newinst(OpMemberDecorate, &instructionsData[instructionsDataIndex], 4);
					structtypeindices.push_back(instructionsDataIndex);
					instructionsData[instructionsDataIndex++] = 0;
					instructionsData[instructionsDataIndex++] = i;
					instructionsData[instructionsDataIndex++] = DecorationOffset;
					instructionsData[instructionsDataIndex++] = offset;
					newinstructions.push_back(newinst);

					// TODO: Next two only for matrices
					Instruction dec2(OpMemberDecorate, &instructionsData[instructionsDataIndex], 3);
					structtypeindices.push_back(instructionsDataIndex);
					instructionsData[instructionsDataIndex++] = 0;
					instructionsData[instructionsDataIndex++] = i;
					instructionsData[instructionsDataIndex++] = DecorationColMajor;
					newinstructions.push_back(dec2);

					Instruction dec3(OpMemberDecorate, &instructionsData[instructionsDataIndex], 4);
					structtypeindices.push_back(instructionsDataIndex);
					instructionsData[instructionsDataIndex++] = 0;
					instructionsData[instructionsDataIndex++] = i;
					instructionsData[instructionsDataIndex++] = DecorationMatrixStride;
					instructionsData[instructionsDataIndex++] = 16;
					newinstructions.push_back(dec3);

					++offset; // TODO: Calculate proper offsets
				}
				if (uniforms.size() > 0) {
					Instruction dec1(OpDecorate, &instructionsData[instructionsDataIndex], 2);
					structtypeindices.push_back(instructionsDataIndex);
					instructionsData[instructionsDataIndex++] = 0;
					instructionsData[instructionsDataIndex++] = DecorationBlock;
					newinstructions.push_back(dec1);
				}
				state = SpirVTypes;
			}
			break;
		case SpirVTypes:
			if (inst.opcode == OpFunction) {
				if (uniforms.size() > 0) {
					Instruction typestruct(OpTypeStruct, &instructionsData[instructionsDataIndex], 1 + uniforms.size());
					unsigned structtype = instructionsData[instructionsDataIndex++] = currentId++;
					for (unsigned i = 0; i < uniforms.size(); ++i) {
						instructionsData[instructionsDataIndex++] = pointers[uniforms[i].type];
					}
					for (auto index : structtypeindices) instructionsData[index] = structtype;
					newinstructions.push_back(typestruct);
					Instruction typepointer(OpTypePointer, &instructionsData[instructionsDataIndex], 3);
					unsigned pointertype = instructionsData[instructionsDataIndex++] = currentId++;
					instructionsData[instructionsDataIndex++] = StorageClassUniform;
					instructionsData[instructionsDataIndex++] = structtype;
					newinstructions.push_back(typepointer);
					Instruction variable(OpVariable, &instructionsData[instructionsDataIndex], 3);
					instructionsData[instructionsDataIndex++] = pointertype;
					structid = instructionsData[instructionsDataIndex++] = currentId++;
					instructionsData[structvarindex] = structid;
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
						unsigned constantid = currentId++;
						instructionsData[instructionsDataIndex++] = constantid;
						constants[i] = constantid;
						instructionsData[instructionsDataIndex++] = i;
						newinstructions.push_back(constant);
						Instruction typepointer(OpTypePointer, &instructionsData[instructionsDataIndex], 3);
						uniforms[i].pointertype = instructionsData[instructionsDataIndex++] = currentId++;
						instructionsData[instructionsDataIndex++] = StorageClassUniform;
						instructionsData[instructionsDataIndex++] = pointers[uniforms[i].type];
						newinstructions.push_back(typepointer);
					}
				}

				if (stage == EShLangVertex) {
					Instruction floaty(OpTypeFloat, &instructionsData[instructionsDataIndex], 2);
					floattype = instructionsData[instructionsDataIndex++] = currentId++;
					instructionsData[instructionsDataIndex++] = 32;
					newinstructions.push_back(floaty);

					Instruction floatpointer(OpTypePointer, &instructionsData[instructionsDataIndex], 3);
					floatpointertype = instructionsData[instructionsDataIndex++] = currentId++;
					instructionsData[instructionsDataIndex++] = StorageClassPrivate;
					instructionsData[instructionsDataIndex++] = floattype;
					newinstructions.push_back(floatpointer);

					Instruction dotfiveconstant(OpConstant, &instructionsData[instructionsDataIndex], 3);
					instructionsData[instructionsDataIndex++] = floattype;
					dotfive = instructionsData[instructionsDataIndex++] = currentId++;
					*(float*)&instructionsData[instructionsDataIndex++] = 0.5f;
					newinstructions.push_back(dotfiveconstant);

					Instruction inty(OpTypeInt, &instructionsData[instructionsDataIndex], 3);
					unsigned inttype = instructionsData[instructionsDataIndex++] = currentId++;
					instructionsData[instructionsDataIndex++] = 32;
					instructionsData[instructionsDataIndex++] = 0;
					newinstructions.push_back(inty);

					Instruction twoconstant(OpConstant, &instructionsData[instructionsDataIndex], 3);
					instructionsData[instructionsDataIndex++] = inttype;
					two = instructionsData[instructionsDataIndex++] = currentId++;
					instructionsData[instructionsDataIndex++] = 2;
					newinstructions.push_back(twoconstant);

					Instruction threeconstant(OpConstant, &instructionsData[instructionsDataIndex], 3);
					instructionsData[instructionsDataIndex++] = inttype;
					three = instructionsData[instructionsDataIndex++] = currentId++;
					instructionsData[instructionsDataIndex++] = 3;
					newinstructions.push_back(threeconstant);
					
					Instruction vec4(OpTypeVector, &instructionsData[instructionsDataIndex], 3);
					vec4type = instructionsData[instructionsDataIndex++] = currentId++;
					instructionsData[instructionsDataIndex++] = floattype;
					instructionsData[instructionsDataIndex++] = 4;
					newinstructions.push_back(vec4);
					
					Instruction vec4pointer(OpTypePointer, &instructionsData[instructionsDataIndex], 3);
					unsigned vec4pointertype = instructionsData[instructionsDataIndex++] = currentId++;
					instructionsData[instructionsDataIndex++] = StorageClassPrivate;
					instructionsData[instructionsDataIndex++] = vec4type;
					newinstructions.push_back(vec4pointer);

					Instruction varinst(OpVariable, &instructionsData[instructionsDataIndex], 3);
					instructionsData[instructionsDataIndex++] = vec4pointertype;
					tempposition = instructionsData[instructionsDataIndex++] = currentId++;
					instructionsData[instructionsDataIndex++] = StorageClassPrivate;
					newinstructions.push_back(varinst);
				}

				state = SpirVFunctions;
			}
			break;
		}
		
		if (inst.opcode == OpEntryPoint) {
			unsigned executionModel = inst.operands[0];
			if (executionModel == 4) { // Fragment Shader
				unsigned i = 2;
				for (; ; ++i) {
					char* chars = (char*)&inst.operands[i];
					if (chars[0] == 0 || chars[1] == 0 || chars[2] == 0 || chars[3] == 0) break;
				}
				Instruction newinst(OpEntryPoint, &instructionsData[instructionsDataIndex], 0);
				unsigned length = 0;
				for (unsigned i2 = 0; i2 <= i; ++i2) {
					instructionsData[instructionsDataIndex++] = inst.operands[i2];
					++length;
				}
				for (auto var : invars) {
					instructionsData[instructionsDataIndex++] = var.id;
					++length;
				}
				for (auto var : outvars) {
					instructionsData[instructionsDataIndex++] = var.id;
					++length;
				}
				newinst.length = length;
				newinstructions.push_back(newinst);
			}
			else {
				newinstructions.push_back(inst);
			}
		}
		else if (inst.opcode == OpVariable) {
			unsigned type = inst.operands[0];
			unsigned id = inst.operands[1];
			StorageClass storage = (StorageClass)inst.operands[2];
			if (storage != StorageClassUniformConstant || imageTypes[type]) {
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
				instructionsData[instructionsDataIndex++] = constants[index];
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
		else if (inst.opcode == OpStore) {
			if (stage == EShLangVertex) {
				//gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;
				unsigned to = inst.operands[0];
				unsigned from = inst.operands[1];
				if (to == position) {
					//OpStore tempposition from
					Instruction store1(OpStore, &instructionsData[instructionsDataIndex], 2);
					instructionsData[instructionsDataIndex++] = tempposition;
					instructionsData[instructionsDataIndex++] = from;
					newinstructions.push_back(store1);

					//%27 = OpAccessChain floatpointer tempposition two
					Instruction access1(OpAccessChain, &instructionsData[instructionsDataIndex], 4);
					instructionsData[instructionsDataIndex++] = floatpointertype;
					unsigned _27 = instructionsData[instructionsDataIndex++] = currentId++;
					instructionsData[instructionsDataIndex++] = tempposition;
					instructionsData[instructionsDataIndex++] = two;
					newinstructions.push_back(access1);

					//%28 = OpLoad float %27
					Instruction load1(OpLoad, &instructionsData[instructionsDataIndex], 3);
					instructionsData[instructionsDataIndex++] = floattype;
					unsigned _28 = instructionsData[instructionsDataIndex++] = currentId++;
					instructionsData[instructionsDataIndex++] = _27;
					newinstructions.push_back(load1);

					//%30 = OpAccessChain floatpointer tempposition three
					Instruction access2(OpAccessChain, &instructionsData[instructionsDataIndex], 4);
					instructionsData[instructionsDataIndex++] = floatpointertype;
					unsigned _30 = instructionsData[instructionsDataIndex++] = currentId++;
					instructionsData[instructionsDataIndex++] = tempposition;
					instructionsData[instructionsDataIndex++] = three;
					newinstructions.push_back(access2);

					//%31 = OpLoad float %30
					Instruction load2(OpLoad, &instructionsData[instructionsDataIndex], 3);
					instructionsData[instructionsDataIndex++] = floattype;
					unsigned _31 = instructionsData[instructionsDataIndex++] = currentId++;
					instructionsData[instructionsDataIndex++] = _30;
					newinstructions.push_back(load2);

					//%32 = OpFAdd float %28 %31
					Instruction add(OpFAdd, &instructionsData[instructionsDataIndex], 4);
					instructionsData[instructionsDataIndex++] = floattype;
					unsigned _32 = instructionsData[instructionsDataIndex++] = currentId++;
					instructionsData[instructionsDataIndex++] = _28;
					instructionsData[instructionsDataIndex++] = _31;
					newinstructions.push_back(add);

					//%34 = OpFMul float %32 dotfive
					Instruction mult(OpFMul, &instructionsData[instructionsDataIndex], 4);
					instructionsData[instructionsDataIndex++] = floattype;
					unsigned _34 = instructionsData[instructionsDataIndex++] = currentId++;
					instructionsData[instructionsDataIndex++] = _32;
					instructionsData[instructionsDataIndex++] = dotfive;
					newinstructions.push_back(mult);

					//%35 = OpAccessChain floatpointer tempposition two
					Instruction access3(OpAccessChain, &instructionsData[instructionsDataIndex], 4);
					instructionsData[instructionsDataIndex++] = floatpointertype;
					unsigned _35 = instructionsData[instructionsDataIndex++] = currentId++;
					instructionsData[instructionsDataIndex++] = tempposition;
					instructionsData[instructionsDataIndex++] = two;
					newinstructions.push_back(access3);

					//OpStore %35 %34
					Instruction store2(OpStore, &instructionsData[instructionsDataIndex], 2);
					instructionsData[instructionsDataIndex++] = _35;
					instructionsData[instructionsDataIndex++] = _34;
					newinstructions.push_back(store2);

					//%38 = OpLoad vec4 tempposition
					Instruction load3(OpLoad, &instructionsData[instructionsDataIndex], 3);
					instructionsData[instructionsDataIndex++] = vec4type;
					unsigned _38 = instructionsData[instructionsDataIndex++] = currentId++;
					instructionsData[instructionsDataIndex++] = tempposition;
					newinstructions.push_back(load3);

					//OpStore position %38
					Instruction store3(OpStore, &instructionsData[instructionsDataIndex], 2);
					instructionsData[instructionsDataIndex++] = position;
					instructionsData[instructionsDataIndex++] = _38;
					newinstructions.push_back(store3);
				}
				else {
					newinstructions.push_back(inst);
				}
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
